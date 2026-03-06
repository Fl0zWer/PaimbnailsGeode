#include "HttpClient.hpp"
#include "Debug.hpp"
#include "WebHelper.hpp"
#include <Geode/Geode.hpp>
#include <Geode/utils/web.hpp>
#include <Geode/binding/GJAccountManager.hpp>
#include <ctime>
#include <map>
#include <chrono>
#include <algorithm>

using namespace geode::prelude;

HttpClient::HttpClient() {
    // config base del server
    m_serverURL = "https://paimon-thumbnails-server.paimonalcuadrado.workers.dev";
    
    // api key del servidor oficial de Paimbnails.
    // si quieres usar tu propio servidor, cambia esta key por la de tu instancia.
    m_apiKey = "074b91c9-6631-4670-a6f08a2ce970-0183-471b";

    // cargo el mod code que tenga guardado
    m_modCode = Mod::get()->getSavedValue<std::string>("mod-code", "");

    PaimonDebug::log("[HttpClient] Initialized with server: {}", m_serverURL);
}

void HttpClient::cleanTasks() {
    // en v5 esto ya no hace nada
}

void HttpClient::setServerURL(std::string const& url) {
    m_serverURL = url;
    if (!m_serverURL.empty() && m_serverURL.back() == '/') {
        m_serverURL.pop_back();
    }
    PaimonDebug::log("[HttpClient] Server URL updated to: {}", m_serverURL);
}

void HttpClient::setModCode(std::string const& code) {
    m_modCode = code;
    Mod::get()->setSavedValue("mod-code", code);
    PaimonDebug::log("[HttpClient] Mod code updated.");
}

void HttpClient::performRequest(
    std::string const& url,
    std::string const& method,
    std::string const& postData,
    std::vector<std::string> const& headers,
    geode::CopyableFunction<void(bool, std::string const&)> callback
) {
    auto req = web::WebRequest();

    // pongo headers
    for (auto const& header : headers) {
        size_t colonPos = header.find(':');
        if (colonPos != std::string::npos) {
            std::string key = header.substr(0, colonPos);
            std::string value = header.substr(colonPos + 1);
            value.erase(0, value.find_first_not_of(" \t"));
            req.header(key, value);
        }
    }

    if (!m_modCode.empty()) {
        req.header("X-Mod-Code", m_modCode);
    }

    if (method == "POST" && !postData.empty()) {
        req.bodyString(postData);
    }

    WebHelper::dispatch(std::move(req), method, url, [callback](web::WebResponse res) {
        bool success = res.ok();
        std::string responseStr = success
            ? res.string().unwrapOr("")
            : ("HTTP " + std::to_string(res.code()) + ": " + res.string().unwrapOr("Unknown error"));

        if (callback) callback(success, responseStr);
    });
}

void HttpClient::performBinaryRequest(
    std::string const& url,
    std::vector<std::string> const& headers,
    geode::CopyableFunction<void(bool, std::vector<uint8_t> const&)> callback
) {
    auto req = web::WebRequest();

    // meto headers
    for (auto const& header : headers) {
        size_t colonPos = header.find(':');
        if (colonPos != std::string::npos) {
            std::string key = header.substr(0, colonPos);
            std::string value = header.substr(colonPos + 1);
            value.erase(0, value.find_first_not_of(" \t"));
            req.header(key, value);
        }
    }

    if (!m_modCode.empty()) {
        req.header("X-Mod-Code", m_modCode);
    }

    std::string urlCopy = url; // pa logs

    WebHelper::dispatch(std::move(req), "GET", url, [callback, urlCopy](web::WebResponse res) {
        bool success = res.ok();
        std::vector<uint8_t> data = success ? res.data() : std::vector<uint8_t>{};

        int statusCode = res.code();
        PaimonDebug::log("[HttpClient] Binary GET {} -> status={}, size={}", urlCopy, statusCode, data.size());

        // Check Content-Type: if server returned JSON/HTML error, treat as failure
        if (success && !data.empty()) {
            auto ct = res.header("Content-Type");
            std::string contentType = ct.has_value() ? std::string(ct.value()) : "";
            PaimonDebug::log("[HttpClient] Binary response Content-Type: {}", contentType);

            // If content-type is JSON or HTML, it's an error response, not binary data
            if (contentType.find("application/json") != std::string::npos ||
                contentType.find("text/html") != std::string::npos) {
                std::string body(data.begin(), data.begin() + std::min(data.size(), (size_t)500));
                PaimonDebug::log("[HttpClient] Binary request got non-image response: {}", body);
                success = false;
                data.clear();
            }

            // Also validate magic bytes: PNG, JPEG, GIF, WEBP, BMP
            if (success && data.size() >= 4) {
                bool validImage = false;
                // PNG: 89 50 4E 47
                if (data[0] == 0x89 && data[1] == 0x50 && data[2] == 0x4E && data[3] == 0x47) validImage = true;
                // JPEG: FF D8 FF
                else if (data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF) validImage = true;
                // GIF: GIF8
                else if (data[0] == 'G' && data[1] == 'I' && data[2] == 'F' && data[3] == '8') validImage = true;
                // WEBP: RIFF....WEBP
                else if (data.size() >= 12 && data[0] == 'R' && data[1] == 'I' && data[2] == 'F' && data[3] == 'F'
                    && data[8] == 'W' && data[9] == 'E' && data[10] == 'B' && data[11] == 'P') validImage = true;
                // BMP: BM
                else if (data[0] == 'B' && data[1] == 'M') validImage = true;

                if (!validImage) {
                    std::string preview(data.begin(), data.begin() + std::min(data.size(), (size_t)200));
                    PaimonDebug::log("[HttpClient] Binary response does not look like an image. First bytes: {}", preview);
                    success = false;
                    data.clear();
                }
            }
        }

        if (callback) callback(success, data);
    });
}

void HttpClient::performUpload(
    std::string const& url,
    std::string const& fieldName,
    std::string const& filename,
    std::vector<uint8_t> const& data,
    std::vector<std::pair<std::string, std::string>> const& formFields,
    std::vector<std::string> const& headers,
    geode::CopyableFunction<void(bool, std::string const&)> callback,
    std::string const& fileContentType
) {
    // uso el MultipartForm de geode v5
    web::MultipartForm form;

    // meto los campos del form
    for (auto const& field : formFields) {
        form.param(field.first, field.second);
    }
    
    // agrego el archivo
    form.file(fieldName, std::span<uint8_t const>(data), filename, fileContentType);

    auto req = web::WebRequest();

    // aplico headers
    for (auto const& header : headers) {
        size_t colonPos = header.find(':');
        if (colonPos != std::string::npos) {
            std::string key = header.substr(0, colonPos);
            std::string value = header.substr(colonPos + 1);
            value.erase(0, value.find_first_not_of(" \t"));
            req.header(key, value);
        }
    }

    if (!m_modCode.empty()) {
        req.header("X-Mod-Code", m_modCode);
    }

    // mando body multipart de geode
    req.bodyMultipart(form);

    WebHelper::dispatch(std::move(req), "POST", url, [callback](web::WebResponse res) {
        bool success = res.ok();
        std::string responseStr = success
            ? res.string().unwrapOr("")
            : ("HTTP " + std::to_string(res.code()) + ": " + res.string().unwrapOr("Unknown error"));

        if (callback) callback(success, responseStr);
    });
}

void HttpClient::uploadProfile(int accountID, std::vector<uint8_t> const& pngData, std::string const& username, UploadCallback callback) {
    PaimonDebug::log("[HttpClient] Uploading profile background for account {} ({} bytes)", accountID, pngData.size());

    std::string url = m_serverURL + "/api/backgrounds/upload";
    std::string filename = std::to_string(accountID) + ".png";

    std::vector<std::pair<std::string, std::string>> formFields = {
        {"levelId", std::to_string(accountID)},
        {"username", username},
        {"accountID", std::to_string(accountID)}
    };

    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey
    };

    performUpload(
        url,
        "image",
        filename,
        pngData,
        formFields,
        headers,
        [callback = std::move(callback), accountID](bool success, std::string const& response) {
            if (success) {
                PaimonDebug::log("[HttpClient] Profile upload successful for account {}", accountID);
                // parse response to detect pending verification
                std::string resultMsg = "Profile upload successful";
                auto jsonRes = matjson::parse(response);
                if (jsonRes.isOk()) {
                    auto json = jsonRes.unwrap();
                    if (json.contains("pendingVerification") && json["pendingVerification"].asBool().unwrapOr(false)) {
                        resultMsg = "pending_verification";
                    }
                    if (json.contains("message") && json["message"].isString()) {
                        auto serverMsg = json["message"].asString().unwrapOr("");
                        if (!serverMsg.empty()) resultMsg = serverMsg;
                    }
                }
                callback(true, resultMsg);
            } else {
                log::error("[HttpClient] Profile upload failed for account {}: {}", accountID, response);
                callback(false, "Profile upload failed: " + response);
            }
        },
        "image/png"
    );
}

void HttpClient::uploadProfileGIF(int accountID, std::vector<uint8_t> const& gifData, std::string const& username, UploadCallback callback) {
    PaimonDebug::log("[HttpClient] Uploading profile background GIF for account {} ({} bytes)", accountID, gifData.size());

    std::string url = m_serverURL + "/api/backgrounds/upload-gif";
    std::string filename = std::to_string(accountID) + ".gif";

    std::vector<std::pair<std::string, std::string>> formFields = {
        {"levelId", std::to_string(accountID)},
        {"username", username},
        {"accountID", std::to_string(accountID)}
    };

    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey
    };

    performUpload(
        url,
        "image",
        filename,
        gifData,
        formFields,
        headers,
        [callback = std::move(callback), accountID](bool success, std::string const& response) {
            if (success) {
                PaimonDebug::log("[HttpClient] Profile GIF upload successful for account {}", accountID);
                std::string resultMsg = "Profile GIF upload successful";
                auto jsonRes = matjson::parse(response);
                if (jsonRes.isOk()) {
                    auto json = jsonRes.unwrap();
                    if (json.contains("pendingVerification") && json["pendingVerification"].asBool().unwrapOr(false)) {
                        resultMsg = "pending_verification";
                    }
                    if (json.contains("message") && json["message"].isString()) {
                        auto serverMsg = json["message"].asString().unwrapOr("");
                        if (!serverMsg.empty()) resultMsg = serverMsg;
                    }
                }
                callback(true, resultMsg);
            } else {
                log::error("[HttpClient] Profile GIF upload failed for account {}: {}", accountID, response);
                callback(false, "Profile GIF upload failed: " + response);
            }
        },
        "image/gif"
    );
}

void HttpClient::uploadProfileImg(int accountID, std::vector<uint8_t> const& imgData, std::string const& username, std::string const& contentType, UploadCallback callback) {
    PaimonDebug::log("[HttpClient] Uploading profile image for account {} ({} bytes, type: {})", accountID, imgData.size(), contentType);

    std::string url = m_serverURL + "/api/profileimgs/upload";

    // deducir extension del content type
    std::string ext = "png";
    if (contentType == "image/gif") ext = "gif";
    else if (contentType == "image/jpeg") ext = "jpg";
    else if (contentType == "image/webp") ext = "webp";
    else if (contentType == "image/bmp") ext = "bmp";
    else if (contentType == "image/tiff") ext = "tiff";

    std::string filename = "profileimg" + std::to_string(accountID) + "." + ext;

    std::vector<std::pair<std::string, std::string>> formFields = {
        {"path", "/profileimgs"},
        {"levelId", std::to_string(accountID)},
        {"username", username},
        {"accountID", std::to_string(accountID)}
    };

    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey
    };

    performUpload(
        url,
        "image",
        filename,
        imgData,
        formFields,
        headers,
        [callback = std::move(callback), accountID](bool success, std::string const& response) {
            if (success) {
                PaimonDebug::log("[HttpClient] Profile image upload successful for account {}", accountID);
                // parse response to detect pending verification
                std::string resultMsg = "Profile image upload successful";
                auto jsonRes = matjson::parse(response);
                if (jsonRes.isOk()) {
                    auto json = jsonRes.unwrap();
                    if (json.contains("pendingVerification") && json["pendingVerification"].asBool().unwrapOr(false)) {
                        resultMsg = "pending_verification";
                    }
                    if (json.contains("message") && json["message"].isString()) {
                        auto serverMsg = json["message"].asString().unwrapOr("");
                        if (!serverMsg.empty()) resultMsg = serverMsg;
                    }
                }
                callback(true, resultMsg);
            } else {
                log::error("[HttpClient] Profile image upload failed for account {}: {}", accountID, response);
                callback(false, "Profile image upload failed: " + response);
            }
        },
        contentType
    );
}

void HttpClient::uploadProfileImgGIF(int accountID, std::vector<uint8_t> const& gifData, std::string const& username, UploadCallback callback) {
    uploadProfileImg(accountID, gifData, username, "image/gif", callback);
}

void HttpClient::downloadProfileImg(int accountID, DownloadCallback callback, bool isSelf) {
    PaimonDebug::log("[HttpClient] Downloading profile image for account {} (self={})", accountID, isSelf);

    auto now = std::chrono::system_clock::now().time_since_epoch();
    auto ts = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();

    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey,
        "Cache-Control: no-cache"
    };

    std::string url = m_serverURL + "/profileimgs/" + std::to_string(accountID) + "?_ts=" + std::to_string(ts);
    if (isSelf) {
        url += "&self=1";
    }

    performBinaryRequest(url, headers, [callback = std::move(callback), accountID](bool success, std::vector<uint8_t> const& data) {
        if (success && !data.empty()) {
            PaimonDebug::log("[HttpClient] Profile image downloaded for account {}: {} bytes", accountID, data.size());
            callback(true, data, 0, 0);
        } else {
            PaimonDebug::warn("[HttpClient] No profile image found for account {}", accountID);
            callback(false, {}, 0, 0);
        }
    });
}

void HttpClient::uploadProfileConfig(int accountID, std::string const& jsonConfig, GenericCallback callback) {
    PaimonDebug::log("[HttpClient] Uploading profile config for account {}", accountID);
    
    std::string url = m_serverURL + "/api/profiles/config/upload";

    // uso MultipartForm de Geode v5
    web::MultipartForm form;
    form.param("accountID", std::to_string(accountID));
    form.param("config", jsonConfig);

    auto req = web::WebRequest();
    req.header("X-API-Key", m_apiKey);
    if (!m_modCode.empty()) {
        req.header("X-Mod-Code", m_modCode);
    }
    req.bodyMultipart(form);

    WebHelper::dispatch(std::move(req), "POST", url, [callback = std::move(callback)](web::WebResponse res) mutable {
        bool success = res.ok();
        std::string responseStr = success
            ? res.string().unwrapOr("")
            : ("HTTP " + std::to_string(res.code()) + ": " + res.string().unwrapOr("Unknown error"));

        if (callback) callback(success, responseStr);
    });
}

void HttpClient::downloadProfileConfig(int accountID, GenericCallback callback) {
    PaimonDebug::log("[HttpClient] Downloading profile config for account {}", accountID);
    
    auto now = std::chrono::system_clock::now().time_since_epoch();
    auto ts = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    
    std::string url = m_serverURL + "/api/profiles/config/" + std::to_string(accountID) + ".json?_ts=" + std::to_string(ts);
    
    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey,
        "Cache-Control: no-cache"
    };
    
    performRequest(url, "GET", "", headers, [callback = std::move(callback)](bool success, std::string const& response) {
        callback(success, response);
    });
}

void HttpClient::downloadProfile(int accountID, std::string const& username, DownloadCallback callback) {
    PaimonDebug::log("[HttpClient] Downloading profile background for account {} (user: {})", accountID, username);

    auto now = std::chrono::system_clock::now().time_since_epoch();
    auto ts = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    
    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey,
        "Cache-Control: no-cache"
    };
    
    // descargar desde /backgrounds/ (endpoint dedicado, separado de thumbnails)

    std::string url = m_serverURL + "/backgrounds/" + std::to_string(accountID) + "?_ts=" + std::to_string(ts);
    if (!username.empty()) {
        url += "&username=" + username;
    }
    
    performBinaryRequest(url, headers, [callback = std::move(callback), accountID](bool success, std::vector<uint8_t> const& data) {
        if (success && !data.empty()) {
            PaimonDebug::log("[HttpClient] Profile downloaded for account {}: {} bytes", accountID, data.size());
            callback(true, data, 0, 0);
        } else {
            PaimonDebug::warn("[HttpClient] No profile found for account {}", accountID);
            callback(false, {}, 0, 0);
        }
    });
}

void HttpClient::uploadThumbnail(int levelId, std::vector<uint8_t> const& pngData, std::string const& username, UploadCallback callback) {
    PaimonDebug::log("[HttpClient] Uploading thumbnail as PNG for level {}, size: {} bytes", levelId, pngData.size());
    
    std::string url = m_serverURL + "/mod/upload";
    std::string filename = std::to_string(levelId) + ".png"; 
    
    int accountID = GJAccountManager::get()->m_accountID;

    std::vector<std::pair<std::string, std::string>> formFields = {
        {"path", "/thumbnails"},
        {"levelId", std::to_string(levelId)},
        {"username", username},
        {"accountID", std::to_string(accountID)}
    };
    
    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey
    };
    
    performUpload(url, "image", filename, pngData, formFields, headers, 
        [callback = std::move(callback), levelId](bool success, std::string const& response) {
            if (success) {
                PaimonDebug::log("[HttpClient] Upload successful for level {}", levelId);
                callback(true, "Upload successful");
            } else {
                log::error("[HttpClient] Upload failed for level {}: {}", levelId, response);
                callback(false, "Upload failed: " + response);
            }
        },
        "image/png"
    );
}

void HttpClient::uploadGIF(int levelId, std::vector<uint8_t> const& gifData, std::string const& username, UploadCallback callback) {
    PaimonDebug::log("[HttpClient] Uploading GIF for level {}, size: {} bytes", levelId, gifData.size());
    
    std::string url = m_serverURL + "/mod/upload-gif";
    std::string filename = std::to_string(levelId) + ".gif";
    
    int accountID = GJAccountManager::get()->m_accountID;

    std::vector<std::pair<std::string, std::string>> formFields = {
        {"path", "/thumbnails"},
        {"levelId", std::to_string(levelId)},
        {"username", username},
        {"accountID", std::to_string(accountID)}
    };
    
    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey
    };
    
    performUpload(url, "image", filename, gifData, formFields, headers, 
        [callback = std::move(callback), levelId](bool success, std::string const& response) {
            if (success) {
                PaimonDebug::log("[HttpClient] GIF upload successful for level {}", levelId);
                callback(true, "GIF Upload successful");
            } else {
                log::error("[HttpClient] GIF upload failed for level {}: {}", levelId, response);
                callback(false, "GIF Upload failed: " + response);
            }
        },
        "image/gif"
    );
}

void HttpClient::getThumbnails(int levelId, GenericCallback callback) {
    std::string url = m_serverURL + "/api/thumbnails/list?levelId=" + std::to_string(levelId);
    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey,
        "Cache-Control: no-cache"
    };
    
    performRequest(url, "GET", "", headers, [callback = std::move(callback)](bool success, std::string const& response) {
        callback(success, response);
    });
}

void HttpClient::getThumbnailInfo(int levelId, GenericCallback callback) {
     std::string url = m_serverURL + "/api/thumbnails/info?levelId=" + std::to_string(levelId);
     performRequest(url, "GET", "", {}, callback);
}

void HttpClient::uploadSuggestion(int levelId, std::vector<uint8_t> const& pngData, std::string const& username, UploadCallback callback) {
    PaimonDebug::log("[HttpClient] Uploading suggestion for level {}, size: {} bytes", levelId, pngData.size());
    
    std::string url = m_serverURL + "/api/suggestions/upload";
    std::string filename = std::to_string(levelId) + ".webp";
    
    int accountID = GJAccountManager::get()->m_accountID;

    std::vector<std::pair<std::string, std::string>> formFields = {
        {"path", "/suggestions"},
        {"levelId", std::to_string(levelId)},
        {"username", username},
        {"accountID", std::to_string(accountID)}
    };
    
    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey
    };
    
    performUpload(url, "image", filename, pngData, formFields, headers, 
        [callback = std::move(callback), levelId](bool success, std::string const& response) {
            if (success) {
                PaimonDebug::log("[HttpClient] Suggestion upload successful for level {}", levelId);
                callback(true, "Suggestion uploaded successfully");
            } else {
                log::error("[HttpClient] Suggestion upload failed for level {}: {}", levelId, response);
                callback(false, "Suggestion upload failed: " + response);
            }
        },
        "image/png"
    );
}

void HttpClient::uploadUpdate(int levelId, std::vector<uint8_t> const& pngData, std::string const& username, UploadCallback callback) {
    PaimonDebug::log("[HttpClient] Uploading update for level {}, size: {} bytes", levelId, pngData.size());
    
    std::string url = m_serverURL + "/api/updates/upload";
    std::string filename = std::to_string(levelId) + ".webp";
    
    int accountID = GJAccountManager::get()->m_accountID;

    std::vector<std::pair<std::string, std::string>> formFields = {
        {"path", "/updates"},
        {"levelId", std::to_string(levelId)},
        {"username", username},
        {"accountID", std::to_string(accountID)}
    };
    
    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey
    };
    
    performUpload(url, "image", filename, pngData, formFields, headers, 
        [callback = std::move(callback), levelId](bool success, std::string const& response) {
            if (success) {
                PaimonDebug::log("[HttpClient] Update upload successful for level {}", levelId);
                callback(true, "Update uploaded successfully");
            } else {
                log::error("[HttpClient] Update upload failed for level {}: {}", levelId, response);
                callback(false, "Update upload failed: " + response);
            }
        },
        "image/png"
    );
}

void HttpClient::downloadSuggestion(int levelId, DownloadCallback callback) {
    PaimonDebug::log("[HttpClient] Downloading suggestion for level {}", levelId);
    
    auto now = std::chrono::system_clock::now().time_since_epoch();
    auto ts = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    
    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey,
        "Cache-Control: no-cache"
    };
    
    std::string url = m_serverURL + "/suggestions/" + std::to_string(levelId) + ".webp?_ts=" + std::to_string(ts);
    
    performBinaryRequest(url, headers, [callback = std::move(callback), levelId](bool success, std::vector<uint8_t> const& data) {
        if (success && !data.empty()) {
            PaimonDebug::log("[HttpClient] Suggestion downloaded for level {}: {} bytes", levelId, data.size());
            callback(true, data, 0, 0);
        } else {
            PaimonDebug::warn("[HttpClient] No suggestion found for level {}", levelId);
            callback(false, {}, 0, 0);
        }
    });
}

void HttpClient::downloadUpdate(int levelId, DownloadCallback callback) {
    PaimonDebug::log("[HttpClient] Downloading update for level {}", levelId);
    
    auto now = std::chrono::system_clock::now().time_since_epoch();
    auto ts = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    
    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey,
        "Cache-Control: no-cache"
    };
    
    std::string url = m_serverURL + "/updates/" + std::to_string(levelId) + ".webp?_ts=" + std::to_string(ts);
    
    performBinaryRequest(url, headers, [callback = std::move(callback), levelId](bool success, std::vector<uint8_t> const& data) {
        if (success && !data.empty()) {
            PaimonDebug::log("[HttpClient] Update downloaded for level {}: {} bytes", levelId, data.size());
            callback(true, data, 0, 0);
        } else {
            PaimonDebug::warn("[HttpClient] No update found for level {}", levelId);
            callback(false, {}, 0, 0);
        }
    });
}

void HttpClient::downloadReported(int levelId, DownloadCallback callback) {
    PaimonDebug::log("[HttpClient] Downloading reported thumbnail for level {}", levelId);
    downloadThumbnail(levelId, callback);
}

void HttpClient::downloadThumbnail(int levelId, bool isGif, DownloadCallback callback) {
    if (!isGif) {
        downloadThumbnail(levelId, callback);
        return;
    }
    std::string url = m_serverURL + "/t/" + std::to_string(levelId) + ".gif";
    downloadFromUrl(url, callback);
}

void HttpClient::downloadThumbnail(int levelId, DownloadCallback callback) {
    PaimonDebug::log("[HttpClient] downloadThumbnail llamado para level {} (priorizando WebP/GIF)", levelId);
    
    bool preferGif = false; // logica fija, default en false

    auto headers = std::vector<std::string>{
        "X-API-Key: " + m_apiKey,
        "Connection: keep-alive"
    };
    
    std::string gifURL = m_serverURL + "/t/" + std::to_string(levelId) + ".gif";
    std::string webpURL = m_serverURL + "/t/" + std::to_string(levelId) + ".webp";
    std::string pngURL = m_serverURL + "/t/" + std::to_string(levelId) + ".png";
    
    PaimonDebug::log("[HttpClient] Prioridad: {} -> WebP -> PNG (fallback)", preferGif ? "GIF" : "WebP");

    // lista con prioridad y callbacks en cadena (sin recursion)

    auto tryPNG = [this, levelId, pngURL, headers, callback]() {
        performBinaryRequest(pngURL, headers, [callback = std::move(callback), levelId](bool success, std::vector<uint8_t> const& data) {
             if (success && !data.empty()) {
                 PaimonDebug::log("[HttpClient] Found PNG for level {}", levelId);
                callback(true, data, 0, 0);
            } else {
                 PaimonDebug::warn("[HttpClient] Format PNG failed for level {}", levelId);
                callback(false, {}, 0, 0);
            }
        });
    };

    auto trySecondary = [this, levelId, gifURL, webpURL, headers, callback, preferGif, tryPNG](bool primaryFailed) {
        std::string url = preferGif ? webpURL : gifURL; // si preferGif, el secundario es webp; si no, gif

        PaimonDebug::log("[HttpClient] Primary failed, trying secondary: {}", url);
        performBinaryRequest(url, headers, [callback = std::move(callback), levelId, tryPNG](bool success, std::vector<uint8_t> const& data) {
            if (success && !data.empty()) {
                callback(true, data, 0, 0);
            } else {
                tryPNG();
            }
        });
    };

    std::string primaryURL = preferGif ? gifURL : webpURL;
    PaimonDebug::log("[HttpClient] Trying primary: {}", primaryURL);
    
    performBinaryRequest(primaryURL, headers, [callback = std::move(callback), levelId, trySecondary](bool success, std::vector<uint8_t> const& data) {
        if (success && !data.empty()) {
            PaimonDebug::log("[HttpClient] Found primary for level {}", levelId);
            callback(true, data, 0, 0);
        } else {
            trySecondary(true);
        }
    });
}

void HttpClient::checkThumbnailExists(int levelId, CheckCallback callback) {
    time_t now = std::time(nullptr);
    auto cacheIt = m_existsCache.find(levelId);
    if (cacheIt != m_existsCache.end()) {
        if (now - cacheIt->second.timestamp < EXISTS_CACHE_DURATION) {
            callback(cacheIt->second.exists);
            return;
        } else {
            m_existsCache.erase(cacheIt);
        }
    }
    
    std::string url = m_serverURL + "/api/exists?levelId=" + std::to_string(levelId) + "&path=thumbnails";
    std::vector<std::string> headers = { "X-API-Key: " + m_apiKey };
    
    performRequest(url, "GET", "", headers, [this, callback, levelId, now](bool success, std::string const& response) {
        if (success) {
            bool exists = response.find("\"exists\":true") != std::string::npos || 
                          response.find("\"exists\": true") != std::string::npos;
            
            m_existsCache[levelId] = {exists, now};
            PaimonDebug::log("[HttpClient] Thumbnail exists check for level {}: {} (cached)", levelId, exists);
            callback(exists);
        } else {
            PaimonDebug::warn("[HttpClient] Failed to check thumbnail exists for level {}", levelId);
            callback(false);
        }
    });
}

void HttpClient::checkModerator(std::string const& username, ModeratorCallback callback) {
    checkModeratorAccount(username, 0, callback);
}

void HttpClient::checkModeratorAccount(std::string const& username, int accountID, ModeratorCallback callback) {
    PaimonDebug::log("[HttpClient] Checking moderator status for user: {} id:{}", username, accountID);
    
    std::string url = m_serverURL + "/api/moderator/check?username=" + username;
    if (accountID > 0) url += "&accountID=" + std::to_string(accountID);
    
    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey,
        "Accept: application/json"
    };
    
    performRequest(url, "GET", "", headers, [this, callback, username, accountID](bool success, std::string const& response) {
        if (success) {
            bool isMod = false;
            bool isAdmin = false;
            bool isVip = false;
            auto jsonRes = matjson::parse(response);
            if (jsonRes.isOk()) {
                auto json = jsonRes.unwrap();
                if (json.contains("isModerator")) {
                    isMod = json["isModerator"].asBool().unwrapOr(false);
                }
                if (json.contains("isAdmin")) {
                    isAdmin = json["isAdmin"].asBool().unwrapOr(false);
                }
                if (json.contains("isVip")) {
                    isVip = json["isVip"].asBool().unwrapOr(false);
                }
                // guardo el nuevo mod code si viene del server
                if (json.contains("newModCode")) {
                    std::string newCode = json["newModCode"].asString().unwrapOr("");
                    if (!newCode.empty()) {
                        this->setModCode(newCode);
                        PaimonDebug::log("[HttpClient] Received and saved new moderator code");
                    }
                }
            } else {
                PaimonDebug::warn("[HttpClient] JSON parse failed in moderator check, falling back to string search");
                // fallback: busco a mano en el string
                isMod = response.find("\"isModerator\":true") != std::string::npos || response.find("\"isModerator\": true") != std::string::npos;
                isAdmin = response.find("\"isAdmin\":true") != std::string::npos || response.find("\"isAdmin\": true") != std::string::npos;
                isVip = response.find("\"isVip\":true") != std::string::npos || response.find("\"isVip\": true") != std::string::npos;
            }
            // guardar estado VIP como saved value pa uso local
            Mod::get()->setSavedValue<bool>("is-verified-vip", isVip);
            PaimonDebug::log("[HttpClient] User {}#{} => moderator: {}, admin: {}, vip: {}", username, accountID, isMod, isAdmin, isVip);
            callback(isMod, isAdmin);
        } else {
            log::error("[HttpClient] Failed secure moderator check for {}#{}: {}", username, accountID, response);
            callback(false, false);
        }
    });
}

void HttpClient::getBanList(BanListCallback callback) {
    PaimonDebug::log("[HttpClient] Getting ban list");
    std::string url = m_serverURL + "/api/admin/banlist";
    std::vector<std::string> headers = { "X-API-Key: " + m_apiKey, "Accept: application/json" };
    performRequest(url, "GET", "", headers, callback);
}

void HttpClient::banUser(std::string const& username, std::string const& reason, BanUserCallback callback) {
    std::string url = m_serverURL + "/api/admin/ban";
    std::string adminUser = GJAccountManager::get()->m_username;
    int accountID = GJAccountManager::get()->m_accountID;

    matjson::Value json = matjson::makeObject({
        {"username", username},
        {"reason", reason},
        {"admin", adminUser},
        {"adminUser", adminUser},
        {"accountID", accountID}
    });
    
    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey,
        "X-Mod-Code: " + m_modCode,
        "Content-Type: application/json",
        "Accept: application/json"
    };
    performRequest(url, "POST", json.dump(), headers, [callback = std::move(callback)](bool success, std::string const& resp) {
        if (callback) callback(success, resp);
    });
}

void HttpClient::unbanUser(std::string const& username, BanUserCallback callback) {
    std::string url = m_serverURL + "/api/admin/unban";
    std::string adminUser = GJAccountManager::get()->m_username;
    int accountID = GJAccountManager::get()->m_accountID;

    matjson::Value json = matjson::makeObject({
        {"username", username},
        {"adminUser", adminUser},
        {"accountID", accountID}
    });
    
    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey,
        "X-Mod-Code: " + m_modCode,
        "Content-Type: application/json",
        "Accept: application/json"
    };
    performRequest(url, "POST", json.dump(), headers, [callback = std::move(callback)](bool success, std::string const& resp) {
        if (callback) callback(success, resp);
    });
}

void HttpClient::getModerators(ModeratorsListCallback callback) {
    std::string url = m_serverURL + "/api/moderators";
    std::vector<std::string> headers = { "X-API-Key: " + m_apiKey };
    
    performRequest(url, "GET", "", headers, [callback = std::move(callback)](bool success, std::string const& response) {
        if (!success) {
            callback(false, {});
            return;
        }
        auto res = matjson::parse(response);
        if (!res.isOk()) {
            callback(false, {});
            return;
        }
        auto json = res.unwrap();
        std::vector<std::string> moderators;
        if (json.contains("moderators") && json["moderators"].isArray()) {
            auto arrRes = json["moderators"].asArray();
            if (arrRes.isOk()) {
                for (auto const& item : arrRes.unwrap()) {
                    if (item.contains("username")) {
                         moderators.push_back(item["username"].asString().unwrapOr(""));
                    }
                }
            }
        }
        callback(true, moderators);
    });
}

void HttpClient::submitReport(int levelId, std::string const& username, std::string const& note, GenericCallback callback) {
    PaimonDebug::log("[HttpClient] Submitting report for level {} by user {}", levelId, username);
    std::string url = m_serverURL + "/api/report/submit";
    matjson::Value json = matjson::makeObject({
        {"levelId", levelId},
        {"username", username},
        {"note", note}
    });
    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey,
        "Content-Type: application/json",
        "Accept: application/json"
    };
    performRequest(url, "POST", json.dump(), headers, callback);
}

void HttpClient::getRating(int levelId, std::string const& username, std::string const& thumbnailId, GenericCallback callback) {
    std::string url = m_serverURL + "/api/v2/ratings/" + std::to_string(levelId) + "?username=" + username;
    if (!thumbnailId.empty()) url += "&thumbnailId=" + thumbnailId;
    std::vector<std::string> headers = { "X-API-Key: " + m_apiKey, "Accept: application/json" };
    performRequest(url, "GET", "", headers, callback);
}

void HttpClient::submitVote(int levelId, int stars, std::string const& username, std::string const& thumbnailId, GenericCallback callback) {
    std::string url = m_serverURL + "/api/v2/ratings/vote";
    matjson::Value json = matjson::makeObject({
        {"levelID", levelId},
        {"stars", stars},        
        {"username", username}
    });
    if (!thumbnailId.empty()) json["thumbnailId"] = thumbnailId;
    
    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey,
        "Content-Type: application/json",
        "Accept: application/json"
    };
    performRequest(url, "POST", json.dump(), headers, callback);
}

void HttpClient::downloadFromUrl(std::string const& url, DownloadCallback callback) {
    std::vector<std::string> headers = { "X-API-Key: " + m_apiKey };
    performBinaryRequest(url, headers, [callback = std::move(callback)](bool success, std::vector<uint8_t> const& data) {
        if (success && !data.empty()) {
            callback(true, data, 0, 0);
        } else {
            callback(false, {}, 0, 0);
        }
    });
}

void HttpClient::downloadFromUrlRaw(std::string const& url, DownloadCallback callback) {
    // Descarga binaria SIN validar magic bytes de imagen.
    // util para archivos de audio (MP3, OGG, etc.) que no pasan
    // la validacion de formato de imagen en performBinaryRequest.
    auto req = web::WebRequest();

    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey,
        "Cache-Control: no-cache, no-store, must-revalidate",
        "Pragma: no-cache"
    };
    for (auto const& header : headers) {
        size_t colonPos = header.find(':');
        if (colonPos != std::string::npos) {
            std::string key = header.substr(0, colonPos);
            std::string value = header.substr(colonPos + 1);
            value.erase(0, value.find_first_not_of(" \t"));
            req.header(key, value);
        }
    }

    if (!m_modCode.empty()) {
        req.header("X-Mod-Code", m_modCode);
    }

    std::string urlCopy = url;

    WebHelper::dispatch(std::move(req), "GET", url, [callback, urlCopy](web::WebResponse res) {
        bool success = res.ok();
        std::vector<uint8_t> data = success ? res.data() : std::vector<uint8_t>{};

        int statusCode = res.code();
        PaimonDebug::log("[HttpClient] Raw binary GET {} -> status={}, size={}", urlCopy, statusCode, data.size());

        // Solo verificar Content-Type para rechazar errores JSON/HTML
        if (success && !data.empty()) {
            auto ct = res.header("Content-Type");
            std::string contentType = ct.has_value() ? std::string(ct.value()) : "";

            if (contentType.find("application/json") != std::string::npos ||
                contentType.find("text/html") != std::string::npos) {
                std::string body(data.begin(), data.begin() + std::min(data.size(), (size_t)500));
                PaimonDebug::log("[HttpClient] Raw binary request got error response: {}", body);
                success = false;
                data.clear();
            }
        }

        if (callback) {
            if (success && !data.empty()) {
                callback(true, data, 0, 0);
            } else {
                callback(false, {}, 0, 0);
            }
        }
    });
}

void HttpClient::get(std::string const& endpoint, GenericCallback callback) {
    std::string url = m_serverURL + endpoint;
    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey,
        "Accept: application/json"
    };
    performRequest(url, "GET", "", headers, callback);
}

void HttpClient::post(std::string const& endpoint, std::string const& data, GenericCallback callback) {
    std::string url = m_serverURL + endpoint;
    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey,
        "Content-Type: application/json",
        "Accept: application/json"
    };
    performRequest(url, "POST", data, headers, callback);
}

void HttpClient::postWithAuth(std::string const& endpoint, std::string const& data, GenericCallback callback) {
    std::string url = m_serverURL + endpoint;
    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey,
        "X-Mod-Code: " + m_modCode,
        "Content-Type: application/json",
        "Accept: application/json"
    };
    performRequest(url, "POST", data, headers, callback);
}

// ════════════════════════════════════════════════════════════
// Pet Shop API
// ════════════════════════════════════════════════════════════

void HttpClient::getPetShopList(GenericCallback callback) {
    get("/api/pet-shop/list", callback);
}

void HttpClient::downloadPetShopItem(std::string const& itemId, std::string const& format,
    geode::CopyableFunction<void(bool, std::vector<uint8_t> const&)> callback) {
    std::string url = m_serverURL + "/api/pet-shop/download/" + itemId + "." + format;
    std::vector<std::string> headers = { "X-API-Key: " + m_apiKey };
    performBinaryRequest(url, headers, callback);
}

void HttpClient::uploadPetShopItem(std::string const& name, std::string const& creator,
    std::vector<uint8_t> const& imageData, std::string const& format,
    UploadCallback callback) {
    std::string url = m_serverURL + "/api/pet-shop/upload";
    std::string ct = (format == "gif") ? "image/gif" : "image/png";
    std::string filename = "pet." + format;

    std::vector<std::pair<std::string, std::string>> fields = {
        {"name", name},
        {"creator", creator}
    };
    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey,
        "X-Mod-Code: " + m_modCode
    };
    performUpload(url, "image", filename, imageData, fields, headers, callback, ct);
}


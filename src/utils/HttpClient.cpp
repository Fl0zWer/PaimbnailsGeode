#include "HttpClient.hpp"
#include "Debug.hpp"
#include <Geode/Geode.hpp>
#include <Geode/utils/web.hpp>
#include <Geode/binding/GJAccountManager.hpp>
#include <ctime>
#include <map>
#include <chrono>
#include <algorithm>
#include <thread>

using namespace geode::prelude;

namespace {
constexpr int kUnboundAccountID = 0; // geode no suelta el accountID en bindings
}

HttpClient::HttpClient() {
    // config base del server
    m_serverURL = "https://paimon-thumbnails-server.paimonalcuadrado.workers.dev";
    
    // api key pa el server
    m_apiKey = "074b91c9-6631-4670-a6f08a2ce970-0183-471b";
    
    // cargo el mod code que tenga guardado
    m_modCode = Mod::get()->getSavedValue<std::string>("mod-code", "");

    PaimonDebug::log("[HttpClient] Initialized with server: {}", m_serverURL);
}

void HttpClient::cleanTasks() {
    // en v5 esto ya no hace nada
}

void HttpClient::setServerURL(const std::string& url) {
    m_serverURL = url;
    if (!m_serverURL.empty() && m_serverURL.back() == '/') {
        m_serverURL.pop_back();
    }
    PaimonDebug::log("[HttpClient] Server URL updated to: {}", m_serverURL);
}

void HttpClient::setModCode(const std::string& code) {
    m_modCode = code;
    Mod::get()->setSavedValue("mod-code", code);
    PaimonDebug::log("[HttpClient] Mod code updated.");
}

void HttpClient::performRequest(
    const std::string& url,
    const std::string& method,
    const std::string& postData,
    const std::vector<std::string>& headers,
    std::function<void(bool, const std::string&)> callback
) {
    // copio todo lo que necesito pa el thread
    std::string urlCopy = url;
    std::string methodCopy = method;
    std::string postDataCopy = postData;
    std::vector<std::string> headersCopy = headers;
    std::string modCodeCopy = m_modCode;
    
        // tiro el request en otro thread
    std::thread([urlCopy, methodCopy, postDataCopy, headersCopy, modCodeCopy, callback]() {
        auto req = web::WebRequest();

        // pongo headers
        for (const auto& header : headersCopy) {
            size_t colonPos = header.find(':');
            if (colonPos != std::string::npos) {
                std::string key = header.substr(0, colonPos);
                std::string value = header.substr(colonPos + 1);
                value.erase(0, value.find_first_not_of(" \t"));
                req.header(key, value);
            }
        }

        if (!modCodeCopy.empty()) {
            req.header("X-Mod-Code", modCodeCopy);
        }

        if (methodCopy == "POST" && !postDataCopy.empty()) {
            req.bodyString(postDataCopy);
        }

        // uso la versión sincrona
        web::WebResponse res = (methodCopy == "POST") ? req.postSync(urlCopy) : req.getSync(urlCopy);

        // enviar resultado al main thread
        bool success = res.ok();
        std::string responseStr = success ? res.string().unwrapOr("") : ("HTTP " + std::to_string(res.code()) + ": " + res.string().unwrapOr("Unknown error"));

        queueInMainThread([callback, success, responseStr]() {
            if (callback) callback(success, responseStr);
        });
    }).detach();
}

void HttpClient::performBinaryRequest(
    const std::string& url,
    const std::vector<std::string>& headers,
    std::function<void(bool, const std::vector<uint8_t>&)> callback
) {
    std::string urlCopy = url;
    std::vector<std::string> headersCopy = headers;
    std::string modCodeCopy = m_modCode;

    std::thread([urlCopy, headersCopy, modCodeCopy, callback]() {
        auto req = web::WebRequest();

        // meto headers
        for (const auto& header : headersCopy) {
            size_t colonPos = header.find(':');
            if (colonPos != std::string::npos) {
                std::string key = header.substr(0, colonPos);
                std::string value = header.substr(colonPos + 1);
                value.erase(0, value.find_first_not_of(" \t"));
                req.header(key, value);
            }
        }

        if (!modCodeCopy.empty()) {
            req.header("X-Mod-Code", modCodeCopy);
        }

        web::WebResponse res = req.getSync(urlCopy);

        bool success = res.ok();
        std::vector<uint8_t> data = success ? res.data() : std::vector<uint8_t>{};

        queueInMainThread([callback, success, data]() {
            if (callback) callback(success, data);
        });
    }).detach();
}

void HttpClient::performUpload(
    const std::string& url,
    const std::string& fieldName,
    const std::string& filename,
    const std::vector<uint8_t>& data,
    const std::vector<std::pair<std::string, std::string>>& formFields,
    const std::vector<std::string>& headers,
    std::function<void(bool, const std::string&)> callback,
    const std::string& fileContentType
) {
    // uso el MultipartForm de geode v5
    web::MultipartForm form;

    // meto los campos del form
    for (const auto& field : formFields) {
        form.param(field.first, field.second);
    }
    
    // agrego el archivo
    form.file(fieldName, std::span<uint8_t const>(data), filename, fileContentType);

    std::string urlCopy = url;
    std::vector<std::string> headersCopy = headers;
    std::string modCodeCopy = m_modCode;

    std::thread([urlCopy, headersCopy, modCodeCopy, form = std::move(form), callback]() mutable {
        auto req = web::WebRequest();

        // aplico headers
        for (const auto& header : headersCopy) {
            size_t colonPos = header.find(':');
            if (colonPos != std::string::npos) {
                std::string key = header.substr(0, colonPos);
                std::string value = header.substr(colonPos + 1);
                value.erase(0, value.find_first_not_of(" \t"));
                req.header(key, value);
            }
        }

        if (!modCodeCopy.empty()) {
            req.header("X-Mod-Code", modCodeCopy);
        }

        // mando body multipart de geode
        req.bodyMultipart(form);

        web::WebResponse res = req.postSync(urlCopy);

        bool success = res.ok();
        std::string responseStr = success ? res.string().unwrapOr("") : ("HTTP " + std::to_string(res.code()) + ": " + res.string().unwrapOr("Unknown error"));

        queueInMainThread([callback, success, responseStr]() {
            if (callback) callback(success, responseStr);
        });
    }).detach();
}

void HttpClient::uploadProfile(int accountID, const std::vector<uint8_t>& pngData, const std::string& username, UploadCallback callback) {
    PaimonDebug::log("[HttpClient] Uploading profile for account {} ({} bytes)", accountID, pngData.size());

    std::string url = m_serverURL + "/mod/upload";
    std::string filename = std::to_string(accountID) + ".png"; 

    std::vector<std::pair<std::string, std::string>> formFields = {
        {"path", "/profiles"},
        {"levelId", std::to_string(accountID)}, // el server a veces espera levelId
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
        [callback, accountID](bool success, const std::string& response) {
            if (success) {
                PaimonDebug::log("[HttpClient] Profile upload successful for account {}", accountID);
                callback(true, "Profile upload successful");
            } else {
                log::error("[HttpClient] Profile upload failed for account {}: {}", accountID, response);
                callback(false, "Profile upload failed: " + response);
            }
        },
        "image/png"
    );
}

void HttpClient::uploadProfileGIF(int accountID, const std::vector<uint8_t>& gifData, const std::string& username, UploadCallback callback) {
    PaimonDebug::log("[HttpClient] Uploading profile GIF for account {} ({} bytes)", accountID, gifData.size());

    std::string url = m_serverURL + "/mod/upload-gif";
    std::string filename = std::to_string(accountID) + ".gif";

    std::vector<std::pair<std::string, std::string>> formFields = {
        {"path", "/profiles"},
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
        [callback, accountID](bool success, const std::string& response) {
            if (success) {
                PaimonDebug::log("[HttpClient] Profile GIF upload successful for account {}", accountID);
                callback(true, "Profile GIF upload successful");
            } else {
                log::error("[HttpClient] Profile GIF upload failed for account {}: {}", accountID, response);
                callback(false, "Profile GIF upload failed: " + response);
            }
        },
        "image/gif"
    );
}

void HttpClient::uploadProfileConfig(int accountID, const std::string& jsonConfig, GenericCallback callback) {
    PaimonDebug::log("[HttpClient] Uploading profile config for account {}", accountID);
    
    std::string url = m_serverURL + "/api/profiles/config/upload";

    // uso MultipartForm de Geode v5
    web::MultipartForm form;
    form.param("accountID", std::to_string(accountID));
    form.param("config", jsonConfig);

    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey
    };
    
    std::string modCodeCopy = m_modCode;

    std::thread([url, headers, modCodeCopy, form = std::move(form), callback]() mutable {
        auto req = web::WebRequest();

        for (const auto& header : headers) {
            size_t colonPos = header.find(':');
            if (colonPos != std::string::npos) {
                std::string key = header.substr(0, colonPos);
                std::string value = header.substr(colonPos + 1);
                value.erase(0, value.find_first_not_of(" \t"));
                req.header(key, value);
            }
        }

        if (!modCodeCopy.empty()) {
            req.header("X-Mod-Code", modCodeCopy);
        }

        req.bodyMultipart(form);

        web::WebResponse res = req.postSync(url);

        bool success = res.ok();
        std::string responseStr = success ? res.string().unwrapOr("") : ("HTTP " + std::to_string(res.code()) + ": " + res.string().unwrapOr("Unknown error"));

        queueInMainThread([callback, success, responseStr]() {
            if (callback) callback(success, responseStr);
        });
    }).detach();
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
    
    performRequest(url, "GET", "", headers, [callback](bool success, const std::string& response) {
        callback(success, response);
    });
}

void HttpClient::downloadProfile(int accountID, const std::string& username, DownloadCallback callback) {
    PaimonDebug::log("[HttpClient] Downloading profile for account {} (user: {})", accountID, username);
    
    auto now = std::chrono::system_clock::now().time_since_epoch();
    auto ts = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    
    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey,
        "Cache-Control: no-cache"
    };
    
    // intentar descargar webp primero; timestamp pa evitar cache

    std::string url = m_serverURL + "/profiles/" + std::to_string(accountID) + "?_ts=" + std::to_string(ts);
    if (!username.empty()) {
        url += "&username=" + username;
    }
    
    performRequest(url, "GET", "", headers, [callback, accountID](bool success, const std::string& resp) {
        if (success && !resp.empty()) {
            std::vector<uint8_t> data(resp.begin(), resp.end());
            PaimonDebug::log("[HttpClient] Profile downloaded for account {}: {} bytes", accountID, data.size());
            callback(true, data, 0, 0);
        } else {
            PaimonDebug::warn("[HttpClient] No profile found for account {}", accountID);
            callback(false, {}, 0, 0);
        }
    });
}

void HttpClient::uploadThumbnail(int levelId, const std::vector<uint8_t>& pngData, const std::string& username, UploadCallback callback) {
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
        [callback, levelId](bool success, const std::string& response) {
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

void HttpClient::uploadGIF(int levelId, const std::vector<uint8_t>& gifData, const std::string& username, UploadCallback callback) {
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
        [callback, levelId](bool success, const std::string& response) {
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
    
    performRequest(url, "GET", "", headers, [callback](bool success, const std::string& response) {
        callback(success, response);
    });
}

void HttpClient::getThumbnailInfo(int levelId, GenericCallback callback) {
     std::string url = m_serverURL + "/api/thumbnails/info?levelId=" + std::to_string(levelId);
     performRequest(url, "GET", "", {}, callback);
}

void HttpClient::uploadSuggestion(int levelId, const std::vector<uint8_t>& pngData, const std::string& username, UploadCallback callback) {
    PaimonDebug::log("[HttpClient] Uploading suggestion for level {}, size: {} bytes", levelId, pngData.size());
    
    std::string url = m_serverURL + "/api/suggestions/upload";
    std::string filename = std::to_string(levelId) + ".webp";
    
    int accountID = kUnboundAccountID;
    
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
        [callback, levelId](bool success, const std::string& response) {
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

void HttpClient::uploadUpdate(int levelId, const std::vector<uint8_t>& pngData, const std::string& username, UploadCallback callback) {
    PaimonDebug::log("[HttpClient] Uploading update for level {}, size: {} bytes", levelId, pngData.size());
    
    std::string url = m_serverURL + "/api/updates/upload";
    std::string filename = std::to_string(levelId) + ".webp";
    
    int accountID = kUnboundAccountID;
    
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
        [callback, levelId](bool success, const std::string& response) {
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
    
    performRequest(url, "GET", "", headers, [callback, levelId](bool success, const std::string& resp) {
        if (success && !resp.empty()) {
            std::vector<uint8_t> data(resp.begin(), resp.end());
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
    
    performRequest(url, "GET", "", headers, [callback, levelId](bool success, const std::string& resp) {
        if (success && !resp.empty()) {
            std::vector<uint8_t> data(resp.begin(), resp.end());
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
    
    bool preferGif = false; // lógica fija, default en false

    auto headers = std::vector<std::string>{
        "X-API-Key: " + m_apiKey,
        "Connection: keep-alive"
    };
    
    std::string gifURL = m_serverURL + "/t/" + std::to_string(levelId) + ".gif";
    std::string webpURL = m_serverURL + "/t/" + std::to_string(levelId) + ".webp";
    std::string pngURL = m_serverURL + "/t/" + std::to_string(levelId) + ".png";
    
    PaimonDebug::log("[HttpClient] Prioridad: {} -> WebP -> PNG (fallback)", preferGif ? "GIF" : "WebP");

    // lista con prioridad y callbacks en cadena (sin recursión)

    auto tryPNG = [this, levelId, pngURL, headers, callback]() {
        performRequest(pngURL, "GET", "", headers, [callback, levelId, pngURL](bool success, const std::string& resp) {
             if (success && !resp.empty()) {
                std::vector<uint8_t> data(resp.begin(), resp.end());
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
        performRequest(url, "GET", "", headers, [callback, levelId, tryPNG](bool success, const std::string& resp) {
            if (success && !resp.empty()) {
                std::vector<uint8_t> data(resp.begin(), resp.end());
                callback(true, data, 0, 0);
            } else {
                tryPNG();
            }
        });
    };

    std::string primaryURL = preferGif ? gifURL : webpURL;
    PaimonDebug::log("[HttpClient] Trying primary: {}", primaryURL);
    
    performRequest(primaryURL, "GET", "", headers, [callback, levelId, trySecondary](bool success, const std::string& resp) {
        if (success && !resp.empty()) {
            std::vector<uint8_t> data(resp.begin(), resp.end());
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
    
    performRequest(url, "GET", "", headers, [this, callback, levelId, now](bool success, const std::string& response) {
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

void HttpClient::checkModerator(const std::string& username, ModeratorCallback callback) {
    checkModeratorAccount(username, 0, callback);
}

void HttpClient::checkModeratorAccount(const std::string& username, int accountID, ModeratorCallback callback) {
    PaimonDebug::log("[HttpClient] Checking moderator status for user: {} id:{}", username, accountID);
    
    std::string url = m_serverURL + "/api/moderator/check?username=" + username;
    if (accountID > 0) url += "&accountID=" + std::to_string(accountID);
    
    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey,
        "Accept: application/json"
    };
    
    performRequest(url, "GET", "", headers, [this, callback, username, accountID](bool success, const std::string& response) {
        if (success) {
            bool isMod = false;
            bool isAdmin = false;
            try {
                auto jsonRes = matjson::parse(response);
                if (jsonRes.isOk()) {
                    auto json = jsonRes.unwrap();
                    if (json.contains("isModerator")) {
                        isMod = json["isModerator"].asBool().unwrapOr(false);
                    }
                    if (json.contains("isAdmin")) {
                        isAdmin = json["isAdmin"].asBool().unwrapOr(false);
                    }
                    // guardo el nuevo mod code si viene del server
                    if (json.contains("newModCode")) {
                        std::string newCode = json["newModCode"].asString().unwrapOr("");
                        if (!newCode.empty()) {
                            this->setModCode(newCode);
                            PaimonDebug::log("[HttpClient] Received and saved new moderator code");
                        }
                    }
                }
            } catch (const std::exception& e) {
                PaimonDebug::warn("[HttpClient] JSON Error in moderator check: {}, falling back to string search", e.what());
                // fallback: busco a mano en el string
                isMod = response.find("\"isModerator\":true") != std::string::npos || response.find("\"isModerator\": true") != std::string::npos;
                isAdmin = response.find("\"isAdmin\":true") != std::string::npos || response.find("\"isAdmin\": true") != std::string::npos;
            } catch (...) {
                PaimonDebug::warn("[HttpClient] Unknown JSON Error in moderator check, falling back to string search");
                isMod = response.find("\"isModerator\":true") != std::string::npos || response.find("\"isModerator\": true") != std::string::npos;
                isAdmin = response.find("\"isAdmin\":true") != std::string::npos || response.find("\"isAdmin\": true") != std::string::npos;
            }
            PaimonDebug::log("[HttpClient] User {}#{} => moderator: {}, admin: {}", username, accountID, isMod, isAdmin);
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

void HttpClient::banUser(const std::string& username, const std::string& reason, BanUserCallback callback) {
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
    performRequest(url, "POST", json.dump(), headers, [callback](bool success, const std::string& resp) {
        if (callback) callback(success, resp);
    });
}

void HttpClient::unbanUser(const std::string& username, BanUserCallback callback) {
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
    performRequest(url, "POST", json.dump(), headers, [callback](bool success, const std::string& resp) {
        if (callback) callback(success, resp);
    });
}

void HttpClient::getModerators(ModeratorsListCallback callback) {
    std::string url = m_serverURL + "/api/moderators";
    std::vector<std::string> headers = { "X-API-Key: " + m_apiKey };
    
    performRequest(url, "GET", "", headers, [callback](bool success, const std::string& response) {
        if (!success) {
            callback(false, {});
            return;
        }
        try {
            auto res = matjson::parse(response);
            if (!res.isOk()) {
                callback(false, {});
                return;
            }
            auto json = res.unwrap();
            std::vector<std::string> moderators;
            if (json.contains("moderators") && json["moderators"].isArray()) {
                for (const auto& item : json["moderators"].asArray().unwrap()) {
                    if (item.contains("username")) { // array de objetos que traen username
                         if (item.contains("username")) {
                             moderators.push_back(item["username"].asString().unwrapOr(""));
                         }
                    }
                }
            }
            callback(true, moderators);
        } catch(...) {
            callback(false, {});
        }
    });
}

void HttpClient::submitReport(int levelId, const std::string& username, const std::string& note, GenericCallback callback) {
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

void HttpClient::getRating(int levelId, const std::string& username, const std::string& thumbnailId, GenericCallback callback) {
    std::string url = m_serverURL + "/api/v2/ratings/" + std::to_string(levelId) + "?username=" + username;
    if (!thumbnailId.empty()) url += "&thumbnailId=" + thumbnailId;
    std::vector<std::string> headers = { "X-API-Key: " + m_apiKey, "Accept: application/json" };
    performRequest(url, "GET", "", headers, callback);
}

void HttpClient::submitVote(int levelId, int stars, const std::string& username, const std::string& thumbnailId, GenericCallback callback) {
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

void HttpClient::downloadFromUrl(const std::string& url, DownloadCallback callback) {
    std::vector<std::string> headers = { "X-API-Key: " + m_apiKey };
    performRequest(url, "GET", "", headers, [callback](bool success, const std::string& resp) {
        if (success && !resp.empty()) {
            std::vector<uint8_t> data(resp.begin(), resp.end());
            callback(true, data, 0, 0);
        } else {
            callback(false, {}, 0, 0);
        }
    });
}

void HttpClient::get(const std::string& endpoint, GenericCallback callback) {
    std::string url = m_serverURL + endpoint;
    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey,
        "Accept: application/json"
    };
    performRequest(url, "GET", "", headers, callback);
}

void HttpClient::post(const std::string& endpoint, const std::string& data, GenericCallback callback) {
    std::string url = m_serverURL + endpoint;
    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey,
        "Content-Type: application/json",
        "Accept: application/json"
    };
    performRequest(url, "POST", data, headers, callback);
}

void HttpClient::postWithAuth(const std::string& endpoint, const std::string& data, GenericCallback callback) {
    std::string url = m_serverURL + endpoint;
    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey,
        "X-Mod-Code: " + m_modCode,
        "Content-Type: application/json",
        "Accept: application/json"
    };
    performRequest(url, "POST", data, headers, callback);
}

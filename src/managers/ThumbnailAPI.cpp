#include "ThumbnailAPI.hpp"
#include "ThumbnailLoader.hpp"
#include "ProfileThumbs.hpp"
#include "../utils/ImageConverter.hpp"
#include "../utils/AnimatedGIFSprite.hpp"
#include "../utils/WebHelper.hpp"
#include <Geode/loader/Log.hpp>
#include <Geode/utils/string.hpp>
#include <Geode/utils/web.hpp>
#include <Geode/utils/function.hpp>
#include <Geode/binding/GJAccountManager.hpp>
#include <fstream>
#include <chrono>

using namespace geode::prelude;


ThumbnailAPI::ThumbnailAPI() {
    m_serverEnabled = true;
    log::info("[ThumbnailAPI] inicializado - servidor activado: {}", m_serverEnabled);
}

void ThumbnailAPI::setServerEnabled(bool enabled) {
    m_serverEnabled = enabled;
    log::info("[ThumbnailAPI] modo servidor cambiado a: {}", enabled);
}

void ThumbnailAPI::uploadThumbnail(int levelId, std::vector<uint8_t> const& pngData, std::string const& username, UploadCallback callback) {
    if (GJAccountManager::get()->m_accountID <= 0) {
        log::warn("[ThumbnailAPI] usuario no logueado, subida denegada.");
        callback(false, "Debes estar logueado para subir miniaturas.");
        return;
    }
    if (!m_serverEnabled) {
        log::warn("[ThumbnailAPI] servidor desactivado, saltando subida pal nivel {}", levelId);
        callback(false, "Funcionalidad de servidor desactivada");
        return;
    }
    
    log::info("[ThumbnailAPI] subiendo miniatura pal nivel {} ({} bytes)", levelId, pngData.size());
    
    HttpClient::get().uploadThumbnail(levelId, pngData, username, [this, callback, levelId](bool success, std::string const& message) {
        if (success) {
            m_uploadCount++;
            log::info("[ThumbnailAPI] subida exitosa - total subidas: {}", m_uploadCount);
            ThumbnailLoader::get().invalidateLevel(levelId);
        }
        callback(success, message);
    });
}

void ThumbnailAPI::uploadGIF(int levelId, std::vector<uint8_t> const& gifData, std::string const& username, UploadCallback callback) {
    if (GJAccountManager::get()->m_accountID <= 0) {
        log::warn("[ThumbnailAPI] usuario no logueado, subida denegada.");
        callback(false, "Debes estar logueado para subir miniaturas.");
        return;
    }
    if (!m_serverEnabled) {
        log::warn("[ThumbnailAPI] servidor desactivado, saltando subida de gif pal nivel {}", levelId);
        callback(false, "Funcionalidad de servidor desactivada");
        return;
    }
    
    log::info("[ThumbnailAPI] subiendo gif pal nivel {} ({} bytes)", levelId, gifData.size());
    
    HttpClient::get().uploadGIF(levelId, gifData, username, [this, callback, levelId](bool success, std::string const& message) {
        if (success) {
            m_uploadCount++;
            log::info("[ThumbnailAPI] subida de gif exitosa - total subidas: {}", m_uploadCount);
            ThumbnailLoader::get().invalidateLevel(levelId);
        }
        callback(success, message);
    });
}

// overload eliminado


void ThumbnailAPI::getThumbnails(int levelId, ThumbnailListCallback callback) {
    if (!m_serverEnabled) {
        callback(false, {});
        return;
    }
    
    HttpClient::get().getThumbnails(levelId, [callback](bool success, std::string const& response) {
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
        std::vector<ThumbnailInfo> thumbnails;

        if (json.contains("thumbnails") && json["thumbnails"].isArray()) {
                auto arrRes = json["thumbnails"].asArray();
                if (!arrRes.isOk()) { callback(false, {}); return; }
                for (auto const& item : arrRes.unwrap()) {
                    ThumbnailInfo info;
                    info.id = item["id"].asString().unwrapOr("");
                    info.url = item["url"].asString().unwrapOr("");
                    info.type = item["type"].asString().unwrapOr("");
                    info.format = item["format"].asString().unwrapOr("");
                    // intentar sacar el autor
                    if (item.contains("author")) info.creator = item["author"].asString().unwrapOr("Unknown");
                    else if (item.contains("username")) info.creator = item["username"].asString().unwrapOr("Unknown");
                    else if (item.contains("uploader")) info.creator = item["uploader"].asString().unwrapOr("Unknown");
                    else if (item.contains("uploaded_by")) info.creator = item["uploaded_by"].asString().unwrapOr("Unknown");
                    else if (item.contains("submitted_by")) info.creator = item["submitted_by"].asString().unwrapOr("Unknown");
                    else if (item.contains("user")) info.creator = item["user"].asString().unwrapOr("Unknown");
                    else if (item.contains("owner")) info.creator = item["owner"].asString().unwrapOr("Unknown");
                    else info.creator = "Unknown";

                    // intentar sacar la fecha
                    if (item.contains("date")) info.date = item["date"].asString().unwrapOr("Unknown");
                    else if (item.contains("created_at")) info.date = item["created_at"].asString().unwrapOr("Unknown");
                    else if (item.contains("timestamp")) info.date = item["timestamp"].asString().unwrapOr("Unknown");
                    else if (item.contains("uploaded_at")) info.date = item["uploaded_at"].asString().unwrapOr("Unknown");
                    else info.date = "Unknown";

                    thumbnails.push_back(info);
                }
        }
        callback(true, thumbnails);
    });
}

void ThumbnailAPI::getThumbnailInfo(int levelId, ActionCallback callback) {
    if (!m_serverEnabled) {
        callback(false, "Server disabled");
        return;
    }
    
    HttpClient::get().getThumbnailInfo(levelId, [callback](bool success, std::string const& response) {
        callback(success, response);
    });
}

std::string ThumbnailAPI::getThumbnailURL(int levelId) {
    return HttpClient::get().getServerURL() + "/thumbnails/" + std::to_string(levelId) + ".png";
}

void ThumbnailAPI::uploadSuggestion(int levelId, std::vector<uint8_t> const& pngData, std::string const& username, UploadCallback callback) {
    if (GJAccountManager::get()->m_accountID <= 0) {
        log::warn("[ThumbnailAPI] usuario no logueado, subida denegada.");
        callback(false, "Debes estar logueado para subir miniaturas.");
        return;
    }
    if (!m_serverEnabled) {
        log::warn("[ThumbnailAPI] servidor desactivado, saltando subida de sugerencia pal nivel {}", levelId);
        callback(false, "Funcionalidad de servidor desactivada");
        return;
    }
    
    log::info("[ThumbnailAPI] subiendo sugerencia pal nivel {} ({} bytes)", levelId, pngData.size());
    
    HttpClient::get().uploadSuggestion(levelId, pngData, username, [this, callback](bool success, std::string const& message) {
        if (success) {
            m_uploadCount++;
            log::info("[ThumbnailAPI] subida de sugerencia exitosa - total subidas: {}", m_uploadCount);
        }
        callback(success, message);
    });
}

void ThumbnailAPI::uploadUpdate(int levelId, std::vector<uint8_t> const& pngData, std::string const& username, UploadCallback callback) {
    if (GJAccountManager::get()->m_accountID <= 0) {
        log::warn("[ThumbnailAPI] usuario no logueado, subida denegada.");
        callback(false, "Debes estar logueado para subir miniaturas.");
        return;
    }
    if (!m_serverEnabled) {
        log::warn("[ThumbnailAPI] servidor desactivado, saltando subida de update pal nivel {}", levelId);
        callback(false, "Funcionalidad de servidor desactivada");
        return;
    }
    
    log::info("[ThumbnailAPI] subiendo update pal nivel {} ({} bytes)", levelId, pngData.size());
    
    HttpClient::get().uploadUpdate(levelId, pngData, username, [this, callback, levelId](bool success, std::string const& message) {
        if (success) {
            m_uploadCount++;
            log::info("[ThumbnailAPI] subida de update exitosa - total subidas: {}", m_uploadCount);
            ThumbnailLoader::get().invalidateLevel(levelId);
        }
        callback(success, message);
    });
}

void ThumbnailAPI::uploadProfile(int accountID, std::vector<uint8_t> const& pngData, std::string const& username, UploadCallback callback) {
    if (GJAccountManager::get()->m_accountID <= 0) {
        log::warn("[ThumbnailAPI] usuario no logueado, subida denegada.");
        callback(false, "Debes estar logueado para subir miniaturas.");
        return;
    }
    if (!m_serverEnabled) {
        log::warn("[ThumbnailAPI] servidor desactivado, saltando subida de perfil pal account {}", accountID);
        callback(false, "Funcionalidad de servidor desactivada");
        return;
    }
    log::info("[ThumbnailAPI] subiendo perfil pal account {} ({} bytes)", accountID, pngData.size());
    HttpClient::get().uploadProfile(accountID, pngData, username, [this, callback, accountID](bool success, std::string const& message){
        if (success) {
            m_uploadCount++;
            log::info("[ThumbnailAPI] subida de perfil exitosa - total subidas: {}", m_uploadCount);
            ProfileThumbs::get().deleteProfile(accountID);
        }
        callback(success, message);
    });
}

void ThumbnailAPI::uploadProfileGIF(int accountID, std::vector<uint8_t> const& gifData, std::string const& username, UploadCallback callback) {
    if (GJAccountManager::get()->m_accountID <= 0) {
        log::warn("[ThumbnailAPI] usuario no logueado, subida denegada.");
        callback(false, "Debes estar logueado para subir miniaturas.");
        return;
    }
    if (!m_serverEnabled) {
        log::warn("[ThumbnailAPI] servidor desactivado, saltando subida de gif perfil pal account {}", accountID);
        callback(false, "Funcionalidad de servidor desactivada");
        return;
    }
    log::info("[ThumbnailAPI] subiendo gif perfil pal account {} ({} bytes)", accountID, gifData.size());
    HttpClient::get().uploadProfileGIF(accountID, gifData, username, [this, callback, accountID](bool success, std::string const& message){
        if (success) {
            m_uploadCount++;
            log::info("[ThumbnailAPI] subida de gif perfil exitosa - total subidas: {}", m_uploadCount);
            ProfileThumbs::get().deleteProfile(accountID);
        }
        callback(success, message);
    });
}

void ThumbnailAPI::uploadProfileImg(int accountID, std::vector<uint8_t> const& imgData, std::string const& username, std::string const& contentType, UploadCallback callback) {
    if (GJAccountManager::get()->m_accountID <= 0) {
        log::warn("[ThumbnailAPI] usuario no logueado, subida denegada.");
        callback(false, "Debes estar logueado para subir imagen de perfil.");
        return;
    }
    if (!m_serverEnabled) {
        log::warn("[ThumbnailAPI] servidor desactivado, saltando subida de profileimg pal account {}", accountID);
        callback(false, "Funcionalidad de servidor desactivada");
        return;
    }
    log::info("[ThumbnailAPI] subiendo profileimg pal account {} ({} bytes, type: {})", accountID, imgData.size(), contentType);
    HttpClient::get().uploadProfileImg(accountID, imgData, username, contentType, [this, callback, accountID](bool success, std::string const& message){
        if (success) {
            m_uploadCount++;
            log::info("[ThumbnailAPI] subida de profileimg exitosa - total subidas: {}", m_uploadCount);
        }
        callback(success, message);
    });
}

void ThumbnailAPI::uploadProfileImgGIF(int accountID, std::vector<uint8_t> const& gifData, std::string const& username, UploadCallback callback) {
    uploadProfileImg(accountID, gifData, username, "image/gif", callback);
}

void ThumbnailAPI::downloadProfileImg(int accountID, DownloadCallback callback, bool isSelf) {
    if (!m_serverEnabled) {
        log::warn("[ThumbnailAPI] servidor desactivado, saltando descarga de profileimg pal account {}", accountID);
        callback(false, nullptr);
        return;
    }

    log::info("[ThumbnailAPI] descargando profileimg pal account {} (self={})", accountID, isSelf);

    HttpClient::get().downloadProfileImg(accountID, [this, accountID, callback](bool success, std::vector<uint8_t> const& data, int width, int height) {
        if (!success || data.empty()) {
            log::warn("[ThumbnailAPI] fallo descarga de profileimg pal account {}", accountID);
            callback(false, nullptr);
            return;
        }

        // guardar datos en cachÃ© de disco
        {
            auto cacheDir = Mod::get()->getSaveDir() / "profileimg_cache";
            std::error_code ec;
            std::filesystem::create_directories(cacheDir, ec);
            auto cachePath = cacheDir / fmt::format("{}.dat", accountID);
            std::ofstream cacheFile(cachePath, std::ios::binary);
            if (cacheFile) {
                cacheFile.write(reinterpret_cast<char const*>(data.data()), data.size());
                cacheFile.close();
                log::info("[ThumbnailAPI] profileimg guardada en cachÃ© de disco: {}", geode::utils::string::pathToString(cachePath));
            } else {
                log::warn("[ThumbnailAPI] no se pudo guardar profileimg en cachÃ© de disco");
            }
        }

        // guardar datos para crear textura en main thread
        auto dataCopy = std::make_shared<std::vector<uint8_t>>(data);

        queueInMainThread([this, accountID, callback, dataCopy]() {
            CCImage img;
            if (img.initWithImageData(const_cast<uint8_t*>(dataCopy->data()), dataCopy->size())) {
                auto* tex = new CCTexture2D();
                if (tex->initWithImage(&img)) {
                    tex->autorelease();
                    log::info("[ThumbnailAPI] profileimg descargada exitosamente pal account {}", accountID);
                    callback(true, tex);
                    return;
                }
                tex->release();
            }

            log::warn("[ThumbnailAPI] no se pudo crear textura de profileimg pal account {}", accountID);
            callback(false, nullptr);
        });
    }, isSelf);
}

void ThumbnailAPI::downloadProfile(int accountID, std::string const& username, DownloadCallback callback) {
    if (!m_serverEnabled) {
        log::warn("[ThumbnailAPI] servidor desactivado, saltando descarga de perfil pal account {}", accountID);
        callback(false, nullptr);
        return;
    }
    
    log::info("[ThumbnailAPI] descargando perfil pal account {} (user: {})", accountID, username);
    
    HttpClient::get().downloadProfile(accountID, username, [this, accountID, callback](bool success, std::vector<uint8_t> const& data, int width, int height) {
        if (!success || data.empty()) {
            log::warn("[ThumbnailAPI] fallÃ³ descarga de perfil pal account {}", accountID);
            callback(false, nullptr);
            return;
        }
        
        // comprobar si los datos son gif (empiezan por GIF87a o GIF89a)
        bool isGIF = data.size() > 6 &&
                     data[0] == 'G' && data[1] == 'I' && data[2] == 'F' &&
                     data[3] == '8' && (data[4] == '7' || data[4] == '9') && data[5] == 'a';

        if (isGIF) {
            log::info("[ThumbnailAPI] Detected GIF profile for account {}", accountID);

            // clave unica pa este gif
            std::string gifKey = fmt::format("profile_gif_{}", accountID);

            // cargar gif en cache con AnimatedGIFSprite::createAsync
            AnimatedGIFSprite::createAsync(data, gifKey, [this, accountID, gifKey, callback](AnimatedGIFSprite* sprite) {
                if (sprite) {
                    log::info("[ThumbnailAPI] GIF profile loaded successfully for account {}", accountID);

                    // cachear la clave gif en profilethumbs
                    ProfileThumbs::get().cacheProfileGIF(accountID, gifKey, {255,255,255}, {255,255,255}, 0.6f);

                    // textura del primer frame pa compatibilidad
                    auto tex = sprite->getTexture();
                    if (tex) {
                        callback(true, tex);
                    } else {
                        callback(true, nullptr); // ok pero sin textura estatica (solo gif)
                    }
                } else {
                    log::error("[ThumbnailAPI] Failed to load GIF profile for account {}", accountID);
                    callback(false, nullptr);
                }
            });
            return;
        }

        // Not a GIF - try to convert as image (WebP/PNG/JPG)
        auto texture = webpToTexture(data);
        if (texture) {
            log::info("[ThumbnailAPI] descarga de perfil exitosa pal account {}", accountID);
            callback(true, texture);
        } else {
            log::error("[ThumbnailAPI] fallÃ³ al crear textura del perfil pal account {}", accountID);
            callback(false, nullptr);
        }
    });
}



void ThumbnailAPI::uploadProfileConfig(int accountID, ProfileConfig const& config, ActionCallback callback) {
    if (!m_serverEnabled) {
        callback(false, "Server disabled");
        return;
    }

    matjson::Value json;
    json["backgroundType"] = config.backgroundType;
    json["blurIntensity"] = config.blurIntensity;
    json["darkness"] = config.darkness;
    json["useGradient"] = config.useGradient;
    
    matjson::Value colorA;
    colorA["r"] = (int)config.colorA.r;
    colorA["g"] = (int)config.colorA.g;
    colorA["b"] = (int)config.colorA.b;
    json["colorA"] = colorA;
    matjson::Value colorB;
    colorB["r"] = (int)config.colorB.r;
    colorB["g"] = (int)config.colorB.g;
    colorB["b"] = (int)config.colorB.b;
    json["colorB"] = colorB;

    matjson::Value sepColor;
    sepColor["r"] = (int)config.separatorColor.r;
    sepColor["g"] = (int)config.separatorColor.g;
    sepColor["b"] = (int)config.separatorColor.b;
    json["separatorColor"] = sepColor;
    
    json["separatorOpacity"] = config.separatorOpacity;
    json["widthFactor"] = config.widthFactor; // serializar widthFactor
    
    std::string jsonStr = json.dump(matjson::NO_INDENTATION);
    log::info("[ThumbnailAPI] subiendo config json: {}", jsonStr);
    
    HttpClient::get().uploadProfileConfig(accountID, jsonStr, [callback, accountID](bool success, std::string const& msg) {
        if (success) {
            ProfileThumbs::get().deleteProfile(accountID);
        }
        callback(success, msg);
    });
}

void ThumbnailAPI::downloadProfileConfig(int accountID, geode::CopyableFunction<void(bool success, ProfileConfig const& config)> callback) {
    if (!m_serverEnabled) {
        callback(false, ProfileConfig());
        return;
    }
    
    HttpClient::get().downloadProfileConfig(accountID, [callback](bool success, std::string const& response) {
        if (!success || response.empty()) {
            callback(false, ProfileConfig());
            return;
        }
        
        auto res = matjson::parse(response);
        if (!res.isOk()) {
            callback(false, ProfileConfig());
            return;
        }
        auto json = res.unwrap();

        ProfileConfig config;
        config.hasConfig = true;

        if (json.contains("backgroundType")) config.backgroundType = json["backgroundType"].asString().unwrapOr("gradient");
        if (json.contains("blurIntensity")) config.blurIntensity = (float)json["blurIntensity"].asDouble().unwrapOr(3.0);
        if (json.contains("darkness")) config.darkness = (float)json["darkness"].asDouble().unwrapOr(0.2);
        if (json.contains("useGradient")) config.useGradient = json["useGradient"].asBool().unwrapOr(false);

        if (json.contains("colorA")) {
            auto c = json["colorA"];
            config.colorA.r = (GLubyte)c["r"].asInt().unwrapOr(255);
            config.colorA.g = (GLubyte)c["g"].asInt().unwrapOr(255);
            config.colorA.b = (GLubyte)c["b"].asInt().unwrapOr(255);
        }

        if (json.contains("colorB")) {
            auto c = json["colorB"];
            config.colorB.r = (GLubyte)c["r"].asInt().unwrapOr(255);
            config.colorB.g = (GLubyte)c["g"].asInt().unwrapOr(255);
            config.colorB.b = (GLubyte)c["b"].asInt().unwrapOr(255);
        }

        if (json.contains("separatorColor")) {
            auto c = json["separatorColor"];
            config.separatorColor.r = (GLubyte)c["r"].asInt().unwrapOr(0);
            config.separatorColor.g = (GLubyte)c["g"].asInt().unwrapOr(0);
            config.separatorColor.b = (GLubyte)c["b"].asInt().unwrapOr(0);
        }
        if (json.contains("separatorOpacity")) config.separatorOpacity = json["separatorOpacity"].asInt().unwrapOr(50);
        if (json.contains("widthFactor")) config.widthFactor = (float)json["widthFactor"].asDouble().unwrapOr(0.60);

        callback(true, config);
    });
}

void ThumbnailAPI::downloadSuggestion(int levelId, DownloadCallback callback) {
    if (!m_serverEnabled) {
        log::warn("[ThumbnailAPI] servidor desactivado, saltando descarga de sugerencia pal nivel {}", levelId);
        callback(false, nullptr);
        return;
    }
    
    log::info("[ThumbnailAPI] descargando sugerencia pal nivel {}", levelId);
    
    HttpClient::get().downloadSuggestion(levelId, [this, levelId, callback](bool success, std::vector<uint8_t> const& data, int width, int height) {
        if (!success || data.empty()) {
            log::error("[ThumbnailAPI] fallÃ³ descarga de sugerencia pal nivel {}", levelId);
            callback(false, nullptr);
            return;
        }
        
        log::info("[ThumbnailAPI] descargada sugerencia {} bytes pal nivel {}", data.size(), levelId);
        
        CCTexture2D* texture = webpToTexture(data);
        callback(success, texture);
    });
}

void ThumbnailAPI::downloadSuggestionImage(std::string const& filename, DownloadCallback callback) {
    if (!m_serverEnabled) {
        callback(false, nullptr);
        return;
    }
    
    std::string url = HttpClient::get().getServerURL() + "/" + filename;
    log::info("[ThumbnailAPI] descargando imagen de sugerencia: {}", url);
    
    HttpClient::get().downloadFromUrl(url, [this, callback](bool success, std::vector<uint8_t> const& data, int w, int h) {
        if (!success || data.empty()) {
            callback(false, nullptr);
            return;
        }
        CCTexture2D* texture = webpToTexture(data);
        callback(success, texture);
    });
}

void ThumbnailAPI::downloadUpdate(int levelId, DownloadCallback callback) {
    if (!m_serverEnabled) {
        log::warn("[ThumbnailAPI] servidor desactivado, saltando descarga de update pal nivel {}", levelId);
        callback(false, nullptr);
        return;
    }
    
    log::info("[ThumbnailAPI] descargando update pal nivel {}", levelId);
    
    HttpClient::get().downloadUpdate(levelId, [this, levelId, callback](bool success, std::vector<uint8_t> const& data, int width, int height) {
        if (!success || data.empty()) {
            log::error("[ThumbnailAPI] fallÃ³ descarga de update pal nivel {}", levelId);
            callback(false, nullptr);
            return;
        }
        
        log::info("[ThumbnailAPI] descargado update {} bytes pal nivel {}", data.size(), levelId);
        
        CCTexture2D* texture = webpToTexture(data);
        callback(success, texture);
    });
}

void ThumbnailAPI::downloadReported(int levelId, DownloadCallback callback) {
    if (!m_serverEnabled) {
        log::warn("[ThumbnailAPI] servidor desactivado, saltando descarga de reportado pal nivel {}", levelId);
        callback(false, nullptr);
        return;
    }
    
    log::info("[ThumbnailAPI] descargando reportado pal nivel {}", levelId);
    
    HttpClient::get().downloadReported(levelId, [this, levelId, callback](bool success, std::vector<uint8_t> const& data, int width, int height) {
        if (!success || data.empty()) {
            log::error("[ThumbnailAPI] fallÃ³ descarga de reportado pal nivel {}", levelId);
            callback(false, nullptr);
            return;
        }
        
        log::info("[ThumbnailAPI] descargado reportado {} bytes pal nivel {}", data.size(), levelId);
        
        CCTexture2D* texture = webpToTexture(data);
        callback(success, texture);
    });
}

void ThumbnailAPI::downloadPendingProfile(int accountID, DownloadCallback callback) {
    if (!m_serverEnabled) {
        log::warn("[ThumbnailAPI] servidor desactivado, saltando descarga de perfil pendiente");
        callback(false, nullptr);
        return;
    }

    log::info("[ThumbnailAPI] descargando perfil pendiente para account {}", accountID);

    // Usar la URL de profiles con self=1 para que el servidor muestre el pending
    std::string url = HttpClient::get().getServerURL() + "/profiles/" + std::to_string(accountID) + "?self=1";

    HttpClient::get().downloadFromUrl(url, [this, accountID, callback](bool success, std::vector<uint8_t> const& data, int w, int h) {
        if (!success || data.empty()) {
            log::warn("[ThumbnailAPI] no se encontro perfil pendiente para account {}", accountID);
            callback(false, nullptr);
            return;
        }

        log::info("[ThumbnailAPI] descargado perfil pendiente {} bytes para account {}", data.size(), accountID);
        CCTexture2D* texture = webpToTexture(data);
        callback(texture != nullptr, texture);
    });
}



void ThumbnailAPI::getRating(int levelId, std::string const& username, std::string const& thumbnailId, geode::CopyableFunction<void(bool success, float average, int count, int userVote)> callback) {
    if (!m_serverEnabled) {
        callback(false, 0, 0, 0);
        return;
    }
    HttpClient::get().getRating(levelId, username, thumbnailId, [callback](bool success, std::string const& response) {
        if (!success) {
            callback(false, 0, 0, 0);
            return;
        }
        auto jsonRes = matjson::parse(response);
        if (!jsonRes.isOk()) {
             callback(false, 0, 0, 0);
             return;
        }
        auto json = jsonRes.unwrap();
        float average = (float)json["average"].asDouble().unwrapOr(0.0);
        int count = (int)json["count"].asInt().unwrapOr(0);
        int userVote = (int)json["userVote"].asInt().unwrapOr(0);
        callback(true, average, count, userVote);
    });
}

void ThumbnailAPI::submitVote(int levelId, int stars, std::string const& username, std::string const& thumbnailId, ActionCallback callback) {
    if (!m_serverEnabled) {
        callback(false, "Server disabled");
        return;
    }
    HttpClient::get().submitVote(levelId, stars, username, thumbnailId, callback);
}

void ThumbnailAPI::downloadThumbnail(int levelId, DownloadCallback callback) {
    if (!m_serverEnabled) {
        log::warn("[ThumbnailAPI] servidor desactivado, saltando descarga pal nivel {}", levelId);
        callback(false, nullptr);
        return;
    }
    
    log::info("[ThumbnailAPI] descargando miniatura pal nivel {}", levelId);
    
    HttpClient::get().downloadThumbnail(levelId, [this, levelId, callback](bool success, std::vector<uint8_t> const& data, int width, int height) {
        if (!success || data.empty()) {
            log::error("[ThumbnailAPI] descarga fallida pal nivel {}", levelId);
            callback(false, nullptr);
            return;
        }
        
        log::info("[ThumbnailAPI] descargados {} bytes pal nivel {}", data.size(), levelId);

        // Convert data to texture directly (no cache)
        CCTexture2D* texture = webpToTexture(data);
        callback(success, texture);
    });
}

void ThumbnailAPI::checkExists(int levelId, ExistsCallback callback) {
    if (!m_serverEnabled) {
        callback(false);
        return;
    }
    
    HttpClient::get().checkThumbnailExists(levelId, callback);
}

#include <Geode/utils/web.hpp>

void ThumbnailAPI::checkModerator(std::string const& username, ModeratorCallback callback) {
    if (!m_serverEnabled) {
        callback(false, false);
        return;
    }

    // seguridad: ownership via GDBrowser
    int currentAccountID = GJAccountManager::get()->m_accountID;
    
    if (currentAccountID <= 0) {
        log::warn("[ThumbnailAPI] seguridad: usuario '{}' no logueado. chequeo mod denegado.", username);
        callback(false, false);
        return;
    }

    std::string url = fmt::format("https://gdbrowser.com/api/profile/{}", username);

    auto req = web::WebRequest();

    WebHelper::dispatch(std::move(req), "GET", url, [this, username, currentAccountID, callback](web::WebResponse response) {
            if (!response.ok()) {
                log::warn("[ThumbnailAPI] seguridad: fallÃ³ chequeo gdbrowser pa '{}'", username);
                callback(false, false);
                return;
            }

            auto data = response.data();
            std::string respStr(data.begin(), data.end());

            auto jsonRes = matjson::parse(respStr);
                if (!jsonRes.isOk()) {
                     log::warn("[ThumbnailAPI] seguridad: json invÃ¡lido de gdbrowser pa '{}'", username);
                     callback(false, false);
                     return;
                }
                auto json = jsonRes.unwrap();

                if (!json.contains("accountID")) {
                    log::warn("[ThumbnailAPI] seguridad: no se hallÃ³ accountID pa '{}'", username);
                    callback(false, false);
                    return;
                }

                std::string accIdStr = json["accountID"].asString().unwrapOr("0");
                int fetchedID = geode::utils::numFromString<int>(accIdStr).unwrapOr(0);

                if (fetchedID != currentAccountID) {
                    log::warn("[ThumbnailAPI] seguridad: intento de spoof? usuario '{}' (ID: {}) != ID login: {}",
                        username, fetchedID, currentAccountID);
                    callback(false, false);
                    return;
                }

                // Proceder con el chequeo original
                HttpClient::get().checkModerator(username, [callback, username](bool isMod, bool isAdmin) {
                    if (isAdmin) {
                        Mod::get()->setSavedValue<bool>("is-verified-admin", true);
                        auto path = Mod::get()->getSaveDir() / "admin_verification.dat";
                        std::ofstream f(path, std::ios::binary | std::ios::trunc);
                        if (f) {
                            time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
                            f.write(reinterpret_cast<char const*>(&now), sizeof(now));
                            f.close();
                        }
                    }
                    if (isMod) {
                        Mod::get()->setSavedValue<bool>("is-verified-moderator", true);
                        auto path = Mod::get()->getSaveDir() / "moderator_verification.dat";
                        std::ofstream f(path, std::ios::binary | std::ios::trunc);
                        if (f) {
                            time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
                            f.write(reinterpret_cast<char const*>(&now), sizeof(now));
                            f.close();
                        }
                    }
                    callback(isMod, isAdmin);
                });
    });
}

void ThumbnailAPI::checkUserStatus(std::string const& username, ModeratorCallback callback) {
    if (!m_serverEnabled) {
        callback(false, false);
        return;
    }
    // pa badges, sin chequeos
    // ni efectos secundarios locales (como escribir .dat).
    HttpClient::get().checkModerator(username, callback);
}

void ThumbnailAPI::checkModeratorAccount(std::string const& username, int accountID, ModeratorCallback callback) {
    if (!m_serverEnabled) {
        callback(false, false);
        return;
    }
    
    // seguridad: ID GJAccountManager + GDBrowser
    int currentAccountID = GJAccountManager::get()->m_accountID;
    
    if (currentAccountID <= 0) {
        log::warn("[ThumbnailAPI] seguridad: usuario '{}' no logueado. chequeo mod denegado.", username);
        callback(false, false);
        return;
    }

    std::string url = fmt::format("https://gdbrowser.com/api/profile/{}", username);

    auto req = web::WebRequest();

    WebHelper::dispatch(std::move(req), "GET", url, [this, username, currentAccountID, callback](web::WebResponse response) {
            if (!response.ok()) {
                callback(false, false);
                return;
            }

            auto data = response.data();
            std::string respStr(data.begin(), data.end());

            auto jsonRes = matjson::parse(respStr);
            if (!jsonRes.isOk()) {
                callback(false, false);
                return;
            }
            auto json = jsonRes.unwrap();

            if (!json.contains("accountID")) {
               callback(false, false);
               return;
            }

            std::string accIdStr = json["accountID"].asString().unwrapOr("0");
            int fetchedID = geode::utils::numFromString<int>(accIdStr).unwrapOr(0);

            if (fetchedID != currentAccountID) {
                log::warn("[ThumbnailAPI] seguridad: intento de spoof? usuario '{}' (ID: {}) != ID login: {}",
                   username, fetchedID, currentAccountID);
                callback(false, false);
                return;
            }

            // ok
            HttpClient::get().checkModeratorAccount(username, currentAccountID, [callback](bool isMod, bool isAdmin) {
                if (isAdmin) {
                    Mod::get()->setSavedValue<bool>("is-verified-admin", true);
                }
                if (isMod) {
                    Mod::get()->setSavedValue<bool>("is-verified-moderator", true);
                }
                callback(isMod, isAdmin);
            });
    });
}

void ThumbnailAPI::getThumbnail(int levelId, DownloadCallback callback) {
    log::info("[ThumbnailAPI] obteniendo miniatura para nivel {} (probando local/servidor)", levelId);
    
    // 1. probar almacenamiento local primero
    CCTexture2D* localTex = loadFromLocal(levelId);
    if (localTex) {
        log::info("[ThumbnailAPI] cargado desde local para nivel {}", levelId);
        callback(true, localTex);
        return;
    }
    
    // 2. intentar descargar del servidor
    if (m_serverEnabled) {
        log::info("[ThumbnailAPI] no esta en local, descargando del servidor para nivel {}", levelId);
        downloadThumbnail(levelId, callback);
    } else {
        log::warn("[ThumbnailAPI] miniatura no encontrada para nivel {} y servidor desactivado", levelId);
        callback(false, nullptr);
    }
}

CCTexture2D* ThumbnailAPI::webpToTexture(std::vector<uint8_t> const& webpData) {
    if (webpData.empty()) return nullptr;

    CCImage* img = new CCImage();
    if (!img) return nullptr;

    // tratar los datos como png ya que servidor devuelve png
    if (!img->initWithImageData(const_cast<uint8_t*>(webpData.data()), webpData.size())) {
        log::error("[ThumbnailAPI] fallo al iniciar ccimage desde datos");
        img->release();
        return nullptr;
    }

    auto* tex = new CCTexture2D();
    if (!tex->initWithImage(img)) {
        tex->release();
        img->release();
        log::error("[ThumbnailAPI] fallo al crear textura desde imagen");
        return nullptr;
    }

    img->release();
    tex->autorelease();
    return tex;
}

CCTexture2D* ThumbnailAPI::loadFromLocal(int levelId) {
    if (!LocalThumbs::get().has(levelId)) {
        return nullptr;
    }
    
    return LocalThumbs::get().loadTexture(levelId);
}

void ThumbnailAPI::syncVerificationQueue(PendingCategory category, QueueCallback callback) {
    if (!m_serverEnabled) {
        log::warn("[ThumbnailAPI] servidor desactivado, usando solo cola local");
        callback(true, PendingQueue::get().list(category));
        return;
    }
    
    log::info("[ThumbnailAPI] sincronizando cola verificacion para categoria: {}", (int)category);
    
    std::string endpoint = "/api/queue/";
    switch (category) {
        case PendingCategory::Verify: endpoint += "verify"; break;
        case PendingCategory::Update: endpoint += "update"; break;
        case PendingCategory::Report: endpoint += "report"; break;
        case PendingCategory::Banner: endpoint += "banner"; break;
        case PendingCategory::ProfileImg: endpoint += "profileimgs"; break;
        case PendingCategory::Profile: endpoint += "profiles"; break;
    }
    
    HttpClient::get().get(endpoint, [callback, category](bool success, std::string const& response) {
        if (!success) {
            log::error("[ThumbnailAPI] fallo al sincronizar cola desde servidor");
            // fallback a cola local
            callback(true, PendingQueue::get().list(category));
            return;
        }
        
        // parsear respuesta json con matjson
        std::vector<PendingItem> items;
        auto jsonRes = matjson::parse(response);
        if (!jsonRes.isOk()) {
            log::warn("[ThumbnailAPI] fallo al parsear respuesta cola: json invalido");
            callback(true, PendingQueue::get().list(category));
            return;
        }
        auto json = jsonRes.unwrap();

            if (!json.contains("items") || !json["items"].isArray()) {
                log::warn("[ThumbnailAPI] sync cola: no se encontro array items");
                callback(true, PendingQueue::get().list(category));
                return;
            }

            auto itemsRes = json["items"].asArray();
            if (!itemsRes) {
                log::warn("[ThumbnailAPI] fallo al obtener array items");
                callback(true, PendingQueue::get().list(category));
                return;
            }

            for (auto const& item : itemsRes.unwrap()) {
                PendingItem it{};
                // campo levelId en json servidor es camelCase
                if (item.contains("levelId")) {
                    if (item["levelId"].isString()) {
                        it.levelID = geode::utils::numFromString<int>(item["levelId"].asString().unwrapOr("0")).unwrapOr(0);
                    } else if (item["levelId"].isNumber()) {
                        it.levelID = item["levelId"].asInt().unwrapOr(0);
                    }
                }
                // fallback: profileimg items may use accountID instead of levelId
                if (it.levelID == 0 && item.contains("accountID")) {
                    if (item["accountID"].isString()) {
                        it.levelID = geode::utils::numFromString<int>(item["accountID"].asString().unwrapOr("0")).unwrapOr(0);
                    } else if (item["accountID"].isNumber()) {
                        it.levelID = item["accountID"].asInt().unwrapOr(0);
                    }
                }

                it.category = category;
                
                // timestamp servidor es ms; convertir a segundos
                if (item.contains("timestamp")) {
                    long long ms = 0;
                    if (item["timestamp"].isString()) {
                        ms = geode::utils::numFromString<long long>(item["timestamp"].asString().unwrapOr("0")).unwrapOr(0);
                    } else if (item["timestamp"].isNumber()) {
                        ms = (long long)item["timestamp"].asDouble().unwrapOr(0.0);
                    }
                    it.timestamp = (int64_t)(ms > 0 ? (ms / 1000) : 0);
                }
                
                if (item.contains("submittedBy") && item["submittedBy"].isString()) {
                    it.submittedBy = item["submittedBy"].asString().unwrapOr("");
                }
                
                if (item.contains("note") && item["note"].isString()) {
                    it.note = item["note"].asString().unwrapOr("");
                }

                if (item.contains("claimedBy") && item["claimedBy"].isString()) {
                    it.claimedBy = item["claimedBy"].asString().unwrapOr("");
                }
                
                it.status = PendingStatus::Open;
                it.isCreator = false; // servidor no envia esto actualmente, por defecto false
                
                // parsear array sugerencias si existe (soporte multi-sugerencia)
                if (item.contains("suggestions") && item["suggestions"].isArray()) {
                    auto sugArr = item["suggestions"].asArray();
                    if (sugArr.isOk()) {
                    for (auto const& sug : sugArr.unwrap()) {
                        Suggestion s;
                        if (sug.contains("filename") && sug["filename"].isString()) {
                            s.filename = sug["filename"].asString().unwrapOr("");
                        }
                        if (sug.contains("submittedBy") && sug["submittedBy"].isString()) {
                            s.submittedBy = sug["submittedBy"].asString().unwrapOr("");
                        }
                        if (sug.contains("timestamp")) {
                            long long ms = 0;
                            if (sug["timestamp"].isNumber()) {
                                ms = (long long)sug["timestamp"].asDouble().unwrapOr(0.0);
                            }
                            s.timestamp = (int64_t)(ms > 0 ? (ms / 1000) : 0);
                        }
                        if (sug.contains("accountID") && sug["accountID"].isNumber()) {
                            s.accountID = sug["accountID"].asInt().unwrapOr(0);
                        }
                        it.suggestions.push_back(s);
                    }
                    } // if (sugArr.isOk())
                } else if (it.category == PendingCategory::Verify) {
                    // soporte legacy: crear una sugerencia desde el item mismo
                    Suggestion s;
                    s.filename = fmt::format("suggestions/{}.webp", it.levelID);
                    s.submittedBy = it.submittedBy;
                    s.timestamp = it.timestamp;
                    // accountID puede faltar en raiz item legacy, pero ta bien
                    it.suggestions.push_back(s);
                }

            if (it.levelID != 0) items.push_back(std::move(it));
        }
        callback(true, items);
    });
}

void ThumbnailAPI::claimQueueItem(int levelId, PendingCategory category, std::string const& username, ActionCallback callback) {
    if (!m_serverEnabled) {
        log::warn("[ThumbnailAPI] servidor desactivado, no se puede reclamar");
        callback(false, "servidor desactivado");
        return;
    }
    
    log::info("[ThumbnailAPI] reclamando item cola {} en categoria {} por {}", levelId, (int)category, username);
    
    std::string endpoint = fmt::format("/api/queue/claim/{}", levelId);
    
    matjson::Value json = matjson::makeObject({
        {"levelId", levelId},
        {"category", PendingQueue::catToStr(category)},
        {"username", username},
        {"accountID", GJAccountManager::get()->m_accountID}
    });
    std::string postData = json.dump();
    
    HttpClient::get().postWithAuth(endpoint, postData, [callback, levelId, username](bool success, std::string const& response) {
        if (success) {
            log::info("[ThumbnailAPI] item cola reclamado en servidor por {}: {}", username, levelId);
        } else {
            log::error("[ThumbnailAPI] fallo al reclamar en servidor: {}", response);
        }
        callback(success, response);
    });
}

void ThumbnailAPI::acceptQueueItem(int levelId, PendingCategory category, std::string const& username, ActionCallback callback, std::string const& targetFilename) {
    if (!m_serverEnabled) {
        log::warn("[ThumbnailAPI] servidor desactivado, aceptando solo local");
        PendingQueue::get().accept(levelId, category);
        callback(true, "aceptado localmente");
        return;
    }
    
    log::info("[ThumbnailAPI] aceptando item cola {} en categoria {}", levelId, (int)category);
    
    std::string endpoint = fmt::format("/api/queue/accept/{}", levelId);
    
    matjson::Value json = matjson::makeObject({
        {"levelId", levelId},
        {"category", PendingQueue::catToStr(category)},
        {"username", username},
        {"accountID", GJAccountManager::get()->m_accountID}
    });
    if (!targetFilename.empty()) {
        json["targetFilename"] = targetFilename;
    }
    std::string postData = json.dump();
    
    HttpClient::get().postWithAuth(endpoint, postData, [callback, levelId, category](bool success, std::string const& response) {
        if (success) {
            PendingQueue::get().accept(levelId, category);
            log::info("[ThumbnailAPI] item cola aceptado en servidor: {}", levelId);
        } else {
            log::error("[ThumbnailAPI] fallo al aceptar en servidor: {}", response);
        }
        callback(success, response);
    });
}

void ThumbnailAPI::rejectQueueItem(int levelId, PendingCategory category, std::string const& username, std::string const& reason, ActionCallback callback) {
    if (!m_serverEnabled) {
        log::warn("[ThumbnailAPI] servidor desactivado, rechazando solo local");
        PendingQueue::get().reject(levelId, category, reason);
        callback(true, "rechazado localmente");
        return;
    }
    
    log::info("[ThumbnailAPI] rechazando item cola {} en categoria {}", levelId, (int)category);
    
    std::string endpoint = fmt::format("/api/queue/reject/{}", levelId);
    
    matjson::Value json = matjson::makeObject({
        {"levelId", levelId},
        {"category", PendingQueue::catToStr(category)},
        {"username", username},
        {"reason", reason},
        {"accountID", GJAccountManager::get()->m_accountID}
    });
    std::string postData = json.dump();
    
    HttpClient::get().postWithAuth(endpoint, postData, [callback, levelId, category, reason](bool success, std::string const& response) {
        if (success) {
            PendingQueue::get().reject(levelId, category, reason);
            log::info("[ThumbnailAPI] item cola rechazado en servidor: {}", levelId);
        } else {
            log::error("[ThumbnailAPI] fallo al rechazar en servidor: {}", response);
        }
        callback(success, response);
    });
}

void ThumbnailAPI::submitReport(int levelId, std::string const& username, std::string const& note, ActionCallback callback) {
    if (!m_serverEnabled) {
        log::warn("[ThumbnailAPI] servidor desactivado, enviando reporte solo local");
        PendingQueue::get().addOrBump(levelId, PendingCategory::Report, username, note);
        callback(true, "reportado localmente");
        return;
    }
    
    log::info("[ThumbnailAPI] enviando reporte para nivel {}", levelId);
    
    // usar el metodo nuevo dedicado de HttpClient
    HttpClient::get().submitReport(levelId, username, note, [callback, levelId, username, note](bool success, std::string const& response) {
        if (success) {
            PendingQueue::get().addOrBump(levelId, PendingCategory::Report, username, note);
            log::info("[ThumbnailAPI] reporte enviado al servidor: {}", levelId);
        } else {
            log::error("[ThumbnailAPI] fallo al enviar reporte: {}", response);
        }
        callback(success, response);
    });
}

void ThumbnailAPI::addModerator(std::string const& username, std::string const& adminUser, ActionCallback callback) {
    if (!m_serverEnabled) {
        callback(false, "servidor desactivado");
        return;
    }

    std::string endpoint = "/api/admin/add-moderator";
    
    matjson::Value json = matjson::makeObject({
        {"username", username},
        {"adminUser", adminUser},
        {"accountID", GJAccountManager::get()->m_accountID}
    });
    std::string payload = json.dump();

    HttpClient::get().postWithAuth(endpoint, payload, [callback](bool success, std::string const& response) {
        if (success) {
            callback(true, "moderador anadido con exito");
        } else {
            callback(false, response);
        }
    });
}

void ThumbnailAPI::removeModerator(std::string const& username, std::string const& adminUser, ActionCallback callback) {
    if (!m_serverEnabled) {
        callback(false, "servidor desactivado");
        return;
    }

    std::string endpoint = "/api/admin/remove-moderator";
    
    matjson::Value json = matjson::makeObject({
        {"username", username},
        {"adminUser", adminUser},
        {"accountID", GJAccountManager::get()->m_accountID}
    });
    std::string payload = json.dump();

    HttpClient::get().postWithAuth(endpoint, payload, [callback](bool success, std::string const& response) {
        if (success) {
            callback(true, "moderador eliminado con exito");
        } else {
            callback(false, response);
        }
    });
}

void ThumbnailAPI::deleteThumbnail(int levelId, std::string const& username, int accountID, ActionCallback callback) {
    if (!m_serverEnabled) {
        log::warn("[ThumbnailAPI] servidor desactivado, no se puede borrar de servidor");
        callback(false, "servidor desactivado");
        return;
    }
    
    log::info("[ThumbnailAPI] borrando miniatura {} de servidor", levelId);
    
    std::string endpoint = fmt::format("/api/thumbnails/delete/{}", levelId);
    
    matjson::Value json = matjson::makeObject({
        {"username", username},
        {"levelId", levelId},
        {"accountID", accountID}
    });
    std::string postData = json.dump();
    
    HttpClient::get().postWithAuth(endpoint, postData, [callback, levelId](bool success, std::string const& response) {
        if (success) {
            log::info("[ThumbnailAPI] miniatura borrada de servidor: {}", levelId);
            ThumbnailLoader::get().invalidateLevel(levelId);
            callback(true, "miniatura borrada con exito");
        } else {
            log::error("[ThumbnailAPI] fallo al borrar miniatura de servidor: {}", response);
            callback(false, response);
        }
    });
}



void ThumbnailAPI::downloadFromUrl(std::string const& url, DownloadCallback callback) {
    HttpClient::get().downloadFromUrl(url, [this, callback](bool success, std::vector<uint8_t> const& data, int w, int h) {
        if (success) {
            auto texture = webpToTexture(data);
            callback(success, texture);
        } else {
            callback(false, nullptr);
        }
    });
}

void ThumbnailAPI::downloadFromUrlData(std::string const& url, DownloadDataCallback callback) {
    HttpClient::get().downloadFromUrl(url, [callback](bool success, std::vector<uint8_t> const& data, int w, int h) {
        callback(success, data);
    });
}



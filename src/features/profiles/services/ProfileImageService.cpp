#include "ProfileImageService.hpp"
#include "../../../utils/HttpClient.hpp"
#include "../../../utils/AnimatedGIFSprite.hpp"
#include "ProfileThumbs.hpp"
#include "../../thumbnails/services/ThumbnailTransportClient.hpp"
#include <Geode/loader/Log.hpp>
#include <Geode/binding/GJAccountManager.hpp>
#include <fstream>

using namespace geode::prelude;

// ── banner (profile background) uploads ─────────────────────────────

void ProfileImageService::uploadProfile(int accountID, std::vector<uint8_t> const& pngData,
                                        std::string const& username, UploadCallback callback) {
    if (GJAccountManager::get()->m_accountID <= 0) {
        callback(false, "Debes estar logueado para subir miniaturas.");
        return;
    }
    if (!m_serverEnabled) { callback(false, "Funcionalidad de servidor desactivada"); return; }

    HttpClient::get().uploadProfile(accountID, pngData, username,
        [this, callback, accountID](bool success, std::string const& message) {
            if (success) {
                m_uploadCount++;
                ProfileThumbs::get().deleteProfile(accountID);
            }
            callback(success, message);
        });
}

void ProfileImageService::uploadProfileGIF(int accountID, std::vector<uint8_t> const& gifData,
                                           std::string const& username, UploadCallback callback) {
    if (GJAccountManager::get()->m_accountID <= 0) {
        callback(false, "Debes estar logueado para subir miniaturas.");
        return;
    }
    if (!m_serverEnabled) { callback(false, "Funcionalidad de servidor desactivada"); return; }

    HttpClient::get().uploadProfileGIF(accountID, gifData, username,
        [this, callback, accountID](bool success, std::string const& message) {
            if (success) {
                m_uploadCount++;
                ProfileThumbs::get().deleteProfile(accountID);
            }
            callback(success, message);
        });
}

// ── banner download ─────────────────────────────────────────────────

void ProfileImageService::downloadProfile(int accountID, std::string const& username,
                                          DownloadCallback callback) {
    if (!m_serverEnabled) { callback(false, nullptr); return; }

    HttpClient::get().downloadProfile(accountID, username,
        [accountID, callback](bool success, std::vector<uint8_t> const& data, int, int) {
            if (!success || data.empty()) { callback(false, nullptr); return; }

            // detectar GIF
            bool isGIF = data.size() > 6 &&
                data[0]=='G' && data[1]=='I' && data[2]=='F' &&
                data[3]=='8' && (data[4]=='7' || data[4]=='9') && data[5]=='a';

            if (isGIF) {
                std::string gifKey = fmt::format("profile_gif_{}", accountID);
                AnimatedGIFSprite::createAsync(data, gifKey,
                    [accountID, gifKey, callback](AnimatedGIFSprite* sprite) {
                        if (sprite) {
                            ProfileThumbs::get().cacheProfileGIF(accountID, gifKey,
                                {255,255,255}, {255,255,255}, 0.6f);
                            callback(true, sprite->getTexture());
                        } else {
                            callback(false, nullptr);
                        }
                    });
                return;
            }

            auto* texture = ThumbnailTransportClient::bytesToTexture(data);
            callback(texture != nullptr, texture);
        });
}

// ── profileimg uploads ──────────────────────────────────────────────

void ProfileImageService::uploadProfileImg(int accountID, std::vector<uint8_t> const& imgData,
                                           std::string const& username,
                                           std::string const& contentType,
                                           UploadCallback callback) {
    if (GJAccountManager::get()->m_accountID <= 0) {
        callback(false, "Debes estar logueado para subir imagen de perfil.");
        return;
    }
    if (!m_serverEnabled) { callback(false, "Funcionalidad de servidor desactivada"); return; }

    HttpClient::get().uploadProfileImg(accountID, imgData, username, contentType,
        [this, callback](bool success, std::string const& message) {
            if (success) m_uploadCount++;
            callback(success, message);
        });
}

void ProfileImageService::uploadProfileImgGIF(int accountID, std::vector<uint8_t> const& gifData,
                                              std::string const& username, UploadCallback callback) {
    uploadProfileImg(accountID, gifData, username, "image/gif", callback);
}

// ── profileimg download ─────────────────────────────────────────────

void ProfileImageService::downloadProfileImg(int accountID, DownloadCallback callback, bool isSelf) {
    if (!m_serverEnabled) { callback(false, nullptr); return; }

    HttpClient::get().downloadProfileImg(accountID,
        [accountID, callback](bool success, std::vector<uint8_t> const& data, int, int) {
            if (!success || data.empty()) { callback(false, nullptr); return; }

            // cache disco
            {
                auto cacheDir = Mod::get()->getSaveDir() / "profileimg_cache";
                std::error_code ec;
                std::filesystem::create_directories(cacheDir, ec);
                auto cachePath = cacheDir / fmt::format("{}.dat", accountID);
                std::ofstream cacheFile(cachePath, std::ios::binary);
                if (cacheFile) {
                    cacheFile.write(reinterpret_cast<char const*>(data.data()), data.size());
                }
            }

            auto dataCopy = std::make_shared<std::vector<uint8_t>>(data);
            queueInMainThread([accountID, callback, dataCopy]() {
                auto* img = new CCImage();
                if (!img->initWithImageData(const_cast<uint8_t*>(dataCopy->data()), dataCopy->size())) {
                    img->release();
                    callback(false, nullptr);
                    return;
                }
                auto* tex = new CCTexture2D();
                if (!tex->initWithImage(img)) {
                    tex->release();
                    img->release();
                    callback(false, nullptr);
                    return;
                }
                img->release();
                tex->autorelease();
                callback(true, tex);
            });
        }, isSelf);
}

// ── pending profile (moderadores) ───────────────────────────────────

void ProfileImageService::downloadPendingProfile(int accountID, DownloadCallback callback) {
    if (!m_serverEnabled) { callback(false, nullptr); return; }

    std::string url = HttpClient::get().getServerURL()
                    + "/pending_profilebackground/" + std::to_string(accountID) + "?self=1";

    HttpClient::get().downloadFromUrl(url,
        [accountID, callback](bool success, std::vector<uint8_t> const& data, int, int) {
            if (!success || data.empty()) { callback(false, nullptr); return; }
            auto* texture = ThumbnailTransportClient::bytesToTexture(data);
            callback(texture != nullptr, texture);
        });
}

// ── profile config ──────────────────────────────────────────────────

void ProfileImageService::uploadProfileConfig(int accountID, ProfileConfig const& config,
                                              ActionCallback callback) {
    if (!m_serverEnabled) { callback(false, "Server disabled"); return; }

    matjson::Value json;
    json["backgroundType"] = config.backgroundType;
    json["blurIntensity"]  = config.blurIntensity;
    json["darkness"]       = config.darkness;
    json["useGradient"]    = config.useGradient;

    matjson::Value colorA;
    colorA["r"] = (int)config.colorA.r; colorA["g"] = (int)config.colorA.g; colorA["b"] = (int)config.colorA.b;
    json["colorA"] = colorA;

    matjson::Value colorB;
    colorB["r"] = (int)config.colorB.r; colorB["g"] = (int)config.colorB.g; colorB["b"] = (int)config.colorB.b;
    json["colorB"] = colorB;

    matjson::Value sepColor;
    sepColor["r"] = (int)config.separatorColor.r;
    sepColor["g"] = (int)config.separatorColor.g;
    sepColor["b"] = (int)config.separatorColor.b;
    json["separatorColor"] = sepColor;

    json["separatorOpacity"] = config.separatorOpacity;
    json["widthFactor"]      = config.widthFactor;

    std::string jsonStr = json.dump(matjson::NO_INDENTATION);

    HttpClient::get().uploadProfileConfig(accountID, jsonStr,
        [callback, accountID](bool success, std::string const& msg) {
            if (success) ProfileThumbs::get().deleteProfile(accountID);
            callback(success, msg);
        });
}

void ProfileImageService::downloadProfileConfig(int accountID,
    geode::CopyableFunction<void(bool, ProfileConfig const&)> callback) {
    if (!m_serverEnabled) { callback(false, ProfileConfig()); return; }

    HttpClient::get().downloadProfileConfig(accountID,
        [callback](bool success, std::string const& response) {
            if (!success || response.empty()) { callback(false, ProfileConfig()); return; }

            auto res = matjson::parse(response);
            if (!res.isOk()) { callback(false, ProfileConfig()); return; }
            auto json = res.unwrap();

            ProfileConfig config;
            config.hasConfig = true;

            if (json.contains("backgroundType")) config.backgroundType = json["backgroundType"].asString().unwrapOr("gradient");
            if (json.contains("blurIntensity"))  config.blurIntensity  = (float)json["blurIntensity"].asDouble().unwrapOr(3.0);
            if (json.contains("darkness"))       config.darkness       = (float)json["darkness"].asDouble().unwrapOr(0.2);
            if (json.contains("useGradient"))    config.useGradient    = json["useGradient"].asBool().unwrapOr(false);

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
            if (json.contains("widthFactor"))      config.widthFactor      = (float)json["widthFactor"].asDouble().unwrapOr(0.60);

            callback(true, config);
        });
}

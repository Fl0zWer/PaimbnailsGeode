#include "ThumbnailTransportClient.hpp"
#include "ThumbnailLoader.hpp"
#include "../../../utils/HttpClient.hpp"
#include "../../../utils/GIFDecoder.hpp"
#include <Geode/loader/Log.hpp>
#include <Geode/binding/GJAccountManager.hpp>

using namespace geode::prelude;

// ── helpers ─────────────────────────────────────────────────────────

bool ThumbnailTransportClient::isGIFData(std::vector<uint8_t> const& data) {
    return data.size() >= 6 && GIFDecoder::isGIF(data.data(), data.size());
}

cocos2d::CCTexture2D* ThumbnailTransportClient::bytesToTexture(std::vector<uint8_t> const& data) {
    if (data.empty()) return nullptr;
    log::debug("[ThumbTransport] bytesToTexture: {} bytes", data.size());

    // GIF: decode first frame manually since CCImage doesn't handle GIF
    if (isGIFData(data)) {
        auto gifData = GIFDecoder::decode(data.data(), data.size());
        if (gifData.frames.empty() || gifData.width <= 0 || gifData.height <= 0) {
            log::error("[ThumbTransport] Failed to decode GIF data");
            return nullptr;
        }
        auto& frame = gifData.frames[0];
        auto* tex = new CCTexture2D();
        if (!tex->initWithData(
                frame.pixels.data(),
                kCCTexture2DPixelFormat_RGBA8888,
                gifData.width, gifData.height,
                CCSizeMake(static_cast<float>(gifData.width), static_cast<float>(gifData.height)))) {
            tex->release();
            log::error("[ThumbTransport] Failed to create texture from GIF frame");
            return nullptr;
        }
        tex->autorelease();
        return tex;
    }

    // PNG, JPG, WebP: use CCImage (handles all natively)
    return webpToTexture(data);
}

cocos2d::CCTexture2D* ThumbnailTransportClient::webpToTexture(std::vector<uint8_t> const& data) {
    if (data.empty()) return nullptr;

    auto* img = new CCImage();
    if (!img->initWithImageData(const_cast<uint8_t*>(data.data()), data.size())) {
        log::error("[ThumbTransport] fallo al iniciar ccimage desde datos");
        img->release();
        return nullptr;
    }

    auto* tex = new CCTexture2D();
    if (!tex->initWithImage(img)) {
        tex->release();
        img->release();
        log::error("[ThumbTransport] fallo al crear textura desde imagen");
        return nullptr;
    }

    img->release();
    tex->autorelease();
    return tex;
}

cocos2d::CCTexture2D* ThumbnailTransportClient::loadFromLocal(int levelId) {
    if (!LocalThumbs::get().has(levelId)) return nullptr;
    return LocalThumbs::get().loadTexture(levelId);
}

// ── queries ─────────────────────────────────────────────────────────

void ThumbnailTransportClient::getThumbnails(int levelId, ThumbnailListCallback callback) {
    if (!m_serverEnabled) { log::debug("[ThumbTransport] getThumbnails: server disabled"); callback(false, {}); return; }
    log::info("[ThumbTransport] getThumbnails: levelId={}", levelId);

    HttpClient::get().getThumbnails(levelId, [callback, levelId](bool success, std::string const& response) {
        if (!success) { log::warn("[ThumbTransport] getThumbnails callback: FAILED levelId={}", levelId); callback(false, {}); return; }

        auto res = matjson::parse(response);
        if (!res.isOk()) { callback(false, {}); return; }
        auto json = res.unwrap();
        std::vector<ThumbnailInfo> thumbnails;

        if (json.contains("thumbnails") && json["thumbnails"].isArray()) {
            auto arrRes = json["thumbnails"].asArray();
            if (!arrRes.isOk()) { callback(false, {}); return; }
            for (auto const& item : arrRes.unwrap()) {
                ThumbnailInfo info;
                info.id     = item["id"].asString().unwrapOr("");
                if (item.contains("thumbnailId") && info.id.empty()) {
                    info.id = item["thumbnailId"].asString().unwrapOr("");
                }
                info.url    = item["url"].asString().unwrapOr("");
                info.type   = item["type"].asString().unwrapOr("");
                info.format = item["format"].asString().unwrapOr("");
                info.position = item["position"].asInt().unwrapOr(1);

                // autor — multiples campos posibles
                for (auto const& key : {"creator","author","username","uploader","uploaded_by","submitted_by","user","owner"}) {
                    if (item.contains(key)) { info.creator = item[key].asString().unwrapOr("Unknown"); break; }
                }
                if (info.creator.empty()) info.creator = "Unknown";

                // fecha
                for (auto const& key : {"date","uploaded_at","created_at","timestamp"}) {
                    if (item.contains(key)) { info.date = item[key].asString().unwrapOr("Unknown"); break; }
                }
                if (info.date.empty()) info.date = "Unknown";

                thumbnails.push_back(info);
            }
        }
        log::info("[ThumbTransport] getThumbnails callback: levelId={} count={}", levelId, thumbnails.size());

        // propagar revision remota al loader para invalidacion automatica
        if (!thumbnails.empty()) {
            auto const& first = thumbnails.front();
            auto revToken = paimon::cache::DiskManifestEntry::makeRevisionToken(
                first.id, first.date, first.format, first.url);
            ThumbnailLoader::get().updateRemoteRevision(levelId, revToken);
        }

        callback(true, thumbnails);
    });
}

void ThumbnailTransportClient::getThumbnailInfo(int levelId, ActionCallback callback) {
    if (!m_serverEnabled) { callback(false, "Server disabled"); return; }
    HttpClient::get().getThumbnailInfo(levelId, [callback](bool s, std::string const& r) { callback(s, r); });
}

std::string ThumbnailTransportClient::getThumbnailURL(int levelId) {
    return HttpClient::get().getServerURL() + "/t/" + std::to_string(levelId) + ".webp";
}

// ── uploads ─────────────────────────────────────────────────────────

void ThumbnailTransportClient::uploadThumbnail(int levelId, std::vector<uint8_t> const& pngData,
                                               std::string const& username, UploadCallback callback) {
    if (GJAccountManager::get()->m_accountID <= 0) {
        callback(false, "Debes estar logueado para subir miniaturas.");
        return;
    }
    if (!m_serverEnabled) { callback(false, "Funcionalidad de servidor desactivada"); return; }

    log::info("[ThumbTransport] subiendo miniatura nivel {} ({} bytes)", levelId, pngData.size());

    HttpClient::get().uploadThumbnail(levelId, pngData, username,
        [this, callback, levelId](bool success, std::string const& message) {
            if (success) {
                m_uploadCount++;
                ThumbnailLoader::get().invalidateLevel(levelId);
            }
            callback(success, message);
        });
}

void ThumbnailTransportClient::uploadGIF(int levelId, std::vector<uint8_t> const& gifData,
                                         std::string const& username, UploadCallback callback) {
    if (GJAccountManager::get()->m_accountID <= 0) {
        callback(false, "Debes estar logueado para subir miniaturas.");
        return;
    }
    if (!m_serverEnabled) { callback(false, "Funcionalidad de servidor desactivada"); return; }

    log::info("[ThumbTransport] subiendo gif nivel {} ({} bytes)", levelId, gifData.size());

    HttpClient::get().uploadGIF(levelId, gifData, username,
        [this, callback, levelId](bool success, std::string const& message) {
            if (success) {
                m_uploadCount++;
                ThumbnailLoader::get().invalidateLevel(levelId);
            }
            callback(success, message);
        });
}

// ── downloads ───────────────────────────────────────────────────────

void ThumbnailTransportClient::downloadThumbnail(int levelId, DownloadCallback callback) {
    if (!m_serverEnabled) { callback(false, nullptr); return; }
    log::info("[ThumbTransport] downloadThumbnail: levelId={}", levelId);

    HttpClient::get().downloadThumbnail(levelId,
        [callback, levelId](bool success, std::vector<uint8_t> const& data, int, int) {
            if (!success || data.empty()) { log::warn("[ThumbTransport] downloadThumbnail callback: FAILED levelId={}", levelId); callback(false, nullptr); return; }
            log::info("[ThumbTransport] downloadThumbnail callback: OK levelId={} bytes={}", levelId, data.size());
            callback(success, bytesToTexture(data));
        });
}

void ThumbnailTransportClient::getThumbnail(int levelId, DownloadCallback callback) {
    // 1. local
    if (auto* tex = loadFromLocal(levelId)) { log::debug("[ThumbTransport] getThumbnail: local hit levelId={}", levelId); callback(true, tex); return; }
    // 2. servidor
    if (m_serverEnabled) {
        log::debug("[ThumbTransport] getThumbnail: fetching from server levelId={}", levelId);
        downloadThumbnail(levelId, callback);
    } else {
        callback(false, nullptr);
    }
}

void ThumbnailTransportClient::downloadFromUrl(std::string const& url, DownloadCallback callback) {
    log::debug("[ThumbTransport] downloadFromUrl: {}", url);
    HttpClient::get().downloadFromUrl(url, [callback, url](bool success, std::vector<uint8_t> const& data, int, int) {
        if (success) { log::debug("[ThumbTransport] downloadFromUrl callback: OK bytes={}", data.size()); callback(success, bytesToTexture(data)); }
        else         { log::warn("[ThumbTransport] downloadFromUrl callback: FAILED url={}", url); callback(false, nullptr); }
    });
}

void ThumbnailTransportClient::downloadFromUrlData(std::string const& url, DownloadDataCallback callback) {
    HttpClient::get().downloadFromUrl(url, [callback](bool success, std::vector<uint8_t> const& data, int, int) {
        callback(success, data);
    });
}

// ── exists / delete ─────────────────────────────────────────────────

void ThumbnailTransportClient::checkExists(int levelId, ExistsCallback callback) {
    if (!m_serverEnabled) { callback(false); return; }
    log::debug("[ThumbTransport] checkExists: levelId={}", levelId);
    HttpClient::get().checkThumbnailExists(levelId, callback);
}

void ThumbnailTransportClient::deleteThumbnail(int levelId, std::string const& thumbnailId, std::string const& username,
                                               int accountID, ActionCallback callback) {
    if (!m_serverEnabled) { callback(false, "servidor desactivado"); return; }
    log::info("[ThumbTransport] deleteThumbnail: levelId={} thumbId={} user={}", levelId, thumbnailId, username);

    std::string endpoint = fmt::format("/api/thumbnails/delete/{}", levelId);

    matjson::Value json = matjson::makeObject({
        {"username", username},
        {"levelId", levelId},
        {"thumbnailId", thumbnailId},
        {"accountID", accountID}
    });
    std::string postData = json.dump();

    HttpClient::get().postWithAuth(endpoint, postData,
        [callback, levelId](bool success, std::string const& response) {
            if (success) {
                log::info("[ThumbTransport] deleteThumbnail callback: OK levelId={}", levelId);
                ThumbnailLoader::get().invalidateLevel(levelId);
                callback(true, "miniatura borrada con exito");
            } else {
                log::warn("[ThumbTransport] deleteThumbnail callback: FAILED levelId={} resp={}", levelId, response);
                callback(false, response);
            }
        });
}

// ── ratings ─────────────────────────────────────────────────────────

void ThumbnailTransportClient::getRating(int levelId, std::string const& username,
                                         std::string const& thumbnailId,
                                         geode::CopyableFunction<void(bool, float, int, int)> callback) {
    if (!m_serverEnabled) { callback(false, 0, 0, 0); return; }
    log::debug("[ThumbTransport] getRating: levelId={} thumbId={}", levelId, thumbnailId);

    HttpClient::get().getRating(levelId, username, thumbnailId,
        [callback](bool success, std::string const& response) {
            if (!success) { callback(false, 0, 0, 0); return; }
            auto jsonRes = matjson::parse(response);
            if (!jsonRes.isOk()) { callback(false, 0, 0, 0); return; }
            auto json = jsonRes.unwrap();
            float average = (float)json["average"].asDouble().unwrapOr(0.0);
            int count     = (int)json["count"].asInt().unwrapOr(0);
            int userVote  = (int)json["userVote"].asInt().unwrapOr(0);
            callback(true, average, count, userVote);
        });
}

void ThumbnailTransportClient::submitVote(int levelId, int stars, std::string const& username,
                                          std::string const& thumbnailId, ActionCallback callback) {
    if (!m_serverEnabled) { callback(false, "Server disabled"); return; }
    log::info("[ThumbTransport] submitVote: levelId={} stars={} thumbId={}", levelId, stars, thumbnailId);
    HttpClient::get().submitVote(levelId, stars, username, thumbnailId, callback);
}

// ── top lists ───────────────────────────────────────────────────────

void ThumbnailTransportClient::getTopCreators(ActionCallback callback) {
    if (!m_serverEnabled) { callback(false, "servidor desactivado"); return; }
    HttpClient::get().getTopCreators([callback](bool s, std::string const& r) { callback(s, r); });
}

void ThumbnailTransportClient::getTopThumbnails(ActionCallback callback) {
    if (!m_serverEnabled) { callback(false, "servidor desactivado"); return; }
    HttpClient::get().getTopThumbnails([callback](bool s, std::string const& r) { callback(s, r); });
}

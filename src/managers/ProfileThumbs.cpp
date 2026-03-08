#include "ProfileThumbs.hpp"
#include "ThumbnailAPI.hpp"
#include "../utils/AnimatedGIFSprite.hpp"
#include <Geode/utils/file.hpp>
#include <Geode/utils/string.hpp>
#include <filesystem>
#include <Geode/loader/Mod.hpp>
#include <fstream>
#include <algorithm>
#include <deque>

#include "../utils/Shaders.hpp"

using namespace geode::prelude;
using namespace cocos2d;
using namespace Shaders;




namespace {
    struct Header { int32_t w; int32_t h; int32_t fmt; };
}

ProfileThumbs& ProfileThumbs::get() {
    static ProfileThumbs inst; 
    static bool initialized = false;
    if (!initialized) {
        initialized = true;
        // limpio el cache de disco al inicio
        auto dir = Mod::get()->getSaveDir() / "thumbnails" / "profiles";
        std::error_code ec;
        if (std::filesystem::exists(dir, ec)) {
            std::filesystem::remove_all(dir, ec);
            if (ec) {
                log::error("[ProfileThumbs] Failed to clear profile cache: {}", ec.message());
            } else {
                log::info("[ProfileThumbs] Profile cache cleared on startup");
            }
        }
    }
    return inst;
}

std::string ProfileThumbs::makePath(int accountID) const {
    auto dir = Mod::get()->getSaveDir() / "thumbnails" / "profiles";
    (void)file::createDirectoryAll(dir);
    return geode::utils::string::pathToString(dir / fmt::format("{}.rgb", accountID));
}

bool ProfileThumbs::saveRGB(int accountID, const uint8_t* rgb, int width, int height) {
    // no guardo a disco si solo es sesion
    // aqui actualizo la cache en memoria
    
    if (!rgb || width <= 0 || height <= 0) return false;

    // convierto RGB a RGBA
    std::vector<uint8_t> rgbaBuf(width * height * 4);
    // copio RGB para escribirlo luego en disco
    std::vector<uint8_t> rgbCopy(width * height * 3);
    
    for (int i = 0; i < width * height; ++i) {
        rgbaBuf[i * 4 + 0] = rgb[i * 3 + 0];
        rgbaBuf[i * 4 + 1] = rgb[i * 3 + 1];
        rgbaBuf[i * 4 + 2] = rgb[i * 3 + 2];
        rgbaBuf[i * 4 + 3] = 255;
        
        rgbCopy[i * 3 + 0] = rgb[i * 3 + 0];
        rgbCopy[i * 3 + 1] = rgb[i * 3 + 1];
        rgbCopy[i * 3 + 2] = rgb[i * 3 + 2];
    }
    
    auto* tex = new CCTexture2D();
    if (tex->initWithData(rgbaBuf.data(), kCCTexture2DPixelFormat_RGBA8888, width, height, { (float)width, (float)height })) {
        tex->autorelease();
        
        // guardo a disco en otro thread para no frenar la UI
        // hilo de I/O de disco — no migrable a WebTask
        std::thread([this, accountID, width, height, data = std::move(rgbCopy)]() {
            auto path = makePath(accountID);
            std::ofstream out(path, std::ios::binary);
            if (out) {
                Header h{width, height, 24};
                out.write(reinterpret_cast<char const*>(&h), sizeof(h));
                out.write(reinterpret_cast<char const*>(data.data()), data.size());
                // uso queueInMainThread solo para loguear sin romper nada
                geode::Loader::get()->queueInMainThread([accountID]() {
                     log::debug("[ProfileThumbs] Saved profile to disk asynchronously for account {}", accountID);
                });
            }
        }).detach();

        // actualizo cache
        // intento preservar colores/ancho que ya tenia el usuario
        // primero leo la config existente para no pisarla
        ccColor3B cA = {255,255,255};
        ccColor3B cB = {255,255,255};
        float wF = 0.6f;
        
        auto it = m_profileCache.find(accountID);
        if (it != m_profileCache.end()) {
            cA = it->second.colorA;
            cB = it->second.colorB;
            wF = it->second.widthFactor;
        }
        
        this->cacheProfile(accountID, tex, cA, cB, wF);
        log::info("[ProfileThumbs] Memory cache updated for account {}", accountID);
    } else {
        tex->release();
        return false;
    }
    
    return true; 
}

bool ProfileThumbs::has(int accountID) const {
    std::error_code ec;
    return std::filesystem::exists(makePath(accountID), ec);
}

void ProfileThumbs::deleteProfile(int accountID) {
    clearCache(accountID);
    auto path = makePath(accountID);
    std::error_code ec;
    if (std::filesystem::exists(path, ec)) {
        std::filesystem::remove(path, ec);
        log::debug("[ProfileThumbs] Deleted profile thumbnail for account {}", accountID);
    }
}

CCTexture2D* ProfileThumbs::loadTexture(int accountID) {
    auto path = makePath(accountID);
    log::debug("[ProfileThumbs] Loading profile thumbnail for account {}: {}", accountID, path);
    
    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || ec) {
        log::debug("[ProfileThumbs] Thumbnail not found for account {}", accountID);
        return nullptr;
    }
    
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        log::error("[ProfileThumbs] Error opening file: {}", path);
        return nullptr;
    }
    
    Header h{}; 
    in.read(reinterpret_cast<char*>(&h), sizeof(h));
    
    if (h.fmt != 24 || h.w <= 0 || h.h <= 0) {
        log::error("[ProfileThumbs] Invalid header: fmt={}, w={}, h={}", h.fmt, h.w, h.h);
        return nullptr;
    }
    
    log::debug("[ProfileThumbs] Loading thumbnail {}x{}", h.w, h.h);
    
    std::vector<uint8_t> buf(h.w * h.h * 3);
    in.read(reinterpret_cast<char*>(buf.data()), buf.size());
    
    if (in.gcount() != static_cast<std::streamsize>(buf.size())) {
        log::error("[ProfileThumbs] File truncated: expected {} bytes, got {}", buf.size(), in.gcount());
        return nullptr;
    }

    // paso de RGB a RGBA por compatibilidad con cocos
    std::vector<uint8_t> rgbaBuf(h.w * h.h * 4);
    for (int i = 0; i < h.w * h.h; ++i) {
        rgbaBuf[i * 4 + 0] = buf[i * 3 + 0]; // R
        rgbaBuf[i * 4 + 1] = buf[i * 3 + 1]; // G
        rgbaBuf[i * 4 + 2] = buf[i * 3 + 2]; // B
        rgbaBuf[i * 4 + 3] = 255;            // A fijo
    }
    
    auto* tex = new CCTexture2D();
    if (!tex->initWithData(rgbaBuf.data(), kCCTexture2DPixelFormat_RGBA8888, h.w, h.h, { (float)h.w, (float)h.h })) {
        log::error("[ProfileThumbs] Failed to create texture");
        tex->release();
        return nullptr;
    }
    tex->autorelease();
    
    log::info("[ProfileThumbs] Thumbnail loaded successfully for account {}", accountID);
    return tex;
}

bool ProfileThumbs::loadRGB(int accountID, std::vector<uint8_t>& out, int& w, int& h) {
    auto path = makePath(accountID);
    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || ec) return false;
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    Header head{}; in.read(reinterpret_cast<char*>(&head), sizeof(head));
    if (head.fmt != 24 || head.w <= 0 || head.h <= 0) return false;
    out.resize(head.w * head.h * 3);
    in.read(reinterpret_cast<char*>(out.data()), out.size());
    w = head.w; h = head.h; return static_cast<bool>(in);
}

void ProfileThumbs::cacheProfile(int accountID, CCTexture2D* texture, 
                                 ccColor3B colorA, ccColor3B colorB, float widthFactor) {
    if (!texture) return;
    
    std::lock_guard<std::mutex> lock(m_cacheMutex);
    clearOldCache(); // limpio antes de meter la nueva
    
    log::debug("[ProfileThumbs] Caching profile for account {} with colors RGB({},{},{}) -> RGB({},{},{}), width: {}", 
               accountID, colorA.r, colorA.g, colorA.b, colorB.r, colorB.g, colorB.b, widthFactor);
    
    // preservo config existente y gifKey si ya habia algo
    ProfileConfig existingConfig;
    std::string existingGifKey;
    auto it = m_profileCache.find(accountID);
    if (it != m_profileCache.end()) {
        existingConfig = it->second.config;
        existingGifKey = it->second.gifKey;
    }

    m_profileCache[accountID] = ProfileCacheEntry(texture, colorA, colorB, widthFactor);
    m_profileCache[accountID].config = existingConfig;

    // si ya tenia gifKey la vuelvo a poner (GIF ya estaba cacheado)
    if (!existingGifKey.empty()) {
        m_profileCache[accountID].gifKey = existingGifKey;
        log::debug("[ProfileThumbs] Preserved existing gifKey: {} for account {}", existingGifKey, accountID);
    }

    // evictar si sobrepasamos el limite
    while (m_profileCache.size() > MAX_PROFILE_CACHE_SIZE) {
        // encontrar la entrada mas antigua (excluyendo la que acabamos de meter)
        auto oldest = m_profileCache.end();
        for (auto it2 = m_profileCache.begin(); it2 != m_profileCache.end(); ++it2) {
            if (it2->first == accountID) continue;
            if (oldest == m_profileCache.end() || it2->second.timestamp < oldest->second.timestamp) {
                oldest = it2;
            }
        }
        if (oldest != m_profileCache.end()) {
            if (!oldest->second.gifKey.empty()) {
                AnimatedGIFSprite::unpinGIF(oldest->second.gifKey);
            }
            m_profileCache.erase(oldest);
        } else {
            break;
        }
    }
}

void ProfileThumbs::cacheProfileGIF(int accountID, std::string const& gifKey, 
                                    cocos2d::ccColor3B colorA, cocos2d::ccColor3B colorB, float widthFactor) {
    std::lock_guard<std::mutex> lock(m_cacheMutex);
    clearOldCache();
    
    log::debug("[ProfileThumbs] Caching GIF profile for account {} with key {}", accountID, gifKey);
    
    AnimatedGIFSprite::pinGIF(gifKey);

    // preservo config existente si hay
    ProfileConfig existingConfig;
    auto it = m_profileCache.find(accountID);
    if (it != m_profileCache.end()) {
        existingConfig = it->second.config;
    }

    // aqui no hay textura directa, solo la key del GIF
    // queueload espera textura, pero la parte visual ya maneja la gifKey
    
    m_profileCache[accountID] = ProfileCacheEntry(gifKey, colorA, colorB, widthFactor);
    m_profileCache[accountID].config = existingConfig;
}

void ProfileThumbs::cacheProfileConfig(int accountID, ProfileConfig const& config) {
    auto it = m_profileCache.find(accountID);
    if (it != m_profileCache.end()) {
        it->second.config = config;
    } else {
        // si no habia entrada, creo una solo con config
        ProfileCacheEntry entry;
        entry.config = config;
        m_profileCache[accountID] = std::move(entry);
    }
}

ProfileConfig ProfileThumbs::getProfileConfig(int accountID) {
    auto it = m_profileCache.find(accountID);
    if (it != m_profileCache.end()) {
        ProfileConfig config = it->second.config;
        // si en cache hay gifKey, lo inyecto en la config
        if (!it->second.gifKey.empty()) {
            config.gifKey = it->second.gifKey;
        }
        return config;
    }
    return ProfileConfig();
}

ProfileCacheEntry* ProfileThumbs::getCachedProfile(int accountID) {
    std::lock_guard<std::mutex> lock(m_cacheMutex);
    auto it = m_profileCache.find(accountID);
    if (it == m_profileCache.end()) {
        return nullptr;
    }
    
    // ¿se ha caducado ya la cache?
    auto now = std::chrono::steady_clock::now();
    if (now - it->second.timestamp > CACHE_DURATION) {
        log::debug("[ProfileThumbs] Cache expired for account {}", accountID);
        m_profileCache.erase(it);
        return nullptr;
    }
    
    log::debug("[ProfileThumbs] Cache found for account {}", accountID);
    return &it->second;
}

void ProfileThumbs::clearCache(int accountID) {
    std::lock_guard<std::mutex> lock(m_cacheMutex);
    auto it = m_profileCache.find(accountID);
    if (it != m_profileCache.end()) {
        log::debug("[ProfileThumbs] Clearing cache for account {}", accountID);
        m_profileCache.erase(it);
    }
    removeFromNoProfileCache(accountID);
}

void ProfileThumbs::clearOldCache() {
    auto now = std::chrono::steady_clock::now();
    
    for (auto it = m_profileCache.begin(); it != m_profileCache.end();) {
        if (now - it->second.timestamp > CACHE_DURATION) {
            log::debug("[ProfileThumbs] Removing old cache for account {}", it->first);
            if (!it->second.gifKey.empty()) {
                AnimatedGIFSprite::unpinGIF(it->second.gifKey);
            }
            it = m_profileCache.erase(it);
        } else {
            ++it;
        }
    }
}

void ProfileThumbs::clearAllCache() {
    std::lock_guard<std::mutex> lock(m_cacheMutex);
    log::info("[ProfileThumbs] Clearing all profile cache ({} entries)", m_profileCache.size());
    for (auto const& [id, entry] : m_profileCache) {
        if (!entry.gifKey.empty()) {
            AnimatedGIFSprite::unpinGIF(entry.gifKey);
        }
    }
    m_profileCache.clear();
    m_noProfileCache.clear();
}

void ProfileThumbs::markNoProfile(int accountID) {
    m_noProfileCache.insert(accountID);
}

void ProfileThumbs::removeFromNoProfileCache(int accountID) {
    m_noProfileCache.erase(accountID);
}

bool ProfileThumbs::isNoProfile(int accountID) const {
    return m_noProfileCache.find(accountID) != m_noProfileCache.end();
}

void ProfileThumbs::clearNoProfileCache() {
    m_noProfileCache.clear();
}

void ProfileThumbs::clearPendingDownloads() {
    m_pendingCallbacks.clear();
    m_downloadQueue.clear();
    m_usernameMap.clear();
    m_activeDownloads = 0;
}

CCNode* ProfileThumbs::createProfileNode(CCTexture2D* texture, ProfileConfig const& config, CCSize cs, bool onlyBackground) {
    // si tenemos gifKey intento crear un AnimatedGIFSprite
    AnimatedGIFSprite* gifSprite = nullptr;
    if (!config.gifKey.empty()) {
        // primero pruebo a crearlo desde el cache
        if (AnimatedGIFSprite::isCached(config.gifKey)) {
            gifSprite = AnimatedGIFSprite::createFromCache(config.gifKey);
        }
    }

    if (!texture && !gifSprite) return nullptr;

    // nodo contenedor
    auto container = CCNode::create();
    container->setContentSize(cs);

    // --- logica del fondo ---
    CCNode* bg = nullptr;

    // tipo de fondo final que se va a usar
    std::string bgType = config.backgroundType;
    
    // fuerzo modo thumbnail si hay textura/GIF y:
    // 1. estamos en modo banner (onlyBackground=true)
    // 2. o config viene en "gradient" por defecto
    if ((onlyBackground || bgType == "gradient") && (texture || !config.gifKey.empty())) {
        bgType = "thumbnail";
    }

    if (bgType == "thumbnail") {
        if (gifSprite) {
            // --- fondo con GIF (blur en tiempo real) ---
            auto bgSprite = AnimatedGIFSprite::createFromCache(config.gifKey);
            if (bgSprite) {
                CCSize targetSize = cs;
                targetSize.width = std::max(targetSize.width, 512.f);
                targetSize.height = std::max(targetSize.height, 256.f);
                
                float scaleX = targetSize.width / gifSprite->getContentSize().width;
                float scaleY = targetSize.height / gifSprite->getContentSize().height;
                float scale = std::max(scaleX, scaleY);
                
                bgSprite->setScale(scale);
                bgSprite->setPosition(targetSize * 0.5f);
                
                // shader de blur rapido por encima
                auto shader = getOrCreateShader("fast-blur", vertexShaderCell, fragmentShaderFastBlur);
                if (shader) {
                    bgSprite->setShaderProgram(shader);
                    // AnimatedGIFSprite::draw se encarga de los uniforms
                }
                
                // clipper para que no se salga del rectangulo
                auto stencil = CCDrawNode::create();
                CCPoint rect[4];
                rect[0] = ccp(0, 0);
                rect[1] = ccp(cs.width, 0);
                rect[2] = ccp(cs.width, cs.height);
                rect[3] = ccp(0, cs.height);
                ccColor4F white = {1, 1, 1, 1};
                stencil->drawPolygon(rect, 4, white, 0, white);
                
                auto clipper = CCClippingNode::create(stencil);
                clipper->setAlphaThreshold(0.05f);
                clipper->setContentSize(cs);
                clipper->setPosition({0,0});
                clipper->setZOrder(-10);
                
                bgSprite->setPosition(cs / 2);
                clipper->addChild(bgSprite);
                bg = clipper;
                
                if (config.darkness > 0.0f) {
                    auto overlay = CCLayerColor::create({0, 0, 0, static_cast<GLubyte>(config.darkness * 255)});
                    overlay->setContentSize(cs);
                    overlay->setPosition({0, 0});
                    overlay->setZOrder(-5); 
                    container->addChild(overlay);
                }
            }
        } else if (texture) {
            CCSize targetSize = cs;
            targetSize.width = std::max(targetSize.width, 512.f);
            targetSize.height = std::max(targetSize.height, 256.f);

            CCSprite* bgSprite = Shaders::createBlurredSprite(texture, targetSize, config.blurIntensity);
            if (!bgSprite) bgSprite = CCSprite::createWithTexture(texture);

            if (bgSprite) {
                // clipper para el fondo estatico
                auto stencil = CCDrawNode::create();
                CCPoint rect[4];
                rect[0] = ccp(0, 0);
                rect[1] = ccp(cs.width, 0);
                rect[2] = ccp(cs.width, cs.height);
                rect[3] = ccp(0, cs.height);
                ccColor4F white = {1, 1, 1, 1};
                stencil->drawPolygon(rect, 4, white, 0, white);
                
                auto clipper = CCClippingNode::create(stencil);
                clipper->setAlphaThreshold(0.05f);
                clipper->setContentSize(cs);
                clipper->setPosition({0,0});
                clipper->setZOrder(-10); // bien detras de todo
                
                float targetW = cs.width;
                float targetH = cs.height;
                float finalScale = std::max(
                    targetW / bgSprite->getContentSize().width,
                    targetH / bgSprite->getContentSize().height
                );
                bgSprite->setScale(finalScale);
                bgSprite->setPosition(cs / 2);
                
                clipper->addChild(bgSprite);
                bg = clipper;

                // capa oscura extra por encima
                if (config.darkness > 0.0f) {
                    auto overlay = CCLayerColor::create({0, 0, 0, static_cast<GLubyte>(config.darkness * 255)});
                    overlay->setContentSize(cs);
                    overlay->setPosition({0, 0});
                    overlay->setZOrder(-5); 
                    container->addChild(overlay);
                }
            }
        }
    } else if (bgType != "none") {
        // degradado o color solido
        if (config.useGradient) {
            auto grad = CCLayerGradient::create(
                ccc4(config.colorA.r, config.colorA.g, config.colorA.b, 255),
                ccc4(config.colorB.r, config.colorB.g, config.colorB.b, 255)
            );
            grad->setContentSize(cs);
            grad->setAnchorPoint({0,0});
            grad->setPosition({0,0});
            grad->setVector({1.f, 0.f}); // horizontal
            grad->setZOrder(-10);
            bg = grad;
        } else {
            auto solid = CCLayerColor::create(ccc4(config.colorA.r, config.colorA.g, config.colorA.b, 255));
            solid->setContentSize(cs);
            solid->setAnchorPoint({0,0});
            solid->setPosition({0,0});
            solid->setZOrder(-10);
            bg = solid;
        }
    }

    if (bg) {
        container->addChild(bg);
    }

    if (onlyBackground) {
        return container;
    }

    // --- sprite de perfil ---
    CCNode* mainSprite = nullptr;
    float contentW = 0, contentH = 0;

    if (gifSprite) {
        mainSprite = gifSprite;
        contentW = gifSprite->getContentSize().width;
        contentH = gifSprite->getContentSize().height;
        // me aseguro de que el GIF este actualizando
        gifSprite->scheduleUpdate();
    } else if (texture) {
        auto s = CCSprite::createWithTexture(texture);
        mainSprite = s;
        contentW = s->getContentWidth();
        contentH = s->getContentHeight();
    }

    if (mainSprite && contentW > 0 && contentH > 0) {
        float factor = 0.60f;
        if (config.hasConfig) {
            factor = config.widthFactor;
        } else {
            factor = Mod::get()->getSavedValue<float>("profile-thumb-width", 0.6f);
        }
        factor = std::max(0.30f, std::min(0.95f, factor));
        float desiredWidth = cs.width * factor;

        float scaleY = cs.height / contentH;
        float scaleX = desiredWidth / contentW;

        mainSprite->setScaleY(scaleY);
        mainSprite->setScaleX(scaleX);
        
        // clipping inclinado estilo “banner”
        constexpr float angle = 18.f;
        CCSize scaledSize{ desiredWidth, contentH * scaleY };
        auto mask = CCLayerColor::create({255,255,255});
        mask->setContentSize(scaledSize);
        mask->setAnchorPoint({1,0});
        mask->setSkewX(angle);

        auto clip = CCClippingNode::create();
        clip->setStencil(mask);
        clip->setAlphaThreshold(0.5f);
        clip->setContentSize(scaledSize);
        clip->setAnchorPoint({1,0});
        
        // pego el clip al lado derecho
        clip->setPosition({cs.width, 0});
        clip->setZOrder(10); // lo dejo por encima del fondo
        
        // ajusto posicion del sprite dentro del clip
        mainSprite->setAnchorPoint({1,0});
        mainSprite->setPosition({scaledSize.width, 0});
        
        clip->addChild(mainSprite);
        container->addChild(clip);
        
        // linea separadora
        auto separator = CCLayerColor::create({
            config.separatorColor.r, 
            config.separatorColor.g, 
            config.separatorColor.b, 
            (GLubyte)std::clamp(config.separatorOpacity, 0, 255)
        });
        separator->setContentSize({2.0f, cs.height * 1.2f}); // un poco mas alto para cubrir la inclinacion
        separator->setAnchorPoint({0.5f, 0});
        separator->setSkewX(angle);
        separator->setPosition({cs.width - desiredWidth, 0});
        separator->setZOrder(15); // por encima del clip
        container->addChild(separator);
    }

    return container;
}

void ProfileThumbs::queueLoad(int accountID, std::string const& username, geode::CopyableFunction<void(bool, cocos2d::CCTexture2D*)> callback) {
    // 0. miro cache negativa (si ya fallo antes, ni lo intento en esta sesion)
    if (isNoProfile(accountID)) {
        if (callback) callback(false, nullptr);
        return;
    }

    // 1. miro cache primero
    auto cached = getCachedProfile(accountID);
    if (cached && cached->texture) {
        if (callback) callback(true, cached->texture);
        return;
    }

    // 2. si ya esta en cola, solo apilo el callback
    if (m_pendingCallbacks.find(accountID) != m_pendingCallbacks.end()) {
        m_pendingCallbacks[accountID].push_back(callback);
        return;
    }

    // 3. lo meto en la cola (FIFO, al final)
    // asi la lista carga de arriba a abajo, y la visibilidad afina el orden
    m_downloadQueue.push_back(accountID);
    m_pendingCallbacks[accountID].push_back(callback);
    
    // guardo username asociado a esta peticion
    m_usernameMap[accountID] = username;

    // 4. arranco el procesado de la cola
    processQueue();
}

void ProfileThumbs::notifyVisible(int accountID) {
    m_visibilityMap[accountID] = std::chrono::steady_clock::now();
}

void ProfileThumbs::processQueue() {
    while (m_activeDownloads < MAX_CONCURRENT_DOWNLOADS && !m_downloadQueue.empty()) {
        m_activeDownloads++;

        // busco el mejor candidato
        // estrategia: primer item (mas viejo) que siga siendo visible
        // asi mantengo FIFO para lo que se ve en pantalla
        auto now = std::chrono::steady_clock::now();
        auto bestIt = m_downloadQueue.end();
        
        for (auto it = m_downloadQueue.begin(); it != m_downloadQueue.end(); ++it) {
            int id = *it;
            if (m_visibilityMap.count(id)) {
                auto lastSeen = m_visibilityMap[id];
                if (now - lastSeen < std::chrono::milliseconds(200)) { // visible en los ultimos 200ms
                    bestIt = it;
                    break; // ya encontre uno visible, no sigo buscando
                }
            }
        }

        // si no hay nada visible, hago fallback a LIFO (la mas nueva)
        // ayuda cuando el usuario se mueve muy rapido y aun no ha “notificado” visibilidad
        if (bestIt == m_downloadQueue.end()) {
            // uso el ultimo elemento (el mas nuevo)
            bestIt = m_downloadQueue.end() - 1;
        }

        int accountID = *bestIt;
        m_downloadQueue.erase(bestIt);

        log::debug("[ProfileThumbs] Processing queue: AccountID {}", accountID);
        
        std::string username = "";
        if (m_usernameMap.find(accountID) != m_usernameMap.end()) {
            username = m_usernameMap[accountID];
            m_usernameMap.erase(accountID);
        }

        ThumbnailAPI::get().downloadProfile(accountID, username, [this, accountID](bool success, CCTexture2D* texture) {
            
            // Ref mantiene la textura viva durante la cadena async
            Ref<CCTexture2D> texRef = texture;

            // engancho la descarga de la config para tener imagen + settings
            ThumbnailAPI::get().downloadProfileConfig(accountID, [this, accountID, success, texRef](bool configSuccess, ProfileConfig const& config) {
                
                if (success && texRef) {
                    // cacheo el perfil con la config (o defaults si la config fallo)
                    this->cacheProfile(accountID, texRef, config.colorA, config.colorB, config.widthFactor);
                }

                if (configSuccess) {
                    this->cacheProfileConfig(accountID, config);
                }

                if (!success && !configSuccess) {
                    markNoProfile(accountID);
                }

                // llamo a todos los callbacks pendientes
                auto it = m_pendingCallbacks.find(accountID);
                if (it != m_pendingCallbacks.end()) {
                    for (auto const& cb : it->second) {
                        if (cb) cb(success, texRef);
                    }
                    m_pendingCallbacks.erase(it);
                }

                // sigo con la cola
                m_activeDownloads--;
                
                // vuelvo a llamar processQueue en el siguiente frame (stack limpio)
                Loader::get()->queueInMainThread([this]() {
                    processQueue();
                });
            });
        });
    }
}

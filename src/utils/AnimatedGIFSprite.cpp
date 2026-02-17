#include "AnimatedGIFSprite.hpp"
#include "GIFDecoder.hpp"
#include "DominantColors.hpp"
#include "Debug.hpp"
#include <Geode/loader/Log.hpp>
#include <fstream>
#include <filesystem>
#include <Geode/utils/string.hpp>

using namespace geode::prelude;

static float getContentScaleFactorSafe() {
    return 1.0f;
    /*
    auto* director = CCDirector::sharedDirector();
    float sf = director ? director->getContentScaleFactor() : 1.0f;
    if (sf <= 0.0f) sf = 1.0f;
    return sf;
    */
}

std::map<std::string, AnimatedGIFSprite::SharedGIFData> AnimatedGIFSprite::s_gifCache;
std::list<std::string> AnimatedGIFSprite::s_lruList;
std::set<std::string> AnimatedGIFSprite::s_pinnedGIFs;
size_t AnimatedGIFSprite::s_currentCacheSize = 0;

size_t AnimatedGIFSprite::getMaxCacheMem() {
    bool ramCache = Mod::get()->getSettingValue<bool>("gif-ram-cache");
    if (ramCache) {
        // 300mb en móvil es jugársela, en android bajo más
        // en safe mode todavía reduzco más la memoria
#ifdef GEODE_IS_ANDROID
        bool safeMode = true;
        try { safeMode = Mod::get()->getSettingValue<bool>("android-safe-mode"); } catch (...) {}
        return safeMode ? (20 * 1024 * 1024) : (80 * 1024 * 1024);
#else
        return 300 * 1024 * 1024; // 300 MB
#endif
    } else {
        return 10 * 1024 * 1024; // 10 MB (cache mínimo pa la escena actual)
    }
}

// cosas estáticas de la cola de workers
std::deque<AnimatedGIFSprite::GIFTask> AnimatedGIFSprite::s_taskQueue;
std::mutex AnimatedGIFSprite::s_queueMutex;
std::condition_variable AnimatedGIFSprite::s_queueCV;
std::thread AnimatedGIFSprite::s_workerThread;
bool AnimatedGIFSprite::s_workerRunning = false;

void AnimatedGIFSprite::initWorker() {
    if (!s_workerRunning) {
        s_workerRunning = true;
        s_workerThread = std::thread(workerLoop);
        s_workerThread.detach();
        PaimonDebug::log("[AnimatedGIFSprite] Worker thread started");
    }
}

// helper pa pasar de rgba8888 a rgba4444
static std::vector<uint16_t> convertToRGBA4444(const uint32_t* pixels, int width, int height) {
    std::vector<uint16_t> out(width * height);
    for (int i = 0; i < width * height; ++i) {
        uint32_t p = pixels[i];
        // rgba8888: r=0-7, g=8-15, b=16-23, a=24-31 (en memoria suele ser abgr pero aquí asumo packing normal)
        // cocos2d realmente espera:
        // rgba8888: r, g, b, a en ese orden.
        // rgba4444: r, g, b, a en nibbles (0xrgba).

        uint8_t r = p & 0xFF;
        uint8_t g = (p >> 8) & 0xFF;
        uint8_t b = (p >> 16) & 0xFF;
        uint8_t a = (p >> 24) & 0xFF;
        
        uint8_t r4 = r >> 4;
        uint8_t g4 = g >> 4;
        uint8_t b4 = b >> 4;
        uint8_t a4 = a >> 4;
        
        // empaqueto en 16 bits: r4 g4 b4 a4
        out[i] = (r4 << 12) | (g4 << 8) | (b4 << 4) | a4;
    }
    return out;
}

void AnimatedGIFSprite::pinGIF(const std::string& key) {
    s_pinnedGIFs.insert(key);
    // lo saco de la lista LRU si estaba, así no se lo carga la limpieza
    s_lruList.remove(key);
}

void AnimatedGIFSprite::unpinGIF(const std::string& key) {
    if (s_pinnedGIFs.erase(key)) {
        // si lo despincho, lo meto de vuelta en la LRU
        // solo si sigue en el cache
        if (s_gifCache.find(key) != s_gifCache.end()) {
            s_lruList.push_back(key);
            
            // disparo limpieza si ya me pasé del límite
            size_t maxMem = getMaxCacheMem();
            while (s_currentCacheSize > maxMem && !s_lruList.empty()) {
                std::string toRemove = s_lruList.front();
                s_lruList.pop_front();
                auto it = s_gifCache.find(toRemove);
                if (it != s_gifCache.end()) {
                    size_t removeSize = 0;
                    for (auto* tex : it->second.textures) {
                        removeSize += tex->getPixelsWide() * tex->getPixelsHigh() * 2;
                        if (tex) tex->release();
                    }
                    if (s_currentCacheSize >= removeSize) s_currentCacheSize -= removeSize;
                    else s_currentCacheSize = 0;
                    
                    s_gifCache.erase(it);
                }
            }
        }
    }
}

bool AnimatedGIFSprite::isPinned(const std::string& key) {
    return s_pinnedGIFs.find(key) != s_pinnedGIFs.end();
}

AnimatedGIFSprite* AnimatedGIFSprite::create(const std::string& filename) {
    auto ret = new AnimatedGIFSprite();
    if (ret && ret->init()) {
        ret->autorelease();
        ret->m_filename = filename;
        
        if (ret->initFromCache(filename)) {
            return ret;
        }
        
        // carga sincrona
        std::ifstream file(filename, std::ios::binary);
        if (!file) return nullptr;
        
        std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        file.close();
        
        if (!GIFDecoder::isGIF(data.data(), data.size())) return nullptr;
        
        auto gifData = GIFDecoder::decode(data.data(), data.size());
        if (gifData.frames.empty()) return nullptr;

        float sf = getContentScaleFactorSafe();
        
        // lo guardo en cache
        SharedGIFData sharedData;
        sharedData.width = gifData.width;
        sharedData.height = gifData.height;
        
        for (const auto& frame : gifData.frames) {
            auto pixels4444 = convertToRGBA4444(reinterpret_cast<const uint32_t*>(frame.pixels.data()), frame.width, frame.height);
            auto texture = new CCTexture2D();
            texture->initWithData(
                pixels4444.data(),
                kCCTexture2DPixelFormat_RGBA4444,
                frame.width,
                frame.height,
                CCSize(frame.width / sf, frame.height / sf)
            );
            texture->setAntiAliasTexParameters();
            
            sharedData.textures.push_back(texture);
            sharedData.delays.push_back(frame.delayMs / 1000.0f);
            sharedData.frameRects.push_back(CCRect(0, 0, frame.width, frame.height));
        }
        
        // guardo la entrada en cache
        s_gifCache[filename] = sharedData;
        
        // calculo tamaño aproximado en RAM
        size_t entrySize = 0;
        for (auto* tex : sharedData.textures) {
            entrySize += tex->getPixelsWide() * tex->getPixelsHigh() * 2;
        }
        s_currentCacheSize += entrySize;

        if (!isPinned(filename)) {
            s_lruList.push_back(filename);
        }
        
        size_t maxMem = getMaxCacheMem();
        while (s_currentCacheSize > maxMem && !s_lruList.empty()) {
            std::string toRemove = s_lruList.front();
            s_lruList.pop_front();
            auto it = s_gifCache.find(toRemove);
            if (it != s_gifCache.end()) {
                size_t removeSize = 0;
                for (auto* tex : it->second.textures) {
                    removeSize += tex->getPixelsWide() * tex->getPixelsHigh() * 2;
                    if (tex) tex->release();
                }
                if (s_currentCacheSize >= removeSize) s_currentCacheSize -= removeSize;
                else s_currentCacheSize = 0;
                
                s_gifCache.erase(it);
            }
        }
        
        // lo vuelvo a inicializar pero ya tirando del cache
        ret->initFromCache(filename);
        
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

AnimatedGIFSprite* AnimatedGIFSprite::create(const void* data, size_t size) {
    auto ret = new AnimatedGIFSprite();
    if (ret && ret->init()) {
        ret->autorelease();
        ret->m_filename = "memory"; // nombre fake, solo indica que viene de memoria
        
        if (!GIFDecoder::isGIF(static_cast<const uint8_t*>(data), size)) return nullptr;
        
        auto gifData = GIFDecoder::decode(static_cast<const uint8_t*>(data), size);
        if (gifData.frames.empty()) return nullptr;
        
        // GIFs en memoria no se cachean globalmente salvo que tengamos key
        
        ret->m_canvasWidth = gifData.width;
        ret->m_canvasHeight = gifData.height;

        float sf = getContentScaleFactorSafe();
        
        for (const auto& frame : gifData.frames) {
            auto pixels4444 = convertToRGBA4444(reinterpret_cast<const uint32_t*>(frame.pixels.data()), frame.width, frame.height);
            auto texture = new CCTexture2D();
            texture->initWithData(
                pixels4444.data(),
                kCCTexture2DPixelFormat_RGBA4444,
                frame.width,
                frame.height,
                CCSize(frame.width / sf, frame.height / sf)
            );
            texture->setAntiAliasTexParameters();
            
            auto* gifFrame = new GIFFrame();
            gifFrame->texture = texture;
            gifFrame->delay = frame.delayMs / 1000.0f;
            gifFrame->rect = CCRect(0, 0, frame.width, frame.height);
            ret->m_frames.push_back(gifFrame);
            
            ret->m_frameColors.push_back({ {0,0,0}, {255,255,255} });
        }
        
        ret->setContentSize(CCSize(ret->m_canvasWidth / sf, ret->m_canvasHeight / sf));
        ret->setCurrentFrame(0);
        ret->scheduleUpdate();
        
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

void AnimatedGIFSprite::updateTextureLoading(float dt) {
    if (m_pendingFrames.empty()) {
        this->unschedule(schedule_selector(AnimatedGIFSprite::updateTextureLoading));
        
        SharedGIFData cacheEntry;
        cacheEntry.width = m_canvasWidth;
        cacheEntry.height = m_canvasHeight;
        
        for (auto* frame : m_frames) {
            cacheEntry.textures.push_back(frame->texture);
            cacheEntry.delays.push_back(frame->delay);
            cacheEntry.frameRects.push_back(frame->rect);
        }
        
        s_gifCache[m_filename] = cacheEntry;
        
        // calculo tamano
        size_t entrySize = 0;
        for (auto* tex : cacheEntry.textures) {
            // RGBA4444 = 2 bytes por pixel
            entrySize += tex->getPixelsWide() * tex->getPixelsHigh() * 2;
        }
        s_currentCacheSize += entrySize;

        // actualizo lru - quito entrada existente pa evitar duplicados
        if (!isPinned(m_filename)) {
            s_lruList.remove(m_filename);
            s_lruList.push_back(m_filename);
        }
        
        size_t maxMem = getMaxCacheMem();
        while (s_currentCacheSize > maxMem && !s_lruList.empty()) {
            std::string toRemove = s_lruList.front();
            s_lruList.pop_front();
            auto it = s_gifCache.find(toRemove);
            if (it != s_gifCache.end()) {
                size_t removeSize = 0;
                for (auto* tex : it->second.textures) {
                    removeSize += tex->getPixelsWide() * tex->getPixelsHigh() * 2;
                    if (tex) tex->release();
                }
                if (s_currentCacheSize >= removeSize) s_currentCacheSize -= removeSize;
                else s_currentCacheSize = 0;
                
                s_gifCache.erase(it);
            }
        }

        this->scheduleUpdate();
        return;
    }

    int framesToProcess = 1; 
    float sf = getContentScaleFactorSafe();
    
    while (framesToProcess > 0 && !m_pendingFrames.empty()) {
        auto frameData = m_pendingFrames.front();
        
        auto* gifFrame = new GIFFrame();
        auto* texture = new CCTexture2D();
        
        bool success = false;
        try {
            auto pixels4444 = convertToRGBA4444(reinterpret_cast<const uint32_t*>(frameData.pixels.data()), frameData.width, frameData.height);
            
            success = texture->initWithData(
                pixels4444.data(),
                kCCTexture2DPixelFormat_RGBA4444,
                frameData.width,
                frameData.height,
                CCSize(frameData.width / sf, frameData.height / sf)
            );
        } catch (...) { success = false; }

        if (success) {
            texture->setAntiAliasTexParameters();
            gifFrame->texture = texture;
            gifFrame->delay = frameData.delayMs / 1000.0f;
            gifFrame->rect = CCRect(frameData.left, frameData.top, frameData.width, frameData.height);

            m_frames.push_back(gifFrame);
            m_frameColors.push_back({ {0,0,0}, {255,255,255} });
            
            texture->retain();
        } else {
            delete gifFrame;
            texture->release();
        }
        
        m_pendingFrames.erase(m_pendingFrames.begin());
        framesToProcess--;
    }
    
    if (m_frames.size() == 1) {
        this->setCurrentFrame(0);
    }
}

bool AnimatedGIFSprite::processNextPendingFrame() {
    if (m_pendingFrames.empty()) return false;

    float sf = getContentScaleFactorSafe();
    auto frameData = m_pendingFrames.front();

    auto* gifFrame = new GIFFrame();
    auto* texture = new CCTexture2D();

    bool success = false;
    try {
        auto pixels4444 = convertToRGBA4444(reinterpret_cast<const uint32_t*>(frameData.pixels.data()), frameData.width, frameData.height);
        success = texture->initWithData(
            pixels4444.data(),
            kCCTexture2DPixelFormat_RGBA4444,
            frameData.width,
            frameData.height,
            CCSize(frameData.width / sf, frameData.height / sf)
        );
    } catch (...) { success = false; }

    if (success) {
        texture->setAntiAliasTexParameters();
        gifFrame->texture = texture;
        gifFrame->delay = frameData.delayMs / 1000.0f;
        gifFrame->rect = CCRect(frameData.left, frameData.top, frameData.width, frameData.height);

        m_frames.push_back(gifFrame);
        m_frameColors.push_back({ {0,0,0}, {255,255,255} });
        texture->retain();
    } else {
        delete gifFrame;
        texture->release();
    }

    m_pendingFrames.erase(m_pendingFrames.begin());

    if (success && m_frames.size() == 1) {
        this->setCurrentFrame(0);
    }

    return success;
}

std::string AnimatedGIFSprite::getCachePath(const std::string& path) {
    auto cacheDir = Mod::get()->getSaveDir() / "gif_cache";
    if (!std::filesystem::exists(cacheDir)) {
        std::filesystem::create_directories(cacheDir);
    }
    
    std::hash<std::string> hasher;
    auto hash = hasher(path);
    return (cacheDir / (std::to_string(hash) + ".bin")).string();
}

bool AnimatedGIFSprite::loadFromDiskCache(const std::string& path, DiskCacheEntry& outEntry) {
    auto cachePath = getCachePath(path);
    if (!std::filesystem::exists(cachePath)) return false;
    
        // miro la fecha de modificación
    try {
        auto cacheTime = std::filesystem::last_write_time(cachePath);
        auto sourceTime = std::filesystem::last_write_time(path);
        
        // si la fuente es más nueva que la cache, la tiro
        if (sourceTime > cacheTime) return false;
        
        std::ifstream file(cachePath, std::ios::binary);
        if (!file) return false;
        
        // cabecera: versión, ancho, alto, nº de frames
        uint32_t version;
        file.read(reinterpret_cast<char*>(&version), sizeof(version));
        if (version != 1) return false;
        
        file.read(reinterpret_cast<char*>(&outEntry.width), sizeof(outEntry.width));
        file.read(reinterpret_cast<char*>(&outEntry.height), sizeof(outEntry.height));
        
        uint32_t frameCount;
        file.read(reinterpret_cast<char*>(&frameCount), sizeof(frameCount));
        
        outEntry.frames.resize(frameCount);
        for (uint32_t i = 0; i < frameCount; ++i) {
            auto& frame = outEntry.frames[i];
            file.read(reinterpret_cast<char*>(&frame.delay), sizeof(frame.delay));
            file.read(reinterpret_cast<char*>(&frame.width), sizeof(frame.width));
            file.read(reinterpret_cast<char*>(&frame.height), sizeof(frame.height));
            
            uint32_t dataSize;
            file.read(reinterpret_cast<char*>(&dataSize), sizeof(dataSize));
            
            frame.pixels.resize(dataSize / 2); // va en uint16_t
            file.read(reinterpret_cast<char*>(frame.pixels.data()), dataSize);
        }
        
        return true;
    } catch(...) {
        return false;
    }
}

void AnimatedGIFSprite::saveToDiskCache(const std::string& path, const DiskCacheEntry& entry) {
    auto cachePath = getCachePath(path);
    try {
        std::ofstream file(cachePath, std::ios::binary);
        if (!file) return;
        
        uint32_t version = 1;
        file.write(reinterpret_cast<const char*>(&version), sizeof(version));
        file.write(reinterpret_cast<const char*>(&entry.width), sizeof(entry.width));
        file.write(reinterpret_cast<const char*>(&entry.height), sizeof(entry.height));
        
        uint32_t frameCount = entry.frames.size();
        file.write(reinterpret_cast<const char*>(&frameCount), sizeof(frameCount));
        
        for (const auto& frame : entry.frames) {
            file.write(reinterpret_cast<const char*>(&frame.delay), sizeof(frame.delay));
            file.write(reinterpret_cast<const char*>(&frame.width), sizeof(frame.width));
            file.write(reinterpret_cast<const char*>(&frame.height), sizeof(frame.height));
            
            uint32_t dataSize = frame.pixels.size() * 2;
            file.write(reinterpret_cast<const char*>(&dataSize), sizeof(dataSize));
            file.write(reinterpret_cast<const char*>(frame.pixels.data()), dataSize);
        }
    } catch(...) {}
}

void AnimatedGIFSprite::workerLoop() {
    while (s_workerRunning) {
        GIFTask task;
        {
            std::unique_lock<std::mutex> lock(s_queueMutex);
            s_queueCV.wait(lock, []{ return !s_taskQueue.empty() || !s_workerRunning; });
            
            if (!s_workerRunning) break;
            
            task = s_taskQueue.front();
            s_taskQueue.pop_front();
        }
        
        // proceso la tarea que toque
        if (task.isData) {
            // decodificar desde memoria
            if (!GIFDecoder::isGIF(task.data.data(), task.data.size())) {
                Loader::get()->queueInMainThread([cb = task.callback]() { if (cb) cb(nullptr); });
                continue;
            }
            
            auto gifData = GIFDecoder::decode(task.data.data(), task.data.size());
            
            Loader::get()->queueInMainThread([key = task.key, gifData = std::move(gifData), cb = task.callback]() mutable {
                if (gifData.frames.empty()) {
                    if (cb) cb(nullptr);
                    return;
                }
                
                auto ret = new AnimatedGIFSprite();
                if (ret) {
                    ret->m_filename = key;
                    ret->m_canvasWidth = gifData.width;
                    ret->m_canvasHeight = gifData.height;

                    if (!ret->init()) {
                        CC_SAFE_DELETE(ret);
                        if (cb) cb(nullptr);
                        return;
                    }

                    float sf = getContentScaleFactorSafe();
                    ret->setContentSize(CCSize(ret->m_canvasWidth / sf, ret->m_canvasHeight / sf));

                    // proceso todos los frames ya pa dejar el GIF entero en cache
                    for (const auto& frameData : gifData.frames) {
                        auto* gifFrame = new GIFFrame();
                        auto* texture = new CCTexture2D();

                        bool success = false;
                        try {
                            auto pixels4444 = convertToRGBA4444(
                                reinterpret_cast<const uint32_t*>(frameData.pixels.data()),
                                frameData.width, frameData.height);

                            success = texture->initWithData(
                                pixels4444.data(),
                                kCCTexture2DPixelFormat_RGBA4444,
                                frameData.width,
                                frameData.height,
                                CCSize(frameData.width / sf, frameData.height / sf)
                            );
                        } catch (...) { success = false; }

                        if (success) {
                            texture->setAntiAliasTexParameters();
                            gifFrame->texture = texture;
                            gifFrame->delay = frameData.delayMs / 1000.0f;
                            gifFrame->rect = CCRect(frameData.left, frameData.top, frameData.width, frameData.height);
                            ret->m_frames.push_back(gifFrame);
                            ret->m_frameColors.push_back({ {0,0,0}, {255,255,255} });
                            texture->retain();
                        } else {
                            delete gifFrame;
                            texture->release();
                        }
                    }

                    if (ret->m_frames.empty()) {
                        CC_SAFE_DELETE(ret);
                        if (cb) cb(nullptr);
                        return;
                    }

                    // guardo en cache con todos los frames ya listos
                    SharedGIFData cacheEntry;
                    cacheEntry.width = ret->m_canvasWidth;
                    cacheEntry.height = ret->m_canvasHeight;

                    for (auto* frame : ret->m_frames) {
                        cacheEntry.textures.push_back(frame->texture);
                        cacheEntry.delays.push_back(frame->delay);
                        cacheEntry.frameRects.push_back(frame->rect);
                    }

                    s_gifCache[key] = cacheEntry;

                    if (!isPinned(key)) {
                        s_lruList.push_back(key);
                    }

                    log::info("[AnimatedGIFSprite] Cached complete GIF from data with key: {} ({} frames)", key, ret->m_frames.size());

                    // pongo el primer frame y arranco la animación
                    ret->setCurrentFrame(0);
                    ret->scheduleUpdate();
                    ret->autorelease();

                    if (cb) cb(ret);
                } else {
                    if (cb) cb(nullptr);
                }
            });
            
        } else {
            // decodificar desde archivo
            // primero intento tirar del cache en disco
            DiskCacheEntry cachedEntry;
            if (loadFromDiskCache(task.path, cachedEntry)) {
                Loader::get()->queueInMainThread([path = task.path, cachedEntry = std::move(cachedEntry), cb = task.callback]() mutable {
                    auto ret = new AnimatedGIFSprite();
                    if (ret) {
                        ret->m_filename = path;
                        ret->m_canvasWidth = cachedEntry.width;
                        ret->m_canvasHeight = cachedEntry.height;
                        
                        if (!ret->init()) {
                            CC_SAFE_DELETE(ret);
                            if (cb) cb(nullptr);
                            return;
                        }
                        
                        float sf = getContentScaleFactorSafe();
                        ret->setContentSize(CCSize(ret->m_canvasWidth / sf, ret->m_canvasHeight / sf));
                        
                        // cargo todos los frames desde el cache (ya vienen procesados)
                        for (const auto& frame : cachedEntry.frames) {
                            auto texture = new CCTexture2D();
                            texture->initWithData(
                                frame.pixels.data(),
                                kCCTexture2DPixelFormat_RGBA4444,
                                frame.width,
                                frame.height,
                                CCSize(frame.width / sf, frame.height / sf)
                            );
                            texture->setAntiAliasTexParameters();
                            
                            auto* gifFrame = new GIFFrame();
                            gifFrame->texture = texture;
                            gifFrame->delay = frame.delay;
                            gifFrame->rect = CCRect(0, 0, frame.width, frame.height);
                            
                            ret->m_frames.push_back(gifFrame);
                            ret->m_frameColors.push_back({ {0,0,0}, {255,255,255} });
                        }
                        
                        if (!ret->m_frames.empty()) {
                            ret->setCurrentFrame(0);
                            ret->scheduleUpdate();
                        }
                        
                        ret->autorelease();
                        if (cb) cb(ret);
                    } else {
                        if (cb) cb(nullptr);
                    }
                });
                continue;
            }

            std::ifstream file(task.path, std::ios::binary);
            if (!file) {
                Loader::get()->queueInMainThread([cb = task.callback]() { if (cb) cb(nullptr); });
                continue;
            }

            std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            file.close();

            if (!GIFDecoder::isGIF(data.data(), data.size())) {
                Loader::get()->queueInMainThread([cb = task.callback]() { if (cb) cb(nullptr); });
                continue;
            }

            auto gifData = GIFDecoder::decode(data.data(), data.size());
            
            // guardo en cache de disco (ya convertidos a RGBA4444)
            DiskCacheEntry newCacheEntry;
            newCacheEntry.width = gifData.width;
            newCacheEntry.height = gifData.height;
            
            for (const auto& frame : gifData.frames) {
                DiskCacheEntry::Frame cacheFrame;
                cacheFrame.delay = frame.delayMs / 1000.0f;
                cacheFrame.width = frame.width;
                cacheFrame.height = frame.height;
                cacheFrame.pixels = convertToRGBA4444(reinterpret_cast<const uint32_t*>(frame.pixels.data()), frame.width, frame.height);
                newCacheEntry.frames.push_back(std::move(cacheFrame));
            }
            saveToDiskCache(task.path, newCacheEntry);
            
            Loader::get()->queueInMainThread([path = task.path, gifData = std::move(gifData), cb = task.callback]() mutable {
                if (gifData.frames.empty()) {
                    if (cb) cb(nullptr);
                    return;
                }
                
                auto ret = new AnimatedGIFSprite();
                if (ret) {
                    ret->m_filename = path;
                    ret->m_canvasWidth = gifData.width;
                    ret->m_canvasHeight = gifData.height;
                    ret->m_pendingFrames = std::move(gifData.frames);

                    if (!ret->init()) {
                        CC_SAFE_DELETE(ret);
                        if (cb) cb(nullptr);
                        return;
                    }

                    float sf = getContentScaleFactorSafe();
                    ret->setContentSize(CCSize(ret->m_canvasWidth / sf, ret->m_canvasHeight / sf));

                    // me aseguro de tener al menos un frame antes del callback
                    ret->processNextPendingFrame();

                    // el resto de frames se cargan poco a poco en el update
                    if (!ret->m_pendingFrames.empty()) {
                        ret->schedule(schedule_selector(AnimatedGIFSprite::updateTextureLoading));
                    } else {
                        ret->scheduleUpdate();
                    }
                    ret->autorelease();

                    if (cb) cb(ret);
                } else {
                    if (cb) cb(nullptr);
                }
            });
        }
    }
}

void AnimatedGIFSprite::clearCache() {
    for (auto& [key, data] : s_gifCache) {
        for (auto* tex : data.textures) {
            if (tex) tex->release();
        }
    }
    s_gifCache.clear();
    s_lruList.clear();
    PaimonDebug::log("[AnimatedGIFSprite] Cache cleared");
}

void AnimatedGIFSprite::remove(const std::string& filename) {
    auto it = s_gifCache.find(filename);
    if (it != s_gifCache.end()) {
        for (auto* tex : it->second.textures) {
            if (tex) tex->release();
        }
        s_gifCache.erase(it);
        s_lruList.remove(filename);
        PaimonDebug::log("[AnimatedGIFSprite] Removed from cache: {}", filename);
    }
}

bool AnimatedGIFSprite::isCached(const std::string& filename) {
    return s_gifCache.find(filename) != s_gifCache.end();
}

AnimatedGIFSprite::~AnimatedGIFSprite() {
    PaimonDebug::log("[AnimatedGIFSprite] Destroying sprite for file: {}", m_filename);
    this->unscheduleUpdate();
    this->setTexture(nullptr);
    
    for (auto* frame : m_frames) {
        if (frame) {
            delete frame;
        }
    }
    m_frames.clear();
}

bool AnimatedGIFSprite::initFromCache(const std::string& cacheKey) {
    m_filename = cacheKey;

    // comprobar cache primero
    auto it = s_gifCache.find(cacheKey);
    if (it == s_gifCache.end()) {
        return false;
    }

    const auto& cachedData = it->second;
    m_canvasWidth = cachedData.width;
    m_canvasHeight = cachedData.height;
    
    // actualizar LRU
    s_lruList.remove(cacheKey);
    s_lruList.push_back(cacheKey);
    
    PaimonDebug::log("[AnimatedGIFSprite] Cache hit for: {}, size: {}x{}, frames: {}", 
        cacheKey, m_canvasWidth, m_canvasHeight, cachedData.textures.size());

    for (size_t i = 0; i < cachedData.textures.size(); ++i) {
        auto* gifFrame = new GIFFrame();
        gifFrame->texture = cachedData.textures[i];
        gifFrame->texture->retain(); // retain pa esta instancia del sprite
        gifFrame->delay = (i < cachedData.delays.size()) ? cachedData.delays[i] : 0.1f;
        gifFrame->rect = (i < cachedData.frameRects.size()) ? cachedData.frameRects[i] : CCRect(0, 0, m_canvasWidth, m_canvasHeight);
        m_frames.push_back(gifFrame);
        
        // colores por defecto pa evitar indice fuera de rango
        m_frameColors.push_back({ {0,0,0}, {255,255,255} });
    }
    
    if (m_frames.empty()) {
        log::error("[AnimatedGIFSprite] Cached frames empty for: {}", cacheKey);
        return false;
    }
    
    // init con primer frame
    if (!CCSprite::init()) {
        log::error("[AnimatedGIFSprite] CCSprite::init failed");
        return false;
    }

    float sf = getContentScaleFactorSafe();
    this->setContentSize(CCSize(m_canvasWidth / sf, m_canvasHeight / sf));
    this->setCurrentFrame(0);
    this->scheduleUpdate();
    
    return true;
}

AnimatedGIFSprite* AnimatedGIFSprite::createFromCache(const std::string& key) {
    auto ret = new (std::nothrow) AnimatedGIFSprite();
    if (ret && ret->initFromCache(key)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

void AnimatedGIFSprite::createAsync(const std::vector<uint8_t>& data, const std::string& key, AsyncCallback callback) {
    if (data.empty()) {
        if (callback) callback(nullptr);
        return;
    }

    // comprobar cache primero
    if (isCached(key)) {
        auto ret = createFromCache(key);
        if (callback) callback(ret);
        return;
    }

    initWorker();

    {
        std::lock_guard<std::mutex> lock(s_queueMutex);
        GIFTask task;
        task.data = data;
        task.key = key;
        task.callback = callback;
        task.isData = true;
        s_taskQueue.push_back(task);
    }
    s_queueCV.notify_one();
}



void AnimatedGIFSprite::createAsync(const std::string& path, AsyncCallback callback) {
    if (!std::filesystem::exists(path)) {
        if (callback) callback(nullptr);
        return;
    }

    // comprobar cache primero (Main Thread)
    if (isCached(path)) {
        auto ret = createFromCache(path);
        if (callback) callback(ret);
        return;
    }

    initWorker();

    {
        std::lock_guard<std::mutex> lock(s_queueMutex);
        GIFTask task;
        task.path = path;
        task.callback = callback;
        task.isData = false;
        s_taskQueue.push_back(task);
    }
    s_queueCV.notify_one();
}

void AnimatedGIFSprite::update(float dt) {
    CCSprite::update(dt);
    updateAnimation(dt);
}

void AnimatedGIFSprite::updateAnimation(float dt) {
    if (!m_isPlaying || m_frames.empty()) {
        return;
    }
    
    m_frameTimer += dt;
    
    // Obtener el delay del frame actual
    float currentDelay = 0.1f;
    if (m_currentFrame < m_frames.size() && m_frames[m_currentFrame]) {
        currentDelay = m_frames[m_currentFrame]->delay;
    }
    if (currentDelay <= 0.0f) {
        currentDelay = 0.1f; // Delay por defecto de 100ms
    }
    
    if (m_frameTimer >= currentDelay) {
        m_frameTimer = 0.0f;
        
        // Avanzar al siguiente frame
        m_currentFrame++;
        
        if (m_currentFrame >= m_frames.size()) {
            if (m_loop) {
                m_currentFrame = 0;
            } else {
                m_currentFrame = m_frames.size() - 1;
                m_isPlaying = false;
                return;
            }
        }
        
        setCurrentFrame(m_currentFrame);
    }
}

void AnimatedGIFSprite::setCurrentFrame(unsigned int frame) {
    if (frame >= m_frames.size()) {
        log::warn("[AnimatedGIFSprite] Invalid frame index: {}", frame);
        return;
    }
    
    m_currentFrame = frame;
    m_frameTimer = 0.0f;
    
    if (m_frames[m_currentFrame] && m_frames[m_currentFrame]->texture) {
        auto* gifFrame = m_frames[m_currentFrame];

        float sf = getContentScaleFactorSafe();
        
        // offset pa CCSpriteFrame (gif top-left, cocos bottom-left)
        float left = gifFrame->rect.origin.x;
        float top = gifFrame->rect.origin.y;
        float w = gifFrame->rect.size.width;
        float h = gifFrame->rect.size.height;
        
        float centerX = left + w / 2.0f;
        float centerY = (m_canvasHeight - top) - h / 2.0f;
        
        float canvasCenterX = m_canvasWidth / 2.0f;
        float canvasCenterY = m_canvasHeight / 2.0f;
        
        CCPoint offset((centerX - canvasCenterX) / sf, (centerY - canvasCenterY) / sf);
        
        // comprobar si la textura es placeholder (mas chica que el frame)
        auto texPx = gifFrame->texture->getContentSizeInPixels();
        bool isPlaceholder = (texPx.width < w || texPx.height < h);
        
        CCRect rectToUse = CCRect(0, 0, w, h);
        CCPoint offsetToUse = offset;
        
        if (isPlaceholder) {
            rectToUse = CCRect(0, 0, texPx.width, texPx.height);
            offsetToUse = CCPoint(0, 0);
        }

        auto spriteFrame = CCSpriteFrame::createWithTexture(
            gifFrame->texture, 
            rectToUse, 
            false, 
            offsetToUse, 
            CCSize(m_canvasWidth / sf, m_canvasHeight / sf)
        );
        
        this->setDisplayFrame(spriteFrame);
    }
}

void AnimatedGIFSprite::draw() {
    if (getShaderProgram()) {
        getShaderProgram()->use();
        getShaderProgram()->setUniformsForBuiltins();

        GLint intensityLoc = getShaderProgram()->getUniformLocationForName("u_intensity");
        if (intensityLoc != -1) {
            getShaderProgram()->setUniformLocationWith1f(intensityLoc, m_intensity);
        }

        GLint timeLoc = getShaderProgram()->getUniformLocationForName("u_time");
        if (timeLoc != -1) {
            getShaderProgram()->setUniformLocationWith1f(timeLoc, m_time);
        }

        GLint brightLoc = getShaderProgram()->getUniformLocationForName("u_brightness");
        if (brightLoc != -1) {
            getShaderProgram()->setUniformLocationWith1f(brightLoc, m_brightness);
        }

        GLint sizeLoc = getShaderProgram()->getUniformLocationForName("u_texSize");
        if (sizeLoc != -1) {
            if (m_texSize.width == 0 && getTexture()) {
                m_texSize = getTexture()->getContentSizeInPixels();
            }
            float w = m_texSize.width > 0 ? m_texSize.width : 1.0f;
            float h = m_texSize.height > 0 ? m_texSize.height : 1.0f;
            getShaderProgram()->setUniformLocationWith2f(sizeLoc, w, h);
        }
    }

    CCSprite::draw();
}

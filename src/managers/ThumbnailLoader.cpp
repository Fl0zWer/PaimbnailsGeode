#include "ThumbnailLoader.hpp"
#include "LocalThumbs.hpp"
#include "LevelColors.hpp"
#include "../utils/Constants.hpp"
#include "../utils/HttpClient.hpp"
#include "../utils/DominantColors.hpp"
#include "../utils/GIFDecoder.hpp"
#include "../utils/Debug.hpp"
#define STB_IMAGE_IMPLEMENTATION
#include "../utils/stb_image.h"
#include <Geode/loader/Log.hpp>
#include <Geode/loader/Mod.hpp>
#include <Geode/utils/string.hpp>
#include <Geode/cocos/base_nodes/CCNode.h>
#include <Geode/cocos/cocoa/CCGeometry.h>
#include <filesystem>
#include <fstream>
#include <atomic>
#include <thread>
#include <cmath>
#include <algorithm>

using namespace geode::prelude;

ThumbnailLoader& ThumbnailLoader::get() {
    static ThumbnailLoader instance;
    return instance;
}

ThumbnailLoader::ThumbnailLoader() {
    m_maxConcurrentTasks = 5; // por defecto en 5 como se pidio
    initDiskCache();
}

ThumbnailLoader::~ThumbnailLoader() {
    // le aviso a los threads de background que paren
    m_shuttingDown = true;

    // durante el cierre del proceso (destructores estaticos) el orden de destruccion
    // es indefinido: Cocos2d, el autorelease pool y otros singletons pueden ya estar muertos.
    // geode::Ref llama release() al destruirse. Usamos take() para sacar los punteros sin release().
    for (auto& [id, ref] : m_textureCache) {
        (void)ref.take();
    }
}

void ThumbnailLoader::initDiskCache() {
    std::thread([this]() {
        geode::utils::thread::setName("ThumbnailLoader Cache Init");
        std::lock_guard<std::mutex> lock(m_diskMutex);
        auto path = Mod::get()->getSaveDir() / "cache";
        PaimonDebug::log("[ThumbnailLoader] iniciando cache de disco en: {}", geode::utils::string::pathToString(path));
        
        std::error_code ec;
        if (!std::filesystem::exists(path, ec)) {
            std::filesystem::create_directories(path, ec);
            PaimonDebug::log("[ThumbnailLoader] carpeta de cache creada");
            return;
        }
        
        // limpieza al inicio: ya no se borra nada aqui
        // si clear-cache-on-exit esta activo se borro al cerrar la sesion anterior
        // si no, el cache se mantiene entre sesiones
        const bool clearOnStart = false;

        int deletedCount = 0;
        int keptCount = 0;
        
        auto now = std::filesystem::file_time_type::clock::now();
        auto defaultRetention = std::chrono::hours(24 * 15); // 15 dias, por si alguna vez lo uso

        std::error_code dirEc;
        for (auto const& entry : std::filesystem::directory_iterator(path, dirEc)) {
            if (dirEc) break;
            // compruebo si estamos en shutdown antes de seguir
            if (m_shuttingDown.load(std::memory_order_relaxed)) {
                PaimonDebug::log("[ThumbnailLoader] shutdown durante init cache, abortando");
                return;
            }
            if (entry.is_regular_file()) {
                auto stem = geode::utils::string::pathToString(entry.path().stem());
                // saco el id desde el nombre del archivo
                int id = 0;
                bool isGif = false;
                if (stem.find("_anim") != std::string::npos) {
                    std::string idStr = stem.substr(0, stem.find("_anim"));
                    id = geode::utils::numFromString<int>(idStr).unwrapOr(0);
                    isGif = true;
                } else {
                    id = geode::utils::numFromString<int>(stem).unwrapOr(0);
                }

                if (id == 0) {
                    // si el nombre no es un id valido lo elimino
                    std::error_code rmEc;
                    std::filesystem::remove(entry.path(), rmEc);
                    continue;
                }

                // miro si es main level
                int realID = std::abs(id);
                bool isMain = (realID >= 1 && realID <= 22);

                // si es main level no lo borro ni por tiempo ni por sesion
                if (!isMain) {
                    if (clearOnStart) {
                        std::error_code rmEc;
                        std::filesystem::remove(entry.path(), rmEc);
                        deletedCount++;
                        continue;
                    }
                }

                m_diskCache.insert(isGif ? -id : id);
                keptCount++;
            }
        }
        
        PaimonDebug::log("[ThumbnailLoader] cache de disco lista. borrados: {}, guardados: {}", deletedCount, keptCount);
    }).detach();
}

void ThumbnailLoader::setMaxConcurrentTasks(int max) {
    // dejo hasta 100 descargas simultaneas por si te quieres pasar
    m_maxConcurrentTasks = std::max(1, std::min(100, max));
}

bool ThumbnailLoader::isLoaded(int levelID, bool isGif) const {
    int key = isGif ? -levelID : levelID;
    return m_textureCache.find(key) != m_textureCache.end();
}

bool ThumbnailLoader::isPending(int levelID, bool isGif) const {
    int key = isGif ? -levelID : levelID;
    return m_tasks.find(key) != m_tasks.end();
}

bool ThumbnailLoader::isFailed(int levelID, bool isGif) const {
    int key = isGif ? -levelID : levelID;
    return m_failedCache.find(key) != m_failedCache.end();
}

bool ThumbnailLoader::hasGIFData(int levelID) const {
    return m_gifLevels.find(levelID) != m_gifLevels.end();
}

std::filesystem::path ThumbnailLoader::getCachePath(int levelID, bool isGif) {
    if (isGif) {
        return Mod::get()->getSaveDir() / "cache" / fmt::format("{}_anim.gif", levelID);
    }
    return Mod::get()->getSaveDir() / "cache" / fmt::format("{}.png", levelID);
}

void ThumbnailLoader::requestLoad(int levelID, std::string fileName, LoadCallback callback, int priority, bool isGif) {
    int key = isGif ? -levelID : levelID;
    
    // lock protege m_textureCache, m_failedCache y m_tasks de carreras con finishTask
    std::lock_guard<std::mutex> lock(m_queueMutex);

    // 1. reviso cache en RAM
    auto it = m_textureCache.find(key);
    if (it != m_textureCache.end()) {
        // actualizo la LRU
        m_lruOrder.remove(key);
        m_lruOrder.push_back(key);
        
        // fuerzo callback asincrono para no trabar la UI
        auto tex = it->second;
        Loader::get()->queueInMainThread([callback, tex]() {
            if (callback) callback(tex, true);
        });
        return;
    }

    // 2. miro el cache de fallos
    if (m_failedCache.count(key)) {
        if (callback) callback(nullptr, false);
        return;
    }

    // 3. reviso si ya hay una tarea en cola
    auto taskIt = m_tasks.find(key);
    if (taskIt != m_tasks.end()) {
        // solo agrego el callback a la tarea existente
        if (callback) taskIt->second->callbacks.push_back(callback);
        // si viene con mas prioridad se la subo
        if (priority > taskIt->second->priority) {
            taskIt->second->priority = priority;
            // el reordenado se hace luego en processQueue
        }
        return;
    }

    // 4. creo una tarea nueva
    auto task = std::make_shared<Task>();
    task->levelID = key;
    task->fileName = fileName;
    task->priority = priority;
    if (callback) task->callbacks.push_back(callback);

    m_tasks[key] = task;
    m_queueOrder.push_back(key);
    
    processQueue();
}

void ThumbnailLoader::cancelLoad(int levelID, bool isGif) {
    int key = isGif ? -levelID : levelID;
    std::lock_guard<std::mutex> lock(m_queueMutex);
    auto it = m_tasks.find(key);
    if (it != m_tasks.end()) {
        it->second->cancelled = true;
        // si ya va corriendo no la paro, solo ignoro el resultado
        // si esta en cola con marcarla cancelada me sobra
    }
}

void ThumbnailLoader::processQueue() {
    // debe llamarse con m_queueMutex pillado

    // modo batch: si esta activo no arranco nada nuevo si ya hay algo corriendo
    // asi termino el batch actual antes de empezar el siguiente
    if (m_batchMode && m_activeTaskCount > 0) {
        return;
    }

    // busco el mejor candidato para correr ahora
    while (m_activeTaskCount < m_maxConcurrentTasks && !m_queueOrder.empty()) {
        // encuentro la tarea con mayor prioridad
        auto bestIt = m_queueOrder.end();
        int maxPrio = -9999;

        for (auto it = m_queueOrder.begin(); it != m_queueOrder.end(); ++it) {
            // por si acaso: aseguro que la tarea todavia existe
            if (m_tasks.find(*it) == m_tasks.end()) continue;
            
            auto task = m_tasks[*it];
            if (!task) continue;

            if (task->cancelled) continue; // me salto las canceladas
            
            if (task->priority > maxPrio) {
                maxPrio = task->priority;
                bestIt = it;
            }
        }

        if (bestIt == m_queueOrder.end()) {
            // si llegamos aqui es que todas las que quedan estan canceladas
            m_queueOrder.clear();
            // limpio las tareas canceladas del mapa
            for (auto it = m_tasks.begin(); it != m_tasks.end();) {
                if (it->second->cancelled && !it->second->running) {
                    it = m_tasks.erase(it);
                } else {
                    ++it;
                }
            }
            break;
        }

        int levelID = *bestIt;
        m_queueOrder.erase(bestIt);
        
        auto task = m_tasks[levelID];
        if (task->cancelled) continue;

        startTask(task);
    }
}

void ThumbnailLoader::startTask(std::shared_ptr<Task> task) {
    task->running = true;
    m_activeTaskCount++;

    // siempre tiro de disco primero para evitar carreras con initDiskCache
    // workerLoadFromDisk mira el FS y si no encuentra descarga
    std::thread([this, task]() {
        geode::utils::thread::setName("ThumbnailLoader Disk Worker");
        workerLoadFromDisk(task);
    }).detach();
}

void ThumbnailLoader::workerLoadFromDisk(std::shared_ptr<Task> task) {
    if (task->cancelled) {
        Loader::get()->queueInMainThread([this, task]() {
            finishTask(task, nullptr, false);
        });
        return;
    }

    bool isGif = task->levelID < 0;
    int realID = std::abs(task->levelID);
    auto path = getCachePath(realID, isGif);
    std::vector<uint8_t> data;
    bool success = false;

    std::error_code ec;
    if (std::filesystem::exists(path, ec)) {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (file.is_open()) {
            size_t size = file.tellg();
            file.seekg(0, std::ios::beg);
            data.resize(size);
            file.read(reinterpret_cast<char*>(data.data()), size);
            success = static_cast<bool>(file);
            if (success) {
                PaimonDebug::log("[ThumbnailLoader] cargados {} bytes del disco pal nivel {}{}", size, realID, isGif ? " (gif)" : "");
            }
        } else {
            PaimonDebug::warn("[ThumbnailLoader] no se pudo abrir archivo pal nivel {}", realID);
        }
    }

    if (success && !data.empty()) {
        // optimizado: decodifico y proceso en un thread aparte
        bool isNativeGif = GIFDecoder::isGIF(data.data(), data.size());
        
        if (isNativeGif) {
            // procesamiento del GIF en main thread (por los cocos/sprites)
            Loader::get()->queueInMainThread([this, task, data, realID]() {
                if (task->cancelled) { finishTask(task, nullptr, false); return; }
                
                m_gifLevels.insert(realID);
                auto image = new CCImage();
                if (image->initWithImageData(const_cast<uint8_t*>(data.data()), data.size())) {
                    auto tex = new CCTexture2D();
                    if (tex->initWithImage(image)) {
                        image->release();
                        tex->autorelease();
                        finishTask(task, tex, true);
                    } else {
                        image->release();
                        tex->release();
                        workerDownload(task);
                    }
                } else {
                    image->release();
                    workerDownload(task);
                }
            });
        } 
        else {
            // imagen estatica (png/jpg/webp)
            // uso stb_image para decodificar fuera del main thread
            int w = 0, h = 0, ch = 0;
            unsigned char* pixels = stbi_load_from_memory(data.data(), (int)data.size(), &w, &h, &ch, 4); // fuerzo RGBA

            if (pixels) {
                // saco los colores en este mismo thread
                if (!LevelColors::get().getPair(realID)) {
                    LevelColors::get().extractFromRawData(realID, pixels, w, h, true);
                }
                
                // copio los pixeles a un vector para pasarlos al main sin sustos
                std::vector<uint8_t> rawData(pixels, pixels + (w * h * 4));
                stbi_image_free(pixels);
                
                Loader::get()->queueInMainThread([this, task, rawData, w, h, realID]() {
                    if (task->cancelled) { finishTask(task, nullptr, false); return; }
                    
                    auto tex = new CCTexture2D();
                    if (tex->initWithData(rawData.data(), kCCTexture2DPixelFormat_RGBA8888, w, h, CCSize((float)w, (float)h))) {
                        tex->autorelease();
                        finishTask(task, tex, true);
                } else {
                    tex->release();
                    // si falla crear la textura casi siempre es por el formato, reintento por red
                    PaimonDebug::warn("[ThumbnailLoader] fallo crear textura pal nivel {}", realID);
                    workerDownload(task);
                }
                });
            } else {
                 PaimonDebug::warn("[ThumbnailLoader] fallo stb decode pal nivel {}", realID);
                 workerDownload(task);
            }
        }
    } else {
        // fallo lectura, intento descargar
        workerDownload(task);
    }
}

void ThumbnailLoader::workerDownload(std::shared_ptr<Task> task) {
    if (task->cancelled) {
        Loader::get()->queueInMainThread([this, task]() {
            finishTask(task, nullptr, false);
        });
        return;
    }

    int realID = std::abs(task->levelID);
    bool isGif = task->levelID < 0;

    Loader::get()->queueInMainThread([this, task, realID, isGif]() {
        HttpClient::get().downloadThumbnail(realID, isGif, 
            [this, task, isGif, realID](bool success, std::vector<uint8_t> const& data, int w, int h) {
                if (task->cancelled) {
                    finishTask(task, nullptr, false);
                    return;
                }

                if (success && !data.empty()) {
                    // empiezo procesamiento en thread background
                    std::thread([this, task, data, isGif, realID]() {
                        geode::utils::thread::setName("ThumbnailLoader Download Worker");
                        // 1. guardo en disco
                        {
                            auto path = getCachePath(realID, isGif);
                            std::ofstream file(path, std::ios::binary);
                            if (file) {
                                file.write(reinterpret_cast<char const*>(data.data()), data.size());
                                file.close();

                                std::lock_guard<std::mutex> lock(m_diskMutex);
                                m_diskCache.insert(task->levelID);
                            } else {
                                log::error("[ThumbnailLoader] no se pudo abrir archivo para guardar en disco");
                            }
                        }
                        
                        // 2. decodifico y extraco colores en background
                        if (GIFDecoder::isGIF(data.data(), data.size())) {
                            // logica gif (mando a main thread)
                            Loader::get()->queueInMainThread([this, task, data, realID]() {
                                m_gifLevels.insert(realID);
                                auto image = new CCImage();
                                if (image->initWithImageData(const_cast<uint8_t*>(data.data()), data.size())) {
                                    auto tex = new CCTexture2D();
                                    if (tex->initWithImage(image)) {
                                        image->release();
                                        tex->autorelease();
                                        finishTask(task, tex, true);
                                    } else {
                                        image->release();
                                        tex->release();
                                        finishTask(task, nullptr, false);
                                    }
                                } else {
                                    image->release();
                                    finishTask(task, nullptr, false);
                                }
                            });
                        } else {
                            // imagen estatica optimizada
                            int sw = 0, sh = 0, ch = 0;
                            unsigned char* pixels = stbi_load_from_memory(data.data(), (int)data.size(), &sw, &sh, &ch, 4);
                             
                            if (pixels) {
                                if (!LevelColors::get().getPair(realID)) {
                                    LevelColors::get().extractFromRawData(realID, pixels, sw, sh, true);
                                }
                                
                                std::vector<uint8_t> rawData(pixels, pixels + (sw * sh * 4));
                                stbi_image_free(pixels);
                                
                                Loader::get()->queueInMainThread([this, task, rawData, sw, sh]() {
                                    if (task->cancelled) { finishTask(task, nullptr, false); return; }
                                    
                                    auto tex = new CCTexture2D();
                                    if (tex->initWithData(rawData.data(), kCCTexture2DPixelFormat_RGBA8888, sw, sh, CCSize((float)sw, (float)sh))) {
                                        tex->autorelease();
                                        finishTask(task, tex, true);
                                    } else {
                                        tex->release();
                                        finishTask(task, nullptr, false);
                                    }
                                });
                            } else {
                                // si ni stb_image quiere, marco fallo y ya esta
                                Loader::get()->queueInMainThread([this, task]() {
                                     finishTask(task, nullptr, false);
                                });
                            }
                        }
                    }).detach();
                } else {
                    finishTask(task, nullptr, false);
                }
            }
        );
    });
}

void ThumbnailLoader::finishTask(std::shared_ptr<Task> task, cocos2d::CCTexture2D* texture, bool success) {
    // main thread: lock protege m_textureCache, m_failedCache, m_tasks
    std::lock_guard<std::mutex> lock(m_queueMutex);
    
    if (success && texture) {
        addToCache(task->levelID, texture);
    } else {
        if (!task->cancelled) {
            m_failedCache.insert(task->levelID);
        }
    }

    // los callbacks fuera del lock serian mas correctos, pero como estamos en main thread
    // y los callbacks no deberian re-entrar requestLoad de forma inmediata, es seguro aqui
    if (!task->cancelled) {
        for (auto& cb : task->callbacks) {
            if (cb) cb(texture, success);
        }
    }

    // limpieza
    m_tasks.erase(task->levelID);
    m_activeTaskCount--;
    
    // proceso el siguiente
    processQueue();
}

void ThumbnailLoader::addToCache(int levelID, cocos2d::CCTexture2D* texture) {
    if (!texture) return;
    
    m_textureCache[levelID] = texture;
    
    m_lruOrder.remove(levelID);
    m_lruOrder.push_back(levelID);
    
    // recorto cache si pasa del maximo
    while (m_lruOrder.size() > MAX_CACHE_SIZE) {
        int removeID = m_lruOrder.front();
        m_lruOrder.pop_front();
        m_textureCache.erase(removeID);
    }
}

void ThumbnailLoader::clearCache() {
    m_textureCache.clear();
    m_lruOrder.clear();
    m_failedCache.clear();
}

void ThumbnailLoader::invalidateLevel(int levelID, bool isGif) {
    int key = isGif ? -levelID : levelID;
    // incremento la version de invalidacion para que los consumidores sepan que hay cambio
    m_invalidationVersions[levelID]++;

    // quito la entrada de la RAM
    auto it = m_textureCache.find(key);
    if (it != m_textureCache.end()) {
        m_textureCache.erase(it);
        m_lruOrder.remove(key);
    }

    // quito del cache de fallos
    m_failedCache.erase(key);

    // borro tambien el archivo en disco
    std::thread([this, levelID, isGif]() {
        geode::utils::thread::setName("ThumbnailLoader Invalidator");
        std::error_code ec;
        auto path = getCachePath(levelID, isGif);
        if (std::filesystem::exists(path, ec)) {
            std::filesystem::remove(path, ec);
        }
        std::lock_guard<std::mutex> lock(m_diskMutex);
        m_diskCache.erase(isGif ? -levelID : levelID);
    }).detach();
}

int ThumbnailLoader::getInvalidationVersion(int levelID) const {
    auto it = m_invalidationVersions.find(levelID);
    return it != m_invalidationVersions.end() ? it->second : 0;
}

void ThumbnailLoader::clearDiskCache() {
    std::thread([this]() {
        geode::utils::thread::setName("ThumbnailLoader Disk Clear");
        std::error_code ec;
        std::filesystem::remove_all(Mod::get()->getSaveDir() / "cache", ec);
        if (ec) {
            log::error("[ThumbnailLoader] error limpiando cache de disco: {}", ec.message());
        }
        std::lock_guard<std::mutex> lock(m_diskMutex);
        m_diskCache.clear();
        initDiskCache(); // vuelvo a crear la carpeta
    }).detach();
}

void ThumbnailLoader::clearPendingQueue() {
    std::lock_guard<std::mutex> lock(m_queueMutex);
    for (auto& [id, task] : m_tasks) {
        task->cancelled = true;
    }
    // no limpio el mapa aqui porque algunas siguen corriendo
    // con marcarlas canceladas me vale
}

void ThumbnailLoader::updateSessionCache(int levelID, cocos2d::CCTexture2D* texture) {
    addToCache(levelID, texture);
}

void ThumbnailLoader::cleanup() {
    clearPendingQueue();
    clearCache();
}

bool ThumbnailLoader::isTextureSane(cocos2d::CCTexture2D* tex) {
    if (!tex) return false;
    uintptr_t addr = reinterpret_cast<uintptr_t>(tex);
    if (addr < 0x10000) return false; // puntero nulo o muy bajo, no es valido

#ifdef GEODE_IS_WINDOWS
    __try {
        auto sz = tex->getContentSize();
        if (std::isnan(sz.width) || std::isnan(sz.height)) return false;
        return sz.width > 0.f && sz.height > 0.f;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
#else
    return true;
#endif
}


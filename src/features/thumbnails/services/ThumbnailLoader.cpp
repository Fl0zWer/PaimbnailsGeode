#include "ThumbnailLoader.hpp"
#include "LocalThumbs.hpp"
#include "LevelColors.hpp"
#include "../../../utils/Constants.hpp"
#include "../../../utils/HttpClient.hpp"
#include "../../../utils/DominantColors.hpp"
#include "../../../utils/GIFDecoder.hpp"
#include "../../../utils/Debug.hpp"
#define STB_IMAGE_IMPLEMENTATION
#include "../../../utils/stb_image.h"
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
    m_maxConcurrentTasks = 20; // aumentado para descargas mas rapidas
    initDiskCache();
}

ThumbnailLoader::~ThumbnailLoader() {
    // le aviso a los threads de background que paren
    m_shuttingDown = true;

    // durante el cierre del proceso (destructores estaticos) el orden de destruccion
    // es indefinido: Cocos2d, el autorelease pool y otros singletons pueden ya estar muertos.
    // geode::Ref llama release() al destruirse. Usamos take() para sacar los punteros sin release().
    std::lock_guard<std::mutex> lock(m_queueMutex);
    for (auto& [id, ref] : m_textureCache) {
        (void)ref.take();
    }
    m_textureCache.clear();
}

void ThumbnailLoader::initDiskCache() {
    // hilo de I/O de disco — no migrable a WebTask (no es peticion web)
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
                auto ext = geode::utils::string::pathToString(entry.path().extension());
                // saco el id desde el nombre del archivo
                int id = 0;
                bool isGif = (ext == ".gif");
                // compatibilidad: archivos legacy con _anim en el nombre
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
    std::lock_guard<std::mutex> lock(m_queueMutex);
    return m_textureCache.find(key) != m_textureCache.end();
}

bool ThumbnailLoader::isPending(int levelID, bool isGif) const {
    int key = isGif ? -levelID : levelID;
    std::lock_guard<std::mutex> lock(m_queueMutex);
    return m_tasks.find(key) != m_tasks.end();
}

bool ThumbnailLoader::isFailed(int levelID, bool isGif) const {
    int key = isGif ? -levelID : levelID;
    std::lock_guard<std::mutex> lock(m_queueMutex);
    auto it = m_failedCache.find(key);
    if (it == m_failedCache.end()) return false;
    // expirado = no fallido
    return (std::chrono::steady_clock::now() - it->second) < FAILED_CACHE_TTL;
}

bool ThumbnailLoader::hasGIFData(int levelID) const {
    std::lock_guard<std::mutex> lock(m_queueMutex);
    return m_gifLevels.find(levelID) != m_gifLevels.end();
}

std::filesystem::path ThumbnailLoader::getCachePath(int levelID, bool isGif) {
    if (isGif) {
        return Mod::get()->getSaveDir() / "cache" / fmt::format("{}.gif", levelID);
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
        // actualizo la LRU en O(1)
        auto lruIt = m_lruMap.find(key);
        if (lruIt != m_lruMap.end()) {
            m_lruOrder.erase(lruIt->second);
        }
        m_lruOrder.push_back(key);
        m_lruMap[key] = std::prev(m_lruOrder.end());
        
        // fuerzo callback asincrono para no trabar la UI
        auto tex = it->second;
        Loader::get()->queueInMainThread([callback, tex]() {
            if (callback) callback(tex, true);
        });
        return;
    }

    // 2. miro el cache de fallos (con TTL)
    auto failIt = m_failedCache.find(key);
    if (failIt != m_failedCache.end()) {
        auto now = std::chrono::steady_clock::now();
        if (now - failIt->second < FAILED_CACHE_TTL) {
            Loader::get()->queueInMainThread([callback]() {
                if (callback) callback(nullptr, false);
            });
            return;
        }
        // TTL expirado, permito reintento
        m_failedCache.erase(failIt);
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
    m_priorityQueue.emplace(priority, key);
    
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

    // busco el mejor candidato para correr ahora (multimap ya esta ordenado desc)
    while (m_activeTaskCount < m_maxConcurrentTasks && !m_priorityQueue.empty()) {
        auto bestIt = m_priorityQueue.end();

        for (auto it = m_priorityQueue.begin(); it != m_priorityQueue.end(); ++it) {
            int levelID = it->second;
            auto taskIt = m_tasks.find(levelID);
            if (taskIt == m_tasks.end() || !taskIt->second) {
                continue; // tarea huerfana, se limpiara
            }
            if (taskIt->second->cancelled) continue;

            bestIt = it;
            break; // el primero valido es el de mayor prioridad
        }

        if (bestIt == m_priorityQueue.end()) {
            // todas canceladas u huerfanas, limpio
            m_priorityQueue.clear();
            for (auto it = m_tasks.begin(); it != m_tasks.end();) {
                if (it->second->cancelled && !it->second->running) {
                    it = m_tasks.erase(it);
                } else {
                    ++it;
                }
            }
            break;
        }

        int levelID = bestIt->second;
        m_priorityQueue.erase(bestIt);
        
        auto task = m_tasks[levelID];
        if (task->cancelled) continue;

        startTask(task);
    }
}

void ThumbnailLoader::startTask(std::shared_ptr<Task> task) {
    task->running = true;
    m_activeTaskCount.fetch_add(1, std::memory_order_relaxed);

    // siempre tiro de disco primero para evitar carreras con initDiskCache
    // workerLoadFromDisk mira el FS y si no encuentra descarga
    // hilo de I/O de disco + decodificacion CPU — no migrable a WebTask
    std::thread([this, task]() {
        if (m_shuttingDown.load(std::memory_order_relaxed)) {
            Loader::get()->queueInMainThread([this, task]() {
                finishTask(task, nullptr, false);
            });
            return;
        }
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

    // consulto el indice de disco antes de ir al filesystem
    bool inDiskIndex = false;
    bool gifInDiskIndex = false;
    {
        std::lock_guard<std::mutex> lock(m_diskMutex);
        inDiskIndex = m_diskCache.count(isGif ? -realID : realID) > 0;
        if (!isGif) gifInDiskIndex = m_diskCache.count(-realID) > 0;
    }

    auto path = getCachePath(realID, isGif);

    // fallback: si el request estatico no esta en el indice, probar con GIF
    if (!inDiskIndex && !isGif && gifInDiskIndex) {
        path = getCachePath(realID, true);
        inDiskIndex = true;
    }
    // si el indice no lo tiene pero el FS si (race con initDiskCache), verificar FS
    if (!inDiskIndex) {
        std::error_code ec;
        if (std::filesystem::exists(path, ec)) {
            inDiskIndex = true;
        } else if (!isGif) {
            auto gifPath = getCachePath(realID, true);
            if (std::filesystem::exists(gifPath, ec)) {
                path = gifPath;
                inDiskIndex = true;
            }
        }
    }

    std::vector<uint8_t> data;
    bool success = false;

    if (inDiskIndex) {
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

    // fallback: si no hay archivo en cache, buscar en LocalThumbs (.rgb local)
    if (!success || data.empty()) {
        if (!isGif) {
            auto localPath = LocalThumbs::get().findAnyThumbnail(realID);
            if (localPath.has_value()) {
                std::string localPathStr = localPath.value();
                PaimonDebug::log("[ThumbnailLoader] fallback LocalThumbs pal nivel {}: {}", realID, localPathStr);
                
                // detectar si es .rgb (formato custom con header)
                std::filesystem::path fsPath(localPathStr);
                bool isRgbFormat = (fsPath.extension() == ".rgb");
                
                if (isRgbFormat) {
                    // leer archivo .rgb: header (8 bytes: width + height) + datos RGB24
                    std::ifstream rgbFile(fsPath, std::ios::binary);
                    if (rgbFile) {
                        uint32_t rgbW = 0, rgbH = 0;
                        rgbFile.read(reinterpret_cast<char*>(&rgbW), sizeof(rgbW));
                        rgbFile.read(reinterpret_cast<char*>(&rgbH), sizeof(rgbH));
                        if (rgbFile && rgbW > 0 && rgbH > 0) {
                            size_t rgbSize = static_cast<size_t>(rgbW) * rgbH * 3;
                            std::vector<uint8_t> rgbBuf(rgbSize);
                            rgbFile.read(reinterpret_cast<char*>(rgbBuf.data()), rgbSize);
                            if (rgbFile) {
                                // convertir RGB24 -> RGBA32
                                size_t pixelCount = static_cast<size_t>(rgbW) * rgbH;
                                std::vector<uint8_t> rgbaData(pixelCount * 4);
                                for (size_t i = 0; i < pixelCount; ++i) {
                                    rgbaData[i * 4 + 0] = rgbBuf[i * 3 + 0];
                                    rgbaData[i * 4 + 1] = rgbBuf[i * 3 + 1];
                                    rgbaData[i * 4 + 2] = rgbBuf[i * 3 + 2];
                                    rgbaData[i * 4 + 3] = 255;
                                }
                                
                                if (!LevelColors::get().getPair(realID)) {
                                    LevelColors::get().extractFromRawData(realID, rgbaData.data(), rgbW, rgbH, true);
                                }
                                
                                int finalW = static_cast<int>(rgbW);
                                int finalH = static_cast<int>(rgbH);
                                Loader::get()->queueInMainThread([this, task, rgbaData, finalW, finalH, realID]() {
                                    if (task->cancelled) { finishTask(task, nullptr, false); return; }
                                    auto tex = new CCTexture2D();
                                    if (tex->initWithData(rgbaData.data(), kCCTexture2DPixelFormat_RGBA8888, finalW, finalH, CCSize((float)finalW, (float)finalH))) {
                                        tex->autorelease();
                                        PaimonDebug::log("[ThumbnailLoader] textura cargada desde LocalThumbs .rgb pal nivel {}", realID);
                                        finishTask(task, tex, true);
                                    } else {
                                        tex->release();
                                        workerDownload(task);
                                    }
                                });
                                return;
                            }
                        }
                    }
                } else {
                    // formato estandar (png/jpg/webp): leer y decodificar con stb
                    std::ifstream imgFile(fsPath, std::ios::binary | std::ios::ate);
                    if (imgFile.is_open()) {
                        size_t fileSize = imgFile.tellg();
                        imgFile.seekg(0, std::ios::beg);
                        data.resize(fileSize);
                        imgFile.read(reinterpret_cast<char*>(data.data()), fileSize);
                        success = static_cast<bool>(imgFile);
                    }
                }
            }
        }
    }

    if (success && !data.empty()) {
        // optimizado: decodifico y proceso en un thread aparte
        bool isNativeGif = GIFDecoder::isGIF(data.data(), data.size());
        
        if (isNativeGif) {
            // procesamiento del GIF en main thread (por los cocos/sprites)
            Loader::get()->queueInMainThread([this, task, data, realID]() {
                if (task->cancelled) { finishTask(task, nullptr, false); return; }
                
                { std::lock_guard<std::mutex> lock(m_queueMutex); m_gifLevels.insert(realID); }
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
                    // hilo de I/O disco + decodificacion stb_image — no migrable a WebTask
                    std::thread([this, task, data, isGif, realID]() {
                        geode::utils::thread::setName("ThumbnailLoader Download Worker");
                        // 1. guardo en disco con nombre segun formato real
                        // asi no se duplica si el request estatico baja un GIF
                        {
                            bool dataIsGif = GIFDecoder::isGIF(data.data(), data.size());
                            auto path = getCachePath(realID, dataIsGif);
                            std::ofstream file(path, std::ios::binary);
                            if (file) {
                                file.write(reinterpret_cast<char const*>(data.data()), data.size());
                                file.close();

                                std::lock_guard<std::mutex> lock(m_diskMutex);
                                m_diskCache.insert(dataIsGif ? -realID : realID);
                            } else {
                                log::error("[ThumbnailLoader] no se pudo abrir archivo para guardar en disco");
                            }
                        }
                        
                        // 2. decodifico y extraco colores en background
                        if (GIFDecoder::isGIF(data.data(), data.size())) {
                            // logica gif (mando a main thread)
                            Loader::get()->queueInMainThread([this, task, data, realID]() {
                                { std::lock_guard<std::mutex> lock(m_queueMutex); m_gifLevels.insert(realID); }
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
    std::vector<LoadCallback> callbacks;
    bool shouldNotify = !task->cancelled;

    // main thread: lock protege m_textureCache, m_failedCache, m_tasks
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);

        if (success && texture) {
            addToCache(task->levelID, texture);
        } else if (!task->cancelled) {
            m_failedCache[task->levelID] = std::chrono::steady_clock::now();
        }

        if (shouldNotify) {
            callbacks = task->callbacks;
        }

        // limpieza
        m_tasks.erase(task->levelID);
        m_activeTaskCount.fetch_sub(1, std::memory_order_relaxed);

        // proceso el siguiente
        processQueue();
    }

    // callbacks fuera del lock para evitar deadlocks/re-entradas
    if (shouldNotify) {
        for (auto& cb : callbacks) {
            if (cb) cb(texture, success);
        }
    }
}

void ThumbnailLoader::addToCache(int levelID, cocos2d::CCTexture2D* texture) {
    if (!texture) return;
    
    m_textureCache[levelID] = texture;
    
    // LRU O(1): quito el viejo iterador si existe, agrego al final
    auto lruIt = m_lruMap.find(levelID);
    if (lruIt != m_lruMap.end()) {
        m_lruOrder.erase(lruIt->second);
    }
    m_lruOrder.push_back(levelID);
    m_lruMap[levelID] = std::prev(m_lruOrder.end());
    
    // recorto cache si pasa del maximo
    while (m_lruOrder.size() > MAX_CACHE_SIZE) {
        int removeID = m_lruOrder.front();
        m_lruOrder.pop_front();
        m_lruMap.erase(removeID);
        m_textureCache.erase(removeID);
    }
}

void ThumbnailLoader::clearCache() {
    std::lock_guard<std::mutex> lock(m_queueMutex);
    m_textureCache.clear();
    m_lruOrder.clear();
    m_lruMap.clear();
    m_failedCache.clear();
    m_gifLevels.clear();
}

void ThumbnailLoader::invalidateLevel(int levelID, bool isGif) {
    int key = isGif ? -levelID : levelID;

    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        // incremento la version de invalidacion para que los consumidores sepan que hay cambio
        m_invalidationVersions[levelID]++;

        // quito la entrada de la RAM
        auto it = m_textureCache.find(key);
        if (it != m_textureCache.end()) {
            m_textureCache.erase(it);
            auto lruIt = m_lruMap.find(key);
            if (lruIt != m_lruMap.end()) {
                m_lruOrder.erase(lruIt->second);
                m_lruMap.erase(lruIt);
            }
        }

        // quito del cache de fallos y gif
        m_failedCache.erase(key);
        m_gifLevels.erase(levelID);
    }

    // borro ambos formatos en disco para no dejar huerfanos
    // hilo de I/O de disco - no migrable a WebTask
    std::thread([this, levelID]() {
        geode::utils::thread::setName("ThumbnailLoader Invalidator");
        std::error_code ec;
        auto pngPath = getCachePath(levelID, false);
        auto gifPath = getCachePath(levelID, true);
        if (std::filesystem::exists(pngPath, ec)) {
            std::filesystem::remove(pngPath, ec);
        }
        if (std::filesystem::exists(gifPath, ec)) {
            std::filesystem::remove(gifPath, ec);
        }
        std::lock_guard<std::mutex> lock(m_diskMutex);
        m_diskCache.erase(levelID);
        m_diskCache.erase(-levelID);
    }).detach();
}

int ThumbnailLoader::getInvalidationVersion(int levelID) const {
    std::lock_guard<std::mutex> lock(m_queueMutex);
    auto it = m_invalidationVersions.find(levelID);
    return it != m_invalidationVersions.end() ? it->second : 0;
}

void ThumbnailLoader::clearDiskCache() {
    // hilo de I/O de disco — no migrable a WebTask
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
    std::lock_guard<std::mutex> lock(m_queueMutex);
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

    auto sz = tex->getContentSize();
    if (std::isnan(sz.width) || std::isnan(sz.height)) return false;
    return sz.width > 0.f && sz.height > 0.f;
}

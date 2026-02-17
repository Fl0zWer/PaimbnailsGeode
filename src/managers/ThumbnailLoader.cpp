#include "ThumbnailLoader.hpp"
#include "LocalThumbs.hpp"
#include "LevelColors.hpp"
#include "../utils/Constants.hpp"
#include "../utils/HttpClient.hpp"
#include "../utils/DominantColors.hpp"
#include "../utils/GIFDecoder.hpp"
#include "../utils/Debug.hpp"
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
    m_maxConcurrentTasks = 5; // por defecto en 5 porque así se pidió
    initDiskCache();
}

ThumbnailLoader::~ThumbnailLoader() {
    clearCache();
}

void ThumbnailLoader::initDiskCache() {
    std::thread([this]() {
        geode::utils::thread::setName("ThumbnailLoader Cache Init");
        std::lock_guard<std::mutex> lock(m_diskMutex);
        auto path = Mod::get()->getSaveDir() / "cache";
        PaimonDebug::log("[ThumbnailLoader] iniciando cache de disco en: {}", geode::utils::string::pathToString(path));
        
        if (!std::filesystem::exists(path)) {
            std::filesystem::create_directories(path);
            PaimonDebug::log("[ThumbnailLoader] carpeta de cache creada");
            return;
        }
        
        // limpieza al inicio
        // niveles principales (1–22) nunca expiran (los guardo pa siempre)
        // el resto se borra para no dejar la carpeta gigante

        int deletedCount = 0;
        int keptCount = 0;
        
        auto now = std::filesystem::file_time_type::clock::now();
        auto defaultRetention = std::chrono::hours(24 * 15); // 15 días, por si algún día lo uso

        try {
            for (const auto& entry : std::filesystem::directory_iterator(path)) {
                if (entry.is_regular_file()) {
                    try {
                        auto stem = geode::utils::string::pathToString(entry.path().stem());
                        // saco la id desde el nombre del archivo
                        int id = 0;
                        bool isGif = false;
                        if (stem.find("_anim") != std::string::npos) {
                            std::string idStr = stem.substr(0, stem.find("_anim"));
                            id = geode::utils::numFromString<int>(idStr).unwrapOr(0);
                            isGif = true;
                        } else {
                            id = geode::utils::numFromString<int>(stem).unwrapOr(0);
                        }
                        
                        // miro si es un main level
                        int realID = std::abs(id);
                        bool isMain = (realID >= 1 && realID <= 22);

                        // si es main level no lo borro ni por tiempo ni por sesión
                        if (!isMain) {
                            // a petición del usuario: borro cache de no-mains al iniciar
                            // así solo se quedan cacheados mientras estás jugando
                            std::filesystem::remove(entry.path());
                            deletedCount++;
                            continue;
                        }
                        
                        m_diskCache.insert(isGif ? -id : id);
                        keptCount++;
                    } catch(...) {
                        // si el nombre del archivo es raro o no es id, lo vuelo sin pensarlo
                        try {
                            std::filesystem::remove(entry.path());
                        } catch(...) {}
                    }
                }
            }
        } catch(const std::exception& e) {
            log::error("[ThumbnailLoader] error limpiando carpeta de cache: {}", e.what());
        }
        
        PaimonDebug::log("[ThumbnailLoader] cache de disco lista. borrados: {}, guardados: {}", deletedCount, keptCount);
    }).detach();
}

void ThumbnailLoader::setMaxConcurrentTasks(int max) {
    // dejo hasta 100 descargas simultáneas por si te quieres pasar
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
    
    // 1. reviso cache en RAM
    auto it = m_textureCache.find(key);
    if (it != m_textureCache.end()) {
        // actualizo la LRU
        m_lruOrder.remove(key);
        m_lruOrder.push_back(key);
        
        // fuerzo callback asíncrono pa no trabar la UI
        auto tex = it->second;
        tex->retain();
        Loader::get()->queueInMainThread([callback, tex]() {
            if (callback) callback(tex, true);
            tex->release();
        });
        return;
    }

    // 2. miro el cache de fallos
    if (m_failedCache.count(key)) {
        if (callback) callback(nullptr, false);
        return;
    }

    // 3. reviso si ya hay una tarea en cola
    std::lock_guard<std::mutex> lock(m_queueMutex);
    auto taskIt = m_tasks.find(key);
    if (taskIt != m_tasks.end()) {
        // solo agrego el callback a la tarea existente
        if (callback) taskIt->second->callbacks.push_back(callback);
        // si viene con más prioridad, se la subo
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
        // si ya va corriendo no lo paro, solo ignoro el resultado
        // si está en cola, con marcar cancelado me sobra
    }
}

void ThumbnailLoader::processQueue() {
    // debe llamarse con m_queueMutex pillado o encargarse dentro
    // processQueue llama startTask pero ahí no soltamos nada raro
    
    // modo batch: si está activo, no arranco nada nuevo si ya hay algo corriendo
    // así termino el batch actual antes de empezar el siguiente
    if (m_batchMode && m_activeTaskCount > 0) {
        return;
    }

    // busco el mejor candidato para correr ahora
    while (m_activeTaskCount < m_maxConcurrentTasks && !m_queueOrder.empty()) {
        // encuentro la tarea con mayor prioridad
        auto bestIt = m_queueOrder.end();
        int maxPrio = -9999;

        for (auto it = m_queueOrder.begin(); it != m_queueOrder.end(); ++it) {
            // por si acaso: aseguro que la tarea todavía existe
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
            // si llegamos aquí es que todas las que quedan están canceladas
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

    // siempre tiro de disco primero pa evitar carreras con initDiskCache
    // workerLoadFromDisk mira el FS y si falta, descarga
    std::thread([this, task]() {
        geode::utils::thread::setName("ThumbnailLoader Disk Worker");
        try {
            workerLoadFromDisk(task);
        } catch(...) {
            log::error("[ThumbnailLoader] error desconocido en thread de carga de disco");
        }
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

    try {
        if (std::filesystem::exists(path)) {
            std::ifstream file(path, std::ios::binary | std::ios::ate);
            if (file.is_open()) {
                size_t size = file.tellg();
                file.seekg(0, std::ios::beg);
                data.resize(size);
                file.read(reinterpret_cast<char*>(data.data()), size);
                success = true;
                PaimonDebug::log("[ThumbnailLoader] cargados {} bytes del disco pal nivel {}{}", size, realID, isGif ? " (gif)" : "");
            } else {
                PaimonDebug::warn("[ThumbnailLoader] no se pudo abrir archivo pal nivel {}: {}", realID, path.generic_string());
            }
        } else {
            // si no existe es normal en la primera carga, no hace falta spamear logs
            // log::warn("[ThumbnailLoader] archivo no encontrado pal nivel {}: {}", task->levelID, path.generic_string());
        }
    } catch(const std::exception& e) {
        log::error("[ThumbnailLoader] excepcion cargando del disco pal nivel {}: {}", realID, e.what());
    } catch(...) {
        log::error("[ThumbnailLoader] excepcion desconocida cargando del disco pal nivel {}", realID);
    }

    if (success && !data.empty()) {
        // optimizado: decodifico y proceso en un thread aparte
        bool isNativeGif = GIFDecoder::isGIF(data.data(), data.size());
        
        if (isNativeGif) {
            // procesamiento de GIF en main thread (por los cocos/sprites)
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
            // imagen estática (png/jpg/webp)
            // uso stb_image pa decodificar fuera del main thread
            int w = 0, h = 0, ch = 0;
            unsigned char* pixels = stbi_load_from_memory(data.data(), (int)data.size(), &w, &h, &ch, 4); // Force RGBA
            
            if (pixels) {
                // saco los colores en este mismo thread
                if (!LevelColors::get().getPair(realID)) {
                    LevelColors::get().extractFromRawData(realID, pixels, w, h, true);
                }
                
                // copio los píxeles a un vector para pasarlos al main sin sustos
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
                    // si falla crear textura normalmente es cosa del formato, así que reintento por red
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
        // fallo lectura, intentar descargar
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
            [this, task, isGif, realID](bool success, const std::vector<uint8_t>& data, int w, int h) {
                if (task->cancelled) {
                    finishTask(task, nullptr, false);
                    return;
                }

                if (success && !data.empty()) {
                    // empezar procesamiento en thread background
                    std::thread([this, task, data, isGif, realID]() {
                        geode::utils::thread::setName("ThumbnailLoader Download Worker");
                        try {
                        // 1. guardar en disco
                        try {
                            auto path = getCachePath(realID, isGif);
                            std::ofstream file(path, std::ios::binary);
                            file.write(reinterpret_cast<const char*>(data.data()), data.size());
                            file.close();
                            
                            std::lock_guard<std::mutex> lock(m_diskMutex);
                            m_diskCache.insert(task->levelID);
                        } catch(const std::exception& e) {
                            log::error("[ThumbnailLoader] error guardando en disco: {}", e.what());
                        } catch(...) {
                            log::error("[ThumbnailLoader] error desconocido guardando en disco");
                        }
                        
                        // 2. decodificar y extraer colores (background)
                        if (GIFDecoder::isGIF(data.data(), data.size())) {
                            // logica gif (cola a main)
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
                                // si ni stb_image quiere, marco fallo y ya
                                Loader::get()->queueInMainThread([this, task]() {
                                     finishTask(task, nullptr, false);
                                });
                            }
                        }
                        } catch (...) {
                            log::error("[ThumbnailLoader] error desconocido en worker de descarga");
                            geode::Loader::get()->queueInMainThread([this, task]() {
                                finishTask(task, nullptr, false);
                            });
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
    // main thread
    
    if (success && texture) {
        addToCache(task->levelID, texture);
    } else {
        if (!task->cancelled) {
            m_failedCache.insert(task->levelID);
        }
    }

    if (!task->cancelled) {
        for (auto& cb : task->callbacks) {
            if (cb) cb(texture, success);
        }
    }

    // limpieza
    std::lock_guard<std::mutex> lock(m_queueMutex);
    m_tasks.erase(task->levelID);
    m_activeTaskCount--;
    
    // procesar siguiente
    processQueue();
}

void ThumbnailLoader::addToCache(int levelID, cocos2d::CCTexture2D* texture) {
    if (!texture) return;
    
    if (m_textureCache.count(levelID)) {
        m_textureCache[levelID]->release();
    }
    
    texture->retain();
    m_textureCache[levelID] = texture;
    
    m_lruOrder.remove(levelID);
    m_lruOrder.push_back(levelID);
    
    // recortar cache
    while (m_lruOrder.size() > MAX_CACHE_SIZE) {
        int removeID = m_lruOrder.front();
        m_lruOrder.pop_front();
        
        auto it = m_textureCache.find(removeID);
        if (it != m_textureCache.end()) {
            it->second->release();
            m_textureCache.erase(it);
        }
    }
}

void ThumbnailLoader::clearCache() {
    for (auto& [id, tex] : m_textureCache) {
        tex->release();
    }
    m_textureCache.clear();
    m_lruOrder.clear();
    m_failedCache.clear();
}

void ThumbnailLoader::invalidateLevel(int levelID, bool isGif) {
    int key = isGif ? -levelID : levelID;
    // quito entrada de la RAM
    auto it = m_textureCache.find(key);
    if (it != m_textureCache.end()) {
        it->second->release();
        m_textureCache.erase(it);
        m_lruOrder.remove(key);
    }
    
    // quito del cache de fallos
    m_failedCache.erase(key);
    
    // borro también el archivo en disco
    std::thread([this, levelID, isGif]() {
        geode::utils::thread::setName("ThumbnailLoader Invalidator");
        try {
            auto path = getCachePath(levelID, isGif);
            if(std::filesystem::exists(path)) {
                std::filesystem::remove(path);
            }
            std::lock_guard<std::mutex> lock(m_diskMutex);
            m_diskCache.erase(isGif ? -levelID : levelID);
        } catch(const std::exception& e) {
            log::error("[ThumbnailLoader] error borrando cache: {}", e.what());
        } catch(...) {}
    }).detach();
}

void ThumbnailLoader::clearDiskCache() {
    std::thread([this]() {
        geode::utils::thread::setName("ThumbnailLoader Disk Clear");
        try {
            std::filesystem::remove_all(Mod::get()->getSaveDir() / "cache");
            std::lock_guard<std::mutex> lock(m_diskMutex);
            m_diskCache.clear();
            initDiskCache(); // re-crear carpeta
        } catch(const std::exception& e) {
            log::error("[ThumbnailLoader] error limpiando cache de disco: {}", e.what());
        } catch(...) {
            log::error("[ThumbnailLoader] error desconocido limpiando cache de disco");
        }
    }).detach();
}

void ThumbnailLoader::clearPendingQueue() {
    std::lock_guard<std::mutex> lock(m_queueMutex);
    for (auto& [id, task] : m_tasks) {
        task->cancelled = true;
    }
    // no limpio el mapa aquí porque algunas siguen corriendo
    // con marcarlas como canceladas ya vale
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
    if (addr < 0x10000) return false; // Null or low pointer
    
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



#include "GifManager.hpp"
#include <thread>

#define STB_IMAGE_IMPLEMENTATION
#include "../utils/stb_image.h"

using namespace geode::prelude;

GifData::~GifData() {
    if (rawData) {
        stbi_image_free(rawData);
        rawData = nullptr;
    }
}

GifManager* GifManager::get() {
    static GifManager instance;
    return &instance;
}

GifManager::GifManager() : m_running(true) {
    // determino cuento optimo de hilos
    unsigned int threads = std::thread::hardware_concurrency();
    // restrinjo entre 2 y 8 por seguridad (dejo 1 pal main thread)
    // si hardware_concurrency da 0 (error), default en 2.
    if (threads == 0) threads = 2;
    else if (threads > 8) threads = 8;
    
    // me aseguro de no usar todos los hilos si la cpu tiene pocos nucleos
    if (threads > 2) threads -= 1;

    log::info("gifmanager: iniciando thread pool con {} workers.", threads);

    for (unsigned int i = 0; i < threads; ++i) {
        m_workers.emplace_back(&GifManager::workerLoop, this);
    }
}

GifManager::~GifManager() {
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_running = false;
    }
    m_condition.notify_all(); // despertar todos workers
    
    for (auto& worker : m_workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

void GifManager::loadGif(const std::string& path, std::function<void(std::shared_ptr<GifData>)> callback) {
    // chequear cache primero
    {
        std::lock_guard<std::mutex> lock(m_cacheMutex);
        if (m_cache.find(path) != m_cache.end()) {
            auto data = m_cache[path];
            Loader::get()->queueInMainThread([callback, data]() {
                callback(data);
            });
            return;
        }
    }

    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_taskQueue.push({path, path, {}, callback});
    }
    m_condition.notify_one();
}

void GifManager::loadGifFromUrl(const std::string& url, std::function<void(std::shared_ptr<GifData>)> callback) {
    // chequear cache primero
    {
        std::lock_guard<std::mutex> lock(m_cacheMutex);
        if (m_cache.find(url) != m_cache.end()) {
            auto data = m_cache[url];
            Loader::get()->queueInMainThread([callback, data]() {
                callback(data);
            });
            return;
        }
    }

    // descargar primero usando thread
    std::thread([this, url, callback]() {
        auto req = web::WebRequest();
        auto res = req.getSync(url);

        queueInMainThread([this, url, callback, res = std::move(res)]() {
            if (res.ok()) {
                auto data = res.data();
                // empujar al worker thread para decodificar
                {
                    std::lock_guard<std::mutex> lock(m_queueMutex);
                    m_taskQueue.push({url, "", std::move(data), callback});
                }
                m_condition.notify_one();
            } else {
                log::error("fallo al descargar gif: {}", res.code());
                callback(nullptr);
            }
        });
    }).detach();
}

void GifManager::workerLoop() {
    while (true) {
        GifTask task;
        {
            std::unique_lock<std::mutex> lock(m_queueMutex);
            m_condition.wait(lock, [this] { return !m_taskQueue.empty() || !m_running; });

            if (!m_running && m_taskQueue.empty()) {
                return;
            }

            task = m_taskQueue.front();
            m_taskQueue.pop();
        }

        // chequear cache de nuevo por si se cargo mientras esperaba
        std::shared_ptr<GifData> data;
        {
            std::lock_guard<std::mutex> lock(m_cacheMutex);
            if (m_cache.find(task.key) != m_cache.end()) {
                data = m_cache[task.key];
            }
        }

        if (!data) {
            if (!task.path.empty()) {
                data = processGif(task.path);
            } else if (!task.data.empty()) {
                data = processGifData(task.data.data(), task.data.size(), task.key);
            }
            
            if (data) {
                std::lock_guard<std::mutex> lock(m_cacheMutex);
                m_cache[task.key] = data;
            }
        }

        Loader::get()->queueInMainThread([callback = task.callback, data]() {
            callback(data);
        });
    }
}

std::shared_ptr<GifData> GifManager::processGif(const std::string& path) {
    // leer contenido archivo
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        log::error("fallo al abrir archivo gif: {}", path);
        return nullptr;
    }

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<unsigned char> buffer(size);
    if (!file.read((char*)buffer.data(), size)) {
        log::error("fallo al leer archivo gif: {}", path);
        return nullptr;
    }

    return processGifData(buffer.data(), (size_t)size, path);
}

std::shared_ptr<GifData> GifManager::processGifData(const unsigned char* bufferData, size_t size, const std::string& key) {
    int width, height, frames, channels;
    int* delays = nullptr;

    // forzar 4 canales (rgba)
    stbi_uc* data = stbi_load_gif_from_memory(
        bufferData, 
        (int)size, 
        &delays, 
        &width, 
        &height, 
        &frames, 
        &channels, 
        4
    );

    if (!data) {
        log::error("fallo al decodificar gif: {}", key);
        return nullptr;
    }

    auto result = std::make_shared<GifData>();
    result->width = width;
    result->height = height;
    result->totalFrames = frames;
    result->rawData = data;

    int frameSize = width * height * 4;

    for (int i = 0; i < frames; i++) {
        GifFrame frame;
        frame.pixels = data + (i * frameSize);
        frame.delay = delays[i];
        result->frames.push_back(frame);
    }

    STBI_FREE(delays);

    return result;
}

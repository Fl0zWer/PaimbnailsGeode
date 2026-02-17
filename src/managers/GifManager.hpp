#pragma once

#include <Geode/Geode.hpp>
#include <Geode/utils/web.hpp>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <vector>
#include <string>
#include <map>
#include <memory>

struct GifFrame {
    unsigned char* pixels;
    int delay; // en milisegundos
};

struct GifData {
    std::vector<GifFrame> frames;
    int width;
    int height;
    int totalFrames;
    unsigned char* rawData; // puntero a datos crudos alojados por stbi

    ~GifData();
};

class GifManager {
public:
    static GifManager* get();

    // callback recibe puntero gifdata (o nullptr si falla).
    // callback se ejecuta en main thread.
    void loadGif(const std::string& path, std::function<void(std::shared_ptr<GifData>)> callback);

    // descarga gif desde url, decodifica en background, y retorna datos.
    void loadGifFromUrl(const std::string& url, std::function<void(std::shared_ptr<GifData>)> callback);

private:
    GifManager();
    ~GifManager();

    void workerLoop();
    std::shared_ptr<GifData> processGif(const std::string& path);
    std::shared_ptr<GifData> processGifData(const unsigned char* data, size_t size, const std::string& key);

    std::vector<std::thread> m_workers;
    // tarea puede ser ruta archivo o datos crudos.
    // si string no esta vacio, es ruta. si vector no esta vacio, son datos crudos.
    struct GifTask {
        std::string key; // ruta archivo o url (para clave cache)
        std::string path; // ruta archivo (opcional)
        std::vector<unsigned char> data; // datos crudos (opcional)
        std::function<void(std::shared_ptr<GifData>)> callback;
    };

    std::queue<GifTask> m_taskQueue;
    std::mutex m_queueMutex;
    std::condition_variable m_condition;
    bool m_running;

    std::map<std::string, std::shared_ptr<GifData>> m_cache;
    std::mutex m_cacheMutex;
};

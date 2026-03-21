#pragma once

#include <Geode/DefaultInclude.hpp>
#include <Geode/utils/cocos.hpp>
#include <Geode/utils/function.hpp>
#include <cocos2d.h>
#include <string>
#include <deque>
#include <map>
#include <unordered_set>
#include <unordered_map>
#include <vector>
#include <memory>
#include <mutex>
#include <atomic>
#include <chrono>
#include <future>
#include <functional>
#include "../../../utils/GIFDecoder.hpp"

/**
 * cargador de thumbnails optimizado:
 * - limite de concurrencia
 * - cola por prioridad
 * - cache automatico
 * - evita lag
 */
class ThumbnailLoader {
public:
    using LoadCallback = geode::CopyableFunction<void(cocos2d::CCTexture2D* texture, bool success)>;
    using InvalidationCallback = geode::CopyableFunction<void(int levelID)>;

    static ThumbnailLoader& get();

    // pedir carga de thumbnail. mayor valor = mas prioridad
    void requestLoad(int levelID, std::string fileName, LoadCallback callback, int priority = 0, bool isGif = false);
    
    // cancelar carga pendiente
    void cancelLoad(int levelID, bool isGif = false);
    
    // cache
    bool isLoaded(int levelID, bool isGif = false) const;
    bool isPending(int levelID, bool isGif = false) const;
    bool isFailed(int levelID, bool isGif = false) const;
    void clearCache();
    void invalidateLevel(int levelID, bool isGif = false);

    // version de invalidacion: se incrementa cada vez que se invalida un level
    // los consumidores (LevelCell, etc) guardan la version cuando cargan
    // y la comparan pa saber si deben recargar
    int getInvalidationVersion(int levelID) const;
    int addInvalidationListener(InvalidationCallback callback);
    void removeInvalidationListener(int listenerId);

    // config
    void setMaxConcurrentTasks(int max);
    void setBatchMode(bool enabled) { m_batchMode = enabled; }

    int getActiveTaskCount() const { return m_activeTaskCount; }

    // helpers
    static bool isTextureSane(cocos2d::CCTexture2D* tex);
    std::filesystem::path getCachePath(int levelID, bool isGif = false);
    
    // compatibilidad
    void updateSessionCache(int levelID, cocos2d::CCTexture2D* texture);
    bool hasGIFData(int levelID) const;
    void cleanup();
    void clearDiskCache();
    void clearPendingQueue();

private:
    ThumbnailLoader();
    ~ThumbnailLoader();

    struct Task {
        int levelID;
        std::string fileName;
        int priority;
        std::vector<LoadCallback> callbacks;
        bool running = false;
        bool cancelled = false;
    };

    // manejo de cola
    std::unordered_map<int, std::shared_ptr<Task>> m_tasks; // id -> tarea (pendiente y corriendo)
    std::multimap<int, int, std::greater<int>> m_priorityQueue; // prioridad (desc) -> levelID
    std::atomic<int> m_activeTaskCount{0};
    int m_maxConcurrentTasks = 20;
    mutable std::mutex m_queueMutex;

    // cache ram (sesion) — LRU O(1) con list + iterador en map
    std::unordered_map<int, geode::Ref<cocos2d::CCTexture2D>> m_textureCache;
    std::list<int> m_lruOrder;
    std::unordered_map<int, std::list<int>::iterator> m_lruMap; // key -> iterador en m_lruOrder
    size_t const MAX_CACHE_SIZE = 60;
    size_t m_textureCacheBytes = 0;
    size_t const MAX_CACHE_BYTES = 160 * 1024 * 1024;

    // indice cache disco
    std::unordered_set<int> m_diskCache;
    std::mutex m_diskMutex;
    
    // cache fallidos con TTL (5 minutos)
    std::unordered_map<int, std::chrono::steady_clock::time_point> m_failedCache;
    static constexpr auto FAILED_CACHE_TTL = std::chrono::minutes(5);
    static constexpr auto FAILED_CACHE_PRUNE_INTERVAL = std::chrono::minutes(1);
    std::chrono::steady_clock::time_point m_lastFailedCachePrune = std::chrono::steady_clock::time_point::min();
    
    // cache gifs
    std::unordered_set<int> m_gifLevels;

    // version de invalidacion por level (se incrementa al invalidar)
    std::unordered_map<int, int> m_invalidationVersions;
    std::unordered_map<int, InvalidationCallback> m_invalidationListeners;
    int m_nextInvalidationListenerId = 1;

    bool m_batchMode = false; // por defecto "smart" (desactivado por velocidad)

    // flag de shutdown: cuando es true, los threads de background deben parar lo antes posible
    std::atomic<bool> m_shuttingDown{false};

    // metodos
    void processQueue();
    void startTask(std::shared_ptr<Task> task);
    void finishTask(std::shared_ptr<Task> task, cocos2d::CCTexture2D* texture, bool success);
    
    void addToCache(int levelID, cocos2d::CCTexture2D* texture);
    void initDiskCache();
    void pruneFailedCacheLocked(std::chrono::steady_clock::time_point now);
    void pruneDiskCache();
    
    // Worker methods
    void workerLoadFromDisk(std::shared_ptr<Task> task);
    void workerDownload(std::shared_ptr<Task> task);
    void spawnBackground(std::function<void()> job);
    void waitBackgroundWorkers();
    void pruneFinishedWorkers();

    std::vector<std::future<void>> m_backgroundWorkers;
    mutable std::mutex m_workerMutex;

    // quota de cache de disco (thumbnails png/gif)
    static constexpr size_t MAX_DISK_CACHE_BYTES = 512ull * 1024ull * 1024ull;
    static constexpr auto MAX_DISK_CACHE_AGE = std::chrono::hours(24 * 21);
};

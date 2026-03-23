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
#include "../../../core/QualityConfig.hpp"
#include "CacheModels.hpp"
#include "DiskManifest.hpp"

/**
 * cargador de thumbnails optimizado:
 * - limite de concurrencia
 * - cola por prioridad
 * - cache automatico con manifest persistente
 * - claves canonicas (tipo + levelID/URL + calidad)
 * - cache compartido de URLs de galeria
 * - instrumentacion de hits/misses/evictions
 * - evita lag
 */
class ThumbnailLoader {
public:
    using LoadCallback = geode::CopyableFunction<void(cocos2d::CCTexture2D* texture, bool success)>;
    using InvalidationCallback = geode::CopyableFunction<void(int levelID)>;

    static ThumbnailLoader& get();

    // pedir carga de thumbnail. mayor valor = mas prioridad
    void requestLoad(int levelID, std::string fileName, LoadCallback callback, int priority = 0, bool isGif = false);
    void prefetchLevelAssets(int levelID, int priority = 0);
    void prefetchLevels(std::vector<int> const& levelIDs, int priority = 0);
    
    // carga por URL (para gallery thumbnails compartidos entre vistas)
    void requestUrlLoad(std::string const& url, LoadCallback callback, int priority = 0);
    bool isUrlLoaded(std::string const& url) const;
    void cancelUrlLoad(std::string const& url);

    // cancelar carga pendiente
    void cancelLoad(int levelID, bool isGif = false);
    
    // cache
    bool isLoaded(int levelID, bool isGif = false) const;
    bool isPending(int levelID, bool isGif = false) const;
    bool isFailed(int levelID, bool isGif = false) const;
    void clearCache();
    void invalidateLevel(int levelID, bool isGif = false);

    // revision remota: actualiza el token de revision conocido para un level
    // si es distinto al actual, invalida la cache automaticamente
    void updateRemoteRevision(int levelID, std::string const& revisionToken);

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

    // manifest
    void flushManifest();
    paimon::cache::DiskManifest& diskManifest() { return m_manifest; }

    // instrumentacion
    paimon::cache::CacheStats& stats() { return m_stats; }
    paimon::cache::CacheStats const& stats() const { return m_stats; }

    // deteccion de cambio de quality mid-session
    bool detectQualityChange();

private:
    ThumbnailLoader();
    ~ThumbnailLoader();

    struct Task {
        int levelID;
        std::string fileName;
        std::string url; // para tareas URL-based (gallery)
        int priority;
        std::vector<LoadCallback> callbacks;
        bool running = false;
        bool cancelled = false;
        bool isUrlTask = false; // true si es carga por URL (gallery cache compartido)
    };

    // manejo de cola — int key para level tasks, string key para url tasks
    std::unordered_map<int, std::shared_ptr<Task>> m_tasks; // id -> tarea (pendiente y corriendo)
    std::unordered_map<std::string, std::shared_ptr<Task>> m_urlTasks; // url -> tarea gallery
    std::multimap<int, int, std::greater<int>> m_priorityQueue; // prioridad (desc) -> levelID
    std::atomic<int> m_activeTaskCount{0};
    int m_maxConcurrentTasks = 20;
    mutable std::mutex m_queueMutex;

    // entrada de cache con version de invalidacion
    struct CacheEntry {
        geode::Ref<cocos2d::CCTexture2D> texture;
        int version = 0;
        size_t byteSize = 0; // bytes estimados de la textura
    };

    // cache ram (sesion) — LRU O(1) con list + iterador en map
    std::unordered_map<int, CacheEntry> m_textureCache;
    std::list<int> m_lruOrder;
    std::unordered_map<int, std::list<int>::iterator> m_lruMap;
    size_t m_textureCacheBytes = 0;

    // cache ram URL (gallery compartido) — LRU O(1)
    std::unordered_map<std::string, CacheEntry> m_urlTextureCache;
    std::list<std::string> m_urlLruOrder;
    std::unordered_map<std::string, std::list<std::string>::iterator> m_urlLruMap;
    size_t m_urlCacheBytes = 0;
    static constexpr size_t URL_CACHE_MAX_ENTRIES = 60;
    static constexpr size_t URL_CACHE_MAX_BYTES = 64ull * 1024 * 1024;

    // disk manifest (reemplaza el antiguo unordered_set<int> m_diskCache)
    paimon::cache::DiskManifest m_manifest;

    // legacy disk index — mantenido temporalmente para compatibilidad durante la transicion
    std::unordered_set<int> m_diskCache;
    std::mutex m_diskMutex;
    
    // cache fallidos con TTL (5 minutos)
    std::unordered_map<int, std::chrono::steady_clock::time_point> m_failedCache;
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> m_urlFailedCache;
    static constexpr auto FAILED_CACHE_TTL = std::chrono::minutes(5);
    static constexpr auto FAILED_CACHE_PRUNE_INTERVAL = std::chrono::minutes(1);
    std::chrono::steady_clock::time_point m_lastFailedCachePrune = std::chrono::steady_clock::time_point::min();
    
    // cache gifs
    std::unordered_set<int> m_gifLevels;

    // remote revision tokens por level (thumbnailId o fallback)
    std::unordered_map<int, std::string> m_remoteRevisions;

    // version de invalidacion por level (se incrementa al invalidar)
    std::unordered_map<int, int> m_invalidationVersions;
    std::unordered_map<int, InvalidationCallback> m_invalidationListeners;
    int m_nextInvalidationListenerId = 1;

    bool m_batchMode = false;

    // flag de shutdown
    std::atomic<bool> m_shuttingDown{false};

    // instrumentacion
    paimon::cache::CacheStats m_stats;

    // metodos
    void processQueue();
    void startTask(std::shared_ptr<Task> task);
    void finishTask(std::shared_ptr<Task> task, cocos2d::CCTexture2D* texture, bool success);
    
    void addToCache(int levelID, cocos2d::CCTexture2D* texture, int version = -1);
    void addToUrlCache(std::string const& url, cocos2d::CCTexture2D* texture);
    void evictUrlCacheLocked();
    int getVersionForKeyLocked(int key) const;
    void initDiskCache();
    void pruneFailedCacheLocked(std::chrono::steady_clock::time_point now);
    void pruneDiskCache();
    
    // Worker methods
    void workerLoadFromDisk(std::shared_ptr<Task> task);
    void workerDownload(std::shared_ptr<Task> task);
    void workerUrlDownload(std::shared_ptr<Task> task);
    void spawnBackground(std::function<void()> job);
    void waitBackgroundWorkers();
    void pruneFinishedWorkers();

    // decode helper: decodifica pixeles y aplica downscale por quality fuera del main thread
    struct DecodeResult {
        std::vector<uint8_t> pixels;
        int width = 0;
        int height = 0;
        bool isGif = false;
        bool success = false;
        int64_t decodeTimeUs = 0;
    };
    DecodeResult decodeImageData(std::vector<uint8_t> const& data, int realID);

    std::vector<std::future<void>> m_backgroundWorkers;
    mutable std::mutex m_workerMutex;

    // quota de cache de disco — dynamic per quality tier
    static constexpr auto MAX_DISK_CACHE_AGE = std::chrono::hours(24 * 21);

    // quality tag stored at init so we can detect quality changes mid-session
    std::string m_qualityTag;

    // helper: estima bytes de textura
    static size_t estimateTextureBytes(cocos2d::CCTexture2D* tex);
};

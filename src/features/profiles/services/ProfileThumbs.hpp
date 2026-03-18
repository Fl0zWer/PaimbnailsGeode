#pragma once
#include <Geode/DefaultInclude.hpp>
#include <Geode/utils/cocos.hpp>
#include <Geode/utils/function.hpp>
#include <cocos2d.h>
#include <optional>
#include <unordered_map>
#include <chrono>
#include <string>
#include <mutex>
#include <atomic>

#include <unordered_set>

struct ProfileConfig {
    std::string backgroundType = "gradient";
    float blurIntensity = 3.0f;
    float darkness = 0.2f;
    bool useGradient = false;
    cocos2d::ccColor3B colorA = {255,255,255};
    cocos2d::ccColor3B colorB = {255,255,255};
    cocos2d::ccColor3B separatorColor = {0,0,0};
    int separatorOpacity = 50;
    float widthFactor = 0.60f;
    bool hasConfig = false;
    std::string gifKey = ""; // gifKey anadido para referencia en cache local
};

struct ProfileCacheEntry {
    geode::Ref<cocos2d::CCTexture2D> texture;
    std::string gifKey;
    cocos2d::ccColor3B colorA;
    cocos2d::ccColor3B colorB;
    float widthFactor;
    std::chrono::steady_clock::time_point timestamp;
    ProfileConfig config;
    
    ProfileCacheEntry() : texture(nullptr), gifKey(""), colorA({255,255,255}), colorB({255,255,255}), 
                         widthFactor(0.5f), timestamp(std::chrono::steady_clock::now()) {}
    
    ProfileCacheEntry(cocos2d::CCTexture2D* tex, cocos2d::ccColor3B ca, cocos2d::ccColor3B cb, float w) 
        : texture(tex), gifKey(""), colorA(ca), colorB(cb), widthFactor(w), 
          timestamp(std::chrono::steady_clock::now()) {}

    ProfileCacheEntry(std::string const& key, cocos2d::ccColor3B ca, cocos2d::ccColor3B cb, float w) 
        : texture(nullptr), gifKey(key), colorA(ca), colorB(cb), widthFactor(w), 
          timestamp(std::chrono::steady_clock::now()) {}
};

class ProfileThumbs {
public:
    static ProfileThumbs& get();
    
    // flag de shutdown: cuando es true, no se liberan objetos cocos en destructores estaticos
    static inline std::atomic<bool> s_shutdownMode{false};

    ~ProfileThumbs() {
        if (s_shutdownMode.load(std::memory_order_acquire)) {
            m_pendingCallbacks.clear();
            m_downloadQueue.clear();
            for (auto& [id, entry] : m_profileCache) {
                (void)entry.texture.take();
            }
            m_lruOrder.clear();
            m_lruMap.clear();
        }
    }

    bool saveRGB(int accountID, const uint8_t* rgb, int width, int height);
    bool has(int accountID) const;
    void deleteProfile(int accountID); // borra local + cache
    cocos2d::CCTexture2D* loadTexture(int accountID);
    bool loadRGB(int accountID, std::vector<uint8_t>& out, int& w, int& h);

    // cache temporal de perfiles
    void cacheProfile(int accountID, cocos2d::CCTexture2D* texture, 
                     cocos2d::ccColor3B colorA, cocos2d::ccColor3B colorB, float widthFactor);
    
    void cacheProfileGIF(int accountID, std::string const& gifKey, 
                     cocos2d::ccColor3B colorA, cocos2d::ccColor3B colorB, float widthFactor);
    
    void cacheProfileConfig(int accountID, ProfileConfig const& config);
    ProfileConfig getProfileConfig(int accountID);

    ProfileCacheEntry* getCachedProfile(int accountID);
    void clearCache(int accountID); // elimina entrada especifica
    void clearOldCache(); // elimina entradas mas viejas de 14 dias
    void clearAllCache(); // limpia todas las entradas cacheadas

    // crea un nodo con fondo + imagen de perfil
    cocos2d::CCNode* createProfileNode(cocos2d::CCTexture2D* texture, ProfileConfig const& config, cocos2d::CCSize size, bool onlyBackground = false);

    // mete en cola la descarga de un perfil
    void queueLoad(int accountID, std::string const& username, geode::CopyableFunction<void(bool, cocos2d::CCTexture2D*)> callback);

    // notifica que un perfil es visible en pantalla (sube prioridad)
    void notifyVisible(int accountID);

    // manejo de cache negativa (usuarios sin perfil)
    void markNoProfile(int accountID);
    void removeFromNoProfileCache(int accountID);
    bool isNoProfile(int accountID) const;
    void clearNoProfileCache();

    // limpia callbacks y cola de descarga pendientes (para shutdown seguro)
    void clearPendingDownloads();

private:
    ProfileThumbs() = default;
    std::string makePath(int accountID) const;
    void processQueue();
    
    std::unordered_map<int, ProfileCacheEntry> m_profileCache;
    // LRU O(1): lista de orden de acceso + mapa de iteradores
    std::list<int> m_lruOrder;
    std::unordered_map<int, std::list<int>::iterator> m_lruMap;
    mutable std::mutex m_cacheMutex; // protege m_profileCache de acceso concurrente
    std::unordered_map<int, std::chrono::steady_clock::time_point> m_visibilityMap; // cuando se vio por ultima vez el perfil
    std::unordered_set<int> m_noProfileCache; // cuentas sin perfil, pa no spamear requests
    std::unordered_map<int, std::string> m_usernameMap; // accountID -> username para descargas pendientes
    static constexpr auto CACHE_DURATION = std::chrono::hours(24 * 14); // 14 dias
    static constexpr size_t MAX_PROFILE_CACHE_SIZE = 100; // limite de entradas en cache
    int m_insertsSinceCleanup = 0;
    static constexpr int CLEANUP_INTERVAL = 20; // cada N inserciones revisar expiradas

    // sistema de cola
    std::deque<int> m_downloadQueue;
    std::unordered_map<int, std::vector<geode::CopyableFunction<void(bool, cocos2d::CCTexture2D*)>>> m_pendingCallbacks;
    int m_activeDownloads = 0;
    const int MAX_CONCURRENT_DOWNLOADS = 50;
};


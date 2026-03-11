#pragma once

#include <string>
#include <optional>
#include <unordered_map>
#include <list>
#include <mutex>
#include <cocos2d.h>

// Cache de roles de moderador/admin por username.
// Singleton thread-safe con eviction LRU.
// Reemplaza los extern globales de BadgeCache.hpp.

struct ModStatus {
    bool isMod = false;
    bool isAdmin = false;
};

class ModeratorCache {
public:
    static ModeratorCache& get() {
        static ModeratorCache instance;
        return instance;
    }

    // inserta o actualiza el rol de un usuario.
    // admin implica mod (convencion del proyecto).
    void insert(std::string const& username, bool isMod, bool isAdmin) {
        std::lock_guard lock(m_mutex);

        // normalizar: admin siempre es mod
        if (isAdmin) isMod = true;

        if (m_cache.contains(username)) {
            m_cache[username] = {isMod, isAdmin};
            return;
        }

        // purgar la mitad mas antigua si superamos el limite
        while (m_cache.size() >= MAX_SIZE && !m_order.empty()) {
            m_cache.erase(m_order.front());
            m_order.pop_front();
        }

        m_cache[username] = {isMod, isAdmin};
        m_order.push_back(username);
    }

    // devuelve el estado del usuario si esta en cache
    std::optional<ModStatus> lookup(std::string const& username) {
        std::lock_guard lock(m_mutex);

        auto it = m_cache.find(username);
        if (it == m_cache.end()) return std::nullopt;
        return it->second;
    }

    void clear() {
        std::lock_guard lock(m_mutex);
        m_cache.clear();
        m_order.clear();
    }

private:
    ModeratorCache() = default;
    ModeratorCache(ModeratorCache const&) = delete;
    ModeratorCache& operator=(ModeratorCache const&) = delete;

    static constexpr size_t MAX_SIZE = 200;
    std::mutex m_mutex;
    std::unordered_map<std::string, ModStatus> m_cache;
    std::list<std::string> m_order;
};

// === Wrappers legacy (definidos en BadgeHooks.cpp) ===
// Mantienen compatibilidad con código que usaba BadgeCache.hpp.
void moderatorCacheInsert(std::string const& username, bool isMod, bool isAdmin);
bool moderatorCacheGet(std::string const& username, bool& isMod, bool& isAdmin);

// muestra el popup con info del badge (implementada en BadgeHooks.cpp)
void showBadgeInfoPopup(cocos2d::CCNode* sender);

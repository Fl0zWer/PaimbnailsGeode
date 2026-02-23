#pragma once

#include <Geode/DefaultInclude.hpp>
#include <Geode/utils/cocos.hpp>
#include <cocos2d.h>
#include <unordered_map>
#include <memory>

// gestiona miniaturas temporales descargadas del servidor (solo memoria, limpiadas al reiniciar)
class TempThumbnails {
public:
    static TempThumbnails& get();
    
    void store(int levelID, cocos2d::CCTexture2D* texture);
    cocos2d::CCTexture2D* get(int levelID);
    bool has(int levelID) const;
    void clear();
    
    // flag de shutdown: cuando es true, no se liberan objetos cocos en destructores estáticos
    static inline bool s_shutdownMode = false;

private:
    TempThumbnails() = default;
    ~TempThumbnails() {
        // durante el cierre del proceso los destructores estáticos se ejecutan en orden indefinido
        // y el CCPoolManager de cocos2d puede ya estar muerto. geode::Ref llama release() al destruirse.
        // en modo shutdown usamos take() para sacar los objetos sin llamar release().
        if (s_shutdownMode) {
            for (auto& [id, ref] : m_cache) {
                (void)ref.take(); // saca el puntero sin hacer release()
            }
        }
    }
    std::unordered_map<int, geode::Ref<cocos2d::CCTexture2D>> m_cache;
};


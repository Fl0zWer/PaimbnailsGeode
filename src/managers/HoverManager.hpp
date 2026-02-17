#pragma once

#include <Geode/Geode.hpp>
#include <unordered_map>

// forward declaration
class LevelCell;

// info tracking hover por celda.
struct CellHoverInfo {
    cocos2d::CCNode* cell = nullptr;              // nodo celda nivel
    cocos2d::CCSprite* detector = nullptr;         // detector sprite invisible
    cocos2d::CCPoint lastWorldPos = {0, 0};       // ultima posicion conocida (optimizacion)
    bool isHovered = false;              // estado actual de hover
    float hoverLerp = 0.0f;              // smooth lerp (0.0 a 1.0)
    
    // callbacks para updates estado hover.
    std::function<void(bool)> onHoverChange = nullptr;
    std::function<void(float)> onHoverUpdate = nullptr;
};

/**
 * hovermanager - deteccion hover centralizada para celdas.
 * mantiene detectores invisibles en capa separada y sinc con celdas.
 */
class HoverManager : public cocos2d::CCNode {
private:
    static HoverManager* s_instance;
    
    cocos2d::CCLayer* m_detectorLayer = nullptr;
    std::unordered_map<cocos2d::CCNode*, CellHoverInfo> m_cellTracking;
    cocos2d::CCPoint m_lastMousePos = {0, 0};
    bool m_enabled = true;
    
    // solo re-chequear hover cuando mouse se mueve.
    bool m_mouseMoved = false;
    
    HoverManager();
    
public:
    ~HoverManager();
    
    static HoverManager* get();
    static void destroy();
    
    bool init() override;
    void update(float dt) override;
    
    /**
        * registrar celda para tracking hover.
     */
    void registerCell(
        cocos2d::CCNode* cell,
        std::function<void(bool)> onHoverChange = nullptr,
        std::function<void(float)> onHoverUpdate = nullptr
    );
    
    /**
        * desregistrar celda.
     */
    void unregisterCell(cocos2d::CCNode* cell);
    
    /**
        * retorna si celda tiene hover.
     */
    bool isCellHovered(cocos2d::CCNode* cell) const;
    
    /**
        * retorna valor lerp hover (0.0 a 1.0).
     */
    float getCellHoverLerp(cocos2d::CCNode* cell) const;
    
    /**
        * eliminar todos detectores.
     */
    void clearAll();
    
    /**
        * activar/desactivar sistema.
     */
    void setEnabled(bool enabled);
    
private:
    /**
     * actualizar posicion detector para coincidir con celda.
     */
    void updateDetectorPosition(CellHoverInfo& info);
    
    /**
     * chequear si mouse esta sobre detector.
     */
    bool checkMouseOver(const CellHoverInfo& info) const;
    
    /**
     * limpiar detectores de celdas destruidas.
     */
    void cleanupDestroyedCells();
};


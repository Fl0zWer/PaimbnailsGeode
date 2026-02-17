#include "HoverManager.hpp"
#include <Geode/utils/cocos.hpp>

using namespace geode::prelude;

HoverManager* HoverManager::s_instance = nullptr;

HoverManager::HoverManager() {}

HoverManager::~HoverManager() {
    clearAll();
    if (m_detectorLayer) {
        m_detectorLayer->removeFromParent();
        m_detectorLayer = nullptr;
    }
    s_instance = nullptr;
}

HoverManager* HoverManager::get() {
    if (!s_instance) {
        s_instance = new HoverManager();
        if (!s_instance->init()) {
            delete s_instance;
            s_instance = nullptr;
        }
    }
    return s_instance;
}

void HoverManager::destroy() {
    if (s_instance) {
        s_instance->removeFromParent();
        s_instance = nullptr;
    }
}

bool HoverManager::init() {
    if (!CCNode::init()) {
        return false;
    }
    
    // sin hover en android/ios; desactivar sistema.
#if defined(GEODE_IS_ANDROID) || defined(GEODE_IS_IOS)
    m_enabled = false;
    log::info("[HoverManager] sistema hover desactivado en movil (sin mouse)");
    return true;
#endif
    
    // capa detector (solo plataformas mouse).
    m_detectorLayer = CCLayer::create();
    m_detectorLayer->setID("hover-detector-layer"_spr);
    m_detectorLayer->retain();
    
    // no programar updates aqui; hacedlo al aÃ±adir a la escena.
    
    return true;
}

void HoverManager::update(float dt) {
    // movil: no-op.
#if defined(GEODE_IS_ANDROID) || defined(GEODE_IS_IOS)
    return;
#endif
    
    static bool firstCall = true;
    if (firstCall) {
        log::info("[HoverManager] primer update()! sistema funciona.");
        firstCall = false;
    }
    
    if (!m_enabled) {
        log::debug("[HoverManager] update llamado pero desactivado");
        return;
    }
    
    static int frameCount = 0;
    if (frameCount++ % 60 == 0) {
        log::debug("[HoverManager] update corriendo, trackeando {} celdas", m_cellTracking.size());
    }
    
    // obtener posicion mouse actual.
    CCPoint currentMousePos = {0, 0};
    bool hasMousePos = false;
    
#if defined(_WIN32)
    auto* view = CCEGLView::sharedOpenGLView();
    if (view) {
        currentMousePos = view->getMousePosition();
        // invertir y: mouse usa y=0 arriba, cocos2d usa y=0 abajo.
        currentMousePos.y = view->getFrameSize().height - currentMousePos.y;
        hasMousePos = true;
        
        static int logCounter = 0;
        if (logCounter++ % 60 == 0) {
            log::debug("[HoverManager] pos mouse: ({}, {})", currentMousePos.x, currentMousePos.y);
        }
    }
#endif
    
    if (!hasMousePos) {
        log::warn("[HoverManager] sin posicion mouse disponible");
        return;
    }
    
    // solo re-chequear hover cuando mouse se mueve.
    m_mouseMoved = (currentMousePos.x != m_lastMousePos.x || 
                   currentMousePos.y != m_lastMousePos.y);
    m_lastMousePos = currentMousePos;
    
    // eliminar celdas destruidas.
    cleanupDestroyedCells();
    
    // actualizar celdas registradas.
    for (auto& pair : m_cellTracking) {
        CellHoverInfo& info = pair.second;
        
        // saltar celdas perdidas/ocultas.
        if (!info.cell || !info.cell->isVisible()) {
            continue;
        }
        
        // actualizar posicion detector.
        updateDetectorPosition(info);
        
        // chequear hover si mouse se movio.
        bool wasHovered = info.isHovered;
        if (m_mouseMoved) {
            info.isHovered = checkMouseOver(info);
            
            if (info.isHovered != wasHovered) {
                log::info("[HoverManager] hover celda cambiado a: {}", info.isHovered);
            }
        }
        
        // notificar cambio estado hover.
        if (info.isHovered != wasHovered && info.onHoverChange) {
            log::debug("[HoverManager] hover celda cambiado: {}", info.isHovered);
            info.onHoverChange(info.isHovered);
        }
        
        // smooth hover lerp.
        float target = info.isHovered ? 1.0f : 0.0f;
        float speed = 8.0f;
        info.hoverLerp += (target - info.hoverLerp) * std::min(1.0f, dt * speed);
        
        // notificar updates hover lerp.
        if (info.onHoverUpdate) {
            info.onHoverUpdate(info.hoverLerp);
        }
    }
}

void HoverManager::registerCell(
    CCNode* cell,
    std::function<void(bool)> onHoverChange,
    std::function<void(float)> onHoverUpdate
) {
    if (!cell) return;
    
    // movil: desactivado.
#if defined(GEODE_IS_ANDROID) || defined(GEODE_IS_IOS)
    return;
#endif
    
    log::info("[HoverManager] registrando celda en {}", fmt::ptr(cell));
    
    // asegurar que manager existe en escena activa.
    auto* scene = CCDirector::sharedDirector()->getRunningScene();
    if (scene && !this->getParent()) {
        scene->addChild(this, 10000); // Alto z-order
        // programar updates cada frame.
        this->schedule(schedule_selector(HoverManager::update), 0.0f);
        log::info("[HoverManager] manager anadido a escena y updates programados con selector");
        log::info("[HoverManager] padre manager: {}, corriendo: {}", fmt::ptr(this->getParent()), this->isRunning());
    } else if (this->getParent()) {
        log::debug("[HoverManager] manager ya en escena");
    }
    
    // asegurar capa detector existe en escena.
    if (!m_detectorLayer->getParent()) {
        if (scene) {
            scene->addChild(m_detectorLayer, 1000); // Z-order alto pero no bloquea clicks
            log::info("[HoverManager] capa detector anadida a escena");
        } else {
            log::warn("[HoverManager] no hay escena corriendo para anadir capa detector!");
            return;
        }
    }
    
    // si ya trackeada, actualizar callbacks.
    if (m_cellTracking.find(cell) != m_cellTracking.end()) {
        auto& info = m_cellTracking[cell];
        info.onHoverChange = onHoverChange;
        info.onHoverUpdate = onHoverUpdate;
        log::info("[HoverManager] callbacks actualizados para celda existente");
        return;
    }
    
    // crear detector invisible.
    auto detector = CCSprite::create();
    detector->setContentSize(cell->getContentSize());
    detector->setOpacity(0); // invisible
    detector->setAnchorPoint({0.5f, 0.5f});
    
    // anadir a capa detector.
    m_detectorLayer->addChild(detector);
    
    // trackear celda.
    CellHoverInfo info;
    info.cell = cell;
    info.detector = detector;
    info.onHoverChange = onHoverChange;
    info.onHoverUpdate = onHoverUpdate;
    info.isHovered = false;
    info.hoverLerp = 0.0f;
    
    // posicion inicial.
    updateDetectorPosition(info);
    
    m_cellTracking[cell] = info;
    
    log::debug("[HoverManager] celda registrada, total: {}", m_cellTracking.size());
}

void HoverManager::unregisterCell(CCNode* cell) {
    auto it = m_cellTracking.find(cell);
    if (it != m_cellTracking.end()) {
        // Remover detector
        if (it->second.detector) {
            it->second.detector->removeFromParent();
        }
        m_cellTracking.erase(it);
        log::debug("[HoverManager] celda desregistrada, quedan: {}", m_cellTracking.size());
    }
}

bool HoverManager::isCellHovered(CCNode* cell) const {
    auto it = m_cellTracking.find(cell);
    return (it != m_cellTracking.end()) ? it->second.isHovered : false;
}

float HoverManager::getCellHoverLerp(CCNode* cell) const {
    auto it = m_cellTracking.find(cell);
    return (it != m_cellTracking.end()) ? it->second.hoverLerp : 0.0f;
}

void HoverManager::clearAll() {
    for (auto& pair : m_cellTracking) {
        if (pair.second.detector) {
            pair.second.detector->removeFromParent();
        }
    }
    m_cellTracking.clear();
    log::debug("[HoverManager] todas las celdas limpiadas");
}

void HoverManager::setEnabled(bool enabled) {
    m_enabled = enabled;
    if (!enabled) {
        // resetear estado hover.
        for (auto& pair : m_cellTracking) {
            if (pair.second.isHovered && pair.second.onHoverChange) {
                pair.second.onHoverChange(false);
            }
            pair.second.isHovered = false;
            pair.second.hoverLerp = 0.0f;
        }
    }
}

void HoverManager::updateDetectorPosition(CellHoverInfo& info) {
    if (!info.cell || !info.detector) return;
    
    // calcular centro celda en coordenadas locales.
    CCSize cellSize = info.cell->getContentSize();
    CCPoint centerLocal = {
        cellSize.width * 0.5f,
        cellSize.height * 0.5f
    };
    
    // convertir a coordenadas mundo.
    CCPoint worldPos = info.cell->convertToWorldSpace(centerLocal);
    
    // saltar movimientos pequenos.
    float threshold = 0.5f; // pixels
    if (std::abs(worldPos.x - info.lastWorldPos.x) < threshold &&
        std::abs(worldPos.y - info.lastWorldPos.y) < threshold) {
        return;
    }
    
    info.lastWorldPos = worldPos;
    
    // convertir a espacio capa detector.
    CCPoint detectorPos = m_detectorLayer->convertToNodeSpace(worldPos);
    
    // actualizar posicion detector.
    info.detector->setPosition(detectorPos);
    
    // sinc tamano (por si celda escalo).
    info.detector->setContentSize(cellSize);
}

bool HoverManager::checkMouseOver(const CellHoverInfo& info) const {
    if (!info.cell) return false;
    
    // usar bounding box celda en espacio mundo.
    CCRect worldBounds = info.cell->boundingBox();
    
    // convertir a espacio mundo.
    if (auto parent = info.cell->getParent()) {
        CCPoint worldPos = parent->convertToWorldSpace(worldBounds.origin);
        worldBounds.origin = worldPos;
    }
    
    // chequear posicion mouse.
    return worldBounds.containsPoint(m_lastMousePos);
}

void HoverManager::cleanupDestroyedCells() {
    std::vector<CCNode*> toRemove;
    
    for (auto& pair : m_cellTracking) {
        CCNode* cell = pair.first;
        
        // eliminar celdas que se fueron o detacharon.
        if (!cell || !cell->getParent() || cell->retainCount() <= 1) {
            toRemove.push_back(cell);
        }
    }
    
    for (CCNode* cell : toRemove) {
        unregisterCell(cell);
    }
}


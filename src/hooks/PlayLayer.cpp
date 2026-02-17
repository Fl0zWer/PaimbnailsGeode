#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/ui/Notification.hpp>
#include <Geode/binding/PlayerObject.hpp>
#include <Geode/binding/HardStreak.hpp>
#include "../layers/CapturePreviewPopup.hpp"
#include "../utils/FramebufferCapture.hpp"
#include "../utils/RenderTexture.hpp"
#include "../utils/Localization.hpp"
#include "../managers/LocalThumbs.hpp"
#include "../managers/ThumbnailAPI.hpp"
#include "../managers/PendingQueue.hpp"
#include "../utils/ImageConverter.hpp"
#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/UILayer.hpp>
#include "../utils/DominantColors.hpp"
#include "../managers/LevelColors.hpp"
#include <cstring>
#include <memory>

using namespace geode::prelude;

namespace {
    std::atomic_bool gCaptureInProgress{false};

    CCNode* findGameplayNode(CCNode* root) {
        if (!root) return nullptr;
        auto* children = root->getChildren();
        if (!children) return nullptr;
        CCObject* obj = nullptr;
        
        // 1. primero busco un GJBaseGameLayer / GameLayer
        for (auto* node : CCArrayExt<CCNode*>(children)) {
            if (node) {
                if (typeinfo_cast<GJBaseGameLayer*>(node)) {
                    log::info("[FindGameplay] Found GJBaseGameLayer");
                    return node;
                }
            }
        }

        // 2. si no, pruebo por ID tipo "game-layer"
        for (auto* node : CCArrayExt<CCNode*>(children)) {
            if (node) {
                std::string id = node->getID();
                if (id == "game-layer" || id == "GameLayer") {
                    log::info("[FindGameplay] Found by ID: {}", id);
                    return node;
                }
            }
        }

        // 3. recursivo por el árbol (saltando UILayer/PauseLayer)
        for (auto* node : CCArrayExt<CCNode*>(children)) {
            if (node) {
                std::string cls = typeid(*node).name();
                std::string id = node->getID();
                
                // salto contenedores que pintan a UI
                if (cls.find("UILayer") != std::string::npos || id == "UILayer") continue;
                if (cls.find("PauseLayer") != std::string::npos) continue;

                if (auto* found = findGameplayNode(node)) return found;
            }
        }
        return nullptr;
    }

    bool buildPathToNode(CCNode* root, CCNode* target, std::vector<CCNode*>& path) {
        if (!root) return false;
        path.push_back(root);
        if (root == target) return true;
        auto* children = root->getChildren();
        if (children) {
            for (auto* obj : CCArrayExt<CCObject*>(children)) {
                if (auto* node = typeinfo_cast<CCNode*>(obj)) {
                    if (buildPathToNode(node, target, path)) return true;
                }
            }
        }
        path.pop_back();
        return false;
    }

    void hideSiblingsOutsidePath(const std::vector<CCNode*>& path, std::vector<CCNode*>& hidden) {
        if (path.size() < 2) return;
        for (size_t i = 0; i + 1 < path.size(); ++i) {
            auto* parent = path[i];
            auto* keepChild = path[i + 1];
            auto* children = parent->getChildren();
            if (!children) continue;
            for (auto* obj : CCArrayExt<CCObject*>(children)) {
                if (auto* node = typeinfo_cast<CCNode*>(obj)) {
                    if (node != keepChild && node->isVisible()) {
                        node->setVisible(false);
                        hidden.push_back(node);
                    }
                }
            }
        }
    }

    bool isNonGameplayOverlay(CCNode* node, bool checkZ) {
        if (!node) return false;
        
        // el player nunca se considera UI
        if (typeinfo_cast<PlayerObject*>(node)) return false;

        // 1. check de z‑order
        if (checkZ && node->getZOrder() >= 10) return true;

        // 2. heurísticas según el nombre de la clase
        std::string cls = typeid(*node).name();
        auto clsL = cls; for (auto& c : clsL) c = (char)tolower(c);
        
        if (clsL.find("uilayer") != std::string::npos ||
            clsL.find("pause") != std::string::npos ||
            clsL.find("menu") != std::string::npos ||
            clsL.find("dialog") != std::string::npos ||
            clsL.find("popup") != std::string::npos ||
            clsL.find("editor") != std::string::npos ||
            clsL.find("notification") != std::string::npos ||
            (clsL.find("label") != std::string::npos && clsL.find("gameobject") == std::string::npos) || // excluyo LabelGameObject
            clsL.find("progress") != std::string::npos ||
            clsL.find("status") != std::string::npos || // overlays tipo Megahack status
            clsL.find("trajectory") != std::string::npos || // cosas de showTrajectory
            clsL.find("hitbox") != std::string::npos) return true;

        // 3. heurísticas por ID
        std::string id = node->getID();
        auto idL = id; for (auto& c : idL) c = (char)tolower(c);
        if (!idL.empty()) {
            static const std::vector<std::string> patterns = {
                "ui", "uilayer", "pause", "menu", "dialog", "popup", "editor", "notification", "btn", "button", "overlay", "checkpoint", "fps", "debug", "attempt", "percent", "progress", "bar", "score", "practice", "hitbox", "trajectory", "status"
            };
            for (auto const& p : patterns) {
                if (idL.find(p) != std::string::npos) return true;
            }
        }
        
        // 4. tipos explícitos
        if (typeinfo_cast<CCMenu*>(node) != nullptr) return true;
        
        // CCLabelBMFont
        if (auto* label = typeinfo_cast<CCLabelBMFont*>(node)) {
             // si es hijo directo con Z alta casi seguro es UI
             // con Z baja todavía puede ser texto del propio nivel
             if (checkZ && node->getZOrder() >= 10) return true;
             // si no es hijo directo, dejo que ID/clase decidan
        }

        return false;
    }

    // oculto UI de forma recursiva
    // checkZ solo lo uso en el primer nivel
    void hideNonGameplayDescendants(CCNode* root, std::vector<CCNode*>& hidden, bool checkZ, PlayLayer* pl) {
        if (!root) return;
        auto* children = root->getChildren();
        if (!children) return;

        for (auto* obj : CCArrayExt<CCObject*>(children)) {
            auto* node = typeinfo_cast<CCNode*>(obj);
            if (!node) continue;

            // no toco a los jugadores
            if (pl) {
                if (node == pl->m_player1 || node == pl->m_player2) continue;
            }

            // si parece UI lo oculto y no sigo bajando
            if (node->isVisible() && isNonGameplayOverlay(node, checkZ)) {
                node->setVisible(false);
                hidden.push_back(node);
                log::info("[Capture] Hide: ID='{}', Class='{}', Z={}", node->getID(), typeid(*node).name(), node->getZOrder());
            } 
            // si no es UI, solo bajo si parece contenedor/layer
            // con checkZ=false ya no toco cosas del juego
            else {
                std::string cls = typeid(*node).name();
                 // solo bajo si parece contenedor o layer
                 // no bajo en GameObjects, batch nodes, particle systems, etc
                if (cls.find("CCNode") != std::string::npos || cls.find("Layer") != std::string::npos) {
                     // evito meterme en GameLayer
                     if (cls.find("GameLayer") == std::string::npos) {
                        hideNonGameplayDescendants(node, hidden, false, pl);
                     }
                }
            }
        }
    }
}

static bool s_hidePlayerForCapture = false;

class $modify(GIFRecordPlayLayer, PlayLayer) {
    struct Fields {
        float m_frameTimer = 0.0f;
    };

    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        s_hidePlayerForCapture = false;
        log::info("[GIFRecord] init() llamado para level {}", level ? level->m_levelID : 0);
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;
        log::info("[GIFRecord] PlayLayer::init() exitoso");

        log::info("[GIFRecord] init() completado exitosamente");
        return true;
    }
    
    void triggerRecapture(float dt) {
        this->captureScreenshot();
    }

    void captureScreenshot(CapturePreviewPopup* existingPopup = nullptr) {
        if (gCaptureInProgress.load()) return;
        gCaptureInProgress.store(true);

        auto* director = CCDirector::sharedDirector();
        if (!director || !this->m_level) { gCaptureInProgress.store(false); return; }
        auto* scene = director->getRunningScene();

        log::info("=== STARTING CAPTURE ===");
        // debug: logueo los hijos directos del PlayLayer
        {
            auto children = this->getChildren();
            if (children) {
                for (auto* obj : CCArrayExt<CCObject*>(children)) {
                    auto node = typeinfo_cast<CCNode*>(obj);
                    if (node) {
                        std::string cls = typeid(*node).name();
                        std::string id = node->getID();
                        log::info("PlayLayer Child: Class='{}', ID='{}', Z={}, Vis={}", cls, id, node->getZOrder(), node->isVisible());
                    }
                }
            }
        }

        std::vector<CCNode*> hidden;
        
        // estado de visibilidad del player y sus partículas
        struct PlayerVisState {
            bool visible = true;
            bool regTrail = true;
            bool waveTrail = true;
            bool ghostTrail = true;
            bool vehicleGroundPart = true;
            bool robotFire = true;
            
            bool playerGroundPart = true;
            bool trailingPart = true;
            bool shipClickPart = true;
            bool ufoClickPart = true;
            bool robotBurstPart = true;
            bool dashPart = true;
            bool swingBurstPart1 = true;
            bool swingBurstPart2 = true;
            bool landPart0 = true;
            bool landPart1 = true;
            bool dashFireSprite = true;

            std::vector<std::pair<CCNode*, bool>> otherParticles;
        };
        PlayerVisState p1State, p2State;

        auto togglePlayer = [](PlayerObject* p, PlayerVisState& state, bool hide) {
            if (!p) return;
            
            auto toggle = [&](CCNode* node, bool& stateVar, bool hideNode) {
                if (node) {
                    if (hideNode) {
                        stateVar = node->isVisible();
                        node->setVisible(false);
                    } else {
                        node->setVisible(stateVar);
                    }
                }
            };

            if (hide) {
                state.visible = p->isVisible();
                p->setVisible(false);
                
                toggle(p->m_regularTrail, state.regTrail, true);
                toggle(p->m_waveTrail, state.waveTrail, true);
                toggle(p->m_ghostTrail, state.ghostTrail, true);
                toggle(p->m_vehicleGroundParticles, state.vehicleGroundPart, true);
                toggle(p->m_robotFire, state.robotFire, true);
                
                toggle(p->m_playerGroundParticles, state.playerGroundPart, true);
                toggle(p->m_trailingParticles, state.trailingPart, true);
                toggle(p->m_shipClickParticles, state.shipClickPart, true);
                toggle(p->m_ufoClickParticles, state.ufoClickPart, true);
                toggle(p->m_robotBurstParticles, state.robotBurstPart, true);
                toggle(p->m_dashParticles, state.dashPart, true);
                toggle(p->m_swingBurstParticles1, state.swingBurstPart1, true);
                toggle(p->m_swingBurstParticles2, state.swingBurstPart2, true);
                toggle(p->m_landParticles0, state.landPart0, true);
                toggle(p->m_landParticles1, state.landPart1, true);
                toggle(p->m_dashFireSprite, state.dashFireSprite, true);

                // oculto otras partículas que cuelgan del player
                auto children = p->getChildren();
                if (children) {
                    for (auto* obj : CCArrayExt<CCObject*>(children)) {
                        if (auto* node = typeinfo_cast<CCNode*>(obj)) {
                            // me salto miembros que ya manejo arriba para no tocarlos dos veces
                            if (node == p->m_vehicleGroundParticles || 
                                node == p->m_robotFire ||
                                node == p->m_playerGroundParticles ||
                                node == p->m_trailingParticles ||
                                node == p->m_shipClickParticles ||
                                node == p->m_ufoClickParticles ||
                                node == p->m_robotBurstParticles ||
                                node == p->m_dashParticles ||
                                node == p->m_swingBurstParticles1 ||
                                node == p->m_swingBurstParticles2 ||
                                node == p->m_landParticles0 ||
                                node == p->m_landParticles1 ||
                                node == p->m_dashFireSprite) continue;
                            
                            // solo escondo partículas o sprites que parezcan efectos
                            if (typeinfo_cast<CCParticleSystemQuad*>(node) || typeinfo_cast<CCSprite*>(node)) {
                                state.otherParticles.push_back({node, node->isVisible()});
                                node->setVisible(false);
                            }
                        }
                    }
                }

            } else {
                p->setVisible(state.visible);
                
                toggle(p->m_regularTrail, state.regTrail, false);
                toggle(p->m_waveTrail, state.waveTrail, false);
                toggle(p->m_ghostTrail, state.ghostTrail, false);
                toggle(p->m_vehicleGroundParticles, state.vehicleGroundPart, false);
                toggle(p->m_robotFire, state.robotFire, false);

                toggle(p->m_playerGroundParticles, state.playerGroundPart, false);
                toggle(p->m_trailingParticles, state.trailingPart, false);
                toggle(p->m_shipClickParticles, state.shipClickPart, false);
                toggle(p->m_ufoClickParticles, state.ufoClickPart, false);
                toggle(p->m_robotBurstParticles, state.robotBurstPart, false);
                toggle(p->m_dashParticles, state.dashPart, false);
                toggle(p->m_swingBurstParticles1, state.swingBurstPart1, false);
                toggle(p->m_swingBurstParticles2, state.swingBurstPart2, false);
                toggle(p->m_landParticles0, state.landPart0, false);
                toggle(p->m_landParticles1, state.landPart1, false);
                toggle(p->m_dashFireSprite, state.dashFireSprite, false);

                for (auto& pair : state.otherParticles) {
                    pair.first->setVisible(pair.second);
                }
                state.otherParticles.clear();
            }
        };

        if (s_hidePlayerForCapture) {
            togglePlayer(this->m_player1, p1State, true);
            togglePlayer(this->m_player2, p2State, true);
        }
        
        // oculto UI con checkZ activado en la raíz
        log::info("[Capture] Iniciando limpieza recursiva de UI en PlayLayer");
        hideNonGameplayDescendants(this, hidden, true, this);

        // debug
        if (!hidden.empty()) log::info("[Capture] Ocultados {} nodos (recursivo)", hidden.size());

        auto hiddenCopy = hidden; // copia para restaurar luego
        int levelID = this->m_level->m_levelID;
        
        // capturo el framebuffer completo con overlays ya ocultos
        log::info("[Capture] Capturando PlayLayer usando RenderTexture");
        
        // oculto m_uiLayer a mano por si no lo pilló la recursiva
        if (this->m_uiLayer && this->m_uiLayer->isVisible()) {
            this->m_uiLayer->setVisible(false);
            hidden.push_back(this->m_uiLayer);
            log::info("[Capture] Ocultando m_uiLayer explícitamente");
        }

        // pasada extra por los hijos directos para pillar overlays que se hayan escapado
        for (auto* obj : CCArrayExt<CCObject*>(this->getChildren())) {
            auto* node = typeinfo_cast<CCNode*>(obj);
            if (!node) continue;
            if (node == this->m_uiLayer) continue; 
            
            // visible y marcado como overlay
            // aquí checkZ=true porque son hijos directos del PlayLayer
            if (node->isVisible() && isNonGameplayOverlay(node, true)) {
                // si ya está en hidden, lo dejo en paz
                bool alreadyHidden = false;
                for(auto* h : hidden) if(h == node) { alreadyHidden = true; break; }
                
                if (!alreadyHidden) {
                    node->setVisible(false);
                    hidden.push_back(node);
                    log::info("[Capture] Ocultando nodo UI (Backup Loop): ID='{}', Class='{}', Z={}", 
                        node->getID(), typeid(*node).name(), node->getZOrder());
                }
            }
        }

        // 2. captura propiamente dicha
        auto view = CCEGLView::sharedOpenGLView();
        auto screenSize = view->getFrameSize();
        int width = static_cast<int>(screenSize.width);
        int height = static_cast<int>(screenSize.height);

        std::unique_ptr<uint8_t[]> data;
        bool needsVerticalFlip = true;

        // pinto en un RenderTexture para leer los píxeles
        RenderTexture rt(width, height);
        rt.begin();
        this->visit();
        rt.end();
        data = rt.getData();
        
        needsVerticalFlip = true; // necesito flip porque glReadPixels va de abajo a arriba

        // restauro la visibilidad de todo lo que oculté
        for (auto* n : hiddenCopy) {
            if (n) {
                try {
                    n->setVisible(true);
                } catch (...) {
                    // el nodo ya no existe, lo ignoro
                }
            }
        }
        
        // restauro visibilidad de los jugadores
        if (s_hidePlayerForCapture) {
            togglePlayer(this->m_player1, p1State, false);
            togglePlayer(this->m_player2, p2State, false);
        }
        
        if (!data) { 
            gCaptureInProgress.store(false); 
            return; 
        }

        // flip en Y (para dejar la imagen con origen en la esquina superior)
        if (needsVerticalFlip) {
            int rowSize = width * 4;
            std::vector<uint8_t> tempRow(rowSize);
            uint8_t* buffer = data.get();
            for (int y = 0; y < height / 2; ++y) {
                uint8_t* topRow = buffer + y * rowSize;
                uint8_t* bottomRow = buffer + (height - y - 1) * rowSize;
                std::memcpy(tempRow.data(), topRow, rowSize);
                std::memcpy(topRow, bottomRow, rowSize);
                std::memcpy(bottomRow, tempRow.data(), rowSize);
            }
        }

        // creo un CCTexture2D con los datos RGBA8888
        auto* tex = new CCTexture2D();
        tex->initWithData(data.get(), kCCTexture2DPixelFormat_RGBA8888, width, height, CCSize(width, height));
        tex->autorelease();
        tex->retain(); // lo retengo mientras lo use el popup

        // copio a un shared_ptr para que el popup lo gestione cómodo
        std::shared_ptr<uint8_t> rgba(new uint8_t[width * height * 4], std::default_delete<uint8_t[]>());
        memcpy(rgba.get(), data.get(), width * height * 4);
        
        bool pausedByPopup = false;
        if (!this->m_isPaused) { this->pauseGame(true); pausedByPopup = true; }
        
        // si el popup ya existe, solo actualizo su contenido y lo muestro
        if (existingPopup) {
            existingPopup->updateContent(tex, rgba, width, height);
            existingPopup->setVisible(true);
            tex->release(); // updateContent ya hizo su propio retain
            return;
        }

        // helper para ver si el usuario es moderador (copiado de PauseLayer)
        auto isUserModerator = []() -> bool {
            try {
                auto modDataPath = Mod::get()->getSaveDir() / "moderator_verification.dat";
                if (std::filesystem::exists(modDataPath)) {
                    std::ifstream modFile(modDataPath, std::ios::binary);
                    if (modFile) {
                        time_t timestamp{};
                        modFile.read(reinterpret_cast<char*>(&timestamp), sizeof(timestamp));
                        modFile.close();
                        auto now = std::chrono::system_clock::now();
                        auto fileTime = std::chrono::system_clock::from_time_t(timestamp);
                        auto daysDiff = std::chrono::duration_cast<std::chrono::hours>(now - fileTime).count() / 24;
                        if (daysDiff < 30) {
                            return true;
                        }
                    }
                }
                return Mod::get()->getSavedValue<bool>("is-verified-moderator", false);
            } catch (...) {
                return false;
            }
        };

        bool isMod = isUserModerator();

        auto* popup = CapturePreviewPopup::create(
            tex, 
            levelID, 
            rgba, 
            width, 
            height, 
            [levelID, pausedByPopup](bool okSave, int levelIDAccepted, std::shared_ptr<uint8_t> buf, int W, int H, std::string mode, std::string replaceId){
            try {
                // reseteo la flag al cerrar el popup
                // así puedes hacer más capturas aunque sigan corriendo callbacks async
                gCaptureInProgress.store(false);
                
                // intento despausar el juego si lo pausé para mostrar el popup
                // solo si no hay PauseLayer en escena y PlayLayer sigue vivo
                if (pausedByPopup) {
                    auto* pl = PlayLayer::get();
                    if (pl && pl->m_isPaused) {
                        bool hasPause = false;
                        if (auto* sc = CCDirector::sharedDirector()->getRunningScene()) {
                            CCArrayExt<CCNode*> children(sc->getChildren());
                            for (auto child : children) { 
                                if (typeinfo_cast<PauseLayer*>(child)) { 
                                    hasPause = true; 
                                    break; 
                                } 
                            }
                        }
                        if (!hasPause) {
                            if (auto* d = CCDirector::sharedDirector()) {
                                if (d->getScheduler() && d->getActionManager()) {
                                    d->getScheduler()->resumeTarget(pl);
                                    d->getActionManager()->resumeTarget(pl);
                                    pl->m_isPaused = false;
                                }
                            }
                        }
                    }
                }

                // si el usuario acepta guardar, proceso la mini
                if (okSave && levelIDAccepted > 0 && buf) {
                    // aquí podríamos guardar el buffer en disco local si se quiere
                    // LocalThumbs::get().saveFromRGBA(...)

                    // saco colores dominantes para los gradientes
                    // DominantColors trabaja en RGB, así que paso RGBA->RGB primero
                    
                    std::vector<uint8_t> rgbData(static_cast<size_t>(W) * static_cast<size_t>(H) * 3);
                    const uint8_t* src = buf.get();
                    for(size_t i=0; i < static_cast<size_t>(W)*H; ++i) {
                        rgbData[i*3+0] = src[i*4+0];
                        rgbData[i*3+1] = src[i*4+1];
                        rgbData[i*3+2] = src[i*4+2];
                    }

                    auto pair = DominantColors::extract(rgbData.data(), W, H);
                    ccColor3B A{pair.first.r, pair.first.g, pair.first.b};
                    ccColor3B B{pair.second.r, pair.second.g, pair.second.b};
                    LevelColors::get().set(levelIDAccepted, A, B);

                    // convierto a PNG (ImageConverter::rgbToPng espera vector)
                    
                    std::vector<uint8_t> rgbaData(static_cast<size_t>(W) * static_cast<size_t>(H) * 4);
                    memcpy(rgbaData.data(), buf.get(), rgbaData.size());
                    
                    std::vector<uint8_t> pngData;
                    if (!ImageConverter::rgbToPng(rgbaData, static_cast<uint32_t>(W), static_cast<uint32_t>(H), pngData)) {
                        Notification::create(Localization::get().getString("capture.save_png_error"), NotificationIcon::Error)->show();
                    } else {
                        // username/accountID del jugador actual
                        std::string username;
                        int accountID = 0;
                        if (auto* gm = GameManager::sharedState()) {
                            username = gm->m_playerName;
                            accountID = gm->m_playerUserID;
                        }
                        if (username.empty()) username = "unknown";

                        if (accountID <= 0) {
                            Notification::create("Tienes que tener cuenta para subir", NotificationIcon::Error)->show();
                            return;
                        }

                        // verifico si el usuario es moderador antes de subir
                        Notification::create(Localization::get().getString("capture.verifying"), NotificationIcon::Info)->show();
                        // el server igual valida por nombre/accountID
                        ThumbnailAPI::get().checkModeratorAccount(username, accountID, [levelIDAccepted, pngData, username](bool approved, bool isAdmin) {
                            bool allowModeratorFlow = approved;
                            if (allowModeratorFlow) {
                                // moderador verificado -> subo como thumbnail oficial
                                log::info("[PlayLayer] Usuario verificado como moderador, subiendo thumbnail");
                                Notification::create(Localization::get().getString("capture.uploading"), NotificationIcon::Info)->show();
                                ThumbnailAPI::get().uploadThumbnail(levelIDAccepted, pngData, username, [levelIDAccepted](bool success, const std::string& msg){
                                    if (success) {
                                        Notification::create(Localization::get().getString("capture.upload_success"), NotificationIcon::Success)->show();
                                        PendingQueue::get().removeForLevel(levelIDAccepted);
                                    } else {
                                        Notification::create(Localization::get().getString("capture.upload_error") + (msg.empty() ? std::string("") : (" (" + msg + ")")), NotificationIcon::Error)->show();
                                    }
                                });
                            } else {
                                // si no es moderador: subo como sugerencia y lo meto en la cola local
                                log::info("[PlayLayer] Usuario no es moderador, subiendo sugerencia y encolando");
                                
                                // primero subo la sugerencia
                                Notification::create(Localization::get().getString("capture.uploading_suggestion"), NotificationIcon::Info)->show();
                                ThumbnailAPI::get().uploadSuggestion(levelIDAccepted, pngData, username, [levelIDAccepted, username](bool success, const std::string& msg) {
                                    if (success) {
                                        log::info("[PlayLayer] Sugerencia subida exitosamente");
                                        // suggestions -> cola de Verify
                                        // no puedo saber si es creador aquí, así que uso false
                                        PendingQueue::get().addOrBump(levelIDAccepted, PendingCategory::Verify, username, {}, false);
                                        Notification::create(Localization::get().getString("capture.suggested"), NotificationIcon::Success)->show();
                                    } else {
                                        log::error("[PlayLayer] Error subiendo sugerencia: {}", msg);
                                        Notification::create(Localization::get().getString("capture.upload_error") + (msg.empty() ? std::string("") : (" (" + msg + ")")), NotificationIcon::Error)->show();
                                    }
                                });
                            }
                        });
                    }
                }
                // la flag ya se reseteó arriba
            } catch (const std::exception& e) {
                log::error("[Capture] Exception en callback de captura: {}", e.what());
                gCaptureInProgress.store(false);
            } catch (...) {
                log::error("[Capture] Exception desconocida en callback de captura");
                gCaptureInProgress.store(false);
            }
        },
        [this](bool hidePlayer, CapturePreviewPopup* popup) {
            s_hidePlayerForCapture = hidePlayer;
            
            // oculto el popup un momento para hacer la nueva captura
            if (popup) popup->setVisible(false);

            gCaptureInProgress = false;
            Loader::get()->queueInMainThread([this, popup]() {
                this->captureScreenshot(popup);
            });
        },
        s_hidePlayerForCapture
        );
        if (popup) { 
            if (existingPopup) {
                // popup ya existe -> esto debería ser solo un update, no una creación nueva
                // TODO: reestructurar este flujo para que sea más claro
            } else {
                popup->show(); 
            }
            try {
                tex->release();
            } catch (...) {
                log::error("[Capture] Error releasing texture after showing popup");
            }
        }
        else { 
            try {
                tex->release();
            } catch (...) {
                log::error("[Capture] Error releasing texture when popup creation failed");
            }
            gCaptureInProgress.store(false); 
        }
    }
};

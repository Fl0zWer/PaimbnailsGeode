#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/ui/Notification.hpp>
#include "../utils/PaimonNotification.hpp"
#include <Geode/binding/PlayerObject.hpp>
#include <Geode/binding/HardStreak.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <Geode/loader/GameEvent.hpp>
#include <Geode/utils/Keyboard.hpp>
#include "../layers/CapturePreviewPopup.hpp"
#include "../utils/FramebufferCapture.hpp"
#include "../utils/RenderTexture.hpp"
#include "../utils/PlayerToggleHelper.hpp"
#include "../utils/Localization.hpp"
#include "../managers/LocalThumbs.hpp"
#include "../managers/ThumbnailAPI.hpp"
#include "../managers/PendingQueue.hpp"
#include "../utils/ImageConverter.hpp"
#include "../utils/ModeratorUtils.hpp"
#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/UILayer.hpp>
#include <Geode/binding/CCTextInputNode.hpp>
#include "../utils/DominantColors.hpp"
#include "../managers/LevelColors.hpp"
#include <cstring>
#include <memory>

#include "../managers/DynamicSongManager.hpp"

using namespace geode::prelude;

namespace {
    std::atomic_bool gCaptureInProgress{false};

    // Detecta si hay un campo de texto activo (CCTextInputNode, chat, busqueda, etc.)
    // Cuando el usuario esta escribiendo, NO debemos interceptar teclas normales.
    bool isTextInputActive() {
        auto* scene = CCDirector::sharedDirector()->getRunningScene();
        if (!scene) return false;

        // Buscar iterativamente si hay algun CCTextInputNode con m_selected == true
        // (m_selected se activa cuando el campo de texto tiene el foco del IME)
        std::vector<CCNode*> stack;
        stack.push_back(scene);
        while (!stack.empty()) {
            auto* node = stack.back();
            stack.pop_back();
            if (!node) continue;
            if (auto* textInput = typeinfo_cast<CCTextInputNode*>(node)) {
                if (textInput->m_selected) return true;
            }
            auto* children = node->getChildren();
            if (!children) continue;
            for (auto* child : CCArrayExt<CCNode*>(children)) {
                stack.push_back(child);
            }
        }
        return false;
    }

    CCNode* findGameplayNode(CCNode* root) {
        if (!root) return nullptr;
        auto* children = root->getChildren();
        if (!children) return nullptr;
        CCObject* obj = nullptr;
        
        // 1. primero busco un GJBaseGameLayer / GameLayer
        for (auto* node : CCArrayExt<CCNode*>(children)) {
            if (node) {
                if (typeinfo_cast<GJBaseGameLayer*>(node)) {
                    log::debug("[FindGameplay] Found GJBaseGameLayer");
                    return node;
                }
            }
        }

        // 2. si no, pruebo por ID tipo "game-layer"
        for (auto* node : CCArrayExt<CCNode*>(children)) {
            if (node) {
                std::string id = node->getID();
                if (id == "game-layer" || id == "GameLayer") {
                    log::debug("[FindGameplay] Found by ID: {}", id);
                    return node;
                }
            }
        }

        // 3. recursivo por el arbol (saltando UILayer/PauseLayer)
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

    void hideSiblingsOutsidePath(std::vector<CCNode*> const& path, std::vector<CCNode*>& hidden) {
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

        // 2. heuristicas segun el nombre de la clase
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

        // 3. heuristicas por ID
        std::string id = node->getID();
        auto idL = id; for (auto& c : idL) c = (char)tolower(c);
        if (!idL.empty()) {
            static std::vector<std::string> patterns = {
                "ui", "uilayer", "pause", "menu", "dialog", "popup", "editor", "notification", "btn", "button", "overlay", "checkpoint", "fps", "debug", "attempt", "percent", "progress", "bar", "score", "practice", "hitbox", "trajectory", "status"
            };
            for (auto const& p : patterns) {
                if (idL.find(p) != std::string::npos) return true;
            }
        }
        
        // 4. tipos explicitos
        if (typeinfo_cast<CCMenu*>(node) != nullptr) return true;
        
        // CCLabelBMFont
        if (auto* label = typeinfo_cast<CCLabelBMFont*>(node)) {
             // si es hijo directo con Z alta casi seguro es UI
             // con Z baja todavia puede ser texto del propio nivel
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
                log::debug("[Capture] Hide: ID='{}', Class='{}', Z={}", node->getID(), typeid(*node).name(), node->getZOrder());
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

class $modify(PaimonCapturePlayLayer, PlayLayer) {
    struct Fields {
        float m_frameTimer = 0.0f;
    };

    $override
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        // DEFENSA FINAL: matar dynamic song al entrar al gameplay
        // No importa como llegamos aqui (LevelSelect, LevelInfo, etc.)
        DynamicSongManager::get()->forceKill();

        s_hidePlayerForCapture = false;
        log::info("[PaimonCapture] init() llamado para level {}", level ? level->m_levelID : 0);
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;
        log::info("[PaimonCapture] PlayLayer::init() exitoso");

        // registra listener LOCAL para el keybind de captura.
        // addEventListener se limpia automaticamente cuando PlayLayer se destruye,
        // evitando dangling pointers y crashes por teclas presionadas fuera del gameplay.
        if (Mod::get()->getSettingValue<bool>("enable-thumbnail-taking")) {
            this->addEventListener(
                KeybindSettingPressedEventV3(Mod::get(), "capture-keybind"),
                [this](Keybind const& keybind, bool down, bool repeat, double timestamp) {
                    // solo actua cuando se presiona, no cuando se suelta o repite
                    if (!down || repeat) return;

                    // GUARD: ignorar si hay un campo de texto activo
                    // (evita crash al escribir 'T' en chat, busqueda, etc.)
                    if (isTextInputActive()) return;

                    // GUARD: verificar que este PlayLayer sigue siendo el activo
                    if (PlayLayer::get() != this) return;

                    // verifica que no estemos en pausa
                    if (this->m_isPaused) return;

                    // verifica que el nivel tenga ID valido
                    if (!this->m_level || this->m_level->m_levelID <= 0) {
                        log::info("[Keybind] Level ID invalido, ignorando captura");
                        return;
                    }

                    // evita capturas simultaneas
                    if (gCaptureInProgress.load()) return;
                    gCaptureInProgress.store(true);

                    log::info("[Keybind] Captura activada con tecla: {} (timestamp: {})", keybind.toString(), timestamp);

                    // usa FramebufferCapture (igual que PauseLayer) para diferir
                    // la captura al siguiente frame completo y evitar artefactos
                    int levelID = this->m_level->m_levelID;

                    Ref<PlayLayer> safeRef = this;
                    FramebufferCapture::requestCapture(levelID, [this, safeRef, levelID](bool success, CCTexture2D* texture, std::shared_ptr<uint8_t> rgbaData, int width, int height) {
                        Loader::get()->queueInMainThread([this, safeRef, success, texture, rgbaData, width, height, levelID]() {
                            if (!success || !texture || !rgbaData) {
                                log::error("[Keybind] FramebufferCapture fallo");
                                gCaptureInProgress.store(false);
                                return;
                            }

                            log::info("[Keybind] Captura exitosa: {}x{}", width, height);

                            // pausa el juego para mostrar el popup de preview
                            bool pausedByPopup = false;
                            if (!safeRef->m_isPaused) { safeRef->pauseGame(true); pausedByPopup = true; }

                            auto* popup = CapturePreviewPopup::create(
                                texture,
                                levelID,
                                rgbaData,
                                width,
                                height,
                                [levelID, pausedByPopup](bool okSave, int levelIDAccepted, std::shared_ptr<uint8_t> buf, int W, int H, std::string mode, std::string replaceId){
                                    gCaptureInProgress.store(false);

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

                                    if (okSave && levelIDAccepted > 0 && buf) {
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

                                        std::vector<uint8_t> rgbaVec(static_cast<size_t>(W) * static_cast<size_t>(H) * 4);
                                        memcpy(rgbaVec.data(), buf.get(), rgbaVec.size());
                                        
                                        std::vector<uint8_t> pngData;
                                        if (!ImageConverter::rgbToPng(rgbaVec, static_cast<uint32_t>(W), static_cast<uint32_t>(H), pngData)) {
                                            PaimonNotify::create(Localization::get().getString("capture.save_png_error"), NotificationIcon::Error)->show();
                                        } else {
                                            std::string username;
                                            int accountID = 0;
                                            if (auto* gm = GameManager::sharedState()) {
                                                username = gm->m_playerName;
                                                accountID = gm->m_playerUserID;
                                            }
                                            if (username.empty()) username = "unknown";

                                            if (accountID <= 0) {
                                                PaimonNotify::create(Localization::get().getString("level.account_required"), NotificationIcon::Error)->show();
                                                return;
                                            }

                                            PaimonNotify::create(Localization::get().getString("capture.verifying"), NotificationIcon::Info)->show();
                                            ThumbnailAPI::get().checkModeratorAccount(username, accountID, [levelIDAccepted, pngData, username](bool approved, bool isAdmin) {
                                                bool allowModeratorFlow = approved;
                                                if (allowModeratorFlow) {
                                                    log::info("[Keybind] Usuario verificado como moderador, subiendo thumbnail");
                                                    PaimonNotify::create(Localization::get().getString("capture.uploading"), NotificationIcon::Info)->show();
                                                    ThumbnailAPI::get().uploadThumbnail(levelIDAccepted, pngData, username, [levelIDAccepted](bool success, std::string const& msg){
                                                        if (success) {
                                                            PaimonNotify::create(Localization::get().getString("capture.upload_success"), NotificationIcon::Success)->show();
                                                            PendingQueue::get().removeForLevel(levelIDAccepted);
                                                        } else {
                                                            PaimonNotify::create(Localization::get().getString("capture.upload_error") + (msg.empty() ? std::string("") : (" (" + msg + ")")), NotificationIcon::Error)->show();
                                                        }
                                                    });
                                                } else {
                                                    log::info("[Keybind] Usuario no es moderador, subiendo sugerencia");
                                                    PaimonNotify::create(Localization::get().getString("capture.uploading_suggestion"), NotificationIcon::Info)->show();
                                                    ThumbnailAPI::get().uploadSuggestion(levelIDAccepted, pngData, username, [levelIDAccepted, username](bool success, std::string const& msg) {
                                                        if (success) {
                                                            log::info("[Keybind] Sugerencia subida exitosamente");
                                                            PendingQueue::get().addOrBump(levelIDAccepted, PendingCategory::Verify, username, {}, false);
                                                            PaimonNotify::create(Localization::get().getString("capture.suggested"), NotificationIcon::Success)->show();
                                                        } else {
                                                            log::error("[Keybind] Error subiendo sugerencia: {}", msg);
                                                            PaimonNotify::create(Localization::get().getString("capture.upload_error") + (msg.empty() ? std::string("") : (" (" + msg + ")")), NotificationIcon::Error)->show();
                                                        }
                                                    });
                                                }
                                            });
                                        }
                                    }
                                },
                                [this, safeRef](bool hidePlayer, CapturePreviewPopup* popup) {
                                    s_hidePlayerForCapture = hidePlayer;
                                    if (popup) popup->setVisible(false);
                                    gCaptureInProgress = false;
                                    // safeRef mantiene vivo el objeto; this es valido porque safeRef == this
                                    Loader::get()->queueInMainThread([this, safeRef, popup]() {
                                        this->captureScreenshot(popup);
                                    });
                                },
                                s_hidePlayerForCapture
                            );
                            if (popup) {
                                popup->show();
                            } else {
                                gCaptureInProgress.store(false);
                            }
                        });
                    });
                }
            );
            log::info("[PaimonCapture] Keybind listener LOCAL registrado para captura");
        }

        log::info("[PaimonCapture] init() completado exitosamente");
        return true;
    }
    
    $override
    void onQuit() {
        // Cancela cualquier captura pendiente para evitar que s_request
        // mantenga una referencia (Ref<PlayLayer>) a este nodo despues de
        // que se destruya. Sin esto, la lambda capturada sobrevive como
        // variable estatica y al cerrar el proceso intenta liberar un
        // PlayLayer ya destruido -> crash en PlayLayer::~PlayLayer.
        FramebufferCapture::cancelPending();
        gCaptureInProgress.store(false);

        // addEventListener se limpia automaticamente al destruir el nodo
        PlayLayer::onQuit();
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

        log::debug("=== STARTING CAPTURE ===");
        // debug: logueo los hijos directos del PlayLayer
        {
            auto children = this->getChildren();
            if (children) {
                for (auto* obj : CCArrayExt<CCObject*>(children)) {
                    auto node = typeinfo_cast<CCNode*>(obj);
                    if (node) {
                        std::string cls = typeid(*node).name();
                        std::string id = node->getID();
                        log::debug("PlayLayer Child: Class='{}', ID='{}', Z={}, Vis={}", cls, id, node->getZOrder(), node->isVisible());
                    }
                }
            }
        }

        std::vector<CCNode*> hidden;
        
        PlayerVisState p1State, p2State;

        if (s_hidePlayerForCapture) {
            paimTogglePlayer(this->m_player1, p1State, true);
            paimTogglePlayer(this->m_player2, p2State, true);
        }
        
        // oculto UI con checkZ activado en la raiz
        log::debug("[Capture] Iniciando limpieza recursiva de UI en PlayLayer");
        hideNonGameplayDescendants(this, hidden, true, this);

        // debug
        if (!hidden.empty()) log::debug("[Capture] Ocultados {} nodos (recursivo)", hidden.size());

        auto hiddenCopy = hidden; // copia para restaurar luego
        int levelID = this->m_level->m_levelID;
        
        // capturo el framebuffer completo con overlays ya ocultos
        log::debug("[Capture] Capturando PlayLayer usando RenderTexture");

        // oculto m_uiLayer a mano por si no lo pillo la recursiva
        if (this->m_uiLayer && this->m_uiLayer->isVisible()) {
            this->m_uiLayer->setVisible(false);
            hidden.push_back(this->m_uiLayer);
            log::debug("[Capture] Ocultando m_uiLayer explicitamente");
        }

        // pasada extra por los hijos directos para pillar overlays que se hayan escapado
        for (auto* obj : CCArrayExt<CCObject*>(this->getChildren())) {
            auto* node = typeinfo_cast<CCNode*>(obj);
            if (!node) continue;
            if (node == this->m_uiLayer) continue; 
            
            // visible y marcado como overlay
            // aqui checkZ=true porque son hijos directos del PlayLayer
            if (node->isVisible() && isNonGameplayOverlay(node, true)) {
                // si ya esta en hidden, lo dejo en paz
                bool alreadyHidden = false;
                for(auto* h : hidden) if(h == node) { alreadyHidden = true; break; }
                
                if (!alreadyHidden) {
                    node->setVisible(false);
                    hidden.push_back(node);
                    log::debug("[Capture] Ocultando nodo UI (Backup Loop): ID='{}', Class='{}', Z={}",
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

        // pinto en un RenderTexture para leer los pixeles
        RenderTexture rt(width, height);
        rt.begin();
        this->visit();
        rt.end();
        data = rt.getData();
        
        needsVerticalFlip = true; // necesito flip porque glReadPixels va de abajo a arriba

        // restauro la visibilidad de todo lo que oculte
        for (auto* n : hiddenCopy) {
            if (n) {
                n->setVisible(true);
            }
        }
        
        // restauro visibilidad de los jugadores
        if (s_hidePlayerForCapture) {
            paimTogglePlayer(this->m_player1, p1State, false);
            paimTogglePlayer(this->m_player2, p2State, false);
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
        if (!tex->initWithData(data.get(), kCCTexture2DPixelFormat_RGBA8888, width, height, CCSize(width, height))) {
            tex->release();
            gCaptureInProgress.store(false);
            return;
        }
        // tex tiene refcount=1 desde new. Cada consumidor (popup/updateContent) debe retener.
        // release final al terminar este scope (lineas abajo).

        // copio a un shared_ptr para que el popup lo gestione comodo
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

        bool isMod = PaimonUtils::isUserModerator();

        auto* popup = CapturePreviewPopup::create(
            tex, 
            levelID, 
            rgba, 
            width, 
            height, 
            [levelID, pausedByPopup](bool okSave, int levelIDAccepted, std::shared_ptr<uint8_t> buf, int W, int H, std::string mode, std::string replaceId){
                // reseteo la flag al cerrar el popup
                // asi puedes hacer mas capturas aunque sigan corriendo callbacks async
                gCaptureInProgress.store(false);
                
                // intento despausar el juego si lo pause para mostrar el popup
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
                    // aqui podriamos guardar el buffer en disco local si se quiere
                    // LocalThumbs::get().saveFromRGBA(...)

                    // saco colores dominantes para los gradientes
                    // DominantColors trabaja en RGB, asi que paso RGBA->RGB primero
                    
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
                        PaimonNotify::create(Localization::get().getString("capture.save_png_error"), NotificationIcon::Error)->show();
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
                            PaimonNotify::create(Localization::get().getString("level.account_required"), NotificationIcon::Error)->show();
                            return;
                        }

                        // verifico si el usuario es moderador antes de subir
                        PaimonNotify::create(Localization::get().getString("capture.verifying"), NotificationIcon::Info)->show();
                        // el server igual valida por nombre/accountID
                        ThumbnailAPI::get().checkModeratorAccount(username, accountID, [levelIDAccepted, pngData, username](bool approved, bool isAdmin) {
                            bool allowModeratorFlow = approved;
                            if (allowModeratorFlow) {
                                // moderador verificado -> subo como thumbnail oficial
                                log::info("[PlayLayer] Usuario verificado como moderador, subiendo thumbnail");
                                PaimonNotify::create(Localization::get().getString("capture.uploading"), NotificationIcon::Info)->show();
                                ThumbnailAPI::get().uploadThumbnail(levelIDAccepted, pngData, username, [levelIDAccepted](bool success, std::string const& msg){
                                    if (success) {
                                        PaimonNotify::create(Localization::get().getString("capture.upload_success"), NotificationIcon::Success)->show();
                                        PendingQueue::get().removeForLevel(levelIDAccepted);
                                    } else {
                                        PaimonNotify::create(Localization::get().getString("capture.upload_error") + (msg.empty() ? std::string("") : (" (" + msg + ")")), NotificationIcon::Error)->show();
                                    }
                                });
                            } else {
                                // si no es moderador: subo como sugerencia y lo meto en la cola local
                                log::info("[PlayLayer] Usuario no es moderador, subiendo sugerencia y encolando");
                                
                                // primero subo la sugerencia
                                PaimonNotify::create(Localization::get().getString("capture.uploading_suggestion"), NotificationIcon::Info)->show();
                                ThumbnailAPI::get().uploadSuggestion(levelIDAccepted, pngData, username, [levelIDAccepted, username](bool success, std::string const& msg) {
                                    if (success) {
                                        log::info("[PlayLayer] Sugerencia subida exitosamente");
                                        // suggestions -> cola de Verify
                                        // no puedo saber si es creador aqui, asi que uso false
                                        PendingQueue::get().addOrBump(levelIDAccepted, PendingCategory::Verify, username, {}, false);
                                        PaimonNotify::create(Localization::get().getString("capture.suggested"), NotificationIcon::Success)->show();
                                    } else {
                                        log::error("[PlayLayer] Error subiendo sugerencia: {}", msg);
                                        PaimonNotify::create(Localization::get().getString("capture.upload_error") + (msg.empty() ? std::string("") : (" (" + msg + ")")), NotificationIcon::Error)->show();
                                    }
                                });
                            }
                        });
                    }
                }
                // la flag ya se reseteo arriba
        },
        [this](bool hidePlayer, CapturePreviewPopup* popup) {
            s_hidePlayerForCapture = hidePlayer;
            
            // oculto el popup un momento para hacer la nueva captura
            if (popup) popup->setVisible(false);

            gCaptureInProgress = false;
            this->retain();
            Loader::get()->queueInMainThread([this, popup]() {
                this->captureScreenshot(popup);
                this->release();
            });
        },
        s_hidePlayerForCapture
        );
        if (popup) { 
            if (existingPopup) {
                // popup ya existe -> esto deberia ser solo un update, no una creacion nueva
                // TODO: reestructurar este flujo para que sea mas claro
            } else {
                popup->show(); 
            }
            if (tex) tex->release();
        }
        else { 
            if (tex) tex->release();
            gCaptureInProgress.store(false);
        }
    }
};

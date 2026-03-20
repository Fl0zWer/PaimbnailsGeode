#include <Geode/modify/PauseLayer.hpp>
#include <Geode/binding/PlayLayer.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/utils/cocos.hpp>
#include <Geode/utils/string.hpp>
#include <Geode/loader/Event.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>

#include "../features/thumbnails/services/LocalThumbs.hpp"
#include "../features/capture/ui/CapturePreviewPopup.hpp"
#include "../features/thumbnails/services/ThumbsRegistry.hpp"
#include "../features/capture/services/FramebufferCapture.hpp"
#include "../utils/DominantColors.hpp"
#include "../features/thumbnails/services/LevelColors.hpp"
#include "../utils/Localization.hpp"
#include "../features/moderation/services/PendingQueue.hpp"
#include "../utils/Assets.hpp"
#include "../managers/ThumbnailAPI.hpp"
#include "../utils/ImageConverter.hpp"
#include "../utils/GIFDecoder.hpp"
#include "../utils/FileDialog.hpp"
#include "../features/moderation/services/ModeratorUtils.hpp"
#include "../features/thumbnails/services/ThumbnailLoader.hpp"
#include <Geode/binding/LoadingCircle.hpp>
#include <Geode/binding/GJAccountManager.hpp>
#include <Geode/ui/LoadingSpinner.hpp>
#include "../utils/PaimonNotification.hpp"

#include "../utils/SpriteHelper.hpp"

using namespace geode::prelude;

static CCSprite* tryCreateIcon() {
    auto spr = CCSprite::create("paim_capturadora.png"_spr);
    if (!paimon::SpriteHelper::isValidSprite(spr)) {
        spr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_checkOn_001.png");
    }
    if (!spr) spr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_button_01.png");
    if (spr) {
        constexpr float targetSize = 35.0f;
        float currentSize = std::max(spr->getContentSize().width, spr->getContentSize().height);
        if (currentSize > 0.0f) spr->setScale(targetSize / currentSize);
        spr->setRotation(-90.0f);
    }
    return spr;
}

class $modify(PaimonPauseLayer, PauseLayer) {
    static void onModify(auto& self) {
        // Necesita menus con IDs estables antes de insertar/rehacer botones.
        (void)self.setHookPriorityAfterPost("PauseLayer::customSetup", "geode.node-ids");
    }

                // reconecta boton nativo para usar mismo handler
                // busca items con id de camara o captura
    struct Fields {
        bool m_fileDialogOpen = false;
        bool m_captureInProgress = false;
    };
    $override
    void customSetup() {
        PauseLayer::customSetup();

        log::debug("PauseLayer customSetup called");

        if (!Mod::get()->getSettingValue<bool>("enable-thumbnail-taking")) {
            log::debug("Thumbnail taking disabled in settings");
            return;
        }

        auto playLayer = PlayLayer::get();
        if (!playLayer) {
                        // si id falla, usa nombre de clase
            return;
        }

        if (!playLayer->m_level) {
            log::warn("Level not available in PlayLayer");
            return;
        }

        if (playLayer->m_level->m_levelID <= 0) {
            log::debug("Level ID is {} (not saving thumbnails for this level)", playLayer->m_level->m_levelID.value());
            return;
        }

        auto findButtonMenu = [this](char const* id, bool rightSide) -> CCMenu* {
            if (auto byId = typeinfo_cast<CCMenu*>(this->getChildByID(id))) {
                return byId;
            }
            auto winSize = CCDirector::sharedDirector()->getWinSize();
            CCMenu* best = nullptr;
            float bestScore = 0.f;
            for (auto* node : CCArrayExt<CCNode*>(this->getChildren())) {
                auto menu = typeinfo_cast<CCMenu*>(node);
                if (!menu) continue;
                float x = menu->getPositionX();
                bool sideMatch = rightSide ? (x > winSize.width * 0.5f) : (x < winSize.width * 0.5f);
                if (!sideMatch) continue;
                float score = menu->getChildrenCount();
                if (!best || score > bestScore) {
                    best = menu;
                    bestScore = score;
                }
            }
            return best;
        };

        auto rightMenu = findButtonMenu("right-button-menu", true);
        if (!rightMenu) {
            log::error("Right button menu not found in PauseLayer (including fallback)");
            return;
        }

        auto spr = tryCreateIcon();
            if (!spr) {
                log::error("Failed to create button sprite");
                return;
            }

            auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(PaimonPauseLayer::onScreenshot));
            if (!btn) {
                log::error("Failed to create menu button");
                return;
            }

            btn->setID("thumbnail-capture-button"_spr);
            rightMenu->addChild(btn);
            rightMenu->updateLayout();

            // anade boton para elegir archivo (icono de carpeta)
            {
                auto selectSpr = Assets::loadButtonSprite(
                    "pause-select-file",
                    "frame:accountBtn_myLevels_001.png",
                    []() {
                        if (auto spr = paimon::SpriteHelper::safeCreateWithFrameName("accountBtn_myLevels_001.png")) return spr;
                        return paimon::SpriteHelper::safeCreateWithFrameName("GJ_button_01.png");
                    }
                );

                if (selectSpr) {
                    float targetSize = 30.0f;
                    float currentSize = std::max(selectSpr->getContentSize().width, selectSpr->getContentSize().height);

                    if (currentSize > 0) {
                        float scale = targetSize / currentSize;
                        selectSpr->setScale(scale);
                    }

                    auto selectBtn = CCMenuItemSpriteExtra::create(
                        selectSpr,
                        this,
                        menu_selector(PaimonPauseLayer::onSelectPNGFile)
                    );
                    if (selectBtn) {
                        selectBtn->setID("thumbnail-select-button"_spr);
                        rightMenu->addChild(selectBtn);
                        rightMenu->updateLayout();

                        log::debug("[PauseLayer] Select-file button added");
                    }
                }
            }

            // reconecta boton nativo
            // busca items de camara
            auto rewireScreenshotInMenu = [this](CCNode* menu){
                if (!menu) return;
                CCArray* arr = menu->getChildren();
                if (!arr) return;

                for (auto* obj : CCArrayExt<CCObject*>(arr)) {
                    auto* node = typeinfo_cast<CCNode*>(obj);
                    if (!node) continue;
                    std::string id = node->getID();
                    auto idL = geode::utils::string::toLower(id);
                    bool looksLikeCamera = (!idL.empty() && (idL.find("camera") != std::string::npos || idL.find("screenshot") != std::string::npos));
                    if (auto* item = typeinfo_cast<CCMenuItemSpriteExtra*>(node)) {
                        // usa heuristica de nombre de clase
                        if (!looksLikeCamera) {
                            if (auto* normal = item->getNormalImage()) {
                                auto cls = std::string(typeid(*normal).name());
                                auto clsL = geode::utils::string::toLower(cls);
                                if (clsL.find("camera") != std::string::npos || clsL.find("screenshot") != std::string::npos) {
                                    looksLikeCamera = true;
                                }
                            }
                        }

                        if (looksLikeCamera) {
                            log::info("[PauseLayer] Rewiring native capture button '{}' to onScreenshot", id);
                            item->setTarget(this, menu_selector(PaimonPauseLayer::onScreenshot));
                        }
                    }
                }
            };

            // prueba ambos menus
            rewireScreenshotInMenu(findButtonMenu("right-button-menu", true));
            rewireScreenshotInMenu(findButtonMenu("left-button-menu", false));

            // no llama updateLayout para mantener posiciones
            log::info("Thumbnail capture + extra buttons added successfully");
    }

    void onScreenshot(CCObject*) {
        log::info("[PauseLayer] Capture button pressed; hiding pause menu");
        if (m_fields->m_captureInProgress) {
            log::warn("[PauseLayer] Capture already in progress, ignoring duplicate request");
            return;
        }

        auto pl = PlayLayer::get();
        if (!pl) {
            log::error("[PauseLayer] PlayLayer not available");
            PaimonNotify::create(Localization::get().getString("pause.playlayer_error").c_str(), NotificationIcon::Error)->show();
            return;
        }

        // oculta menu de pausa temporalmente
        this->setVisible(false);
        m_fields->m_captureInProgress = true;

        // muestra circulo de carga inmediatamente
        showLoadingOverlay();
        // Guard rail: si el callback de captura nunca vuelve, restaurar UI.
        this->scheduleOnce(schedule_selector(PaimonPauseLayer::captureSafetyRestore), 8.0f);

        // programa captura y restaura menu
        auto scheduler = CCDirector::sharedDirector()->getScheduler();
        scheduler->scheduleSelector(
            schedule_selector(PaimonPauseLayer::performCaptureAndRestore),
            this,
            0.05f,
            0,
            0.0f,
            false
        );
    }

    void showLoadingOverlay() {
        auto scene = CCDirector::sharedDirector()->getRunningScene();
        if (!scene) return;
        if (auto existing = scene->getChildByID("paimon-loading-overlay"_spr)) {
            existing->removeFromParentAndCleanup(true);
        }

        auto winSize = CCDirector::sharedDirector()->getWinSize();

        auto overlay = CCLayerColor::create({0, 0, 0, 100});
        overlay->setID("paimon-loading-overlay"_spr);
        scene->addChild(overlay, 10000);

        // geode::LoadingSpinner gira automaticamente via scheduler (funciona durante pausa de GD)
        auto spinner = geode::LoadingSpinner::create(40.f);
        spinner->setPosition(winSize / 2);
        spinner->setID("paimon-loading-spinner"_spr);
        overlay->addChild(spinner, 1);
    }

    void reShowOverlay(float dt) {
        auto scene = CCDirector::sharedDirector()->getRunningScene();
        if (!scene) return;
        auto overlay = scene->getChildByID("paimon-loading-overlay"_spr);
        if (overlay) overlay->setVisible(true);
    }

    void removeLoadingOverlay() {
        auto scheduler = CCDirector::sharedDirector()->getScheduler();
        scheduler->unscheduleSelector(
            schedule_selector(PaimonPauseLayer::reShowOverlay), this
        );
        scheduler->unscheduleSelector(
            schedule_selector(PaimonPauseLayer::captureSafetyRestore), this
        );

        auto scene = CCDirector::sharedDirector()->getRunningScene();
        if (!scene) return;
        auto overlay = scene->getChildByID("paimon-loading-overlay"_spr);
        if (overlay) overlay->removeFromParentAndCleanup(true);
    }

    void captureSafetyRestore(float) {
        if (!m_fields->m_captureInProgress) return;
        log::warn("[PauseLayer] Capture watchdog restored UI state");
        m_fields->m_captureInProgress = false;
        removeLoadingOverlay();
        this->setVisible(true);
        PaimonNotify::create(Localization::get().getString("pause.capture_error").c_str(), NotificationIcon::Warning)->show();
    }

    void performCaptureAndRestore(float dt) {
        log::info("[PauseLayer] Performing capture");
        CCDirector::sharedDirector()->getScheduler()->unscheduleSelector(
            schedule_selector(PaimonPauseLayer::performCaptureAndRestore), this
        );

            auto* pl = PlayLayer::get();
            if (!pl || !pl->m_level) {
                log::error("[PauseLayer] PlayLayer or level not available for capture");
                PaimonNotify::create(Localization::get().getString("pause.capture_error").c_str(), NotificationIcon::Error)->show();
                removeLoadingOverlay();
                this->setVisible(true);
                return;
            }

            int levelID = pl->m_level->m_levelID;

            // oculta overlay para captura limpia
            auto scene = CCDirector::sharedDirector()->getRunningScene();
            if (scene) {
                auto overlay = scene->getChildByID("paimon-loading-overlay"_spr);
                if (overlay) overlay->setVisible(false);
            }

            // muestra overlay en siguiente frame
            CCDirector::sharedDirector()->getScheduler()->scheduleSelector(
                schedule_selector(PaimonPauseLayer::reShowOverlay),
                this, 0.0f, 0, 0.0f, false
            );

            // Ref<> en vez de retain/release para seguridad de memoria
            Ref<PauseLayer> safeRef = this;

            // usa framebufferCapture para pantalla
            FramebufferCapture::requestCapture(levelID, [safeRef, levelID](bool success, CCTexture2D* texture, std::shared_ptr<uint8_t> rgbData, int width, int height) {
                Loader::get()->queueInMainThread([safeRef, success, texture, rgbData, width, height, levelID]() {
                    auto* self = static_cast<PaimonPauseLayer*>(safeRef.data());
                    self->removeLoadingOverlay();
                    self->m_fields->m_captureInProgress = false;
                    if (!self->getParent()) return;

                    if (success && texture && rgbData) {
                        log::info("[PauseLayer] Capture successful: {}x{}", width, height);

                        // muestra popup de previsualizacion
                        auto popup = CapturePreviewPopup::create(
                            texture,
                            levelID,
                            rgbData,
                            width,
                            height,
                            // callback aceptar: verifica moderador y sube
                            [](bool accepted, int lvlID, std::shared_ptr<uint8_t> buf, int w, int h, std::string mode, std::string replaceId) {
                                if (!accepted || !buf) {
                                    log::info("[PauseLayer] Thumbnail rejected or invalid buffer");
                                    return;
                                }

                                log::info("[PauseLayer] Thumbnail accepted for level {}", lvlID);

                                // verifica si guardar localmente
                                bool saveLocally = Mod::get()->getSettingValue<bool>("save-thumbnails-locally");
                                if (saveLocally) {
                                    // convierte rgba a rgb para guardar
                                    std::vector<uint8_t> rgbBuf(w * h * 3);
                                    for (int i = 0; i < w * h; i++) {
                                        rgbBuf[i*3 + 0] = buf.get()[i*4 + 0];
                                        rgbBuf[i*3 + 1] = buf.get()[i*4 + 1];
                                        rgbBuf[i*3 + 2] = buf.get()[i*4 + 2];
                                    }
                                    LocalThumbs::get().saveRGB(lvlID, rgbBuf.data(), w, h);
                                    log::info("[PauseLayer] Thumbnail saved locally for level {}", lvlID);
                                }

                                // obtiene nombre usuario para subir
                                std::string username;
                                int accountID = 0;
                                auto* gm = GameManager::get();
                                if (gm) {
                                    username = gm->m_playerName;
                                    if (auto* am = GJAccountManager::get()) {
                                        accountID = am->m_accountID;
                                    }
                                }

                                if (username.empty()) {
                                    log::warn("[PauseLayer] No username available");
                                    PaimonNotify::create(Localization::get().getString("profile.username_error").c_str(), NotificationIcon::Error)->show();
                                    return;
                                }

                                // convierte a png en memoria (sin tocar filesystem = Unicode-safe)
                                std::vector<uint8_t> pngData;
                                if (!ImageConverter::rgbaToPngBuffer(buf.get(), w, h, pngData)) {
                                    log::error("[PauseLayer] Failed to encode PNG in memory");
                                    PaimonNotify::create(Localization::get().getString("capture.save_png_error").c_str(), NotificationIcon::Error)->show();
                                    return;
                                }

                                if (accountID <= 0) {
                                    PaimonNotify::create(Localization::get().getString("level.account_required").c_str(), NotificationIcon::Error)->show();
                                    return;
                                }

                                // verifica moderador y sube
                                PaimonNotify::create(Localization::get().getString("capture.verifying").c_str(), NotificationIcon::Info)->show();

                                ThumbnailAPI::get().checkModeratorAccount(username, accountID, [lvlID, pngData, username](bool isMod, bool isAdmin) {
                                    if (isMod || isAdmin) {
                                        // mods/admins suben directamente
                                        log::info("[PauseLayer] User is moderator, uploading directly");
                                        PaimonNotify::create(Localization::get().getString("capture.uploading").c_str(), NotificationIcon::Info)->show();

                                        ThumbnailAPI::get().uploadThumbnail(lvlID, pngData, username, [lvlID](bool success, std::string const& msg) {
                                            if (success) {
                                                PendingQueue::get().removeForLevel(lvlID);
                                                PaimonNotify::create(Localization::get().getString("capture.upload_success").c_str(), NotificationIcon::Success)->show();
                                                log::info("[PauseLayer] Upload successful for level {}", lvlID);
                                            } else {
                                                PaimonNotify::create(Localization::get().getString("capture.upload_error").c_str(), NotificationIcon::Error)->show();
                                                log::error("[PauseLayer] Upload failed: {}", msg);
                                            }
                                        });
                                    } else {
                                        // usuarios normales suben sugerencia
                                        log::info("[PauseLayer] User is not moderator, uploading as suggestion");
                                        PaimonNotify::create(Localization::get().getString("capture.uploading_suggestion").c_str(), NotificationIcon::Info)->show();

                                        ThumbnailAPI::get().uploadSuggestion(lvlID, pngData, username, [lvlID, username](bool success, std::string const& msg) {
                                            if (success) {
                                                ThumbnailAPI::get().checkExists(lvlID, [lvlID, username](bool exists) {
                                                    auto cat = exists ? PendingCategory::Update : PendingCategory::Verify;
                                                    PendingQueue::get().addOrBump(lvlID, cat, username, {}, false);
                                                    PaimonNotify::create(Localization::get().getString("capture.suggested").c_str(), NotificationIcon::Success)->show();
                                                });
                                            } else {
                                                PaimonNotify::create(Localization::get().getString("capture.upload_error").c_str(), NotificationIcon::Error)->show();
                                                log::error("[PauseLayer] Suggestion upload failed: {}", msg);
                                            }
                                        });
                                    }
                                });
                            },
                            // Recapture callback
                            nullptr,
                            false, // isPlayerHidden
                            PaimonUtils::isUserModerator() // isModerator
                        );

                        if (popup) {
                            popup->show();
                        }
                    } else {
                        log::error("[PauseLayer] Capture failed");
                        PaimonNotify::create(Localization::get().getString("pause.capture_error").c_str(), NotificationIcon::Error)->show();
                    }

                    // restaura menu de pausa
                    safeRef->setVisible(true);
                    log::info("[PauseLayer] Pause menu restored after capture");
                });
            });

    }

    $override
    void onExit() {
        m_fields->m_captureInProgress = false;
        m_fields->m_fileDialogOpen = false;
        auto scheduler = CCDirector::sharedDirector()->getScheduler();
        scheduler->unscheduleSelector(
            schedule_selector(PaimonPauseLayer::performCaptureAndRestore), this
        );
        removeLoadingOverlay();
        PauseLayer::onExit();
    }

    void restorePauseMenu(float dt) {
        this->setVisible(true);
        log::info("[PauseLayer] Pause menu restored");
    }

    void processSelectedFile(std::filesystem::path selectedPath, int levelID) {
        log::info("[PauseLayer] Selected file: {}", geode::utils::string::pathToString(selectedPath));

        // decide formato por extension
        std::string ext = geode::utils::string::pathToString(selectedPath.extension());
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext == ".gif") {
            // previsualiza gif y permite subir
            std::ifstream gifFile(selectedPath, std::ios::binary | std::ios::ate);
                if (!gifFile) {
                    log::error("[PauseLayer] Could not open GIF file");
                    PaimonNotify::create(Localization::get().getString("pause.gif_open_error").c_str(), NotificationIcon::Error)->show();
                    return;
                }
                size_t size = static_cast<size_t>(gifFile.tellg());
                gifFile.seekg(0, std::ios::beg);
                std::vector<uint8_t> gifData(size);
                gifFile.read(reinterpret_cast<char*>(gifData.data()), size);
                gifFile.close();

                // usa ccimage desde memoria
                CCImage* image = new CCImage();
                bool loaded = image->initWithImageData(
                    const_cast<void*>(static_cast<const void*>(gifData.data())),
                    gifData.size()
                );

                if (!loaded) {
                    image->release();
                    PaimonNotify::create(Localization::get().getString("pause.gif_read_error").c_str(), NotificationIcon::Error)->show();
                    return;
                }

                int width = image->getWidth();
                int height = image->getHeight();

                // crea textura desde ccimage
                CCTexture2D* texture = new CCTexture2D();
                bool ok = texture->initWithImage(image);
                image->release();

                if (!ok) {
                    texture->release();
                    PaimonNotify::create(Localization::get().getString("pause.gif_texture_error").c_str(), NotificationIcon::Error)->show();
                    return;
                }
                texture->setAntiAliasTexParameters();
                // autorelease balancea el new; CapturePreviewPopup::m_texture (Ref<>) retiene.
                texture->autorelease();

                // obtiene pixeles con ccrendertexture
                auto renderTex = CCRenderTexture::create(width, height, kCCTexture2DPixelFormat_RGBA8888);
                if (!renderTex) {
                    PaimonNotify::create("Failed to create render texture", NotificationIcon::Error)->show();
                    return;
                }

                renderTex->begin();
                auto sprite = CCSprite::createWithTexture(texture);
                sprite->setPosition(ccp(width/2, height/2));
                sprite->visit();
                renderTex->end();

                // lee datos rgba
                auto renderedImage = renderTex->newCCImage(false);
                if (!renderedImage) {
                    PaimonNotify::create("Failed to read rendered image", NotificationIcon::Error)->show();
                    return;
                }

                auto imageData = renderedImage->getData();
                size_t rgbaSize = static_cast<size_t>(width) * height * 4;
                std::shared_ptr<uint8_t> rgbaData(new uint8_t[rgbaSize], std::default_delete<uint8_t[]>());

                // copia rgba directamente (el popup necesita 4bpp para crop/borders)
                std::memcpy(rgbaData.get(), imageData, rgbaSize);

                renderedImage->release();

                // muestra preview y sube gif si acepta
                auto popup = CapturePreviewPopup::create(
                    texture,
                    levelID,
                    rgbaData,
                    width,
                    height,
                    [levelID, gifData = std::move(gifData)](bool accepted, int lvlID, std::shared_ptr<uint8_t> buf, int w, int h, std::string mode, std::string replaceId) mutable {
                        if (!accepted) {
                            log::info("[PauseLayer] User cancelled GIF preview");
                            return;
                        }

                        // extrae colores dominantes primer frame (convierte rgba a rgb)
                        std::vector<uint8_t> rgbBuf(w * h * 3);
                        for (int i = 0; i < w * h; ++i) {
                            rgbBuf[i*3 + 0] = buf.get()[i*4 + 0];
                            rgbBuf[i*3 + 1] = buf.get()[i*4 + 1];
                            rgbBuf[i*3 + 2] = buf.get()[i*4 + 2];
                        }
                        auto pair = DominantColors::extract(rgbBuf.data(), w, h);
                        ccColor3B A{pair.first.r, pair.first.g, pair.first.b};
                        ccColor3B B{pair.second.r, pair.second.g, pair.second.b};
                        LevelColors::get().set(lvlID, A, B);

                        LocalThumbs::get().saveRGB(lvlID, rgbBuf.data(), w, h);
                        ThumbsRegistry::get().mark(ThumbKind::Level, lvlID, false);

                        // obtiene usuario y verifica mod
                        std::string username;
                        int accountID = 0;
                        if (auto* gm = GameManager::get()) {
                            username = gm->m_playerName;
                            if (auto* am = GJAccountManager::get()) {
                                accountID = am->m_accountID;
                            }
                        }
                        if (username.empty()) {
                            PaimonNotify::create(Localization::get().getString("profile.username_error").c_str(), NotificationIcon::Error)->show();
                            return;
                        }
                        if (accountID <= 0) {
                            PaimonNotify::create(Localization::get().getString("level.account_required").c_str(), NotificationIcon::Error)->show();
                            return;
                        }

                        PaimonNotify::create(Localization::get().getString("capture.verifying").c_str(), NotificationIcon::Info)->show();
                        ThumbnailAPI::get().checkModeratorAccount(username, accountID, [lvlID, username, gifData = std::move(gifData)](bool approved, bool isAdmin) mutable {
                            bool allowModeratorFlow = approved || isAdmin;
                            if (allowModeratorFlow) {
                                PaimonNotify::create(Localization::get().getString("pause.gif_uploading").c_str(), NotificationIcon::Loading)->show();
                                ThumbnailAPI::get().uploadGIF(lvlID, gifData, username, [lvlID](bool ok, std::string const& msg){
                                    if (ok) {
                                        PendingQueue::get().removeForLevel(lvlID);
                                        PaimonNotify::create(Localization::get().getString("pause.gif_uploaded").c_str(), NotificationIcon::Success)->show();
                                    } else {
                                        PaimonNotify::create(Localization::get().getString("pause.gif_upload_error").c_str(), NotificationIcon::Error)->show();
                                    }
                                });
                            } else {
                                ThumbnailAPI::get().checkExists(lvlID, [lvlID, username](bool exists){
                                    auto cat = exists ? PendingCategory::Update : PendingCategory::Verify;
                                    // no puede verificar creador (nivel destruido)
                                    bool isCreator = false;
                                    PendingQueue::get().addOrBump(lvlID, cat, username, {}, isCreator);
                                    PaimonNotify::create(Localization::get().getString("capture.suggested").c_str(), NotificationIcon::Info)->show();
                                });
                            }
                        });
                    }
                );

                if (popup) {
                    popup->show();
                } else {
                    log::error("[PauseLayer] Failed to create GIF preview popup");
                    texture->release();
                }

            return; // detiene flujo png
        }

        // lee png completo en memoria
        std::ifstream pngFile(selectedPath, std::ios::binary | std::ios::ate);
        if (!pngFile) {
            log::error("[PauseLayer] Could not open PNG file");
            PaimonNotify::create(Localization::get().getString("pause.file_open_error").c_str(), NotificationIcon::Error)->show();
            return;
        }

        size_t fileSize = (size_t)pngFile.tellg();
        pngFile.seekg(0, std::ios::beg);
        std::vector<uint8_t> pngData(fileSize);
        pngFile.read(reinterpret_cast<char*>(pngData.data()), fileSize);
        pngFile.close();

        log::info("[PauseLayer] PNG file read ({} bytes)", fileSize);

        // carga imagen en ccimage (auto-detecta formato: png, jpg, webp, bmp, etc.)
        CCImage* img = new CCImage();
        if (!img->initWithImageData(pngData.data(), fileSize)) {
            log::error("[PauseLayer] Failed to decode selected image file");
            PaimonNotify::create(Localization::get().getString("pause.png_invalid").c_str(), NotificationIcon::Error)->show();
            img->release();
            return;
        }

        int width = img->getWidth();
        int height = img->getHeight();
        unsigned char* imgData = img->getData();

        if (!imgData) {
            log::error("[PauseLayer] Failed to get image pixel data");
            PaimonNotify::create(Localization::get().getString("pause.process_image_error").c_str(), NotificationIcon::Error)->show();
            img->release();
            return;
        }

        // lee datos de imagen
        int bpp = img->getBitsPerComponent();
        bool hasAlpha = img->hasAlpha();

        log::info("[PauseLayer] Image loaded {}x{} (BPP: {}, Alpha: {})",
                  width, height, bpp, hasAlpha);

        // calcula tamano esperado
        int bytesPerPixel = hasAlpha ? 4 : 3;
        size_t expectedDataSize = static_cast<size_t>(width) * height * bytesPerPixel;

        // convierte si es necesario
        size_t rgbaSize = static_cast<size_t>(width) * height * 4;
        std::vector<uint8_t> rgbaPixels(rgbaSize);

        if (hasAlpha) {
            memcpy(rgbaPixels.data(), imgData, std::min(rgbaSize, expectedDataSize));
            log::info("[PauseLayer] Alpha detected; copied {} bytes", expectedDataSize);
        } else {
            log::info("[PauseLayer] RGB detected; converting to RGBA ({} -> {} bytes)",
                      expectedDataSize, rgbaSize);
            for (size_t i = 0; i < static_cast<size_t>(width) * height; ++i) {
                rgbaPixels[i*4 + 0] = imgData[i*3 + 0]; // R
                rgbaPixels[i*4 + 1] = imgData[i*3 + 1]; // G
                rgbaPixels[i*4 + 2] = imgData[i*3 + 2]; // B
                rgbaPixels[i*4 + 3] = 255;              // opacidad maxima
            }
        }

        img->release();

        log::debug("[PauseLayer] RGBA data ready ({} bytes)", rgbaSize);

        // crea textura como en captura
        CCTexture2D* texture = new CCTexture2D();
        if (!texture) {
            log::error("[PauseLayer] Failed to create CCTexture2D");
            PaimonNotify::create(Localization::get().getString("pause.create_texture_error").c_str(), NotificationIcon::Error)->show();
            return;
        }

        // inicia textura con datos
        if (!texture->initWithData(
            rgbaPixels.data(),
            kCCTexture2DPixelFormat_RGBA8888,
            width,
            height,
            CCSize(width, height)
        )) {
            log::error("[PauseLayer] Failed to initialize texture from data");
            texture->release();
            PaimonNotify::create(Localization::get().getString("pause.init_texture_error").c_str(), NotificationIcon::Error)->show();
            return;
        }

        // mejores parametros
        texture->setAntiAliasTexParameters();

        // new CCTexture2D() ya da refcount=1. NO retener de nuevo.
        // El popup hara retain al recibirla, y release al cerrar.
        // Si el popup no se crea, hacemos release abajo.

        log::info("[PauseLayer] Texture created successfully using FramebufferCapture method");

        // envoltorio para datos
        std::shared_ptr<uint8_t> rgbaData(new uint8_t[rgbaSize], std::default_delete<uint8_t[]>());
        std::memcpy(rgbaData.get(), rgbaPixels.data(), rgbaSize);

        log::info("[PauseLayer] Showing preview with RGBA data");

        // muestra popup
        auto popup = CapturePreviewPopup::create(
            texture,
            levelID,
            rgbaData,
            width,
            height,
            [levelID](bool accepted, int lvlID, std::shared_ptr<uint8_t> buf, int w, int h, std::string mode, std::string replaceId) {
                if (accepted) {
                    log::info("[PauseLayer] User accepted image loaded from disk");

                    // convierte rgba a rgb
                    std::vector<uint8_t> rgbBuf(w * h * 3);
                    for (int i = 0; i < w * h; i++) {
                        rgbBuf[i*3 + 0] = buf.get()[i*4 + 0];
                        rgbBuf[i*3 + 1] = buf.get()[i*4 + 1];
                        rgbBuf[i*3 + 2] = buf.get()[i*4 + 2];
                    }

                    // extrae colores dominantes
                    auto pair = DominantColors::extract(rgbBuf.data(), w, h);
                    ccColor3B A{pair.first.r, pair.first.g, pair.first.b};
                    ccColor3B B{pair.second.r, pair.second.g, pair.second.b};

                    LevelColors::get().set(lvlID, A, B);

                    log::info("[PauseLayer] Saving locally");

                    // guarda en rgb
                    LocalThumbs::get().saveRGB(lvlID, rgbBuf.data(), w, h);
                    ThumbsRegistry::get().mark(ThumbKind::Level, lvlID, false);

                    // convierte a png en memoria (sin saveToFile = Unicode-safe)
                    if (buf) {
                        std::vector<uint8_t> pngData;
                        if (!ImageConverter::rgbaToPngBuffer(buf.get(), w, h, pngData)) {
                            log::error("[PauseLayer] Failed to encode PNG in memory");
                            PaimonNotify::create(Localization::get().getString("capture.save_png_error").c_str(), NotificationIcon::Error)->show();
                        } else {
                                // guardar copia PNG en cache de ThumbnailLoader para que
                                // LevelInfoLayer pueda encontrar el thumbnail sin depender del server
                                {
                                    auto cachePath = ThumbnailLoader::get().getCachePath(lvlID);
                                    std::error_code cacheEc;
                                    std::filesystem::create_directories(cachePath.parent_path(), cacheEc);
                                    std::ofstream cacheOut(cachePath, std::ios::binary | std::ios::trunc);
                                    if (cacheOut) {
                                        cacheOut.write(reinterpret_cast<char const*>(pngData.data()), pngData.size());
                                        log::info("[PauseLayer] PNG guardado en cache de ThumbnailLoader: {}", geode::utils::string::pathToString(cachePath));
                                    }
                                }

                                std::string username;
                                int accountID = 0;
                                if (auto gm = GameManager::get()) {
                                    username = gm->m_playerName;
                                    if (auto* am = GJAccountManager::get()) {
                                        accountID = am->m_accountID;
                                    }
                                }

                                if (!username.empty() && accountID > 0) {
                                    PaimonNotify::create(Localization::get().getString("capture.verifying").c_str(), NotificationIcon::Info)->show();

                                    ThumbnailAPI::get().checkModeratorAccount(username, accountID, [lvlID, pngData, username](bool isMod, bool isAdmin) {
                                        if (isMod || isAdmin) {
                                            PaimonNotify::create(Localization::get().getString("capture.uploading").c_str(), NotificationIcon::Info)->show();
                                            ThumbnailAPI::get().uploadThumbnail(lvlID, pngData, username, [lvlID](bool s, std::string const& msg){
                                                if (s) {
                                                    PendingQueue::get().removeForLevel(lvlID);
                                                    PaimonNotify::create(Localization::get().getString("capture.upload_success").c_str(), NotificationIcon::Success)->show();
                                                } else {
                                                    PaimonNotify::create(Localization::get().getString("capture.upload_error").c_str(), NotificationIcon::Error)->show();
                                                }
                                            });
                                        } else {
                                            PaimonNotify::create(Localization::get().getString("capture.uploading_suggestion").c_str(), NotificationIcon::Info)->show();
                                            ThumbnailAPI::get().uploadSuggestion(lvlID, pngData, username, [lvlID, username](bool s, std::string const& msg){
                                                if (s) {
                                                    ThumbnailAPI::get().checkExists(lvlID, [lvlID, username](bool exists) {
                                                        auto cat = exists ? PendingCategory::Update : PendingCategory::Verify;
                                                        PendingQueue::get().addOrBump(lvlID, cat, username, {}, false);
                                                        PaimonNotify::create(Localization::get().getString("capture.suggested").c_str(), NotificationIcon::Success)->show();
                                                    });
                                                } else {
                                                    PaimonNotify::create(Localization::get().getString("capture.upload_error").c_str(), NotificationIcon::Error)->show();
                                                }
                                            });
                                        }
                                    });
                                } else {
                                    PaimonNotify::create(Localization::get().getString("level.account_required").c_str(), NotificationIcon::Error)->show();
                                }
                        }
                    }
                } else {
                    log::info("[PauseLayer] User cancelled image preview");
                }
            }
        );

        if (popup) {
            popup->show();
        } else {
            log::error("[PauseLayer] Failed to create preview popup");
            texture->release();
        }

    }

    void onSelectPNGFile(CCObject*) {
        log::info("[PauseLayer] Select file button pressed");

        if (m_fields->m_fileDialogOpen) {
            log::warn("[PauseLayer] File dialog already open, ignoring");
            return;
        }

        auto pl = PlayLayer::get();
            if (!pl || !pl->m_level) {
                log::error("[PauseLayer] PlayLayer or level not available");
                return;
            }

            int levelID = pl->m_level->m_levelID;

            m_fields->m_fileDialogOpen = true;
            WeakRef<PaimonPauseLayer> self = this;
            pt::openImageFileDialog([self, levelID](std::optional<std::filesystem::path> result) {
                auto layer = self.lock();
                if (!layer) return;
                layer->m_fields->m_fileDialogOpen = false;

                if (result.has_value()) {
                    auto path = result.value();
                    if (!path.empty()) {
                        layer->processSelectedFile(path, levelID);
                    } else {
                        log::warn("[PauseLayer] User cancelled file picker");
                    }
                }
            });
    }
};

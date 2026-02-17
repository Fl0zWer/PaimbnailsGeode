#include <Geode/modify/PauseLayer.hpp>
#include <Geode/binding/PlayLayer.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/utils/cocos.hpp>
#include <Geode/loader/Event.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>

#include "../managers/LocalThumbs.hpp"
#include "../layers/CapturePreviewPopup.hpp"
#include "../managers/ThumbsRegistry.hpp"
#include "../utils/FramebufferCapture.hpp"
#include "../utils/DominantColors.hpp"
#include "../managers/LevelColors.hpp"
#include "../utils/Localization.hpp"
#include "../managers/PendingQueue.hpp"
#include "../utils/Assets.hpp"
#include "../managers/ThumbnailAPI.hpp"
#include "../utils/ImageConverter.hpp"
#include "../utils/GIFDecoder.hpp"
#include "../utils/FileDialog.hpp"
#include <Geode/binding/LoadingCircle.hpp>

using namespace geode::prelude;
using namespace cocos2d;

// helper: verifica si el usuario es moderador
static bool isUserModerator() {
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
}

static CCSprite* tryCreateIcon() {
    // intenta cargar el asset empaquetado primero
    if (auto spr = CCSprite::create("paim_capturadora.png"_spr)) {
        float targetSize = 35.0f;
        float currentSize = std::max(spr->getContentSize().width, spr->getContentSize().height);
        
        if (currentSize > 0) {
            float scale = targetSize / currentSize;
            spr->setScale(scale);
        }
        spr->setRotation(-90.0f);  // rota 90 grados
        return spr;
    }
    // plan b: usa sprite frame
    auto frameSpr = CCSprite::createWithSpriteFrameName("GJ_checkOn_001.png");
    if (!frameSpr) frameSpr = CCSprite::createWithSpriteFrameName("GJ_button_01.png");
    if (frameSpr) {
        float targetSize = 35.0f;
        float currentSize = std::max(frameSpr->getContentSize().width, frameSpr->getContentSize().height);
        
        if (currentSize > 0) {
            float scale = targetSize / currentSize;
            frameSpr->setScale(scale);
        } else {
            frameSpr->setScale(1.0f); // reinicia escala por si acaso
        }
        frameSpr->setRotation(-90.0f);  // rota 90 grados
    }
    log::info("[PauseLayer] Select-file button added");
    return frameSpr;
}

class $modify(PaimonPauseLayer, PauseLayer) {
                // reconecta boton nativo para usar mismo handler
                // busca items con id de camara o captura
    void customSetup() {
        PauseLayer::customSetup();
        
        log::info("PauseLayer customSetup called");

        if (!Mod::get()->getSettingValue<bool>("enable-thumbnail-taking")) {
            log::info("Thumbnail taking disabled in settings");
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
            log::info("Level ID is {} (not saving thumbnails for this level)", playLayer->m_level->m_levelID.value());
            return;
        }

        auto rightMenu = this->getChildByID("right-button-menu");
        if (!rightMenu) {
            log::error("Right button menu not found in PauseLayer");
            return;
        }

        try {
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
            
            // guarda posicion para apilar botones
            CCPoint capturePos = btn->getPosition();
            
            // añade boton de subir (solo moderador)
            if (isUserModerator()) {
                int levelID = playLayer->m_level->m_levelID;
                
                // solo si hay miniatura local
                if (LocalThumbs::get().has(levelID)) {
                    log::info("[PauseLayer] User is moderator and local thumbnail exists; adding upload button");
                    
                    // sprite del boton: recurso o plan b
                    auto uploadSpr = CCSprite::create("paim_Subida.png"_spr);
                    
                    if (!uploadSpr) {
                        uploadSpr = Assets::loadButtonSprite(
                            "pause-upload",
                            "frame:GJ_downloadBtn_001.png",
                            []() {
                                if (auto spr = CCSprite::createWithSpriteFrameName("GJ_downloadBtn_001.png")) return spr;
                                return CCSprite::createWithSpriteFrameName("GJ_checkOn_001.png");
                            }
                        );
                    }
                    if (uploadSpr) {
                        float targetSize = 30.0f;
                        float currentSize = std::max(uploadSpr->getContentSize().width, uploadSpr->getContentSize().height);
                        
                        if (currentSize > 0) {
                            float scale = targetSize / currentSize;
                            uploadSpr->setScale(scale);
                        }
                        uploadSpr->setRotation(-90.0f); // rota 90 grados

                        auto uploadBtn = CCMenuItemSpriteExtra::create(
                            uploadSpr, 
                            this, 
                            menu_selector(PaimonPauseLayer::onUploadThumbnail)
                        );
                        if (uploadBtn) {
                            uploadBtn->setID("thumbnail-upload-button"_spr);
                            rightMenu->addChild(uploadBtn);
                            rightMenu->updateLayout();
                            
                            log::info("[PauseLayer] Upload button added");
                        }
                    }
                }
            }
            
            // añade boton para elegir archivo
            {
                CCSprite* selectSpr = nullptr;
                
                // intenta cargar paim_Subida.png primero
                selectSpr = CCSprite::create("paim_Subida.png"_spr);
                
                if (!selectSpr) {
                    selectSpr = Assets::loadButtonSprite(
                        "pause-select-file",
                        "frame:GJ_folderBtn_001.png",
                        []() {
                            if (auto spr = CCSprite::createWithSpriteFrameName("GJ_folderBtn_001.png")) return spr;
                            return CCSprite::createWithSpriteFrameName("GJ_button_01.png");
                        }
                    );
                }

                if (selectSpr) {
                    float targetSize = 30.0f;
                    float currentSize = std::max(selectSpr->getContentSize().width, selectSpr->getContentSize().height);
                    
                    if (currentSize > 0) {
                        float scale = targetSize / currentSize;
                        selectSpr->setScale(scale);
                    }
                    selectSpr->setRotation(-90.0f);
                    
                    auto selectBtn = CCMenuItemSpriteExtra::create(
                        selectSpr,
                        this,
                        menu_selector(PaimonPauseLayer::onSelectPNGFile)
                    );
                    if (selectBtn) {
                        selectBtn->setID("thumbnail-select-button"_spr);
                        rightMenu->addChild(selectBtn);
                        rightMenu->updateLayout();
                        
                        log::info("[PauseLayer] Select-file button added");
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
                    std::string idL = id; for (auto& c : idL) c = (char)tolower(c);
                    bool looksLikeCamera = (!idL.empty() && (idL.find("camera") != std::string::npos || idL.find("screenshot") != std::string::npos));
                    if (auto* item = typeinfo_cast<CCMenuItemSpriteExtra*>(node)) {
                        // usa heuristica de nombre de clase
                        if (!looksLikeCamera) {
                            if (auto* normal = item->getNormalImage()) {
                                auto cls = std::string(typeid(*normal).name());
                                auto clsL = cls; for (auto& c : clsL) c = (char)tolower(c);
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
            rewireScreenshotInMenu(this->getChildByID("right-button-menu"));
            rewireScreenshotInMenu(this->getChildByID("left-button-menu"));

            // no llama updateLayout para mantener posiciones
            log::info("Thumbnail capture + extra buttons added successfully");
        } catch (std::exception& e) {
            log::error("Exception while adding thumbnail button: {}", e.what());
        } catch (...) {
            log::error("Unknown exception while adding thumbnail button");
        }
    }

    void onScreenshot(CCObject*) {
        log::info("[PauseLayer] Capture button pressed; hiding pause menu");
        
        auto pl = PlayLayer::get();
        if (!pl) {
            log::error("[PauseLayer] PlayLayer not available");
            Notification::create(Localization::get().getString("pause.playlayer_error").c_str(), NotificationIcon::Error)->show();
            return;
        }
        
        // oculta menu de pausa temporalmente
        this->setVisible(false);
        
        // muestra circulo de carga inmediatamente
        showLoadingOverlay();

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

        auto winSize = CCDirector::sharedDirector()->getWinSize();

        auto overlay = CCLayerColor::create({0, 0, 0, 100});
        overlay->setTag(9999);
        scene->addChild(overlay, 10000);

        auto spinner = CCSprite::create("loadingCircle.png");
        if (!spinner) spinner = CCSprite::createWithSpriteFrameName("loadingCircle.png");
        if (spinner) {
            spinner->setPosition(winSize / 2);
            spinner->setScale(1.0f);
            spinner->setBlendFunc({GL_SRC_ALPHA, GL_ONE});
            overlay->addChild(spinner, 1);
        }

        // rota via scheduler (ccactions no corren durante pausa de gd)
        CCDirector::sharedDirector()->getScheduler()->scheduleSelector(
            schedule_selector(PaimonPauseLayer::updateSpinner),
            this, 0.0f, false
        );
    }

    void updateSpinner(float dt) {
        auto scene = CCDirector::sharedDirector()->getRunningScene();
        if (!scene) return;
        auto overlay = scene->getChildByTag(9999);
        if (!overlay) {
            CCDirector::sharedDirector()->getScheduler()->unscheduleSelector(
                schedule_selector(PaimonPauseLayer::updateSpinner), this
            );
            return;
        }
        auto children = overlay->getChildren();
        if (children && children->count() > 0) {
            auto spinner = static_cast<CCNode*>(children->objectAtIndex(0));
            if (spinner) spinner->setRotation(spinner->getRotation() + dt * 360.0f);
        }
    }

    void reShowOverlay(float dt) {
        auto scene = CCDirector::sharedDirector()->getRunningScene();
        if (!scene) return;
        auto overlay = scene->getChildByTag(9999);
        if (overlay) overlay->setVisible(true);
    }

    void removeLoadingOverlay() {
        auto scheduler = CCDirector::sharedDirector()->getScheduler();
        scheduler->unscheduleSelector(
            schedule_selector(PaimonPauseLayer::updateSpinner), this
        );
        scheduler->unscheduleSelector(
            schedule_selector(PaimonPauseLayer::reShowOverlay), this
        );

        auto scene = CCDirector::sharedDirector()->getRunningScene();
        if (!scene) return;
        auto overlay = scene->getChildByTag(9999);
        if (overlay) overlay->removeFromParentAndCleanup(true);
    }
    
    void performCaptureAndRestore(float dt) {
        try {
            log::info("[PauseLayer] Performing capture");

            auto* pl = PlayLayer::get();
            if (!pl || !pl->m_level) {
                log::error("[PauseLayer] PlayLayer or level not available for capture");
                Notification::create(Localization::get().getString("pause.capture_error").c_str(), NotificationIcon::Error)->show();
                removeLoadingOverlay();
                this->setVisible(true);
                return;
            }

            int levelID = pl->m_level->m_levelID;

            // oculta overlay para captura limpia
            auto scene = CCDirector::sharedDirector()->getRunningScene();
            if (scene) {
                auto overlay = scene->getChildByTag(9999);
                if (overlay) overlay->setVisible(false);
            }

            // muestra overlay en siguiente frame
            CCDirector::sharedDirector()->getScheduler()->scheduleSelector(
                schedule_selector(PaimonPauseLayer::reShowOverlay),
                this, 0.0f, 0, 0.0f, false
            );

            // retiene objeto durante proceso asincrono
            this->retain();

            // usa framebufferCapture para pantalla
            FramebufferCapture::requestCapture(levelID, [this, levelID](bool success, CCTexture2D* texture, std::shared_ptr<uint8_t> rgbData, int width, int height) {
                Loader::get()->queueInMainThread([this, success, texture, rgbData, width, height, levelID]() {
                    removeLoadingOverlay();

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
                                    accountID = gm->m_playerUserID;
                                }

                                if (username.empty()) {
                                    log::warn("[PauseLayer] No username available");
                                    Notification::create(Localization::get().getString("profile.username_error").c_str(), NotificationIcon::Error)->show();
                                    return;
                                }

                                // convierte a png para subir
                                CCImage img;
                                if (!img.initWithImageData(buf.get(), w * h * 4, CCImage::kFmtRawData, w, h)) {
                                    log::error("[PauseLayer] Failed to create image for PNG");
                                    Notification::create(Localization::get().getString("capture.process_error").c_str(), NotificationIcon::Error)->show();
                                    return;
                                }

                                auto tmpDir = Mod::get()->getSaveDir() / "tmp";
                                std::error_code dirEc;
                                std::filesystem::create_directories(tmpDir, dirEc);
                                auto tempPath = tmpDir / (std::string("thumb_") + std::to_string(lvlID) + ".png");

                                if (!img.saveToFile(tempPath.string().c_str(), false)) {
                                    log::error("[PauseLayer] Failed to save temporary PNG");
                                    Notification::create(Localization::get().getString("capture.save_png_error").c_str(), NotificationIcon::Error)->show();
                                    return;
                                }

                                // lee datos png
                                std::ifstream pngFile(tempPath, std::ios::binary);
                                if (!pngFile) {
                                    log::error("[PauseLayer] Failed to open PNG file");
                                    return;
                                }
                                pngFile.seekg(0, std::ios::end);
                                size_t pngSize = (size_t)pngFile.tellg();
                                pngFile.seekg(0, std::ios::beg);
                                std::vector<uint8_t> pngData(pngSize);
                                pngFile.read(reinterpret_cast<char*>(pngData.data()), pngSize);
                                pngFile.close();
                                std::filesystem::remove(tempPath);

                                // verifica moderador y sube
                                Notification::create(Localization::get().getString("capture.verifying").c_str(), NotificationIcon::Info)->show();

                                ThumbnailAPI::get().checkModeratorAccount(username, accountID, [lvlID, pngData, username](bool isMod, bool isAdmin) {
                                    if (isMod || isAdmin) {
                                        // mods/admins suben directamente
                                        log::info("[PauseLayer] User is moderator, uploading directly");
                                        Notification::create(Localization::get().getString("capture.uploading").c_str(), NotificationIcon::Info)->show();

                                        ThumbnailAPI::get().uploadThumbnail(lvlID, pngData, username, [lvlID](bool success, const std::string& msg) {
                                            if (success) {
                                                PendingQueue::get().removeForLevel(lvlID);
                                                Notification::create(Localization::get().getString("capture.upload_success").c_str(), NotificationIcon::Success)->show();
                                                log::info("[PauseLayer] Upload successful for level {}", lvlID);
                                            } else {
                                                Notification::create(Localization::get().getString("capture.upload_error").c_str(), NotificationIcon::Error)->show();
                                                log::error("[PauseLayer] Upload failed: {}", msg);
                                            }
                                        });
                                    } else {
                                        // usuarios normales suben sugerencia
                                        log::info("[PauseLayer] User is not moderator, uploading as suggestion");
                                        Notification::create(Localization::get().getString("capture.uploading_suggestion").c_str(), NotificationIcon::Info)->show();

                                        ThumbnailAPI::get().uploadSuggestion(lvlID, pngData, username, [lvlID, username](bool success, const std::string& msg) {
                                            if (success) {
                                                ThumbnailAPI::get().checkExists(lvlID, [lvlID, username](bool exists) {
                                                    auto cat = exists ? PendingCategory::Update : PendingCategory::Verify;
                                                    PendingQueue::get().addOrBump(lvlID, cat, username, {}, false);
                                                    Notification::create(Localization::get().getString("capture.suggested").c_str(), NotificationIcon::Success)->show();
                                                });
                                            } else {
                                                Notification::create(Localization::get().getString("capture.upload_error").c_str(), NotificationIcon::Error)->show();
                                                log::error("[PauseLayer] Suggestion upload failed: {}", msg);
                                            }
                                        });
                                    }
                                });
                            },
                            // Recapture callback
                            nullptr,
                            false, // isPlayerHidden
                            isUserModerator() // isModerator
                        );

                        if (popup) {
                            popup->show();
                        }
                    } else {
                        log::error("[PauseLayer] Capture failed");
                        Notification::create(Localization::get().getString("pause.capture_error").c_str(), NotificationIcon::Error)->show();
                    }

                    // restaura menu de pausa
                    this->setVisible(true);
                    this->release();
                    log::info("[PauseLayer] Pause menu restored after capture");
                });
            });

        } catch (std::exception const& e) {
            log::error("[PauseLayer] Failed to perform capture: {}", e.what());
            Notification::create(Localization::get().getString("pause.capture_error").c_str(), NotificationIcon::Error)->show();
            removeLoadingOverlay();
            this->setVisible(true);
        }
    }
    
    void restorePauseMenu(float dt) {
        this->setVisible(true);
        log::info("[PauseLayer] Pause menu restored");
    }

    void onUploadThumbnail(CCObject*) {
        log::info("[PauseLayer] Upload button pressed");
        
        try {
            auto pl = PlayLayer::get();
            if (!pl || !pl->m_level) {
                log::error("[PauseLayer] PlayLayer or level not available");
                return;
            }
            
            int levelID = pl->m_level->m_levelID;
            
            // comprueba existencia miniatura local
            if (!LocalThumbs::get().has(levelID)) {
                log::warn("[PauseLayer] No local thumbnail for level {}", levelID);
                Notification::create(Localization::get().getString("pause.no_local_thumb").c_str(), NotificationIcon::Warning)->show();
                return;
            }
            
            // verifica moderador con servidor
            std::string username;
            try {
                auto* gm = GameManager::get();
                if (gm) {
                    username = gm->m_playerName;
                    log::info("[PauseLayer] Username: '{}'", username);
                } else {
                    log::warn("[PauseLayer] GameManager::get() is null");
                }
            } catch(...) {
                log::error("[PauseLayer] Exception accessing GameManager");
            }
            
            if (username.empty()) {
                log::error("[PauseLayer] Username is empty; cannot verify moderator status");
                Notification::create(Localization::get().getString("profile.username_error").c_str(), NotificationIcon::Error)->show();
                return;
            }
            
            // no captura puntero nivel (inseguro async)
            Notification::create(Localization::get().getString("capture.verifying").c_str(), NotificationIcon::Info)->show();

            // nota: validacion accountID removida
            ThumbnailAPI::get().checkModeratorAccount(username, 0, [levelID, username](bool approved, bool isAdmin) {
                bool allowModeratorFlow = approved;
                if (allowModeratorFlow) {
                    log::info("[PauseLayer] User verified as moderator; uploading level {}", levelID);
                    auto thumbPathOpt = LocalThumbs::get().getThumbPath(levelID);
                    if (!thumbPathOpt.has_value()) {
                        log::error("[PauseLayer] Could not get local thumbnail path");
                        Notification::create(Localization::get().getString("pause.access_error").c_str(), NotificationIcon::Error)->show();
                        return;
                    }
                    
                    // carga rgb y convierte a png
                    std::vector<uint8_t> pngData;
                    if (!ImageConverter::loadRgbFileToPng(*thumbPathOpt, pngData)) {
                        log::error("[PauseLayer] Failed to convert thumbnail to PNG");
                        Notification::create(Localization::get().getString("pause.process_thumbnail_error").c_str(), NotificationIcon::Error)->show();
                        return;
                    }
                    
                    if (allowModeratorFlow) {
                        log::info("[PauseLayer] Uploading thumbnail ({} bytes) for level {}", pngData.size(), levelID);
                        Notification::create(Localization::get().getString("capture.uploading").c_str(), NotificationIcon::Info)->show();
                        
                        ThumbnailAPI::get().uploadThumbnail(levelID, pngData, username,
                            [levelID](bool success, const std::string& message) {
                                if (success) {
                                    Notification::create(Localization::get().getString("capture.upload_success").c_str(), NotificationIcon::Success)->show();
                                    PendingQueue::get().removeForLevel(levelID);
                                    log::info("[PauseLayer] Upload successful for level {}", levelID);
                                } else {
                                    Notification::create(Localization::get().getString("capture.upload_error").c_str(), NotificationIcon::Error)->show();
                                    log::error("[PauseLayer] Upload failed for level {}: {}", levelID, message);
                                }
                            }
                        );
                    } else {
                        log::info("[PauseLayer] User is not moderator; uploading suggestion and enqueueing");
                        Notification::create(Localization::get().getString("capture.uploading_suggestion").c_str(), NotificationIcon::Info)->show();
                        
                        ThumbnailAPI::get().uploadSuggestion(levelID, pngData, username, [levelID, username](bool success, const std::string& msg) {
                            if (success) {
                                log::info("[PauseLayer] Suggestion uploaded successfully");
                                ThumbnailAPI::get().checkExists(levelID, [levelID, username](bool exists) {
                                    auto cat = exists ? PendingCategory::Update : PendingCategory::Verify;
                                    // no puede verificar creador seguro
                                    bool isCreator = false;
                                    PendingQueue::get().addOrBump(levelID, cat, username, {}, isCreator);
                                    Notification::create(Localization::get().getString("capture.suggested").c_str(), NotificationIcon::Success)->show();
                                });
                            } else {
                                log::error("[PauseLayer] Failed to upload suggestion: {}", msg);
                                Notification::create(Localization::get().getString("capture.upload_error").c_str(), NotificationIcon::Error)->show();
                            }
                        });
                    }
                }
            });
            
        } catch (std::exception const& e) {
            log::error("[PauseLayer] Exception in onUploadThumbnail: {}", e.what());
            Notification::create((Localization::get().getString("level.error_prefix") + e.what()).c_str(), NotificationIcon::Error)->show();
        }
    }
    

    
    void processSelectedFile(std::filesystem::path selectedPath, int levelID) {
        log::info("[PauseLayer] Selected file: {}", selectedPath.string());
        
        // decide formato por extension
        std::string ext = selectedPath.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext == ".gif") {
#if !defined(GEODE_IS_WINDOWS) && !defined(_WIN32)
            Notification::create(Localization::get().getString("pause.gif_not_supported").c_str(), NotificationIcon::Warning)->show();
            return;
#else
            // previsualiza gif y permite subir
            try {
                std::ifstream gifFile(selectedPath, std::ios::binary | std::ios::ate);
                if (!gifFile) {
                    log::error("[PauseLayer] Could not open GIF file");
                    Notification::create(Localization::get().getString("pause.gif_open_error").c_str(), NotificationIcon::Error)->show();
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
                    Notification::create(Localization::get().getString("pause.gif_read_error").c_str(), NotificationIcon::Error)->show();
                    return;
                }
                
                int width = image->getWidth();
                int height = image->getHeight();
                
                // crea textura desde ccimage
                CCTexture2D* texture = new CCTexture2D();
                bool ok = texture->initWithImage(image);
                image->release();
                
                if (!ok) {
                    delete texture;
                    Notification::create(Localization::get().getString("pause.gif_texture_error").c_str(), NotificationIcon::Error)->show();
                    return;
                }
                texture->setAntiAliasTexParameters();
                texture->retain();
                
                // obtiene pixeles con ccrendertexture
                auto renderTex = CCRenderTexture::create(width, height, kCCTexture2DPixelFormat_RGBA8888);
                if (!renderTex) {
                    texture->release();
                    Notification::create("Failed to create render texture", NotificationIcon::Error)->show();
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
                    texture->release();
                    Notification::create("Failed to read rendered image", NotificationIcon::Error)->show();
                    return;
                }
                
                auto imageData = renderedImage->getData();
                size_t rgbSize = static_cast<size_t>(width) * height * 3;
                std::shared_ptr<uint8_t> rgbData(new uint8_t[rgbSize], std::default_delete<uint8_t[]>());
                
                // convierte rgba a rgb
                for (int i = 0; i < width * height; ++i) {
                    rgbData.get()[i*3 + 0] = imageData[i*4 + 0];
                    rgbData.get()[i*3 + 1] = imageData[i*4 + 1];
                    rgbData.get()[i*3 + 2] = imageData[i*4 + 2];
                }
                
                renderedImage->release();

                // muestra preview y sube gif si acepta
                auto popup = CapturePreviewPopup::create(
                    texture,
                    levelID,
                    rgbData,
                    width,
                    height,
                    [levelID, gifData = std::move(gifData)](bool accepted, int lvlID, std::shared_ptr<uint8_t> buf, int w, int h, std::string mode, std::string replaceId) mutable {
                        if (!accepted) {
                            log::info("[PauseLayer] User cancelled GIF preview");
                            return;
                        }

                        // extrae colores dominantes primer frame
                        auto pair = DominantColors::extract(buf.get(), w, h);
                        ccColor3B A{pair.first.r, pair.first.g, pair.first.b};
                        ccColor3B B{pair.second.r, pair.second.g, pair.second.b};
                        LevelColors::get().set(lvlID, A, B);

                        LocalThumbs::get().saveRGB(lvlID, buf.get(), w, h);
                        ThumbsRegistry::get().mark(ThumbKind::Level, lvlID, false);

                        // obtiene usuario y verifica mod
                        std::string username;
                        try {
                            if (auto* gm = GameManager::get()) username = gm->m_playerName;
                        } catch(...) {}
                        if (username.empty()) {
                            Notification::create(Localization::get().getString("profile.username_error").c_str(), NotificationIcon::Error)->show();
                            return;
                        }

                        Notification::create(Localization::get().getString("capture.verifying").c_str(), NotificationIcon::Info)->show();
                        // nota: validacion accountID removida
                        ThumbnailAPI::get().checkModeratorAccount(username, 0, [lvlID, username, gifData = std::move(gifData)](bool approved, bool isAdmin) mutable {
                            bool allowModeratorFlow = approved;
                            if (allowModeratorFlow) {
                                Notification::create(Localization::get().getString("pause.gif_uploading").c_str(), NotificationIcon::Loading)->show();
                                ThumbnailAPI::get().uploadGIF(lvlID, gifData, username, [lvlID](bool ok, const std::string& msg){
                                    if (ok) {
                                        PendingQueue::get().removeForLevel(lvlID);
                                        Notification::create(Localization::get().getString("pause.gif_uploaded").c_str(), NotificationIcon::Success)->show();
                                    } else {
                                        Notification::create(Localization::get().getString("pause.gif_upload_error").c_str(), NotificationIcon::Error)->show();
                                    }
                                });
                            } else {
                                ThumbnailAPI::get().checkExists(lvlID, [lvlID, username](bool exists){
                                    auto cat = exists ? PendingCategory::Update : PendingCategory::Verify;
                                    // no puede verificar creador (nivel destruido)
                                    bool isCreator = false;
                                    PendingQueue::get().addOrBump(lvlID, cat, username, {}, isCreator);
                                    Notification::create(Localization::get().getString("capture.suggested").c_str(), NotificationIcon::Info)->show();
                                });
                            }
                        });
                    }
                );

                if (popup) {
                    popup->show();
                } else {
                    log::error("[PauseLayer] Failed to create GIF preview popup");
                    delete texture;
                }

            } catch (std::exception const& e) {
                log::error("[PauseLayer] Error processing GIF: {}", e.what());
                Notification::create(Localization::get().getString("pause.gif_process_error").c_str(), NotificationIcon::Error)->show();
            }
#endif  // GEODE_IS_WINDOWS
            return; // detiene flujo png
        }
        
        // lee png completo en memoria
        std::ifstream pngFile(selectedPath, std::ios::binary | std::ios::ate);
        if (!pngFile) {
            log::error("[PauseLayer] Could not open PNG file");
            Notification::create(Localization::get().getString("pause.file_open_error").c_str(), NotificationIcon::Error)->show();
            return;
        }
        
        size_t fileSize = (size_t)pngFile.tellg();
        pngFile.seekg(0, std::ios::beg);
        std::vector<uint8_t> pngData(fileSize);
        pngFile.read(reinterpret_cast<char*>(pngData.data()), fileSize);
        pngFile.close();
        
        log::info("[PauseLayer] PNG file read ({} bytes)", fileSize);
        
        // carga png en ccimage
        CCImage* img = new CCImage();
        if (!img->initWithImageData(pngData.data(), fileSize, CCImage::kFmtPng)) {
            log::error("[PauseLayer] Failed to decode selected PNG file");
            Notification::create(Localization::get().getString("pause.png_invalid").c_str(), NotificationIcon::Error)->show();
            delete img;
            return;
        }
        
        int width = img->getWidth();
        int height = img->getHeight();
        unsigned char* imgData = img->getData();
        
        if (!imgData) {
            log::error("[PauseLayer] Failed to get image pixel data");
            Notification::create(Localization::get().getString("pause.process_image_error").c_str(), NotificationIcon::Error)->show();
            delete img;
            return;
        }
        
        // lee datos de imagen
        int bpp = img->getBitsPerComponent();
        bool hasAlpha = img->hasAlpha();
        
        log::info("[PauseLayer] Image loaded {}x{} (BPP: {}, Alpha: {})", 
                  width, height, bpp, hasAlpha);
        
        // calcula tamaño esperado
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
        
        delete img;
        
        log::debug("[PauseLayer] RGBA data ready ({} bytes)", rgbaSize);
        
        // crea textura como en captura
        CCTexture2D* texture = new CCTexture2D();
        if (!texture) {
            log::error("[PauseLayer] Failed to create CCTexture2D");
            Notification::create(Localization::get().getString("pause.create_texture_error").c_str(), NotificationIcon::Error)->show();
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
            delete texture;
            Notification::create(Localization::get().getString("pause.init_texture_error").c_str(), NotificationIcon::Error)->show();
            return;
        }
        
        // mejores parametros
        texture->setAntiAliasTexParameters();
        
        // retiene textura
        texture->retain();
        
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

                    // guarda png para subir (requiere rgba)
                    CCImage img;
                    if (buf && !img.initWithImageData(buf.get(), w * h * 4, CCImage::kFmtRawData, w, h)) {
                        log::error("[PauseLayer] Failed to create image for PNG");
                        Notification::create(Localization::get().getString("capture.process_error").c_str(), NotificationIcon::Error)->show();
                    } else if (buf) {
                        auto tmpDir = Mod::get()->getSaveDir() / "tmp";
                        std::error_code dirEc;
                        std::filesystem::create_directories(tmpDir, dirEc);
                        auto tempPath = tmpDir / (std::string("thumb_") + std::to_string(lvlID) + ".png");
                        if (!img.saveToFile(tempPath.string().c_str(), false)) {
                            log::error("[PauseLayer] Failed to save temporary PNG");
                            Notification::create(Localization::get().getString("capture.save_png_error").c_str(), NotificationIcon::Error)->show();
                        } else {
                            std::ifstream pngFile(tempPath, std::ios::binary);
                            if (pngFile) {
                                pngFile.seekg(0, std::ios::end);
                                size_t pngSize = (size_t)pngFile.tellg();
                                pngFile.seekg(0, std::ios::beg);
                                std::vector<uint8_t> pngData(pngSize);
                                pngFile.read(reinterpret_cast<char*>(pngData.data()), pngSize);
                                pngFile.close();
                                std::filesystem::remove(tempPath);

                                std::string username;
                                int accountID = 0;
                                if (auto gm = GameManager::get()) {
                                    username = gm->m_playerName;
                                    accountID = gm->m_playerUserID;
                                }

                                if (!username.empty() && accountID > 0) {
                                    ThumbnailAPI::get().checkModeratorAccount(username, accountID, [lvlID, pngData, username](bool isMod, bool isAdmin) {
                                        if (isMod || isAdmin) {
                                            ThumbnailAPI::get().uploadThumbnail(lvlID, pngData, username, [](bool s, std::string){});
                                        } else {
                                            ThumbnailAPI::get().uploadSuggestion(lvlID, pngData, username, [](bool, std::string){});
                                        }
                                    });
                                }
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
            delete texture;
        }

    }

    void onSelectPNGFile(CCObject*) {
        log::info("[PauseLayer] Select file button pressed");
        
        try {
            auto pl = PlayLayer::get();
            if (!pl || !pl->m_level) {
                log::error("[PauseLayer] PlayLayer or level not available");
                return;
            }
            
            int levelID = pl->m_level->m_levelID;
            
            auto result = pt::openImageFileDialog();

            if (result.has_value()) {
                auto path = result.value();
                if (!path.empty()) {
                    this->processSelectedFile(path, levelID);
                } else {
                    log::warn("[PauseLayer] User cancelled file picker");
                }
            }

        } catch (std::exception const& e) {
            log::error("[PauseLayer] Exception in onSelectPNGFile: {}", e.what());
            Notification::create(Localization::get().getString("level.error_prefix") + std::string(e.what()), NotificationIcon::Error)->show();
        }
    }
};

#include "ProfileMusicPopup.hpp"
#include "../utils/PaimonNotification.hpp"
#include "../utils/Localization.hpp"
#include <Geode/binding/FLAlertLayer.hpp>
#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/GJAccountManager.hpp>

using namespace geode::prelude;

ProfileMusicPopup* ProfileMusicPopup::create(int accountID) {
    auto ret = new ProfileMusicPopup();
    if (ret && ret->init(accountID)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool ProfileMusicPopup::init(int accountID) {
    if (!Popup::init(440.f, 300.f)) return false;

    m_accountID = accountID;

    this->setTitle("Profile Music");

    m_mainMenu = CCMenu::create();
    m_mainMenu->setID("main-menu"_spr);
    m_mainMenu->setPosition(CCPointZero);
    m_mainLayer->addChild(m_mainMenu);

    this->setTouchEnabled(true);
    this->setTouchMode(kCCTouchesOneByOne);
    this->setTouchPriority(-200);

    createSongIdInput();
    createWaveformDisplay();
    createControlButtons();

    // Cargar configuracion existente si la hay
    loadExistingConfig();

    return true;
}

void ProfileMusicPopup::createSongIdInput() {
    auto winSize = m_mainLayer->getContentSize();

    // Label "Song ID:"
    auto idLabel = CCLabelBMFont::create("Newgrounds Song ID:", "bigFont.fnt");
    idLabel->setScale(0.4f);
    idLabel->setAnchorPoint({0, 0.5f});
    idLabel->setPosition({20.f, winSize.height - 50.f});
    m_mainLayer->addChild(idLabel);

    // Input field usando TextInput de Geode (maneja touch correctamente)
    m_songIdInput = TextInput::create(120.f, "Enter ID...");
    m_songIdInput->setPosition({230.f, winSize.height - 50.f});
    m_songIdInput->setFilter("0123456789");
    m_songIdInput->setMaxCharCount(10);
    m_songIdInput->setID("song-id-input"_spr);
    m_mainLayer->addChild(m_songIdInput, 11);

    // Load button
    auto loadSpr = ButtonSprite::create("Load", 60, true, "bigFont.fnt", "GJ_button_01.png", 25.f, 0.6f);
    auto loadBtn = CCMenuItemSpriteExtra::create(loadSpr, this, menu_selector(ProfileMusicPopup::onLoadSong));
    loadBtn->setPosition({350.f, winSize.height - 50.f});
    m_mainMenu->addChild(loadBtn);

    // Song info label
    m_songInfoLabel = CCLabelBMFont::create("No song loaded", "goldFont.fnt");
    m_songInfoLabel->setScale(0.4f);
    m_songInfoLabel->setPosition({winSize.width / 2, winSize.height - 80.f});
    m_mainLayer->addChild(m_songInfoLabel);
}

void ProfileMusicPopup::createWaveformDisplay() {
    auto winSize = m_mainLayer->getContentSize();

    // Waveform container
    m_waveformWidth = 360.f;
    m_waveformHeight = 50.f; 
    m_waveformX = (winSize.width - m_waveformWidth) / 2;
    m_waveformY = winSize.height - 140.f;

    // Fondo estilo GD
    auto waveformBg = CCScale9Sprite::create("square02b_001.png", {0, 0, 80, 80});
    waveformBg->setContentSize({m_waveformWidth + 10.f, m_waveformHeight + 10.f});
    waveformBg->setColor({0, 0, 0});
    waveformBg->setOpacity(180);
    waveformBg->setPosition({winSize.width / 2, m_waveformY + m_waveformHeight / 2});
    m_mainLayer->addChild(waveformBg);

    // Waveform container
    m_waveformContainer = CCNode::create();
    m_waveformContainer->setPosition({m_waveformX, m_waveformY});
    m_waveformContainer->setContentSize({m_waveformWidth, m_waveformHeight});
    m_mainLayer->addChild(m_waveformContainer);

    // Selection overlay
    m_selectionOverlay = CCLayerColor::create({100, 255, 255, 30}); 
    m_selectionOverlay->setContentSize({m_waveformWidth, m_waveformHeight});
    m_selectionOverlay->setPosition({0, 0});
    m_selectionOverlay->setVisible(false); 
    m_waveformContainer->addChild(m_selectionOverlay, 1);

    // Start handle
    m_startHandle = CCSprite::createWithSpriteFrameName("edit_rightBtn_001.png");
    if (!m_startHandle) m_startHandle = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png");
    if (m_startHandle) {
        m_startHandle->setScale(0.7f);
        m_startHandle->setColor({0, 255, 100}); 
        m_startHandle->setPosition({0, m_waveformHeight / 2});
        m_startHandle->setVisible(false); 
        m_waveformContainer->addChild(m_startHandle, 3);
    }

    // End handle
    m_endHandle = CCSprite::createWithSpriteFrameName("edit_leftBtn_001.png");
    if (!m_endHandle) m_endHandle = CCSprite::createWithSpriteFrameName("GJ_arrow_02_001.png");
    if (m_endHandle) {
        m_endHandle->setScale(0.7f);
        m_endHandle->setColor({255, 50, 100}); 
        m_endHandle->setPosition({m_waveformWidth * 0.5f, m_waveformHeight / 2});
        m_endHandle->setVisible(false);
        m_waveformContainer->addChild(m_endHandle, 3);
    }

    // Selection label mas compacto
    m_selectionLabel = CCLabelBMFont::create("0:00 - 0:20", "bigFont.fnt");
    m_selectionLabel->setScale(0.38f);
    m_selectionLabel->setPosition({winSize.width / 2, m_waveformY - 18.f});
    m_mainLayer->addChild(m_selectionLabel);

    // Duration label
    m_durationLabel = CCLabelBMFont::create("Duration: --:--", "bigFont.fnt");
    m_durationLabel->setScale(0.32f);
    m_durationLabel->setPosition({winSize.width / 2, m_waveformY - 35.f});
    m_mainLayer->addChild(m_durationLabel);

    // Placeholder text
    auto placeholderLabel = CCLabelBMFont::create("Enter song ID and press Load", "chatFont.fnt");
    placeholderLabel->setScale(0.75f);
    placeholderLabel->setOpacity(160);
    placeholderLabel->setPosition({m_waveformWidth / 2, m_waveformHeight / 2});
    placeholderLabel->setTag(999);
    m_waveformContainer->addChild(placeholderLabel, 0);

    updateSelectionLabel();
}

void ProfileMusicPopup::createControlButtons() {
    auto winSize = m_mainLayer->getContentSize();

    // Primera fila de botones - controles de reproduccion (mas arriba)
    float row1Y = 80.f;

    // Play preview
    auto playSpr = CCSprite::createWithSpriteFrameName("GJ_playBtn2_001.png");
    playSpr->setScale(0.5f);
    auto playBtn = CCMenuItemSpriteExtra::create(playSpr, this, menu_selector(ProfileMusicPopup::onPlayPreview));
    playBtn->setPosition({winSize.width / 2 - 80.f, row1Y});
    m_mainMenu->addChild(playBtn);

    // Stop preview
    auto stopSpr = CCSprite::createWithSpriteFrameName("GJ_stopBtn_001.png");
    if (!stopSpr) stopSpr = CCSprite::createWithSpriteFrameName("GJ_deleteBtn_001.png");
    stopSpr->setScale(0.5f);
    auto stopBtn = CCMenuItemSpriteExtra::create(stopSpr, this, menu_selector(ProfileMusicPopup::onStopPreview));
    stopBtn->setPosition({winSize.width / 2 - 20.f, row1Y});
    m_mainMenu->addChild(stopBtn);

    // Download song button
    auto downloadSpr = CCSprite::createWithSpriteFrameName("GJ_downloadBtn_001.png");
    downloadSpr->setScale(0.55f);
    auto downloadBtn = CCMenuItemSpriteExtra::create(downloadSpr, this, menu_selector(ProfileMusicPopup::onDownloadSong));
    downloadBtn->setPosition({winSize.width / 2 + 40.f, row1Y});
    m_mainMenu->addChild(downloadBtn);

    // Segunda fila de botones - Save y Delete (mas abajo)
    float row2Y = 45.f;

    // Save button
    auto saveSpr = ButtonSprite::create("Save", 70, true, "bigFont.fnt", "GJ_button_01.png", 28.f, 0.65f);
    auto saveBtn = CCMenuItemSpriteExtra::create(saveSpr, this, menu_selector(ProfileMusicPopup::onSave));
    saveBtn->setPosition({winSize.width / 2 - 50.f, row2Y});
    m_mainMenu->addChild(saveBtn);

    // Delete button
    auto deleteSpr = ButtonSprite::create("Delete", 70, true, "bigFont.fnt", "GJ_button_06.png", 28.f, 0.65f);
    auto deleteBtn = CCMenuItemSpriteExtra::create(deleteSpr, this, menu_selector(ProfileMusicPopup::onDelete));
    deleteBtn->setPosition({winSize.width / 2 + 50.f, row2Y});
    m_mainMenu->addChild(deleteBtn);
}


void ProfileMusicPopup::onLoadSong(CCObject*) {
    std::string idStr = m_songIdInput->getString();
    if (idStr.empty()) {
        showError("Please enter a song ID");
        return;
    }

    auto parsed = geode::utils::numFromString<int>(idStr);
    if (!parsed.isOk()) {
        showError("Invalid song ID");
        return;
    }
    m_songID = parsed.unwrap();
    if (m_songID <= 0) {
        showError("Invalid song ID");
        return;
    }

    showLoading();

    // Obtener info de la cancion
    ProfileMusicManager::get().getSongInfo(m_songID, [this](bool success, std::string const& name, std::string const& artist, int durationMs) {
        if (!success) {
            hideLoading();
            showError("Could not load song info. Make sure the ID is valid.");
            return;
        }

        m_songName = name;
        m_artistName = artist;
        m_songDurationMs = durationMs;

        // Actualizar UI
        std::string infoText = fmt::format("{} - {}", m_artistName, m_songName);
        if (infoText.length() > 50) {
            infoText = infoText.substr(0, 47) + "...";
        }
        m_songInfoLabel->setString(infoText.c_str());

        int mins = m_songDurationMs / 60000;
        int secs = (m_songDurationMs % 60000) / 1000;
        m_durationLabel->setString(fmt::format("Duration: {}:{:02d}", mins, secs).c_str());

        // Ajustar seleccion si excede la duracion
        if (m_endMs > m_songDurationMs) {
            m_endMs = std::min(m_songDurationMs, MAX_FRAGMENT_MS);
            m_startMs = std::max(0, m_endMs - MAX_FRAGMENT_MS);
        }

        // Cargar waveform
        loadWaveform();
    });
}

void ProfileMusicPopup::loadWaveform() {
    // Primero descargar la cancion para preview
    ProfileMusicManager::get().downloadSongForPreview(m_songID, [this](bool success, std::string const& path) {
        if (!success || path.empty()) {
            hideLoading();
            showError("Could not download song");
            return;
        }

        // Guardar path para preview
        m_previewPath = path;

        // Ahora obtener el waveform
        ProfileMusicManager::get().getWaveformPeaks(m_songID, [this](bool success, std::vector<float> const& peaks, int durationMs) {
            hideLoading();

            if (!success) {
                showError("Could not analyze song");
                return;
            }

            m_peaks = peaks;

            // Set duration from waveform analysis
            if (durationMs > 0) {
                m_songDurationMs = durationMs;

                // Update duration label
                int mins = m_songDurationMs / 60000;
                int secs = (m_songDurationMs % 60000) / 1000;
                m_durationLabel->setString(fmt::format("Duration: {}:{:02d}", mins, secs).c_str());

                // Set default selection to first 20 seconds (or less if song is shorter)
                m_startMs = 0;
                m_endMs = std::min(m_songDurationMs, MAX_FRAGMENT_MS);
            }

            // Eliminar placeholder
            if (auto placeholder = m_waveformContainer->getChildByTag(999)) {
                placeholder->removeFromParent();
            }

            renderWaveform();

            // Mostrar overlay y handles ahora que tenemos el waveform
            if (m_selectionOverlay) {
                m_selectionOverlay->setVisible(true);
            }
            if (m_startHandle) {
                m_startHandle->setVisible(true);
            }
            if (m_endHandle) {
                m_endHandle->setVisible(true);
            }

            updateSelectionOverlay();
            updateSelectionLabel();
        });
    });
}

void ProfileMusicPopup::renderWaveform() {
    // Limpiar barras anteriores
    for (auto bar : m_waveformBars) {
        bar->removeFromParent();
    }
    m_waveformBars.clear();

    // En lugar del waveform, crear una barra de progreso simple
    auto progressBar = CCSprite::create("square.png");
    if (!progressBar) progressBar = CCSprite::createWithSpriteFrameName("whiteSquare60_001.png");
    if (!progressBar) {
        progressBar = CCSprite::create();
        progressBar->setTextureRect({0, 0, 1, 1});
    }

    if (progressBar) {
        progressBar->setScaleX(m_waveformWidth);
        progressBar->setScaleY(4.f); // Linea delgada
        progressBar->setAnchorPoint({0.5f, 0.5f});
        progressBar->setPosition({m_waveformWidth / 2, m_waveformHeight / 2});
        progressBar->setColor({100, 100, 110}); // Gris
        progressBar->setOpacity(150);
        m_waveformContainer->addChild(progressBar, 0);
        m_waveformBars.push_back(progressBar);
    }
}

void ProfileMusicPopup::updateSelectionOverlay() {
    if (!m_selectionOverlay || m_songDurationMs <= 0) return;

    float startX = msToPosition(m_startMs);
    float endX = msToPosition(m_endMs);

    // Actualizar posicion y tamano del overlay
    m_selectionOverlay->setPosition({startX, 0});
    m_selectionOverlay->setContentSize({endX - startX, m_waveformHeight});

    // Actualizar handles
    if (m_startHandle) {
        m_startHandle->setPosition({startX, m_waveformHeight / 2});
    }
    if (m_endHandle) {
        m_endHandle->setPosition({endX, m_waveformHeight / 2});
    }
}

void ProfileMusicPopup::updateSelectionLabel() {
    int startSecs = m_startMs / 1000;
    int endSecs = m_endMs / 1000;
    int durationSecs = (m_endMs - m_startMs) / 1000;

    std::string text = fmt::format("{}:{:02d} - {}:{:02d} ({} sec)",
        startSecs / 60, startSecs % 60,
        endSecs / 60, endSecs % 60,
        durationSecs);

    m_selectionLabel->setString(text.c_str());

    // Color rojo si excede 20 segundos
    if (durationSecs > 20) {
        m_selectionLabel->setColor({255, 100, 100});
    } else {
        m_selectionLabel->setColor({255, 255, 255});
    }
}

int ProfileMusicPopup::positionToMs(float x) {
    if (m_songDurationMs <= 0) return 0;
    float ratio = x / m_waveformWidth;
    return static_cast<int>(ratio * m_songDurationMs);
}

float ProfileMusicPopup::msToPosition(int ms) {
    if (m_songDurationMs <= 0) return 0;
    return (static_cast<float>(ms) / m_songDurationMs) * m_waveformWidth;
}

void ProfileMusicPopup::clampSelection() {
    // Asegurar que no exceda la duracion de la cancion
    if (m_startMs < 0) m_startMs = 0;
    if (m_endMs > m_songDurationMs) m_endMs = m_songDurationMs;

    // Asegurar minimo de 5 segundos
    if (m_endMs - m_startMs < MIN_FRAGMENT_MS) {
        if (m_endMs + MIN_FRAGMENT_MS - (m_endMs - m_startMs) <= m_songDurationMs) {
            m_endMs = m_startMs + MIN_FRAGMENT_MS;
        } else {
            m_startMs = m_endMs - MIN_FRAGMENT_MS;
        }
    }

    // Asegurar maximo de 20 segundos
    if (m_endMs - m_startMs > MAX_FRAGMENT_MS) {
        m_endMs = m_startMs + MAX_FRAGMENT_MS;
    }

    // Re-clampar despues de ajustes
    if (m_startMs < 0) m_startMs = 0;
    if (m_endMs > m_songDurationMs) m_endMs = m_songDurationMs;
}

bool ProfileMusicPopup::ccTouchBegan(CCTouch* touch, CCEvent* event) {
    // Let the parent handle it first
    if (!Popup::ccTouchBegan(touch, event)) return false;

    // Don't handle waveform touches if no song loaded
    if (m_songDurationMs <= 0) return true;

    auto touchPos = touch->getLocation();
    auto localPos = m_waveformContainer->convertToNodeSpace(touchPos);

    // Check if touch is inside waveform area
    if (localPos.x < -20 || localPos.x > m_waveformWidth + 20 ||
        localPos.y < -20 || localPos.y > m_waveformHeight + 20) {
        // Outside waveform - don't handle dragging
        return true;
    }

    float startX = msToPosition(m_startMs);
    float endX = msToPosition(m_endMs);

    // Check handles (with tolerance) - prioritize the closest one
    float tolerance = 20.f;

    float distToStart = std::abs(localPos.x - startX);
    float distToEnd = std::abs(localPos.x - endX);

    // Check if touching either handle
    bool touchingStart = distToStart < tolerance;
    bool touchingEnd = distToEnd < tolerance;

    if (touchingStart && touchingEnd) {
        // Both handles are close, pick the closest one
        if (distToStart < distToEnd) {
            m_isDraggingStart = true;
            m_dragStartX = localPos.x;
            m_dragStartMs = m_startMs;
            return true;
        } else {
            m_isDraggingEnd = true;
            m_dragStartX = localPos.x;
            m_dragStartMs = m_endMs;
            return true;
        }
    } else if (touchingStart) {
        m_isDraggingStart = true;
        m_dragStartX = localPos.x;
        m_dragStartMs = m_startMs;
        return true;
    } else if (touchingEnd) {
        m_isDraggingEnd = true;
        m_dragStartX = localPos.x;
        m_dragStartMs = m_endMs;
        return true;
    }

    // Check if inside selection (to move entire selection)
    if (localPos.x >= startX && localPos.x <= endX) {
        m_isDraggingSelection = true;
        m_dragStartX = localPos.x;
        m_dragStartMs = m_startMs;
        return true;
    }

    return true;
}

void ProfileMusicPopup::ccTouchMoved(CCTouch* touch, CCEvent* event) {
    if (m_songDurationMs <= 0) return;

    auto touchPos = touch->getLocation();
    auto localPos = m_waveformContainer->convertToNodeSpace(touchPos);

    // Clampar dentro del area
    localPos.x = std::max(0.f, std::min(m_waveformWidth, localPos.x));

    if (m_isDraggingStart) {
        int newStartMs = positionToMs(localPos.x);
        int duration = m_endMs - m_startMs;

        // Si la flecha verde intenta pasar la roja, mover ambas juntas
        if (newStartMs > m_endMs - MIN_FRAGMENT_MS) {
            // Mover ambas flechas manteniendo la distancia
            newStartMs = std::min(newStartMs, m_songDurationMs - duration);
            newStartMs = std::max(0, newStartMs);
            m_startMs = newStartMs;
            m_endMs = newStartMs + duration;
        } else {
            // Movimiento normal
            newStartMs = std::max(newStartMs, m_endMs - MAX_FRAGMENT_MS);
            newStartMs = std::max(0, newStartMs);
            m_startMs = newStartMs;
        }
    }
    else if (m_isDraggingEnd) {
        int newEndMs = positionToMs(localPos.x);
        int duration = m_endMs - m_startMs;

        // Si la flecha roja intenta pasar la verde, mover ambas juntas
        if (newEndMs < m_startMs + MIN_FRAGMENT_MS) {
            // Mover ambas flechas manteniendo la distancia
            newEndMs = std::max(newEndMs, duration);
            newEndMs = std::min(newEndMs, m_songDurationMs);
            m_endMs = newEndMs;
            m_startMs = newEndMs - duration;
        } else {
            // Movimiento normal
            newEndMs = std::min(newEndMs, m_startMs + MAX_FRAGMENT_MS);
            newEndMs = std::min(newEndMs, m_songDurationMs);
            m_endMs = newEndMs;
        }
    }
    else if (m_isDraggingSelection) {
        float deltaX = localPos.x - m_dragStartX;
        int deltaMs = positionToMs(m_dragStartX + deltaX) - positionToMs(m_dragStartX);

        int duration = m_endMs - m_startMs;
        int newStartMs = m_dragStartMs + deltaMs;

        // Clampar
        if (newStartMs < 0) newStartMs = 0;
        if (newStartMs + duration > m_songDurationMs) newStartMs = m_songDurationMs - duration;

        m_startMs = newStartMs;
        m_endMs = newStartMs + duration;
    }

    updateSelectionOverlay();
    updateSelectionLabel();
}

void ProfileMusicPopup::ccTouchEnded(CCTouch* touch, CCEvent* event) {
    m_isDraggingStart = false;
    m_isDraggingEnd = false;
    m_isDraggingSelection = false;
}

void ProfileMusicPopup::onPlayPreview(CCObject*) {
    if (m_previewPath.empty() || m_songID <= 0) {
        showError("Load a song first");
        return;
    }

    ProfileMusicManager::get().playPreview(m_previewPath, m_startMs, m_endMs);
}

void ProfileMusicPopup::onStopPreview(CCObject*) {
    ProfileMusicManager::get().stopPreview();
}

void ProfileMusicPopup::onDownloadSong(CCObject*) {
    if (m_songID <= 0) {
        showError("Load a song first");
        return;
    }

    showLoading();

    ProfileMusicManager::get().downloadSongForPreview(m_songID, [this](bool success, std::string const& path) {
        hideLoading();

        if (success) {
            m_previewPath = path;
            PaimonNotify::create("Song downloaded! You can now preview.", NotificationIcon::Success)->show();
        } else {
            showError("Failed to download song");
        }
    });
}

void ProfileMusicPopup::onSave(CCObject*) {
    if (m_songID <= 0) {
        showError("Please load a song first");
        return;
    }

    if (m_endMs - m_startMs > MAX_FRAGMENT_MS) {
        showError("Fragment must be 20 seconds or less");
        return;
    }

    if (m_endMs - m_startMs < MIN_FRAGMENT_MS) {
        showError("Fragment must be at least 5 seconds");
        return;
    }

    showLoading();

    ProfileMusicManager::ProfileMusicConfig config;
    config.songID = m_songID;
    config.startMs = m_startMs;
    config.endMs = m_endMs;
    config.volume = 1.0f; // Siempre usar 1.0
    config.enabled = true;
    config.songName = m_songName;
    config.artistName = m_artistName;

    std::string username = GJAccountManager::get()->m_username;

    ProfileMusicManager::get().uploadProfileMusic(m_accountID, username, config, [this](bool success, std::string const& msg) {
        hideLoading();

        if (success) {
            PaimonNotify::create("Profile music saved!", NotificationIcon::Success)->show();
            this->onClose(nullptr);
        } else {
            showError(fmt::format("Failed to save: {}", msg));
        }
    });
}

void ProfileMusicPopup::onDelete(CCObject*) {
    // Create a simple confirmation
    geode::createQuickPopup(
        "Delete Music",
        "Are you sure you want to remove your profile music?",
        "Cancel",
        "Delete",
        [this](auto, bool confirmed) {
            if (confirmed) {
                showLoading();

                std::string username = GJAccountManager::get()->m_username;

                ProfileMusicManager::get().deleteProfileMusic(m_accountID, username, [this](bool success, std::string const& msg) {
                    hideLoading();

                    if (success) {
                        PaimonNotify::create("Profile music deleted!", NotificationIcon::Success)->show();
                        this->onClose(nullptr);
                    } else {
                        showError(fmt::format("Failed to delete: {}", msg));
                    }
                });
            }
        }
    );
}

void ProfileMusicPopup::onClose(CCObject* sender) {
    ProfileMusicManager::get().stopPreview();
    Popup::onClose(sender);
}

void ProfileMusicPopup::loadExistingConfig() {
    ProfileMusicManager::get().getProfileMusicConfig(m_accountID, [this](bool success, const ProfileMusicManager::ProfileMusicConfig& config) {
        if (!success || config.songID <= 0) return;

        m_songID = config.songID;
        m_startMs = config.startMs;
        m_endMs = config.endMs;
        m_songName = config.songName;
        m_artistName = config.artistName;

        // Actualizar UI
        m_songIdInput->setString(std::to_string(m_songID));


        // Cargar info y waveform
        onLoadSong(nullptr);
    });
}

void ProfileMusicPopup::showLoading() {
    if (m_loadingSpinner) return;

    m_loadingSpinner = geode::LoadingSpinner::create(30.f);
    m_loadingSpinner->setPosition(m_mainLayer->getContentSize() / 2);
    m_loadingSpinner->setID("paimon-loading-spinner"_spr);
    m_mainLayer->addChild(m_loadingSpinner, 100);
}

void ProfileMusicPopup::hideLoading() {
    if (m_loadingSpinner) {
        m_loadingSpinner->removeFromParent();
        m_loadingSpinner = nullptr;
    }
}

void ProfileMusicPopup::showError(std::string const& message) {
    FLAlertLayer::create(nullptr, "Error", message, "OK", nullptr)->show();
}




















#pragma once

#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/TextInput.hpp>
#include "../managers/ProfileMusicManager.hpp"
#include <vector>

using namespace geode::prelude;

/**
 * ProfileMusicPopup - UI para configurar la música del perfil
 * Muestra waveform visual y permite seleccionar fragmento de 20 segundos
 */
class ProfileMusicPopup : public geode::Popup {
protected:
    // Datos
    int m_accountID;
    int m_songID = 0;
    int m_startMs = 0;
    int m_endMs = 20000;
    int m_songDurationMs = 0;
    std::string m_songName;
    std::string m_artistName;
    std::string m_previewPath;

    // UI Components
    CCMenu* m_mainMenu = nullptr;
    geode::TextInput* m_songIdInput = nullptr;
    CCLabelBMFont* m_songInfoLabel = nullptr;
    CCLabelBMFont* m_durationLabel = nullptr;
    CCLabelBMFont* m_selectionLabel = nullptr;
    CCNode* m_waveformContainer = nullptr;
    CCLayerColor* m_selectionOverlay = nullptr;
    CCSprite* m_startHandle = nullptr;
    CCSprite* m_endHandle = nullptr;
    LoadingCircle* m_loadingCircle = nullptr;

    // Waveform data
    std::vector<float> m_peaks;
    std::vector<CCSprite*> m_waveformBars;

    // Waveform dimensions
    float m_waveformX = 0;
    float m_waveformY = 0;
    float m_waveformWidth = 360.f;
    float m_waveformHeight = 50.f;

    // Dragging state
    bool m_isDraggingStart = false;
    bool m_isDraggingEnd = false;
    bool m_isDraggingSelection = false;
    float m_dragStartX = 0;
    int m_dragStartMs = 0;

    // Max fragment duration (20 seconds)
    static constexpr int MAX_FRAGMENT_MS = 20000;
    static constexpr int MIN_FRAGMENT_MS = 5000;

    bool setup(int accountID);

    void onClose(CCObject*) override;

    // UI Creation
    void createSongIdInput();
    void createWaveformDisplay();
    void createControlButtons();

    // Actions
    void onLoadSong(CCObject*);
    void onPlayPreview(CCObject*);
    void onStopPreview(CCObject*);
    void onSave(CCObject*);
    void onDelete(CCObject*);
    void onDownloadSong(CCObject*);

    // Waveform
    void loadWaveform();
    void renderWaveform();
    void updateSelectionOverlay();
    void updateSelectionLabel();

    // Helpers
    int positionToMs(float x);
    float msToPosition(int ms);
    void clampSelection();

    // Touch handling for waveform
    bool ccTouchBegan(CCTouch* touch, CCEvent* event) override;
    void ccTouchMoved(CCTouch* touch, CCEvent* event) override;
    void ccTouchEnded(CCTouch* touch, CCEvent* event) override;

    void showLoading();
    void hideLoading();
    void showError(const std::string& message);

public:
    static ProfileMusicPopup* create(int accountID);

    void loadExistingConfig();
};





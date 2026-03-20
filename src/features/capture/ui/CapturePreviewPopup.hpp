#pragma once
#include <Geode/DefaultInclude.hpp>
#include <Geode/utils/function.hpp>
#include <cocos2d.h>
#include <memory>
#include <unordered_set>
#include "../../../utils/PlayerToggleHelper.hpp"

class CapturePreviewPopup : public geode::Popup {
public:
    static CapturePreviewPopup* create(
        cocos2d::CCTexture2D* texture, 
        int levelID,
        std::shared_ptr<uint8_t> buffer,
        int width,
        int height,
        geode::CopyableFunction<void(bool accepted, int levelID, std::shared_ptr<uint8_t> buffer, int width, int height, std::string mode, std::string replaceId)> callback,
        geode::CopyableFunction<void(bool hideP1, bool hideP2, CapturePreviewPopup* popup)> recaptureCallback = nullptr,
        bool isPlayer1Hidden = false,
        bool isPlayer2Hidden = false,
        bool isModerator = false
    );
    
    virtual ~CapturePreviewPopup();

    void updateContent(cocos2d::CCTexture2D* texture, std::shared_ptr<uint8_t> buffer, int width, int height);

    void recapture();
    void liveRecapture(bool updateBuffer = true);

    // Editing actions (called from CaptureEditPopup)
    void onCropBtn(cocos2d::CCObject*);
    void onToggleFillBtn(cocos2d::CCObject*);
    void onTogglePlayer1Btn(cocos2d::CCObject*);
    void onTogglePlayer2Btn(cocos2d::CCObject*);
    void onDownloadBtn(cocos2d::CCObject*);

    bool isPlayer1Hidden() const { return m_isPlayer1Hidden; }
    bool isPlayer2Hidden() const { return m_isPlayer2Hidden; }

    // Called by child popups to signal open/close
    void setChildPopupOpen(bool open) { m_childPopupOpen = open; }
    void setPausedMusic(bool v) { m_pausedMusic = v; }

protected:
    bool init() override;

    // Touch zoom/pan (same pattern as LocalThumbnailViewPopup)
    bool ccTouchBegan(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void ccTouchMoved(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void ccTouchEnded(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void ccTouchCancelled(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void scrollWheel(float x, float y) override;

private:
    geode::Ref<cocos2d::CCTexture2D> m_texture;
    geode::CopyableFunction<void(bool, int, std::shared_ptr<uint8_t>, int, int, std::string, std::string)> m_callback;
    geode::CopyableFunction<void(bool, bool, CapturePreviewPopup*)> m_recaptureCallback;
    std::shared_ptr<uint8_t> m_buffer;
    int m_levelID = 0;
    int m_width = 0;
    int m_height = 0;
    bool m_isPlayer1Hidden = false;
    bool m_isPlayer2Hidden = false;
    bool m_isModerator = false;
    bool m_childPopupOpen = false;
    bool m_touchDelegateRegistered = false;
    bool m_pausedMusic = false;

    // Per-player saved visibility state (persists across recaptures)
    PlayerVisState m_p1SavedState;
    PlayerVisState m_p2SavedState;
    
    cocos2d::CCSprite* m_previewSprite = nullptr;
    cocos2d::CCClippingNode* m_clippingNode = nullptr;
    cocos2d::CCMenu* m_buttonMenu = nullptr;

    bool m_isCropped = false;
    bool m_fillMode = true;
    bool m_callbackExecuted = false;
    bool m_recapturePending = false;

    // Zoom/pan state (mirrors LocalThumbnailViewPopup)
    float m_viewWidth = 0.f;
    float m_viewHeight = 0.f;
    float m_initialScale = 1.f;
    float m_minScale = 0.5f;
    float m_maxScale = 4.f;
    std::unordered_set<cocos2d::CCTouch*> m_activeTouches;
    float m_initialDistance = 0.f;
    float m_savedScale = 1.f;
    cocos2d::CCPoint m_touchMidPoint{0, 0};
    bool m_wasZooming = false;

    void updatePreviewScale();
    void onRecaptureTimeout(float dt);
    void onClose(cocos2d::CCObject*) override;

    void onAcceptBtn(cocos2d::CCObject*);
    void onCancelBtn(cocos2d::CCObject*);
    void onEditBtn(cocos2d::CCObject*);
    void onRecenterBtn(cocos2d::CCObject*);

    struct CropRect { int x, y, width, height; };
    CropRect detectBlackBorders();
    void applyCrop(const CropRect& rect);

    // Zoom helpers
    static float clampF(float value, float mn, float mx);
    void clampSpritePosition();
    void clampSpritePositionAnimated();
};

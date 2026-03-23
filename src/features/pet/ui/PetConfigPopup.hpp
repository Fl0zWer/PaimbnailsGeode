#pragma once
#include <Geode/Geode.hpp>
#include <Geode/binding/Slider.hpp>

class PetConfigPopup : public geode::Popup {
protected:
    void onExit() override;
    void scrollWheel(float x, float y) override;

    // gallery
    cocos2d::CCNode* m_galleryContainer = nullptr;
    cocos2d::CCMenu* m_galleryMenu = nullptr;
    cocos2d::CCSprite* m_previewSprite = nullptr;
    cocos2d::CCLabelBMFont* m_selectedLabel = nullptr;

    // scroll for settings
    geode::ScrollLayer* m_scrollLayer = nullptr;
    cocos2d::CCSprite* m_scrollArrow = nullptr;

    // sliders
    Slider* m_scaleSlider = nullptr;
    cocos2d::CCLabelBMFont* m_scaleLabel = nullptr;
    Slider* m_sensitivitySlider = nullptr;
    cocos2d::CCLabelBMFont* m_sensitivityLabel = nullptr;
    Slider* m_opacitySlider = nullptr;
    cocos2d::CCLabelBMFont* m_opacityLabel = nullptr;
    Slider* m_bounceHeightSlider = nullptr;
    cocos2d::CCLabelBMFont* m_bounceHeightLabel = nullptr;
    Slider* m_bounceSpeedSlider = nullptr;
    cocos2d::CCLabelBMFont* m_bounceSpeedLabel = nullptr;
    Slider* m_rotDampSlider = nullptr;
    cocos2d::CCLabelBMFont* m_rotDampLabel = nullptr;
    Slider* m_maxTiltSlider = nullptr;
    cocos2d::CCLabelBMFont* m_maxTiltLabel = nullptr;
    Slider* m_trailLengthSlider = nullptr;
    cocos2d::CCLabelBMFont* m_trailLengthLabel = nullptr;
    Slider* m_trailWidthSlider = nullptr;
    cocos2d::CCLabelBMFont* m_trailWidthLabel = nullptr;
    Slider* m_breathScaleSlider = nullptr;
    cocos2d::CCLabelBMFont* m_breathScaleLabel = nullptr;
    Slider* m_breathSpeedSlider = nullptr;
    cocos2d::CCLabelBMFont* m_breathSpeedLabel = nullptr;
    Slider* m_squishSlider = nullptr;
    cocos2d::CCLabelBMFont* m_squishLabel = nullptr;
    Slider* m_offsetXSlider = nullptr;
    cocos2d::CCLabelBMFont* m_offsetXLabel = nullptr;
    Slider* m_offsetYSlider = nullptr;
    cocos2d::CCLabelBMFont* m_offsetYLabel = nullptr;

    // toggles
    CCMenuItemToggler* m_enableToggle = nullptr;
    CCMenuItemToggler* m_flipToggle = nullptr;
    CCMenuItemToggler* m_trailToggle = nullptr;
    CCMenuItemToggler* m_idleToggle = nullptr;
    CCMenuItemToggler* m_bounceToggle = nullptr;
    CCMenuItemToggler* m_squishToggle = nullptr;

    // tabs
    int m_currentTab = 0; // 0 = gallery, 1 = settings
    cocos2d::CCNode* m_galleryTab = nullptr;
    cocos2d::CCNode* m_settingsTab = nullptr;
    std::vector<CCMenuItemSpriteExtra*> m_tabs;

    bool init() override;
    void createTabButtons();
    void onTabSwitch(cocos2d::CCObject* sender);

    // gallery
    void buildGalleryTab();
    void refreshGallery();
    void onAddImage(cocos2d::CCObject*);
    void onDeleteImage(cocos2d::CCObject*);
    void onDeleteAllImages(cocos2d::CCObject*);
    void onSelectImage(cocos2d::CCObject*);

    // settings
    void buildSettingsTab();
    void checkScrollPosition(float dt);

    // slider callbacks
    void onScaleChanged(cocos2d::CCObject*);
    void onSensitivityChanged(cocos2d::CCObject*);
    void onOpacityChanged(cocos2d::CCObject*);
    void onBounceHeightChanged(cocos2d::CCObject*);
    void onBounceSpeedChanged(cocos2d::CCObject*);
    void onRotDampChanged(cocos2d::CCObject*);
    void onMaxTiltChanged(cocos2d::CCObject*);
    void onTrailLengthChanged(cocos2d::CCObject*);
    void onTrailWidthChanged(cocos2d::CCObject*);
    void onBreathScaleChanged(cocos2d::CCObject*);
    void onBreathSpeedChanged(cocos2d::CCObject*);
    void onSquishChanged(cocos2d::CCObject*);
    void onOffsetXChanged(cocos2d::CCObject*);
    void onOffsetYChanged(cocos2d::CCObject*);

    // toggle callbacks
    void onEnableToggled(cocos2d::CCObject*);
    void onFlipToggled(cocos2d::CCObject*);
    void onTrailToggled(cocos2d::CCObject*);
    void onIdleToggled(cocos2d::CCObject*);
    void onBounceToggled(cocos2d::CCObject*);
    void onSquishToggled(cocos2d::CCObject*);
    void onLayerToggled(cocos2d::CCObject*);
    void onOpenShop(cocos2d::CCObject*);

    void applyLive();

public:
    static PetConfigPopup* create();
};


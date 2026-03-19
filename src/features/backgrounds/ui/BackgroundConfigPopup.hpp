#pragma once
#include <Geode/Geode.hpp>
#include <Geode/ui/TextInput.hpp>
#include <Geode/binding/Slider.hpp>

class BackgroundConfigPopup : public geode::Popup {
protected:
    geode::TextInput* m_idInput;
    cocos2d::CCLayer* m_menuLayer;
    cocos2d::CCLayer* m_profileLayer;
    cocos2d::CCLayer* m_petLayer = nullptr;
    cocos2d::CCLayer* m_layerBgLayer = nullptr;
    std::vector<CCMenuItemSpriteExtra*> m_tabs;
    int m_selectedTab = 0;
    Slider* m_slider = nullptr;

    // tab de fondos por layer
    std::string m_selectedLayerKey = "creator";
    geode::TextInput* m_layerIdInput = nullptr;
    Slider* m_layerDarkSlider = nullptr;
    std::vector<CCMenuItemSpriteExtra*> m_layerSelectBtns;
    CCMenuItemToggler* m_layerDarkToggle = nullptr;

    bool init();
    
    // armado general
    void createTabs();
    void onTab(cocos2d::CCObject* sender);
    void updateTabs();
    cocos2d::CCNode* createMenuTab();
    cocos2d::CCNode* createProfileTab();
    cocos2d::CCNode* createPetTab();
    cocos2d::CCNode* createLayerBgTab();

    // menu
    void onCustomImage(cocos2d::CCObject* sender);
    void onDownloadedThumbnails(cocos2d::CCObject* sender);
    void onSetID(cocos2d::CCObject* sender);
    void onApply(cocos2d::CCObject* sender);
    void onDarkMode(cocos2d::CCObject* sender);
    void onIntensityChanged(cocos2d::CCObject* sender);

    // perfil
    void onProfileCustomImage(cocos2d::CCObject* sender);
    void onProfileClear(cocos2d::CCObject* sender);
    void onCustomizePhoto(cocos2d::CCObject* sender);

    // mascota
    void onOpenPetConfig(cocos2d::CCObject* sender);

    // extras
    void onDefaultMenu(cocos2d::CCObject* sender);
    void onAdaptiveColors(cocos2d::CCObject* sender);

    // fondos por layer
    void onLayerSelect(cocos2d::CCObject* sender);
    void onLayerCustomImage(cocos2d::CCObject* sender);
    void onLayerRandom(cocos2d::CCObject* sender);
    void onLayerSameAs(cocos2d::CCObject* sender);
    void onLayerDefault(cocos2d::CCObject* sender);
    void onLayerSetID(cocos2d::CCObject* sender);
    void onLayerDarkMode(cocos2d::CCObject* sender);
    void onLayerDarkIntensity(cocos2d::CCObject* sender);
    void updateLayerSelectButtons();

    // util
    CCMenuItemSpriteExtra* createBtn(char const* text, cocos2d::CCPoint pos, cocos2d::SEL_MenuHandler handler, cocos2d::CCNode* parent);

public:
    static BackgroundConfigPopup* create();
};

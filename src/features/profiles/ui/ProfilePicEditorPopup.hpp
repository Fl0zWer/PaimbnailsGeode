#pragma once
#include <Geode/Geode.hpp>
#include <Geode/ui/TextInput.hpp>
#include <Geode/binding/Slider.hpp>
#include "../services/ProfilePicCustomizer.hpp"

class ProfilePicEditorPopup : public geode::Popup {
protected:
    // estado actual
    ProfilePicConfig m_editConfig;
    
    // vista previa
    cocos2d::CCNode* m_previewContainer = nullptr;
    cocos2d::CCNode* m_previewPic = nullptr;
    
    // tabs
    cocos2d::CCLayer* m_frameTab = nullptr;
    cocos2d::CCLayer* m_shapeTab = nullptr;
    cocos2d::CCLayer* m_decoTab = nullptr;
    std::vector<CCMenuItemSpriteExtra*> m_tabBtns;
    int m_currentTab = 0;

    // controles del borde
    Slider* m_thicknessSlider = nullptr;
    cocos2d::CCLabelBMFont* m_thicknessLabel = nullptr;

    // controles de forma
    Slider* m_scaleXSlider = nullptr;
    Slider* m_scaleYSlider = nullptr;
    Slider* m_sizeSlider = nullptr;
    cocos2d::CCLabelBMFont* m_scaleXLabel = nullptr;
    cocos2d::CCLabelBMFont* m_scaleYLabel = nullptr;
    cocos2d::CCLabelBMFont* m_sizeLabel = nullptr;

    // decoraciones
    cocos2d::CCLayer* m_decoListContent = nullptr;
    int m_decoPage = 0;
    int m_selectedDecoIdx = -1;  // decoracion seleccionada para mover/editar

    bool init();

    // tabs
    void createTabs();
    void switchTab(int tab);
    void onTabBtn(cocos2d::CCObject* sender);

    // borde
    cocos2d::CCNode* createFrameTab();
    void onFrameToggle(cocos2d::CCObject* sender);
    void onFrameSelect(cocos2d::CCObject* sender);
    void onFrameColorR(cocos2d::CCObject* sender);
    void onFrameColorG(cocos2d::CCObject* sender);
    void onFrameColorB(cocos2d::CCObject* sender);
    void onThicknessChanged(cocos2d::CCObject* sender);
    void onBorderColorSelect(cocos2d::CCObject* sender);

    // forma
    cocos2d::CCNode* createShapeTab();
    void onScaleXChanged(cocos2d::CCObject* sender);
    void onScaleYChanged(cocos2d::CCObject* sender);
    void onSizeChanged(cocos2d::CCObject* sender);
    void onStencilSelect(cocos2d::CCObject* sender);
    void onResetShape(cocos2d::CCObject* sender);

    // decoraciones
    cocos2d::CCNode* createDecoTab();
    void onAddDeco(cocos2d::CCObject* sender);
    void onRemoveDeco(cocos2d::CCObject* sender);
    void onDecoPage(cocos2d::CCObject* sender);
    void refreshDecoList();

    // vista previa
    void rebuildPreview();

    // acciones
    void onSave(cocos2d::CCObject* sender);
    void onReset(cocos2d::CCObject* sender);

    // util
    CCMenuItemSpriteExtra* makeBtn(char const* text, cocos2d::SEL_MenuHandler sel, cocos2d::CCNode* parent, float scale = 0.5f);

public:
    static ProfilePicEditorPopup* create();
};

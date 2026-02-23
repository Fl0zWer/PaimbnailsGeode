#pragma once
#include <Geode/DefaultInclude.hpp>
#include <cocos2d.h>
#include <Geode/cocos/extensions/GUI/CCScrollView/CCScrollView.h>
#include <vector>
#include <string>

class CapturePreviewPopup;

class CaptureLayerEditorPopup : public geode::Popup {
public:
    static CaptureLayerEditorPopup* create(CapturePreviewPopup* previewPopup);

    static void restoreAllLayers();

protected:
    bool init();

private:
    CapturePreviewPopup* m_previewPopup = nullptr;
    cocos2d::CCSprite* m_miniPreview = nullptr;
    cocos2d::CCMenu* m_listMenu = nullptr;
    cocos2d::extension::CCScrollView* m_scrollView = nullptr;

    struct LayerEntry {
        cocos2d::CCNode* node = nullptr;
        std::string name;
        bool currentVisibility = true;
    };

    std::vector<LayerEntry> m_layers;

    void populateLayers();
    void buildList();
    void updateMiniPreview();
    void onToggleLayer(cocos2d::CCObject* sender);
    void onDoneBtn(cocos2d::CCObject* sender);
    void onRestoreAllBtn(cocos2d::CCObject* sender);
};

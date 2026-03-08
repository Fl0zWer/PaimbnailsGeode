#pragma once
#include <Geode/DefaultInclude.hpp>
#include <cocos2d.h>
#include <Geode/ui/ScrollLayer.hpp>
#include <Geode/binding/CCMenuItemToggler.hpp>
#include <vector>
#include <string>

class CapturePreviewPopup;

class CaptureLayerEditorPopup : public geode::Popup {
public:
    static CaptureLayerEditorPopup* create(CapturePreviewPopup* previewPopup);

    static void restoreAllLayers();

protected:
    bool init() override;

private:
    CapturePreviewPopup* m_previewPopup = nullptr;
    cocos2d::CCSprite* m_miniPreview = nullptr;
    geode::ScrollLayer* m_scrollView = nullptr;
    cocos2d::CCNode* m_listRoot = nullptr;

    struct LayerEntry {
        cocos2d::CCNode* node = nullptr;
        std::string name;
        bool currentVisibility = true;
        bool isGroup = false;
        int depth = 0;
        int parentIndex = -1;
        std::vector<int> childIndices;
        // UI references for in-place updates (no rebuild needed)
        CCMenuItemToggler* toggler = nullptr;
        cocos2d::CCLabelBMFont* label = nullptr;
    };

    std::vector<LayerEntry> m_layers;

    void populateLayers();
    void buildList();
    void updateMiniPreview();
    void refreshRowVisuals(int idx);
    [[nodiscard]] bool isEntryVisible(int idx) const;
    void setEntryVisible(int idx, bool visible, bool cascadeChildren);
    void onToggleLayer(cocos2d::CCObject* sender);
    void onDoneBtn(cocos2d::CCObject* sender);
    void onRestoreAllBtn(cocos2d::CCObject* sender);
};

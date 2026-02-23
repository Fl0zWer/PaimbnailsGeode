#pragma once
#include <Geode/DefaultInclude.hpp>
#include <cocos2d.h>

class CapturePreviewPopup;

class CaptureEditPopup : public geode::Popup {
public:
    static CaptureEditPopup* create(CapturePreviewPopup* previewPopup);

protected:
    bool init();

private:
    CapturePreviewPopup* m_previewPopup = nullptr;

    void onTogglePlayerBtn(cocos2d::CCObject*);
    void onCropBtn(cocos2d::CCObject*);
    void onToggleFillBtn(cocos2d::CCObject*);
    void onLayerEditorBtn(cocos2d::CCObject*);
};

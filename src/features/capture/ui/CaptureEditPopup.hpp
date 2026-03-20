#pragma once
#include <Geode/DefaultInclude.hpp>
#include <cocos2d.h>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include "CapturePreviewPopup.hpp"

class CaptureEditPopup : public geode::Popup {
public:
    static CaptureEditPopup* create(CapturePreviewPopup* previewPopup);

protected:
    bool init() override;
    void onClose(cocos2d::CCObject*) override;
    void keyBackClicked() override;
    void onExit() override;

private:
    geode::WeakRef<CapturePreviewPopup> m_previewPopup = nullptr;
    CCMenuItemSpriteExtra* m_player1Btn = nullptr;
    CCMenuItemSpriteExtra* m_player2Btn = nullptr;
    bool m_parentNotifiedClosed = false;
    void notifyParentClosed();

    void onTogglePlayer1Btn(cocos2d::CCObject*);
    void onTogglePlayer2Btn(cocos2d::CCObject*);
    void onCropBtn(cocos2d::CCObject*);
    void onToggleFillBtn(cocos2d::CCObject*);
    void onLayerEditorBtn(cocos2d::CCObject*);
};

#include "CaptureEditPopup.hpp"
#include "CapturePreviewPopup.hpp"
#include "CaptureLayerEditorPopup.hpp"
#include "../utils/Localization.hpp"
#include "../utils/PaimonButtonHighlighter.hpp"
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/binding/ButtonSprite.hpp>

using namespace geode::prelude;
using namespace cocos2d;

CaptureEditPopup* CaptureEditPopup::create(CapturePreviewPopup* previewPopup) {
    auto ret = new CaptureEditPopup();
    ret->m_previewPopup = previewPopup;
    if (ret->init()) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool CaptureEditPopup::init() {
    if (!Popup::init(220.f, 180.f)) return false;

    this->setTitle(Localization::get().getString("edit.title").c_str());

    auto content = this->m_mainLayer->getContentSize();

    auto menu = CCMenu::create();
    menu->setPosition({content.width * 0.5f, content.height * 0.5f - 5.f});

    auto makeButton = [&](const char* labelKey, const char* icon,
                          SEL_MenuHandler handler) -> CCMenuItemSpriteExtra* {
        auto spr = ButtonSprite::create(
            Localization::get().getString(labelKey).c_str(),
            130, true, "bigFont.fnt", icon, 28.f, 0.5f
        );
        if (!spr) return nullptr;
        auto btn = CCMenuItemSpriteExtra::create(spr, this, handler);
        PaimonButtonHighlighter::registerButton(btn);
        return btn;
    };

    auto playerBtn = makeButton("edit.toggle_player", "GJ_button_01.png",
                                menu_selector(CaptureEditPopup::onTogglePlayerBtn));
    auto cropBtn = makeButton("edit.crop_borders", "GJ_button_01.png",
                              menu_selector(CaptureEditPopup::onCropBtn));
    auto fillBtn = makeButton("edit.toggle_fill", "GJ_button_01.png",
                              menu_selector(CaptureEditPopup::onToggleFillBtn));
    auto layerBtn = makeButton("edit.edit_layers", "GJ_button_02.png",
                               menu_selector(CaptureEditPopup::onLayerEditorBtn));

    if (playerBtn) menu->addChild(playerBtn);
    if (cropBtn)   menu->addChild(cropBtn);
    if (fillBtn)   menu->addChild(fillBtn);
    if (layerBtn)  menu->addChild(layerBtn);

    menu->alignItemsVerticallyWithPadding(6.f);
    this->m_mainLayer->addChild(menu);

    return true;
}

void CaptureEditPopup::onTogglePlayerBtn(CCObject* sender) {
    if (m_previewPopup) {
        m_previewPopup->onTogglePlayerBtn(sender);
    }
}

void CaptureEditPopup::onCropBtn(CCObject* sender) {
    if (m_previewPopup) {
        m_previewPopup->onCropBtn(sender);
    }
}

void CaptureEditPopup::onToggleFillBtn(CCObject* sender) {
    if (m_previewPopup) {
        m_previewPopup->onToggleFillBtn(sender);
    }
}

void CaptureEditPopup::onLayerEditorBtn(CCObject* sender) {
    if (!sender || !m_previewPopup) return;

    log::info("[CaptureEditPopup] Opening layer editor");
    auto* editor = CaptureLayerEditorPopup::create(m_previewPopup);
    if (editor) {
        editor->show();
    }
}

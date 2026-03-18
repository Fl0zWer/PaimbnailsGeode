#include "CaptureEditPopup.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "CapturePreviewPopup.hpp"
#include "CaptureLayerEditorPopup.hpp"
#include "../../../utils/Localization.hpp"
#include "../../../utils/PaimonButtonHighlighter.hpp"
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/binding/ButtonSprite.hpp>

using namespace geode::prelude;
using namespace cocos2d;

CaptureEditPopup* CaptureEditPopup::create(CapturePreviewPopup* previewPopup) {
    auto ret = new CaptureEditPopup();
    ret->m_previewPopup = previewPopup; // must be set BEFORE init() so labels are correct
    if (ret && ret->init()) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool CaptureEditPopup::init() {
    if (!Popup::init(220.f, 210.f)) return false;

    this->setTitle(Localization::get().getString("edit.title").c_str());

    auto content = this->m_mainLayer->getContentSize();

    auto menu = CCMenu::create();
    menu->setPosition({content.width * 0.5f, content.height * 0.5f - 5.f});

    auto makeButton = [&](char const* labelKey, char const* icon,
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

    // Dynamic label based on current hide state
    std::string p1Label = (m_previewPopup && m_previewPopup->isPlayer1Hidden())
        ? Localization::get().getString("edit.show_player1")
        : Localization::get().getString("edit.hide_player1");
    std::string p2Label = (m_previewPopup && m_previewPopup->isPlayer2Hidden())
        ? Localization::get().getString("edit.show_player2")
        : Localization::get().getString("edit.hide_player2");

    auto makeDynButton = [&](std::string const& label, char const* icon,
                             SEL_MenuHandler handler) -> CCMenuItemSpriteExtra* {
        auto spr = ButtonSprite::create(
            label.c_str(),
            130, true, "bigFont.fnt", icon, 28.f, 0.5f
        );
        if (!spr) return nullptr;
        auto btn = CCMenuItemSpriteExtra::create(spr, this, handler);
        PaimonButtonHighlighter::registerButton(btn);
        return btn;
    };

    auto player1Btn = makeDynButton(p1Label, "GJ_button_01.png",
                                    menu_selector(CaptureEditPopup::onTogglePlayer1Btn));
    auto player2Btn = makeDynButton(p2Label, "GJ_button_01.png",
                                    menu_selector(CaptureEditPopup::onTogglePlayer2Btn));
    auto cropBtn = makeButton("edit.crop_borders", "GJ_button_01.png",
                              menu_selector(CaptureEditPopup::onCropBtn));
    auto fillBtn = makeButton("edit.toggle_fill", "GJ_button_01.png",
                              menu_selector(CaptureEditPopup::onToggleFillBtn));
    auto layerBtn = makeButton("edit.edit_layers", "GJ_button_02.png",
                               menu_selector(CaptureEditPopup::onLayerEditorBtn));

    if (player1Btn) menu->addChild(player1Btn);
    if (player2Btn) menu->addChild(player2Btn);
    if (cropBtn)   menu->addChild(cropBtn);
    if (fillBtn)   menu->addChild(fillBtn);
    if (layerBtn)  menu->addChild(layerBtn);

    m_player1Btn = player1Btn;
    m_player2Btn = player2Btn;

    menu->alignItemsVerticallyWithPadding(6.f);
    this->m_mainLayer->addChild(menu);

    paimon::markDynamicPopup(this);
    return true;
}

void CaptureEditPopup::onClose(CCObject* sender) {
    // Notify parent that child popup is closing
    if (m_previewPopup) {
        m_previewPopup->setChildPopupOpen(false);
    }
    Popup::onClose(sender);
}

void CaptureEditPopup::onTogglePlayer1Btn(CCObject* sender) {
    if (!m_previewPopup) return;
    m_previewPopup->onTogglePlayer1Btn(sender);
    if (m_player1Btn) {
        std::string label = m_previewPopup->isPlayer1Hidden()
            ? Localization::get().getString("edit.show_player1")
            : Localization::get().getString("edit.hide_player1");
        auto newSpr = ButtonSprite::create(label.c_str(), 130, true, "bigFont.fnt", "GJ_button_01.png", 28.f, 0.5f);
        if (newSpr) m_player1Btn->setNormalImage(newSpr);
    }
}

void CaptureEditPopup::onTogglePlayer2Btn(CCObject* sender) {
    if (!m_previewPopup) return;
    m_previewPopup->onTogglePlayer2Btn(sender);
    if (m_player2Btn) {
        std::string label = m_previewPopup->isPlayer2Hidden()
            ? Localization::get().getString("edit.show_player2")
            : Localization::get().getString("edit.hide_player2");
        auto newSpr = ButtonSprite::create(label.c_str(), 130, true, "bigFont.fnt", "GJ_button_01.png", 28.f, 0.5f);
        if (newSpr) m_player2Btn->setNormalImage(newSpr);
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

    auto& loc = Localization::get();
    geode::createQuickPopup(
        loc.getString("layers.beta_title").c_str(),
        loc.getString("layers.beta_message").c_str(),
        loc.getString("layers.beta_cancel").c_str(),
        loc.getString("layers.beta_confirm").c_str(),
        [this](auto, bool confirmed) {
            if (!confirmed) return;
            log::info("[CaptureEditPopup] Opening layer editor");
            auto* editor = CaptureLayerEditorPopup::create(m_previewPopup);
            if (editor) {
                editor->show();
            }
        }
    );
}

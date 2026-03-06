#include "ReportInputPopup.hpp"
#include "../utils/PaimonNotification.hpp"
#include "../utils/Localization.hpp"

using namespace geode::prelude;
using namespace cocos2d;

bool ReportInputPopup::init(int levelID, geode::CopyableFunction<void(std::string)> callback) {
    if (!Popup::init(420.f, 200.f)) return false;

    m_levelID = levelID;
    m_callback = callback;

    this->setTitle(Localization::get().getString("report.title").c_str());

    auto contentSize = m_mainLayer->getContentSize();

    // campo de texto (geode::TextInput incluye fondo integrado)
    m_textInput = geode::TextInput::create(360.f, Localization::get().getString("report.placeholder").c_str(), "chatFont.fnt");
    m_textInput->setFilter("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 .,;:!?()-_");
    m_textInput->setMaxCharCount(120);
    m_textInput->setPosition({contentSize.width / 2, contentSize.height / 2 + 5.f});
    m_mainLayer->addChild(m_textInput);

    // boton enviar
    auto sendSpr = ButtonSprite::create(
        Localization::get().getString("report.send").c_str(),
        "goldFont.fnt", "GJ_button_01.png", 0.8f
    );
    auto sendBtn = CCMenuItemSpriteExtra::create(
        sendSpr, this, menu_selector(ReportInputPopup::onSend)
    );
    sendBtn->setID("send-report-btn"_spr);
    sendBtn->setPosition({contentSize.width / 2, 30.f});
    m_buttonMenu->addChild(sendBtn);

    return true;
}

void ReportInputPopup::onSend(CCObject*) {
    std::string reason = m_textInput->getString();
    if (reason.empty()) {
        PaimonNotify::create(Localization::get().getString("report.empty_reason").c_str(), NotificationIcon::Warning)->show();
        return;
    }

    if (m_callback) {
        m_callback(reason);
    }
    this->onClose(nullptr);
}

ReportInputPopup* ReportInputPopup::create(int levelID, geode::CopyableFunction<void(std::string)> callback) {
    auto ret = new ReportInputPopup();
    if (ret && ret->init(levelID, callback)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

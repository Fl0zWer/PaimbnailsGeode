#include "BanUserPopup.hpp"
#include "../utils/HttpClient.hpp"
#include "../utils/Localization.hpp"

using namespace geode::prelude;

BanUserPopup* BanUserPopup::create(std::string const& username) {
    auto ret = new BanUserPopup();
    if (ret && ret->init(username)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool BanUserPopup::init(std::string const& username) {
    if (!Popup::init(300.f, 200.f)) return false;

    m_username = username;
    this->setTitle(Localization::get().getString("ban.popup.title"));

    auto content = m_mainLayer->getContentSize();

    auto lbl = CCLabelBMFont::create(fmt::format(fmt::runtime(Localization::get().getString("ban.popup.user")), username).c_str(), "goldFont.fnt");
    lbl->setScale(0.6f);
    lbl->setPosition({content.width / 2, content.height - 60.f});
    m_mainLayer->addChild(lbl);

    m_input = TextInput::create(240.f, Localization::get().getString("ban.popup.placeholder"));
    m_input->setPosition({content.width / 2, content.height / 2});
    m_mainLayer->addChild(m_input);

    auto btnSpr = ButtonSprite::create(Localization::get().getString("ban.popup.ban_btn").c_str(), "goldFont.fnt", "GJ_button_01.png", .8f);
    auto btn = CCMenuItemSpriteExtra::create(btnSpr, this, menu_selector(BanUserPopup::onBan));
    
    auto menu = CCMenu::create();
    menu->addChild(btn);
    menu->setPosition({content.width / 2, 40.f});
    m_mainLayer->addChild(menu);

    return true;
}

void BanUserPopup::onBan(CCObject*) {
    std::string reason = m_input->getString();
    if (reason.empty()) {
        Notification::create(Localization::get().getString("ban.popup.enter_reason"), NotificationIcon::Error)->show();
        return;
    }

    auto loading = LoadingCircle::create();
    loading->setParentLayer(this);
    loading->setFade(true);
    loading->show();

    WeakRef<BanUserPopup> self = this;
    HttpClient::get().banUser(m_username, reason, [self, loading](bool success, std::string msg) {
        if (auto popup = self.lock()) {
            if (loading) loading->fadeAndRemove();
            if (success) {
                Notification::create(Localization::get().getString("ban.popup.success"), NotificationIcon::Success)->show();
                popup->onClose(nullptr);
            } else {
                Notification::create(Localization::get().getString("ban.popup.error"), NotificationIcon::Error)->show();
            }
        }
    });
}

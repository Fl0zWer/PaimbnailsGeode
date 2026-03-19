#include "RatePopup.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/GJAccountManager.hpp>

using namespace geode::prelude;

RatePopup* RatePopup::create(int levelID, std::string thumbnailId) {
    auto ret = new RatePopup();
    if (ret && ret->init(levelID, thumbnailId)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool RatePopup::init(int levelID, std::string thumbnailId) {
    if (!Popup::init(300.f, 200.f)) return false;

    m_levelID = levelID;
    m_thumbnailId = thumbnailId;
    
    this->setTitle("Rate Thumbnail");

    auto menu = CCMenu::create();
    menu->setID("stars-menu"_spr);
    menu->setPosition({m_mainLayer->getContentSize().width / 2, m_mainLayer->getContentSize().height / 2});
    m_mainLayer->addChild(menu);

    float startX = -60;
    for (int i = 1; i <= 5; i++) {
        auto offSpr = CCSprite::createWithSpriteFrameName("GJ_starBtn_001.png");
        offSpr->setColor({100, 100, 100});
        offSpr->setScale(0.8f);
        
        auto onSpr = CCSprite::createWithSpriteFrameName("GJ_starBtn_001.png");
        onSpr->setScale(0.8f);
        
        auto btn = CCMenuItemSpriteExtra::create(offSpr, this, menu_selector(RatePopup::onStar));
        btn->setTag(i);
        btn->setPosition({startX + (i - 1) * 30.0f, 10});
        menu->addChild(btn);
        m_starBtns.push_back(btn);
    }

    auto submitSpr = ButtonSprite::create("Submit", "goldFont.fnt", "GJ_button_01.png", 0.8f);
    auto submitBtn = CCMenuItemSpriteExtra::create(submitSpr, this, menu_selector(RatePopup::onSubmit));
    submitBtn->setID("submit-btn"_spr);
    submitBtn->setPosition({0, -50});
    menu->addChild(submitBtn);

    paimon::markDynamicPopup(this);
    return true;
}

void RatePopup::onStar(CCObject* sender) {
    auto btn = typeinfo_cast<CCMenuItemSpriteExtra*>(sender);
    if (!btn) return;
    m_rating = btn->getTag();
    
    for (int i = 0; i < 5; i++) {
        auto b = m_starBtns[i];
        if (auto spr = typeinfo_cast<CCSprite*>(b->getNormalImage())) {
            if (i < m_rating) {
                spr->setColor({255, 255, 255});
            } else {
                spr->setColor({100, 100, 100});
            }
        }
    }
}

void RatePopup::onSubmit(CCObject* sender) {
    if (m_rating == 0) {
        PaimonNotify::create("Please select a rating", NotificationIcon::Error)->show();
        return;
    }
    
    if (GJAccountManager::get()->m_accountID <= 0) {
        PaimonNotify::create("You must be logged in to vote", NotificationIcon::Error)->show();
        return;
    }

    std::string username = "";
    if (auto gm = GameManager::get()) {
        username = gm->m_playerName;
    }
    
    // spinner mientras manda el voto
    auto spinner = geode::LoadingSpinner::create(30.f);
    spinner->setPosition(m_mainLayer->getContentSize() / 2);
    spinner->setID("paimon-loading-spinner"_spr);
    m_mainLayer->addChild(spinner, 100);

    // desactivo btn para no spamear
    auto btn = typeinfo_cast<CCMenuItemSpriteExtra*>(sender);
    if (btn) btn->setEnabled(false);
    
    // weakref por si async
    WeakRef<RatePopup> self = this;
    Ref<geode::LoadingSpinner> spinnerRef = spinner;
    ThumbnailAPI::get().submitVote(m_levelID, m_rating, username, m_thumbnailId, [self, spinnerRef, btn](bool success, std::string const& msg) {
        if (auto popup = self.lock()) {
            if (spinnerRef) spinnerRef->removeFromParent();
            
            if (success) {
                PaimonNotify::create("Rating submitted!", NotificationIcon::Success)->show();
                if (popup->m_onRateCallback) {
                    popup->m_onRateCallback();
                }
                popup->onClose(nullptr);
            } else {
                if (btn) btn->setEnabled(true);
                // msg del server si hay
                std::string errorMsg = "Failed to submit rating";
                if (!msg.empty()) {
                    errorMsg += ": " + msg;
                }
                PaimonNotify::create(errorMsg.c_str(), NotificationIcon::Error)->show();
            }
        }
    });
}

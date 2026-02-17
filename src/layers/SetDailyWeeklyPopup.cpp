#include "SetDailyWeeklyPopup.hpp"
#include "../utils/HttpClient.hpp"
#include <Geode/utils/web.hpp>

using namespace geode::prelude;

bool SetDailyWeeklyPopup::init(int levelID) {
    m_levelID = levelID;
    
    // inicia popup con tamaño
    if (!Popup::init(300.f, 220.f)) return false;

    this->setTitle("Set Daily/Weekly");

    // no tocar m_buttonMenu (tiene X). menu separado para contenido.
    
    auto actionMenu = CCMenu::create();
    actionMenu->setPosition(this->getContentSize() / 2);
    actionMenu->setContentSize({ 200.f, 160.f }); // ancho, alto contenedor
    actionMenu->ignoreAnchorPointForPosition(false);
    
    // columnlayout
    actionMenu->setLayout(
        ColumnLayout::create()
            ->setGap(10.f)
            ->setAxisReverse(true) // de arriba a abajo
    );
    
    this->addChild(actionMenu);

    // btn daily
    // cast explicito a ccobject* por si acaso, aunque innecesario usualmente
    auto dailyBtn = CCMenuItemSpriteExtra::create(
        ButtonSprite::create("Set Daily", 0, false, "goldFont.fnt", "GJ_button_01.png", 0, 1.0f),
        this,
        menu_selector(SetDailyWeeklyPopup::onSetDaily)
    );
    actionMenu->addChild(dailyBtn);

    // btn weekly
    auto weeklyBtn = CCMenuItemSpriteExtra::create(
        ButtonSprite::create("Set Weekly", 0, false, "goldFont.fnt", "GJ_button_01.png", 0, 1.0f),
        this,
        menu_selector(SetDailyWeeklyPopup::onSetWeekly)
    );
    actionMenu->addChild(weeklyBtn);
    
    // btn unset
    auto unsetBtn = CCMenuItemSpriteExtra::create(
        ButtonSprite::create("Unset", 0, false, "goldFont.fnt", "GJ_button_06.png", 0, 1.0f),
        this,
        menu_selector(SetDailyWeeklyPopup::onUnset)
    );
    unsetBtn->setScale(0.8f);
    actionMenu->addChild(unsetBtn);

    // layout menu
    actionMenu->updateLayout();

    return true;
}

SetDailyWeeklyPopup* SetDailyWeeklyPopup::create(int levelID) {
    auto ret = new SetDailyWeeklyPopup();
    if (ret && ret->init(levelID)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

void SetDailyWeeklyPopup::onSetDaily(CCObject* sender) {
    createQuickPopup(
        "Confirm",
        "Set this level as <cy>Daily</c>?",
        "Cancel", "Set",
        [this](auto, bool btn2) {
            if (btn2) {
                auto gm = GameManager::get();
                std::string username = gm->m_playerName;
                int accountID = GJAccountManager::get()->m_accountID;

                matjson::Value json = matjson::makeObject({
                    {"levelID", m_levelID},
                    {"username", username},
                    {"accountID", accountID}
                });
                
                Notification::create("Setting daily...", NotificationIcon::Info)->show();
                
                // weakref para no crash si cierra antes
                WeakRef<SetDailyWeeklyPopup> self = this;
                
                HttpClient::get().post("/api/daily/set", json.dump(), [self](bool success, const std::string& msg) {
                    if (auto popup = self.lock()) {
                        if (success) {
                            Notification::create("Daily set successfully", NotificationIcon::Success)->show();
                            popup->onClose(nullptr);
                        } else {
                            Notification::create("Failed to set daily: " + msg, NotificationIcon::Error)->show();
                        }
                    }
                });
            }
        }
    );
}

void SetDailyWeeklyPopup::onSetWeekly(CCObject* sender) {
    createQuickPopup(
        "Confirm",
        "Set this level as <cy>Weekly</c>?",
        "Cancel", "Set",
        [this](auto, bool btn2) {
            if (btn2) {
                auto gm = GameManager::get();
                std::string username = gm->m_playerName;
                int accountID = GJAccountManager::get()->m_accountID;

                matjson::Value json = matjson::makeObject({
                    {"levelID", m_levelID},
                    {"username", username},
                    {"accountID", accountID}
                });
                
                Notification::create("Setting weekly...", NotificationIcon::Info)->show();
                
                // usar weakref por seguridad
                WeakRef<SetDailyWeeklyPopup> self = this;

                HttpClient::get().post("/api/weekly/set", json.dump(), [self](bool success, const std::string& msg) {
                    if (auto popup = self.lock()) {
                        if (success) {
                            Notification::create("Weekly set successfully", NotificationIcon::Success)->show();
                            popup->onClose(nullptr);
                        } else {
                            Notification::create("Failed to set weekly: " + msg, NotificationIcon::Error)->show();
                        }
                    }
                });
            }
        }
    );
}

void SetDailyWeeklyPopup::onUnset(CCObject* sender) {
    createQuickPopup(
        "Confirm",
        "Unset this level from Daily/Weekly?",
        "Cancel", "Unset",
        [this](auto, bool btn2) {
             if (btn2) {
                 auto gm = GameManager::get();
                 std::string username = gm->m_playerName;

                 matjson::Value json = matjson::makeObject({
                    {"levelID", m_levelID},
                    {"type", "unset"},
                    {"username", username}
                });
                
                Notification::create("Unsetting...", NotificationIcon::Info)->show();
                
                WeakRef<SetDailyWeeklyPopup> self = this;

                HttpClient::get().post("/api/admin/set-daily", json.dump(), [self](bool success, const std::string& msg) {
                    if (auto popup = self.lock()) {
                        if (success) {
                            Notification::create("Unset successfully", NotificationIcon::Success)->show();
                            popup->onClose(nullptr);
                        } else {
                            Notification::create("Failed to unset: " + msg, NotificationIcon::Error)->show();
                        }
                    }
                });
            }
        }
    );
}

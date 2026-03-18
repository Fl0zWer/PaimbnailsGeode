#include "RateProfilePopup.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "ReportUserPopup.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/GJAccountManager.hpp>

using namespace geode::prelude;

RateProfilePopup* RateProfilePopup::create(int accountID, std::string const& targetUsername) {
    auto ret = new RateProfilePopup();
    if (ret && ret->init(accountID, targetUsername)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool RateProfilePopup::init(int accountID, std::string const& targetUsername) {
    if (!Popup::init(340.f, 240.f)) return false;

    m_accountID = accountID;
    m_targetUsername = targetUsername;

    this->setTitle("Rate Profile");

    auto contentSize = m_mainLayer->getContentSize();
    float centerX = contentSize.width / 2.f;

    // --- average display ---
    m_averageLabel = CCLabelBMFont::create("...", "bigFont.fnt");
    m_averageLabel->setScale(0.5f);
    m_averageLabel->setPosition({centerX, contentSize.height - 52.f});
    m_averageLabel->setColor({255, 215, 0});
    m_averageLabel->setID("average-label"_spr);
    m_mainLayer->addChild(m_averageLabel);

    m_countLabel = CCLabelBMFont::create("Loading...", "chatFont.fnt");
    m_countLabel->setScale(0.55f);
    m_countLabel->setPosition({centerX, contentSize.height - 67.f});
    m_countLabel->setColor({180, 180, 180});
    m_countLabel->setID("count-label"_spr);
    m_mainLayer->addChild(m_countLabel);

    // --- star buttons 1-5 ---
    auto starMenu = CCMenu::create();
    starMenu->setID("stars-menu"_spr);
    starMenu->setPosition({centerX, contentSize.height - 100.f});
    m_mainLayer->addChild(starMenu);

    for (int i = 1; i <= 5; i++) {
        auto spr = CCSprite::createWithSpriteFrameName("GJ_starsIcon_001.png");
        if (!spr) spr = CCSprite::createWithSpriteFrameName("GJ_starBtn_001.png");
        if (!spr) spr = CCSprite::create();
        spr->setColor({100, 100, 100});
        spr->setScale(0.7f);

        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(RateProfilePopup::onStar));
        btn->setTag(i);

        float col = (i - 3.f);  // -2, -1, 0, 1, 2
        btn->setPosition({col * 36.f, 0.f});

        starMenu->addChild(btn);
        m_starBtns.push_back(btn);
    }

    // selected rating label
    m_selectedLabel = CCLabelBMFont::create("", "bigFont.fnt");
    m_selectedLabel->setScale(0.35f);
    m_selectedLabel->setPosition({centerX, contentSize.height - 135.f});
    m_selectedLabel->setColor({255, 255, 255});
    m_selectedLabel->setID("selected-label"_spr);
    m_mainLayer->addChild(m_selectedLabel);

    // --- message input ---
    m_messageInput = TextInput::create(280.f, "Leave a message (optional)", "chatFont.fnt");
    m_messageInput->setID("message-input"_spr);
    m_messageInput->setPosition({centerX, contentSize.height - 160.f});
    m_messageInput->setMaxCharCount(150);
    m_messageInput->setScale(0.8f);
    m_mainLayer->addChild(m_messageInput);

    // --- submit button ---
    auto submitMenu = CCMenu::create();
    submitMenu->setID("submit-menu"_spr);
    submitMenu->setPosition({centerX, contentSize.height - 195.f});
    m_mainLayer->addChild(submitMenu);

    auto submitSpr = ButtonSprite::create("Submit", "goldFont.fnt", "GJ_button_01.png", 0.8f);
    auto submitBtn = CCMenuItemSpriteExtra::create(submitSpr, this, menu_selector(RateProfilePopup::onSubmit));
    submitBtn->setID("submit-btn"_spr);
    submitBtn->setPosition({0, 0});
    submitMenu->addChild(submitBtn);

    // --- report button ---
    auto reportSpr = ButtonSprite::create("Report", "bigFont.fnt", "GJ_button_06.png", 0.6f);
    reportSpr->setScale(0.65f);
    auto reportBtn = CCMenuItemSpriteExtra::create(reportSpr, this, menu_selector(RateProfilePopup::onReport));
    reportBtn->setID("report-btn"_spr);
    reportBtn->setPosition({0, -30.f});
    submitMenu->addChild(reportBtn);

    // load existing rating data
    loadExistingRating();

    paimon::markDynamicPopup(this);
    return true;
}

void RateProfilePopup::onStar(CCObject* sender) {
    auto btn = typeinfo_cast<CCMenuItemSpriteExtra*>(sender);
    if (!btn) return;
    m_rating = btn->getTag();
    updateStarVisuals();
}

void RateProfilePopup::updateStarVisuals() {
    for (int i = 0; i < (int)m_starBtns.size(); i++) {
        auto b = m_starBtns[i];
        if (auto spr = typeinfo_cast<CCSprite*>(b->getNormalImage())) {
            if (i < m_rating) {
                spr->setColor({255, 255, 50});
            } else {
                spr->setColor({100, 100, 100});
            }
        }
    }
    if (m_selectedLabel) {
        if (m_rating > 0) {
            m_selectedLabel->setString(fmt::format("{}/5", m_rating).c_str());
        } else {
            m_selectedLabel->setString("");
        }
    }
}

void RateProfilePopup::loadExistingRating() {
    std::string username;
    if (auto gm = GameManager::get()) {
        username = gm->m_playerName;
    }

    std::string endpoint = fmt::format(
        "/api/profile-ratings/{}?username={}",
        m_accountID,
        HttpClient::encodeQueryParam(username)
    );

    WeakRef<RateProfilePopup> self = this;
    HttpClient::get().get(endpoint, [self](bool ok, std::string const& resp) {
        auto popup = self.lock();
        if (!popup) return;

        if (!ok) {
            if (popup->m_countLabel) popup->m_countLabel->setString("No ratings yet");
            if (popup->m_averageLabel) popup->m_averageLabel->setString("0.0");
            return;
        }

        auto parsed = matjson::parse(resp);
        if (!parsed.isOk()) return;
        auto root = parsed.unwrap();

        float avg = 0.f;
        int count = 0;

        if (root["average"].isNumber()) avg = static_cast<float>(root["average"].asDouble().unwrapOr(0.0));
        if (root["count"].isNumber()) count = static_cast<int>(root["count"].asInt().unwrapOr(0));

        popup->m_currentAverage = avg;
        popup->m_totalVotes = count;

        if (popup->m_averageLabel) {
            popup->m_averageLabel->setString(fmt::format("{:.1f}/5", avg).c_str());
        }
        if (popup->m_countLabel) {
            if (count == 0) {
                popup->m_countLabel->setString("No ratings yet");
            } else {
                popup->m_countLabel->setString(fmt::format("{} rating{}", count, count == 1 ? "" : "s").c_str());
            }
        }

        // Restore user's existing vote if any
        if (root["userVote"].isObject()) {
            auto uv = root["userVote"];
            if (uv["stars"].isNumber()) {
                popup->m_rating = static_cast<int>(uv["stars"].asInt().unwrapOr(0));
                popup->updateStarVisuals();
            }
            if (uv["message"].isString() && popup->m_messageInput) {
                popup->m_messageInput->setString(uv["message"].asString().unwrapOr(""));
            }
        }
    });
}

void RateProfilePopup::onSubmit(CCObject* sender) {
    if (m_rating == 0) {
        PaimonNotify::create("Select a rating first", NotificationIcon::Error)->show();
        return;
    }

    if (GJAccountManager::get()->m_accountID <= 0) {
        PaimonNotify::create("You must be logged in to rate", NotificationIcon::Error)->show();
        return;
    }

    std::string username;
    if (auto gm = GameManager::get()) {
        username = gm->m_playerName;
    }

    // loading
    auto spinner = geode::LoadingSpinner::create(30.f);
    spinner->setPosition(m_mainLayer->getContentSize() / 2);
    spinner->setID("paimon-loading-spinner"_spr);
    m_mainLayer->addChild(spinner, 100);

    auto btn = typeinfo_cast<CCMenuItemSpriteExtra*>(sender);
    if (btn) btn->setEnabled(false);

    std::string message = m_messageInput ? m_messageInput->getString() : "";

    // Build JSON body safely (matjson handles escaping)
    matjson::Value bodyObj = matjson::makeObject({
        {"accountID", m_accountID},
        {"stars", m_rating},
        {"username", username},
        {"message", message}
    });
    auto body = bodyObj.dump();

    WeakRef<RateProfilePopup> self = this;
    Ref<geode::LoadingSpinner> spinnerRef = spinner;
    HttpClient::get().post("/api/profile-ratings/vote", body, [self, spinnerRef, btn](bool success, std::string const& msg) {
        auto popup = self.lock();
        if (!popup) return;

        if (spinnerRef) spinnerRef->removeFromParent();

        if (success) {
            auto parsed = matjson::parse(msg);
            if (parsed.isOk()) {
                auto root = parsed.unwrap();
                if (root["error"].isString()) {
                    if (btn) btn->setEnabled(true);
                    PaimonNotify::create(root["error"].asString().unwrapOr("Unknown error"), NotificationIcon::Error)->show();
                    return;
                }
                bool updated = false;
                if (root["updated"].isBool()) updated = root["updated"].asBool().unwrapOr(false);
                if (updated) {
                    PaimonNotify::create("Rating updated!", NotificationIcon::Success)->show();
                } else {
                    PaimonNotify::create("Rating submitted!", NotificationIcon::Success)->show();
                }
                popup->onClose(nullptr);
                return;
            }

            PaimonNotify::create("Rating submitted!", NotificationIcon::Success)->show();
            popup->onClose(nullptr);
        } else {
            if (btn) btn->setEnabled(true);
            std::string errorMsg = "Failed to submit rating";
            if (!msg.empty()) {
                auto parsed = matjson::parse(msg);
                if (parsed.isOk()) {
                    auto root = parsed.unwrap();
                    if (root["error"].isString()) {
                        errorMsg = root["error"].asString().unwrapOr(errorMsg);
                    }
                } else {
                    errorMsg += ": " + msg;
                }
            }
            PaimonNotify::create(errorMsg.c_str(), NotificationIcon::Error)->show();
        }
    });
}

void RateProfilePopup::onReport(CCObject* sender) {
    auto popup = ReportUserPopup::create(m_accountID, m_targetUsername);
    if (popup) popup->show();
}

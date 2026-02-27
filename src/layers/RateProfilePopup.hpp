#pragma once
#include <Geode/Geode.hpp>
#include <Geode/ui/LoadingSpinner.hpp>
#include <Geode/ui/TextInput.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include "../utils/HttpClient.hpp"

using namespace geode::prelude;

class RateProfilePopup : public geode::Popup {
protected:
    int m_accountID;
    std::string m_targetUsername;
    int m_rating = 0;
    float m_currentAverage = 0.f;
    int m_totalVotes = 0;
    std::vector<CCMenuItemSpriteExtra*> m_starBtns;
    geode::TextInput* m_messageInput = nullptr;
    CCLabelBMFont* m_averageLabel = nullptr;
    CCLabelBMFont* m_countLabel = nullptr;
    CCLabelBMFont* m_selectedLabel = nullptr;

    bool init(int accountID, const std::string& targetUsername);
    void onStar(cocos2d::CCObject* sender);
    void onSubmit(cocos2d::CCObject* sender);
    void updateStarVisuals();
    void loadExistingRating();

public:
    static RateProfilePopup* create(int accountID, const std::string& targetUsername);
};

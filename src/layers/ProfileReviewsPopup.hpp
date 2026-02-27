#pragma once
#include <Geode/Geode.hpp>
#include <Geode/ui/LoadingSpinner.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include "../utils/HttpClient.hpp"

using namespace geode::prelude;

class ProfileReviewsPopup : public geode::Popup {
protected:
    int m_accountID;
    cocos2d::CCLabelBMFont* m_averageLabel = nullptr;
    cocos2d::CCLabelBMFont* m_countLabel = nullptr;
    cocos2d::extension::CCScrollView* m_scrollView = nullptr;
    CCNode* m_scrollContent = nullptr;
    geode::LoadingSpinner* m_spinner = nullptr;

    bool init(int accountID);
    void loadReviews();
    void buildReviewList(float average, int count, const matjson::Value& reviews);
    CCNode* createReviewCell(const std::string& username, int stars, const std::string& message, float width);

public:
    static ProfileReviewsPopup* create(int accountID);
};

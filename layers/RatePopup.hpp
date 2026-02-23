#pragma once
#include <Geode/Geode.hpp>
#include "../managers/ThumbnailAPI.hpp"

class RatePopup : public geode::Popup {
protected:
    int m_levelID;
    std::string m_thumbnailId;
    int m_rating = 0;
    std::vector<CCMenuItemSpriteExtra*> m_starBtns;

    bool init(int levelID, std::string thumbnailId);
    void onStar(cocos2d::CCObject* sender);
    void onSubmit(cocos2d::CCObject* sender);

public:
    std::function<void()> m_onRateCallback;
    static RatePopup* create(int levelID, std::string thumbnailId);
};

#include <Geode/Geode.hpp>
#include <Geode/modify/LevelSearchLayer.hpp>
#include "../layers/LeaderboardLayer.hpp"

using namespace geode::prelude;

class $modify(MyLevelSearchLayer, LevelSearchLayer) {
    bool init(int searchType) {
        if (!LevelSearchLayer::init(searchType)) return false;

        CCSprite* spr = CCSprite::create("paim_Daily.png"_spr);
        if (!spr) {
            spr = CCSprite::createWithSpriteFrameName("GJ_trophyBtn_001.png");
        } else {
            float targetSize = 35.0f;
            float currentSize = std::max(spr->getContentWidth(), spr->getContentHeight());
            if (currentSize > 0) {
                spr->setScale(targetSize / currentSize);
            }
        }

        auto btn = CCMenuItemSpriteExtra::create(
            spr,
            this,
            menu_selector(MyLevelSearchLayer::onLeaderboard)
        );
        btn->setID("paimon-leaderboard-btn"_spr);

        if (auto menu = this->getChildByID("other-filter-menu")) {
            menu->addChild(btn);
            menu->updateLayout();
        } else {
            log::warn("Could not find 'other-filter-menu' in LevelSearchLayer");
        }

        return true;
    }

    void onLeaderboard(CCObject*) {
        CCDirector::sharedDirector()->replaceScene(CCTransitionFade::create(0.5f, LeaderboardLayer::scene(LeaderboardLayer::BackTarget::LevelSearchLayer)));
    }
};

#include <Geode/Geode.hpp>
#include <Geode/modify/LevelSearchLayer.hpp>
#include "../features/community/ui/LeaderboardLayer.hpp"
#include "../features/backgrounds/services/LayerBackgroundManager.hpp"
#include "../features/transitions/services/TransitionManager.hpp"

using namespace geode::prelude;

class $modify(MyLevelSearchLayer, LevelSearchLayer) {
    static void onModify(auto& self) {
        (void)self.setHookPriorityPost("LevelSearchLayer::init", geode::Priority::Late);
    }

    $override
    bool init(int searchType) {
        if (!LevelSearchLayer::init(searchType)) return false;

        // ── Aplicar fondo custom unificado ──
        bool hasCustomBg = LayerBackgroundManager::get().applyBackground(this, "search");

        // ── Si hay fondo custom, ocultar sprites decorativos de busqueda ──
        if (hasCustomBg) {
            static char const* hideIDs[] = {
                "level-search-bg",
                "quick-search-bg",
                "difficulty-filters-bg",
                "length-filters-bg",
                nullptr
            };
            for (int i = 0; hideIDs[i]; i++) {
                if (auto node = this->getChildByID(hideIDs[i])) {
                    node->setVisible(false);
                }
            }
        }

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
        TransitionManager::get().replaceScene(LeaderboardLayer::scene(LeaderboardLayer::BackTarget::LevelSearchLayer));
    }
};

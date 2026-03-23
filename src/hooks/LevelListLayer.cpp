#include <Geode/Geode.hpp>
#include <Geode/modify/LevelListLayer.hpp>
#include <Geode/modify/LevelBrowserLayer.hpp>
#include <Geode/binding/LevelCell.hpp>
#include "../framework/state/SessionState.hpp"
#include "../features/thumbnails/services/ThumbnailLoader.hpp"
#include "../features/thumbnails/ui/LevelCellSettingsPopup.hpp"
#include "../features/backgrounds/services/LayerBackgroundManager.hpp"
#include "../utils/SpriteHelper.hpp"
#include <unordered_set>
#include <vector>

using namespace geode::prelude;

namespace {
void collectVisibleLevelCellIDs(CCNode* node, std::vector<int>& levelIDs, std::unordered_set<int>& seen) {
    if (!node || !node->isVisible()) {
        return;
    }

    if (auto* levelCell = typeinfo_cast<LevelCell*>(node)) {
        auto* level = levelCell->m_level;
        if (level) {
            int levelID = level->m_levelID.value();
            if (levelID > 0 && seen.insert(levelID).second) {
                levelIDs.push_back(levelID);
            }
        }
    }

    auto* children = node->getChildren();
    if (!children) {
        return;
    }

    for (auto* child : CCArrayExt<CCNode*>(children)) {
        collectVisibleLevelCellIDs(child, levelIDs, seen);
    }
}
}

class $modify(PaimonLevelListLayer, LevelListLayer) {
    static void onModify(auto& self) {
        // Pre: capturar el ID de lista ANTES de que init() cree los nodos
        (void)self.setHookPriorityPre("LevelListLayer::init", geode::Priority::Normal);
    }

    $override
    bool init(GJLevelList* list) {
        // guardar id pa usarlo en LevelInfoLayer
        if (list) {
            paimon::SessionState::get().currentListID = list->m_listID;
            log::debug("Entered List: {}", list->m_listID);
        } else {
            paimon::SessionState::get().currentListID = 0;
        }

        return LevelListLayer::init(list);
    }
};

class $modify(ContextTrackingBrowser, LevelBrowserLayer) {
    static void onModify(auto& self) {
        // AfterPost: correr despues de geode.node-ids para acceder a IDs de nodos
        (void)self.setHookPriorityAfterPost("LevelBrowserLayer::init", "geode.node-ids");
    }

    $override
    bool init(GJSearchObject* p0) {
        // limpiar contexto al entrar a busqueda normal
        paimon::SessionState::get().currentListID = 0;
        if (!LevelBrowserLayer::init(p0)) return false;

        // fondo custom
        LayerBackgroundManager::get().applyBackground(this, "browser");

        // engranaje de settings
        addSettingsGearButton();

        this->schedule(schedule_selector(ContextTrackingBrowser::prefetchVisibleLevelCells), 0.35f);
        this->scheduleOnce(schedule_selector(ContextTrackingBrowser::prefetchVisibleLevelCells), 0.0f);

        return true;
    }

    $override
    void onExit() {
        this->unschedule(schedule_selector(ContextTrackingBrowser::prefetchVisibleLevelCells));
        LevelBrowserLayer::onExit();
    }

    CCMenuItemSpriteExtra* createGearButton(float sprScale) {
        auto spr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_optionsBtn_001.png");
        if (!spr) spr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_optionsBtn02_001.png");
        if (!spr) return nullptr;
        spr->setScale(sprScale);
        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(ContextTrackingBrowser::onLevelCellSettings));
        btn->setID("paimon-levelcell-settings-btn"_spr);
        return btn;
    }

    void addSettingsGearButton() {
        CCMenu* searchMenu = nullptr;
        if (auto node = this->getChildByID("search-menu")) {
            searchMenu = typeinfo_cast<CCMenu*>(node);
        }

        // si no hay search-menu, buscarlo entre los menus hijos
        if (!searchMenu) {
            for (auto* child : CCArrayExt<CCNode*>(this->getChildren())) {
                if (auto menu = typeinfo_cast<CCMenu*>(child)) {
                    if (menu->getPosition().y > CCDirector::sharedDirector()->getWinSize().height * 0.7f) {
                        searchMenu = menu;
                        break;
                    }
                }
            }
        }

        if (!searchMenu) {
            auto winSize = CCDirector::sharedDirector()->getWinSize();
            auto gearMenu = CCMenu::create();
            gearMenu->setPosition({0, 0});
            gearMenu->setID("paimon-levelcell-settings-menu"_spr);
            this->addChild(gearMenu, 100);

            if (auto btn = createGearButton(0.45f)) {
                btn->setPosition({winSize.width - 30.f, winSize.height - 30.f});
                gearMenu->addChild(btn);
            }
            return;
        }

        auto gearBtn = createGearButton(0.5f);
        if (!gearBtn) return;

        if (searchMenu->getLayout()) {
            searchMenu->addChild(gearBtn);
            searchMenu->updateLayout();
        } else {
            float rightMostX = 0.f;
            if (auto children = searchMenu->getChildren()) {
                for (auto* child : CCArrayExt<CCNode*>(children)) {
                    float r = child->getPositionX() + child->getContentSize().width * child->getScaleX() * 0.5f;
                    if (r > rightMostX) rightMostX = r;
                }
            }
            gearBtn->setPosition({rightMostX + 25.f, searchMenu->getContentSize().height / 2.f});
            searchMenu->addChild(gearBtn);
        }
    }

    void onLevelCellSettings(CCObject*) {
        auto popup = LevelCellSettingsPopup::create();
        if (!popup) return;

        popup->setOnSettingsChanged([]() {
            log::info("[LevelBrowserLayer] LevelCell settings changed, will apply on next cell load");
        });

        popup->show();
    }

    void prefetchVisibleLevelCells(float) {
        std::vector<int> levelIDs;
        std::unordered_set<int> seen;
        collectVisibleLevelCellIDs(this, levelIDs, seen);

        if (levelIDs.empty()) {
            return;
        }

        ThumbnailLoader::get().prefetchLevels(levelIDs, 3);
    }

};

// NOTE: limpieza de current-list-id esta en MenuLayer::init

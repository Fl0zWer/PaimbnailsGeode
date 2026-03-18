#include <Geode/Geode.hpp>
#include <Geode/modify/LevelListLayer.hpp>
#include <Geode/modify/LevelBrowserLayer.hpp>
#include "../framework/state/SessionState.hpp"
#include "../features/thumbnails/services/ThumbnailLoader.hpp"
#include "../features/thumbnails/ui/LevelCellSettingsPopup.hpp"
#include "../features/backgrounds/services/LayerBackgroundManager.hpp"
#include "../utils/SpriteHelper.hpp"

using namespace geode::prelude;

class $modify(PaimonLevelListLayer, LevelListLayer) {
    static void onModify(auto& self) {
        (void)self.setHookPriorityPre("LevelListLayer::init", geode::Priority::Normal);
    }

    $override
    bool init(GJLevelList* list) {
        // guardar id de lista pa LevelInfoLayer
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
        (void)self.setHookPriorityAfterPost("LevelBrowserLayer::init", "geode.node-ids");
    }

    $override
    bool init(GJSearchObject* p0) {
        // limpiar id de lista al entrar al browser normal (busqueda, etc)
        paimon::SessionState::get().currentListID = 0;
        if (!LevelBrowserLayer::init(p0)) return false;

        // ── Aplicar fondo custom unificado ──
        LayerBackgroundManager::get().applyBackground(this, "browser");

        // ── Boton engranaje en search-menu ──
        addSettingsGearButton();

        return true;
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

        // fallback: buscar entre los menus hijos
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

};

// NOTE: current-list-id cleanup moved to main MenuLayer hook (main.cpp) to avoid double-hooking MenuLayer::init

#include <Geode/Geode.hpp>
#include <Geode/modify/LevelListLayer.hpp>
#include <Geode/modify/LevelBrowserLayer.hpp>
#include "../managers/ThumbnailLoader.hpp"
#include "../layers/LevelCellSettingsPopup.hpp"
#include "../managers/LayerBackgroundManager.hpp"

using namespace geode::prelude;

class $modify(PaimonLevelListLayer, LevelListLayer) {
    bool init(GJLevelList* list) {
        // guardar id de lista pa LevelInfoLayer
        if (list) {
            Mod::get()->setSavedValue("current-list-id", list->m_listID);
            log::debug("Entered List: {}", list->m_listID);
        } else {
            Mod::get()->setSavedValue("current-list-id", 0);
        }

        return LevelListLayer::init(list);
    }

    void onEnter() {
        LevelListLayer::onEnter();
    }

    void onExit() {
        LevelListLayer::onExit();
    }
};

class $modify(ContextTrackingBrowser, LevelBrowserLayer) {
    static void onModify(auto& self) {
        (void)self.setHookPriorityPost("LevelBrowserLayer::init", geode::Priority::Late);
    }

    bool init(GJSearchObject* p0) {
        // limpiar id de lista al entrar al browser normal (busqueda, etc)
        Mod::get()->setSavedValue("current-list-id", 0);
        if (!LevelBrowserLayer::init(p0)) return false;

        // ── Aplicar fondo custom unificado ──
        LayerBackgroundManager::get().applyBackground(this, "browser");

        // ── Boton engranaje en search-menu ──
        addSettingsGearButton();

        return true;
    }

    void addSettingsGearButton() {
        // buscar el search-menu por ID (geode.node-ids)
        CCMenu* searchMenu = nullptr;

        // intentar por ID primero
        if (auto node = this->getChildByID("search-menu")) {
            searchMenu = typeinfo_cast<CCMenu*>(node);
        }

        // fallback: buscar entre los menus hijos
        if (!searchMenu) {
            auto children = this->getChildren();
            if (children) {
                for (auto* child : CCArrayExt<CCNode*>(children)) {
                    if (auto menu = typeinfo_cast<CCMenu*>(child)) {
                        // el search-menu suele estar en la parte superior
                        auto pos = menu->getPosition();
                        auto winSize = CCDirector::sharedDirector()->getWinSize();
                        if (pos.y > winSize.height * 0.7f) {
                            searchMenu = menu;
                            break;
                        }
                    }
                }
            }
        }

        if (!searchMenu) {
            log::warn("[LevelBrowserLayer] search-menu not found, creating gear menu standalone");
            // crear menu propio si no se encontro search-menu
            auto winSize = CCDirector::sharedDirector()->getWinSize();
            auto gearMenu = CCMenu::create();
            gearMenu->setPosition({0, 0});
            gearMenu->setID("paimon-levelcell-settings-menu"_spr);
            this->addChild(gearMenu, 100);

            auto gearSpr = CCSprite::createWithSpriteFrameName("GJ_optionsBtn_001.png");
            if (!gearSpr) gearSpr = CCSprite::createWithSpriteFrameName("GJ_optionsBtn02_001.png");
            if (gearSpr) {
                gearSpr->setScale(0.45f);
                auto gearBtn = CCMenuItemSpriteExtra::create(
                    gearSpr, this,
                    menu_selector(ContextTrackingBrowser::onLevelCellSettings)
                );
                gearBtn->setPosition({winSize.width - 30.f, winSize.height - 30.f});
                gearBtn->setID("paimon-levelcell-settings-btn"_spr);
                gearMenu->addChild(gearBtn);
            }
            return;
        }

        // anadir el boton al search-menu existente
        auto gearSpr = CCSprite::createWithSpriteFrameName("GJ_optionsBtn_001.png");
        if (!gearSpr) gearSpr = CCSprite::createWithSpriteFrameName("GJ_optionsBtn02_001.png");
        if (!gearSpr) {
            log::warn("[LevelBrowserLayer] Could not create gear sprite");
            return;
        }
        gearSpr->setScale(0.5f);

        auto gearBtn = CCMenuItemSpriteExtra::create(
            gearSpr, this,
            menu_selector(ContextTrackingBrowser::onLevelCellSettings)
        );
        gearBtn->setID("paimon-levelcell-settings-btn"_spr);

        // posicionar a la derecha del menu
        auto menuSize = searchMenu->getContentSize();
        // buscar el ultimo hijo para posicionar despues
        float rightMostX = 0.f;
        if (auto children = searchMenu->getChildren()) {
            for (auto* child : CCArrayExt<CCNode*>(children)) {
                float childRight = child->getPositionX() + child->getContentSize().width * child->getScaleX() * 0.5f;
                if (childRight > rightMostX) rightMostX = childRight;
            }
        }

        // si el menu tiene layout, agregar y actualizar
        if (searchMenu->getLayout()) {
            searchMenu->addChild(gearBtn);
            searchMenu->updateLayout();
        } else {
            // posicion manual: a la derecha del ultimo elemento, o extremo derecho
            gearBtn->setPosition({rightMostX + 25.f, menuSize.height / 2.f});
            searchMenu->addChild(gearBtn);
        }

        log::info("[LevelBrowserLayer] Gear button added to search-menu");
    }

    void onLevelCellSettings(CCObject*) {
        auto popup = LevelCellSettingsPopup::create();
        if (!popup) return;

        popup->setOnSettingsChanged([]() {
            log::info("[LevelBrowserLayer] LevelCell settings changed, will apply on next cell load");
        });

        popup->show();
    }

    void loadLevelsFinished(cocos2d::CCArray* levels, char const* key, int type) {
        LevelBrowserLayer::loadLevelsFinished(levels, key, type);
    }
};

// NOTE: current-list-id cleanup moved to main MenuLayer hook (main.cpp) to avoid double-hooking MenuLayer::init

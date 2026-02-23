#include <Geode/Geode.hpp>
#include <Geode/modify/LevelListLayer.hpp>
#include <Geode/modify/LevelBrowserLayer.hpp>
#include "../managers/ThumbnailLoader.hpp"

using namespace geode::prelude;

class $modify(PaimonLevelListLayer, LevelListLayer) {
    bool init(GJLevelList* list) {
        // guardar id de lista pa LevelInfoLayer
        if (list) {
            Mod::get()->setSavedValue("current-list-id", list->m_listID);
            log::info("Entered List: {}", list->m_listID);
        } else {
            Mod::get()->setSavedValue("current-list-id", 0);
        }

        // reanudar cola al entrar a vista de lista
        ThumbnailLoader::get().resumeQueue();
        return LevelListLayer::init(list);
    }

    void onEnter() {
        LevelListLayer::onEnter();
        ThumbnailLoader::get().resumeQueue();
    }

    void onExit() {
        LevelListLayer::onExit();
    }
};

class $modify(ContextTrackingBrowser, LevelBrowserLayer) {
    bool init(GJSearchObject* p0) {
        // limpiar id de lista al entrar al browser normal (busqueda, etc)
        Mod::get()->setSavedValue("current-list-id", 0);
        return LevelBrowserLayer::init(p0);
    }
};

// NOTE: current-list-id cleanup moved to main MenuLayer hook (main.cpp) to avoid double-hooking MenuLayer::init

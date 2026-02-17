#include <Geode/Geode.hpp>
#include "managers/ProfileThumbs.hpp"
#include "managers/LevelColors.hpp"
#include "utils/Localization.hpp"
#include <thread>
#include <chrono>
#include <filesystem>

using namespace geode::prelude;

#include <Geode/modify/EndLevelLayer.hpp>
#include <Geode/modify/MenuLayer.hpp>

#include "managers/ThumbnailLoader.hpp"

// no hay un "$on_mod(unloaded)" decente, así que limpio al arrancar

void PaimonOnModLoaded() { // el $on_mod(loaded) está comentado pa evitar el bug del linker
    bool optimizer = true;
    try {
        optimizer = Mod::get()->getSettingValue<bool>("optimizer");
    } catch(...) {}
    
    if (optimizer) {
        Mod::get()->setLoggingEnabled(false);
    }


    log::info("[PaimonThumbnails][Init] Loaded event start");
    bool safeMode = false;
#ifdef GEODE_IS_ANDROID
    safeMode = Mod::get()->getSettingValue<bool>("android-safe-mode");
    log::info("[PaimonThumbnails][Init] Android detected. SafeMode={} ", safeMode ? "true" : "false");

    if (safeMode) {
        ThumbnailLoader::get().setMaxConcurrentTasks(2);
    }
#endif

    if (!safeMode) {
        // limpio el cache al empezar pa que esté fresco (si lo hago al salir se bugea)
        ThumbnailLoader::get().clearDiskCache();

        log::info("[PaimonThumbnails] Queueing main level thumbnails...");
        for (int i = 1; i <= 22; i++) {
            std::string fileName = fmt::format("{}.png", i);
            ThumbnailLoader::get().requestLoad(i, fileName, nullptr, 0);
        }
    } else {
        log::info("[PaimonThumbnails][Init] Skipping prefetch due to SafeMode");
    }

    try {
        std::string langStr = Mod::get()->getSettingValue<std::string>("language");
        log::info("[PaimonThumbnails][Init] Language setting='{}'", langStr);
        if (langStr == "english") Localization::get().setLanguage(Localization::Language::ENGLISH);
        else Localization::get().setLanguage(Localization::Language::SPANISH);
    } catch (...) { log::warn("[PaimonThumbnails][Init] Failed to apply language"); }

    log::info("[PaimonThumbnails][Init] Applying startup init");

    // borro el cache de perfiles cuando se abre el mod
    auto profileDir = Mod::get()->getSaveDir() / "thumbnails" / "profiles";
    
    if (std::filesystem::exists(profileDir)) {
        std::error_code ec;
        std::filesystem::remove_all(profileDir, ec);
        if (ec) {
            log::warn("[PaimonThumbnails] Failed to delete profiles directory: {}", ec.message());
        } else {
            log::info("[PaimonThumbnails] Deleted profiles directory for update synchronization");
        }
    }
    

    // saco los colores en masa (si es android safe mode me lo salto)
    if (!safeMode) {
        log::info("[PaimonThumbnails][Init] Scheduling color extraction thread");
        geode::Loader::get()->queueInMainThread([]() {
            std::thread([]() {
                try {
                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
                    LevelColors::get().extractColorsFromCache();
                    log::info("[PaimonThumbnails][Init] Color extraction finished");
                } catch (const std::exception& e) {
                    log::error("[PaimonThumbnails] Error extracting colors: {}", e.what());
                } catch (...) {
                    log::error("[PaimonThumbnails] Unknown error extracting colors");
                }
            }).detach();
        });
    } else {
        log::info("[PaimonThumbnails][Init] Color extraction skipped due to SafeMode");
    }

    log::info("[PaimonThumbnails][Init] Startup init complete");
}

class $modify(PaimonMenuLayer, MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) return false;

        static bool s_paimonLoaded = false;
        if (!s_paimonLoaded) {
            s_paimonLoaded = true;
            log::info("[PaimonThumbnails] Invoking delayed Mod Loaded initialization from MenuLayer");
            PaimonOnModLoaded();
        }
        return true;
    }
};

#include <Geode/Geode.hpp>
#include <Geode/utils/string.hpp>
#include "managers/ProfileThumbs.hpp"
#include "managers/LevelColors.hpp"
#include "managers/TempThumbnails.hpp"
#include "utils/Localization.hpp"
#include <thread>
#include <chrono>
#include <filesystem>

using namespace geode::prelude;

#include <Geode/modify/EndLevelLayer.hpp>
#include <Geode/loader/GameEvent.hpp>

#include "managers/ThumbnailLoader.hpp"

// limpieza de cache de disco (unificada para startup y exit)
static void cleanupDiskCache(const char* context) {
    bool clearCache = true;
    try {
        clearCache = Mod::get()->getSettingValue<bool>("clear-cache-on-exit");
    } catch (...) {}

    if (!clearCache) {
        log::info("[PaimonThumbnails] Cache cleanup disabled by setting ({})", context);
        return;
    }

    log::info("[PaimonThumbnails] Cleaning thumbnail disk cache ({})...", context);
    try {
        auto cachePath = Mod::get()->getSaveDir() / "cache";
        if (!std::filesystem::exists(cachePath)) return;

        int deletedCount = 0;
        for (const auto& entry : std::filesystem::directory_iterator(cachePath)) {
            if (!entry.is_regular_file()) continue;
            try {
                auto stem = geode::utils::string::pathToString(entry.path().stem());
                int id = 0;
                if (stem.find("_anim") != std::string::npos) {
                    std::string idStr = stem.substr(0, stem.find("_anim"));
                    id = geode::utils::numFromString<int>(idStr).unwrapOr(0);
                } else {
                    id = geode::utils::numFromString<int>(stem).unwrapOr(0);
                }
                // mantener main levels (1-22) cacheados siempre
                int realID = std::abs(id);
                if (realID >= 1 && realID <= 22) continue;

                std::filesystem::remove(entry.path());
                deletedCount++;
            } catch (...) {
                try { std::filesystem::remove(entry.path()); } catch (...) {}
            }
        }
        log::info("[PaimonThumbnails] Deleted {} cached thumbnails ({})", deletedCount, context);
    } catch (const std::exception& e) {
        log::error("[PaimonThumbnails] Error cleaning cache ({}): {}", context, e.what());
    } catch (...) {
        log::error("[PaimonThumbnails] Unknown error cleaning cache ({})", context);
    }
}

// al cerrar el juego:
// - activamos flags para que los destructores estáticos no hagan release() sobre objetos Cocos2d
// - limpiamos el caché de disco aquí mismo (operaciones de filesystem, no dependen de Cocos2d)
$on_game(Exiting) {
    ProfileThumbs::s_shutdownMode = true;
    TempThumbnails::s_shutdownMode = true;
    cleanupDiskCache("exit");
}

// no hay un "$on_mod(unloaded)" decente, así que limpio al arrancar

void PaimonOnModLoaded() { // el $on_mod(loaded) está comentado pa evitar el bug del linker
    log::info("[PaimonThumbnails][Init] Loaded event start");

    // limpiar cache de disco de la sesión anterior ANTES de cargar thumbnails nuevos
    cleanupDiskCache("startup");

    bool safeMode = false;
#ifdef GEODE_IS_ANDROID
    safeMode = Mod::get()->getSettingValue<bool>("android-safe-mode");
    log::info("[PaimonThumbnails][Init] Android detected. SafeMode={} ", safeMode ? "true" : "false");

    if (safeMode) {
        ThumbnailLoader::get().setMaxConcurrentTasks(2);
    }
#endif

    if (!safeMode) {
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

// MenuLayer hook moved to src/hooks/MenuLayer.cpp (PaimonMenuLayer)
// to avoid two $modify classes on the same class, which is undefined behavior.

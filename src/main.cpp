#include <Geode/Geode.hpp>
#include <Geode/utils/string.hpp>
#include "managers/ProfileThumbs.hpp"
#include "managers/LevelColors.hpp"
#include "managers/ProfileMusicManager.hpp"
#include "utils/Localization.hpp"
#include "utils/AnimatedGIFSprite.hpp"
#include <thread>
#include <chrono>
#include <filesystem>

using namespace geode::prelude;

#include <Geode/modify/EndLevelLayer.hpp>
#include <Geode/loader/GameEvent.hpp>

#include "managers/ThumbnailLoader.hpp"
#include "managers/LayerBackgroundManager.hpp"
#include "managers/TransitionManager.hpp"

// limpieza de cache de disco, la uso tanto al arrancar como al salir
static void cleanupDiskCache(char const* context) {
    bool clearCache = Mod::get()->getSettingValue<bool>("clear-cache-on-exit");

    if (!clearCache) {
        log::info("[PaimonThumbnails] Cache cleanup disabled by setting ({})", context);
        return;
    }

    log::info("[PaimonThumbnails] Cleaning thumbnail disk cache ({})...", context);

    auto cachePath = Mod::get()->getSaveDir() / "cache";
    std::error_code ec;
    if (!std::filesystem::exists(cachePath, ec)) return;

    int deletedCount = 0;
    for (auto const& entry : std::filesystem::directory_iterator(cachePath, ec)) {
        if (ec) break;
        if (!entry.is_regular_file()) continue;
        auto stem = geode::utils::string::pathToString(entry.path().stem());
        int id = 0;
        if (stem.find("_anim") != std::string::npos) {
            std::string idStr = stem.substr(0, stem.find("_anim"));
            id = geode::utils::numFromString<int>(idStr).unwrapOr(0);
        } else {
            id = geode::utils::numFromString<int>(stem).unwrapOr(0);
        }
        // los main levels (1-22) siempre se mantienen en cache
        int realID = std::abs(id);
        if (realID >= 1 && realID <= 22) continue;

        std::filesystem::remove(entry.path(), ec);
        if (!ec) deletedCount++;
    }
    log::info("[PaimonThumbnails] Deleted {} cached thumbnails ({})", deletedCount, context);
}

// al cerrar el juego:
// - activamos flags para que los destructores estaticos no hagan release() sobre objetos Cocos2d
// - limpiamos caches de datos del servidor (perfiles, GIFs, musica, profileimg)
// - NO tocamos datos offline del usuario (fondos de menu, thumbnails locales, settings)
$on_game(Exiting) {
    ProfileThumbs::s_shutdownMode = true;

    // limpieza de caches de disco del servidor
    cleanupDiskCache("exit");

    // 1. cache de perfiles de otros usuarios (thumbnails + GIFs en memoria)
    ProfileThumbs::get().clearAllCache();
    ProfileThumbs::get().clearNoProfileCache();

    // 2. cache global de GIFs animados en RAM
    AnimatedGIFSprite::clearCache();

    // 3. musica de perfiles: para la reproduccion y limpia el cache de disco (.mp3)
    ProfileMusicManager::get().stopProfileMusic();
    ProfileMusicManager::get().clearCache();

    // 4. cache de disco de profileimg (fotos de perfil descargadas del servidor)
    {
        auto profileImgDir = Mod::get()->getSaveDir() / "profileimg_cache";
        std::error_code ec;
        if (std::filesystem::exists(profileImgDir, ec)) {
            std::filesystem::remove_all(profileImgDir, ec);
            if (!ec) {
                log::info("[PaimonThumbnails] profileimg_cache cleared on exit");
            }
        }
    }

    // 5. cache de disco de perfiles RGB (thumbnails/profiles)
    {
        auto profileDir = Mod::get()->getSaveDir() / "thumbnails" / "profiles";
        std::error_code ec;
        if (std::filesystem::exists(profileDir, ec)) {
            std::filesystem::remove_all(profileDir, ec);
            if (!ec) {
                log::info("[PaimonThumbnails] profile thumbnail cache cleared on exit");
            }
        }
    }

    log::info("[PaimonThumbnails] Server caches cleaned on exit");
}

// no hay un "$on_mod(unloaded)" decente, asi que limpio al arrancar

void PaimonOnModLoaded() { // el $on_mod(loaded) esta comentado para evitar el bug del linker
    log::info("[PaimonThumbnails][Init] Loaded event start");

    // migra los settings legacy al formato unificado per-layer
    LayerBackgroundManager::get().migrateFromLegacy();
    // migra la musica per-layer a global (solo corre una vez)
    LayerBackgroundManager::get().migrateToGlobalMusic();

    // carga la configuracion de transiciones personalizables
    TransitionManager::get().loadConfig();

    // limpia el cache de disco de la sesion anterior ANTES de cargar thumbnails nuevos
    cleanupDiskCache("startup");

    log::info("[PaimonThumbnails] Queueing main level thumbnails...");
    for (int i = 1; i <= 22; i++) {
        std::string fileName = fmt::format("{}.png", i);
        ThumbnailLoader::get().requestLoad(i, fileName, nullptr, 0);
    }

    std::string langStr = Mod::get()->getSettingValue<std::string>("language");
    log::info("[PaimonThumbnails][Init] Language setting='{}'", langStr);
    if (langStr == "english") Localization::get().setLanguage(Localization::Language::ENGLISH);
    else Localization::get().setLanguage(Localization::Language::SPANISH);

    log::info("[PaimonThumbnails][Init] Applying startup init");

    // borro el cache de perfiles al abrir el mod
    auto profileDir = Mod::get()->getSaveDir() / "thumbnails" / "profiles";
    
    std::error_code profileEc;
    if (std::filesystem::exists(profileDir, profileEc)) {
        std::error_code ec;
        std::filesystem::remove_all(profileDir, ec);
        if (ec) {
            log::warn("[PaimonThumbnails] Failed to delete profiles directory: {}", ec.message());
        } else {
            log::info("[PaimonThumbnails] Deleted profiles directory for update synchronization");
        }
    }
    

    log::info("[PaimonThumbnails][Init] Scheduling color extraction thread");
    geode::Loader::get()->queueInMainThread([]() {
        std::thread([]() {
            geode::utils::thread::setName("PaimonThumbnails ColorExtract");
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            LevelColors::get().extractColorsFromCache();
            log::info("[PaimonThumbnails][Init] Color extraction finished");
        }).detach();
    });

    log::info("[PaimonThumbnails][Init] Startup init complete");
}

// MenuLayer hook movido a src/hooks/MenuLayer.cpp (PaimonMenuLayer)
// para evitar dos $modify sobre la misma clase, que es undefined behavior.

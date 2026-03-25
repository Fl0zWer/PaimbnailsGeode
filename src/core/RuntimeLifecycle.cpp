// RuntimeLifecycle.cpp — Manejo de ciclo de vida: arranque y cierre.
// - cleanupDiskCache(): limpieza selectiva del cache de disco
// - $on_game(Exiting): limpieza de RAM y disco al cerrar el juego

#include <Geode/Geode.hpp>
#include <Geode/loader/GameEvent.hpp>
#include <Geode/utils/string.hpp>
#include "../features/profiles/services/ProfileThumbs.hpp"
#include "../features/profile-music/services/ProfileMusicManager.hpp"
#include "../features/dynamic-songs/services/DynamicSongManager.hpp"
#include "../features/pet/services/PetManager.hpp"
#include "../features/thumbnails/services/ThumbnailLoader.hpp"
#include "../features/thumbnails/services/LocalThumbs.hpp"
#include "../features/thumbnails/services/LevelColors.hpp"
#include "../utils/AnimatedGIFSprite.hpp"
#include "QualityConfig.hpp"
#include <filesystem>

using namespace geode::prelude;

namespace {
void removePathIfExists(std::filesystem::path const& path, char const* label) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        return;
    }

    std::filesystem::remove_all(path, ec);
    if (ec) {
        log::warn("[PaimonThumbnails] Failed to remove {} at {}: {}", label, geode::utils::string::pathToString(path), ec.message());
    } else {
        log::info("[PaimonThumbnails] Removed {} at {}", label, geode::utils::string::pathToString(path));
    }
}
}

// limpieza de cache de disco, la uso tanto al arrancar como al salir
void cleanupDiskCache(char const* context) {
    bool clearCache = Mod::get()->getSettingValue<bool>("clear-cache-on-exit");

    if (!clearCache) {
        log::info("[PaimonThumbnails] Cache cleanup disabled by setting ({})", context);
        return;
    }

    log::info("[PaimonThumbnails] Cleaning quality cache tree ({})...", context);
    removePathIfExists(paimon::quality::cacheDir(), "quality cache");
}

// al cerrar el juego:
// - activamos flags para que los destructores estaticos no hagan release() sobre objetos Cocos2d
// - limpiamos caches de datos del servidor (perfiles, GIFs, musica, profileimg)
// - NO tocamos datos offline del usuario (fondos de menu, thumbnails locales, settings)
$on_game(Exiting) {
    ProfileThumbs::s_shutdownMode.store(true, std::memory_order_release);

    // cancelar tareas pendientes de ThumbnailLoader ANTES de limpiar disco
    // para que los hilos de fondo no reescriban archivos que vamos a borrar
    ThumbnailLoader::get().cleanup();

    bool clearCacheOnExit = Mod::get()->getSettingValue<bool>("clear-cache-on-exit");

    // persistir manifest de disco antes de cerrar (sincrono, ya no hay workers)
    // solo si no se va a borrar inmediatamente despues
    if (!clearCacheOnExit) {
        ThumbnailLoader::get().diskManifest().flush();
    }

    LocalThumbs::get().shutdown();
    ProfileThumbs::get().shutdown();

    // forzar escritura de colores pendientes antes del cierre
    // (siempre flush: thumbnails/ ya no se borra en el cleanup)
    LevelColors::get().flushIfDirty();

    // === RAM cleanup (siempre, para evitar crashes con destructores estaticos) ===

    // 1. cache de perfiles de otros usuarios (thumbnails + GIFs en memoria)
    ProfileThumbs::get().clearAllCache();
    ProfileThumbs::get().clearNoProfileCache();

    // 1b. limpiar callbacks pendientes que capturan Ref<GJScoreCell> etc.
    //     si no se limpian, el destructor estatico de ProfileThumbs los destruiria
    //     despues de que CCPoolManager ya murio → crash
    ProfileThumbs::get().clearPendingDownloads();

    // 2. cache global de GIFs animados en RAM
    AnimatedGIFSprite::clearCache();

    // 3. detener audio dinamico/perfil de forma forzada (evita estados intermedios
    // de fades/transiciones durante shutdown)
    DynamicSongManager::get()->forceKill();
    ProfileMusicManager::get().forceStop();
    PetManager::get().releaseSharedResources();

    // === Disk cleanup (solo si clear-cache-on-exit esta activado) ===

    bool clearCache = clearCacheOnExit;
    if (!clearCache) {
        log::info("[PaimonThumbnails] Disk cache cleanup disabled by setting");
        return;
    }

    // 4. cache regenerable quality-aware (thumbnails, GIFs, profiles, manifests derivados)
    cleanupDiskCache("exit");

    // 5. caches regenerables del servidor (profile music, profile images)
    auto saveDir = Mod::get()->getSaveDir();
    removePathIfExists(saveDir / "profile_music", "profile music cache");
    removePathIfExists(saveDir / "profileimg_cache", "profile image cache");
    // NO tocamos thumbnails/, saved_thumbnails/ ni downloaded_thumbnails/
    // porque son datos locales del usuario que no se pueden regenerar

    log::info("[PaimonThumbnails] All caches cleaned on exit");
}

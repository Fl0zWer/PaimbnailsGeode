// RuntimeLifecycle.cpp — Manejo de ciclo de vida: arranque y cierre.
// - cleanupDiskCache(): limpieza selectiva del cache de disco
// - $on_game(Exiting): limpieza de RAM y disco al cerrar el juego

#include <Geode/Geode.hpp>
#include <Geode/loader/GameEvent.hpp>
#include <Geode/utils/string.hpp>
#include "../features/profiles/services/ProfileThumbs.hpp"
#include "../features/profile-music/services/ProfileMusicManager.hpp"
#include "../features/pet/services/PetManager.hpp"
#include "../features/thumbnails/services/ThumbnailLoader.hpp"
#include "../features/thumbnails/services/LocalThumbs.hpp"
#include "../features/thumbnails/services/LevelColors.hpp"
#include "../utils/AnimatedGIFSprite.hpp"
#include <filesystem>

using namespace geode::prelude;

// limpieza de cache de disco, la uso tanto al arrancar como al salir
void cleanupDiskCache(char const* context) {
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
        // compatibilidad: archivos legacy con _anim en el nombre
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
    ProfileThumbs::s_shutdownMode.store(true, std::memory_order_release);

    // cancelar tareas pendientes de ThumbnailLoader ANTES de limpiar disco
    // para que los hilos de fondo no reescriban archivos que vamos a borrar
    ThumbnailLoader::get().cleanup();
    LocalThumbs::get().shutdown();
    ProfileThumbs::get().shutdown();

    // forzar escritura de colores pendientes antes del cierre
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

    // 3. parar musica de perfiles (siempre)
    ProfileMusicManager::get().stopProfileMusic();
    PetManager::get().releaseSharedResources();

    // === Disk cleanup (solo si clear-cache-on-exit esta activado) ===

    bool clearCache = Mod::get()->getSettingValue<bool>("clear-cache-on-exit");
    if (!clearCache) {
        log::info("[PaimonThumbnails] Disk cache cleanup disabled by setting");
        return;
    }

    // 4. thumbnails de disco (carpeta "cache/", preserva main levels 1-22)
    cleanupDiskCache("exit");

    // 5. cache de disco de GIFs decodificados (carpeta "gif_cache/")
    {
        auto gifCacheDir = Mod::get()->getSaveDir() / "gif_cache";
        std::error_code ec;
        if (std::filesystem::exists(gifCacheDir, ec)) {
            std::filesystem::remove_all(gifCacheDir, ec);
            if (!ec) {
                log::info("[PaimonThumbnails] gif_cache cleared on exit");
            }
        }
    }

    // 6. cache de musica de perfiles (disco: carpeta "profile_music/")
    ProfileMusicManager::get().clearCache();

    // 7. cache de disco de profileimg (fotos de perfil descargadas del servidor)
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

    // 8. cache de disco de perfiles RGB (thumbnails/profiles)
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

    // 9. thumbnails locales subidos por el usuario (thumbnails/*.rgb)
    {
        auto thumbDir = Mod::get()->getSaveDir() / "thumbnails";
        std::error_code ec;
        if (std::filesystem::exists(thumbDir, ec)) {
            int deleted = 0;
            for (auto const& entry : std::filesystem::directory_iterator(thumbDir, ec)) {
                if (ec) break;
                if (!entry.is_regular_file()) continue;
                auto ext = entry.path().extension();
                if (ext == ".rgb" || ext == ".RGB") {
                    std::filesystem::remove(entry.path(), ec);
                    if (!ec) deleted++;
                }
            }
            if (deleted > 0) {
                log::info("[PaimonThumbnails] Deleted {} local thumbnail .rgb files on exit", deleted);
            }
        }
    }

    log::info("[PaimonThumbnails] All caches cleaned on exit");
}

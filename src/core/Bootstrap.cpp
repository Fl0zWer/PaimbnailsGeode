// Bootstrap.cpp — Inicializacion diferida del mod.
// Llamado desde MenuLayer::init() la primera vez que se carga el juego.
// Separa la logica de arranque del entrypoint para reducir acoplamiento.

#include <Geode/Geode.hpp>
#include <Geode/utils/string.hpp>
#include "../features/backgrounds/services/LayerBackgroundManager.hpp"
#include "../features/transitions/services/TransitionManager.hpp"
#include "../features/thumbnails/services/ThumbnailLoader.hpp"
#include "../features/thumbnails/services/LevelColors.hpp"
#include "../utils/Localization.hpp"
#include <thread>
#include <chrono>
#include <filesystem>

using namespace geode::prelude;

// declarada en RuntimeLifecycle.cpp
extern void cleanupDiskCache(char const* context);

void PaimonOnModLoaded() {
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
    // hilo de I/O de disco + procesamiento CPU — no migrable a WebTask (no es peticion web).
    // el delay y la extraccion se ejecutan en background para no bloquear el main thread.
    geode::Loader::get()->queueInMainThread([]() {
        std::thread([]() {
            geode::utils::thread::setName("PaimonThumbnails ColorExtract");
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            LevelColors::get().extractColorsFromCache();
            geode::Loader::get()->queueInMainThread([]() {
                log::info("[PaimonThumbnails][Init] Color extraction finished");
            });
        }).detach();
    });

    log::info("[PaimonThumbnails][Init] Startup init complete");
}

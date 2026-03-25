// Inicializacion diferida del mod desde MenuLayer::init().

#include <Geode/Geode.hpp>
#include <Geode/utils/string.hpp>
#include "../features/backgrounds/services/LayerBackgroundManager.hpp"
#include "../features/transitions/services/TransitionManager.hpp"
#include "../features/thumbnails/services/ThumbnailLoader.hpp"
#include "../features/thumbnails/services/LevelColors.hpp"
#include "../utils/Localization.hpp"
#include "../utils/MainThreadDelay.hpp"
#include "../utils/HttpClient.hpp"
#include "QualityConfig.hpp"
#include <thread>
#include <filesystem>

using namespace geode::prelude;

namespace {
void applyLanguageSetting(std::string const& langStr) {
    if (langStr == "english") {
        Localization::get().setLanguage(Localization::Language::ENGLISH);
    } else {
        Localization::get().setLanguage(Localization::Language::SPANISH);
    }
}

bool g_languageListenerRegistered = false;
}

void PaimonOnModLoaded() {
    log::info("[PaimonThumbnails][Init] Loaded event start");

    // migra los settings legacy al formato unificado per-layer
    LayerBackgroundManager::get().migrateFromLegacy();
    // migra la musica per-layer a global (solo corre una vez)
    LayerBackgroundManager::get().migrateToGlobalMusic();

    // carga la configuracion de transiciones personalizables
    TransitionManager::get().loadConfig();

    log::info("[PaimonThumbnails] Queueing main level thumbnails...");

    // Batch fetch manifest for main levels first, then prefetch
    std::vector<int> mainLevels;
    for (int i = 1; i <= 22; i++) mainLevels.push_back(i);

    HttpClient::get().fetchManifest(mainLevels, [](bool success) {
        log::info("[PaimonThumbnails] Manifest fetch {}, starting prefetch", success ? "succeeded" : "failed (will use Worker fallback)");
        // Prefetch from disk/CDN using manifest data
        for (int i = 1; i <= 22; i++) {
            ThumbnailLoader::get().prefetchLevelAssets(i, 1);
        }
    });

    std::string langStr = Mod::get()->getSettingValue<std::string>("language");
    log::info("[PaimonThumbnails][Init] Language setting='{}'", langStr);
    applyLanguageSetting(langStr);
    if (!g_languageListenerRegistered) {
        g_languageListenerRegistered = true;
        geode::listenForSettingChanges<std::string>("language", +[](std::string value) {
            applyLanguageSetting(value);
            log::info("[PaimonThumbnails][Language] Changed to '{}'", value);
        });
    }

    log::info("[PaimonThumbnails][Init] Applying startup init");

    log::info("[PaimonThumbnails][Init] Scheduling color extraction thread");
    // hilo de I/O de disco + procesamiento CPU — no migrable a WebTask (no es peticion web).
    // el delay y la extraccion se ejecutan en background para no bloquear el main thread.
    paimon::scheduleMainThreadDelay(0.5f, []() {
        std::thread([]() {
            geode::utils::thread::setName("PaimonThumbnails ColorExtract");
            LevelColors::get().extractColorsFromCache();
            geode::Loader::get()->queueInMainThread([]() {
                log::info("[PaimonThumbnails][Init] Color extraction finished");
            });
        }).detach();
    });

    log::info("[PaimonThumbnails][Init] Startup init complete");
}

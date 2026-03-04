#pragma once
#include <Geode/Geode.hpp>
#include <string>
#include <unordered_map>
#include <functional>

// ════════════════════════════════════════════════════════════
// LayerBackgroundManager — aplica fondos personalizados por layer
// Unificado: incluye Menu, LevelInfo, Profile y todos los layers
// Saved value pattern: "layerbg-{key}-type", "layerbg-{key}-path"
// ════════════════════════════════════════════════════════════

struct LayerBgConfig {
    std::string type = "default";   // "default", "custom", "random", "menu", "id"
    std::string customPath;         // ruta imagen/GIF
    int levelId = 0;                // para tipo "id"
    bool darkMode = false;
    float darkIntensity = 0.5f;
    std::string shader = "none";    // "none","grayscale","sepia","vignette","bloom","chromatic","pixelate","posterize","scanlines"
};

// Configuracion de musica per-layer
struct LayerMusicConfig {
    std::string mode = "default";   // "default", "newgrounds", "custom", "dynamic"
    int songID = 0;                 // Newgrounds song ID
    std::string customPath;         // ruta a archivo audio local
    float speed = 1.0f;            // playback speed 0.1 - 1.0
    bool randomStart = false;       // start from a random position
    int startMs = 0;                // loop/play start time (0 = from beginning)
    int endMs = 0;                  // loop/play end time (0 = until end)
    std::string filter = "none";    // audio filter: none, cave, underwater, echo, etc.
};

// Available audio filters
static inline std::vector<std::pair<std::string, std::string>> AUDIO_FILTERS = {
    {"none",        "None"},
    {"cave",        "Cave"},
    {"underwater",  "Underwater"},
    {"echo",        "Echo"},
    {"hall",        "Concert Hall"},
    {"radio",       "Old Radio"},
    {"phone",       "Phone Call"},
    {"chorus",      "Chorus"},
    {"flanger",     "Flanger"},
    {"distortion",  "Distortion"},
    {"tremolo",     "Tremolo"},
    {"nightcore",   "Nightcore"},
    {"vaporwave",   "Vaporwave"},
};

class LayerBackgroundManager {
public:
    static LayerBackgroundManager& get();

    // Aplica fondo al layer segun su key. Llama despues de super::init().
    // Retorna true si se aplico un fondo custom (para que el hook oculte UI extra).
    bool applyBackground(cocos2d::CCLayer* layer, std::string const& layerKey);

    // Consulta rapida: ¿este layer tiene un fondo custom configurado? (no aplica nada)
    bool hasCustomBackground(std::string const& layerKey) const;

    // Lee config de Mod saved values para una key
    LayerBgConfig getConfig(std::string const& layerKey) const;

    // Guarda config a Mod saved values
    void saveConfig(std::string const& layerKey, LayerBgConfig const& cfg);

    // Resuelve la cadena de referencias (Same as...) y retorna la config final concreta.
    // util para previews y consultas sin aplicar nada.
    LayerBgConfig resolveConfig(std::string const& layerKey) const;

    // ── Music per-layer (legacy, kept for migration) ──
    LayerMusicConfig getMusicConfig(std::string const& layerKey) const;
    void saveMusicConfig(std::string const& layerKey, LayerMusicConfig const& cfg);

    // ── Global music (replaces per-layer: one config for ALL layers) ──
    LayerMusicConfig getGlobalMusicConfig() const;
    void saveGlobalMusicConfig(LayerMusicConfig const& cfg);

    // Todos los layers soportados (key, displayName)
    static inline std::vector<std::pair<std::string, std::string>> LAYER_OPTIONS = {
        {"menu",         "Menu"},
        {"levelinfo",    "Level Info"},
        {"levelselect",  "Level Select"},
        {"creator",      "Creator"},
        {"browser",      "Browser"},
        {"search",       "Search"},
        {"leaderboards", "Leaderboards"},
        {"profile",      "Profile"},
    };

    // Migra saved values del formato legacy (bg-type, bg-custom-path, etc.)
    // al nuevo formato unificado layerbg-*. Solo la primera vez.
    void migrateFromLegacy();

    // Migra saved values de music per-layer al formato global.
    void migrateToGlobalMusic();

private:
    LayerBackgroundManager() = default;

    // helpers
    void hideOriginalBg(cocos2d::CCLayer* layer);
    cocos2d::CCTexture2D* loadTextureForConfig(LayerBgConfig const& cfg);
    void applyStaticBg(cocos2d::CCLayer* layer, cocos2d::CCTexture2D* tex, LayerBgConfig const& cfg);
    void applyGifBg(cocos2d::CCLayer* layer, std::string const& path, LayerBgConfig const& cfg);
};


#pragma once
#include <Geode/Geode.hpp>
#include <string>
#include <unordered_map>
#include <functional>

// fondos personalizados por layer

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
    float speed = 1.0f;            // velocidad 0.1 - 1.0
    bool randomStart = false;       // arranca en un punto random
    int startMs = 0;                // inicio del loop/play
    int endMs = 0;                  // final del loop/play
    std::string filter = "none";    // filtro de audio
};

// filtros de audio disponibles
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

    // aplica el fondo del layer; llamalo despues de super::init()
    // devuelve true si habia fondo custom y el hook debe esconder UI extra
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

    LayerMusicConfig getMusicConfig(std::string const& layerKey) const;
    void saveMusicConfig(std::string const& layerKey, LayerMusicConfig const& cfg);

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

    // util
    void hideOriginalBg(cocos2d::CCLayer* layer);
    cocos2d::CCTexture2D* loadTextureForConfig(LayerBgConfig const& cfg);
    void applyStaticBg(cocos2d::CCLayer* layer, cocos2d::CCTexture2D* tex, LayerBgConfig const& cfg);
    void applyGifBg(cocos2d::CCLayer* layer, std::string const& path, LayerBgConfig const& cfg);
};


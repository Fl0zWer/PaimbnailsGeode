#include "LayerBackgroundManager.hpp"
#include "../../thumbnails/services/LocalThumbs.hpp"
#include "../../../utils/AnimatedGIFSprite.hpp"
#include "../../../utils/ImageLoadHelper.hpp"
#include "../../../managers/ThumbnailAPI.hpp"
#include <random>
#include <filesystem>

#include "../../../utils/Shaders.hpp"

using namespace geode::prelude;
using namespace cocos2d;
using namespace Shaders;

LayerBackgroundManager& LayerBackgroundManager::get() {
    static LayerBackgroundManager s_instance;
    return s_instance;
}

LayerBgConfig LayerBackgroundManager::getConfig(std::string const& key) const {
    LayerBgConfig cfg;
    cfg.type          = Mod::get()->getSavedValue<std::string>("layerbg-" + key + "-type", "default");
    cfg.customPath    = Mod::get()->getSavedValue<std::string>("layerbg-" + key + "-path", "");
    cfg.levelId       = Mod::get()->getSavedValue<int>("layerbg-" + key + "-id", 0);
    cfg.darkMode      = Mod::get()->getSavedValue<bool>("layerbg-" + key + "-dark", false);
    cfg.darkIntensity = Mod::get()->getSavedValue<float>("layerbg-" + key + "-dark-intensity", 0.5f);
    cfg.shader        = Mod::get()->getSavedValue<std::string>("layerbg-" + key + "-shader", "none");
    return cfg;
}

void LayerBackgroundManager::saveConfig(std::string const& key, LayerBgConfig const& cfg) {
    log::info("[LayerBgMgr] saveConfig: key={} type={}", key, cfg.type);
    Mod::get()->setSavedValue("layerbg-" + key + "-type", cfg.type);
    Mod::get()->setSavedValue("layerbg-" + key + "-path", cfg.customPath);
    Mod::get()->setSavedValue("layerbg-" + key + "-id", cfg.levelId);
    Mod::get()->setSavedValue("layerbg-" + key + "-dark", cfg.darkMode);
    Mod::get()->setSavedValue("layerbg-" + key + "-dark-intensity", cfg.darkIntensity);
    Mod::get()->setSavedValue("layerbg-" + key + "-shader", cfg.shader);
    (void)Mod::get()->saveData();
}

bool LayerBackgroundManager::hasCustomBackground(std::string const& layerKey) const {
    auto resolved = resolveConfig(layerKey);
    return resolved.type != "default";
}

LayerBgConfig LayerBackgroundManager::resolveConfig(std::string const& layerKey) const {
    auto cfg = getConfig(layerKey);
    log::debug("[LayerBgMgr] resolveConfig: key={} type={}", layerKey, cfg.type);
    if (cfg.type == "default") return cfg;

    std::string resolvedType = cfg.type;
    LayerBgConfig resolvedCfg = cfg;
    int maxHops = 5;

    while (maxHops-- > 0) {
        if (resolvedType == "menu") {
            LayerBgConfig menuCfg = getConfig("menu");
            if (menuCfg.type != "default") {
                resolvedCfg.type = menuCfg.type;
                resolvedCfg.customPath = menuCfg.customPath;
                resolvedCfg.levelId = menuCfg.levelId;
                resolvedType = menuCfg.type;
                continue;
            } else {
                std::string menuType = Mod::get()->getSavedValue<std::string>("bg-type", "default");
                if (menuType == "default" || menuType.empty()) { resolvedCfg.type = "default"; return resolvedCfg; }
                resolvedCfg.type = (menuType == "thumbnails") ? "random" : menuType;
                resolvedCfg.customPath = Mod::get()->getSavedValue<std::string>("bg-custom-path", "");
                resolvedCfg.levelId = Mod::get()->getSavedValue<int>("bg-id", 0);
                return resolvedCfg;
            }
        }

        bool isLayerRef = false;
        for (auto& [k, n] : LAYER_OPTIONS) {
            if (resolvedType == k) { isLayerRef = true; break; }
        }
        if (isLayerRef) {
            auto refCfg = getConfig(resolvedType);
            resolvedCfg.type = refCfg.type;
            resolvedCfg.customPath = refCfg.customPath;
            resolvedCfg.levelId = refCfg.levelId;
            // Keep original dark mode / shader
            resolvedType = refCfg.type;
            if (resolvedType == "default") return resolvedCfg;
            continue;
        }
        break;
    }
    return resolvedCfg;
}

// ── Music per-layer ──
LayerMusicConfig LayerBackgroundManager::getMusicConfig(std::string const& key) const {
    LayerMusicConfig cfg;
    cfg.mode        = Mod::get()->getSavedValue<std::string>("layermusic-" + key + "-mode", "default");
    cfg.songID      = Mod::get()->getSavedValue<int>("layermusic-" + key + "-songid", 0);
    cfg.customPath  = Mod::get()->getSavedValue<std::string>("layermusic-" + key + "-path", "");
    cfg.speed       = Mod::get()->getSavedValue<float>("layermusic-" + key + "-speed", 1.0f);
    cfg.randomStart = Mod::get()->getSavedValue<bool>("layermusic-" + key + "-randomstart", false);
    cfg.startMs     = Mod::get()->getSavedValue<int>("layermusic-" + key + "-startms", 0);
    cfg.endMs       = Mod::get()->getSavedValue<int>("layermusic-" + key + "-endms", 0);
    cfg.filter      = Mod::get()->getSavedValue<std::string>("layermusic-" + key + "-filter", "none");
    return cfg;
}

void LayerBackgroundManager::saveMusicConfig(std::string const& key, LayerMusicConfig const& cfg) {
    Mod::get()->setSavedValue("layermusic-" + key + "-mode", cfg.mode);
    Mod::get()->setSavedValue("layermusic-" + key + "-songid", cfg.songID);
    Mod::get()->setSavedValue("layermusic-" + key + "-path", cfg.customPath);
    Mod::get()->setSavedValue("layermusic-" + key + "-speed", cfg.speed);
    Mod::get()->setSavedValue("layermusic-" + key + "-randomstart", cfg.randomStart);
    Mod::get()->setSavedValue("layermusic-" + key + "-startms", cfg.startMs);
    Mod::get()->setSavedValue("layermusic-" + key + "-endms", cfg.endMs);
    Mod::get()->setSavedValue("layermusic-" + key + "-filter", cfg.filter);
    (void)Mod::get()->saveData();
}

// ── Global music (one config for ALL layers) ──
LayerMusicConfig LayerBackgroundManager::getGlobalMusicConfig() const {
    return getMusicConfig("global");
}

void LayerBackgroundManager::saveGlobalMusicConfig(LayerMusicConfig const& cfg) {
    saveMusicConfig("global", cfg);
}

// ── Migracion de saved values legacy al nuevo formato unificado ──
void LayerBackgroundManager::migrateFromLegacy() {
    if (Mod::get()->getSavedValue<bool>("layerbg-migrated-v2", false)) return;

    // migrar menu bg: bg-type -> layerbg-menu-type
    std::string menuType = Mod::get()->getSavedValue<std::string>("bg-type", "");
    if (!menuType.empty() && menuType != "default") {
        LayerBgConfig menuCfg;
        menuCfg.type = (menuType == "thumbnails") ? "random" : menuType;
        menuCfg.customPath = Mod::get()->getSavedValue<std::string>("bg-custom-path", "");
        menuCfg.levelId = Mod::get()->getSavedValue<int>("bg-id", 0);
        menuCfg.darkMode = Mod::get()->getSavedValue<bool>("bg-dark-mode", false);
        menuCfg.darkIntensity = Mod::get()->getSavedValue<float>("bg-dark-intensity", 0.5f);
        saveConfig("menu", menuCfg);
    }

    // migrar profile bg: profile-bg-type -> layerbg-profile-type
    std::string profileType = Mod::get()->getSavedValue<std::string>("profile-bg-type", "");
    if (!profileType.empty() && profileType != "none") {
        LayerBgConfig profileCfg;
        profileCfg.type = profileType;
        profileCfg.customPath = Mod::get()->getSavedValue<std::string>("profile-bg-path", "");
        saveConfig("profile", profileCfg);
    }

    // migrar dynamic-song -> layermusic-levelinfo
    bool dynSong = Mod::get()->getSavedValue<bool>("dynamic-song", false);
    if (dynSong) {
        LayerMusicConfig mcfg;
        mcfg.mode = "dynamic";
        saveMusicConfig("levelinfo", mcfg);
        saveMusicConfig("levelselect", mcfg);
    }

    Mod::get()->setSavedValue("layerbg-migrated-v2", true);
    (void)Mod::get()->saveData();
    log::info("[LayerBackgroundManager] Legacy settings migrated to v2 format");

    // ── Migrate per-layer music → global music config ──
    migrateToGlobalMusic();
}

void LayerBackgroundManager::migrateToGlobalMusic() {
    if (Mod::get()->getSavedValue<bool>("layermusic-migrated-global", false)) return;

    // Prefer "menu" config first, then try other layers
    static std::vector<std::string> priority = {
        "menu", "creator", "browser", "search", "leaderboards",
        "profile", "levelselect", "levelinfo"
    };

    for (auto const& key : priority) {
        auto cfg = getMusicConfig(key);
        if (cfg.mode != "default" && cfg.mode != "dynamic") {
            saveGlobalMusicConfig(cfg);
            log::info("[LayerBackgroundManager] Migrated per-layer music from '{}' to global config", key);
            break;
        }
    }

    Mod::get()->setSavedValue("layermusic-migrated-global", true);
    (void)Mod::get()->saveData();
}

// ── ocultar fondo original de GD ──
void LayerBackgroundManager::hideOriginalBg(CCLayer* layer) {
    // Geode node-ids: cada layer tiene un ID distinto para su fondo
    // MenuLayer = "main-menu-bg", la mayoria de layers = "background"
    static char const* bgNodeIDs[] = {
        "main-menu-bg",   // MenuLayer
        "background",     // CreatorLayer, LevelSearchLayer, LeaderboardsLayer, LevelBrowserLayer, etc.
        nullptr
    };

    for (int i = 0; bgNodeIDs[i]; i++) {
        if (auto bg = layer->getChildByID(bgNodeIDs[i])) {
            bg->setVisible(false);
        }
    }

    // Tambien ocultar el GJGroundLayer si existe (corners/ground decorativos)
    if (auto children = layer->getChildren()) {
        auto ws = CCDirector::sharedDirector()->getWinSize();
        bool foundByID = false;
        for (int j = 0; bgNodeIDs[j]; j++) {
            if (layer->getChildByID(bgNodeIDs[j])) { foundByID = true; break; }
        }
        // Si no encontramos por ID, fallback: ocultar primer sprite grande (fondo GD)
        if (!foundByID) {
            for (auto* child : CCArrayExt<CCNode*>(children)) {
                auto* sprite = typeinfo_cast<CCSprite*>(child);
                if (!sprite) continue;
                auto* tex = sprite->getTexture();
                if (!tex) continue;
                auto cs = sprite->getContentSize();
                if (cs.width >= ws.width * 0.5f && cs.height >= ws.height * 0.5f) {
                    sprite->setVisible(false);
                    return;
                }
            }
        }
    }
}

// ── cargar textura segun config (optimizado: cache-first) ──
CCTexture2D* LayerBackgroundManager::loadTextureForConfig(LayerBgConfig const& cfg) {
    log::debug("[LayerBgMgr] loadTextureForConfig: type={} id={}", cfg.type, cfg.levelId);
    if (cfg.type == "custom" && !cfg.customPath.empty()) {
        std::error_code ec;
        if (std::filesystem::exists(cfg.customPath, ec)) {
            // no GIF aqui — GIF se maneja aparte
            auto ext = geode::utils::string::pathToString(std::filesystem::path(cfg.customPath).extension());
            for (auto& c : ext) c = (char)std::tolower(c);
            if (ext == ".gif") return nullptr; // senal para usar applyGifBg

            // Intentar cache primero; si no hay, cargar
            auto* cached = CCTextureCache::sharedTextureCache()->textureForKey(cfg.customPath.c_str());
            if (cached) return cached;

            auto* tex = CCTextureCache::sharedTextureCache()->addImage(cfg.customPath.c_str(), false);
            if (!tex) {
                auto stb = ImageLoadHelper::loadWithSTB(std::filesystem::path(cfg.customPath));
                if (stb.success && stb.texture) {
                    stb.texture->autorelease();
                    return stb.texture;
                }
            }
            return tex;
        }
    } else if (cfg.type == "id" && cfg.levelId > 0) {
        return LocalThumbs::get().loadTexture(cfg.levelId);
    } else if (cfg.type == "random") {
        auto ids = LocalThumbs::get().getAllLevelIDs();
        if (!ids.empty()) {
            static std::mt19937 rng(std::random_device{}());
            std::uniform_int_distribution<size_t> dist(0, ids.size() - 1);
            return LocalThumbs::get().loadTexture(ids[dist(rng)]);
        }
    } else if (cfg.type == "menu") {
        // usar la misma config que el menu principal (nuevo formato unificado)
        LayerBgConfig menuCfg = getConfig("menu");
        if (menuCfg.type == "default") {
            // fallback a legacy keys si la migracion aun no corrio
            std::string menuType = Mod::get()->getSavedValue<std::string>("bg-type", "default");
            if (menuType == "default" || menuType.empty()) return nullptr;
            menuCfg.type = (menuType == "thumbnails") ? "random" : menuType;
            menuCfg.customPath = Mod::get()->getSavedValue<std::string>("bg-custom-path", "");
            menuCfg.levelId = Mod::get()->getSavedValue<int>("bg-id", 0);
        }
        menuCfg.darkMode = cfg.darkMode; // usar dark mode del layer, no del menu
        menuCfg.darkIntensity = cfg.darkIntensity;
        return loadTextureForConfig(menuCfg);
    }
    return nullptr;
}

// ── aplicar fondo estatico ──
void LayerBackgroundManager::applyStaticBg(CCLayer* layer, CCTexture2D* tex, LayerBgConfig const& cfg) {
    log::info("[LayerBgMgr] applyStaticBg: dark={} shader={}", cfg.darkMode, cfg.shader);
    auto winSize = CCDirector::sharedDirector()->getWinSize();

    auto container = CCNode::create();
    container->setContentSize(winSize);
    container->setPosition({0, 0});
    container->setAnchorPoint({0, 0});
    container->setID("paimon-layerbg-container"_spr);
    container->setZOrder(-10);

    bool useShader = !cfg.shader.empty() && cfg.shader != "none";
    CCSprite* sprite = nullptr;

    if (useShader) {
        // Usar ShaderBgSprite que re-aplica uniforms en draw()
        auto shaderSpr = ShaderBgSprite::createWithTexture(tex);
        if (!shaderSpr) return;

        auto* program = getBgShaderProgram(cfg.shader);
        if (program) {
            shaderSpr->setShaderProgram(program);
            shaderSpr->m_shaderIntensity = 0.5f;
            shaderSpr->m_screenW = winSize.width;
            shaderSpr->m_screenH = winSize.height;
            shaderSpr->m_shaderTime = 0.f;
            // Schedule para shaders animados (scanlines, etc.)
            shaderSpr->schedule(schedule_selector(ShaderBgSprite::updateShaderTime));
        }

        sprite = shaderSpr;
    } else {
        sprite = CCSprite::createWithTexture(tex);
        if (!sprite) return;
    }

    float scX = winSize.width / sprite->getContentWidth();
    float scY = winSize.height / sprite->getContentHeight();
    sprite->setScale(std::max(scX, scY));
    sprite->setPosition(winSize / 2);
    sprite->setAnchorPoint({0.5f, 0.5f});


    container->addChild(sprite);

    if (cfg.darkMode) {
        GLubyte alpha = static_cast<GLubyte>(cfg.darkIntensity * 200.f);
        auto overlay = CCLayerColor::create({0, 0, 0, alpha});
        overlay->setContentSize(winSize);
        overlay->setZOrder(1);
        container->addChild(overlay);
    }

    layer->addChild(container);
}

// ── aplicar fondo GIF ──
void LayerBackgroundManager::applyGifBg(CCLayer* layer, std::string const& path, LayerBgConfig const& cfg) {
    log::info("[LayerBgMgr] applyGifBg: path={} dark={}", path, cfg.darkMode);
    auto winSize = CCDirector::sharedDirector()->getWinSize();

    auto container = CCNode::create();
    container->setContentSize(winSize);
    container->setPosition({0, 0});
    container->setAnchorPoint({0, 0});
    container->setID("paimon-layerbg-container"_spr);
    container->setZOrder(-10);
    layer->addChild(container);

    Ref<CCLayer> layerRef = layer;
    Ref<CCNode> containerRef = container;
    bool darkMode = cfg.darkMode;
    float darkIntensity = cfg.darkIntensity;
    std::string shaderName = cfg.shader;

    AnimatedGIFSprite::pinGIF(path);
    AnimatedGIFSprite::createAsync(path, [layerRef, containerRef, winSize, darkMode, darkIntensity, shaderName](AnimatedGIFSprite* anim) {
        auto layer = layerRef.data();
        auto container = containerRef.data();
        if (!anim || !container->getParent()) return;

        float cw = anim->getContentWidth();
        float ch = anim->getContentHeight();
        if (cw <= 0 || ch <= 0) return;

        float sc = std::max(winSize.width / cw, winSize.height / ch);
        anim->setAnchorPoint({0.5f, 0.5f});
        anim->setPosition(winSize / 2);
        anim->setScale(sc);

        // Aplicar shader al GIF (AnimatedGIFSprite ya re-aplica uniforms en draw())
        if (!shaderName.empty() && shaderName != "none") {
            auto* program = getBgShaderProgram(shaderName);
            if (program) {
                anim->setShaderProgram(program);
                anim->m_intensity = 0.5f;
                anim->m_texSize = CCSize(winSize.width, winSize.height);
            }
        }

        container->addChild(anim);

        if (darkMode) {
            GLubyte alpha = static_cast<GLubyte>(darkIntensity * 200.f);
            auto overlay = CCLayerColor::create({0, 0, 0, alpha});
            overlay->setContentSize(winSize);
            overlay->setZOrder(1);
            container->addChild(overlay);
        }
    });
}

// ── API principal ──
bool LayerBackgroundManager::applyBackground(CCLayer* layer, std::string const& layerKey) {
    log::info("[LayerBgMgr] applyBackground: layerKey={}", layerKey);
    auto cfg = getConfig(layerKey);

    // Siempre limpiar container previo si existe
    if (auto oldContainer = layer->getChildByID("paimon-layerbg-container"_spr)) {
        oldContainer->removeFromParent();
    }

    if (cfg.type == "default") return false; // no tocar

    // resolver referencias a otros layers (evitar ciclos, max 5 saltos)
    std::string resolvedPath = cfg.customPath;
    std::string resolvedType = cfg.type;
    LayerBgConfig resolvedCfg = cfg;
    int maxHops = 5;

    while (maxHops-- > 0) {
        if (resolvedType == "menu") {
            // Check unified config first
            LayerBgConfig menuCfg = getConfig("menu");
            if (menuCfg.type != "default") {
                resolvedType = menuCfg.type;
                resolvedPath = menuCfg.customPath;
                resolvedCfg.type = menuCfg.type;
                resolvedCfg.customPath = menuCfg.customPath;
                resolvedCfg.levelId = menuCfg.levelId;
                // Keep original dark mode / shader settings
                continue; // keep resolving in case menu points to another layer ref
            } else {
                // Fallback to legacy keys (for pre-migration compat)
                std::string menuType = Mod::get()->getSavedValue<std::string>("bg-type", "default");
                if (menuType == "custom") {
                    resolvedPath = Mod::get()->getSavedValue<std::string>("bg-custom-path", "");
                    resolvedType = "custom";
                    resolvedCfg.type = "custom";
                    resolvedCfg.customPath = resolvedPath;
                } else if (menuType == "thumbnails" || menuType == "random") {
                    resolvedType = "random";
                    resolvedCfg.type = "random";
                } else if (menuType == "id") {
                    resolvedType = "id";
                    resolvedCfg.type = "id";
                    resolvedCfg.levelId = Mod::get()->getSavedValue<int>("bg-id", 0);
                } else {
                    return false; // menu is default
                }
                break;
            }
        }

        // ¿es referencia a otro layer? (creator, browser, search, leaderboards)
        bool isLayerRef = false;
        for (auto& [k, n] : LAYER_OPTIONS) {
            if (resolvedType == k) { isLayerRef = true; break; }
        }
        if (isLayerRef) {
            // cargar config del layer referenciado
            resolvedCfg = getConfig(resolvedType);
            resolvedCfg.darkMode = cfg.darkMode; // mantener dark mode del original
            resolvedCfg.darkIntensity = cfg.darkIntensity;
            resolvedType = resolvedCfg.type;
            resolvedPath = resolvedCfg.customPath;
            if (resolvedType == "default") return false;
            continue;
        }

        break; // tipo concreto (custom, random, id)
    }

    // verificar si es GIF
    if (resolvedType == "custom" && !resolvedPath.empty()) {
        auto ext = geode::utils::string::pathToString(std::filesystem::path(resolvedPath).extension());
        for (auto& c : ext) c = (char)std::tolower(c);
        std::error_code ec;
        if (ext == ".gif" && std::filesystem::exists(resolvedPath, ec)) {
            hideOriginalBg(layer);
            applyGifBg(layer, resolvedPath, cfg);
            return true;
        }
    }

    auto* tex = loadTextureForConfig(resolvedCfg);
    if (tex) {
        hideOriginalBg(layer);
        applyStaticBg(layer, tex, cfg);
        return true;
    }

    // Texture not in local cache — try async download for "id" type
    if (resolvedCfg.type == "id" && resolvedCfg.levelId > 0) {
        Ref<CCLayer> layerRef = layer;
        LayerBgConfig capturedCfg = cfg;
        ThumbnailAPI::get().getThumbnail(resolvedCfg.levelId, [this, layerRef, capturedCfg](bool success, CCTexture2D* dlTex) {
            auto layer = layerRef.data();
            if (success && dlTex && layer->getParent()) {
                hideOriginalBg(layer);
                applyStaticBg(layer, dlTex, capturedCfg);
            }
        });
        return true; // se esta descargando, se aplicara async
    }

    return false;
}



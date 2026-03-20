#include "TransitionManager.hpp"
#include "../ui/CustomTransitionScene.hpp"
#include <Geode/utils/file.hpp>
#include <Geode/loader/Log.hpp>
#include <matjson.hpp>
#include <filesystem>
#include <cmath>
#include <exception>

using namespace geode::prelude;
using namespace cocos2d;

// ════════════════════════════════════════════════════════════
// Singleton
// ════════════════════════════════════════════════════════════

TransitionManager::TransitionManager() {
    m_globalConfig.type = TransitionType::Fade;
    m_globalConfig.duration = 0.5f;
}

TransitionManager& TransitionManager::get() {
    static TransitionManager instance;
    return instance;
}

void TransitionManager::tripCustomSafeMode(std::string const& reason) {
    if (!m_customSafeModeTripped) {
        log::warn("[TransitionManager] Custom safe mode activated: {}", reason);
    }
    m_customSafeModeTripped = true;
}

void TransitionManager::resetCustomSafeMode() {
    if (m_customSafeModeTripped) {
        log::info("[TransitionManager] Custom safe mode reset");
    }
    m_customSafeModeTripped = false;
}

// ════════════════════════════════════════════════════════════
// allTypes — lista ordenada para la UI
// ════════════════════════════════════════════════════════════

std::vector<TransitionType> const& TransitionManager::allTypes() {
    static const std::vector<TransitionType> types = {
        TransitionType::Fade,
        TransitionType::FadeWhite,
        TransitionType::FadeColor,
        TransitionType::CrossFade,
        TransitionType::SlideLeft,
        TransitionType::SlideRight,
        TransitionType::SlideUp,
        TransitionType::SlideDown,
        TransitionType::MoveInLeft,
        TransitionType::MoveInRight,
        TransitionType::ZoomIn,
        TransitionType::ZoomOut,
        TransitionType::FlipX,
        TransitionType::FlipY,
        TransitionType::FlipAngular,
        TransitionType::ShrinkGrow,
        TransitionType::RotoZoom,
        TransitionType::JumpZoom,
        TransitionType::FadeTR,
        TransitionType::FadeBL,
        TransitionType::TurnOffTiles,
        TransitionType::SplitCols,
        TransitionType::SplitRows,
        TransitionType::ProgressRadialCW,
        TransitionType::ProgressRadialCCW,
        TransitionType::ProgressInOut,
        TransitionType::PageForward,
        TransitionType::PageBackward,
        TransitionType::Custom,
        TransitionType::None,
    };
    return types;
}

// ════════════════════════════════════════════════════════════
// String <-> Enum
// ════════════════════════════════════════════════════════════

TransitionType TransitionManager::typeFromString(std::string const& s) {
    if (s == "fade")             return TransitionType::Fade;
    if (s == "fade_white")       return TransitionType::FadeWhite;
    if (s == "fade_color")       return TransitionType::FadeColor;
    if (s == "crossfade")        return TransitionType::CrossFade;
    if (s == "slide_left")       return TransitionType::SlideLeft;
    if (s == "slide_right")      return TransitionType::SlideRight;
    if (s == "slide_up")         return TransitionType::SlideUp;
    if (s == "slide_down")       return TransitionType::SlideDown;
    if (s == "move_in_left")     return TransitionType::MoveInLeft;
    if (s == "move_in_right")    return TransitionType::MoveInRight;
    if (s == "zoom_in")          return TransitionType::ZoomIn;
    if (s == "zoom_out")         return TransitionType::ZoomOut;
    if (s == "flip_x")           return TransitionType::FlipX;
    if (s == "flip_y")           return TransitionType::FlipY;
    if (s == "flip_angular")     return TransitionType::FlipAngular;
    if (s == "shrink_grow")      return TransitionType::ShrinkGrow;
    if (s == "roto_zoom")        return TransitionType::RotoZoom;
    if (s == "jump_zoom")        return TransitionType::JumpZoom;
    if (s == "fade_tr")          return TransitionType::FadeTR;
    if (s == "fade_bl")          return TransitionType::FadeBL;
    if (s == "turn_off_tiles")   return TransitionType::TurnOffTiles;
    if (s == "split_cols")       return TransitionType::SplitCols;
    if (s == "split_rows")       return TransitionType::SplitRows;
    if (s == "progress_cw")      return TransitionType::ProgressRadialCW;
    if (s == "progress_ccw")     return TransitionType::ProgressRadialCCW;
    if (s == "progress_inout")   return TransitionType::ProgressInOut;
    if (s == "page_forward")     return TransitionType::PageForward;
    if (s == "page_backward")    return TransitionType::PageBackward;
    if (s == "custom")           return TransitionType::Custom;
    if (s == "none")             return TransitionType::None;
    return TransitionType::Fade;
}

std::string TransitionManager::typeToString(TransitionType t) {
    switch (t) {
        case TransitionType::Fade:              return "fade";
        case TransitionType::FadeWhite:         return "fade_white";
        case TransitionType::FadeColor:         return "fade_color";
        case TransitionType::CrossFade:         return "crossfade";
        case TransitionType::SlideLeft:         return "slide_left";
        case TransitionType::SlideRight:        return "slide_right";
        case TransitionType::SlideUp:           return "slide_up";
        case TransitionType::SlideDown:         return "slide_down";
        case TransitionType::MoveInLeft:        return "move_in_left";
        case TransitionType::MoveInRight:       return "move_in_right";
        case TransitionType::ZoomIn:            return "zoom_in";
        case TransitionType::ZoomOut:           return "zoom_out";
        case TransitionType::FlipX:             return "flip_x";
        case TransitionType::FlipY:             return "flip_y";
        case TransitionType::FlipAngular:       return "flip_angular";
        case TransitionType::ShrinkGrow:        return "shrink_grow";
        case TransitionType::RotoZoom:          return "roto_zoom";
        case TransitionType::JumpZoom:          return "jump_zoom";
        case TransitionType::FadeTR:            return "fade_tr";
        case TransitionType::FadeBL:            return "fade_bl";
        case TransitionType::TurnOffTiles:      return "turn_off_tiles";
        case TransitionType::SplitCols:         return "split_cols";
        case TransitionType::SplitRows:         return "split_rows";
        case TransitionType::ProgressRadialCW:  return "progress_cw";
        case TransitionType::ProgressRadialCCW: return "progress_ccw";
        case TransitionType::ProgressInOut:     return "progress_inout";
        case TransitionType::PageForward:       return "page_forward";
        case TransitionType::PageBackward:      return "page_backward";
        case TransitionType::Custom:            return "custom";
        case TransitionType::None:              return "none";
    }
    return "fade";
}

std::string TransitionManager::typeDisplayName(TransitionType t) {
    switch (t) {
        case TransitionType::Fade:              return "Fade (Black)";
        case TransitionType::FadeWhite:         return "Fade (White)";
        case TransitionType::FadeColor:         return "Fade (Color)";
        case TransitionType::CrossFade:         return "Cross Fade";
        case TransitionType::SlideLeft:         return "Slide Left";
        case TransitionType::SlideRight:        return "Slide Right";
        case TransitionType::SlideUp:           return "Slide Up";
        case TransitionType::SlideDown:         return "Slide Down";
        case TransitionType::MoveInLeft:        return "Move In Left";
        case TransitionType::MoveInRight:       return "Move In Right";
        case TransitionType::ZoomIn:            return "Zoom Flip In";
        case TransitionType::ZoomOut:           return "Zoom Flip Out";
        case TransitionType::FlipX:             return "Flip Horizontal";
        case TransitionType::FlipY:             return "Flip Vertical";
        case TransitionType::FlipAngular:       return "Flip Angular";
        case TransitionType::ShrinkGrow:        return "Shrink & Grow";
        case TransitionType::RotoZoom:          return "Roto Zoom";
        case TransitionType::JumpZoom:          return "Jump Zoom";
        case TransitionType::FadeTR:            return "Tiles (Top-Right)";
        case TransitionType::FadeBL:            return "Tiles (Bottom-Left)";
        case TransitionType::TurnOffTiles:      return "Turn Off Tiles";
        case TransitionType::SplitCols:         return "Split Columns";
        case TransitionType::SplitRows:         return "Split Rows";
        case TransitionType::ProgressRadialCW:  return "Radial CW";
        case TransitionType::ProgressRadialCCW: return "Radial CCW";
        case TransitionType::ProgressInOut:     return "Circle Iris";
        case TransitionType::PageForward:       return "Page Curl ->";
        case TransitionType::PageBackward:      return "Page Curl <-";
        case TransitionType::Custom:            return "Custom (DSL)";
        case TransitionType::None:              return "None (Instant)";
    }
    return "Fade";
}

std::string TransitionManager::typeDescription(TransitionType t) {
    switch (t) {
        case TransitionType::Fade:              return "Fades to black, then reveals the new screen.";
        case TransitionType::FadeWhite:         return "Fades to white, then reveals the new screen.";
        case TransitionType::FadeColor:         return "Fades to a color you pick, then reveals.";
        case TransitionType::CrossFade:         return "Both screens blend together smoothly.";
        case TransitionType::SlideLeft:         return "New screen slides in from the left.";
        case TransitionType::SlideRight:        return "New screen slides in from the right.";
        case TransitionType::SlideUp:           return "New screen slides in from the top.";
        case TransitionType::SlideDown:         return "New screen slides in from the bottom.";
        case TransitionType::MoveInLeft:        return "New screen moves over the old one from left.";
        case TransitionType::MoveInRight:       return "New screen moves over the old one from right.";
        case TransitionType::ZoomIn:            return "3D zoom flip to reveal the new screen.";
        case TransitionType::ZoomOut:           return "3D zoom flip outward to the new screen.";
        case TransitionType::FlipX:             return "Flips horizontally like a card.";
        case TransitionType::FlipY:             return "Flips vertically like a card.";
        case TransitionType::FlipAngular:       return "Flips diagonally for a dynamic look.";
        case TransitionType::ShrinkGrow:        return "Old screen shrinks, new one grows in.";
        case TransitionType::RotoZoom:          return "Rotates and zooms between screens.";
        case TransitionType::JumpZoom:          return "Jumps and zooms to the new screen.";
        case TransitionType::FadeTR:            return "Tiles appear from top-right corner.";
        case TransitionType::FadeBL:            return "Tiles appear from bottom-left corner.";
        case TransitionType::TurnOffTiles:      return "Tiles randomly disappear like a TV off.";
        case TransitionType::SplitCols:         return "Screen splits into vertical columns.";
        case TransitionType::SplitRows:         return "Screen splits into horizontal rows.";
        case TransitionType::ProgressRadialCW:  return "Circular wipe clockwise, like a clock.";
        case TransitionType::ProgressRadialCCW: return "Circular wipe counter-clockwise.";
        case TransitionType::ProgressInOut:     return "Circle iris opens/closes like a camera.";
        case TransitionType::PageForward:       return "Page curls forward like a book.";
        case TransitionType::PageBackward:      return "Page curls backward.";
        case TransitionType::Custom:            return "Define your own transition with commands!";
        case TransitionType::None:              return "Instant switch, no animation.";
    }
    return "";
}

CommandAction TransitionManager::actionFromString(std::string const& s) {
    if (s == "fade_out")  return CommandAction::FadeOut;
    if (s == "fade_in")   return CommandAction::FadeIn;
    if (s == "move")      return CommandAction::Move;
    if (s == "scale")     return CommandAction::Scale;
    if (s == "rotate")    return CommandAction::Rotate;
    if (s == "wait")      return CommandAction::Wait;
    if (s == "color")     return CommandAction::Color;
    if (s == "ease_in")   return CommandAction::EaseIn;
    if (s == "ease_out")  return CommandAction::EaseOut;
    if (s == "spawn")     return CommandAction::Spawn;
    if (s == "image")     return CommandAction::Image;
    if (s == "shake")     return CommandAction::Shake;
    if (s == "bounce")    return CommandAction::Bounce;
    return CommandAction::Wait;
}

std::string TransitionManager::actionToString(CommandAction a) {
    switch (a) {
        case CommandAction::FadeOut:    return "fade_out";
        case CommandAction::FadeIn:     return "fade_in";
        case CommandAction::Move:       return "move";
        case CommandAction::Scale:      return "scale";
        case CommandAction::Rotate:     return "rotate";
        case CommandAction::Wait:       return "wait";
        case CommandAction::Color:      return "color";
        case CommandAction::EaseIn:     return "ease_in";
        case CommandAction::EaseOut:    return "ease_out";
        case CommandAction::Spawn:      return "spawn";
        case CommandAction::Image:      return "image";
        case CommandAction::Shake:      return "shake";
        case CommandAction::Bounce:     return "bounce";
    }
    return "wait";
}

bool TransitionManager::isValidTarget(std::string const& target) {
    return target == "from" || target == "to";
}

int TransitionManager::sanitizeCommand(TransitionCommand& cmd) {
    int fixes = 0;

    auto sanitizeFloat = [&fixes](float& value, float fallback) {
        if (!std::isfinite(value)) {
            value = fallback;
            fixes++;
        }
    };

    sanitizeFloat(cmd.duration, 0.3f);
    sanitizeFloat(cmd.delay, 0.f);
    sanitizeFloat(cmd.fromX, 0.f);
    sanitizeFloat(cmd.fromY, 0.f);
    sanitizeFloat(cmd.toX, 0.f);
    sanitizeFloat(cmd.toY, 0.f);
    sanitizeFloat(cmd.fromVal, 1.f);
    sanitizeFloat(cmd.toVal, 1.f);
    sanitizeFloat(cmd.intensity, 5.f);

    if (!isValidTarget(cmd.target)) {
        cmd.target = "from";
        fixes++;
    }

    if (cmd.duration < 0.01f) {
        cmd.duration = 0.01f;
        fixes++;
    } else if (cmd.duration > 30.f) {
        cmd.duration = 30.f;
        fixes++;
    }

    if (cmd.delay < 0.f) {
        cmd.delay = 0.f;
        fixes++;
    } else if (cmd.delay > 10.f) {
        cmd.delay = 10.f;
        fixes++;
    }

    if (cmd.action == CommandAction::Scale) {
        float newFrom = std::clamp(cmd.fromVal, 0.01f, 10.f);
        float newTo = std::clamp(cmd.toVal, 0.01f, 10.f);
        if (newFrom != cmd.fromVal) { cmd.fromVal = newFrom; fixes++; }
        if (newTo != cmd.toVal) { cmd.toVal = newTo; fixes++; }
    } else if (
        cmd.action == CommandAction::FadeOut ||
        cmd.action == CommandAction::FadeIn ||
        cmd.action == CommandAction::EaseIn ||
        cmd.action == CommandAction::EaseOut ||
        cmd.action == CommandAction::Bounce
    ) {
        float newFrom = std::clamp(cmd.fromVal, 0.f, 255.f);
        float newTo = std::clamp(cmd.toVal, 0.f, 255.f);
        if (newFrom != cmd.fromVal) { cmd.fromVal = newFrom; fixes++; }
        if (newTo != cmd.toVal) { cmd.toVal = newTo; fixes++; }
    }

    if (cmd.action == CommandAction::Color) {
        int nr = std::clamp(cmd.r, 0, 255);
        int ng = std::clamp(cmd.g, 0, 255);
        int nb = std::clamp(cmd.b, 0, 255);
        if (nr != cmd.r) { cmd.r = nr; fixes++; }
        if (ng != cmd.g) { cmd.g = ng; fixes++; }
        if (nb != cmd.b) { cmd.b = nb; fixes++; }
    }

    if (cmd.action == CommandAction::Shake) {
        float ni = std::clamp(cmd.intensity, 0.5f, 50.f);
        if (ni != cmd.intensity) {
            cmd.intensity = ni;
            fixes++;
        }
    }

    if (cmd.action == CommandAction::Spawn) {
        int sc = std::clamp(cmd.spawnCount, 0, 16);
        if (sc != cmd.spawnCount) {
            cmd.spawnCount = sc;
            fixes++;
        }
    }

    return fixes;
}

int TransitionManager::sanitizeCommands(std::vector<TransitionCommand>& commands) {
    int fixes = 0;
    for (auto& cmd : commands) {
        fixes += sanitizeCommand(cmd);
    }

    if (commands.empty()) {
        commands.push_back({CommandAction::FadeOut, "from", 0.15f, 0, 0, 0, 0, 255.f, 0.f});
        commands.push_back({CommandAction::FadeIn, "to", 0.15f, 0, 0, 0, 0, 0.f, 255.f});
        fixes += 2;
    }
    return fixes;
}

int TransitionManager::sanitizeConfig(TransitionConfig& cfg) {
    int fixes = 0;
    if (!std::isfinite(cfg.duration) || cfg.duration < 0.01f) {
        cfg.duration = 0.5f;
        fixes++;
    } else if (cfg.duration > 30.f) {
        cfg.duration = 30.f;
        fixes++;
    }

    int nr = std::clamp(cfg.colorR, 0, 255);
    int ng = std::clamp(cfg.colorG, 0, 255);
    int nb = std::clamp(cfg.colorB, 0, 255);
    if (nr != cfg.colorR) { cfg.colorR = nr; fixes++; }
    if (ng != cfg.colorG) { cfg.colorG = ng; fixes++; }
    if (nb != cfg.colorB) { cfg.colorB = nb; fixes++; }

    if (cfg.type == TransitionType::Custom) {
        fixes += sanitizeCommands(cfg.commands);
    }

    return fixes;
}

// ════════════════════════════════════════════════════════════
// Config path
// ════════════════════════════════════════════════════════════

std::filesystem::path TransitionManager::getConfigPath() const {
    return Mod::get()->getSaveDir() / "transitions.json";
}

// ════════════════════════════════════════════════════════════
// JSON helpers
// ════════════════════════════════════════════════════════════

static TransitionCommand parseCommand(matjson::Value const& obj) {
    TransitionCommand cmd;
    if (!obj.isObject()) return cmd;
    if (obj.contains("action"))   cmd.action   = TransitionManager::actionFromString(obj["action"].asString().unwrapOr("wait"));
    if (obj.contains("target"))   cmd.target   = obj["target"].asString().unwrapOr("from");
    if (obj.contains("duration")) cmd.duration = static_cast<float>(obj["duration"].asDouble().unwrapOr(0.3));
    if (obj.contains("from_x"))   cmd.fromX    = static_cast<float>(obj["from_x"].asDouble().unwrapOr(0));
    if (obj.contains("from_y"))   cmd.fromY    = static_cast<float>(obj["from_y"].asDouble().unwrapOr(0));
    if (obj.contains("to_x"))     cmd.toX      = static_cast<float>(obj["to_x"].asDouble().unwrapOr(0));
    if (obj.contains("to_y"))     cmd.toY      = static_cast<float>(obj["to_y"].asDouble().unwrapOr(0));
    if (obj.contains("from_val")) cmd.fromVal  = static_cast<float>(obj["from_val"].asDouble().unwrapOr(1));
    if (obj.contains("to_val"))   cmd.toVal    = static_cast<float>(obj["to_val"].asDouble().unwrapOr(1));
    if (obj.contains("r")) cmd.r = static_cast<int>(obj["r"].asInt().unwrapOr(0));
    if (obj.contains("g")) cmd.g = static_cast<int>(obj["g"].asInt().unwrapOr(0));
    if (obj.contains("b")) cmd.b = static_cast<int>(obj["b"].asInt().unwrapOr(0));
    if (obj.contains("image"))      cmd.imagePath  = obj["image"].asString().unwrapOr("");
    if (obj.contains("spawn_count")) cmd.spawnCount = static_cast<int>(obj["spawn_count"].asInt().unwrapOr(0));
    if (obj.contains("delay"))      cmd.delay      = static_cast<float>(obj["delay"].asDouble().unwrapOr(0));
    if (obj.contains("intensity"))  cmd.intensity  = static_cast<float>(obj["intensity"].asDouble().unwrapOr(5));
    return cmd;
}

static TransitionConfig parseConfig(matjson::Value const& obj) {
    TransitionConfig cfg;
    if (!obj.isObject()) return cfg;
    if (obj.contains("type"))     cfg.type     = TransitionManager::typeFromString(obj["type"].asString().unwrapOr("fade"));
    if (obj.contains("duration")) cfg.duration = static_cast<float>(obj["duration"].asDouble().unwrapOr(0.5));
    if (obj.contains("color") && obj["color"].isArray()) {
        auto arr = obj["color"].asArray().unwrapOr(std::vector<matjson::Value>{});
        if (arr.size() >= 3) {
            cfg.colorR = static_cast<int>(arr[0].asInt().unwrapOr(0));
            cfg.colorG = static_cast<int>(arr[1].asInt().unwrapOr(0));
            cfg.colorB = static_cast<int>(arr[2].asInt().unwrapOr(0));
        }
    }
    if (obj.contains("image"))    cfg.imagePath  = obj["image"].asString().unwrapOr("");
    if (obj.contains("script"))   cfg.scriptPath = obj["script"].asString().unwrapOr("");
    if (obj.contains("images") && obj["images"].isArray()) {
        for (auto const& v : obj["images"].asArray().unwrapOr(std::vector<matjson::Value>{})) {
            if (v.isString()) cfg.imageList.push_back(v.asString().unwrapOr(""));
        }
    }
    if (obj.contains("commands") && obj["commands"].isArray()) {
        for (auto const& c : obj["commands"].asArray().unwrapOr(std::vector<matjson::Value>{})) {
            cfg.commands.push_back(parseCommand(c));
        }
    }
    return cfg;
}

static matjson::Value commandToJson(TransitionCommand const& cmd) {
    matjson::Value obj = matjson::makeObject({});
    obj.set("action",   TransitionManager::actionToString(cmd.action));
    obj.set("target",   cmd.target);
    obj.set("duration", static_cast<double>(cmd.duration));
    if (cmd.delay > 0.f) obj.set("delay", static_cast<double>(cmd.delay));
    if (cmd.action == CommandAction::Move) {
        obj.set("from_x", static_cast<double>(cmd.fromX));
        obj.set("from_y", static_cast<double>(cmd.fromY));
        obj.set("to_x",   static_cast<double>(cmd.toX));
        obj.set("to_y",   static_cast<double>(cmd.toY));
    }
    if (cmd.action != CommandAction::Move && cmd.action != CommandAction::Wait &&
        cmd.action != CommandAction::Color && cmd.action != CommandAction::Spawn &&
        cmd.action != CommandAction::Image) {
        obj.set("from_val", static_cast<double>(cmd.fromVal));
        obj.set("to_val",   static_cast<double>(cmd.toVal));
    }
    if (cmd.action == CommandAction::Color) {
        obj.set("r", cmd.r); obj.set("g", cmd.g); obj.set("b", cmd.b);
    }
    if (cmd.action == CommandAction::Spawn) {
        obj.set("spawn_count", cmd.spawnCount);
    }
    if (cmd.action == CommandAction::Image && !cmd.imagePath.empty()) {
        obj.set("image", cmd.imagePath);
    }
    if (cmd.action == CommandAction::Shake) {
        obj.set("intensity", static_cast<double>(cmd.intensity));
    }
    return obj;
}

static matjson::Value configToJson(TransitionConfig const& cfg) {
    matjson::Value obj = matjson::makeObject({});
    obj.set("type", TransitionManager::typeToString(cfg.type));
    obj.set("duration", static_cast<double>(cfg.duration));
    auto colorArr = matjson::Value::array();
    colorArr.push(cfg.colorR); colorArr.push(cfg.colorG); colorArr.push(cfg.colorB);
    obj.set("color", colorArr);
    if (!cfg.imagePath.empty())  obj.set("image", cfg.imagePath);
    if (!cfg.scriptPath.empty()) obj.set("script", cfg.scriptPath);
    if (!cfg.imageList.empty()) {
        auto imgArr = matjson::Value::array();
        for (auto const& img : cfg.imageList) imgArr.push(img);
        obj.set("images", imgArr);
    }
    if (!cfg.commands.empty()) {
        auto cmdsArr = matjson::Value::array();
        for (auto const& cmd : cfg.commands) cmdsArr.push(commandToJson(cmd));
        obj.set("commands", cmdsArr);
    }
    return obj;
}

// ════════════════════════════════════════════════════════════
// Load / Save
// ════════════════════════════════════════════════════════════

void TransitionManager::loadConfig() {
    auto path = getConfigPath();

    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        log::info("[TransitionManager] No config found, using defaults");
        m_loaded = true;
        saveConfig();
        return;
    }

    auto readRes = geode::utils::file::readString(path);
    if (!readRes) { m_loaded = true; return; }

    auto parseRes = matjson::parse(readRes.unwrap());
    if (!parseRes) { m_loaded = true; return; }

    auto& root = parseRes.unwrap();
    if (!root.isObject()) {
        m_loaded = true;
        log::warn("[TransitionManager] Invalid config root type, using defaults");
        return;
    }

    if (root.contains("enabled"))
        m_enabled = root["enabled"].asBool().unwrapOr(true);

    if (root.contains("global") && root["global"].isObject())
        m_globalConfig = parseConfig(root["global"]);
    else if (root.contains("global"))
        log::warn("[TransitionManager] Invalid global config type, using defaults");

    if (root.contains("level_entry") && root["level_entry"].isObject()) {
        m_levelEntryConfig = parseConfig(root["level_entry"]);
        m_hasLevelEntryConfig = true;
        if (!m_levelEntryConfig.scriptPath.empty() && m_levelEntryConfig.commands.empty()) {
            m_levelEntryConfig.commands = parseScriptFile(m_levelEntryConfig.scriptPath);
            if (!m_levelEntryConfig.commands.empty()) m_levelEntryConfig.type = TransitionType::Custom;
        }
    } else if (root.contains("level_entry")) {
        log::warn("[TransitionManager] Invalid level_entry config type, ignoring");
    }

    if (!m_globalConfig.scriptPath.empty() && m_globalConfig.commands.empty()) {
        m_globalConfig.commands = parseScriptFile(m_globalConfig.scriptPath);
        if (!m_globalConfig.commands.empty()) m_globalConfig.type = TransitionType::Custom;
    }

    sanitizeConfig(m_globalConfig);
    if (m_hasLevelEntryConfig) {
        sanitizeConfig(m_levelEntryConfig);
    }

    m_loaded = true;
    log::info("[TransitionManager] Config loaded (enabled={}, hasLevelEntry={})", m_enabled, m_hasLevelEntryConfig);
}

void TransitionManager::saveConfig() {
    matjson::Value root = matjson::makeObject({});
    root.set("enabled", m_enabled);
    root.set("global", configToJson(m_globalConfig));
    if (m_hasLevelEntryConfig) {
        root.set("level_entry", configToJson(m_levelEntryConfig));
    }

    auto str = root.dump(matjson::TAB_INDENTATION);
    auto res = geode::utils::file::writeString(getConfigPath(), str);
    if (!res) log::warn("[TransitionManager] Failed to save: {}", res.unwrapErr());
    else log::info("[TransitionManager] Config saved");
}

// ════════════════════════════════════════════════════════════
// Level entry config
// ════════════════════════════════════════════════════════════

TransitionConfig TransitionManager::getLevelEntryConfig() const {
    return m_hasLevelEntryConfig ? m_levelEntryConfig : m_globalConfig;
}

void TransitionManager::setLevelEntryConfig(TransitionConfig const& cfg) {
    m_levelEntryConfig = cfg;
    m_hasLevelEntryConfig = true;
}

void TransitionManager::clearLevelEntryConfig() {
    m_hasLevelEntryConfig = false;
    m_levelEntryConfig = TransitionConfig{};
}

// ════════════════════════════════════════════════════════════
// createTransition
// ════════════════════════════════════════════════════════════

CCScene* TransitionManager::createTransition(TransitionConfig const& cfg, CCScene* dest) {
    if (!m_loaded) loadConfig();

    if (cfg.type == TransitionType::None) return dest;

    TransitionConfig safeCfg = cfg;
    sanitizeConfig(safeCfg);

    if (safeCfg.type == TransitionType::Custom) {
        if (m_customSafeModeTripped) {
            return CCTransitionFade::create(safeCfg.duration, dest);
        }

        auto commands = safeCfg.commands;
        if (commands.empty() && !safeCfg.scriptPath.empty())
            commands = parseScriptFile(safeCfg.scriptPath);
        sanitizeCommands(commands);

        try {
            auto fromScene = CCDirector::sharedDirector()->getRunningScene();
            auto* transScene = CustomTransitionScene::create(fromScene, dest, commands, false);
            if (transScene) return transScene;
        } catch (std::exception const& e) {
            tripCustomSafeMode(std::string("createTransition exception: ") + e.what());
        } catch (...) {
            tripCustomSafeMode("createTransition unknown exception");
        }
        return CCTransitionFade::create(safeCfg.duration, dest);
    }

    auto* trans = createNativeTransition(safeCfg, dest);
    return trans ? static_cast<CCScene*>(trans) : dest;
}

// ════════════════════════════════════════════════════════════
// Convenience methods
//
// Estos metodos aplican la transicion global configurada por el usuario
// al navegar a escenas propias del mod (PaiConfigLayer, LeaderboardLayer, etc.).
// Si las transiciones estan deshabilitadas o no hay config cargada,
// hacen la navegacion directa sin efectos.
// ════════════════════════════════════════════════════════════

void TransitionManager::replaceScene(CCScene* dest) {
    if (!dest) return;

    try {
        if (m_enabled) {
            if (!m_loaded) loadConfig();
            auto* trans = createTransition(m_globalConfig, dest);
            CCDirector::sharedDirector()->replaceScene(trans ? trans : dest);
        } else {
            CCDirector::sharedDirector()->replaceScene(dest);
        }
    } catch (std::exception const& e) {
        tripCustomSafeMode(std::string("replaceScene exception: ") + e.what());
        CCDirector::sharedDirector()->replaceScene(CCTransitionFade::create(0.3f, dest));
    } catch (...) {
        tripCustomSafeMode("replaceScene unknown exception");
        CCDirector::sharedDirector()->replaceScene(CCTransitionFade::create(0.3f, dest));
    }
}

void TransitionManager::pushScene(CCScene* dest) {
    if (!dest) return;

    try {
        if (m_enabled) {
            if (!m_loaded) loadConfig();
            auto* trans = createTransition(m_globalConfig, dest);
            CCDirector::sharedDirector()->pushScene(trans ? trans : dest);
        } else {
            CCDirector::sharedDirector()->pushScene(dest);
        }
    } catch (std::exception const& e) {
        tripCustomSafeMode(std::string("pushScene exception: ") + e.what());
        CCDirector::sharedDirector()->pushScene(CCTransitionFade::create(0.3f, dest));
    } catch (...) {
        tripCustomSafeMode("pushScene unknown exception");
        CCDirector::sharedDirector()->pushScene(CCTransitionFade::create(0.3f, dest));
    }
}

// ════════════════════════════════════════════════════════════
// Native transitions
// ════════════════════════════════════════════════════════════

CCTransitionScene* TransitionManager::createNativeTransition(TransitionConfig const& cfg, CCScene* dest) const {
    float dur = cfg.duration;
    ccColor3B col = {
        static_cast<GLubyte>(cfg.colorR),
        static_cast<GLubyte>(cfg.colorG),
        static_cast<GLubyte>(cfg.colorB)
    };

    switch (cfg.type) {
        // Fades (CCTransitionFade is available on all platforms)
        case TransitionType::Fade:              return CCTransitionFade::create(dur, dest, {0, 0, 0});
        case TransitionType::FadeWhite:         return CCTransitionFade::create(dur, dest, {255, 255, 255});
        case TransitionType::FadeColor:         return CCTransitionFade::create(dur, dest, col);

#if !defined(GEODE_IS_IOS)
        case TransitionType::CrossFade:         return CCTransitionCrossFade::create(dur, dest);

        // Slides
        case TransitionType::SlideLeft:         return CCTransitionSlideInL::create(dur, dest);
        case TransitionType::SlideRight:        return CCTransitionSlideInR::create(dur, dest);
        case TransitionType::SlideUp:           return CCTransitionSlideInT::create(dur, dest);
        case TransitionType::SlideDown:         return CCTransitionSlideInB::create(dur, dest);
        case TransitionType::MoveInLeft:        return CCTransitionMoveInL::create(dur, dest);
        case TransitionType::MoveInRight:       return CCTransitionMoveInR::create(dur, dest);

        // Zooms & Flips
        case TransitionType::ZoomIn:            return CCTransitionZoomFlipX::create(dur, dest, tOrientation::kCCTransitionOrientationLeftOver);
        case TransitionType::ZoomOut:           return CCTransitionZoomFlipX::create(dur, dest, tOrientation::kCCTransitionOrientationRightOver);
        case TransitionType::FlipX:             return CCTransitionFlipX::create(dur, dest, tOrientation::kCCTransitionOrientationLeftOver);
        case TransitionType::FlipY:             return CCTransitionFlipY::create(dur, dest, tOrientation::kCCTransitionOrientationUpOver);
        case TransitionType::FlipAngular:       return CCTransitionFlipAngular::create(dur, dest, tOrientation::kCCTransitionOrientationLeftOver);
        case TransitionType::ShrinkGrow:        return CCTransitionShrinkGrow::create(dur, dest);
        case TransitionType::RotoZoom:          return CCTransitionRotoZoom::create(dur, dest);
        case TransitionType::JumpZoom:          return CCTransitionJumpZoom::create(dur, dest);

        // Tiles & Progress
        case TransitionType::FadeTR:            return CCTransitionFadeTR::create(dur, dest);
        case TransitionType::FadeBL:            return CCTransitionFadeBL::create(dur, dest);
        case TransitionType::TurnOffTiles:      return CCTransitionTurnOffTiles::create(dur, dest);
        case TransitionType::SplitCols:         return CCTransitionSplitCols::create(dur, dest);
        case TransitionType::SplitRows:         return CCTransitionSplitRows::create(dur, dest);
        case TransitionType::ProgressRadialCW:  return CCTransitionProgressRadialCW::create(dur, dest);
        case TransitionType::ProgressRadialCCW: return CCTransitionProgressRadialCCW::create(dur, dest);
        case TransitionType::ProgressInOut:     return CCTransitionProgressInOut::create(dur, dest);

        // Pages
        case TransitionType::PageForward:       return CCTransitionPageTurn::create(dur, dest, false);
        case TransitionType::PageBackward:      return CCTransitionPageTurn::create(dur, dest, true);
#endif

        default:                                return CCTransitionFade::create(dur, dest, {0, 0, 0});
    }
}

// ════════════════════════════════════════════════════════════
// Script parser
// ════════════════════════════════════════════════════════════

std::vector<TransitionCommand> TransitionManager::parseScriptFile(std::string const& scriptPath) const {
    std::vector<TransitionCommand> commands;
    auto fullPath = Mod::get()->getSaveDir() / scriptPath;
    std::error_code ec;
    if (!std::filesystem::exists(fullPath, ec)) return commands;

    auto readRes = geode::utils::file::readString(fullPath);
    if (!readRes) return commands;

    std::istringstream stream(readRes.unwrap());
    std::string line;

    while (std::getline(stream, line)) {
        auto first = line.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) continue;
        line.erase(0, first);

        auto last = line.find_last_not_of(" \t\r\n");
        if (last == std::string::npos) continue;
        line.erase(last + 1);
        if (line.empty() || line[0] == '#') continue;

        std::istringstream ls(line);
        std::string verb;
        ls >> verb;
        std::transform(verb.begin(), verb.end(), verb.begin(), ::tolower);

        TransitionCommand cmd;
        bool valid = true;

        if (verb == "fade_out") {
            cmd.action = CommandAction::FadeOut; cmd.target = "from";
            valid = static_cast<bool>(ls >> cmd.duration); cmd.fromVal = 255.f; cmd.toVal = 0.f;
        } else if (verb == "fade_in") {
            cmd.action = CommandAction::FadeIn; cmd.target = "to";
            valid = static_cast<bool>(ls >> cmd.duration); cmd.fromVal = 0.f; cmd.toVal = 255.f;
        } else if (verb == "move") {
            cmd.action = CommandAction::Move;
            valid = static_cast<bool>(ls >> cmd.target >> cmd.fromX >> cmd.fromY >> cmd.toX >> cmd.toY >> cmd.duration);
        } else if (verb == "scale") {
            cmd.action = CommandAction::Scale;
            valid = static_cast<bool>(ls >> cmd.target >> cmd.fromVal >> cmd.toVal >> cmd.duration);
        } else if (verb == "rotate") {
            cmd.action = CommandAction::Rotate;
            valid = static_cast<bool>(ls >> cmd.target >> cmd.fromVal >> cmd.toVal >> cmd.duration);
        } else if (verb == "wait") {
            cmd.action = CommandAction::Wait;
            valid = static_cast<bool>(ls >> cmd.duration);
        } else if (verb == "ease_in") {
            cmd.action = CommandAction::EaseIn;
            valid = static_cast<bool>(ls >> cmd.target >> cmd.fromVal >> cmd.toVal >> cmd.duration);
        } else if (verb == "ease_out") {
            cmd.action = CommandAction::EaseOut;
            valid = static_cast<bool>(ls >> cmd.target >> cmd.fromVal >> cmd.toVal >> cmd.duration);
        } else {
            continue;
        }

        if (!valid) continue;
        TransitionManager::sanitizeCommand(cmd);
        commands.push_back(cmd);
    }
    return commands;
}


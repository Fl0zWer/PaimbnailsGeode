#include "ProfilePicCustomizer.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include <Geode/loader/Mod.hpp>
#include <Geode/utils/file.hpp>
#include <Geode/utils/string.hpp>
#include <fstream>

using namespace geode::prelude;
using namespace cocos2d;

namespace {
using NamedSprite = std::pair<std::string, std::string>;

bool canUseSpriteFrame(std::string const& frameName) {
    return paimon::SpriteHelper::safeCreateWithFrameName(frameName.c_str()) != nullptr;
}

void addDecorationIfAvailable(std::vector<NamedSprite>& out, char const* stem, char const* label) {
    auto frameName = std::string(stem) + ".png";
    if (canUseSpriteFrame(frameName)) {
        out.emplace_back(std::move(frameName), label);
    }
}
}

ProfilePicCustomizer& ProfilePicCustomizer::get() {
    static ProfilePicCustomizer inst;
    if (!inst.m_loaded) {
        inst.load();
        inst.m_loaded = true;
    }
    return inst;
}

ProfilePicCustomizer::ProfilePicCustomizer() {}

ProfilePicConfig ProfilePicCustomizer::getConfig() const {
    return m_config;
}

void ProfilePicCustomizer::setConfig(ProfilePicConfig const& config) {
    m_config = config;
}

void ProfilePicCustomizer::save() {
    auto savePath = Mod::get()->getSaveDir() / "profile_pic_config.json";

    matjson::Value root;
    root["scaleX"] = m_config.scaleX;
    root["scaleY"] = m_config.scaleY;
    root["size"] = m_config.size;
    root["frameEnabled"] = m_config.frameEnabled;
    root["stencilSprite"] = m_config.stencilSprite;
    root["offsetX"] = m_config.offsetX;
    root["offsetY"] = m_config.offsetY;

    // Frame
    matjson::Value frameObj;
    frameObj["spriteFrame"] = m_config.frame.spriteFrame;
    frameObj["colorR"] = static_cast<int>(m_config.frame.color.r);
    frameObj["colorG"] = static_cast<int>(m_config.frame.color.g);
    frameObj["colorB"] = static_cast<int>(m_config.frame.color.b);
    frameObj["opacity"] = m_config.frame.opacity;
    frameObj["thickness"] = m_config.frame.thickness;
    frameObj["offsetX"] = m_config.frame.offsetX;
    frameObj["offsetY"] = m_config.frame.offsetY;
    root["frame"] = frameObj;

    // Decorations
    matjson::Value decoArray = matjson::Value::array();
    for (auto const& deco : m_config.decorations) {
        matjson::Value d;
        d["spriteName"] = deco.spriteName;
        d["posX"] = deco.posX;
        d["posY"] = deco.posY;
        d["scale"] = deco.scale;
        d["rotation"] = deco.rotation;
        d["colorR"] = static_cast<int>(deco.color.r);
        d["colorG"] = static_cast<int>(deco.color.g);
        d["colorB"] = static_cast<int>(deco.color.b);
        d["opacity"] = deco.opacity;
        d["flipX"] = deco.flipX;
        d["flipY"] = deco.flipY;
        d["zOrder"] = deco.zOrder;
        decoArray.push(std::move(d));
    }
    root["decorations"] = decoArray;

    auto res = file::writeStringSafe(savePath, root.dump());
    if (!res) {
        log::error("[ProfilePicCustomizer] Failed to save config: {}", res.unwrapErr());
        return;
    }

    log::info("[ProfilePicCustomizer] Config saved");
}

void ProfilePicCustomizer::load() {
    auto savePath = Mod::get()->getSaveDir() / "profile_pic_config.json";

    std::ifstream in(savePath);
    if (!in) return;

    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();

    auto parsed = matjson::parse(content);
    if (!parsed.isOk()) return;
    auto root = parsed.unwrap();

    if (root.contains("scaleX")) m_config.scaleX = root["scaleX"].asDouble().unwrapOr(1.0);
    if (root.contains("scaleY")) m_config.scaleY = root["scaleY"].asDouble().unwrapOr(1.0);
    if (root.contains("size")) m_config.size = root["size"].asDouble().unwrapOr(120.0);
    if (root.contains("frameEnabled")) m_config.frameEnabled = root["frameEnabled"].asBool().unwrapOr(false);
    if (root.contains("stencilSprite")) m_config.stencilSprite = root["stencilSprite"].asString().unwrapOr("circle");
    if (root.contains("offsetX")) m_config.offsetX = root["offsetX"].asDouble().unwrapOr(0.0);
    if (root.contains("offsetY")) m_config.offsetY = root["offsetY"].asDouble().unwrapOr(0.0);

    // Frame
    if (root.contains("frame")) {
        auto& f = root["frame"];
        if (f.contains("spriteFrame")) m_config.frame.spriteFrame = f["spriteFrame"].asString().unwrapOr("");
        if (f.contains("colorR")) m_config.frame.color.r = f["colorR"].asInt().unwrapOr(255);
        if (f.contains("colorG")) m_config.frame.color.g = f["colorG"].asInt().unwrapOr(255);
        if (f.contains("colorB")) m_config.frame.color.b = f["colorB"].asInt().unwrapOr(255);
        if (f.contains("opacity")) m_config.frame.opacity = f["opacity"].asDouble().unwrapOr(255.0);
        if (f.contains("thickness")) m_config.frame.thickness = f["thickness"].asDouble().unwrapOr(4.0);
        if (f.contains("offsetX")) m_config.frame.offsetX = f["offsetX"].asDouble().unwrapOr(0.0);
        if (f.contains("offsetY")) m_config.frame.offsetY = f["offsetY"].asDouble().unwrapOr(0.0);
    }

    // Decorations
    if (root.contains("decorations") && root["decorations"].isArray()) {
        m_config.decorations.clear();
        auto decoArr = root["decorations"].asArray();
        if (decoArr.isOk()) {
        for (auto& d : decoArr.unwrap()) {
            PicDecoration deco;
            deco.spriteName = d["spriteName"].asString().unwrapOr("");
            deco.posX = d["posX"].asDouble().unwrapOr(0.0);
            deco.posY = d["posY"].asDouble().unwrapOr(0.0);
            deco.scale = d["scale"].asDouble().unwrapOr(1.0);
            deco.rotation = d["rotation"].asDouble().unwrapOr(0.0);
            deco.color.r = d["colorR"].asInt().unwrapOr(255);
            deco.color.g = d["colorG"].asInt().unwrapOr(255);
            deco.color.b = d["colorB"].asInt().unwrapOr(255);
            deco.opacity = d["opacity"].asDouble().unwrapOr(255.0);
            deco.flipX = d["flipX"].asBool().unwrapOr(false);
            deco.flipY = d["flipY"].asBool().unwrapOr(false);
            deco.zOrder = d["zOrder"].asInt().unwrapOr(0);
            if (!deco.spriteName.empty()) {
                m_config.decorations.push_back(deco);
            }
        }
        } // if (decoArr.isOk())
    }

    log::info("[ProfilePicCustomizer] Config loaded ({} decorations)", m_config.decorations.size());
}

std::vector<std::pair<std::string, std::string>> ProfilePicCustomizer::getAvailableFrames() {
    return {
        {"GJ_square01.png", "Square"},
        {"GJ_square02.png", "Square Dark"},
        {"GJ_square03.png", "Square Blue"},
        {"GJ_square04.png", "Square Green"},
        {"GJ_square05.png", "Square Purple"},
        {"GJ_square06.png", "Square Brown"},
        {"GJ_square07.png", "Square Pink"},
        {"square02b_001.png", "Rounded"},
        {"GJ_button_01.png", "Green Button"},
        {"GJ_button_02.png", "Pink Button"},
        {"GJ_button_03.png", "Blue Button"},
        {"GJ_button_04.png", "Gray Button"},
        {"GJ_button_05.png", "Red Button"},
        {"GJ_button_06.png", "Cyan Button"},
    };
}

std::vector<std::pair<std::string, std::string>> ProfilePicCustomizer::getAvailableStencils() {
    return {
        {"circle", "Circle"},
        {"square02b_001.png", "Rounded Square"},
        {"GJ_square01.png", "Square"},
        {"triangle", "Triangle"},
        {"diamond", "Diamond"},
        {"pentagon", "Pentagon"},
        {"hexagon", "Hexagon"},
        {"octagon", "Octagon"},
        {"star", "Star 5"},
        {"star6", "Star 6"},
        {"heart", "Heart"},
    };
}

std::vector<std::pair<std::string, std::string>> ProfilePicCustomizer::getAvailableDecorations() {
    std::vector<std::pair<std::string, std::string>> decorations;
    decorations.reserve(31);

    // Estrellas y gemas
    addDecorationIfAvailable(decorations, "star_small01_001", "Star Small");
    addDecorationIfAvailable(decorations, "star_small02_001", "Star Small 2");
    addDecorationIfAvailable(decorations, "star_small03_001", "Star Small 3");
    addDecorationIfAvailable(decorations, "GJ_bigStar_001", "Big Star");
    addDecorationIfAvailable(decorations, "GJ_sStar_001", "Small Star");
    addDecorationIfAvailable(decorations, "diamond_small01_001", "Diamond");
    addDecorationIfAvailable(decorations, "diamond_small02_001", "Diamond 2");
    addDecorationIfAvailable(decorations, "currencyDiamondIcon_001", "Gem Diamond");
    addDecorationIfAvailable(decorations, "currencyOrbIcon_001", "Orb");

    // Iconos de logros
    addDecorationIfAvailable(decorations, "GJ_sRecentIcon_001", "Recent");
    addDecorationIfAvailable(decorations, "GJ_sTrendingIcon_001", "Trending");
    addDecorationIfAvailable(decorations, "GJ_sMagicIcon_001", "Magic");
    addDecorationIfAvailable(decorations, "GJ_sAwardedIcon_001", "Awarded");
    addDecorationIfAvailable(decorations, "GJ_sFeaturedIcon_001", "Featured");
    addDecorationIfAvailable(decorations, "GJ_sHallOfFameIcon_001", "Hall of Fame");

    // Flechas y UI
    addDecorationIfAvailable(decorations, "GJ_arrow_01_001", "Arrow Right");
    addDecorationIfAvailable(decorations, "GJ_arrow_02_001", "Arrow Left");
    addDecorationIfAvailable(decorations, "GJ_arrow_03_001", "Arrow Up");

    // Badges / mod
    addDecorationIfAvailable(decorations, "modBadge_01_001", "Mod Badge");
    addDecorationIfAvailable(decorations, "modBadge_02_001", "Elder Mod Badge");
    addDecorationIfAvailable(decorations, "modBadge_03_001", "Leaderboard Badge");

    // Particulas y efectos
    addDecorationIfAvailable(decorations, "particle_01_001", "Particle Circle");
    addDecorationIfAvailable(decorations, "particle_02_001", "Particle Square");
    addDecorationIfAvailable(decorations, "particle_03_001", "Particle Triangle");
    addDecorationIfAvailable(decorations, "fireEffect_01_001", "Fire");

    // Iconos de dificultad
    addDecorationIfAvailable(decorations, "diffIcon_01_btn_001", "Easy");
    addDecorationIfAvailable(decorations, "diffIcon_02_btn_001", "Normal");
    addDecorationIfAvailable(decorations, "diffIcon_03_btn_001", "Hard");
    addDecorationIfAvailable(decorations, "diffIcon_04_btn_001", "Harder");
    addDecorationIfAvailable(decorations, "diffIcon_05_btn_001", "Insane");
    addDecorationIfAvailable(decorations, "diffIcon_06_btn_001", "Demon");

    // Locks y checks
    addDecorationIfAvailable(decorations, "GJ_lock_001", "Lock");
    addDecorationIfAvailable(decorations, "GJ_completesIcon_001", "Complete");
    addDecorationIfAvailable(decorations, "GJ_deleteIcon_001", "Delete");

    // Corazones y social
    addDecorationIfAvailable(decorations, "GJ_heart_01", "Heart");
    addDecorationIfAvailable(decorations, "gj_heartOn_001", "Heart On");

    // Misc
    addDecorationIfAvailable(decorations, "GJ_infoIcon_001", "Info");
    addDecorationIfAvailable(decorations, "GJ_playBtn2_001", "Play");
    addDecorationIfAvailable(decorations, "GJ_pauseBtn_001", "Pause");
    addDecorationIfAvailable(decorations, "edit_eRotateBtn_001", "Rotate");
    addDecorationIfAvailable(decorations, "edit_eScaleBtn_001", "Scale");

    return decorations;
}

#include "ProfilePicCustomizer.hpp"
#include <Geode/loader/Mod.hpp>
#include <Geode/utils/file.hpp>
#include <Geode/utils/string.hpp>
#include <fstream>

using namespace geode::prelude;
using namespace cocos2d;

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

    auto jsonStr = root.dump();
    std::ofstream out(savePath);
    if (out) {
        out << jsonStr;
        out.close();
    } else {
        log::error("[ProfilePicCustomizer] Failed to open file for saving");
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
    return {
        // Estrellas y gemas
        {"star_small01_001.png", "Star Small"},
        {"star_small02_001.png", "Star Small 2"},
        {"star_small03_001.png", "Star Small 3"},
        {"GJ_bigStar_001.png", "Big Star"},
        {"GJ_sStar_001.png", "Small Star"},
        {"diamond_small01_001.png", "Diamond"},
        {"diamond_small02_001.png", "Diamond 2"},
        {"currencyDiamondIcon_001.png", "Gem Diamond"},
        {"currencyOrbIcon_001.png", "Orb"},
        
        // Iconos de logros
        {"GJ_sRecentIcon_001.png", "Recent"},
        {"GJ_sTrendingIcon_001.png", "Trending"},
        {"GJ_sMagicIcon_001.png", "Magic"},
        {"GJ_sAwardedIcon_001.png", "Awarded"},
        {"GJ_sFeaturedIcon_001.png", "Featured"},
        {"GJ_sHallOfFameIcon_001.png", "Hall of Fame"},
        
        // Flechas y UI
        {"GJ_arrow_01_001.png", "Arrow Right"},
        {"GJ_arrow_02_001.png", "Arrow Left"},
        {"GJ_arrow_03_001.png", "Arrow Up"},
        
        // Badges / mod
        {"modBadge_01_001.png", "Mod Badge"},
        {"modBadge_02_001.png", "Elder Mod Badge"},
        {"modBadge_03_001.png", "Leaderboard Badge"},
        
        // Particulas y efectos
        {"particle_01_001.png", "Particle Circle"},
        {"particle_02_001.png", "Particle Square"},
        {"particle_03_001.png", "Particle Triangle"},
        {"fireEffect_01_001.png", "Fire"},
        
        // Iconos de dificultad
        {"diffIcon_01_btn_001.png", "Easy"},
        {"diffIcon_02_btn_001.png", "Normal"},
        {"diffIcon_03_btn_001.png", "Hard"},
        {"diffIcon_04_btn_001.png", "Harder"},
        {"diffIcon_05_btn_001.png", "Insane"},
        {"diffIcon_06_btn_001.png", "Demon"},
        
        // Locks y checks
        {"GJ_lock_001.png", "Lock"},
        {"GJ_completesIcon_001.png", "Complete"},
        {"GJ_deleteIcon_001.png", "Delete"},
        
        // Corazones y social
        {"GJ_heart_01.png", "Heart"},
        {"gj_heartOn_001.png", "Heart On"},
        
        // Misc
        {"GJ_infoIcon_001.png", "Info"},
        {"GJ_playBtn2_001.png", "Play"},
        {"GJ_pauseBtn_001.png", "Pause"},
        {"edit_eRotateBtn_001.png", "Rotate"},
        {"edit_eScaleBtn_001.png", "Scale"},
    };
}

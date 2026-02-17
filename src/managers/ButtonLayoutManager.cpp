#include "ButtonLayoutManager.hpp"
#include <Geode/loader/Mod.hpp>
#include <Geode/loader/Log.hpp>
#include <fstream>
#include <sstream>

using namespace geode::prelude;

ButtonLayoutManager& ButtonLayoutManager::get() {
    static ButtonLayoutManager instance;
    return instance;
}

void ButtonLayoutManager::load() {
    auto filePath = geode::Mod::get()->getSaveDir() / "button_layouts.txt";
    
    if (!std::filesystem::exists(filePath)) {
        log::debug("[ButtonLayoutManager] no se encontro archivo diseno; usando por defecto");
        m_layouts.clear();
        return;
    }

    try {
        std::ifstream ifs(filePath);
        if (!ifs.is_open()) {
            log::error("[ButtonLayoutManager] no se pudo abrir archivo diseno");
            return;
        }

        m_layouts.clear();
        std::string line;
        // formato: sceneKey|buttonID|x|y|scale|opacity
        while (std::getline(ifs, line)) {
            if (line.empty() || line[0] == '#') continue; // salto vacios y comentarios

            std::istringstream iss(line);
            std::string sceneKey, buttonID;
            ButtonLayout layout;
            layout.scale = 1.0f;
            layout.opacity = 1.0f;
            
            if (std::getline(iss, sceneKey, '|') &&
                std::getline(iss, buttonID, '|') &&
                iss >> layout.position.x && iss.ignore(1) && 
                iss >> layout.position.y) {
                // opcional: leo escala y opacidad si existe
                if (iss.ignore(1) && iss >> layout.scale) {
                    iss.ignore(1);
                    iss >> layout.opacity;
                }
                m_layouts[sceneKey][buttonID] = layout;
            }
        }
        ifs.close();

        log::info("[ButtonLayoutManager] cargados {} disenos de escena", m_layouts.size());
    } catch (const std::exception& e) {
        log::error("[ButtonLayoutManager] fallo cargar disenos: {}", e.what());
    }

    // cargo tambien defaults persistidos aparte
    loadDefaults();
}

void ButtonLayoutManager::save() {
    auto filePath = geode::Mod::get()->getSaveDir() / "button_layouts.txt";

    try {
        std::ofstream ofs(filePath);
        if (!ofs.is_open()) {
            log::error("[ButtonLayoutManager] no se pudo abrir archivo para escribir");
            return;
        }

        ofs << "# Button layouts (sceneKey|buttonID|x|y|scale|opacity)\n";
        for (auto& [sceneKey, buttons] : m_layouts) {
            for (auto& [buttonID, layout] : buttons) {
                ofs << sceneKey << "|" << buttonID << "|" 
                    << layout.position.x << "|" << layout.position.y << "|"
                    << layout.scale << "|" << layout.opacity << "\n";
            }
        }
        ofs.close();

        log::info("[ButtonLayoutManager] disenos botones guardados");
    } catch (const std::exception& e) {
        log::error("[ButtonLayoutManager] error guardando disenos: {}", e.what());
    }
}

void ButtonLayoutManager::loadDefaults() {
    auto filePath = geode::Mod::get()->getSaveDir() / "button_defaults.txt";
    m_defaults.clear();
    if (!std::filesystem::exists(filePath)) {
        log::debug("[ButtonLayoutManager] no se encontro archivo defaults");
        return;
    }
    try {
        std::ifstream ifs(filePath);
        if (!ifs.is_open()) return;
        std::string line;
        // formato: sceneKey|buttonID|x|y|scale|opacity
        while (std::getline(ifs, line)) {
            if (line.empty() || line[0] == '#') continue;
            std::istringstream iss(line);
            std::string sceneKey, buttonID;
            ButtonLayout layout;
            layout.scale = 1.0f;
            layout.opacity = 1.0f;
            if (std::getline(iss, sceneKey, '|') &&
                std::getline(iss, buttonID, '|') &&
                iss >> layout.position.x && iss.ignore(1) &&
                iss >> layout.position.y) {
                if (iss.ignore(1) && iss >> layout.scale) {
                    iss.ignore(1);
                    iss >> layout.opacity;
                }
                m_defaults[sceneKey][buttonID] = layout;
            }
        }
        ifs.close();
        log::info("[ButtonLayoutManager] cargados {} defaults escena", m_defaults.size());
    } catch (const std::exception& e) {
        log::error("[ButtonLayoutManager] error cargando defaults: {}", e.what());
    }
}

void ButtonLayoutManager::saveDefaults() {
    auto filePath = geode::Mod::get()->getSaveDir() / "button_defaults.txt";
    try {
        std::ofstream ofs(filePath);
        if (!ofs.is_open()) return;
        ofs << "# Button defaults (sceneKey|buttonID|x|y|scale|opacity)\n";
        for (auto& [sceneKey, buttons] : m_defaults) {
            for (auto& [buttonID, layout] : buttons) {
                ofs << sceneKey << "|" << buttonID << "|"
                    << layout.position.x << "|" << layout.position.y << "|"
                    << layout.scale << "|" << layout.opacity << "\n";
            }
        }
        ofs.close();
        log::info("[ButtonLayoutManager] defaults botones guardados");
    } catch (const std::exception& e) {
        log::error("[ButtonLayoutManager] error guardando defaults: {}", e.what());
    }
}

std::optional<ButtonLayout> ButtonLayoutManager::getLayout(const std::string& sceneKey, const std::string& buttonID) const {
    auto sceneIt = m_layouts.find(sceneKey);
    if (sceneIt == m_layouts.end()) return std::nullopt;

    auto buttonIt = sceneIt->second.find(buttonID);
    if (buttonIt == sceneIt->second.end()) return std::nullopt;

    return buttonIt->second;
}

void ButtonLayoutManager::setLayout(const std::string& sceneKey, const std::string& buttonID, const ButtonLayout& layout) {
    m_layouts[sceneKey][buttonID] = layout;
    save();
}

void ButtonLayoutManager::removeLayout(const std::string& sceneKey, const std::string& buttonID) {
    auto sceneIt = m_layouts.find(sceneKey);
    if (sceneIt == m_layouts.end()) return;

    sceneIt->second.erase(buttonID);
    if (sceneIt->second.empty()) {
        m_layouts.erase(sceneKey);
    }
    save();
}

bool ButtonLayoutManager::hasCustomLayout(const std::string& sceneKey, const std::string& buttonID) const {
    auto sceneIt = m_layouts.find(sceneKey);
    if (sceneIt == m_layouts.end()) return false;
    return sceneIt->second.find(buttonID) != sceneIt->second.end();
}

void ButtonLayoutManager::resetScene(const std::string& sceneKey) {
    m_layouts.erase(sceneKey);
    save();
}

std::optional<ButtonLayout> ButtonLayoutManager::getDefaultLayout(const std::string& sceneKey, const std::string& buttonID) const {
    auto sceneIt = m_defaults.find(sceneKey);
    if (sceneIt == m_defaults.end()) return std::nullopt;
    auto it = sceneIt->second.find(buttonID);
    if (it == sceneIt->second.end()) return std::nullopt;
    return it->second;
}

void ButtonLayoutManager::setDefaultLayoutIfAbsent(const std::string& sceneKey, const std::string& buttonID, const ButtonLayout& layout) {
    auto& sceneMap = m_defaults[sceneKey];
    if (sceneMap.find(buttonID) == sceneMap.end()) {
        sceneMap[buttonID] = layout;
        saveDefaults();
    }
}

void ButtonLayoutManager::setDefaultLayout(const std::string& sceneKey, const std::string& buttonID, const ButtonLayout& layout) {
    m_defaults[sceneKey][buttonID] = layout;
    saveDefaults();
}

#include "LocalThumbs.hpp"

#include <Geode/DefaultInclude.hpp>
#include <Geode/utils/General.hpp>
#include <Geode/utils/string.hpp>
#include <cocos2d.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_set>

using namespace geode::prelude;

namespace {
#pragma pack(push, 1)
struct RGBHeader {
    uint32_t width;
    uint32_t height;
};
#pragma pack(pop)
}

LocalThumbs::LocalThumbs() {
    // cache en otro hilo
    std::thread([this]() {
        initCache();
    }).detach();
}

void LocalThumbs::initCache() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_availableLevels.clear();
    
    try {
        auto d = dir();
        if (!std::filesystem::exists(d)) {
            m_cacheInitialized = true;
            return;
        }
        
        // que niveles tienen capturas
        for (const auto& entry : std::filesystem::directory_iterator(d)) {
            if (entry.is_regular_file() && entry.path().extension() == ".rgb") {
                auto stemStr = geode::utils::string::pathToString(entry.path().stem());
                if (auto res = geode::utils::numFromString<int32_t>(stemStr); res.isOk()) {
                    m_availableLevels.insert(res.unwrap());
                }
            }
        }
    } catch(...) {}
    
    m_cacheInitialized = true;
}

LocalThumbs& LocalThumbs::get() {
    static LocalThumbs inst;
    // load mappings
    static bool loaded = false;
    if (!loaded) {
        inst.loadMappings();
        loaded = true;
    }
    return inst;
}

std::string LocalThumbs::dir() const {
    auto save = Mod::get()->getSaveDir();
    std::filesystem::path base(geode::utils::string::pathToString(save));
    auto d = base / "thumbnails";
    std::error_code ec;
    // crear carpeta si no existe
    if (!std::filesystem::exists(d)) {
        std::filesystem::create_directories(d, ec);
        if (ec) {
            log::error("no se pudo crear la carpeta thumbnails: {}", ec.message());
        } else {
            log::debug("carpeta thumbnails lista en: {}", geode::utils::string::pathToString(d));
        }
    }
    return geode::utils::string::pathToString(d);
}

std::optional<std::string> LocalThumbs::getThumbPath(int32_t levelID) const {
    // cache primero
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_cacheInitialized) {
            if (m_availableLevels.find(levelID) == m_availableLevels.end()) {
                return std::nullopt;
            }
            // en cache -> ruta directa
            auto p = std::filesystem::path(dir()) / (std::to_string(levelID) + ".rgb");
            return geode::utils::string::pathToString(p);
        }
    }

    auto p = std::filesystem::path(dir()) / (std::to_string(levelID) + ".rgb");
    if (std::filesystem::exists(p)) return geode::utils::string::pathToString(p);
    return std::nullopt;
}

std::optional<std::string> LocalThumbs::findAnyThumbnail(int32_t levelID) const {
    // 1. buscar rgb primero (mayor prioridad pa capturas locales)
    auto rgbPath = getThumbPath(levelID);
    if (rgbPath) return rgbPath;

    // buscar en thumbnails
    auto thumbDir = std::filesystem::path(dir());
    std::vector<std::string> exts = {".png", ".jpg", ".jpeg", ".webp"};
    for (const auto& ext : exts) {
        auto p = thumbDir / (std::to_string(levelID) + ext);
        if (std::filesystem::exists(p)) return geode::utils::string::pathToString(p);
    }

    // 3. buscar en carpeta cache (descargadas)
    auto cacheDir = Mod::get()->getSaveDir() / "cache";
    for (const auto& ext : exts) {
        auto p = cacheDir / (std::to_string(levelID) + ext);
        if (std::filesystem::exists(p)) return geode::utils::string::pathToString(p);
    }

    return std::nullopt;
}

std::vector<int32_t> LocalThumbs::getAllLevelIDs() const {
    std::vector<int32_t> ids;
    std::unordered_set<int32_t> uniqueIds;

    auto scanDir = [&](const std::filesystem::path& path) {
        try {
            if (!std::filesystem::exists(path)) return;
            for (const auto& entry : std::filesystem::directory_iterator(path)) {
                if (entry.is_regular_file()) {
                    auto ext = entry.path().extension().string();
                    if (ext == ".rgb" || ext == ".png" || ext == ".webp" || ext == ".jpg") {
                        std::string stem = entry.path().stem().string();
                        if (auto res = geode::utils::numFromString<int32_t>(stem)) {
                             uniqueIds.insert(res.unwrap());
                        }
                    }
                }
            }
        } catch (...) {}
    };

    scanDir(dir());
    scanDir(Mod::get()->getSaveDir() / "cache");

    ids.assign(uniqueIds.begin(), uniqueIds.end());
    return ids;
}

CCTexture2D* LocalThumbs::loadTexture(int32_t levelID) const {
    log::debug("cargando miniatura pal nivel: {}", levelID);
    
    // try load desde carpeta
    auto tryLoadFromDir = [&](const std::filesystem::path& baseDir) -> CCTexture2D* {
        // rgb primero (viejo/local)
        auto rgbPath = baseDir / (std::to_string(levelID) + ".rgb");
        if (std::filesystem::exists(rgbPath)) {
            log::debug("cargando desde rgb: {}", geode::utils::string::pathToString(rgbPath));
            std::ifstream in(rgbPath, std::ios::binary);
            if (in) {
                RGBHeader head{};
                in.read(reinterpret_cast<char*>(&head), sizeof(head));
                if (in && head.width > 0 && head.height > 0) {
                    const size_t size = static_cast<size_t>(head.width) * head.height * 3;
                    auto buf = std::make_unique<uint8_t[]>(size);
                    in.read(reinterpret_cast<char*>(buf.get()), size);
                    if (in) {
                        // rgb->rgba pa cocos
                        size_t pixelCount = static_cast<size_t>(head.width) * head.height;
                        auto rgbaBuf = std::make_unique<uint8_t[]>(pixelCount * 4);
                        for (size_t i = 0; i < pixelCount; ++i) {
                            rgbaBuf[i * 4 + 0] = buf[i * 3 + 0]; // R
                            rgbaBuf[i * 4 + 1] = buf[i * 3 + 1]; // G
                            rgbaBuf[i * 4 + 2] = buf[i * 3 + 2]; // B
                            rgbaBuf[i * 4 + 3] = 255;            // A
                        }

                        auto tex = new CCTexture2D();
                        if (tex->initWithData(rgbaBuf.get(), kCCTexture2DPixelFormat_RGBA8888, head.width, head.height, CCSize(head.width, head.height))) {
                            ccTexParams params{GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE};
                            tex->setTexParameters(&params);
                            tex->autorelease();
                            return tex;
                        }
                        tex->release();
                    }
                }
            }
        }

        // formatos std
        std::vector<std::string> extensions = {".png", ".webp", ".jpg"};
        for (const auto& ext : extensions) {
            auto p = baseDir / (std::to_string(levelID) + ext);
            if (std::filesystem::exists(p)) {
                std::string pathStr = geode::utils::string::pathToString(p);
                log::debug("cargando imagen: {}", pathStr);
                auto tex = CCTextureCache::sharedTextureCache()->addImage(pathStr.c_str(), false);
                if (tex) {
                    return tex;
                }
            }
        }
        return nullptr;
    };

    // buscar en carpeta local primero
    if (auto tex = tryLoadFromDir(dir())) return tex;

    // buscar en carpeta cache
    if (auto tex = tryLoadFromDir(Mod::get()->getSaveDir() / "cache")) return tex;
    
    log::debug("no se halló miniatura pal nivel: {}", levelID);
    return nullptr;
}

bool LocalThumbs::saveRGB(int32_t levelID, const uint8_t* data, uint32_t width, uint32_t height) {
    log::info("guardando miniatura nivel: {} ({}x{})", levelID, width, height);
    
    if (!data) {
        log::error("no se puede guardar: data es null");
        return false;
    }
    
    if (width == 0 || height == 0) {
        log::error("dimensiones invalidas pa guardar ({}x{})", width, height);
        return false;
    }
    
    auto p = std::filesystem::path(dir()) / (std::to_string(levelID) + ".rgb");
    log::debug("escribiendo en: {}", geode::utils::string::pathToString(p));
    
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    if (!out) {
        log::error("error abriendo archivo pa escribir: {}", geode::utils::string::pathToString(p));
        return false;
    }
    
    RGBHeader head{ width, height };
    out.write(reinterpret_cast<const char*>(&head), sizeof(head));
    
    const size_t size = static_cast<size_t>(width) * height * 3;
    log::debug("escribiendo {} bytes", size);
    out.write(reinterpret_cast<const char*>(data), size);
    
    bool success = static_cast<bool>(out);
    if (success) {
        log::info("miniatura guardada OK pal nivel: {}", levelID);
        
        std::lock_guard<std::mutex> lock(m_mutex);
        m_availableLevels.insert(levelID);
    } else {
        log::error("falló la escritura de datos");
    }
    
    return success;
}

bool LocalThumbs::saveFromRGBA(int32_t levelID, const uint8_t* data, uint32_t width, uint32_t height) {
    if (!data || width == 0 || height == 0) return false;
    
    // rgba -> rgb
    size_t pixelCount = static_cast<size_t>(width) * height;
    std::vector<uint8_t> rgbData(pixelCount * 3);
    
    for (size_t i = 0; i < pixelCount; ++i) {
        rgbData[i * 3 + 0] = data[i * 4 + 0]; // R
        rgbData[i * 3 + 1] = data[i * 4 + 1]; // G
        rgbData[i * 3 + 2] = data[i * 4 + 2]; // B
        // ignoramos alpha
    }
    
    return saveRGB(levelID, rgbData.data(), width, height);
}

// mapping levelID -> fileName

std::string LocalThumbs::mappingFile() const {
    return geode::utils::string::pathToString(std::filesystem::path(dir()) / "filename_mapping.txt");
}

void LocalThumbs::storeFileMapping(int32_t levelID, const std::string& fileName) {
    m_fileMapping[levelID] = fileName;
    saveMappings();
    log::info("mapping guardado: {} -> {}", levelID, fileName);
}

std::optional<std::string> LocalThumbs::getFileName(int32_t levelID) const {
    auto it = m_fileMapping.find(levelID);
    if (it != m_fileMapping.end()) {
        return it->second;
    }
    // fallback default si no mapping
    return std::nullopt;
}

void LocalThumbs::loadMappings() {
    m_fileMapping.clear();
    std::ifstream in(mappingFile());
    if (!in) {
        log::debug("no se halló archivo de mapping, empezamos de cero");
        return;
    }
    
    std::string line;
    int count = 0;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        
        // "levelID fileName"
        std::istringstream iss(line);
        int32_t levelID;
        std::string fileName;
        if (iss >> levelID >> fileName) {
            m_fileMapping[levelID] = fileName;
            count++;
        }
    }
    log::info("se cargaron {} mappings", count);
}

void LocalThumbs::saveMappings() {
    std::ofstream out(mappingFile(), std::ios::trunc);
    if (!out) {
        log::error("error guardando mappings en {}", mappingFile());
        return;
    }
    
    for (const auto& [levelID, fileName] : m_fileMapping) {
        out << levelID << " " << fileName << "\n";
    }
    log::debug("se guardaron {} mappings", m_fileMapping.size());
}


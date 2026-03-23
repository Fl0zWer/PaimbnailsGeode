#include "LocalThumbs.hpp"

#include <Geode/DefaultInclude.hpp>
#include <Geode/utils/general.hpp>
#include <Geode/utils/string.hpp>
#include <cocos2d.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_set>
#include <future>
#include "../../../core/QualityConfig.hpp"

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
    // hilo de I/O de disco para escanear cache — no migrable a WebTask
    m_initFuture = std::async(std::launch::async, [this]() {
        initCache();
    });
}

void LocalThumbs::initCache() {
    log::info("[LocalThumbs] initCache: scanning local thumbnails");
    std::lock_guard<std::mutex> lock(m_mutex);
    m_availableLevels.clear();
    
    auto d = dir();
    std::error_code ec;
    if (!std::filesystem::exists(d, ec)) {
        m_cacheInitialized.store(true, std::memory_order_release);
        return;
    }

    // que niveles tienen capturas
    for (auto const& entry : std::filesystem::directory_iterator(d, ec)) {
        if (m_shuttingDown.load(std::memory_order_relaxed)) {
            break;
        }
        if (ec) break;
        if (entry.is_regular_file() && entry.path().extension() == ".rgb") {
            auto stemStr = geode::utils::string::pathToString(entry.path().stem());
            if (auto res = geode::utils::numFromString<int32_t>(stemStr); res.isOk()) {
                m_availableLevels.insert(res.unwrap());
            }
        }
    }

    log::info("[LocalThumbs] initCache: found {} levels", m_availableLevels.size());
    m_cacheInitialized.store(true, std::memory_order_release);
}

LocalThumbs& LocalThumbs::get() {
    static LocalThumbs inst;
    static std::once_flag loadFlag;
    std::call_once(loadFlag, [&]() {
        inst.loadMappings();
    });
    return inst;
}

std::string LocalThumbs::dir() const {
    auto save = Mod::get()->getSaveDir();
    std::filesystem::path base(geode::utils::string::pathToString(save));
    auto d = base / "thumbnails";
    std::error_code ec;
    // crear carpeta si no existe
    std::error_code ecDir;
    if (!std::filesystem::exists(d, ecDir)) {
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
        if (m_cacheInitialized.load(std::memory_order_acquire)) {
            if (m_availableLevels.find(levelID) == m_availableLevels.end()) {
                return std::nullopt;
            }
            // en cache -> ruta directa
            auto p = std::filesystem::path(dir()) / (std::to_string(levelID) + ".rgb");
            return geode::utils::string::pathToString(p);
        }
    }

    auto p = std::filesystem::path(dir()) / (std::to_string(levelID) + ".rgb");
    std::error_code ecRgb;
    if (std::filesystem::exists(p, ecRgb)) return geode::utils::string::pathToString(p);
    return std::nullopt;
}

std::optional<std::string> LocalThumbs::findAnyThumbnail(int32_t levelID) const {
    // 1. buscar rgb primero (mayor prioridad pa capturas locales)
    auto rgbPath = getThumbPath(levelID);
    if (rgbPath) return rgbPath;

    // buscar en thumbnails
    auto thumbDir = std::filesystem::path(dir());
    std::vector<std::string> exts = {".png", ".jpg", ".jpeg", ".webp", ".gif"};
    std::error_code ecFind;
    for (auto const& ext : exts) {
        auto p = thumbDir / (std::to_string(levelID) + ext);
        if (std::filesystem::exists(p, ecFind)) return geode::utils::string::pathToString(p);
    }

    // 3. buscar en carpeta cache (descargadas)
    auto qualityCacheDir = paimon::quality::cacheDir();
    for (auto const& ext : exts) {
        auto p = qualityCacheDir / (std::to_string(levelID) + ext);
        if (std::filesystem::exists(p, ecFind)) return geode::utils::string::pathToString(p);
    }

    return std::nullopt;
}

std::vector<int32_t> LocalThumbs::getAllLevelIDs() const {
    std::vector<int32_t> ids;
    std::unordered_set<int32_t> uniqueIds;

    auto scanDir = [&](std::filesystem::path const& path) {
        std::error_code ec;
        if (!std::filesystem::exists(path, ec)) return;
        for (auto const& entry : std::filesystem::directory_iterator(path, ec)) {
            if (ec) break;
            if (entry.is_regular_file()) {
                auto ext = geode::utils::string::pathToString(entry.path().extension());
                if (ext == ".rgb" || ext == ".png" || ext == ".webp" || ext == ".jpg") {
                    std::string stem = geode::utils::string::pathToString(entry.path().stem());
                    if (auto res = geode::utils::numFromString<int32_t>(stem); res.isOk()) {
                         uniqueIds.insert(res.unwrap());
                    }
                }
            }
        }
    };

    scanDir(dir());
    scanDir(paimon::quality::cacheDir());

    ids.assign(uniqueIds.begin(), uniqueIds.end());
    return ids;
}

CCTexture2D* LocalThumbs::loadTexture(int32_t levelID) const {
    log::info("[LocalThumbs] loadTexture: levelID={}", levelID);
    
    // try load desde carpeta
    auto tryLoadFromDir = [&](std::filesystem::path const& baseDir) -> CCTexture2D* {
        // rgb primero (viejo/local)
        auto rgbPath = baseDir / (std::to_string(levelID) + ".rgb");
        std::error_code fsEc;
        if (std::filesystem::exists(rgbPath, fsEc) && !fsEc) {
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
        for (auto const& ext : extensions) {
            auto p = baseDir / (std::to_string(levelID) + ext);
            std::error_code extEc;
            if (std::filesystem::exists(p, extEc) && !extEc) {
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
    if (auto tex = tryLoadFromDir(paimon::quality::cacheDir())) return tex;
    
    log::debug("[LocalThumbs] loadTexture: not found levelID={}", levelID);
    return nullptr;
}

bool LocalThumbs::saveRGB(int32_t levelID, const uint8_t* data, uint32_t width, uint32_t height) {
    log::info("[LocalThumbs] saveRGB: levelID={} {}x{}", levelID, width, height);
    
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
    out.write(reinterpret_cast<char const*>(&head), sizeof(head));
    
    const size_t size = static_cast<size_t>(width) * height * 3;
    log::debug("escribiendo {} bytes", size);
    out.write(reinterpret_cast<char const*>(data), size);
    
    bool success = static_cast<bool>(out);
    if (success) {
        log::info("miniatura guardada OK pal nivel: {}", levelID);
        
        std::lock_guard<std::mutex> lock(m_mutex);
        m_availableLevels.insert(levelID);
    } else {
        log::error("fallo la escritura de datos");
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

void LocalThumbs::storeFileMapping(int32_t levelID, std::string const& fileName) {
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
        log::debug("no se hallo archivo de mapping, empezamos de cero");
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
    
    for (auto const& [levelID, fileName] : m_fileMapping) {
        out << levelID << " " << fileName << "\n";
    }
    log::debug("se guardaron {} mappings", m_fileMapping.size());
}

void LocalThumbs::shutdown() {
    log::info("[LocalThumbs] shutdown");
    m_shuttingDown.store(true, std::memory_order_release);
    if (m_initFuture.valid()) {
        m_initFuture.wait();
    }
}


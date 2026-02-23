#pragma once

#include <Geode/DefaultInclude.hpp>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

class LocalThumbs {
public:
    static LocalThumbs& get();

    // ruta local thumb si existe
    std::optional<std::string> getThumbPath(int32_t levelID) const;

    // ruta a thumb valida (rgb/png/jpg/webp)
    std::optional<std::string> findAnyThumbnail(int32_t levelID) const;

    bool has(int32_t levelID) const { return getThumbPath(levelID).has_value(); }

    // load textura levelID; nullptr si no
    cocos2d::CCTexture2D* loadTexture(int32_t levelID) const;

    // todos los levelIDs con thumb local
    std::vector<int32_t> getAllLevelIDs() const;

    // guardar rgb24 + size
    bool saveRGB(int32_t levelID, const uint8_t* data, uint32_t width, uint32_t height);
    
    // guardar rgba32 (-> rgb24 interno)
    bool saveFromRGBA(int32_t levelID, const uint8_t* data, uint32_t width, uint32_t height);

    // Mapping system: levelID -> fileName (para nueva API)
    void storeFileMapping(int32_t levelID, const std::string& fileName);
    std::optional<std::string> getFileName(int32_t levelID) const;
    void loadMappings();
    void saveMappings();

private:
    LocalThumbs(); // privado
    std::string dir() const;
    std::string mappingFile() const;
    std::unordered_map<int32_t, std::string> m_fileMapping;
    
    // cache busqueda
    std::unordered_set<int32_t> m_availableLevels;
    mutable std::mutex m_mutex;
    bool m_cacheInitialized = false;
    void initCache();
};


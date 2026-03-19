#pragma once

#include <vector>
#include <cstdint>
#include <string>
#include <filesystem>
#include <cocos2d.h>

class ImageConverter {
public:
    static std::vector<uint8_t> rgbToRgba(std::vector<uint8_t> const& rgbData, uint32_t width, uint32_t height);
    
    static bool rgbToPng(std::vector<uint8_t> const& rgbData, uint32_t width, uint32_t height, std::vector<uint8_t>& outPngData);
    
    static bool rgbaToPngBuffer(const uint8_t* rgba, uint32_t width, uint32_t height, std::vector<uint8_t>& outPngData);

    static bool saveRGBAToPNG(const uint8_t* rgba, uint32_t width, uint32_t height, std::filesystem::path const& filePath);

    static bool loadRgbFileToPng(std::string const& rgbFilePath, std::vector<uint8_t>& outPngData);
    static bool loadRgbFile(std::string const& rgbFilePath, std::vector<uint8_t>& outRgbData, uint32_t& outWidth, uint32_t& outHeight);

private:
    struct RGBHeader {
        uint32_t width;
        uint32_t height;
    };
};


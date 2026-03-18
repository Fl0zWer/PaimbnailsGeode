#pragma once

#include <vector>
#include <cstdint>
#include <string>
#include <filesystem>
#include <cocos2d.h>

/**
 * Utility class for image format conversions.
 * Centralizes all RGB/RGBA/PNG conversion logic to avoid duplication.
 * All file I/O uses std::filesystem::path for proper Unicode support on Windows.
 */
class ImageConverter {
public:
    static std::vector<uint8_t> rgbToRgba(std::vector<uint8_t> const& rgbData, uint32_t width, uint32_t height);
    
    /**
     * Convert RGB/RGBA data to PNG bytes in memory (no file I/O).
     */
    static bool rgbToPng(std::vector<uint8_t> const& rgbData, uint32_t width, uint32_t height, std::vector<uint8_t>& outPngData);
    
    /**
     * Encode RGBA8888 buffer to PNG bytes in memory.
     */
    static bool rgbaToPngBuffer(const uint8_t* rgba, uint32_t width, uint32_t height, std::vector<uint8_t>& outPngData);

    /**
     * Encode RGBA8888 buffer to PNG and write to file (Unicode-safe on Windows).
     */
    static bool saveRGBAToPNG(const uint8_t* rgba, uint32_t width, uint32_t height, std::filesystem::path const& filePath);

    static bool loadRgbFileToPng(std::string const& rgbFilePath, std::vector<uint8_t>& outPngData);
    static bool loadRgbFile(std::string const& rgbFilePath, std::vector<uint8_t>& outRgbData, uint32_t& outWidth, uint32_t& outHeight);

private:
    struct RGBHeader {
        uint32_t width;
        uint32_t height;
    };
};


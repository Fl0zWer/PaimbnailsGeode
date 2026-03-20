#include "ImageConverter.hpp"
#include <Geode/Geode.hpp>
#include <fstream>
#include <filesystem>

// stbi_write_png_to_func codifica PNG en memoria y evita el bug de rutas UTF-8 en Windows.
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBIW_WINDOWS_UTF8
#include "stb_image_write.h"

using namespace geode::prelude;
using namespace cocos2d;

namespace {
    void stbiWriteToVector(void* context, void* data, int size) {
        auto* vec = static_cast<std::vector<uint8_t>*>(context);
        auto* bytes = static_cast<uint8_t*>(data);
        vec->insert(vec->end(), bytes, bytes + size);
    }
}

std::vector<uint8_t> ImageConverter::rgbToRgba(std::vector<uint8_t> const& rgbData, uint32_t width, uint32_t height) {
    size_t pixelCount = static_cast<size_t>(width) * height;
    std::vector<uint8_t> rgba(pixelCount * 4);
    
    for (size_t i = 0; i < pixelCount; ++i) {
        rgba[i * 4 + 0] = rgbData[i * 3 + 0];
        rgba[i * 4 + 1] = rgbData[i * 3 + 1];
        rgba[i * 4 + 2] = rgbData[i * 3 + 2];
        rgba[i * 4 + 3] = 255; // alpha opaco
    }
    
    return rgba;
}

bool ImageConverter::rgbaToPngBuffer(const uint8_t* rgba, uint32_t width, uint32_t height, std::vector<uint8_t>& outPngData) {
    if (!rgba || width == 0 || height == 0) return false;
    outPngData.clear();
    outPngData.reserve(static_cast<size_t>(width) * height); // reserva heuristica
    int ok = stbi_write_png_to_func(stbiWriteToVector, &outPngData,
        static_cast<int>(width), static_cast<int>(height), 4, rgba,
        static_cast<int>(width) * 4);
    return ok != 0;
}

bool ImageConverter::saveRGBAToPNG(const uint8_t* rgba, uint32_t width, uint32_t height, std::filesystem::path const& filePath) {
    std::vector<uint8_t> pngData;
    if (!rgbaToPngBuffer(rgba, width, height, pngData)) {
        log::error("[ImageConverter] stbi PNG encode failed");
        return false;
    }
    // std::ofstream con filesystem::path usa _wfopen en Windows -> Unicode OK
    std::ofstream out(filePath, std::ios::binary | std::ios::trunc);
    if (!out) {
        log::error("[ImageConverter] Cannot open file for writing: {}", geode::utils::string::pathToString(filePath));
        return false;
    }
    out.write(reinterpret_cast<char const*>(pngData.data()), pngData.size());
    return static_cast<bool>(out);
}

bool ImageConverter::rgbToPng(std::vector<uint8_t> const& rgbData, uint32_t width, uint32_t height, std::vector<uint8_t>& outPngData) {
    bool isRgba = (rgbData.size() == static_cast<size_t>(width) * height * 4);
    
    if (isRgba) {
        return rgbaToPngBuffer(rgbData.data(), width, height, outPngData);
    }

    auto rgba = rgbToRgba(rgbData, width, height);
    return rgbaToPngBuffer(rgba.data(), width, height, outPngData);
}

bool ImageConverter::loadRgbFileToPng(std::string const& rgbFilePath, std::vector<uint8_t>& outPngData) {
    std::vector<uint8_t> rgbData;
    uint32_t width, height;
    
    if (!loadRgbFile(rgbFilePath, rgbData, width, height)) {
        return false;
    }
    
    return rgbToPng(rgbData, width, height, outPngData);
}

bool ImageConverter::loadRgbFile(std::string const& rgbFilePath, std::vector<uint8_t>& outRgbData, uint32_t& outWidth, uint32_t& outHeight) {
    std::ifstream in(rgbFilePath, std::ios::binary);
    if (!in) {
        log::error("[ImageConverter] Failed to open RGB file: {}", rgbFilePath);
        return false;
    }
    
    // lee cabecera
    RGBHeader header{};
    in.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (!in || header.width == 0 || header.height == 0) {
        log::error("[ImageConverter] Invalid RGB header in file: {}", rgbFilePath);
        return false;
    }
    
    // lee datos rgb
    size_t rgbSize = static_cast<size_t>(header.width) * header.height * 3;
    outRgbData.resize(rgbSize);
    in.read(reinterpret_cast<char*>(outRgbData.data()), rgbSize);
    
    if (!in) {
        log::error("[ImageConverter] Failed to read RGB data from file: {}", rgbFilePath);
        return false;
    }
    
    outWidth = header.width;
    outHeight = header.height;
    
    return true;
}

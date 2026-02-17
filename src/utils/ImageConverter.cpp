#include "ImageConverter.hpp"
#include <Geode/Geode.hpp>
#include <fstream>
#include <filesystem>

using namespace geode::prelude;
using namespace cocos2d;

std::vector<uint8_t> ImageConverter::rgbToRgba(const std::vector<uint8_t>& rgbData, uint32_t width, uint32_t height) {
    size_t pixelCount = static_cast<size_t>(width) * height;
    std::vector<uint8_t> rgba(pixelCount * 4);
    
    for (size_t i = 0; i < pixelCount; ++i) {
        rgba[i * 4 + 0] = rgbData[i * 3 + 0]; // r
        rgba[i * 4 + 1] = rgbData[i * 3 + 1]; // g
        rgba[i * 4 + 2] = rgbData[i * 3 + 2]; // b
        rgba[i * 4 + 3] = 255;                // a (completamente opaco)
    }
    
    return rgba;
}

bool ImageConverter::rgbToPng(const std::vector<uint8_t>& rgbData, uint32_t width, uint32_t height, std::vector<uint8_t>& outPngData) {
    // rgb (3) o rgba (4)
    // asume rgba si 4bpp
    bool isRgba = (rgbData.size() == static_cast<size_t>(width) * height * 4);
    
    std::vector<uint8_t> rgba;
    if (isRgba) {
        rgba = rgbData;
    } else {
        // rgb -> rgba
        rgba = rgbToRgba(rgbData, width, height);
    }
    
    // ccimage desde rgba
    CCImage img;
    if (!img.initWithImageData(rgba.data(), rgba.size(), CCImage::kFmtRawData, width, height)) {
        log::error("[ImageConverter] Failed to init CCImage with raw data");
        return false;
    }
    
    // guarda temp local (android ok)
    auto tmpDir = Mod::get()->getSaveDir() / "tmp";
    std::error_code dirEc;
    std::filesystem::create_directories(tmpDir, dirEc);
    auto tempPath = tmpDir / fmt::format("temp_img_{}.png", (uintptr_t)&img);
    if (!img.saveToFile(geode::utils::string::pathToString(tempPath).c_str(), false)) {
        log::error("[ImageConverter] Failed to save image to temp file");
        return false;
    }
    
    // lee bytes png desde archivo temporal
    std::ifstream pngFile(tempPath, std::ios::binary);
    if (!pngFile) {
        log::error("[ImageConverter] Failed to open temp PNG file");
        std::filesystem::remove(tempPath);
        return false;
    }
    
    pngFile.seekg(0, std::ios::end);
    size_t pngSize = static_cast<size_t>(pngFile.tellg());
    pngFile.seekg(0, std::ios::beg);
    
    outPngData.resize(pngSize);
    pngFile.read(reinterpret_cast<char*>(outPngData.data()), pngSize);
    pngFile.close();

    // limpia archivo temporal
    std::error_code ec;
    std::filesystem::remove(tempPath, ec);
    if (ec) log::warn("[ImageConverter] Failed to remove temp file {}: {}", geode::utils::string::pathToString(tempPath), ec.message());
    
    return true;
}

bool ImageConverter::loadRgbFileToPng(const std::string& rgbFilePath, std::vector<uint8_t>& outPngData) {
    std::vector<uint8_t> rgbData;
    uint32_t width, height;
    
    if (!loadRgbFile(rgbFilePath, rgbData, width, height)) {
        return false;
    }
    
    return rgbToPng(rgbData, width, height, outPngData);
}

bool ImageConverter::loadRgbFile(const std::string& rgbFilePath, std::vector<uint8_t>& outRgbData, uint32_t& outWidth, uint32_t& outHeight) {
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

#pragma once

#include <Geode/Geode.hpp>
#include <filesystem>
#include <fstream>
#include <vector>
#include <memory>
#include <algorithm>

// stb_image: solo declaraciones (la implementacion esta en ThumbnailLoader.cpp)
#include "stb_image.h"

// Targeted type imports to avoid namespace pollution in headers
using cocos2d::CCTexture2D;
using cocos2d::CCSize;
using cocos2d::CCImage;
using cocos2d::ccTexParams;
using cocos2d::kCCTexture2DPixelFormat_RGBA8888;

namespace ImageLoadHelper {

    struct LoadedImage {
        CCTexture2D* texture = nullptr;     // textura cocos (retained, caller debe release)
        std::shared_ptr<uint8_t> buffer;    // buffer RGBA
        int width = 0;
        int height = 0;
        bool success = false;
        std::string error;
    };

    inline LoadedImage createFromRGBA(uint8_t const* rgba, int w, int h) {
        LoadedImage result;

        size_t rgbaSize = static_cast<size_t>(w) * static_cast<size_t>(h) * 4;

        auto* tex = new CCTexture2D();
        if (!tex->initWithData(rgba, kCCTexture2DPixelFormat_RGBA8888, w, h, CCSize(w, h))) {
            tex->release();
            result.error = "texture_error";
            return result;
        }
        ccTexParams params{GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE};
        tex->setTexParameters(&params);

        auto buffer = std::shared_ptr<uint8_t>(new uint8_t[rgbaSize], std::default_delete<uint8_t[]>());
        memcpy(buffer.get(), rgba, rgbaSize);

        result.texture = tex;
        result.buffer = buffer;
        result.width = w;
        result.height = h;
        result.success = true;
        return result;
    }

    inline LoadedImage loadWithSTBFromMemory(uint8_t const* fileData, size_t fileSize) {
        LoadedImage result;

        int w = 0, h = 0, channels = 0;
        unsigned char* data = stbi_load_from_memory(fileData, static_cast<int>(fileSize), &w, &h, &channels, 4);
        if (!data) {
            result.error = "image_open_error";
            return result;
        }

        if (w <= 0 || h <= 0 || w > 16384 || h > 16384) {
            stbi_image_free(data);
            result.error = "invalid_image_data";
            return result;
        }

        result = createFromRGBA(data, w, h);
        stbi_image_free(data);

        if (!result.success) {
            result.error = "texture_error";
        } else {
            geode::log::info("[ImageLoadHelper] Loaded via stb_image (memory): {}x{} (original {} channels)", w, h, channels);
        }
        return result;
    }

    inline LoadedImage loadWithSTB(std::filesystem::path const& path) {
        LoadedImage result;

        int w = 0, h = 0, channels = 0;
        // Forzar 4 canales (RGBA) para consistencia
        unsigned char* data = stbi_load(geode::utils::string::pathToString(path).c_str(), &w, &h, &channels, 4);
        if (!data) {
            result.error = "image_open_error";
            return result;
        }

        if (w <= 0 || h <= 0 || w > 16384 || h > 16384) {
            stbi_image_free(data);
            result.error = "invalid_image_data";
            return result;
        }

        result = createFromRGBA(data, w, h);
        stbi_image_free(data);

        if (!result.success) {
            result.error = "texture_error";
        } else {
            geode::log::info("[ImageLoadHelper] Loaded via stb_image: {}x{} (original {} channels)", w, h, channels);
        }
        return result;
    }

    inline LoadedImage loadStaticImage(std::filesystem::path const& path, size_t maxSizeMB = 10) {
        LoadedImage result;

        // verificar tamano
        if (maxSizeMB > 0) {
            std::error_code ec;
            auto fileSize = std::filesystem::file_size(path, ec);
            if (!ec && fileSize > maxSizeMB * 1024 * 1024) {
                result.error = fmt::format("Image too large (max {}MB)", maxSizeMB);
                return result;
            }
        }

        // leer archivo una sola vez a memoria
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            result.error = "image_open_error";
            return result;
        }
        std::vector<uint8_t> fileData((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        file.close();

        if (fileData.empty()) {
            result.error = "image_open_error";
            return result;
        }

        // === Intento 1: CCImage::initWithImageData (PNG, JPEG estandar) ===
        {
            CCImage img;
            if (img.initWithImageData(const_cast<uint8_t*>(fileData.data()), fileData.size())) {
                int w = img.getWidth();
                int h = img.getHeight();
                auto raw = img.getData();
                if (raw && w > 0 && h > 0) {
                    int bpp = img.hasAlpha() ? 4 : 3;

                    size_t rgbaSize = static_cast<size_t>(w) * static_cast<size_t>(h) * 4;
                    std::vector<uint8_t> rgba(rgbaSize);
                    unsigned char const* src = reinterpret_cast<unsigned char const*>(raw);

                    if (bpp == 4) {
                        memcpy(rgba.data(), src, rgbaSize);
                    } else {
                        for (int i = 0; i < w * h; i++) {
                            rgba[i * 4 + 0] = src[i * 3 + 0];
                            rgba[i * 4 + 1] = src[i * 3 + 1];
                            rgba[i * 4 + 2] = src[i * 3 + 2];
                            rgba[i * 4 + 3] = 255;
                        }
                    }

                    return createFromRGBA(rgba.data(), w, h);
                }
            }
        }

        // === Intento 2: stb_image desde memoria (BMP, TGA, PSD, JPEG especiales, etc) ===
        {
            auto stbResult = loadWithSTBFromMemory(fileData.data(), fileData.size());
            if (stbResult.success) return stbResult;
        }

        result.error = "image_open_error";
        return result;
    }

    inline std::vector<uint8_t> readBinaryFile(std::filesystem::path const& path, size_t maxSizeMB = 10) {
        std::ifstream file(path, std::ios::binary);
        if (!file) return {};

        std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

        if (maxSizeMB > 0 && data.size() > maxSizeMB * 1024 * 1024) {
            return {};
        }

        return data;
    }

    inline bool isGIF(std::filesystem::path const& path) {
        std::string ext = geode::utils::string::pathToString(path.extension());
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        return ext == ".gif";
    }
}


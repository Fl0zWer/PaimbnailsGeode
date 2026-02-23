#pragma once

#include <Geode/Geode.hpp>
#include <filesystem>
#include <fstream>
#include <vector>
#include <memory>
#include <algorithm>

using namespace geode::prelude;

/**
 * helper pa cargar imagen desde disco y prepararla pa CapturePreviewPopup.
 * elimina codigo duplicado entre processProfileImage y processProfileImg.
 */
namespace ImageLoadHelper {

    struct LoadedImage {
        CCTexture2D* texture = nullptr;     // textura cocos (retained, caller debe release)
        std::shared_ptr<uint8_t> buffer;    // buffer RGBA
        int width = 0;
        int height = 0;
        bool success = false;
        std::string error;
    };

    /**
     * carga imagen estática (png/jpg/etc) desde path.
     * devuelve textura + buffer RGBA listo pa CapturePreviewPopup.
     *
     * @param path ruta al archivo
     * @param maxSizeMB tamaño maximo en MB (0 = sin limite)
     * @return LoadedImage con los datos o error
     */
    inline LoadedImage loadStaticImage(const std::filesystem::path& path, size_t maxSizeMB = 10) {
        LoadedImage result;

        // verificar tamaño
        if (maxSizeMB > 0) {
            try {
                if (std::filesystem::file_size(path) > maxSizeMB * 1024 * 1024) {
                    result.error = fmt::format("Image too large (max {}MB)", maxSizeMB);
                    return result;
                }
            } catch (...) {}
        }

        // cargar imagen
        CCImage img;
        if (!img.initWithImageFile(path.generic_string().c_str())) {
            result.error = "image_open_error";
            return result;
        }

        int w = img.getWidth();
        int h = img.getHeight();
        auto raw = img.getData();
        if (!raw) {
            result.error = "invalid_image_data";
            return result;
        }

        // detectar bpp
        int bpp = 4;
        try { bpp = img.hasAlpha() ? 4 : 3; } catch (...) { bpp = 4; }

        // convertir a RGBA
        size_t rgbaSize = static_cast<size_t>(w) * static_cast<size_t>(h) * 4;
        std::vector<uint8_t> rgba(rgbaSize);
        const unsigned char* src = reinterpret_cast<const unsigned char*>(raw);

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

        // crear textura
        auto* tex = new CCTexture2D();
        if (!tex->initWithImage(&img)) {
            tex->release();
            result.error = "texture_error";
            return result;
        }
        // retain pa que sobreviva, caller debe release cuando termine
        // no autorelease pa que no muera antes del popup

        // copiar buffer a shared_ptr
        auto buffer = std::shared_ptr<uint8_t>(new uint8_t[rgbaSize], std::default_delete<uint8_t[]>());
        memcpy(buffer.get(), rgba.data(), rgbaSize);

        result.texture = tex;
        result.buffer = buffer;
        result.width = w;
        result.height = h;
        result.success = true;
        return result;
    }

    /**
     * lee archivo en binario (gif, png, etc).
     *
     * @param path ruta al archivo
     * @param maxSizeMB tamaño maximo en MB
     * @return vector con datos o vacio si falla
     */
    inline std::vector<uint8_t> readBinaryFile(const std::filesystem::path& path, size_t maxSizeMB = 10) {
        std::ifstream file(path, std::ios::binary);
        if (!file) return {};

        std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

        if (maxSizeMB > 0 && data.size() > maxSizeMB * 1024 * 1024) {
            return {};
        }

        return data;
    }

    /**
     * verifica si la extension es gif (case insensitive)
     */
    inline bool isGIF(const std::filesystem::path& path) {
        std::string ext = path.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        return ext == ".gif";
    }
}


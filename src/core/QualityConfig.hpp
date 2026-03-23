#pragma once

// QualityConfig.hpp — Unified quality-aware cache key and downscale helpers.
// Every cache subsystem (ThumbnailLoader, ProfileThumbs, LocalThumbs, AnimatedGIFSprite)
// should use these to guarantee one persistent variant per quality tier.

#include "Settings.hpp"
#include <string>
#include <cstdint>
#include <algorithm>
#include <cmath>
#include <vector>

namespace paimon::quality {

// ── Cache key / path helpers ────────────────────────────────────────

// Quality-prefixed cache directory under the mod save dir.
// Returns e.g. "cache_med", "cache_low", "cache_high".
inline std::filesystem::path cacheDir() {
    return geode::Mod::get()->getSaveDir() / settings::quality::cacheSubdir();
}

// Build a cache filename for a level thumbnail: "<levelID>.<ext>"
inline std::string thumbFilename(int levelID, bool isGif) {
    return std::to_string(levelID) + (isGif ? ".gif" : ".png");
}

// Build a cache filename for a profile image: "profile_<accountID>.<ext>"
inline std::string profileFilename(int accountID, bool isGif) {
    return "profile_" + std::to_string(accountID) + (isGif ? ".gif" : ".rgb");
}

// Full cache path for a level thumbnail at the active quality.
inline std::filesystem::path thumbCachePath(int levelID, bool isGif) {
    return cacheDir() / thumbFilename(levelID, isGif);
}

// Full cache path for a profile image at the active quality.
inline std::filesystem::path profileCachePath(int accountID, bool isGif) {
    return cacheDir() / profileFilename(accountID, isGif);
}

// ── Downscale logic ─────────────────────────────────────────────────

struct ScaledSize {
    int width;
    int height;
};

// Compute a downscaled size that fits within maxDim on the longest side
// while keeping the aspect ratio. Returns original size if already fits.
inline ScaledSize fitToQuality(int srcW, int srcH) {
    int maxDim = settings::quality::maxDimension();
    if (srcW <= maxDim && srcH <= maxDim) return {srcW, srcH};

    float scale = static_cast<float>(maxDim) / static_cast<float>(std::max(srcW, srcH));
    int dstW = std::max(1, static_cast<int>(std::floor(srcW * scale)));
    int dstH = std::max(1, static_cast<int>(std::floor(srcH * scale)));
    return {dstW, dstH};
}

// Downscale RGBA8888 pixel data in-place (box filter, simple & fast).
// Returns true if downscale was applied, false if the source already fits.
inline bool downscaleRGBA(std::vector<uint8_t>& pixels, int& w, int& h) {
    auto dst = fitToQuality(w, h);
    if (dst.width == w && dst.height == h) return false;

    int dstW = dst.width;
    int dstH = dst.height;
    std::vector<uint8_t> out(dstW * dstH * 4);

    float xRatio = static_cast<float>(w) / dstW;
    float yRatio = static_cast<float>(h) / dstH;

    for (int y = 0; y < dstH; ++y) {
        for (int x = 0; x < dstW; ++x) {
            int srcX = std::min(static_cast<int>(x * xRatio), w - 1);
            int srcY = std::min(static_cast<int>(y * yRatio), h - 1);
            int srcIdx = (srcY * w + srcX) * 4;
            int dstIdx = (y * dstW + x) * 4;
            out[dstIdx + 0] = pixels[srcIdx + 0];
            out[dstIdx + 1] = pixels[srcIdx + 1];
            out[dstIdx + 2] = pixels[srcIdx + 2];
            out[dstIdx + 3] = pixels[srcIdx + 3];
        }
    }

    pixels = std::move(out);
    w = dstW;
    h = dstH;
    return true;
}

// ── Legacy cache migration ──────────────────────────────────────────

// Rename the old "cache/" directory to the active quality dir if it
// doesn't exist yet.  This preserves every previously-cached thumbnail
// so users don't have to re-download everything after the update.
inline void migrateLegacyCache() {
    auto saveDir  = geode::Mod::get()->getSaveDir();
    auto qualDir  = cacheDir();
    std::error_code ec;

    // If the quality dir already exists, nothing to migrate.
    if (std::filesystem::exists(qualDir, ec)) return;

    auto legacyDir = saveDir / "cache";
    if (std::filesystem::exists(legacyDir, ec) && std::filesystem::is_directory(legacyDir, ec)) {
        std::filesystem::rename(legacyDir, qualDir, ec);
        // rename may fail (cross-device, permissions, etc.) — ignore silently,
        // initDiskCache will just create an empty quality dir.
    }
}

} // namespace paimon::quality

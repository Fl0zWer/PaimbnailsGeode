#pragma once
#include <optional>
#include <string>
#include <filesystem>

namespace pt {
    // Open a native file picker dialog to select an image file (png/jpg/jpeg/webp/gif/bmp/tiff)
    // Windows-only implementation; on other platforms returns std::nullopt.
    std::optional<std::filesystem::path> openImageFileDialog();

    // Open a native file picker dialog to select an audio file (mp3/ogg/wav/flac/m4a)
    // Windows-only implementation; on other platforms returns std::nullopt.
    std::optional<std::filesystem::path> openAudioFileDialog();

    // Save dialog for images (defaults to PNG). Returns chosen path or nullopt on cancel.
    std::optional<std::filesystem::path> saveImageFileDialog(std::wstring const& defaultName);

    // Open a folder picker dialog. Returns chosen folder path or nullopt on cancel.
    std::optional<std::filesystem::path> openFolderDialog();
}


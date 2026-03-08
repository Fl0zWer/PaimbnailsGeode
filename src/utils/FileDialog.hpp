#pragma once
#include <Geode/utils/file.hpp>
#include <functional>
#include <optional>
#include <string>
#include <filesystem>

namespace pt {
    using FileCallback = std::function<void(std::optional<std::filesystem::path>)>;

    // Open a cross-platform file picker to select an image file
    void openImageFileDialog(FileCallback callback);

    // Open a cross-platform file picker to select an audio file
    void openAudioFileDialog(FileCallback callback);

    // Save dialog for images (defaults to PNG)
    void saveImageFileDialog(std::string const& defaultName, FileCallback callback);

    // Open a cross-platform folder picker
    void openFolderDialog(FileCallback callback);
}


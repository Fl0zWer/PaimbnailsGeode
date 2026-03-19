#pragma once
#include <Geode/utils/file.hpp>
#include <functional>
#include <optional>
#include <string>
#include <filesystem>

namespace pt {
    using FileCallback = std::function<void(std::optional<std::filesystem::path>)>;

    // abre un selector de archivos para imagen
    void openImageFileDialog(FileCallback callback);

    // abre un selector de archivos para audio
    void openAudioFileDialog(FileCallback callback);

    // dialogo para guardar imagen, por defecto PNG
    void saveImageFileDialog(std::string const& defaultName, FileCallback callback);

    // abre un selector de carpetas
    void openFolderDialog(FileCallback callback);
}


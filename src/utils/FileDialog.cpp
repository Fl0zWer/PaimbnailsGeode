#include "FileDialog.hpp"

#include <Geode/Geode.hpp>
#include <Geode/utils/file.hpp>

using namespace geode::prelude;
using namespace geode::utils::file;

namespace pt {
    static bool s_dialogOpen = false;

    static void pickFile(PickMode mode, FilePickOptions options, FileCallback callback) {
        if (s_dialogOpen) {
            if (callback) callback(std::nullopt);
            return;
        }
        s_dialogOpen = true;

        auto future = pick(mode, std::move(options));
        auto cb = std::make_shared<FileCallback>(std::move(callback));

        geode::async::spawn(std::move(future), [cb](PickResult result) {
            s_dialogOpen = false;
            if (!cb || !*cb) return;

            if (result.isOk()) {
                (*cb)(result.unwrap());
            } else {
                log::warn("[FileDialog] pick failed: {}", result.unwrapErr());
                (*cb)(std::nullopt);
            }
        });
    }

    void openImageFileDialog(FileCallback callback) {
        FilePickOptions options;
        options.filters = {{
            "Image Files",
            {"*.png", "*.jpg", "*.jpeg", "*.webp", "*.gif", "*.bmp", "*.tiff", "*.tif"}
        }};
        pickFile(PickMode::OpenFile, std::move(options), std::move(callback));
    }

    void openAudioFileDialog(FileCallback callback) {
        FilePickOptions options;
        options.filters = {{
            "Audio Files",
            {"*.mp3", "*.ogg", "*.wav", "*.flac", "*.m4a"}
        }};
        pickFile(PickMode::OpenFile, std::move(options), std::move(callback));
    }

    void saveImageFileDialog(std::string const& defaultName, FileCallback callback) {
        FilePickOptions options;
        options.defaultPath = std::filesystem::path(defaultName);
        options.filters = {{
            "PNG Image",
            {"*.png"}
        }};
        pickFile(PickMode::SaveFile, std::move(options), std::move(callback));
    }

    void openFolderDialog(FileCallback callback) {
        FilePickOptions options;
        pickFile(PickMode::OpenFolder, std::move(options), std::move(callback));
    }
}


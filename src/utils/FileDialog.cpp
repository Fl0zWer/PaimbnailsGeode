#include "FileDialog.hpp"

#include <Geode/Geode.hpp>
#include <Geode/utils/file.hpp>
#include <atomic>

using namespace geode::prelude;
using namespace geode::utils::file;

namespace pt {
    static std::atomic_bool s_dialogOpen = false;

    static void pickFile(PickMode mode, FilePickOptions options, FileCallback callback) {
        bool expected = false;
        if (!s_dialogOpen.compare_exchange_strong(expected, true)) {
            if (callback) callback(std::nullopt);
            return;
        }

        auto future = pick(mode, std::move(options));
        auto cb = std::make_shared<FileCallback>(std::move(callback));

        geode::async::spawn(std::move(future), [cb](PickResult result) {
            s_dialogOpen.store(false, std::memory_order_release);
            if (!cb || !*cb) return;

            std::optional<std::filesystem::path> outPath = std::nullopt;
            if (result.isOk()) {
                outPath = result.unwrap();
            } else {
                log::warn("[FileDialog] pick failed: {}", result.unwrapErr());
            }

            // Garantiza que todo callback que toque UI/cocos corra en main thread.
            Loader::get()->queueInMainThread([cb, outPath]() {
                if (cb && *cb) {
                    (*cb)(outPath);
                }
            });
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


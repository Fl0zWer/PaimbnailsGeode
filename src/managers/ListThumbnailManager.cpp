#include "ListThumbnailManager.hpp"
#include "ThumbnailLoader.hpp"
#include "LocalThumbs.hpp"
#include <Geode/ui/LazySprite.hpp>
#include <fstream>
#include <filesystem>

using namespace geode::prelude;

ListThumbnailManager& ListThumbnailManager::get() {
    static ListThumbnailManager instance;
    return instance;
}

void ListThumbnailManager::processList(const std::vector<int>& levelIDs, ListCallback callback, std::shared_ptr<bool> callerAlive) {
    for (int id : levelIDs) {
        if (callerAlive && !*callerAlive) return;
        
        std::optional<std::filesystem::path> localPath;
        if (auto p = LocalThumbs::get().findAnyThumbnail(id))
            localPath = std::filesystem::path(*p);
        else if (!ThumbnailLoader::get().hasGIFData(id)) {
            auto cp = ThumbnailLoader::get().getCachePath(id, false);
            if (!cp.empty() && std::filesystem::exists(cp))
                localPath = cp;
        }
        if (localPath && callback) {
            auto lazy = LazySprite::create(CCSize(80, 45), false);
            lazy->retain();
            lazy->setLoadCallback([id, callback, callerAlive, lazy](geode::Result<> res) {
                if (callerAlive && !*callerAlive) { lazy->release(); return; }
                if (res.isOk() && lazy->getTexture() && callback)
                    callback(id, lazy->getTexture());
                lazy->release();
            });
            lazy->loadFromFile(*localPath);
        } else {
            std::string fileName = fmt::format("{}.png", id);
            ThumbnailLoader::get().requestLoad(id, fileName, [id, callback, callerAlive](cocos2d::CCTexture2D* tex, bool success) {
                if (callerAlive && !*callerAlive) return;
                if (success && callback)
                    callback(id, tex);
            }, 0);
        }
    }
}

void ListThumbnailManager::workerLoop() {}
void ListThumbnailManager::processBatch(const BatchTask& task) {}
void ListThumbnailManager::cacheTexture(int levelID, cocos2d::CCTexture2D* texture) {}

#include <Geode/modify/LevelPage.hpp>
#include <Geode/ui/LazySprite.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include "../managers/ThumbnailLoader.hpp"
#include "../managers/ThumbnailAPI.hpp"
#include "../managers/LocalThumbs.hpp"
#include "../utils/Assets.hpp"
#include <filesystem>

using namespace geode::prelude;

class $modify(PaimonLevelPage, LevelPage) {
    struct Fields {
        CCNode* m_thumbClipper = nullptr;
        CCSprite* m_thumbSprite = nullptr;
        int m_levelID = 0;
    };

    void updateDynamicPage(GJGameLevel* level) {
        LevelPage::updateDynamicPage(level);
        
        if (!level) return;
        
        m_fields->m_levelID = level->m_levelID;
        
        // solo id > 0
        if (level->m_levelID <= 0) return;
        
        if (!this->m_levelDisplay) return;

        int levelID = level->m_levelID;
        std::string fileName = fmt::format("{}.png", levelID);

        // Ruta rápida: LazySprite si archivo local existe
        std::optional<std::filesystem::path> localPath;
        if (auto p = LocalThumbs::get().findAnyThumbnail(levelID))
            localPath = std::filesystem::path(*p);
        else if (!ThumbnailLoader::get().hasGIFData(levelID)) {
            auto cp = ThumbnailLoader::get().getCachePath(levelID, false);
            if (!cp.empty() && std::filesystem::exists(cp))
                localPath = cp;
        }
        if (localPath) {
            auto lazy = LazySprite::create(m_levelDisplay->getContentSize(), true);
            auto selfPtr = this;
            selfPtr->retain();
            lazy->retain();
            lazy->setLoadCallback([selfPtr, level, lazy](geode::Result<> res) {
                if (res.isOk() && lazy->getTexture() && selfPtr->m_fields->m_levelID == level->m_levelID) {
                    selfPtr->applyThumbnail(lazy->getTexture());
                }
                lazy->release();
                selfPtr->release();
            });
            lazy->loadFromFile(*localPath);
            return;
        }

        auto selfPtr = this;
        this->retain();
        ThumbnailLoader::get().requestLoad(levelID, fileName, [selfPtr, level](CCTexture2D* tex, bool success) {
            if (success && tex && selfPtr->m_fields->m_levelID == level->m_levelID) {
                selfPtr->applyThumbnail(tex);
            }
            selfPtr->release();
        }, 5);
    }
    
    void applyThumbnail(CCTexture2D* tex) {
        if (!tex || !m_levelDisplay) return;
        
        if (m_fields->m_thumbClipper) {
            m_fields->m_thumbClipper->removeFromParent();
            m_fields->m_thumbClipper = nullptr;
            m_fields->m_thumbSprite = nullptr;
        }
        
        auto sprite = CCSprite::createWithTexture(tex);
        if (!sprite) return;
        
        CCSize boxSize = m_levelDisplay->getContentSize();
        
        // clipping pa thumb en caja
        // uso un stencil 'square02_001.png' que es un cuadrado redondeado
        auto stencil = CCScale9Sprite::create("square02_001.png");
        stencil->setContentSize(boxSize);
        stencil->setPosition(boxSize / 2);
        
        auto clipper = CCClippingNode::create(stencil);
        clipper->setContentSize(boxSize);
        clipper->setAnchorPoint({0.5f, 0.5f});
        clipper->setPosition(boxSize / 2); // centro en m_leveldisplay
        clipper->setAlphaThreshold(0.05f);
        
        // scale sprite cover caja
        float scaleX = boxSize.width / sprite->getContentSize().width;
        float scaleY = boxSize.height / sprite->getContentSize().height;
        float scale = std::max(scaleX, scaleY);
        
        sprite->setScale(scale);
        sprite->setPosition(boxSize / 2); // centro en el clipper
        sprite->setColor({150, 150, 150}); // oscurezco un poco

        clipper->addChild(sprite);
        
        // clipper a m_leveldisplay
        m_levelDisplay->addChild(clipper, -1);
        
        m_fields->m_thumbClipper = clipper;
        m_fields->m_thumbSprite = sprite;
    }
};

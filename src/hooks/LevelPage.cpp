#include <Geode/modify/LevelPage.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include "../features/thumbnails/services/ThumbnailLoader.hpp"
#include "../managers/ThumbnailAPI.hpp"
#include "../utils/Assets.hpp"

using namespace geode::prelude;

class $modify(PaimonLevelPage, LevelPage) {
    static void onModify(auto& self) {
        // El thumbnail se aplica despues del layout/base page original.
        (void)self.setHookPriorityPost("LevelPage::updateDynamicPage", geode::Priority::Late);
    }

    struct Fields {
        Ref<CCNode> m_thumbClipper = nullptr;
        Ref<CCSprite> m_thumbSprite = nullptr;
        int m_levelID = 0;
    };

    $override
    void updateDynamicPage(GJGameLevel* level) {
        LevelPage::updateDynamicPage(level);
        
        if (!level) return;
        
        m_fields->m_levelID = level->m_levelID;
        
        // solo id > 0
        if (level->m_levelID <= 0) return;
        
        if (this->m_levelDisplay) {
            std::string fileName = fmt::format("{}.png", level->m_levelID);
            Ref<LevelPage> safeRef = this;
            int capturedLevelID = level->m_levelID;
            ThumbnailLoader::get().requestLoad(level->m_levelID, fileName, [safeRef, capturedLevelID](CCTexture2D* tex, bool success) {
                auto* self = static_cast<PaimonLevelPage*>(safeRef.data());
                if (!self->getParent()) return;
                if (success && tex && self->m_fields->m_levelID == capturedLevelID) {
                    self->applyThumbnail(tex);
                }
            }, 5);
        }
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
        // stencil geometrico — evita conflictos con HappyTextures/TextureLdr
        auto stencil = CCDrawNode::create();
        CCPoint rect[4] = { ccp(0,0), ccp(boxSize.width,0), ccp(boxSize.width,boxSize.height), ccp(0,boxSize.height) };
        ccColor4F white = {1,1,1,1};
        stencil->drawPolygon(rect, 4, white, 0, white);
        
        auto clipper = CCClippingNode::create(stencil);
        clipper->setContentSize(boxSize);
        clipper->setAnchorPoint({0.5f, 0.5f});
        clipper->setPosition(boxSize / 2); // centro en m_leveldisplay
        
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

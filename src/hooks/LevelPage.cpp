#include <Geode/modify/LevelPage.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include "../features/thumbnails/services/ThumbnailLoader.hpp"
#include "../managers/ThumbnailAPI.hpp"
#include "../utils/Assets.hpp"
#include <algorithm>
#include <vector>
#include <cmath>

using namespace geode::prelude;

static CCDrawNode* createRoundedRectStencil(float width, float height, float radius, int cornerSegments = 8) {
    auto draw = CCDrawNode::create();
    if (!draw || width <= 0.f || height <= 0.f) return draw;

    float maxRadius = std::min(width, height) * 0.5f;
    float r = std::clamp(radius, 0.f, maxRadius);

    if (r <= 0.01f) {
        CCPoint rect[4] = { ccp(0, 0), ccp(width, 0), ccp(width, height), ccp(0, height) };
        ccColor4F white = {1.f, 1.f, 1.f, 1.f};
        draw->drawPolygon(rect, 4, white, 0, white);
        draw->setContentSize({width, height});
        return draw;
    }

    int seg = std::max(2, cornerSegments);
    std::vector<CCPoint> verts;
    verts.reserve(static_cast<size_t>(seg * 4 + 4));

    auto appendArc = [&verts, seg](float cx, float cy, float rad, float startDeg, float endDeg) {
        for (int i = 0; i <= seg; ++i) {
            float t = static_cast<float>(i) / static_cast<float>(seg);
            float deg = startDeg + (endDeg - startDeg) * t;
            float a = CC_DEGREES_TO_RADIANS(deg);
            verts.push_back(ccp(cx + cosf(a) * rad, cy + sinf(a) * rad));
        }
    };

    appendArc(r, r, r, 180.f, 270.f);
    appendArc(width - r, r, r, 270.f, 360.f);
    appendArc(width - r, height - r, r, 0.f, 90.f);
    appendArc(r, height - r, r, 90.f, 180.f);

    ccColor4F white = {1.f, 1.f, 1.f, 1.f};
    draw->drawPolygon(verts.data(), static_cast<unsigned int>(verts.size()), white, 0, white);
    draw->setContentSize({width, height});
    return draw;
}

class $modify(PaimonLevelPage, LevelPage) {
    static void onModify(auto& self) {
        // El thumbnail se aplica despues del layout/base page original.
        (void)self.setHookPriorityPost("LevelPage::updateDynamicPage", geode::Priority::Late);
    }

    struct Fields {
        Ref<CCNode> m_thumbClipper = nullptr;
        Ref<CCSprite> m_thumbSprite = nullptr;
        int m_levelID = 0;
        std::vector<ThumbnailAPI::ThumbnailInfo> m_thumbnails;
        int m_currentThumbnailIndex = 0;
        float m_cycleTimer = 0.f;
        int m_cycleToken = 0;
        int m_invalidationListenerId = 0;
    };

    $override
    void updateDynamicPage(GJGameLevel* level) {
        LevelPage::updateDynamicPage(level);
        
        if (!level) return;
        log::info("[LevelPage] updateDynamicPage: levelID={}", level->m_levelID.value());
        
        m_fields->m_levelID = level->m_levelID;
        m_fields->m_cycleTimer = 0.f;
        m_fields->m_currentThumbnailIndex = 0;
        this->unschedule(schedule_selector(PaimonLevelPage::updateGalleryCycle));

        if (m_fields->m_invalidationListenerId == 0) {
            WeakRef<PaimonLevelPage> safeRef = this;
            m_fields->m_invalidationListenerId = ThumbnailLoader::get().addInvalidationListener([safeRef](int invalidLevelID) {
                auto ref = safeRef.lock();
                auto* self = static_cast<PaimonLevelPage*>(ref.data());
                if (!self || !self->getParent()) return;
                if (!self->m_level || self->m_level->m_levelID != invalidLevelID) return;
                self->updateDynamicPage(self->m_level);
            });
        }
        
        // solo id > 0
        if (level->m_levelID <= 0) return;
        
        if (this->m_levelDisplay) {
            int capturedLevelID = level->m_levelID;
            int token = ++m_fields->m_cycleToken;
            Ref<LevelPage> safeRef = this;
            ThumbnailAPI::get().getThumbnails(capturedLevelID, [safeRef, capturedLevelID, token](bool success, std::vector<ThumbnailAPI::ThumbnailInfo> const& thumbs) {
                auto* self = static_cast<PaimonLevelPage*>(safeRef.data());
                if (!self->getParent() || self->m_fields->m_cycleToken != token || self->m_fields->m_levelID != capturedLevelID) return;

                if (success && !thumbs.empty()) {
                    self->m_fields->m_thumbnails = thumbs;
                    self->loadThumbnailAt(0);
                    if (thumbs.size() >= 2 && Mod::get()->getSettingValue<bool>("levelcell-gallery-autocycle")) {
                        self->schedule(schedule_selector(PaimonLevelPage::updateGalleryCycle), 0.f);
                    }
                    return;
                }

                std::string fileName = fmt::format("{}.png", capturedLevelID);
                ThumbnailLoader::get().requestLoad(capturedLevelID, fileName, [safeRef, capturedLevelID](CCTexture2D* tex, bool loadSuccess) {
                    auto* self = static_cast<PaimonLevelPage*>(safeRef.data());
                    if (!self->getParent() || self->m_fields->m_levelID != capturedLevelID) return;
                    if (loadSuccess && tex) self->applyThumbnail(tex);
                }, 5);
            });
        }
    }

    $override
    void onExit() {
        this->unschedule(schedule_selector(PaimonLevelPage::updateGalleryCycle));
        if (m_fields->m_invalidationListenerId != 0) {
            ThumbnailLoader::get().removeInvalidationListener(m_fields->m_invalidationListenerId);
            m_fields->m_invalidationListenerId = 0;
        }
        m_fields->m_cycleToken++;
        LevelPage::onExit();
    }

    void updateGalleryCycle(float dt) {
        if (m_fields->m_thumbnails.size() < 2) return;
        m_fields->m_cycleTimer += dt;
        if (m_fields->m_cycleTimer < 3.0f) return;
        m_fields->m_cycleTimer = 0.f;
        int next = (m_fields->m_currentThumbnailIndex + 1) % static_cast<int>(m_fields->m_thumbnails.size());
        loadThumbnailAt(next);
    }

    void loadThumbnailAt(int index) {
        if (index < 0 || index >= static_cast<int>(m_fields->m_thumbnails.size())) return;
        log::debug("[LevelPage] loadThumbnailAt: index={} levelID={}", index, m_fields->m_levelID);
        m_fields->m_currentThumbnailIndex = index;
        int capturedLevelID = m_fields->m_levelID;
        int token = ++m_fields->m_cycleToken;
        std::string url = m_fields->m_thumbnails[index].url;
        auto sep = (url.find('?') == std::string::npos) ? "?" : "&";
        url += fmt::format("{}_pv={}{}", sep, m_fields->m_thumbnails[index].id, token);
        int attemptIndex = index;
        Ref<LevelPage> safeRef = this;
        ThumbnailAPI::get().downloadFromUrl(url, [safeRef, capturedLevelID, token, attemptIndex](bool success, CCTexture2D* tex) {
            auto* self = static_cast<PaimonLevelPage*>(safeRef.data());
            if (!self->getParent() || self->m_fields->m_cycleToken != token || self->m_fields->m_levelID != capturedLevelID) return;
            if (success && tex) {
                self->applyThumbnail(tex);
            } else if (self->m_fields->m_thumbnails.size() > 1) {
                int next = (attemptIndex + 1) % static_cast<int>(self->m_fields->m_thumbnails.size());
                if (next != attemptIndex) self->loadThumbnailAt(next);
            }
        });
    }
    
    void applyThumbnail(CCTexture2D* tex) {
        if (!tex || !m_levelDisplay) return;
        log::debug("[LevelPage] applyThumbnail: levelID={}", m_fields->m_levelID);
        
        if (m_fields->m_thumbClipper) {
            m_fields->m_thumbClipper->removeFromParent();
            m_fields->m_thumbClipper = nullptr;
            m_fields->m_thumbSprite = nullptr;
        }
        
        auto sprite = CCSprite::createWithTexture(tex);
        if (!sprite) return;
        
        CCSize boxSize = m_levelDisplay->getContentSize();
        
        // clipping redondeado para niveles oficiales (SelectLevel/LevelPage)
        float cornerRadius = std::clamp(boxSize.height * 0.11f, 6.f, 14.f);
        auto stencil = createRoundedRectStencil(boxSize.width, boxSize.height, cornerRadius, 8);
        if (!stencil) return;
        
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
        sprite->setColor({255, 255, 255});

        clipper->addChild(sprite);

        // filtro oscuro transparente dentro del mismo clipper (bordes redondeados)
        auto darkOverlay = CCLayerColor::create({0, 0, 0, 90});
        darkOverlay->setContentSize(boxSize);
        darkOverlay->setPosition({0, 0});
        clipper->addChild(darkOverlay, 2);
        
        // clipper a m_leveldisplay
        m_levelDisplay->addChild(clipper, -1);
        
        m_fields->m_thumbClipper = clipper;
        m_fields->m_thumbSprite = sprite;
    }
};

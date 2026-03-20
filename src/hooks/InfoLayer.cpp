#include <Geode/binding/InfoLayer.hpp>
#include <Geode/modify/InfoLayer.hpp>
#include <Geode/binding/GJUserScore.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/binding/GJLevelList.hpp>
#include <Geode/binding/GJCommentListLayer.hpp>
#include <Geode/binding/CommentCell.hpp>
#include <Geode/utils/cocos.hpp>
#include <Geode/loader/Mod.hpp>
#include "../utils/Shaders.hpp"
#include "../managers/ThumbnailAPI.hpp"
#include "../features/thumbnails/services/ThumbnailLoader.hpp"
#include "../features/profile-music/services/ProfileMusicManager.hpp"
#include <algorithm>
#include <string>

using namespace geode::prelude;

// Declarado en ProfilePage.cpp â€” acceso al cache de texturas de profileimg.
extern CCTexture2D* getProfileImgCachedTexture(int accountID);

class $modify(PaimonInfoLayer, InfoLayer) {
    static void onModify(auto& self) {
        (void)self.setHookPriorityAfterPost("InfoLayer::init", "geode.node-ids");
    }

    struct Fields {
        Ref<CCClippingNode> m_bgClip = nullptr;
        Ref<CCClippingNode> m_commentsBlurClip = nullptr;
        Ref<CCSprite> m_commentsBlurSprite = nullptr;
        Ref<CCLayerColor> m_commentsBlurDark = nullptr;
        bool m_hasCaveEffect = false;
    };

    bool getPopupInteriorGeometry(CCPoint& outCenter, CCSize& outSize) {
        auto layer = this->m_mainLayer;
        if (!layer) return false;
        auto layerSize = layer->getContentSize();
        CCSize popupSize = CCSize(440.f, 290.f);
        CCPoint popupCenter = ccp(layerSize.width * 0.5f, layerSize.height * 0.5f);
        if (auto bg = layer->getChildByID("background")) {
            popupSize = bg->getScaledContentSize();
            popupCenter = bg->getPosition();
        } else {
            for (auto* child : CCArrayExt<CCNode*>(layer->getChildren())) {
                if (typeinfo_cast<CCScale9Sprite*>(child)) {
                    popupSize = child->getScaledContentSize();
                    popupCenter = child->getPosition();
                    break;
                }
            }
        }
        float padding = 3.f;
        outSize = CCSize(
            std::max(1.0f, popupSize.width - padding * 2.f),
            std::max(1.0f, popupSize.height - padding * 2.f)
        );
        outCenter = popupCenter;
        return true;
    }

    $override
    bool init(GJGameLevel* level, GJUserScore* score, GJLevelList* list) {
        if (!InfoLayer::init(level, score, list)) return false;

        // blur del panel de comentarios basado en miniatura del nivel (si existe)
        if (level && level->m_levelID.value() > 0) {
            int32_t levelID = level->m_levelID.value();
            std::string fileName = fmt::format("{}.png", levelID);
            Ref<InfoLayer> safeRef = this;
            ThumbnailLoader::get().requestLoad(levelID, fileName, [safeRef, levelID, fileName](CCTexture2D* texture, bool success) {
                auto applyOnMain = [safeRef](CCTexture2D* tex) {
                    Loader::get()->queueInMainThread([safeRef, tex]() {
                        auto* self = static_cast<PaimonInfoLayer*>(safeRef.data());
                        if (!self || !self->getParent()) return;
                        self->applyCommentsBlurBackground(tex);
                    });
                };

                if (success && texture) {
                    applyOnMain(texture);
                    return;
                }

                ThumbnailLoader::get().requestLoad(levelID, fileName, [applyOnMain](CCTexture2D* fallbackTex, bool fallbackSuccess) {
                    if (fallbackSuccess && fallbackTex) {
                        applyOnMain(fallbackTex);
                    }
                }, 5, false);
            }, 6, true);
        }

        // solo aplicar fondo de perfil si es popup de usuario
        if (!score) return true;

        int accountID = score->m_accountID;
        if (accountID <= 0) return true;

        // Intentar del cache primero
        CCTexture2D* tex = getProfileImgCachedTexture(accountID);
        if (!tex) {
            // no hay cache, bajar en background
            Ref<InfoLayer> safeRef = this;
            ThumbnailAPI::get().downloadProfileImg(accountID, [safeRef](bool success, CCTexture2D* texture) {
                if (success && texture) {
                    Loader::get()->queueInMainThread([safeRef, texture]() {
                        auto* self = static_cast<PaimonInfoLayer*>(safeRef.data());
                        if (self && self->getParent()) {
                            self->applyBlurredBackground(texture);
                        }
                    });
                }
            }, false);
            return true;
        }

        applyBlurredBackground(tex);

        // Aplicar efecto cueva si hay musica de perfil sonando
        auto& musicMgr = ProfileMusicManager::get();
        if (musicMgr.isPlaying() && !musicMgr.isFadingOut() && !musicMgr.hasCaveEffect()) {
            musicMgr.applyCaveEffect();
        }
        m_fields->m_hasCaveEffect = musicMgr.hasCaveEffect();

        return true;
    }

    void applyCommentsBlurBackground(CCTexture2D* tex) {
        if (!tex) return;
        auto layer = this->m_mainLayer;
        if (!layer) return;

        CCPoint panelCenter;
        CCSize panelSize;
        if (!getPopupInteriorGeometry(panelCenter, panelSize)) return;

        if (m_fields->m_commentsBlurClip) {
            m_fields->m_commentsBlurClip->removeFromParent();
            m_fields->m_commentsBlurClip = nullptr;
            m_fields->m_commentsBlurSprite = nullptr;
            m_fields->m_commentsBlurDark = nullptr;
        }

        auto blurred = Shaders::createBlurredSprite(tex, panelSize, 6.5f, true);
        if (!blurred) return;
        blurred->setPosition({panelSize.width * 0.5f, panelSize.height * 0.5f});
        blurred->setID("paimon-infolayer-comments-blur-sprite"_spr);

        auto stencil = CCDrawNode::create();
        CCPoint rect[4] = { ccp(0,0), ccp(panelSize.width,0), ccp(panelSize.width,panelSize.height), ccp(0,panelSize.height) };
        ccColor4F white = {1,1,1,1};
        stencil->drawPolygon(rect, 4, white, 0, white);

        auto clip = CCClippingNode::create();
        clip->setStencil(stencil);
        clip->setContentSize(panelSize);
        clip->setAnchorPoint(ccp(0.5f, 0.5f));
        clip->setPosition(panelCenter);
        clip->setID("paimon-infolayer-comments-blur-clip"_spr);

        clip->addChild(blurred);

        auto dark = CCLayerColor::create(ccc4(0, 0, 0, 95));
        dark->setContentSize(panelSize);
        dark->setAnchorPoint({0.f, 0.f});
        dark->setPosition({0.f, 0.f});
        dark->setID("paimon-infolayer-comments-blur-dark"_spr);
        clip->addChild(dark);

        layer->addChild(clip, -1);

        m_fields->m_commentsBlurClip = clip;
        m_fields->m_commentsBlurSprite = blurred;
        m_fields->m_commentsBlurDark = dark;

        styleInfoLayerBgs(layer);
        this->schedule(schedule_selector(PaimonInfoLayer::tickStyleBgs), 0.5f);
    }

    void applyBlurredBackground(CCTexture2D* tex) {
        if (!tex) return;

        auto layer = this->m_mainLayer;
        if (!layer) return;
        auto layerSize = layer->getContentSize();

        // Buscar el bg del popup por node ID
        CCSize popupSize = CCSize(440.f, 290.f);
        CCPoint popupCenter = ccp(layerSize.width * 0.5f, layerSize.height * 0.5f);

        if (auto bg = layer->getChildByID("background")) {
            popupSize = bg->getScaledContentSize();
            popupCenter = bg->getPosition();
        } else {
            // fallback: primer Scale9 hijo
            for (auto* child : CCArrayExt<CCNode*>(layer->getChildren())) {
                if (typeinfo_cast<CCScale9Sprite*>(child)) {
                    popupSize = child->getScaledContentSize();
                    popupCenter = child->getPosition();
                    break;
                }
            }
        }

        float padding = 3.f;
        CCSize imgArea = CCSize(popupSize.width - padding * 2.f, popupSize.height - padding * 2.f);

        // blur gaussiano multi-paso
        auto blurredSprite = Shaders::createBlurredSprite(tex, imgArea, 7.0f);
        if (!blurredSprite) return;

        auto blurSize = blurredSprite->getContentSize();
        if (blurSize.width > 0.f && blurSize.height > 0.f) {
            float scale = std::max(imgArea.width / blurSize.width, imgArea.height / blurSize.height);
            if (scale > 0.f) {
                blurredSprite->setScale(std::clamp(scale, 0.01f, 64.0f));
            }
        }
        blurredSprite->setPosition(ccp(imgArea.width * 0.5f, imgArea.height * 0.5f));

        // stencil geometrico, no sprites (pa no chocar con HappyTextures)
        auto stencil = CCDrawNode::create();
        CCPoint rect[4] = { ccp(0,0), ccp(imgArea.width,0), ccp(imgArea.width,imgArea.height), ccp(0,imgArea.height) };
        ccColor4F white = {1,1,1,1};
        stencil->drawPolygon(rect, 4, white, 0, white);

        auto clip = CCClippingNode::create();
        clip->setStencil(stencil);
        clip->setContentSize(imgArea);
        clip->setAnchorPoint(ccp(0.5f, 0.5f));
        clip->setPosition(popupCenter);
        clip->setID("paimon-infolayer-bg-clip"_spr);

        clip->addChild(blurredSprite);

        // overlay oscuro pa legibilidad
        auto dark = CCLayerColor::create(ccc4(0, 0, 0, 120));
        dark->setContentSize(imgArea);
        dark->setAnchorPoint(ccp(0, 0));
        dark->setPosition(ccp(0, 0));
        dark->setID("paimon-infolayer-dark-overlay"_spr);
        clip->addChild(dark);

        // detras de todo (zOrder -1)
        layer->addChild(clip, -1);
        m_fields->m_bgClip = clip;

        // ocultar decoraciones del popup
        styleInfoLayerBgs(layer);

        // re-aplicar estilos periodicamente
        this->schedule(schedule_selector(PaimonInfoLayer::tickStyleBgs), 0.5f);
    }

    void styleInfoLayerBgs(CCNode* root) {
        if (!root) return;

        auto walk = [&](auto const& self, CCNode* parent) -> void {
            if (!parent) return;
            auto* children = parent->getChildren();
            if (!children) return;
            for (auto* child : CCArrayExt<CCNode*>(children)) {
                if (!child) continue;
                auto childId = std::string(child->getID());
                if (!childId.empty() && childId.rfind("paimon-infolayer-comments-blur", 0) == 0) {
                    continue;
                }

                // GJCommentListLayer: transparentar y quitar bordes
                if (auto* commentList = typeinfo_cast<GJCommentListLayer*>(child)) {
                    commentList->setOpacity(0);

                    auto* listChildren = commentList->getChildren();
                    if (listChildren) {
                        for (auto* lc : CCArrayExt<CCNode*>(listChildren)) {
                            if (!lc) continue;
                            auto id = lc->getID();
                            if (id == "left-border" || id == "right-border" ||
                                id == "top-border" || id == "bottom-border") {
                                lc->setVisible(false);
                            }
                            if (id.empty()) {
                                lc->setVisible(false);
                            }
                        }
                    }

                    // quitar fondos de las celdas de comentarios
                    hideCommentCellBgs(commentList);
                }

                self(self, child);
            }
        };

        walk(walk, root);
    }

    void hideCommentCellBgs(CCNode* listNode) {
        if (!listNode) return;

        auto findCells = [&](auto const& self, CCNode* node) -> void {
            if (!node) return;
            auto* children = node->getChildren();
            if (!children) return;
            for (auto* child : CCArrayExt<CCNode*>(children)) {
                if (!child) continue;

                if (typeinfo_cast<CommentCell*>(child)) {
                    auto* cellChildren = child->getChildren();
                    if (!cellChildren) continue;
                    for (auto* cc : CCArrayExt<CCNode*>(cellChildren)) {
                        if (!cc) continue;

                        // CCLayerColor = fondo de celda
                        if (typeinfo_cast<CCLayerColor*>(cc)) {
                            cc->setVisible(false);
                        }

                        // en CCLayer hijo: solo quitar Scale9 de fondo
                        if (typeinfo_cast<CCLayer*>(cc) && !typeinfo_cast<CCLayerColor*>(cc)) {
                            auto* layerKids = cc->getChildren();
                            if (!layerKids) continue;
                            for (auto* lk : CCArrayExt<CCNode*>(layerKids)) {
                                if (lk && typeinfo_cast<CCScale9Sprite*>(lk)) {
                                    lk->setVisible(false);
                                }
                            }
                        }
                    }
                }

                self(self, child);
            }
        };

        findCells(findCells, listNode);
    }

    void tickStyleBgs(float) {
        if (auto* layer = this->m_mainLayer) {
            styleInfoLayerBgs(layer);
        }
    }

    $override
    void keyBackClicked() {
        restoreMusicEffect();
        InfoLayer::keyBackClicked();
    }

    $override
    void onExit() {
        restoreMusicEffect();
        this->unschedule(schedule_selector(PaimonInfoLayer::tickStyleBgs));
        if (m_fields->m_commentsBlurClip) {
            m_fields->m_commentsBlurClip->removeFromParent();
            m_fields->m_commentsBlurClip = nullptr;
            m_fields->m_commentsBlurSprite = nullptr;
            m_fields->m_commentsBlurDark = nullptr;
        }
        InfoLayer::onExit();
    }

    void restoreMusicEffect() {
        if (m_fields->m_hasCaveEffect) {
            // Forzar eliminacion inmediata para que el efecto no quede colgado al salir
            ProfileMusicManager::get().forceRemoveCaveEffect();
            m_fields->m_hasCaveEffect = false;
        }
    }
};

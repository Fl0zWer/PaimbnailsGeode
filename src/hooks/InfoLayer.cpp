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
#include "../utils/SpriteHelper.hpp"
#include "../managers/ThumbnailAPI.hpp"
#include "../features/profile-music/services/ProfileMusicManager.hpp"

using namespace geode::prelude;

// Declarado en ProfilePage.cpp â€” acceso al cache de texturas de profileimg.
extern CCTexture2D* getProfileImgCachedTexture(int accountID);

class $modify(PaimonInfoLayer, InfoLayer) {
    static void onModify(auto& self) {
        (void)self.setHookPriorityAfterPost("InfoLayer::init", "geode.node-ids");
    }

    struct Fields {
        Ref<CCClippingNode> m_bgClip = nullptr;
        bool m_hasCaveEffect = false;
    };

    $override
    bool init(GJGameLevel* level, GJUserScore* score, GJLevelList* list) {
        if (!InfoLayer::init(level, score, list)) return false;

    // solo aplicar fondo si es un perfil de usuario
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
                        if (self->getParent()) {
                            self->applyBlurredBackground(texture);
                        }
                    });
                }
            }, false);
            return true;
        }

        applyBlurredBackground(tex);

        // Aplicar efecto cueva si hay musica de perfil sonando
        if (ProfileMusicManager::get().isPlaying()) {
            ProfileMusicManager::get().applyCaveEffect();
            m_fields->m_hasCaveEffect = true;
        }

        return true;
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

                    // agregar fondo propio si no existe ya
                    if (!child->getChildByID("paimon-comment-bg"_spr)) {
                        auto cs = child->getContentSize();
                        auto bg = paimon::SpriteHelper::createDarkPanel(cs.width, cs.height, 90, 4.f);
                        if (bg) {
                            bg->setPosition({0, 0});
                            bg->setZOrder(-10);
                            bg->setID("paimon-comment-bg"_spr);
                            child->addChild(bg);
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

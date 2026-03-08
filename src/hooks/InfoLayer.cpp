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
#include "../managers/ProfileMusicManager.hpp"

using namespace geode::prelude;
using namespace cocos2d;

// Declarado en ProfilePage.cpp — acceso al cache de texturas de profileimg.
extern CCTexture2D* getProfileImgCachedTexture(int accountID);

class $modify(PaimonInfoLayer, InfoLayer) {
    static void onModify(auto& self) {
        (void)self.setHookPriorityPost("InfoLayer::init", geode::Priority::Late);
    }

    struct Fields {
        Ref<CCClippingNode> m_bgClip = nullptr;
        bool m_hasCaveEffect = false;
    };

    bool init(GJGameLevel* level, GJUserScore* score, GJLevelList* list) {
        if (!InfoLayer::init(level, score, list)) return false;

        // Solo aplicar fondo si viene de un perfil de usuario (score != nullptr)
        if (!score) return true;

        int accountID = score->m_accountID;
        if (accountID <= 0) return true;

        // Intentar obtener la textura del cache de profileimg
        CCTexture2D* tex = getProfileImgCachedTexture(accountID);
        if (!tex) {
            // Si no hay cache, intentar descargar en background
            Ref<InfoLayer> safeRef = this;
            ThumbnailAPI::get().downloadProfileImg(accountID, [safeRef](bool success, CCTexture2D* texture) {
                auto* self = static_cast<PaimonInfoLayer*>(safeRef.data());
                if (!self->getParent()) return;
                if (success && texture) {
                    Loader::get()->queueInMainThread([safeRef, texture]() {
                        auto* self2 = static_cast<PaimonInfoLayer*>(safeRef.data());
                        if (self2->getParent()) {
                            self2->applyBlurredBackground(texture);
                        }
                    });
                }
            }, false);
            return true;
        }

        applyBlurredBackground(tex);

        // Aplicar efecto cueva a la musica del perfil si esta sonando
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

        // Buscar el background del popup por node ID de Geode
        CCSize popupSize = CCSize(440.f, 290.f);
        CCPoint popupCenter = ccp(layerSize.width * 0.5f, layerSize.height * 0.5f);

        if (auto bg = layer->getChildByID("background")) {
            popupSize = bg->getScaledContentSize();
            popupCenter = bg->getPosition();
        } else {
            // fallback: primer CCScale9Sprite hijo directo
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

        // Crear sprite con blur gaussiano multi-paso
        auto blurredSprite = Shaders::createBlurredSprite(tex, imgArea, 7.0f);
        if (!blurredSprite) return;

        blurredSprite->setPosition(ccp(imgArea.width * 0.5f, imgArea.height * 0.5f));

        // Stencil con forma del popup
        auto stencil = CCScale9Sprite::create("GJ_square01.png");
        if (!stencil) return;
        stencil->setContentSize(imgArea);
        stencil->setAnchorPoint(ccp(0.5f, 0.5f));
        stencil->setPosition(ccp(imgArea.width * 0.5f, imgArea.height * 0.5f));

        auto clip = CCClippingNode::create();
        clip->setStencil(stencil);
        clip->setAlphaThreshold(0.05f);
        clip->setContentSize(imgArea);
        clip->setAnchorPoint(ccp(0.5f, 0.5f));
        clip->setPosition(popupCenter);
        clip->setID("paimon-infolayer-bg-clip"_spr);

        clip->addChild(blurredSprite);

        // Overlay oscuro para mejorar legibilidad
        auto dark = CCLayerColor::create(ccc4(0, 0, 0, 120));
        dark->setContentSize(imgArea);
        dark->setAnchorPoint(ccp(0, 0));
        dark->setPosition(ccp(0, 0));
        dark->setID("paimon-infolayer-dark-overlay"_spr);
        clip->addChild(dark);

        // Insertar detras del contenido (zOrder -1)
        layer->addChild(clip, -1);
        m_fields->m_bgClip = clip;

        // Ocultar elementos decorativos (igual que en ProfilePage)
        styleInfoLayerBgs(layer);

        // Tick periodico para mantener estilos
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

                // GJCommentListLayer: opacidad 0 + ocultar bordes y fondos
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

                    // Ocultar fondos internos de CommentCells
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

                        // CCLayerColor directo = fondo de la celda
                        if (typeinfo_cast<CCLayerColor*>(cc)) {
                            cc->setVisible(false);
                        }

                        // CCLayer hijo: solo ocultar CCScale9Sprite de fondo, NO el CCLayer
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

    void keyBackClicked() override {
        restoreMusicEffect();
        InfoLayer::keyBackClicked();
    }

    void onExit() override {
        restoreMusicEffect();
        this->unschedule(schedule_selector(PaimonInfoLayer::tickStyleBgs));
        InfoLayer::onExit();
    }

    void restoreMusicEffect() {
        if (m_fields->m_hasCaveEffect) {
            ProfileMusicManager::get().removeCaveEffect();
            m_fields->m_hasCaveEffect = false;
        }
    }
};

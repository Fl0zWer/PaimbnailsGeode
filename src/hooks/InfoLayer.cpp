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
    struct Fields {
        CCClippingNode* m_bgClip = nullptr;
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
            this->retain();
            ThumbnailAPI::get().downloadProfileImg(accountID, [this, accountID](bool success, CCTexture2D* texture) {
                if (!this->getParent()) { this->release(); return; }
                if (success && texture) {
                    Loader::get()->queueInMainThread([this, texture]() {
                        if (this->getParent()) {
                            this->applyBlurredBackground(texture);
                        }
                        this->release();
                    });
                } else {
                    this->release();
                }
            }, false);
            return true;
        }

        applyBlurredBackground(tex);

        // Aplicar efecto cueva a la música del perfil si está sonando
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

        // Insertar detrás del contenido (zOrder -1)
        layer->addChild(clip, -1);
        m_fields->m_bgClip = clip;

        // Ocultar elementos decorativos (igual que en ProfilePage)
        styleInfoLayerBgs(layer);

        // Tick periódico para mantener estilos
        this->schedule(schedule_selector(PaimonInfoLayer::tickStyleBgs), 0.15f);
    }

    void styleInfoLayerBgs(CCNode* root) {
        if (!root) return;

        std::function<void(CCNode*)> walk = [&](CCNode* parent) {
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

                walk(child);
            }
        };

        walk(root);
    }

    void hideCommentCellBgs(CCNode* listNode) {
        if (!listNode) return;

        std::function<void(CCNode*)> findCells = [&](CCNode* node) {
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

                findCells(child);
            }
        };

        findCells(listNode);
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

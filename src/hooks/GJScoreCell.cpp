#include <Geode/modify/GJScoreCell.hpp>
#include <Geode/binding/GJUserScore.hpp>
#include <Geode/binding/FLAlertLayer.hpp>
#include <Geode/binding/GameManager.hpp>
#include <Geode/loader/Mod.hpp>
#include <Geode/utils/cocos.hpp>
#include <algorithm>

#include "../managers/ProfileThumbs.hpp"
#include "../managers/ThumbsRegistry.hpp"
#include "../managers/LocalThumbs.hpp"
#include <Geode/ui/Notification.hpp>
#include "../utils/PaimonButtonHighlighter.hpp"
#include "../managers/ThumbnailAPI.hpp"
#include "../utils/AnimatedGIFSprite.hpp"

using namespace geode::prelude;
#include "../utils/Shaders.hpp"

using namespace cocos2d;
using namespace Shaders;

// sprite helper pa blur de fondo GIF en vivo
class BlurSprite : public CCSprite {
public:
    float m_intensity = 0.0f;
    CCSize m_texSize = {0,0};
    AnimatedGIFSprite* m_syncTarget = nullptr;
    
    static BlurSprite* createWithTexture(CCTexture2D* tex) {
        auto ret = new BlurSprite();
        if (ret && ret->initWithTexture(tex)) {
            ret->autorelease();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }

    void update(float dt) override {
        if (m_syncTarget) {
            auto frame = m_syncTarget->displayFrame();
            if (frame) {
                // siempre poner frame pa sync; comparar igualdad con distintos frames es delicado
                this->setDisplayFrame(frame);
            }
        }
        CCSprite::update(dt);
    }
    
    void draw() override {
        if (getShaderProgram()) {
            getShaderProgram()->use();
            getShaderProgram()->setUniformsForBuiltins();
            
            GLint intensityLoc = getShaderProgram()->getUniformLocationForName("u_intensity");
            if (intensityLoc != -1) {
                getShaderProgram()->setUniformLocationWith1f(intensityLoc, m_intensity);
            }
            
            if (m_texSize.width == 0 && getTexture()) {
                m_texSize = getTexture()->getContentSizeInPixels();
            }
            float w = m_texSize.width > 0 ? m_texSize.width : 1.0f;
            float h = m_texSize.height > 0 ? m_texSize.height : 1.0f;
            
            GLint sizeLoc = getShaderProgram()->getUniformLocationForName("u_texSize");
            if (sizeLoc != -1) {
                getShaderProgram()->setUniformLocationWith2f(sizeLoc, w, h);
            }
            GLint screenLoc = getShaderProgram()->getUniformLocationForName("u_screenSize");
            if (screenLoc != -1) {
                getShaderProgram()->setUniformLocationWith2f(screenLoc, w, h);
            }
        }
        CCSprite::draw();
    }
};



// helpers pa los efectos premium
namespace {
    // mete partículas premium pegadas al nombre
    void addPremiumParticlesToUsername(CCNode* parent, const CCPoint& namePos, float nameWidth) {
        // crear 3 particulas pequeñas alrededor del nombre
        for (int i = 0; i < 3; ++i) {
            auto particle = CCSprite::createWithSpriteFrameName("star_small01_001.png");
            if (!particle) continue;
            
            float offsetX = (i - 1) * (nameWidth / 2.5f);
            float offsetY = 8.0f + (rand() % 5);
            particle->setPosition({namePos.x + offsetX, namePos.y + offsetY});
            particle->setScale(0.25f);
            particle->setOpacity(150);
            particle->setColor({255, 215, 0}); // dorado
            parent->addChild(particle, 200);
            
            // anim flotando chill
            float duration = 1.0f + (rand() % 50) / 100.0f;
            particle->runAction(CCRepeatForever::create(CCSequence::create(
                CCSpawn::create(
                    CCMoveBy::create(duration, {0, 5}),
                    CCSequence::create(
                        CCFadeTo::create(duration / 2, 220),
                        CCFadeTo::create(duration / 2, 120),
                        nullptr
                    ),
                    nullptr
                ),
                CCPlace::create({namePos.x + offsetX, namePos.y + offsetY}),
                nullptr
            )));
        }
    }
}

// cache estático pa mover botones sin gastar de más
namespace {
    struct ButtonMoveCache {
        bool initialized = false;
        float buttonOffset = 30.f;
        std::unordered_set<int> processedCells; // ids de celdas ya tocadas
        
        void reset() {
            initialized = false;
            processedCells.clear();
        }
    };
    
    ButtonMoveCache g_buttonCache;
}

class $modify(PaimonGJScoreCell, GJScoreCell) {
    struct Fields {
        CCClippingNode* m_profileClip = nullptr;
        CCLayerColor* m_profileSeparator = nullptr;
        CCNode* m_profileBg = nullptr;
        CCLayerColor* m_darkOverlay = nullptr;
        bool m_buttonsMoved = false; // pa no andar moviendo botones mil veces
        CCSprite* m_loadingSpinner = nullptr;
        bool m_isBeingDestroyed = false; // pa no tocar celdas que ya se mueren
    };
    
    void showLoadingSpinner() {
        auto f = m_fields.self();
        
        // quita el spinner viejo si ya había
        if (f->m_loadingSpinner) {
            f->m_loadingSpinner->removeFromParent();
            f->m_loadingSpinner = nullptr;
        }
        
        // crea el iconito de carga
        auto spinner = CCSprite::create("loadingCircle.png");
        if (!spinner) {
            spinner = CCSprite::createWithSpriteFrameName("loadingCircle.png");
        }
        if (!spinner) {
            // último recurso: cuadro gris cutre
            spinner = CCSprite::create();
            auto circle = CCLayerColor::create({100, 100, 100, 200});
            circle->setContentSize({40, 40});
            spinner->addChild(circle);
        }
        
        spinner->setScale(0.25f);
        spinner->setOpacity(200);
        
        // lo pongo a la derecha donde iría la mini
        auto cs = this->getContentSize();
        if (cs.width <= 1.f || cs.height <= 1.f) {
            cs.width = this->m_width;
            cs.height = this->m_height;
        }
        spinner->setPosition({35.f, cs.height / 2.f + 20.f});
        spinner->setZOrder(999);
        
        try {
            spinner->setID("paimon-loading-spinner"_spr);
        } catch (...) {}
        
        this->addChild(spinner);
        f->m_loadingSpinner = spinner;
        
        // lo pongo a girar sin parar
        auto rotateAction = CCRepeatForever::create(
            CCRotateBy::create(1.0f, 360.0f)
        );
        spinner->runAction(rotateAction);
    }
    
    void hideLoadingSpinner() {
        auto f = m_fields.self();
        if (f->m_loadingSpinner) {
            f->m_loadingSpinner->stopAllActions();
            f->m_loadingSpinner->removeFromParent();
            f->m_loadingSpinner = nullptr;
        }
    }





    // mover CCLayerColor del juego detras de nuestro fondo
    void pushGameColorLayersBehind(CCNode* node) {
        if (!node) return;
        // no toques los nodos del mod (id empieza con "paimon-")
        std::string id = node->getID();
        if (!id.empty() && id.find("paimon-") == 0) return;

        bool isBackground = false;
        if (geode::cast::typeinfo_cast<CCLayerColor*>(node) != nullptr) isBackground = true;
        else if (geode::cast::typeinfo_cast<CCScale9Sprite*>(node) != nullptr) isBackground = true;

        if (isBackground) {
            // empuja solo si aún no está al fondo
            if (node->getZOrder() > -20) {
                if (auto parent = node->getParent()) parent->reorderChild(node, -20);
                else node->setZOrder(-20);
            }
        }
        // recursivo pa limpiar todo el árbol
        auto children = CCArrayExt<CCNode*>(node->getChildren());
        for (auto* ch : children) pushGameColorLayersBehind(ch);
    }

    void addOrUpdateProfileThumb(CCTexture2D* texture) {
        // dejo textura null si hay gifKey, se apaña con eso
        
        try {
            // check crítico: la celda tiene que tener parent
            if (!this->getParent()) {
                log::warn("[GJScoreCell] Cell has no parent, skipping addOrUpdateProfileThumb");
                return;
            }
            
            log::info("[GJScoreCell] addOrUpdateProfileThumb called");

            auto f = m_fields.self();
            if (!f) {
                log::error("[GJScoreCell] Fields are null in addOrUpdateProfileThumb");
                return;
            }
            
            // mira si la celda ya se está destruyendo
            if (f->m_isBeingDestroyed) {
                log::debug("[GJScoreCell] Cell marked as destroyed, skipping thumbnail update");
                return;
            }
            
            log::debug("[GJScoreCell] Starting profile thumbnail update");
            
            // limpieza agresiva pa que no se apilen cosas raras
            if (auto children = this->getChildren()) {
                for (int i = children->count() - 1; i >= 0; i--) {
                    if (auto node = typeinfo_cast<CCNode*>(children->objectAtIndex(i))) {
                        std::string id = node->getID();
                        if (id == "paimon-profile-bg"_spr || 
                            id == "paimon-profile-clip"_spr || 
                            id == "paimon-profile-thumb"_spr ||
                            id == "paimon-score-bg-clipper"_spr ||
                            id == "paimon-profile-separator"_spr) {
                            node->removeFromParent();
                        }
                    }
                }
            }
            
            f->m_profileClip = nullptr;
            f->m_profileSeparator = nullptr;
            f->m_profileBg = nullptr;
            f->m_darkOverlay = nullptr;

            // geometría base según tamaño de la celda
            auto cs = this->getContentSize();
            if (cs.width <= 0 || cs.height <= 0) {
                log::error("[GJScoreCell] Invalid cell content size: {}x{}", cs.width, cs.height);
                return;
            }
            if (cs.width <= 1.f || cs.height <= 1.f) {
                cs.width = this->m_width;
                cs.height = this->m_height;
            }

            // --- lógica del fondo (gradiente vs mini blur) ---
            std::string bgType = "gradient";
            float blurIntensity = 3.0f;
            float darkness = 0.2f;
            bool useGradient = false;
            ccColor3B colorA = {255,255,255};
            ccColor3B colorB = {255,255,255};
            std::string gifKey = "";

            bool isCurrentUser = false;
            if (this->m_score) isCurrentUser = this->m_score->isCurrentUser();
            
            int accountID = (this->m_score) ? this->m_score->m_accountID : 0;
            auto config = ProfileThumbs::get().getProfileConfig(accountID);

            // gifKey siempre desde config (inyectado desde cache)
            gifKey = config.gifKey;

            if (isCurrentUser) {
                // usuario actual: siempre tira de config local (SavedValue)
                try { bgType = Mod::get()->getSavedValue<std::string>("scorecell-background-type", "thumbnail"); } catch (...) {}
                try { blurIntensity = Mod::get()->getSavedValue<float>("scorecell-background-blur", 3.0f); } catch (...) {}
                try { darkness = Mod::get()->getSavedValue<float>("scorecell-background-darkness", 0.2f); } catch (...) {}
            } else {
                // otros usuarios: intento config desde cache
                if (config.hasConfig) {
                    bgType = config.backgroundType;
                    blurIntensity = config.blurIntensity;
                    darkness = config.darkness;
                    useGradient = config.useGradient;
                    colorA = config.colorA;
                    colorB = config.colorB;
                } else {
                    // si no hay config, tiro de defaults (nada de config local pa otros)
                    bgType = "thumbnail"; // por defecto miniatura blur
                    // mantener otros defaults
                }
            }
            
            // validación básica
            if (!texture && gifKey.empty()) {
                log::error("[GJScoreCell] No texture and no GIF key available for account {}", accountID);
                return;
            }

            // fuerzo modo miniatura si hay textura/gif y el config dice "gradient" por defecto
            if (bgType == "gradient" && (texture || !gifKey.empty())) {
                bgType = "thumbnail";
            }

            BlurSprite* pendingBlurSprite = nullptr;

            if (bgType == "none") {
                // no hago nada, dejo el fondo del juego
            }
            else if (bgType == "thumbnail") {
                // creo fondo con blur
                CCSize targetSize = cs;
                targetSize.width = std::max(targetSize.width, 512.f);
                targetSize.height = std::max(targetSize.height, 256.f);

                CCNode* bgNode = nullptr;

                // primero intento fondo con GIF
                if (!gifKey.empty()) {
                    auto bgSprite = AnimatedGIFSprite::createFromCache(gifKey);
                    if (bgSprite) {
                        auto tex = bgSprite->getTexture();
                        if (tex) {
                            // pa GIFs: BlurSprite con blur en tiempo real
                            // se actualiza cada frame
                            auto blurSprite = BlurSprite::createWithTexture(tex);
                            if (blurSprite) {
                                // escalo pa cubrir toda el área
                                float scaleX = targetSize.width / bgSprite->getContentSize().width;
                                float scaleY = targetSize.height / bgSprite->getContentSize().height;
                                float scale = std::max(scaleX, scaleY);

                                blurSprite->setScale(scale);
                                blurSprite->setAnchorPoint({0.5f, 0.5f});
                                blurSprite->setPosition(targetSize * 0.5f);
                                float norm = (blurIntensity - 1.0f) / 9.0f;
                                blurSprite->m_intensity = std::min(1.7f, norm * 2.5f);
                                blurSprite->m_texSize = tex->getContentSizeInPixels();

                                // mismo shader Dual Kawase que uso en LevelInfoLayer
                                auto shader = Shaders::getBlurSinglePassShader();
                                if (shader) {
                                    blurSprite->setShaderProgram(shader);
                                }

                                blurSprite->scheduleUpdate();
                                bgNode = blurSprite;
                                pendingBlurSprite = blurSprite;
                            } else {
                                // fallback: blur multi-paso ya prehecho (calidad similar)
                                float stronger = std::min(10.0f, blurIntensity + 3.0f);
                                auto staticBg = Shaders::createBlurredSprite(tex, targetSize, stronger);
                                if (staticBg) {
                                    staticBg->setPosition(targetSize * 0.5f);
                                    bgNode = staticBg;
                                } else {
                                    auto plain = CCSprite::createWithTexture(tex);
                                    if (plain) {
                                        float scaleX = targetSize.width / tex->getContentSize().width;
                                        float scaleY = targetSize.height / tex->getContentSize().height;
                                        plain->setScale(std::max(scaleX, scaleY));
                                        plain->setPosition(targetSize * 0.5f);
                                        bgNode = plain;
                                    }
                                }
                            }
                        }
                        
                        if (bgNode) {
                            try { bgNode->setID("paimon-bg-sprite"_spr); } catch(...) {}
                        }
                    }
                }
                
                // fallback a fondo con textura
                if (!bgNode && texture) {
                    // imagen estática: blur multi-paso fino
                    CCSize blurTargetSize = cs;
                    blurTargetSize.width = std::max(blurTargetSize.width, 512.f);
                    blurTargetSize.height = std::max(blurTargetSize.height, 256.f);

                    float stronger = std::min(10.0f, blurIntensity + 3.0f); // le doy un pelín más de blur
                    auto blurredBg = Shaders::createBlurredSprite(texture, blurTargetSize, stronger);
                    if (blurredBg) {
                        blurredBg->setPosition(blurTargetSize * 0.5f);
                        bgNode = blurredBg;
                    } else {
                        // último fallback: textura normal con shader
                        auto tempSprite = CCSprite::createWithTexture(texture);
                        float scaleX = blurTargetSize.width / texture->getContentSize().width;
                        float scaleY = blurTargetSize.height / texture->getContentSize().height;
                        float scale = std::max(scaleX, scaleY);

                        tempSprite->setScale(scale);
                        tempSprite->setPosition(blurTargetSize * 0.5f);

                        auto shader = Shaders::getBlurCellShader();
                        if (shader) {
                            tempSprite->setShaderProgram(shader);
                        }
                        bgNode = tempSprite;
                    }
                }

                if (bgNode) {
                    // creo el Clipper pa el fondo
                    auto stencil = CCDrawNode::create();
                    CCPoint rect[4];
                    rect[0] = ccp(0, 0);
                    rect[1] = ccp(cs.width, 0);
                    rect[2] = ccp(cs.width, cs.height);
                    rect[3] = ccp(0, cs.height);
                    ccColor4F white = {1, 1, 1, 1};
                    stencil->drawPolygon(rect, 4, white, 0, white);
                    
                    auto clipper = CCClippingNode::create(stencil);
                    clipper->setContentSize(cs);
                    clipper->setPosition({0,0});
                    clipper->setZOrder(-2); // bien al fondo
                    try { clipper->setID("paimon-score-bg-clipper"_spr); } catch (...) {}

                    // escalo bgNode pa llenar la celda
                    // el blur viene a targetSize, aquí lo ajusto al tamaño real
                    CCSize bgSize = bgNode->getContentSize();
                    if (bgSize.width > 0 && bgSize.height > 0) {
                        float scaleToFitX = cs.width / bgSize.width;
                        float scaleToFitY = cs.height / bgSize.height;
                        float finalScale = std::max(scaleToFitX, scaleToFitY);
                        bgNode->setScale(finalScale);
                    }
                    bgNode->setAnchorPoint({0.5f, 0.5f});
                    bgNode->setPosition(cs / 2);
                    
                    clipper->addChild(bgNode);
                    this->addChild(clipper);
                    f->m_profileBg = clipper;

                    // le meto una capa oscura encima
                    if (darkness > 0.0f) {
                        auto overlay = CCLayerColor::create({0, 0, 0, static_cast<GLubyte>(darkness * 255)});
                        overlay->setContentSize(cs);
                        overlay->setPosition({0, 0});
                        overlay->setZOrder(-1); 
                        this->addChild(overlay);
                        f->m_darkOverlay = overlay;
                    }

                    // me aseguro de que el fondo original se quede atrás
                    pushGameColorLayersBehind(this);
                }
            }

            // --- lógica del sprite principal ---
            CCNode* mainNode = nullptr;
            float contentW = 0, contentH = 0;

            // primero pruebo con GIF
            if (!gifKey.empty()) {
                log::debug("[GJScoreCell] Trying to create GIF sprite from cache key: {}", gifKey);

                // miro si el GIF ya está en cache
                if (AnimatedGIFSprite::isCached(gifKey)) {
                    log::debug("[GJScoreCell] GIF is cached, creating sprite...");
                    auto gifSprite = AnimatedGIFSprite::createFromCache(gifKey);
                    if (gifSprite) {
                        mainNode = gifSprite;
                        contentW = gifSprite->getContentSize().width;
                        contentH = gifSprite->getContentSize().height;

                        // aseguro que la animación esté corriendo
                        gifSprite->play();

                        try { gifSprite->setID("paimon-profile-thumb-gif"_spr); } catch(...) {}
                        log::debug("[GJScoreCell] Created GIF sprite from key: {}, size: {}x{}, frames: {}",
                            gifKey, contentW, contentH, gifSprite->getFrameCount());

                        // engancho el fondo al GIF pa que estén sync
                        if (pendingBlurSprite) {
                            pendingBlurSprite->m_syncTarget = gifSprite;
                        }
                    } else {
                        log::warn("[GJScoreCell] createFromCache returned null for key: {}", gifKey);
                    }
                } else {
                    log::warn("[GJScoreCell] GIF not in cache for key: {}", gifKey);
                }
            }
            
            // si no hay GIF, tiro de textura
            if (!mainNode && texture) {
                auto sprite = CCSprite::createWithTexture(texture);
                if (sprite) {
                    mainNode = sprite;
                    contentW = sprite->getContentWidth();
                    contentH = sprite->getContentHeight();
                    try { sprite->setID("paimon-profile-thumb"_spr); } catch(...) {}
                }
            }

            if (!mainNode) {
                log::error("[GJScoreCell] Failed to create main sprite");
                return;
            }

        // aseguro que la celda siga existiendo y tenga parent antes de seguir
        if (!this->getParent()) {
            log::warn("[GJScoreCell] Cell was destroyed before thumbnail could be added");
            return;
        }


        
        log::debug("[GJScoreCell] Cell size: {}x{}", cs.width, cs.height);

            // escalo solo en ancho: altura fija y ancho va con factor
            float factor = 0.80f;
            
            if (isCurrentUser) {
                try { factor = Mod::get()->getSavedValue<float>("profile-thumb-width", 0.6f); } catch (...) {}
            } else {
                // resto de usuarios: tiro de config en cache
                int accountID = (this->m_score) ? this->m_score->m_accountID : 0;
                auto config = ProfileThumbs::get().getProfileConfig(accountID);
                if (config.hasConfig) {
                    factor = config.widthFactor;
                } else {
                    // si no hay config, me quedo con el default
                    factor = 0.60f; 
                }
            }
            
            factor = std::max(0.30f, std::min(0.95f, factor));
            float desiredWidth = cs.width * factor;

            float scaleY = cs.height / contentH;
            float scaleX = desiredWidth / contentW;

            mainNode->setScaleY(scaleY);
            mainNode->setScaleX(scaleX);

        // recorte en ángulo que imita el corte del lado derecho
    constexpr float angle = 18.f;
        CCSize scaledSize{ desiredWidth, contentH * scaleY };
        auto mask = CCLayerColor::create({255,255,255});
        mask->setContentSize(scaledSize);
        mask->setAnchorPoint({1,0});
        mask->setSkewX(angle);

        auto clip = CCClippingNode::create();
        clip->setStencil(mask);
        clip->setContentSize(scaledSize);
        clip->setAnchorPoint({1,0});
        // pegado al borde derecho con un pelín de offset pa que no se vea feo
        clip->setPosition({ cs.width, 0.3f });
        try {
            clip->setID("paimon-profile-clip"_spr);
        } catch (...) {}
    // lo dejo detrás de textos/iconos pa no tapar stats
    clip->setZOrder(-1);

        mainNode->setPosition(clip->getContentSize() * 0.5f);
        clip->addChild(mainNode);
        
        // aquí antes había otro check defensivo, pero ya no hace falta
        /*
        if (!this->getParent()) {
            log::warn("[GJScoreCell] Cell destroyed before clipping node could be added");
            clip->removeFromParent();
            return;
        }
        */
        
        this->addChild(clip);
        f->m_profileClip = clip;
        
        // aplicar efectos premium si el usuario tiene banners premium
        bool isPremiumUser = false;
        /*
        if (auto score = this->m_score) {
             // ... logic removed ...
        }
        */

        // borde alrededor de la mini (dorado premium o negro chill)
        float borderThickness = 2.f;
        ccColor4B borderColor = isPremiumUser ? ccc4(255, 215, 0, 200) : ccc4(0, 0, 0, 120);
        
        // borde de arriba
        auto topBorder = CCLayerColor::create(borderColor);
        topBorder->setContentSize({scaledSize.width, borderThickness});
        topBorder->setAnchorPoint({1,0});
        topBorder->setSkewX(angle);
        topBorder->setPosition({ cs.width, 0.3f + scaledSize.height });
        topBorder->setZOrder(-1);
        try {
            topBorder->setID("paimon-profile-border-top"_spr);
        } catch (...) {}
        this->addChild(topBorder);
        
        // animación de brillo solo si es premium
        if (isPremiumUser) {
            topBorder->runAction(CCRepeatForever::create(CCSequence::create(
                CCFadeTo::create(0.8f, 255),
                CCFadeTo::create(0.8f, 180),
                nullptr
            )));
        }
        
        // borde de abajo
        auto bottomBorder = CCLayerColor::create(borderColor);
        bottomBorder->setContentSize({scaledSize.width, borderThickness});
        bottomBorder->setAnchorPoint({1,0});
        bottomBorder->setSkewX(angle);
        bottomBorder->setPosition({ cs.width, 0.3f - borderThickness });
        bottomBorder->setZOrder(-1);
        try {
            bottomBorder->setID("paimon-profile-border-bottom"_spr);
        } catch (...) {}
        this->addChild(bottomBorder);
        
        if (isPremiumUser) {
            bottomBorder->runAction(CCRepeatForever::create(CCSequence::create(
                CCFadeTo::create(0.8f, 255),
                CCFadeTo::create(0.8f, 180),
                nullptr
            )));
        }
        
        // borde derecho
        auto rightBorder = CCLayerColor::create(borderColor);
        rightBorder->setContentSize({borderThickness, scaledSize.height + borderThickness * 2});
        rightBorder->setAnchorPoint({1,0});
        rightBorder->setPosition({ cs.width, 0.3f - borderThickness });
        rightBorder->setZOrder(-1);
        try {
            rightBorder->setID("paimon-profile-border-right"_spr);
        } catch (...) {}
        this->addChild(rightBorder);
        
        if (isPremiumUser) {
            rightBorder->runAction(CCRepeatForever::create(CCSequence::create(
                CCFadeTo::create(0.8f, 255),
                CCFadeTo::create(0.8f, 180),
                nullptr
            )));
        }

    // separador detrás de la imagen (estilo fijo)
    auto sep = CCLayerColor::create(ccc4(0, 0, 0, 50));
    sep->setScaleX(0.45f);
        sep->ignoreAnchorPointForPosition(false);
        sep->setSkewX(angle * 2);
        sep->setContentSize(scaledSize);
        sep->setAnchorPoint({1,0});
        sep->setPosition({ cs.width - sep->getContentSize().width / 2 - 16.f, 0.3f });
    sep->setZOrder(-2);
        try {
            sep->setID("paimon-profile-separator"_spr);
        } catch (...) {}
        this->addChild(sep);
        f->m_profileSeparator = sep;

        // el fondo ya lo metí más arriba
        log::debug("[GJScoreCell] Profile thumbnail added successfully");
        } catch (std::exception& e) {
            log::error("[GJScoreCell] Exception in addOrUpdateProfileThumb: {}", e.what());
        } catch (...) {
            log::error("[GJScoreCell] Unknown exception in addOrUpdateProfileThumb");
        }
    }



    $override void loadFromScore(GJUserScore* score) {
        GJScoreCell::loadFromScore(score);
        
        // empujar capas de color del juego atras pa que se vea el gradiente
        try { pushGameColorLayersBehind(this); } catch (...) {}
        
        try {
            if (!score) return;

            int accountID = score->m_accountID;
            if (accountID <= 0) return;

            bool isCurrent = score->isCurrentUser();
            bool loadedLocally = false;
            
            // usuario actual: antes tiraba del perfil local primero
            if (isCurrent) {
                // ahora paso del disco y uso siempre cache/descarga
                // así se ve igual que en BannerConfigPopup y sin thumbs viejas
                loadedLocally = false;
            }

            if (!loadedLocally) {
                // otros usuarios (o el mismo sin local): primero cache y solo descargo si toca
                std::string username = score->m_userName;
                if (username.empty()) {
                    log::warn("[GJScoreCell] Username empty for account {}", accountID);
                    return;
                }
                
                // comprobar cache primero
                auto cachedProfile = ProfileThumbs::get().getCachedProfile(accountID);
                if (cachedProfile && (cachedProfile->texture || !cachedProfile->gifKey.empty())) {
                    log::debug("[GJScoreCell] Found cached profile for account {}", accountID);
                    // cargo desde cache de forma asíncrona
                    Loader::get()->queueInMainThread([this, accountID]() {
                        try {
                            auto mod = Mod::get();
                            auto oldWidth = mod->getSavedValue<float>("profile-thumb-width", 0.6f);
                            
                            auto cached = ProfileThumbs::get().getCachedProfile(accountID);
                            if (cached) {
                                addOrUpdateProfileThumb(cached->texture);
                            } else {
                                log::warn("[GJScoreCell] Cache entry disappeared for account {}", accountID);
                            }
                            
                            // devuelvo el valor que tenía antes
                            mod->setSavedValue("profile-thumb-width", oldWidth);
                        } catch (...) {}
                    });
                    return;
                }
                
                log::debug("[GJScoreCell] No cache for account {}, downloading...", accountID);
                
                // no está en cache: toca descargar del server
                log::debug("[GJScoreCell] Profile not in cache for user: {} - Downloading...", username);
                
                // muestro el spinner solo si está habilitado
                bool enableSpinners = true;
                // try { // si quieres setting lo descomentas
                //     enableSpinners = Mod::get()->getSettingValue<bool>("enable-loading-spinners");
                // } catch (...) {}
                
                if (enableSpinners) {
                    showLoadingSpinner();
                }
                
                // retengo la celda pa que no crashee si se destruye en mitad de la descarga
                this->retain();
                
                // uso queueLoad en vez de bajar directo
                ProfileThumbs::get().queueLoad(accountID, username, [this, accountID, enableSpinners](bool success, CCTexture2D* texture) {
                    if (!success || !texture) {
                        if (enableSpinners) this->hideLoadingSpinner();
                        log::warn("[GJScoreCell] Failed to download profile for account {}", accountID);
                        this->release();
                        return;
                    }

                    // retener textura pa que no se autorelease en la siguiente llamada async
                    texture->retain();

                    // descargar config
                    ThumbnailAPI::get().downloadProfileConfig(accountID, [this, accountID, texture, enableSpinners](bool success2, const ProfileConfig& config) {
                        try {
                            if (enableSpinners) this->hideLoadingSpinner();
                            
                        // guardo en cache
                            ProfileThumbs::get().cacheProfile(accountID, texture, {255,255,255}, {255,255,255}, 0.5f);
                            if (success2) {
                                ProfileThumbs::get().cacheProfileConfig(accountID, config);
                            }
                            
                            // aplico la textura al final
                            this->addOrUpdateProfileThumb(texture);
                        } catch (...) {
                            log::error("[GJScoreCell] Error handling profile download callback");
                        }
                        
                        // libero la textura después de usarla
                        texture->release();
                        this->release();
                    });
                });
            }
        } catch (...) {}
        
        // muevo el botón de perfil del jugador (tirando del cache ese)
        auto f = m_fields.self();
        if (!f->m_buttonsMoved) {
            f->m_buttonsMoved = true; // marcado como ya procesado
            
            try {
                // init del cache solo una vez
                if (!g_buttonCache.initialized) {
                        g_buttonCache.buttonOffset = 0.0f;
                    g_buttonCache.initialized = true;
                    log::debug("[GJScoreCell] Button cache initialized with offset: {}", g_buttonCache.buttonOffset);
                }
                
                // si el offset es 0, ni me molesto
                if (g_buttonCache.buttonOffset <= 0.01f) {
                    return;
                }
                
                // busco y muevo botones solo entre hijos directos
                auto children = this->getChildren();
                if (!children) return;
                
                bool foundButton = false;
                
                // limito la búsqueda a los primeros 10 nodos (los menús suelen estar al inicio)
                int searchCount = 0;

                for (auto* child : CCArrayExt<CCNode*>(children)) {
                    if (foundButton || searchCount >= 10) break;
                    searchCount++;

                    auto menu = typeinfo_cast<CCMenu*>(child);
                    if (!menu) continue;

                    auto menuChildren = menu->getChildren();
                    if (!menuChildren) continue;
                    
                    // solo miro los primeros 5 items del menú
                    int menuSearchCount = 0;

                    for (auto* menuChild : CCArrayExt<CCNode*>(menuChildren)) {
                        if (foundButton || menuSearchCount >= 5) break;
                        menuSearchCount++;

                        auto btn = typeinfo_cast<CCMenuItemSpriteExtra*>(menuChild);
                        if (!btn) continue;
                        
                        auto btnID = btn->getID();
                        
                        // ignoro botones del mod (solo miro el prefijo)
                        std::string btnIDStr = btnID;
                        if (btnIDStr.empty() || btnIDStr.compare(0, 7, "paimon-") != 0) {
                            auto currentPos = btn->getPosition();
                            
                            // lo muevo solo si está en una posición razonable
                            if (currentPos.x > 50.f && currentPos.x < 400.f) {
                                btn->setPosition({currentPos.x - g_buttonCache.buttonOffset, currentPos.y});
                                foundButton = true;
                                log::debug("[GJScoreCell] Moved button: {}x{} -> {}x{}", 
                                         currentPos.x, currentPos.y, 
                                         currentPos.x - g_buttonCache.buttonOffset, currentPos.y);
                                break;
                            }
                        }
                    }
                }
            } catch (std::exception& e) {
                log::error("[GJScoreCell] Exception moving button: {}", e.what());
            } catch (...) {
                // pillo cualquier excepción pa que no crashee
            }
        }
    }

    // hook de draw quitado pa evitar crashes feos
};



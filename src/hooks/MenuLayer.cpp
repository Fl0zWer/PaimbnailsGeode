#include <Geode/modify/MenuLayer.hpp>
#include <Geode/utils/cocos.hpp>
#include <Geode/utils/file.hpp>
#include "../managers/PetManager.hpp"
#include "../managers/TransitionManager.hpp"
#include "../managers/LocalThumbs.hpp"
#include "../layers/VerificationCenterLayer.hpp"
#include "../layers/PaiConfigLayer.hpp"
#include "../managers/LayerBackgroundManager.hpp"
#include "../utils/AnimatedGIFSprite.hpp"
#include "../utils/DominantColors.hpp"
#include "../utils/ImageLoadHelper.hpp"
#include "../utils/ShapeStencil.hpp"
#include "../managers/ProfilePicCustomizer.hpp"
#include "../utils/Shaders.hpp"
#include <random>
#include <filesystem>

using namespace geode::prelude;

// declarada en main.cpp — inicializacion diferida del mod
extern void PaimonOnModLoaded();

// declarada en PetHook.cpp — registra el ticker del pet con el scheduler
extern void initPetTicker();

// ── Helper: construye el nodo clip+container+borde para la foto de perfil ──
// Centraliza la logica compartida entre la rama GIF y la estatica,
// eliminando ~50 lineas de codigo duplicado.
static CCNode* buildProfileClipContainer(
    CCNode* imageNode,
    std::string const& shapeName,
    float targetSize,
    ProfilePicConfig const& picCfg
) {
    if (!imageNode) return nullptr;

    auto stencil = createShapeStencil(shapeName, targetSize);
    if (!stencil) stencil = createShapeStencil("circle", targetSize);
    if (!stencil) return nullptr;

    auto clipper = CCClippingNode::create();
    clipper->setStencil(stencil);
    clipper->setAlphaThreshold(-1.0f);
    clipper->setContentSize({targetSize, targetSize});
    clipper->setID("paimon-profile-clipper"_spr);

    float scaleX = targetSize / imageNode->getContentWidth();
    float scaleY = targetSize / imageNode->getContentHeight();
    float scale = std::max(scaleX, scaleY);

    imageNode->setScale(scale);
    imageNode->setPosition({targetSize / 2, targetSize / 2});
    imageNode->setAnchorPoint({0.5f, 0.5f});
    imageNode->ignoreAnchorPointForPosition(false);

    clipper->addChild(imageNode);

    auto container = CCNode::create();
    container->setContentSize({targetSize, targetSize});
    container->setAnchorPoint({0.5f, 0.5f});
    container->ignoreAnchorPointForPosition(false);
    container->setID("paimon-profile-container"_spr);
    container->addChild(clipper);

    if (picCfg.frameEnabled) {
        float borderSize = targetSize + picCfg.frame.thickness * 2;
        auto border = createShapeBorder(shapeName, borderSize,
            picCfg.frame.thickness, picCfg.frame.color,
            static_cast<GLubyte>(picCfg.frame.opacity));
        if (border) {
            border->setAnchorPoint({0.5f, 0.5f});
            border->setPosition({targetSize / 2, targetSize / 2});
            container->addChild(border, -1);
        }
    }

    return container;
}

class $modify(PaimonMenuLayer, MenuLayer) {
    static void onModify(auto& self) {
        // Late = ejecutar despues de otros mods (NodeIDs, etc.)
        // para que los IDs de nodos ya esten asignados cuando anadimos botones
        (void)self.setHookPriorityPost("MenuLayer::init", geode::Priority::Late);
    }

    struct Fields {
        // Ref<> para seguridad de memoria — evita dangling pointers
        // si el nodo se elimina externamente (ej. al recargar background)
        Ref<CCSprite> m_bgSprite = nullptr;
        Ref<CCLayerColor> m_bgOverlay = nullptr;
        bool m_adaptiveColors = false;
    };

    // helper para aplicar los colores adaptativos
    void applyAdaptiveColor(ccColor3B color) {
        auto tintNode = [color](CCNode* node) {
             if (!node) return;
             if (auto btn = typeinfo_cast<ButtonSprite*>(node)) {
                 btn->setColor(color);
             } 
             else if (auto spr = typeinfo_cast<CCSprite*>(node)) {
                 spr->setColor(color);
             }
             else if (auto lbl = typeinfo_cast<CCLabelBMFont*>(node)) {
                 lbl->setColor(color);
             }
        };

        // iterar menus conocidos y tintear sus hijos
        static char const* menuIDs[] = {
            "main-menu", "profile-menu", "right-side-menu"
        };
        for (auto const& menuID : menuIDs) {
            if (auto menu = this->getChildByID(menuID)) {
                for (auto* btn : CCArrayExt<CCMenuItem*>(menu->getChildren())) {
                    if (!btn) continue;
                    for (auto* kid : CCArrayExt<CCNode*>(btn->getChildren())) {
                        tintNode(kid);
                    }
                }
            }
        }

        if (auto lbl = typeinfo_cast<CCLabelBMFont*>(this->getChildByID("player-username"))) {
            lbl->setColor(color);
        }
    } 

    $override
    bool init() {
        if (!MenuLayer::init()) {
            return false;
        }

        // inicializacion diferida del mod (una sola vez)
        static bool s_paimonLoaded = false;
        if (!s_paimonLoaded) {
            s_paimonLoaded = true;
            log::info("[PaimonThumbnails] Invoking delayed Mod Loaded initialization from MenuLayer");
            PaimonOnModLoaded();
            PetManager::get().init();
            initPetTicker();
        }

        // limpiar contexto de lista al volver al menu
        Mod::get()->setSavedValue("current-list-id", 0);


        // hago schedule del update para que lea colores del GIF si adaptive esta activo
        this->scheduleUpdate();

        // miro si hay que reabrir el popup de verificacion al arrancar
        // getSavedValue no lanza excepciones — eliminado try/catch por seguridad ABI
        if (Mod::get()->getSavedValue<bool>("reopen-verification-queue", false)) {
            Mod::get()->setSavedValue("reopen-verification-queue", false);
            this->scheduleOnce(schedule_selector(PaimonMenuLayer::openVerificationQueue), 0.6f);
        }

        // meto el boton de config en el menu de abajo
        if (auto bottomMenu = this->getChildByID("bottom-menu")) {
            auto btnSpr = CircleButtonSprite::createWithSpriteFrameName("GJ_paintBtn_001.png", 1.0f, CircleBaseColor::Green, CircleBaseSize::Medium);
            auto btn = CCMenuItemSpriteExtra::create(btnSpr, this, menu_selector(PaimonMenuLayer::onBackgroundConfig));
            btn->setID("background-config-btn"_spr);
            bottomMenu->addChild(btn);
            bottomMenu->updateLayout();
        } else {
            // Fallback: crear menu propio con RowLayout.
            // Nota: la posicion {0, 8} es absoluta intencional — no existe nodo padre
            // con layout propio al que anclarse, y 8px es la separacion minima del borde.
            auto winSize = CCDirector::sharedDirector()->getWinSize();
            auto menu = CCMenu::create();
            menu->setContentSize({winSize.width, 40.f});
            menu->setAnchorPoint({0.f, 0.f});
            menu->ignoreAnchorPointForPosition(false);
            menu->setPosition({0.f, 8.f});
            menu->setLayout(RowLayout::create()
                ->setAxisAlignment(AxisAlignment::Start)
                ->setGap(8.f));
            menu->setID("paimon-fallback-bottom-menu"_spr);

            auto btnSpr = CircleButtonSprite::createWithSpriteFrameName("GJ_paintBtn_001.png", 1.0f, CircleBaseColor::Green, CircleBaseSize::Medium);
            auto btn = CCMenuItemSpriteExtra::create(btnSpr, this, menu_selector(PaimonMenuLayer::onBackgroundConfig));
            btn->setID("background-config-btn"_spr);
            menu->addChild(btn);
            menu->updateLayout();

            this->addChild(menu);
        }

        this->updateBackground();
        this->updateProfileButton();

        // ── Paimon escondida detras del titulo (clickeable) ──
        if (auto* title = this->getChildByID("main-title")) {
            auto paimonSpr = CCSprite::create("paim_Paimon.png"_spr);
            if (paimonSpr) {
                auto titleSize = title->getContentSize();
                auto titleScale = title->getScale();
                float titleW = titleSize.width * titleScale;
                float titleH = titleSize.height * titleScale;

                // escala para que sea mas pequena que el titulo
                float paimonMaxH = titleH * 0.7f;
                float paimonScale = paimonMaxH / paimonSpr->getContentSize().height;
                paimonSpr->setScale(paimonScale);

                // posicion random dentro del area del titulo
                static std::mt19937 rng(std::random_device{}());
                std::uniform_real_distribution<float> distX(-titleW * 0.35f, titleW * 0.35f);
                std::uniform_real_distribution<float> distY(-titleH * 0.15f, titleH * 0.15f);
                std::uniform_real_distribution<float> distRot(-45.f, 45.f);

                auto titlePos = title->getPosition();
                float px = titlePos.x + distX(rng);
                float py = titlePos.y + distY(rng);
                float rot = distRot(rng);

                // crear boton clickeable
                auto paimonBtn = CCMenuItemSpriteExtra::create(
                    paimonSpr, this, menu_selector(PaimonMenuLayer::onPaimonClick));
                paimonBtn->setRotation(rot);
                paimonBtn->setID("paimon-hidden-btn"_spr);

                // menu contenedor para el boton
                // Nota: posicion absoluta intencional — es un easter egg cuya
                // ubicacion es aleatoria relativa al titulo, no necesita layout.
                auto paimonMenu = CCMenu::create();
                paimonMenu->setPosition(ccp(px, py));
                paimonMenu->setContentSize(paimonSpr->getScaledContentSize());
                paimonMenu->setID("paimon-hidden-menu"_spr);
                paimonMenu->addChild(paimonBtn);

                paimonSpr->setOpacity(180);

                // zOrder 1: detras de las letras del titulo
                this->addChild(paimonMenu, 1);
            }
        }

        return true;
    }

    void update(float dt) {
        MenuLayer::update(dt);
        
        if (m_fields->m_adaptiveColors && m_fields->m_bgSprite) {
             // Ref<>::operator->() devuelve el puntero raw para typeinfo_cast
             if (auto gif = typeinfo_cast<AnimatedGIFSprite*>(static_cast<CCSprite*>(m_fields->m_bgSprite))) {
                 auto colors = gif->getCurrentFrameColors();
                 this->applyAdaptiveColor({colors.first.r, colors.first.g, colors.first.b});
             }
        }

        // Si el editor de foto guardo cambios, actualizar el boton de perfil
        if (ProfilePicCustomizer::get().isDirty()) {
            ProfilePicCustomizer::get().setDirty(false);
            this->updateProfileButton();
        }
    }

    void openVerificationQueue(float dt) {
        auto scene = VerificationCenterLayer::scene();
        if (scene) {
            TransitionManager::get().pushScene(scene);
        }
    }

    void onBackgroundConfig(CCObject*) {
        TransitionManager::get().pushScene(PaiConfigLayer::scene());
    }

    void onPaimonClick(CCObject* sender) {
        auto* btn = typeinfo_cast<CCMenuItemSpriteExtra*>(sender);
        if (!btn) return;

        // posicion mundial del boton para la explosion
        auto* parent = btn->getParent();
        CCPoint worldPos = parent
            ? parent->convertToWorldSpace(btn->getPosition())
            : btn->getPosition();

        // ── sonido de explosion random ──
        static char const* explosionSounds[] = {
            "explode_11.ogg",
            "quitSound_01.ogg",
            "endStart_02.ogg",
            "gold_02.ogg",
            "crystalDestroy.ogg",
        };
        static std::mt19937 rng(std::random_device{}());
        std::uniform_int_distribution<int> soundDist(0, 4);
        FMODAudioEngine::sharedEngine()->playEffect(explosionSounds[soundDist(rng)]);

        // ── efecto de particulas de explosion random ──
        static char const* explosionEffects[] = {
            "explodeEffect.plist",
            "firework_01.plist",
            "fireEffect_01.plist",
            "chestOpen.plist",
            "goldPickupEffect.plist",
        };
        std::uniform_int_distribution<int> fxDist(0, 4);
        auto* particles = CCParticleSystemQuad::create(explosionEffects[fxDist(rng)], false);
        if (particles) {
            particles->setPosition(worldPos);
            particles->setPositionType(kCCPositionTypeGrouped);
            particles->setAutoRemoveOnFinish(true);
            particles->setScale(1.5f);
            this->addChild(particles, 100);
        }

        // animacion de desaparicion del paimon
        btn->runAction(CCSequence::create(
            CCSpawn::create(
                CCScaleTo::create(0.3f, 0.f),
                CCRotateBy::create(0.3f, 360.f),
                CCFadeOut::create(0.3f),
                nullptr
            ),
            CCCallFunc::create(btn, callfunc_selector(CCNode::removeFromParent)),
            nullptr
        ));
    }

    void updateBackground() {
        // ── Leer config unificada (layerbg-menu-*) ──
        auto cfg = LayerBackgroundManager::get().getConfig("menu");

        // ── Fallback a legacy keys si la migracion no corrio ──
        if (cfg.type == "default") {
            std::string legacyType = Mod::get()->getSavedValue<std::string>("bg-type", "default");
            if (legacyType == "thumbnails") legacyType = "random";
            if (legacyType != "default" && !legacyType.empty()) {
                // Hay datos legacy sin migrar — migrar ahora al formato unificado
                cfg.type = legacyType;
                cfg.customPath = Mod::get()->getSavedValue<std::string>("bg-custom-path", "");
                cfg.levelId = Mod::get()->getSavedValue<int>("bg-id", 0);
                cfg.darkMode = Mod::get()->getSavedValue<bool>("bg-dark-mode", false);
                cfg.darkIntensity = Mod::get()->getSavedValue<float>("bg-dark-intensity", 0.5f);
                // Guardar al formato unificado para que no vuelva a caer aqui
                LayerBackgroundManager::get().saveConfig("menu", cfg);
            }
        }

        // ── modo "default": restaurar fondo original del juego ──
        if (cfg.type == "default") {
            if (auto bg = this->getChildByID("main-menu-bg")) {
                bg->setVisible(true);
                bg->setZOrder(-10);
            }
            // quito cualquier fondo custom que hubiera
            if (auto oldContainer = this->getChildByID("paimon-bg-container"_spr)) {
                oldContainer->removeFromParent();
            }
            if (auto oldContainer2 = this->getChildByID("paimon-layerbg-container"_spr)) {
                oldContainer2->removeFromParent();
            }
            m_fields->m_bgSprite = nullptr;
            m_fields->m_bgOverlay = nullptr;
            this->applyAdaptiveColor({255, 255, 255});
            return;
        }

        // ── Ocultar fondo original del juego ──
        if (auto bg = this->getChildByID("main-menu-bg")) {
            bg->setVisible(false);
        }

        // ── Limpiar container previo ──
        if (auto oldContainer = this->getChildByID("paimon-bg-container"_spr)) {
            oldContainer->removeFromParent();
        }
        if (auto oldContainer2 = this->getChildByID("paimon-layerbg-container"_spr)) {
            oldContainer2->removeFromParent();
        }
        m_fields->m_bgSprite = nullptr;
        m_fields->m_bgOverlay = nullptr;

        auto winSize = CCDirector::sharedDirector()->getWinSize();

        auto container = CCNode::create();
        container->setContentSize(winSize);
        container->setPosition({0, 0});
        container->setAnchorPoint({0, 0});
        container->setID("paimon-bg-container"_spr);
        container->setZOrder(-10);
        this->addChild(container);

        // ── Resolver tipo: manejar "Same as..." references ──
        std::string resolvedType = cfg.type;
        std::string resolvedPath = cfg.customPath;
        int resolvedId = cfg.levelId;

        int maxHops = 5;
        while (maxHops-- > 0) {
            bool isLayerRef = false;
            for (auto& [k, n] : LayerBackgroundManager::LAYER_OPTIONS) {
                if (resolvedType == k && k != "menu") { isLayerRef = true; break; }
            }
            if (isLayerRef) {
                auto refCfg = LayerBackgroundManager::get().getConfig(resolvedType);
                if (refCfg.type == "default") {
                    container->removeFromParent();
                    if (auto bg = this->getChildByID("main-menu-bg")) bg->setVisible(true);
                    this->applyAdaptiveColor({255, 255, 255});
                    return;
                }
                resolvedType = refCfg.type;
                resolvedPath = refCfg.customPath;
                resolvedId = refCfg.levelId;
                continue;
            }
            break;
        }

        CCSprite* sprite = nullptr;
        CCTexture2D* tex = nullptr;

        if (resolvedType == "custom" && !resolvedPath.empty()) {
            std::error_code fsEc;
            if (!std::filesystem::exists(resolvedPath, fsEc) || fsEc) {
                container->removeFromParent();
                return;
            }
            if (resolvedPath.ends_with(".gif") || resolvedPath.ends_with(".GIF")) {
                // ── GIF async ──
                // retain this + capturar container con Ref<> para evitar use-after-free
                // si el layer se destruye antes de que el callback async ejecute
                this->retain();
                Ref<CCNode> safeContainer = container;
                bool darkMode = cfg.darkMode;
                float darkIntensity = cfg.darkIntensity;
                std::string shaderName = cfg.shader;
                AnimatedGIFSprite::pinGIF(resolvedPath);
                AnimatedGIFSprite::createAsync(resolvedPath, [this, safeContainer, winSize, darkMode, darkIntensity, shaderName](AnimatedGIFSprite* anim) {
                    this->release();
                    if (!anim || !safeContainer->getParent()) {
                        if (!anim && safeContainer->getParent()) safeContainer->removeFromParent();
                        return;
                    }

                    float contentWidth = anim->getContentWidth();
                    float contentHeight = anim->getContentHeight();

                    if (contentWidth <= 0 || contentHeight <= 0) {
                        safeContainer->removeFromParent();
                        return;
                    }

                    float scaleX = winSize.width / contentWidth;
                    float scaleY = winSize.height / contentHeight;
                    float scale = std::max(scaleX, scaleY);

                    anim->ignoreAnchorPointForPosition(false);
                    anim->setAnchorPoint({0.5f, 0.5f});
                    anim->setPosition(winSize / 2);
                    anim->setScale(scale);

                    // Aplicar shader al GIF
                    if (!shaderName.empty() && shaderName != "none") {
                        auto* program = Shaders::getBgShaderProgram(shaderName);
                        if (program) {
                            anim->setShaderProgram(program);
                            anim->m_intensity = 0.5f;
                            anim->m_texSize = CCSize(winSize.width, winSize.height);
                        }
                    }

                    if (darkMode) {
                        GLubyte alpha = static_cast<GLubyte>(darkIntensity * 200.0f);
                        auto overlay = CCLayerColor::create({0, 0, 0, alpha});
                        overlay->setContentSize(winSize);
                        overlay->setZOrder(1);
                        safeContainer->addChild(overlay);
                        m_fields->m_bgOverlay = overlay;
                    }

                    safeContainer->addChild(anim);
                    m_fields->m_bgSprite = anim;
                });
                return;
            } else {
                // ── Imagen estatica ──
                CCTextureCache::sharedTextureCache()->removeTextureForKey(resolvedPath.c_str());
                tex = CCTextureCache::sharedTextureCache()->addImage(resolvedPath.c_str(), false);
                if (!tex) {
                    auto stbResult = ImageLoadHelper::loadWithSTB(std::filesystem::path(resolvedPath));
                    if (stbResult.success && stbResult.texture) {
                        stbResult.texture->autorelease();
                        tex = stbResult.texture;
                    }
                }
            }
        } else if (resolvedType == "id" && resolvedId > 0) {
            tex = LocalThumbs::get().loadTexture(resolvedId);
        }

        if (!sprite && !tex && (resolvedType == "random" || resolvedType == "thumbnails")) {
            auto ids = LocalThumbs::get().getAllLevelIDs();
            if (!ids.empty()) {
                static std::mt19937 rng(std::random_device{}());
                std::uniform_int_distribution<size_t> dist(0, ids.size() - 1);
                int32_t levelID = ids[dist(rng)];
                tex = LocalThumbs::get().loadTexture(levelID);
            }
        }

        if (!sprite && tex) {
            // Usar ShaderBgSprite si hay shader configurado
            if (!cfg.shader.empty() && cfg.shader != "none") {
                auto shaderSpr = Shaders::ShaderBgSprite::createWithTexture(tex);
                if (shaderSpr) {
                    auto* program = Shaders::getBgShaderProgram(cfg.shader);
                    if (program) {
                        shaderSpr->setShaderProgram(program);
                        shaderSpr->m_shaderIntensity = 0.5f;
                        shaderSpr->m_screenW = winSize.width;
                        shaderSpr->m_screenH = winSize.height;
                        shaderSpr->m_shaderTime = 0.f;
                        shaderSpr->schedule(schedule_selector(Shaders::ShaderBgSprite::updateShaderTime));
                    }
                    sprite = shaderSpr;
                }
            }
            if (!sprite) {
                sprite = CCSprite::createWithTexture(tex);
            }
        }

        if (!sprite) {
            container->removeFromParent();
            return;
        }

        float scaleX = winSize.width / sprite->getContentWidth();
        float scaleY = winSize.height / sprite->getContentHeight();
        float scale = std::max(scaleX, scaleY);

        sprite->setScale(scale);
        sprite->setPosition(winSize / 2);
        sprite->ignoreAnchorPointForPosition(false);
        sprite->setAnchorPoint({0.5f, 0.5f});

        // ── Adaptive colors ──
        bool adaptive = Mod::get()->getSavedValue<bool>("bg-adaptive-colors", false);
        m_fields->m_adaptiveColors = adaptive;
        if (adaptive && tex) {
            auto* img = new (std::nothrow) CCImage();
            if (img) {
                bool loaded = false;
                if (resolvedType == "custom" && !resolvedPath.empty()) {
                    loaded = img->initWithImageFile(resolvedPath.c_str());
                }
                if (loaded) {
                    auto colors = DominantColors::extract(img->getData(), img->getWidth(), img->getHeight());
                    ccColor3B primary = { colors.first.r, colors.first.g, colors.first.b };
                    this->applyAdaptiveColor(primary);
                } else {
                    this->applyAdaptiveColor({255, 255, 255});
                }
                img->release();
            } else {
                this->applyAdaptiveColor({255, 255, 255});
            }
        } else {
            this->applyAdaptiveColor({255, 255, 255});
        }

        // ── Dark mode ──
        if (cfg.darkMode) {
            GLubyte alpha = static_cast<GLubyte>(cfg.darkIntensity * 200.0f);
            auto overlay = CCLayerColor::create({0, 0, 0, alpha});
            overlay->setContentSize(winSize);
            overlay->setZOrder(1);
            container->addChild(overlay);
            m_fields->m_bgOverlay = overlay;
        }

        container->addChild(sprite);
        m_fields->m_bgSprite = sprite;

        log::debug("Updated menu background with unified config, type: {}", cfg.type);
    }



    void updateProfileButton() {
        std::string type = Mod::get()->getSavedValue<std::string>("profile-bg-type", "none");
        
        if (type != "custom") return;

        std::string path = Mod::get()->getSavedValue<std::string>("profile-bg-path", "");
        if (path.empty()) return;

        std::error_code fsEc;
        if (!std::filesystem::exists(path, fsEc) || fsEc) {
             return;
        }

        auto profileMenu = this->getChildByID("profile-menu");
        if (!profileMenu) {
            profileMenu = this->getChildByIDRecursive("profile-menu");
        }
        
        if (!profileMenu) return;

        auto profileButton = typeinfo_cast<CCMenuItemSpriteExtra*>(profileMenu->getChildByID("profile-button"));
        if (!profileButton) {
             return;
        }
        
        float const targetSize = 48.0f;

        // Leer config de forma personalizada
        auto picCfg = ProfilePicCustomizer::get().getConfig();
        std::string shapeName = picCfg.stencilSprite;
        if (shapeName.empty()) shapeName = "circle";

        if (path.ends_with(".gif") || path.ends_with(".GIF")) {
             this->retain();
             AnimatedGIFSprite::pinGIF(path);
             // Capturar profileButton con Ref<> para evitar use-after-free
             // si el nodo se destruye antes de que el callback async ejecute
             Ref<CCMenuItemSpriteExtra> safeProfileBtn = profileButton;
             AnimatedGIFSprite::createAsync(path, [this, safeProfileBtn, targetSize, shapeName, picCfg](AnimatedGIFSprite* anim) {
                this->release();
                if (!anim || !safeProfileBtn->getParent()) return;

                auto container = buildProfileClipContainer(anim, shapeName, targetSize, picCfg);
                if (container) {
                    safeProfileBtn->setNormalImage(container);
                }
            });
        } else {
            auto sprite = CCSprite::create(path.c_str());
            if (!sprite) return;

            auto container = buildProfileClipContainer(sprite, shapeName, targetSize, picCfg);
            if (container) {
                profileButton->setNormalImage(container);
            }
        }
    }
};

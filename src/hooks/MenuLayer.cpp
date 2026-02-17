#include <Geode/modify/MenuLayer.hpp>
#include <Geode/utils/cocos.hpp>
#include <Geode/utils/file.hpp>
#include "../managers/HoverManager.hpp"
#include "../managers/LocalThumbs.hpp"
#include "../layers/VerificationQueuePopup.hpp"
#include "../layers/BackgroundConfigPopup.hpp"
#include "../utils/AnimatedGIFSprite.hpp"
#include "../utils/DominantColors.hpp"
#include <random>
#include <filesystem>

using namespace geode::prelude;

class $modify(PaimonMenuLayer, MenuLayer) {
    struct Fields {
        CCSprite* m_bgSprite = nullptr;
        CCLayerColor* m_bgOverlay = nullptr;
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

        if (auto menu = this->getChildByID("main-menu")) {
             for (auto* btn : CCArrayExt<CCMenuItem*>(menu->getChildren())) {
                 if (!btn) continue;
                 for (auto* kid : CCArrayExt<CCNode*>(btn->getChildren())) {
                     tintNode(kid);
                 }
             }
        }
        
        if (auto menu = this->getChildByID("profile-menu")) {
             for (auto* btn : CCArrayExt<CCMenuItem*>(menu->getChildren())) {
                 if (!btn) continue;
                 if (btn->getID() == "profile-button") continue;
                 for (auto* kid : CCArrayExt<CCNode*>(btn->getChildren())) {
                     tintNode(kid);
                 }
             }
        }
        
        if (auto menu = this->getChildByID("right-side-menu")) {
             for (auto* btn : CCArrayExt<CCMenuItem*>(menu->getChildren())) {
                 if (!btn) continue;
                 for (auto* kid : CCArrayExt<CCNode*>(btn->getChildren())) {
                     tintNode(kid);
                 }
             }
        }
        
        if (auto lbl = this->getChildByID("player-username")) {
            static_cast<CCLabelBMFont*>(lbl)->setColor(color);
        }
    } 

    bool init() {
        if (!MenuLayer::init()) {
            return false;
        }

        // inicio el HoverManager global
        auto* hoverManager = HoverManager::get();
        if (hoverManager && !hoverManager->getParent()) {
            this->addChild(hoverManager);
            log::info("[HoverManager] Initialized and added to scene");
        }

        // hago schedule del update para que lea colores del GIF si adaptive está activo
        this->scheduleUpdate();

        // miro si hay que reabrir el popup de verificación al arrancar
        try {
            if (Mod::get()->getSavedValue<bool>("reopen-verification-queue", false)) {
                Mod::get()->setSavedValue("reopen-verification-queue", false);
                this->scheduleOnce(schedule_selector(PaimonMenuLayer::openVerificationQueue), 0.6f);
            }
        } catch(...) {}

        // meto el botón de config en el menú de abajo
        if (auto bottomMenu = this->getChildByID("bottom-menu")) {
            auto btnSpr = CCSprite::createWithSpriteFrameName("GJ_paintBtn_001.png");
            btnSpr->setScale(0.8f);
            auto btn = CCMenuItemSpriteExtra::create(btnSpr, this, menu_selector(PaimonMenuLayer::onBackgroundConfig));
            btn->setID("background-config-btn"_spr);
            bottomMenu->addChild(btn);
            bottomMenu->updateLayout();
        } else {
            auto menu = CCMenu::create();
            menu->setPosition({0, 0});
            this->addChild(menu);

            auto btnSpr = CCSprite::createWithSpriteFrameName("GJ_paintBtn_001.png");
            btnSpr->setScale(0.8f);
            auto btn = CCMenuItemSpriteExtra::create(btnSpr, this, menu_selector(PaimonMenuLayer::onBackgroundConfig));
            btn->setID("background-config-btn"_spr);
            btn->setPosition({30, 30}); // abajo a la izquierda
            menu->addChild(btn);
        }

        this->updateBackground();
        this->updateProfileButton();
        this->updateProfileButton(); // lo llamo dos veces igual que el original por si acaso

        return true;
    }

    void update(float dt) {
        MenuLayer::update(dt);
        
        bool adaptive = Mod::get()->getSavedValue<bool>("bg-adaptive-colors", false);
        if (adaptive && m_fields->m_bgSprite) {
             if (auto gif = typeinfo_cast<AnimatedGIFSprite*>(m_fields->m_bgSprite)) {
                 auto colors = gif->getCurrentFrameColors();
                 this->applyAdaptiveColor({colors.first.r, colors.first.g, colors.first.b});
             }
        }
    }

    void openVerificationQueue(float dt) {
        auto popup = VerificationQueuePopup::create();
        if (popup) {
            popup->show();
        }
    }

    void onBackgroundConfig(CCObject*) {
        BackgroundConfigPopup::create()->show();
    }

    void updateBackground() {
        std::string type = Mod::get()->getSavedValue<std::string>("bg-type", "random");

        // modo “default”: dejo el fondo del juego tal cual
        if (type == "default") {
            if (auto bg = this->getChildByID("main-menu-bg")) {
                bg->setVisible(true);
                bg->setZOrder(-10); // por si hace falta que quede detrás de todo
            }
            // quito cualquier fondo custom que hubiera
            if (m_fields->m_bgSprite) {
                m_fields->m_bgSprite->getParent()->removeFromParent();
                m_fields->m_bgSprite = nullptr;
            }
             if (m_fields->m_bgOverlay) {
                m_fields->m_bgOverlay->removeFromParent();
                m_fields->m_bgOverlay = nullptr;
            }
            // reseteo los colores a blanco al volver al menú default
            this->applyAdaptiveColor({255, 255, 255}); 
            
            return;
        }

        // si usamos fondo custom, oculto el background default
        if (auto bg = this->getChildByID("main-menu-bg")) {
            bg->setVisible(false);
        }

        if (m_fields->m_bgSprite) {
            m_fields->m_bgSprite->getParent()->removeFromParent();
            m_fields->m_bgSprite = nullptr;
        }
        if (m_fields->m_bgOverlay) {
            m_fields->m_bgOverlay->removeFromParent();
            m_fields->m_bgOverlay = nullptr;
        }

        auto winSize = CCDirector::sharedDirector()->getWinSize();

        auto container = CCNode::create();
        container->setContentSize(winSize);
        container->setPosition({0, 0});
        container->setAnchorPoint({0, 0});
        container->setID("paimon-bg-container"_spr);
        container->setZOrder(-10);
        this->addChild(container);

        // el tipo ya lo leí arriba
        CCSprite* sprite = nullptr;
        CCTexture2D* tex = nullptr;

        if (type == "custom") {
            std::string path = Mod::get()->getSavedValue<std::string>("bg-custom-path", "");
            if (!path.empty() && std::filesystem::exists(path)) {
                if (path.ends_with(".gif") || path.ends_with(".GIF")) {
                    this->retain();
                    AnimatedGIFSprite::pinGIF(path);
                    AnimatedGIFSprite::createAsync(path, [this, path, container, winSize](AnimatedGIFSprite* anim) {
                        this->autorelease();
                        if (!anim) {
                             container->removeFromParent();
                             return;
                        }

                        if (m_fields->m_bgSprite) {
                             auto oldContainer = m_fields->m_bgSprite->getParent();
                             if(oldContainer) oldContainer->removeFromParent();
                             m_fields->m_bgSprite = nullptr;
                        }

                        float contentWidth = anim->getContentWidth();
                        float contentHeight = anim->getContentHeight();
                    
                        if (contentWidth <= 0 || contentHeight <= 0) {
                            log::error("[PaimonMenuLayer] Invalid GIF content size: {}x{}", contentWidth, contentHeight);
                            container->removeFromParent();
                            return;
                        }

                        float scaleX = winSize.width / contentWidth;
                        float scaleY = winSize.height / contentHeight;
                        float scale = std::max(scaleX, scaleY);
                        
                        log::info("[PaimonMenuLayer] GIF Debug: ContentSize={}x{}, WinSize={}x{}, ScaleX={}, ScaleY={}, FinalScale={}", 
                            contentWidth, contentHeight, winSize.width, winSize.height, scaleX, scaleY, scale);

                        anim->ignoreAnchorPointForPosition(false);
                        anim->setAnchorPoint({0.5f, 0.5f});
                        anim->setPosition(winSize / 2);
                        anim->setScale(scale);
                        
                        bool darkMode = Mod::get()->getSavedValue<bool>("bg-dark-mode", false);
                        if (darkMode) {
                            float intensity = Mod::get()->getSavedValue<float>("bg-dark-intensity", 0.5f);
                            GLubyte alpha = static_cast<GLubyte>(intensity * 200.0f);
                            
                            auto overlay = CCLayerColor::create({0, 0, 0, alpha});
                            overlay->setContentSize(winSize);
                            overlay->setZOrder(1);
                            container->addChild(overlay);
                            m_fields->m_bgOverlay = overlay;
                            
                            anim->setColor({255, 255, 255});
                        } else {
                            anim->setColor({255, 255, 255});
                        }
                        
                        container->addChild(anim);
                        m_fields->m_bgSprite = anim;
                    });
                    
                    return; 
                } else {
                    auto image = new CCImage();
                    if (image->initWithImageFile(path.c_str())) {
                        tex = new CCTexture2D();
                        if (tex->initWithImage(image)) {
                            tex->autorelease();
                        } else {
                            CC_SAFE_RELEASE(tex);
                            tex = nullptr;
                        }
                    }
                    image->release();
                }
            }
        } else if (type == "id") {
            int id = Mod::get()->getSavedValue<int>("bg-id", 0);
            if (id > 0) {
                tex = LocalThumbs::get().loadTexture(id);
            }
        } 
        
        if (!sprite && !tex && (type == "random" || type == "thumbnails")) {
             auto ids = LocalThumbs::get().getAllLevelIDs();
             if (!ids.empty()) {
                static std::mt19937 rng(std::random_device{}());
                std::uniform_int_distribution<size_t> dist(0, ids.size() - 1);
                int32_t levelID = ids[dist(rng)];
                tex = LocalThumbs::get().loadTexture(levelID);
             }
        }

        if (!sprite && tex) {
            sprite = CCSprite::createWithTexture(tex);
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
        
        bool adaptive = Mod::get()->getSavedValue<bool>("bg-adaptive-colors", false);
        if (adaptive && tex) {
            if (auto img = new CCImage()) {
                bool loaded = false;
                if (type == "custom") {
                     std::string path = Mod::get()->getSavedValue<std::string>("bg-custom-path", "");
                     loaded = img->initWithImageFile(path.c_str());
                }
                
                if (loaded) {
                     auto colors = DominantColors::extract(img->getData(), img->getWidth(), img->getHeight());
                     ccColor3B primary = { colors.first.r, colors.first.g, colors.first.b };
                     this->applyAdaptiveColor(primary);
                } else {
                     this->applyAdaptiveColor({255, 255, 255});
                }
                img->release();
            }
        } else {
             this->applyAdaptiveColor({255, 255, 255});
        }
        
        bool darkMode = Mod::get()->getSavedValue<bool>("bg-dark-mode", false);
        if (darkMode) {
            float intensity = Mod::get()->getSavedValue<float>("bg-dark-intensity", 0.5f);
            GLubyte alpha = static_cast<GLubyte>(intensity * 200.0f);
            
            auto overlay = CCLayerColor::create({0, 0, 0, alpha});
            overlay->setContentSize(winSize);
            overlay->setZOrder(1);
            container->addChild(overlay);
            m_fields->m_bgOverlay = overlay;
            
            sprite->setColor({255, 255, 255});
        } else {
            sprite->setColor({255, 255, 255});
        }
        
        container->addChild(sprite);
        m_fields->m_bgSprite = sprite;
        
        log::info("Updated background with type: {}", type);
    }



    void updateProfileButton() {
        std::string type = Mod::get()->getSavedValue<std::string>("profile-bg-type", "none");
        
        if (type != "custom") return;

        std::string path = Mod::get()->getSavedValue<std::string>("profile-bg-path", "");
        if (path.empty() || !std::filesystem::exists(path)) {
             return;
        }

        auto profileMenu = this->getChildByID("profile-menu");
        if (!profileMenu) {
            profileMenu = this->getChildByIDRecursive("profile-menu");
        }
        
        if (!profileMenu) return;

        auto profileButton = static_cast<CCMenuItemSpriteExtra*>(profileMenu->getChildByID("profile-button"));
        if (!profileButton) {
             return;
        }
        
        const float targetSize = 48.0f;

        if (path.ends_with(".gif") || path.ends_with(".GIF")) {
             this->retain();
             AnimatedGIFSprite::pinGIF(path);
             AnimatedGIFSprite::createAsync(path, [this, profileButton, targetSize](AnimatedGIFSprite* anim) {
                this->autorelease();
                if (!anim) return;

                auto stencil = CCSprite::createWithSpriteFrameName("d_circle_02_001.png");
                if (!stencil) stencil = CCSprite::createWithSpriteFrameName("GJ_circle_01_001.png");
                if (!stencil) return;
                
                stencil->setScale(targetSize / stencil->getContentWidth());
                stencil->setPosition({targetSize/2, targetSize/2});
                
                auto clipper = CCClippingNode::create();
                clipper->setStencil(stencil);
                clipper->setAlphaThreshold(0.5f);
                clipper->setContentSize({targetSize, targetSize});
                clipper->setAnchorPoint({0.5f, 0.5f});
                
                float scaleX = targetSize / anim->getContentWidth();
                float scaleY = targetSize / anim->getContentHeight();
                float scale = std::max(scaleX, scaleY);
                
                anim->setScale(scale);
                anim->setPosition({targetSize/2, targetSize/2});
                anim->setAnchorPoint({0.5f, 0.5f});
                anim->ignoreAnchorPointForPosition(false);
                
                clipper->addChild(anim);
                
                profileButton->setNormalImage(clipper);
            });
        } else {
            auto stencil = CCSprite::createWithSpriteFrameName("d_circle_02_001.png");
            if (!stencil) stencil = CCSprite::createWithSpriteFrameName("GJ_circle_01_001.png");
            if (!stencil) return;
            
            stencil->setScale(targetSize / stencil->getContentWidth());
            stencil->setPosition({targetSize/2, targetSize/2});
            
            auto clipper = CCClippingNode::create();
            clipper->setStencil(stencil);
            clipper->setAlphaThreshold(0.5f);
            clipper->setContentSize({targetSize, targetSize});
            
            auto sprite = CCSprite::create(path.c_str());
            if (sprite) {
                float scaleX = targetSize / sprite->getContentWidth();
                float scaleY = targetSize / sprite->getContentHeight();
                float scale = std::max(scaleX, scaleY);
                
                sprite->setScale(scale);
                sprite->setPosition({targetSize/2, targetSize/2});
                sprite->setAnchorPoint({0.5f, 0.5f});
                
                clipper->addChild(sprite);
                
                profileButton->setNormalImage(clipper);
            }
        }
    }
};

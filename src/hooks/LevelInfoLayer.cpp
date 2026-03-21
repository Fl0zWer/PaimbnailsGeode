#include <Geode/modify/LevelInfoLayer.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/binding/LeaderboardsLayer.hpp>
#include "../utils/PaimonButtonHighlighter.hpp"
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/utils/cocos.hpp>
#include <Geode/utils/string.hpp>
#include <Geode/ui/BasedButtonSprite.hpp>
#include <Geode/ui/LoadingSpinner.hpp>
#include "../utils/PaimonNotification.hpp"
#include "../features/transitions/services/TransitionManager.hpp"
#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/GJAccountManager.hpp>
#include <Geode/binding/LevelSelectLayer.hpp>
#include <vector>
#include <cmath>
#include <filesystem>
#include <sstream>

#include "../features/thumbnails/services/LocalThumbs.hpp"
#include "../managers/ThumbnailAPI.hpp"
#include "../features/thumbnails/services/ThumbnailLoader.hpp"
#include "../features/dynamic-songs/services/DynamicSongManager.hpp"
#include "../utils/AnimatedGIFSprite.hpp"
#include "../features/profiles/ui/RatePopup.hpp"

#include "../utils/Localization.hpp"
#include "../utils/ImageConverter.hpp"
#include "../utils/HttpClient.hpp"

#include "../layers/ButtonEditOverlay.hpp"
#include "../managers/ButtonLayoutManager.hpp"
#include "../features/moderation/ui/SetDailyWeeklyPopup.hpp"
#include "../framework/state/SessionState.hpp"

#include "../utils/Shaders.hpp"

using namespace geode::prelude;
using namespace Shaders;

#include "../features/thumbnails/ui/LocalThumbnailViewPopup.hpp"
#include "../features/thumbnails/ui/ThumbnailSettingsPopup.hpp"

class $modify(PaimonLevelInfoLayer, LevelInfoLayer) {
    CCMenu* findLeftSideMenu() {
        if (auto byId = typeinfo_cast<CCMenu*>(this->getChildByID("left-side-menu"))) {
            return byId;
        }
        if (auto children = this->getChildren()) {
            for (auto* child : CCArrayExt<CCNode*>(children)) {
                auto* menu = typeinfo_cast<CCMenu*>(child);
                if (!menu) continue;
                if (menu->getPositionX() < this->getContentSize().width * 0.5f) {
                    return menu;
                }
            }
        }
        return nullptr;
    }

    static void onModify(auto& self) {
        // Dependemos de node-ids para ubicar nodos y fondos con IDs estables.
        (void)self.setHookPriorityAfterPost("LevelInfoLayer::init", "geode.node-ids");
    }

    struct Fields {
        Ref<CCMenuItemSpriteExtra> m_thumbnailButton = nullptr;
        Ref<CCNode> m_pixelBg = nullptr;
        std::vector<Ref<CCSprite>> m_extraBgSprites;
        float m_shaderTime = 0.0f;
        bool m_animatedShader = false;
        bool m_fromThumbsList = false;
        bool m_fromReportSection = false;
        bool m_fromVerificationQueue = false;
        bool m_fromLeaderboards = false;
        LeaderboardType m_leaderboardType = LeaderboardType::Default;
        LeaderboardStat m_leaderboardStat = LeaderboardStat::Stars;
        Ref<CCMenuItemSpriteExtra> m_acceptThumbBtn = nullptr;
        Ref<CCMenuItemSpriteExtra> m_editModeBtn = nullptr;
        Ref<CCMenuItemSpriteExtra> m_uploadLocalBtn = nullptr;
        Ref<CCMenu> m_extraMenu = nullptr;
        bool m_thumbnailRequested = false; // evita cargas duplicadas
        int m_loadedInvalidationVersion = 0; // version invalidacion pa detectar cambios
        
        // multi-thumb
        std::vector<ThumbnailAPI::ThumbnailInfo> m_thumbnails;
        int m_currentThumbnailIndex = 0;
        Ref<CCMenuItemSpriteExtra> m_prevBtn = nullptr;
        Ref<CCMenuItemSpriteExtra> m_nextBtn = nullptr;
        Ref<CCMenuItemSpriteExtra> m_rateBtn = nullptr;
        bool m_cycling = true;
        float m_cycleTimer = 0.0f;
        int m_galleryToken = 0;
        int m_bgRequestToken = 0;
        int m_invalidationListenerId = 0;
    };
    
    void applyThumbnailBackground(CCTexture2D* tex, int32_t levelID) {
        if (!tex) return;
        
        log::info("[LevelInfoLayer] Aplicando fondo del thumbnail");
        
        // reset animacion de shader previo
        m_fields->m_animatedShader = false;
        m_fields->m_shaderTime = 0.0f;
        
        // limpiar sprites de efectos extra anteriores
        for (auto& s : m_fields->m_extraBgSprites) {
            if (s) s->removeFromParent();
        }
        m_fields->m_extraBgSprites.clear();
        
        // estilo + intensidad
        auto bgStyle = geode::Mod::get()->getSettingValue<std::string>("levelinfo-background-style");
        int intensity = std::clamp(static_cast<int>(geode::Mod::get()->getSettingValue<int64_t>("levelinfo-effect-intensity")), 1, 10);
        auto win = CCDirector::sharedDirector()->getWinSize();

        // tabla de mapeo estilo -> shader/flags (scope de funcion, accesible por applyEffects Y extra-styles)
        struct ShaderEntry {
            char const* name; char const* key; char const* frag;
            bool boosted; bool screenSize; bool time;
        };
        static ShaderEntry const kShaderTable[] = {
            {"grayscale",       "grayscale"_spr,       fragmentShaderGrayscale,       false, false, false},
            {"sepia",           "sepia"_spr,           fragmentShaderSepia,           false, false, false},
            {"vignette",        "vignette"_spr,        fragmentShaderVignette,        false, false, false},
            {"scanlines",       "scanlines"_spr,       fragmentShaderScanlines,       false, true,  false},
            {"bloom",           "bloom"_spr,           fragmentShaderBloom,            true, true,  false},
            {"chromatic",       "chromatic-v2"_spr,    fragmentShaderChromatic,        true, false, true},
            {"radial-blur",     "radial-blur-v2"_spr,  fragmentShaderRadialBlur,       true, false, true},
            {"glitch",          "glitch-v2"_spr,       fragmentShaderGlitch,           true, false, true},
            {"posterize",       "posterize"_spr,       fragmentShaderPosterize,       false, false, false},
            {"rain",            "rain"_spr,            fragmentShaderRain,             true, false, true},
            {"matrix",          "matrix"_spr,          fragmentShaderMatrix,           true, false, true},
            {"neon-pulse",      "neon-pulse"_spr,      fragmentShaderNeonPulse,        true, false, true},
            {"wave-distortion", "wave-distortion"_spr, fragmentShaderWaveDistortion,   true, false, true},
            {"crt",             "crt"_spr,             fragmentShaderCRT,              true, false, true},
        };

        // helper: buscar shader por nombre en la tabla
        auto lookupShader = [&](std::string const& style) -> std::tuple<CCGLProgram*, float, bool, bool> {
            for (auto& e : kShaderTable) {
                if (style == e.name) {
                    float v = e.boosted ? (intensity / 10.0f) * 2.25f : intensity / 10.0f;
                    return {getOrCreateShader(e.key, vertexShaderCell, e.frag), v, e.screenSize, e.time};
                }
            }
            return {nullptr, 0.f, false, false};
        };

        // lambda efectos
        auto applyEffects = [this, &bgStyle, &intensity, &win, &tex, &lookupShader](CCSprite*& sprite, bool isGIF) {
            if (!sprite) return;

            // scale + pos inicial
            float scaleX = win.width / sprite->getContentSize().width;
            float scaleY = win.height / sprite->getContentSize().height;
            float scale = std::max(scaleX, scaleY);
            sprite->setScale(scale);
            sprite->setPosition({win.width / 2.0f, win.height / 2.0f});
            sprite->setAnchorPoint({0.5f, 0.5f});

            if (bgStyle == "normal") {
                ccTexParams params{GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE};
                sprite->getTexture()->setTexParameters(&params);
            }
            else if (bgStyle == "pixel") {
                if (isGIF) {
                     auto shader = getOrCreateShader("pixelate"_spr, vertexShaderCell, fragmentShaderPixelate);
                     if (shader) {
                         sprite->setShaderProgram(shader);
                         shader->use();
                         shader->setUniformsForBuiltins();
                         float intensityVal = (intensity - 1) / 9.0f;
                         if (auto ags = typeinfo_cast<AnimatedGIFSprite*>(sprite)) ags->m_intensity = intensityVal;
                         else shader->setUniformLocationWith1f(shader->getUniformLocationForName("u_intensity"), intensityVal);
                         shader->setUniformLocationWith2f(shader->getUniformLocationForName("u_screenSize"), win.width, win.height);
                     }
                } else {
                    float t = (intensity - 1) / 9.0f;
                    float pixelFactor = 0.5f - (t * 0.47f);
                    int renderWidth = std::max(32, static_cast<int>(win.width * pixelFactor));
                    int renderHeight = std::max(32, static_cast<int>(win.height * pixelFactor));
                    auto renderTex = CCRenderTexture::create(renderWidth, renderHeight);
                    if (renderTex) {
                        float renderScale = std::min(
                            static_cast<float>(renderWidth) / tex->getContentSize().width,
                            static_cast<float>(renderHeight) / tex->getContentSize().height);
                        sprite->setScale(renderScale);
                        sprite->setPosition({renderWidth / 2.0f, renderHeight / 2.0f});
                        renderTex->begin();
                        glClearColor(0, 0, 0, 0);
                        glClear(GL_COLOR_BUFFER_BIT);
                        sprite->visit();
                        renderTex->end();
                        auto pixelTexture = renderTex->getSprite()->getTexture();
                        sprite = CCSprite::createWithTexture(pixelTexture);
                        if (sprite) {
                            float finalScale = std::max(win.width / renderWidth, win.height / renderHeight);
                            sprite->setScale(finalScale);
                            sprite->setFlipY(true);
                            sprite->setAnchorPoint({0.5f, 0.5f});
                            sprite->setPosition({win.width / 2.0f, win.height / 2.0f});
                            ccTexParams params{GL_NEAREST, GL_NEAREST, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE};
                            pixelTexture->setTexParameters(&params);
                        }
                    }
                }
            }
            else if (bgStyle == "blur") {
                if (isGIF) {
                     auto shader = Shaders::getBlurSinglePassShader();
                     if (shader) {
                         sprite->setShaderProgram(shader);
                         shader->use();
                         shader->setUniformsForBuiltins();
                         float intensityVal = (intensity - 1) / 9.0f;
                         if (auto ags = typeinfo_cast<AnimatedGIFSprite*>(sprite)) ags->m_intensity = intensityVal;
                         else shader->setUniformLocationWith1f(shader->getUniformLocationForName("u_intensity"), intensityVal);
                         shader->setUniformLocationWith2f(shader->getUniformLocationForName("u_screenSize"), win.width, win.height);
                     }
                } else {
                    sprite = Shaders::createBlurredSprite(tex, win, static_cast<float>(intensity));
                    if (sprite) sprite->setPosition({win.width / 2.0f, win.height / 2.0f});
                }
            }
            else {
                auto [shader, val, useScreenSize, needsTime] = lookupShader(bgStyle);

                if (shader) {
                    sprite->setShaderProgram(shader);
                    shader->use();
                    shader->setUniformsForBuiltins();
                    if (auto ags = typeinfo_cast<AnimatedGIFSprite*>(sprite)) {
                        ags->m_intensity = val;
                    } else {
                        shader->setUniformLocationWith1f(shader->getUniformLocationForName("u_intensity"), val);
                    }
                    if (useScreenSize) {
                        shader->setUniformLocationWith2f(shader->getUniformLocationForName("u_screenSize"), win.width, win.height);
                    }
                    if (needsTime) {
                        shader->setUniformLocationWith1f(shader->getUniformLocationForName("u_time"), 0.0f);
                        m_fields->m_animatedShader = true;
                        m_fields->m_shaderTime = 0.0f;
                    }
                }
            }
        };

        // 1. sprite estatico
        CCSprite* finalSprite = CCSprite::createWithTexture(tex);
        if (finalSprite) {
            applyEffects(finalSprite, false);
            
            Ref<CCNode> oldBg = m_fields->m_pixelBg;
            if (!oldBg) {
                oldBg = this->getChildByID("paimon-levelinfo-pixel-bg"_spr);
            }
            
            finalSprite->setZOrder(-1);
            finalSprite->setID("paimon-levelinfo-pixel-bg"_spr);
            this->addChild(finalSprite);
            m_fields->m_pixelBg = finalSprite;

            // crossfade: nuevo fondo entra con fade-in, viejo sale con fade-out
            if (oldBg && oldBg->getParent()) {
                finalSprite->setOpacity(0);
                finalSprite->runAction(CCFadeTo::create(0.4f, 255));
                auto* oldPtr = oldBg.data();
                oldPtr->runAction(CCSequence::create(
                    CCFadeTo::create(0.4f, 0),
                    CCCallFunc::create(oldPtr, callfunc_selector(CCNode::removeFromParent)),
                    nullptr
                ));
            } else if (oldBg) {
                oldBg->removeFromParent();
            }
        }

        // === MULTI-EFECTO: capas extra ===
        std::string extraStylesRaw = geode::Mod::get()->getSettingValue<std::string>("levelinfo-extra-styles");
        if (!extraStylesRaw.empty() && finalSprite) {
            // parsear comma-separated, max 4 extra
            std::vector<std::string> extraStyles;
            {
                std::stringstream ss(extraStylesRaw);
                std::string token;
                while (std::getline(ss, token, ',') && extraStyles.size() < 4) {
                    // trim whitespace
                    size_t start = token.find_first_not_of(" \t");
                    size_t end = token.find_last_not_of(" \t");
                    if (start != std::string::npos) {
                        extraStyles.push_back(token.substr(start, end - start + 1));
                    }
                }
            }

            float intensityF = (intensity / 10.0f) * 2.25f;
            for (auto& es : extraStyles) {
                if (es.empty() || es == "normal" || es == bgStyle) continue;

                auto [eshader, eval, eScreenSize, eNeedsTime] = lookupShader(es);
                if (!eshader) continue;

                auto extraSpr = CCSprite::createWithTexture(tex);
                if (!extraSpr) continue;

                float sx = win.width / extraSpr->getContentSize().width;
                float sy = win.height / extraSpr->getContentSize().height;
                extraSpr->setScale(std::max(sx, sy));
                extraSpr->setPosition({win.width / 2.0f, win.height / 2.0f});
                extraSpr->setAnchorPoint({0.5f, 0.5f});
                extraSpr->setOpacity(180); // semi-transparent overlay blend

                extraSpr->setShaderProgram(eshader);
                eshader->use();
                eshader->setUniformsForBuiltins();
                eshader->setUniformLocationWith1f(eshader->getUniformLocationForName("u_intensity"), eval);
                if (eScreenSize) {
                    eshader->setUniformLocationWith2f(eshader->getUniformLocationForName("u_screenSize"), win.width, win.height);
                }
                if (eNeedsTime) {
                    eshader->setUniformLocationWith1f(eshader->getUniformLocationForName("u_time"), 0.0f);
                    m_fields->m_animatedShader = true;
                }

                extraSpr->setZOrder(-1);
                this->addChild(extraSpr);
                m_fields->m_extraBgSprites.push_back(extraSpr);
            }
        }

        // schedule animacion de shader si es necesario
        this->unschedule(schedule_selector(PaimonLevelInfoLayer::updateShaderTime));
        if (m_fields->m_animatedShader && m_fields->m_pixelBg) {
            this->schedule(schedule_selector(PaimonLevelInfoLayer::updateShaderTime));
        }

        // 2. gif? reemplazar
        if (ThumbnailLoader::get().hasGIFData(levelID)) {
             auto path = ThumbnailLoader::get().getCachePath(levelID, true);
             Ref<LevelInfoLayer> self = this;
             AnimatedGIFSprite::createAsync(geode::utils::string::pathToString(path), [self, applyEffects](AnimatedGIFSprite* anim) {
                 auto* layer = static_cast<PaimonLevelInfoLayer*>(self.data());
                 if (anim) {
                     // quitar fondo estatico
                     if (layer->m_fields->m_pixelBg) {
                         layer->m_fields->m_pixelBg->removeFromParent();
                     } else if (auto old = layer->getChildByID("paimon-levelinfo-pixel-bg"_spr)) {
                         old->removeFromParent();
                     }
                     
                     // efectos al gif
                     CCSprite* spritePtr = anim; // el helper espera CCSprite*&
                     applyEffects(spritePtr, true);
                     
                     anim->setZOrder(-1);
                     anim->setID("paimon-levelinfo-pixel-bg"_spr);
                     
                     layer->addChild(anim);
                     layer->m_fields->m_pixelBg = anim;
                 }
             });
        }

        // limpiar overlay anterior para evitar acumulacion de capas
        if (auto oldOverlay = this->getChildByID("paimon-levelinfo-pixel-overlay"_spr)) {
            oldOverlay->removeFromParent();
        }
        // overlay — oscuridad configurable
        int darknessVal = static_cast<int>(geode::Mod::get()->getSettingValue<int64_t>("levelinfo-bg-darkness"));
        darknessVal = std::max(0, std::min(50, darknessVal));
        GLubyte overlayAlpha = static_cast<GLubyte>((darknessVal / 50.0f) * 255.0f);
        auto overlay = CCLayerColor::create({0,0,0,overlayAlpha});
        overlay->setContentSize(win);
        overlay->setAnchorPoint({0,0});
        overlay->setPosition({0,0});
        overlay->setZOrder(-1);
        overlay->setID("paimon-levelinfo-pixel-overlay"_spr);
        this->addChild(overlay);
        
        log::info("[LevelInfoLayer] Fondo aplicado exitosamente (estilo: {}, intensidad: {})", bgStyle, intensity);
    }
    
    $override
    void onEnter() {
        LevelInfoLayer::onEnter();

        // detecta si la miniatura cambio mientras estábamos en PlayLayer
        // (p.ej. el usuario subio una thumb desde PauseLayer)
        if (m_level && m_fields->m_thumbnailRequested) {
            int32_t levelID = m_level->m_levelID.value();
            int currentVersion = ThumbnailLoader::get().getInvalidationVersion(levelID);
            if (currentVersion != m_fields->m_loadedInvalidationVersion) {
                m_fields->m_loadedInvalidationVersion = currentVersion;
                refreshGalleryData(levelID, true);
            }
        }

        // Re-registrar layer para dynamic song (forceKill/fadeOutForLevelStart
        // lo limpia al entrar a PlayLayer, y onEnter necesita restaurarlo)
        DynamicSongManager::get()->enterLayer(DynSongLayer::LevelInfo);

        // Reproducir inmediatamente — el hook de fadeInMenuMusic en GameManager
        // bloqueara la musica de menu de GD porque m_isDynamicSongActive sera true
        if (Mod::get()->getSettingValue<bool>("dynamic-song") && m_level) {
            DynamicSongManager::get()->playSong(m_level);
        }
    }

    void forcePlayDynamic(float dt) {
        if (m_level) {
            DynamicSongManager::get()->playSong(m_level);
        }
    }

    void updateShaderTime(float dt) {
        if (!m_fields->m_animatedShader) return;
        m_fields->m_shaderTime += dt;

        // actualizar u_time en el sprite principal
        if (m_fields->m_pixelBg) {
            auto sprite = typeinfo_cast<CCSprite*>(static_cast<CCNode*>(m_fields->m_pixelBg));
            if (sprite) {
                auto shader = sprite->getShaderProgram();
                if (shader) {
                    shader->use();
                    GLint loc = shader->getUniformLocationForName("u_time");
                    if (loc >= 0) {
                        shader->setUniformLocationWith1f(loc, m_fields->m_shaderTime);
                    }
                }
            }
        }

        // actualizar u_time en sprites extra (multi-efecto)
        for (auto& extra : m_fields->m_extraBgSprites) {
            if (!extra) continue;
            auto shader = extra->getShaderProgram();
            if (!shader) continue;
            shader->use();
            GLint loc = shader->getUniformLocationForName("u_time");
            if (loc >= 0) {
                shader->setUniformLocationWith1f(loc, m_fields->m_shaderTime);
            }
        }
    }

    $override
    void onExit() {
        this->unschedule(schedule_selector(PaimonLevelInfoLayer::updateGallery));
        this->unschedule(schedule_selector(PaimonLevelInfoLayer::updateShaderTime));
        if (m_fields->m_invalidationListenerId != 0) {
            ThumbnailLoader::get().removeInvalidationListener(m_fields->m_invalidationListenerId);
            m_fields->m_invalidationListenerId = 0;
        }
        m_fields->m_galleryToken++;
        m_fields->m_bgRequestToken++;
        LevelInfoLayer::onExit();
    }

    // Crea el boton de set daily/weekly si aun no existe
    void addSetDailyWeeklyButton() {
        // evitar duplicados
        if (this->getChildByIDRecursive("set-daily-weekly-button"_spr)) return;

        CCSprite* iconSpr = CCSprite::createWithSpriteFrameName("GJ_timeIcon_001.png");
        if (!iconSpr) {
            iconSpr = CCSprite::createWithSpriteFrameName("GJ_starBtn_001.png");
        }
        if (!iconSpr) return;

        iconSpr->setScale(0.8f);

        auto btnSprite = CircleButtonSprite::create(
            iconSpr,
            CircleBaseColor::Green,
            CircleBaseSize::Medium
        );
        if (!btnSprite) return;

        auto btn = CCMenuItemSpriteExtra::create(
            btnSprite,
            this,
            menu_selector(PaimonLevelInfoLayer::onSetDailyWeekly)
        );
        btn->setID("set-daily-weekly-button"_spr);

        auto leftMenu = findLeftSideMenu();
        if (leftMenu) {
            leftMenu->addChild(btn);
            leftMenu->updateLayout();
            ButtonLayoutManager::get().applyLayoutToMenu("LevelInfoLayer", leftMenu);
        }
    }

    void onSetDailyWeekly(CCObject* sender) {
        if (m_level->m_levelID.value() <= 0) return;
        SetDailyWeeklyPopup::create(m_level->m_levelID.value())->show();
    }

    $override
    bool init(GJGameLevel* level, bool challenge) {
        // vinimos de leaderboards?
        if (auto scene = CCDirector::sharedDirector()->getRunningScene()) {
            if (scene->getChildByType<LeaderboardsLayer>(0)) {
                m_fields->m_fromLeaderboards = true;
                m_fields->m_leaderboardType = LeaderboardType::Default;
            }
        }

        if (!LevelInfoLayer::init(level, challenge)) return false;
        // thumbnailLoader::get().pauseQueue(); // removido para permitir carga en segundo plano
        
        if (!level || level->m_levelID <= 0) {
                log::debug("Level ID invalid, skipping thumbnail button");
                return true;
            }

            // Paimon: registrar layer y reproducir dynamic song
            DynamicSongManager::get()->enterLayer(DynSongLayer::LevelInfo);
            if (Mod::get()->getSettingValue<bool>("dynamic-song")) {
                // primer intento (puede ser sobreescrito por GD)
                DynamicSongManager::get()->playSong(level);
            }
            // el retry con delay esta en onEnter()

            // consumir el flag "abierto desde lista de miniaturas"
            bool fromThumbs = paimon::SessionState::consumeFlag(paimon::SessionState::get().verification.openFromThumbs);
            m_fields->m_fromThumbsList = fromThumbs;

            // abierto desde report?
            bool fromReport = paimon::SessionState::consumeFlag(paimon::SessionState::get().verification.openFromReport);
            m_fields->m_fromReportSection = fromReport;
            
            // vinimos de cola verify?
            bool fromVerificationQueue = false;
            int verificationQueueCategory = -1;
            int verificationQueueLevelID = paimon::SessionState::get().verification.queueLevelID;

            if (verificationQueueLevelID == level->m_levelID.value()) {
                fromVerificationQueue = true;
                verificationQueueCategory = paimon::SessionState::get().verification.queueCategory;
                m_fields->m_fromVerificationQueue = true;

                // no limpiar, persistir en playlayer
            }

            // fondo pixel thumb
            bool isMainLevel = level->m_levelType == GJLevelType::Main;
            if (m_fields->m_invalidationListenerId == 0) {
                WeakRef<PaimonLevelInfoLayer> safeRef = this;
                m_fields->m_invalidationListenerId = ThumbnailLoader::get().addInvalidationListener([safeRef](int invalidLevelID) {
                    auto selfRef = safeRef.lock();
                    auto* self = static_cast<PaimonLevelInfoLayer*>(selfRef.data());
                    if (!self || !self->getParent() || !self->m_level) return;
                    if (self->m_level->m_levelID.value() != invalidLevelID) return;
                    self->m_fields->m_loadedInvalidationVersion = ThumbnailLoader::get().getInvalidationVersion(invalidLevelID);
                    self->refreshGalleryData(invalidLevelID, true);
                });
            }
            if (!isMainLevel && !m_fields->m_thumbnailRequested) {
                m_fields->m_thumbnailRequested = true;
                int32_t levelID = level->m_levelID.value();
                std::string fileName = fmt::format("{}.png", levelID);
                Ref<LevelInfoLayer> self = this;
                ThumbnailLoader::get().requestLoad(levelID, fileName, [self, levelID](CCTexture2D* tex, bool success) {
                    if (!self->getParent()) {
                        log::warn("[LevelInfoLayer] Layer invalidated before applying pixel background");
                        return;
                    }
                    if (tex) {
                        static_cast<PaimonLevelInfoLayer*>(self.data())->applyThumbnailBackground(tex, levelID);
                    } else {
                        log::warn("[LevelInfoLayer] No texture for pixel background");
                    }
                }, 5);
            }

            // load layouts botones
            ButtonLayoutManager::get().load();
            
            // menu izq
            auto leftMenu = findLeftSideMenu();
            if (!leftMenu) {
                log::warn("Left side menu not found");
                return true;
            }

            // ref menu pa buttoneditoverlay
            m_fields->m_extraMenu = static_cast<CCMenu*>(leftMenu);
            
            // sprite icono btn (con fallbacks)
            CCSprite* iconSprite = CCSprite::create("paim_BotonMostrarThumbnails.png"_spr);
            if (!iconSprite) iconSprite = CCSprite::createWithSpriteFrameName("GJ_infoIcon_001.png");
            if (!iconSprite) iconSprite = CCSprite::createWithSpriteFrameName("GJ_plusBtn_001.png");
            if (!iconSprite) return true;

            // rotar 90
            iconSprite->setRotation(-90.0f);
            // reducir el icono un 20%
            iconSprite->setScale(0.8f);

            // CircleButtonSprite verde
            auto btnSprite = CircleButtonSprite::create(
                iconSprite,
                CircleBaseColor::Green,
                CircleBaseSize::Medium
            );

            if (!btnSprite) {
                log::error("Failed to create CircleButtonSprite");
                return true;
            }
            
            auto button = CCMenuItemSpriteExtra::create(
                btnSprite,
                this,
                menu_selector(PaimonLevelInfoLayer::onThumbnailButton)
            );
            PaimonButtonHighlighter::registerButton(button);
            
            if (!button) {
                log::error("Failed to create menu button");
                return true;
            }
            
            button->setID("thumbnail-view-button"_spr);
            m_fields->m_thumbnailButton = button;

            // thumbs galeria (URLs versionadas via list endpoint)
            this->refreshGalleryData(level->m_levelID.value(), true);

            // add primero pa layout default
            leftMenu->addChild(button);
            leftMenu->updateLayout();

            ButtonLayout defaultLayout;
            defaultLayout.position = button->getPosition();
            defaultLayout.scale = button->getScale();
            defaultLayout.opacity = 1.0f;
            ButtonLayoutManager::get().setDefaultLayoutIfAbsent("LevelInfoLayer", "thumbnail-view-button", defaultLayout);

            // load layout guardado
            auto savedLayout = ButtonLayoutManager::get().getLayout("LevelInfoLayer", "thumbnail-view-button");
            if (savedLayout) {
                button->setPosition(savedLayout->position);
                button->setScale(savedLayout->scale);
                button->setOpacity(static_cast<GLubyte>(savedLayout->opacity * 255));
            }

            // admin? -> btn daily/weekly
            // Verificacion local primero: si el mod code esta guardado y el usuario
            // esta marcado como admin, mostrar el boton inmediatamente sin esperar al server.
            {
                bool localAdmin = Mod::get()->getSavedValue<bool>("is-verified-admin", false);
                bool hasModCode = !HttpClient::get().getModCode().empty();

                if (localAdmin && hasModCode) {
                    this->addSetDailyWeeklyButton();
                }
            }
            // Verificacion con servidor (refresca cache y puede agregar/quitar el boton)
            if (auto gm = GameManager::get()) {
                auto username = gm->m_playerName;
                auto accountID = 0;
                if (auto am = GJAccountManager::get()) accountID = am->m_accountID;
                
                Ref<LevelInfoLayer> selfMod = this;
                ThumbnailAPI::get().checkModeratorAccount(username, accountID, [selfMod](bool isMod, bool isAdmin) {
                    auto* self = static_cast<PaimonLevelInfoLayer*>(selfMod.data());
                    if (!self->getParent()) return;
                    if (isAdmin) {
                        self->addSetDailyWeeklyButton();
                    }
                });
            }

            log::info("Thumbnail button added successfully");

            // cola verify -> guardar categoria
            if (fromVerificationQueue && verificationQueueLevelID == level->m_levelID.value()) {
                log::info("Nivel abierto desde verificacion (categoria: {}) - boton listo para usar", verificationQueueCategory);
                // categoria pa thumbnailviewpopup
                paimon::SessionState::get().verification.verificationCategory = verificationQueueCategory;
            }

            // apply layouts to left menu to restore any vanilla button customizations
            if (auto menu = static_cast<CCMenu*>(leftMenu)) {
                ButtonLayoutManager::get().applyLayoutToMenu("LevelInfoLayer", menu);
            }

            // botones de aceptar/subir ahora se muestran dentro de thumbnailviewpopup
            
        return true;
    }
    
    void onThumbnailButton(CCObject*) {
        log::info("Thumbnail button clicked");
        
        if (!m_level) {
            log::error("Level is null");
            return;
        }

        int32_t levelID = m_level->m_levelID.value();
        log::info("Opening thumbnail view for level ID: {}", levelID);

        // usar utilidad moderatorverification
        bool canAccept = false; // sin funcionalidad de server
        // contexto popup flag
        paimon::SessionState::get().verification.fromReportPopup = m_fields->m_fromReportSection;
        auto popup = LocalThumbnailViewPopup::create(levelID, canAccept);
        if (popup) {
            popup->show();
        } else {
            log::error("Failed to create thumbnail view popup");
            PaimonNotify::create("Error al abrir miniatura", NotificationIcon::Error)->show();
        }
    }

    void onToggleEditMode(CCObject*) {
        if (!m_fields->m_extraMenu) return;

        // overlay edicion
        auto overlay = ButtonEditOverlay::create("LevelInfoLayer", m_fields->m_extraMenu);
        if (auto scene = CCDirector::sharedDirector()->getRunningScene()) {
            scene->addChild(overlay, 1000);
        }
    }

    void onUploadLocalThumbnail(CCObject*) {
        log::info("[LevelInfoLayer] Upload local thumbnail button clicked");
        
        if (!m_level) {
            PaimonNotify::create(Localization::get().getString("level.error_prefix") + "nivel no encontrado", NotificationIcon::Error)->show();
            return;
        }
        
        // ptr nivel antes async
        auto* level = m_level;
        int32_t levelID = level->m_levelID.value();
        
        // existe thumb local?
        if (!LocalThumbs::get().has(levelID)) {
            PaimonNotify::create(Localization::get().getString("level.no_local_thumb").c_str(), NotificationIcon::Error)->show();
            return;
        }
        
        // obtener nombre de usuario
        std::string username;
        int accountID = 0;
        auto* gm = GameManager::get();
        if (gm) {
            username = gm->m_playerName;
            if (auto* am = GJAccountManager::get()) accountID = am->m_accountID;
        } else {
            log::warn("[LevelInfoLayer] GameManager::get() es null");
            username = "Unknown";
        }
        if (accountID <= 0) {
            PaimonNotify::create(Localization::get().getString("level.account_required").c_str(), NotificationIcon::Error)->show();
            return;
        }
        
        // load local -> png
        auto pathOpt = LocalThumbs::get().getThumbPath(levelID);
        if (!pathOpt) {
            PaimonNotify::create("No se pudo encontrar la miniatura", NotificationIcon::Error)->show();
            return;
        }
        
        std::vector<uint8_t> pngData;
        if (!ImageConverter::loadRgbFileToPng(*pathOpt, pngData)) {
            PaimonNotify::create(Localization::get().getString("level.png_error").c_str(), NotificationIcon::Error)->show();
            return;
        }
        
        WeakRef<PaimonLevelInfoLayer> self = this;

        // check mod
        ThumbnailAPI::get().checkModeratorAccount(username, accountID, [self, levelID, pngData, username](bool isMod, bool isAdmin) {
            auto layer = self.lock();
            if (!layer) return;

            if (isMod || isAdmin) {
                // subir directo (sobrescribe si hay algo, el servidor hace enforcement)
                PaimonNotify::create(Localization::get().getString("capture.uploading").c_str(), NotificationIcon::Info)->show();
                ThumbnailAPI::get().uploadThumbnail(levelID, pngData, username, [self, levelID](bool success, std::string const& msg) {
                    auto layer = self.lock();
                    if (!layer) return;

                    if (success) {
                        PaimonNotify::create(Localization::get().getString("capture.upload_success").c_str(), NotificationIcon::Success)->show();
                        auto path = ThumbnailLoader::get().getCachePath(levelID);
                        std::error_code ec;
                        if (std::filesystem::exists(path, ec)) std::filesystem::remove(path, ec);
                        auto gifPath = ThumbnailLoader::get().getCachePath(levelID, true);
                        if (std::filesystem::exists(gifPath, ec)) std::filesystem::remove(gifPath, ec);
                        ThumbnailLoader::get().invalidateLevel(levelID);
                        ThumbnailLoader::get().requestLoad(levelID, "", [self, levelID](CCTexture2D* tex, bool success) {
                            auto layer = self.lock();
                            if (!layer) return;
                            if (success && tex) {
                                if (layer->m_fields->m_pixelBg) {
                                    layer->m_fields->m_pixelBg->removeFromParent();
                                    layer->m_fields->m_pixelBg = nullptr;
                                }
                                layer->applyThumbnailBackground(tex, levelID);
                            }
                        });
                    } else {
                        PaimonNotify::create(Localization::get().getString("capture.upload_error") + msg, NotificationIcon::Error)->show();
                    }
                });
            } else {
                // user: existe thumb? suggestion vs update
                log::info("[LevelInfoLayer] Regular user upload for level {}", levelID);

                ThumbnailAPI::get().checkExists(levelID, [self, levelID, pngData, username](bool exists) {
                    auto layer = self.lock();
                    if (!layer) return;

                    if (exists) {
                        // existe -> update
                        log::info("[LevelInfoLayer] Uploading as update for level {}", levelID);
                        PaimonNotify::create(Localization::get().getString("capture.uploading_suggestion").c_str(), NotificationIcon::Info)->show();

                        ThumbnailAPI::get().uploadUpdate(levelID, pngData, username, [](bool success, std::string const& msg) {
                            if (success) {
                                PaimonNotify::create(Localization::get().getString("capture.suggested").c_str(), NotificationIcon::Success)->show();
                            } else {
                                PaimonNotify::create(Localization::get().getString("capture.upload_error") + msg, NotificationIcon::Error)->show();
                            }
                        });
                    } else {
                        // si no existe -> enviar como sugerencia
                        log::info("[LevelInfoLayer] Uploading as suggestion for level {}", levelID);
                        PaimonNotify::create(Localization::get().getString("capture.uploading_suggestion").c_str(), NotificationIcon::Info)->show();

                        ThumbnailAPI::get().uploadSuggestion(levelID, pngData, username, [](bool success, std::string const& msg) {
                            if (success) {
                                PaimonNotify::create(Localization::get().getString("capture.suggested").c_str(), NotificationIcon::Success)->show();
                            } else {
                                PaimonNotify::create(Localization::get().getString("capture.upload_error") + msg, NotificationIcon::Error)->show();
                            }
                        });
                    }
                });
            }
        });
    }

    $override
    void onPlay(CCObject* sender) {
        // Fade-out de la dynamic song al entrar al nivel
        DynamicSongManager::get()->fadeOutForLevelStart();
        LevelInfoLayer::onPlay(sender);
    }

    $override
    void onBack(CCObject* sender) {
        auto* dsm = DynamicSongManager::get();
        dsm->exitLayer(DynSongLayer::LevelInfo);

        // Verificar si volvemos a LevelSelectLayer
        bool returnsToLevelSelect = false;
        auto scene = CCDirector::sharedDirector()->getRunningScene();
        if (scene) {
            for (auto* child : CCArrayExt<CCNode*>(scene->getChildren())) {
                if (typeinfo_cast<LevelSelectLayer*>(child)) {
                    returnsToLevelSelect = true;
                    break;
                }
            }
        }

        if (returnsToLevelSelect) {
            // Pre-registrar LevelSelect AHORA para que fadeInMenuMusic de GD
            // no se cuele entre exitLayer(LevelInfo) y LevelSelect::onEnter()
            dsm->enterLayer(DynSongLayer::LevelSelect);
        } else if (dsm->m_isDynamicSongActive) {
            dsm->stopSong();
        }

        if (m_fields->m_fromVerificationQueue) {
            // limpiar los flags
            paimon::SessionState::get().verification.openFromQueue = false;
            paimon::SessionState::get().verification.queueLevelID  = -1;
            paimon::SessionState::get().verification.queueCategory  = -1;
            
            // reabrir popup
            paimon::SessionState::get().verification.reopenQueue = true;
            
            // volver a menulayer
            TransitionManager::get().replaceScene(MenuLayer::scene(false));
            return;
        }

        // abrio desde leaderboards?
        if (m_fields->m_fromLeaderboards) {
            auto scene = LeaderboardsLayer::scene(m_fields->m_leaderboardType, m_fields->m_leaderboardStat);
            TransitionManager::get().replaceScene(scene);
            return;
        }

        // sin anim daily
        
        LevelInfoLayer::onBack(sender);
    }

    void setupGallery() {
        if (auto old = this->getChildByID("gallery-menu"_spr)) {
            old->removeFromParent();
        }
        // flechas
        auto menu = CCMenu::create();
        menu->setID("gallery-menu"_spr);

        if (m_fields->m_thumbnailButton) {
            menu->setPosition(m_fields->m_thumbnailButton->getPosition());
            this->addChild(menu, 100);
        }
    }

    void refreshGalleryData(int32_t levelID, bool refreshBackground) {
        int token = ++m_fields->m_galleryToken;
        Ref<LevelInfoLayer> safeRef = this;
        ThumbnailAPI::get().getThumbnails(levelID, [safeRef, levelID, token, refreshBackground](bool success, std::vector<ThumbnailAPI::ThumbnailInfo> const& thumbs) {
            auto* self = static_cast<PaimonLevelInfoLayer*>(safeRef.data());
            if (!self || !self->getParent() || !self->m_level || self->m_level->m_levelID.value() != levelID) return;
            if (self->m_fields->m_galleryToken != token) return;

            self->m_fields->m_thumbnails.clear();
            if (success) self->m_fields->m_thumbnails = thumbs;
            if (self->m_fields->m_thumbnails.empty()) {
                ThumbnailAPI::ThumbnailInfo mainThumb;
                mainThumb.id = "0";
                mainThumb.url = ThumbnailAPI::get().getThumbnailURL(levelID);
                self->m_fields->m_thumbnails.push_back(mainThumb);
            }

            self->m_fields->m_currentThumbnailIndex = 0;
            self->m_fields->m_cycleTimer = 0.f;
            self->setupGallery();

            bool autoCycleEnabled = Mod::get()->getSettingValue<bool>("levelcell-gallery-autocycle");
            if (self->m_fields->m_thumbnails.size() > 1 && autoCycleEnabled) {
                self->schedule(schedule_selector(PaimonLevelInfoLayer::updateGallery));
            } else {
                self->unschedule(schedule_selector(PaimonLevelInfoLayer::updateGallery));
                if (self->m_fields->m_prevBtn) self->m_fields->m_prevBtn->setVisible(false);
                if (self->m_fields->m_nextBtn) self->m_fields->m_nextBtn->setVisible(false);
            }

            if (refreshBackground) {
                self->loadThumbnail(0);
            }
        });
    }
    
    void onRateBtn(CCObject* sender) {
        // abrir ratepopup con estrella pre-seleccionada? o solo abrirlo.
        // el usuario podria querer ratear directamente.
        // ratepopup maneja logica
        if (m_fields->m_currentThumbnailIndex < 0 || m_fields->m_currentThumbnailIndex >= m_fields->m_thumbnails.size()) return;
        
        auto& thumb = m_fields->m_thumbnails[m_fields->m_currentThumbnailIndex];
        RatePopup::create(m_level->m_levelID.value(), thumb.id)->show();
    }
    
    void updateGallery(float dt) {
        if (!m_fields->m_cycling || m_fields->m_thumbnails.size() <= 1) return;
        
        m_fields->m_cycleTimer += dt;
        if (m_fields->m_cycleTimer >= 3.0f) {
            m_fields->m_cycleTimer = 0.0f;
            m_fields->m_currentThumbnailIndex = (m_fields->m_currentThumbnailIndex + 1) % m_fields->m_thumbnails.size();
            this->loadThumbnail(m_fields->m_currentThumbnailIndex);
        }
    }
    
    void onPrevBtn(CCObject*) {
        m_fields->m_cycling = false; // detener auto-ciclado al interactuar
        m_fields->m_currentThumbnailIndex--;
        if (m_fields->m_currentThumbnailIndex < 0) m_fields->m_currentThumbnailIndex = m_fields->m_thumbnails.size() - 1;
        this->loadThumbnail(m_fields->m_currentThumbnailIndex);
    }
    
    void onNextBtn(CCObject*) {
        m_fields->m_cycling = false;
        m_fields->m_currentThumbnailIndex = (m_fields->m_currentThumbnailIndex + 1) % m_fields->m_thumbnails.size();
        this->loadThumbnail(m_fields->m_currentThumbnailIndex);
    }
    
    void loadThumbnail(int index) {
        if (index < 0 || index >= m_fields->m_thumbnails.size()) return;
        
        auto& thumb = m_fields->m_thumbnails[index];
        int requestToken = ++m_fields->m_bgRequestToken;
        std::string url = thumb.url;
        auto sep = (url.find('?') == std::string::npos) ? "?" : "&";
        url += fmt::format("{}_pv={}{}", sep, thumb.id, requestToken);
        // load desde url â€” use Ref<> for safe prevent use-after-free
        Ref<LevelInfoLayer> safeRef = this;
        ThumbnailAPI::get().downloadFromUrl(url, [safeRef, index, requestToken](bool success, CCTexture2D* tex) {
            auto* self = static_cast<PaimonLevelInfoLayer*>(safeRef.data());
            if (!self->getParent()) return;
            if (self->m_fields->m_bgRequestToken != requestToken) return;
            if (success && tex) {
                if (self->m_fields->m_thumbnailButton) {
                    auto spr = (CCSprite*)self->m_fields->m_thumbnailButton->getNormalImage();
                    if (spr) {
                        spr->setTexture(tex);
                        spr->setTextureRect({0, 0, tex->getContentSize().width, tex->getContentSize().height});
                    }
                }
                int32_t levelID = (index == 0 && self->m_level) ? self->m_level->m_levelID.value() : 0;
                self->applyThumbnailBackground(tex, levelID);
            } else if (self->m_fields->m_thumbnails.size() > 1) {
                int next = (index + 1) % static_cast<int>(self->m_fields->m_thumbnails.size());
                if (next != index) self->loadThumbnail(next);
            }
        });
    }
};

// implementacion de onSettings (necesita PaimonLevelInfoLayer ya definido)
void LocalThumbnailViewPopup::onSettings(CCObject*) {
    auto popup = ThumbnailSettingsPopup::create();
    if (!popup) return;

    // Ref<> mantiene la textura viva mientras el callback exista (copia shared, no raw ptr)
    geode::Ref<CCTexture2D> texRef = m_thumbnailTexture;
    int32_t levelID = m_levelID;

    popup->setOnSettingsChanged([texRef, levelID]() {
        log::info("[ThumbnailViewPopup] Settings changed, refrescando fondo");
        auto scene = CCDirector::sharedDirector()->getRunningScene();
        if (!scene) return;

        auto layer = scene->getChildByType<LevelInfoLayer>(0);
        if (!layer) return;

        // quitar fondos viejos por node ID (seguro)
        if (auto old = layer->getChildByID("paimon-levelinfo-pixel-bg"_spr)) {
            old->removeFromParent();
        }
        if (auto oldOverlay = layer->getChildByID("paimon-levelinfo-pixel-overlay"_spr)) {
            oldOverlay->removeFromParent();
        }

        // resetear la ref interna para evitar double-remove dentro de applyThumbnailBackground
        auto paimon = static_cast<PaimonLevelInfoLayer*>(layer);
        paimon->m_fields->m_pixelBg = nullptr;

        // re-aplicar con las nuevas settings
        if (texRef) {
            paimon->applyThumbnailBackground(texRef, levelID);
        }
    });
    popup->show();
}

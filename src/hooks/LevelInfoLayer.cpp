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
#include "../managers/TransitionManager.hpp"
#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/GJAccountManager.hpp>
#include <vector>
#include <cmath>
#include <filesystem>
#include <sstream>

#include "../managers/LocalThumbs.hpp"
#include "../managers/ThumbnailAPI.hpp"
#include "../managers/ThumbnailLoader.hpp"
#include "../managers/DynamicSongManager.hpp"
#include "../utils/AnimatedGIFSprite.hpp"
#include "../layers/RatePopup.hpp"

#include "../utils/Localization.hpp"
#include "../utils/ImageConverter.hpp"
#include "../utils/HttpClient.hpp"

#include "../layers/ButtonEditOverlay.hpp"
#include "../managers/ButtonLayoutManager.hpp"
#include "../layers/SetDailyWeeklyPopup.hpp"

#include "../utils/Shaders.hpp"

using namespace geode::prelude;
using namespace cocos2d;
using namespace Shaders;

#include "../layers/LocalThumbnailViewPopup.hpp"
#include "../layers/ThumbnailSettingsPopup.hpp"

class $modify(PaimonLevelInfoLayer, LevelInfoLayer) {
    static void onModify(auto& self) {
        // Late = ejecutar despues de otros mods (NodeIDs, BetterInfo, etc.)
        // para que los IDs de nodos ya esten asignados cuando inyectamos thumbnails
        (void)self.setHookPriorityPost("LevelInfoLayer::init", geode::Priority::Late);
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
        
        // multi-thumb
        std::vector<ThumbnailAPI::ThumbnailInfo> m_thumbnails;
        int m_currentThumbnailIndex = 0;
        Ref<CCMenuItemSpriteExtra> m_prevBtn = nullptr;
        Ref<CCMenuItemSpriteExtra> m_nextBtn = nullptr;
        Ref<CCMenuItemSpriteExtra> m_rateBtn = nullptr;
        bool m_cycling = true;
        float m_cycleTimer = 0.0f;
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
        std::string bgStyle = "blur";
        int intensity = 5;
        bgStyle = geode::Mod::get()->getSettingValue<std::string>("levelinfo-background-style");
        intensity = static_cast<int>(geode::Mod::get()->getSettingValue<int64_t>("levelinfo-effect-intensity"));

        intensity = std::max(1, std::min(10, intensity));
        auto win = CCDirector::sharedDirector()->getWinSize();

        // lambda efectos
        auto applyEffects = [this, bgStyle, intensity, win, tex](CCSprite*& sprite, bool isGIF) {
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
                    // modo pixel
                    float t = (intensity - 1) / 9.0f; 
                    float pixelFactor = 0.5f - (t * 0.47f); 
                    int renderWidth = std::max(32, static_cast<int>(win.width * pixelFactor));
                    int renderHeight = std::max(32, static_cast<int>(win.height * pixelFactor));
                    
                    auto renderTex = CCRenderTexture::create(renderWidth, renderHeight);
                    if (renderTex) {
                        float renderScaleX = static_cast<float>(renderWidth) / tex->getContentSize().width;
                        float renderScaleY = static_cast<float>(renderHeight) / tex->getContentSize().height;
                        float renderScale = std::min(renderScaleX, renderScaleY);
                        
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
                            float finalScaleX = win.width / renderWidth;
                            float finalScaleY = win.height / renderHeight;
                            float finalScale = std::max(finalScaleX, finalScaleY);
                            
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
                // shaders ambos
                CCGLProgram* shader = nullptr;
                float val = 0.0f;
                bool useScreenSize = false;
                bool needsTime = false;

                if (bgStyle == "grayscale") {
                    val = intensity / 10.0f;
                    shader = getOrCreateShader("grayscale"_spr, vertexShaderCell, fragmentShaderGrayscale);
                } else if (bgStyle == "sepia") {
                    val = intensity / 10.0f;
                    shader = getOrCreateShader("sepia"_spr, vertexShaderCell, fragmentShaderSepia);
                } else if (bgStyle == "vignette") {
                    val = intensity / 10.0f;
                    shader = getOrCreateShader("vignette"_spr, vertexShaderCell, fragmentShaderVignette);
                } else if (bgStyle == "scanlines") {
                    val = intensity / 10.0f;
                    shader = getOrCreateShader("scanlines"_spr, vertexShaderCell, fragmentShaderScanlines);
                    useScreenSize = true;
                } else if (bgStyle == "bloom") {
                    val = (intensity / 10.0f) * 2.25f;
                    shader = getOrCreateShader("bloom"_spr, vertexShaderCell, fragmentShaderBloom);
                    useScreenSize = true;
                } else if (bgStyle == "chromatic") {
                    val = (intensity / 10.0f) * 2.25f;
                    shader = getOrCreateShader("chromatic-v2"_spr, vertexShaderCell, fragmentShaderChromatic);
                    needsTime = true;
                } else if (bgStyle == "radial-blur") {
                    val = (intensity / 10.0f) * 2.25f;
                    shader = getOrCreateShader("radial-blur-v2"_spr, vertexShaderCell, fragmentShaderRadialBlur);
                    needsTime = true;
                } else if (bgStyle == "glitch") {
                    val = (intensity / 10.0f) * 2.25f;
                    shader = getOrCreateShader("glitch-v2"_spr, vertexShaderCell, fragmentShaderGlitch);
                    needsTime = true;
                } else if (bgStyle == "posterize") {
                    val = intensity / 10.0f;
                    shader = getOrCreateShader("posterize"_spr, vertexShaderCell, fragmentShaderPosterize);
                } else if (bgStyle == "rain") {
                    val = (intensity / 10.0f) * 2.25f;
                    shader = getOrCreateShader("rain"_spr, vertexShaderCell, fragmentShaderRain);
                    needsTime = true;
                } else if (bgStyle == "matrix") {
                    val = (intensity / 10.0f) * 2.25f;
                    shader = getOrCreateShader("matrix"_spr, vertexShaderCell, fragmentShaderMatrix);
                    needsTime = true;
                } else if (bgStyle == "neon-pulse") {
                    val = (intensity / 10.0f) * 2.25f;
                    shader = getOrCreateShader("neon-pulse"_spr, vertexShaderCell, fragmentShaderNeonPulse);
                    needsTime = true;
                } else if (bgStyle == "wave-distortion") {
                    val = (intensity / 10.0f) * 2.25f;
                    shader = getOrCreateShader("wave-distortion"_spr, vertexShaderCell, fragmentShaderWaveDistortion);
                    needsTime = true;
                } else if (bgStyle == "crt") {
                    val = (intensity / 10.0f) * 2.25f;
                    shader = getOrCreateShader("crt"_spr, vertexShaderCell, fragmentShaderCRT);
                    needsTime = true;
                }

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
            
            if (m_fields->m_pixelBg) {
                m_fields->m_pixelBg->removeFromParent();
            } else if (auto old = this->getChildByID("paimon-levelinfo-pixel-bg"_spr)) {
                old->removeFromParent();
            }
            
            finalSprite->setZOrder(-1);
            finalSprite->setID("paimon-levelinfo-pixel-bg"_spr);
            this->addChild(finalSprite);
            m_fields->m_pixelBg = finalSprite;
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
                CCGLProgram* eshader = nullptr;
                float eval = intensityF;
                bool eNeedsTime = false;
                bool eScreenSize = false;

                if (es == "grayscale") { eval = intensity / 10.0f; eshader = getOrCreateShader("grayscale"_spr, vertexShaderCell, fragmentShaderGrayscale); }
                else if (es == "sepia") { eval = intensity / 10.0f; eshader = getOrCreateShader("sepia"_spr, vertexShaderCell, fragmentShaderSepia); }
                else if (es == "vignette") { eval = intensity / 10.0f; eshader = getOrCreateShader("vignette"_spr, vertexShaderCell, fragmentShaderVignette); }
                else if (es == "scanlines") { eval = intensity / 10.0f; eshader = getOrCreateShader("scanlines"_spr, vertexShaderCell, fragmentShaderScanlines); eScreenSize = true; }
                else if (es == "bloom") { eshader = getOrCreateShader("bloom"_spr, vertexShaderCell, fragmentShaderBloom); eScreenSize = true; }
                else if (es == "chromatic") { eshader = getOrCreateShader("chromatic-v2"_spr, vertexShaderCell, fragmentShaderChromatic); eNeedsTime = true; }
                else if (es == "radial-blur") { eshader = getOrCreateShader("radial-blur-v2"_spr, vertexShaderCell, fragmentShaderRadialBlur); eNeedsTime = true; }
                else if (es == "glitch") { eshader = getOrCreateShader("glitch-v2"_spr, vertexShaderCell, fragmentShaderGlitch); eNeedsTime = true; }
                else if (es == "posterize") { eval = intensity / 10.0f; eshader = getOrCreateShader("posterize"_spr, vertexShaderCell, fragmentShaderPosterize); }
                else if (es == "rain") { eshader = getOrCreateShader("rain"_spr, vertexShaderCell, fragmentShaderRain); eNeedsTime = true; }
                else if (es == "matrix") { eshader = getOrCreateShader("matrix"_spr, vertexShaderCell, fragmentShaderMatrix); eNeedsTime = true; }
                else if (es == "neon-pulse") { eshader = getOrCreateShader("neon-pulse"_spr, vertexShaderCell, fragmentShaderNeonPulse); eNeedsTime = true; }
                else if (es == "wave-distortion") { eshader = getOrCreateShader("wave-distortion"_spr, vertexShaderCell, fragmentShaderWaveDistortion); eNeedsTime = true; }
                else if (es == "crt") { eshader = getOrCreateShader("crt"_spr, vertexShaderCell, fragmentShaderCRT); eNeedsTime = true; }

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
             auto path = ThumbnailLoader::get().getCachePath(levelID);
             Ref<LevelInfoLayer> self = this;
             AnimatedGIFSprite::createAsync(path.generic_string(), [self, applyEffects](AnimatedGIFSprite* anim) {
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
    
    void onEnter() {
        LevelInfoLayer::onEnter();
        // delay 0.5s para ganarle a fadeInMenuMusic de GD
        // (mismo patron probado que funciona en LevelSelectLayer)
        if (Mod::get()->getSettingValue<bool>("dynamic-song")) {
            this->scheduleOnce(schedule_selector(PaimonLevelInfoLayer::forcePlayDynamic), 0.5f);
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

    void onExit() {
        this->unschedule(schedule_selector(PaimonLevelInfoLayer::updateGallery));
        this->unschedule(schedule_selector(PaimonLevelInfoLayer::updateShaderTime));
        LevelInfoLayer::onExit();
    }

    void onSetDailyWeekly(CCObject* sender) {
        if (m_level->m_levelID.value() <= 0) return;
        SetDailyWeeklyPopup::create(m_level->m_levelID.value())->show();
    }

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
            bool fromThumbs = Mod::get()->getSavedValue<bool>("open-from-thumbs", false);
            if (fromThumbs) Mod::get()->setSavedValue("open-from-thumbs", false);
            m_fields->m_fromThumbsList = fromThumbs;

            // abierto desde report?
            bool fromReport = Mod::get()->getSavedValue<bool>("open-from-report", false);
            if (fromReport) Mod::get()->setSavedValue("open-from-report", false);
            m_fields->m_fromReportSection = fromReport;
            
            // vinimos de cola verify?
            bool fromVerificationQueue = false;
            int verificationQueueCategory = -1;
            int verificationQueueLevelID = Mod::get()->getSavedValue<int>("verification-queue-levelid", -1);

            if (verificationQueueLevelID == level->m_levelID.value()) {
                fromVerificationQueue = true;
                verificationQueueCategory = Mod::get()->getSavedValue<int>("verification-queue-category", -1);
                m_fields->m_fromVerificationQueue = true;

                // no limpiar, persistir en playlayer
            }

            // fondo pixel thumb
            bool isMainLevel = level->m_levelType == GJLevelType::Main;
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
            auto leftMenu = this->getChildByID("left-side-menu");
            if (!leftMenu) {
                log::warn("Left side menu not found");
                return true;
            }

            // ref menu pa buttoneditoverlay
            m_fields->m_extraMenu = static_cast<CCMenu*>(leftMenu);
            
            // sprite icono btn
            CCSprite* iconSprite = nullptr;

            // recurso custom primero
            iconSprite = CCSprite::create("paim_BotonMostrarThumbnails.png"_spr);

            // fallback camara
            if (!iconSprite) {
                iconSprite = CCSprite::createWithSpriteFrameName("GJ_messagesBtn_001.png");
            }
            
            // fallback info
            if (!iconSprite) {
                iconSprite = CCSprite::createWithSpriteFrameName("GJ_infoIcon_001.png");
            }
            
            if (!iconSprite) {
                log::error("Failed to create button sprite");
                return true;
            }
            
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

            // thumbs galeria
            Ref<LevelInfoLayer> selfGallery = this;
            ThumbnailAPI::get().getThumbnails(level->m_levelID.value(), [selfGallery](bool success, std::vector<ThumbnailAPI::ThumbnailInfo> const& thumbs) {
                auto* self = static_cast<PaimonLevelInfoLayer*>(selfGallery.data());
                if (!self->getParent()) {
                    return;
                }
                
                if (success) {
                    self->m_fields->m_thumbnails = thumbs;
                }
                
                // vacio -> default
                if (self->m_fields->m_thumbnails.empty()) {
                     ThumbnailAPI::ThumbnailInfo mainThumb;
                     mainThumb.id = "0";
                     mainThumb.url = ThumbnailAPI::get().getThumbnailURL(self->m_level->m_levelID.value());
                     self->m_fields->m_thumbnails.push_back(mainThumb);
                }

                if (self->m_fields->m_thumbnails.size() > 1) {
                    self->setupGallery();
                    self->schedule(schedule_selector(PaimonLevelInfoLayer::updateGallery));
                } else {
                    self->setupGallery();
                    if (self->m_fields->m_prevBtn) self->m_fields->m_prevBtn->setVisible(false);
                    if (self->m_fields->m_nextBtn) self->m_fields->m_nextBtn->setVisible(false);
                }
            });


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
            if (auto gm = GameManager::get()) {
                auto username = gm->m_playerName;
                auto accountID = gm->m_playerUserID;
                
                Ref<LevelInfoLayer> selfMod = this;
                HttpClient::get().checkModeratorAccount(username, accountID, [selfMod](bool isMod, bool isAdmin) {
                    auto* self = static_cast<PaimonLevelInfoLayer*>(selfMod.data());
                    if (!self->getParent()) {
                        return;
                    }
                    if (isAdmin) {
                        // btn daily
                        // icono estrella/tiempo
                        CCSprite* iconSpr = CCSprite::createWithSpriteFrameName("GJ_timeIcon_001.png");

                        if (!iconSpr) {
                            iconSpr = CCSprite::createWithSpriteFrameName("GJ_starBtn_001.png");
                        }
                        
                        // reducir el icono un 20%
                        iconSpr->setScale(0.8f);

                        // CircleButtonSprite verde
                        auto btnSprite = CircleButtonSprite::create(
                            iconSpr,
                            CircleBaseColor::Green,
                            CircleBaseSize::Medium
                        );

                        if (!btnSprite) {
                            return;
                        }

                        auto btn = CCMenuItemSpriteExtra::create(
                            btnSprite,
                            self,
                            menu_selector(PaimonLevelInfoLayer::onSetDailyWeekly)
                        );
                        btn->setID("set-daily-weekly-button"_spr);
                        
                        auto leftMenu = static_cast<CCMenu*>(self->getChildByID("left-side-menu"));
                        if (leftMenu) {
                            leftMenu->addChild(btn);
                            leftMenu->updateLayout();
                            ButtonLayoutManager::get().applyLayoutToMenu("LevelInfoLayer", leftMenu);
                        }
                    }
                });
            }

            log::info("Thumbnail button added successfully");

            // cola verify -> guardar categoria
            if (fromVerificationQueue && verificationQueueLevelID == level->m_levelID.value()) {
                log::info("Nivel abierto desde verificacion (categoria: {}) - boton listo para usar", verificationQueueCategory);
                // categoria pa thumbnailviewpopup
                Mod::get()->setSavedValue("verification-category", verificationQueueCategory);
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
        Mod::get()->setSavedValue("from-report-popup", m_fields->m_fromReportSection);
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
        auto* gm = GameManager::get();
        if (gm) {
            username = gm->m_playerName;
        } else {
            log::warn("[LevelInfoLayer] GameManager::get() es null");
            username = "Unknown";
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
        
        // retain self pa callbacks
        this->retain();

        // check mod
        ThumbnailAPI::get().checkModerator(username, [this, levelID, pngData, username](bool isMod, bool isAdmin) {
            if (isMod || isAdmin) {
                auto onFinish = [this, levelID](bool success, std::string const& msg) {
                    if (success) {
                        PaimonNotify::create(Localization::get().getString("capture.upload_success").c_str(), NotificationIcon::Success)->show();
                        auto path = ThumbnailLoader::get().getCachePath(levelID);
                        std::error_code ec;
                        if (std::filesystem::exists(path, ec)) std::filesystem::remove(path, ec);
                        ThumbnailLoader::get().invalidateLevel(levelID);
                        ThumbnailLoader::get().requestLoad(levelID, "", [this, levelID](CCTexture2D* tex, bool success) {
                            if (success && tex) {
                                if (m_fields->m_pixelBg) {
                                    m_fields->m_pixelBg->removeFromParent();
                                    m_fields->m_pixelBg = nullptr;
                                }
                                this->applyThumbnailBackground(tex, levelID);
                            }
                            this->release();
                        });
                        return;
                    } else {
                        PaimonNotify::create(Localization::get().getString("capture.upload_error") + msg, NotificationIcon::Error)->show();
                    }
                    this->release();
                };

                // subir directo (sobrescribe si hay algo, el servidor hace enforcement)
                PaimonNotify::create(Localization::get().getString("capture.uploading").c_str(), NotificationIcon::Info)->show();
                ThumbnailAPI::get().uploadThumbnail(levelID, pngData, username, onFinish);
            } else {
                // user: existe thumb? suggestion vs update
                log::info("[LevelInfoLayer] Regular user upload for level {}", levelID);
                
                ThumbnailAPI::get().checkExists(levelID, [this, levelID, pngData, username](bool exists) {
                    if (exists) {
                        // existe -> update
                        log::info("[LevelInfoLayer] Uploading as update for level {}", levelID);
                        PaimonNotify::create(Localization::get().getString("capture.uploading_suggestion").c_str(), NotificationIcon::Info)->show();
                        
                        ThumbnailAPI::get().uploadUpdate(levelID, pngData, username, [this](bool success, std::string const& msg) {
                            if (success) {
                                PaimonNotify::create(Localization::get().getString("capture.suggested").c_str(), NotificationIcon::Success)->show();
                            } else {
                                PaimonNotify::create(Localization::get().getString("capture.upload_error") + msg, NotificationIcon::Error)->show();
                            }
                            this->release();
                        });
                    } else {
                        // si no existe -> enviar como sugerencia
                        log::info("[LevelInfoLayer] Uploading as suggestion for level {}", levelID);
                        PaimonNotify::create(Localization::get().getString("capture.uploading_suggestion").c_str(), NotificationIcon::Info)->show();
                        
                        ThumbnailAPI::get().uploadSuggestion(levelID, pngData, username, [this](bool success, std::string const& msg) {
                            if (success) {
                                PaimonNotify::create(Localization::get().getString("capture.suggested").c_str(), NotificationIcon::Success)->show();
                            } else {
                                PaimonNotify::create(Localization::get().getString("capture.upload_error") + msg, NotificationIcon::Error)->show();
                            }
                            this->release();
                        });
                    }
                });
            }
        });
    }

    void onPlay(CCObject* sender) {
        // Fade-out de la dynamic song al entrar al nivel
        DynamicSongManager::get()->fadeOutForLevelStart();
        LevelInfoLayer::onPlay(sender);
    }

    void onBack(CCObject* sender) {
        DynamicSongManager::get()->exitLayer(DynSongLayer::LevelInfo);
        DynamicSongManager::get()->stopSong();

        if (m_fields->m_fromVerificationQueue) {
            // limpiar los flags
            Mod::get()->setSavedValue("open-from-verification-queue", false);
            Mod::get()->setSavedValue("verification-queue-levelid", -1);
            Mod::get()->setSavedValue("verification-queue-category", -1);
            
            // reabrir popup
            Mod::get()->setSavedValue("reopen-verification-queue", true);
            
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
        // flechas
        auto menu = CCMenu::create();
        menu->setID("gallery-menu"_spr);

        if (m_fields->m_thumbnailButton) {
            menu->setPosition(m_fields->m_thumbnailButton->getPosition());
            this->addChild(menu, 100);
        }
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
        // load desde url — use Ref<> for safe prevent use-after-free
        Ref<LevelInfoLayer> safeRef = this;
        ThumbnailAPI::get().downloadFromUrl(thumb.url, [safeRef, index](bool success, CCTexture2D* tex) {
            auto* self = static_cast<PaimonLevelInfoLayer*>(safeRef.data());
            if (!self->getParent()) return;
            if (success && tex) {
                // update sprite btn thumb
                if (self->m_fields->m_thumbnailButton) {
                    auto spr = (CCSprite*)self->m_fields->m_thumbnailButton->getNormalImage();
                    if (spr) {
                        spr->setTexture(tex);
                        spr->setTextureRect({0, 0, tex->getContentSize().width, tex->getContentSize().height});
                    }
                }
                // update bg
                int32_t levelID = (index == 0 && self->m_level) ? self->m_level->m_levelID.value() : 0;
                self->applyThumbnailBackground(tex, levelID);
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

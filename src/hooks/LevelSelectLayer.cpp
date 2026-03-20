#include <Geode/modify/LevelSelectLayer.hpp>
#include <Geode/modify/GameManager.hpp>
#include <Geode/modify/FMODAudioEngine.hpp>
#include <Geode/utils/cocos.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/BoomScrollLayer.hpp>
#include <Geode/binding/GJGroundLayer.hpp>
#include <Geode/binding/FMODAudioEngine.hpp>
#include "../features/thumbnails/services/ThumbnailLoader.hpp"
#include "../features/dynamic-songs/services/DynamicSongManager.hpp"
#include "../features/profile-music/services/ProfileMusicManager.hpp"
#include "../utils/Shaders.hpp"
#include <array>

using namespace geode::prelude;
using namespace Shaders;

// evitar que GameManager pise la cancion dinamica o la de perfil
class $modify(PaimonGameManager, GameManager) {
    static void onModify(auto& self) {
        // otros hooks van primero, nosotros bloqueamos al final si hace falta
        (void)self.setHookPriorityPre("GameManager::fadeInMenuMusic", geode::Priority::Last);
    }

    $override
    void fadeInMenuMusic() {
        bool passthrough = Mod::get()->getSavedValue<bool>("music-hook-passthrough", false);
        if (passthrough) {
            GameManager::fadeInMenuMusic();
            return;
        }
        auto* dsm = DynamicSongManager::get();
        // bloquear si dynamic song esta sonando en un layer valido
        if (dsm->m_isDynamicSongActive && dsm->isInValidLayer()) {
            return;
        }
        // tampoco si esta la musica de perfil
        if (ProfileMusicManager::get().isPlaying()) {
            return;
        }
        GameManager::fadeInMenuMusic();
    }
};

// evitar que GD reinicie la musica en transiciones
// nuestras llamadas propias pasan por s_selfPlayMusic
class $modify(PaimonFMODAudioEngine, FMODAudioEngine) {
    static void onModify(auto& self) {
        // mismo esquema que fadeInMenuMusic
        (void)self.setHookPriorityPre("FMODAudioEngine::playMusic", geode::Priority::Last);
    }

    $override
    void playMusic(gd::string path, bool shouldLoop, float fadeInTime, int channel) {
        bool passthrough = Mod::get()->getSavedValue<bool>("music-hook-passthrough", false);
        if (passthrough) {
            FMODAudioEngine::playMusic(path, shouldLoop, fadeInTime, channel);
            return;
        }
        if (!DynamicSongManager::s_selfPlayMusic) {
            auto* dsm = DynamicSongManager::get();
            if (dsm->m_isDynamicSongActive && dsm->isInValidLayer()) {
                return;
            }
        }
        FMODAudioEngine::playMusic(path, shouldLoop, fadeInTime, channel);
    }
};

class $modify(PaimonLevelSelectLayer, LevelSelectLayer) {
    static void onModify(auto& self) {
        (void)self.setHookPriorityPost("LevelSelectLayer::init", geode::Priority::Late);
    }

    struct Fields {
        Ref<CCSprite> m_bgSprite = nullptr;
        Ref<CCSprite> m_sharpBgSprite = nullptr;
        Ref<CCNode> m_accentRoot = nullptr;
        Ref<CCLayerColor> m_accentGlow = nullptr;
        Ref<CCDrawNode> m_accentBorder = nullptr;
        int m_currentLevelID = 0;
        int m_accentLevelID = -9999;
        bool m_accentHasTexture = false;
        float m_pageCheckTimer = 0.f;
        float m_smoothedPeak = 0.f;
        int m_verifyFrameCounter = 0;  // verificar musica cada ~1s
        bool m_meteringEnabled = false;
    };

    $override
    bool init(int p0) {
        if (!LevelSelectLayer::init(p0)) return false;

        DynamicSongManager::get()->enterLayer(DynSongLayer::LevelSelect);

        // pagina 0 = nivel 1 (Stereo Madness)
        int levelID = p0 + 1;
        m_fields->m_currentLevelID = levelID;
        
        // arrancar la cancion del nivel de entrada, saltando el default de GD
        if (Mod::get()->getSettingValue<bool>("dynamic-song")) {
             auto level = GameLevelManager::sharedState()->getMainLevel(levelID, false);
             if (level) {
                 DynamicSongManager::get()->playSong(level);
             }
        }
        
        // bg del nivel
        this->updateThumbnailBackground(levelID);
        this->updateAccentOverlay(levelID, false);
        
        // quitar el fondo que GD pone
        CCArray* children = this->getChildren();
        if (children) {
            for (auto* node : CCArrayExt<CCNode*>(children)) {
                if (!node) continue;
                if (node->getZOrder() < -1) {
                    node->setVisible(false);
                }
                
                // quitar ground tambien
                if (typeinfo_cast<GJGroundLayer*>(node)) {
                    node->setVisible(false);
                }
            }
        }
        
        this->schedule(schedule_selector(PaimonLevelSelectLayer::checkPageLoop));
        
        return true;
    }

    $override
    void onEnter() {
        LevelSelectLayer::onEnter();
        
        // re-registrar (PlayLayer lo borra con forceKill)
        auto* dsm = DynamicSongManager::get();
        dsm->enterLayer(DynSongLayer::LevelSelect);
        
        if (Mod::get()->getSettingValue<bool>("dynamic-song")) {
            // si ya suena pa este nivel, no relanzar (evita corte al volver de info)
            if (dsm->m_isDynamicSongActive) {
                return;
            }
            // primera vez o volviendo de PlayLayer
            this->scheduleOnce(schedule_selector(PaimonLevelSelectLayer::forcePlayMusic), 0.5f);
        }
    }

    $override
    void onExit() {
        this->unschedule(schedule_selector(PaimonLevelSelectLayer::checkPageLoop));
        this->removeAccentOverlay();
        // parar musica y desregistrar
        DynamicSongManager::get()->exitLayer(DynSongLayer::LevelSelect);
        DynamicSongManager::get()->stopSong();
        LevelSelectLayer::onExit();
    }

    void forcePlayMusic(float dt) {
         // playSong ya maneja transiciones, no necesito stopSong antes

         int levelID = m_fields->m_currentLevelID;
         if (levelID <= 0) levelID = 1;
         
         auto level = GameLevelManager::sharedState()->getMainLevel(levelID, false);
         if (level) {
             // arrancar
             DynamicSongManager::get()->playSong(level);
         }
    }

    void checkPageLoop(float dt) {
        if (!m_scrollLayer) return;

        // calcular pagina a mano en vez de usar m_page (que lagea al scrollear)

        CCLayer* pagesLayer = m_scrollLayer->m_extendedLayer;
        if (!pagesLayer) return;

        float x = pagesLayer->getPositionX();
        float width = m_scrollLayer->getContentSize().width;
        
        // page = round(-x / width)  (X negativa al scrollear a la derecha)

        int page = 0;
        if (width > 0) {
            page = static_cast<int>(std::round(-x / width));
        }
        
        // “circulo virtual”: 2 secciones vacias despues de los 22 niveles
        // ciclo: [1..22] [empty] [empty] y repite
        // tamano del ciclo = 22 + 2 = 24

        const int totalLevels = 22;
        const int emptySections = 2;
        const int cycleSize = totalLevels + emptySections;
        
        // wrap a 0..23 (funciona con negativo tambien)
        int cycleIndex = (page % cycleSize + cycleSize) % cycleSize;
        
        int levelID = -1;
        

        // indices 0..21 → niveles 1..22
        if (cycleIndex < totalLevels) {
            levelID = cycleIndex + 1;
        } 
        
        // cambio de pagina: actualizar cancion y fondo
        if (m_fields->m_currentLevelID != levelID) {
            m_fields->m_currentLevelID = levelID;
            this->updateAccentOverlay(levelID, false);

            // nueva cancion pa el nivel
            if (Mod::get()->getSettingValue<bool>("dynamic-song")) {
                if (levelID != -1) {
                    if (auto level = GameLevelManager::sharedState()->getMainLevel(levelID, false)) {
                        DynamicSongManager::get()->playSong(level);
                    }
                }
            }

            // nuevo fondo
            this->updateThumbnailBackground(levelID);
        }
        // chequeo cada ~1s si otro mod se metio con la musica
        if (Mod::get()->getSettingValue<bool>("dynamic-song")) {
            // asegurar registro
            auto* dsm = DynamicSongManager::get();
            if (!dsm->isInValidLayer()) {
                dsm->enterLayer(DynSongLayer::LevelSelect);
            }

            m_fields->m_verifyFrameCounter++;
            if (m_fields->m_verifyFrameCounter >= 60) {
                m_fields->m_verifyFrameCounter = 0;
                // no verificar durante transiciones
                if (dsm->m_isDynamicSongActive && !dsm->isTransitioning() && !dsm->verifyPlayback()) {
                    dsm->exitLayer(DynSongLayer::LevelSelect);
                    dsm->onPlaybackHijacked();
                }
            }
        }
        // logica del efecto “pulso” con la musica
        if (m_fields->m_bgSprite && Mod::get()->getSettingValue<bool>("dynamic-song")) {
             // master channel group pa leer picos
             auto engine = FMODAudioEngine::sharedEngine();
             if (engine->m_system) {
                 FMOD::ChannelGroup* masterGroup = nullptr;
                 engine->m_system->getMasterChannelGroup(&masterGroup);
                 
                 if (masterGroup) {
                     FMOD::DSP* headDSP = nullptr;
                     masterGroup->getDSP(FMOD_CHANNELCONTROL_DSP_HEAD, &headDSP);
                     
                     if (headDSP) {
                         if (!m_fields->m_meteringEnabled) {
                             headDSP->setMeteringEnabled(false, true);
                             m_fields->m_meteringEnabled = true;
                         }

                         FMOD_DSP_METERING_INFO meteringInfo = {};
                         headDSP->getMeteringInfo(nullptr, &meteringInfo);
                         
                         float peak = 0.f;
                         if (meteringInfo.numchannels > 0) {
                             for (int i=0; i<meteringInfo.numchannels; i++) {
                                 if (meteringInfo.peaklevel[i] > peak) peak = meteringInfo.peaklevel[i];
                             }
                         }
                         
                         // smoothing: ataque rapido, decay lento
                         if (peak > m_fields->m_smoothedPeak) {
                             m_fields->m_smoothedPeak = peak;
                         } else {
                             m_fields->m_smoothedPeak -= dt * 1.5f; // velocidad de “decay”
                             if (m_fields->m_smoothedPeak < 0.f) m_fields->m_smoothedPeak = 0.f;
                         }
                         
                         // bajar sensibilidad un 30%
                         float val = m_fields->m_smoothedPeak * 0.7f;

                         // brillo: 80 base → 255 en pico
                         float brightnessVal = 80.f + (val * 175.f);
                         if (brightnessVal > 255.f) brightnessVal = 255.f;
                         GLubyte cVal = static_cast<GLubyte>(brightnessVal);

                         if (m_fields->m_bgSprite) {
                             m_fields->m_bgSprite->setColor({cVal, cVal, cVal});
                         }
                         
                         if (m_fields->m_sharpBgSprite) {
                             GLubyte sharpVal = static_cast<GLubyte>(cVal * 0.9f); 
                             m_fields->m_sharpBgSprite->setColor({sharpVal, sharpVal, sharpVal});
                         }
                     }
                 }
             }
        }
    }
    

    void updateThumbnailBackground(int levelID) {
        // solo niveles 1-22 tienen thumbnail
        bool isMainLevel = (levelID >= 1 && levelID <= 22);

        if (!isMainLevel) {
             // secciones vacias = negro
             this->applyBackground(nullptr, levelID); 
             return;
        }

        std::string fileName = fmt::format("{}.png", levelID);

        Ref<LevelSelectLayer> self = this;

        ThumbnailLoader::get().requestLoad(levelID, fileName, [self, levelID](CCTexture2D* tex, bool success) {
            // si cambio de pagina mientras cargaba, ignorar
            auto* layer = static_cast<PaimonLevelSelectLayer*>(self.data());
            if (layer->m_fields->m_currentLevelID == levelID) {
                if (success && tex) {
                    layer->applyBackground(tex, levelID);
                } else {
                    layer->applyBackground(nullptr, levelID);
                }
            }
        }, 5);
    }
    
    
    void applyBackground(CCTexture2D* tex, int levelID = -1) {
        auto win = CCDirector::sharedDirector()->getWinSize();
        CCSprite* finalSprite = nullptr;
        CCSprite* sharpSprite = nullptr;
    
        // limpiar fondos anteriores con fade
        auto fadeAndRemove = [](Ref<CCSprite>& spr) {
            if (spr) {
                spr->stopAllActions();
                spr->runAction(CCSequence::create(CCFadeOut::create(0.5f), CCRemoveSelf::create(), nullptr));
                spr = nullptr;
            }
        };
        fadeAndRemove(m_fields->m_bgSprite);
        fadeAndRemove(m_fields->m_sharpBgSprite);

        if (tex) {
            sharpSprite = CCSprite::createWithTexture(tex);
            CCSize texSize = tex->getContentSize();
            finalSprite = Shaders::createBlurredSprite(tex, texSize, 4.0f, true);
            
            if (finalSprite) {
                    float scaleX = win.width / finalSprite->getContentSize().width;
                    float scaleY = win.height / finalSprite->getContentSize().height;
                    float scale = std::max(scaleX, scaleY);

                    // configura sprite de fondo con fade-in y zoom lento
                    auto setupBgSprite = [&](CCSprite* spr, int z, GLubyte startAlpha, GLubyte targetAlpha, ccColor3B tint) {
                        spr->setScale(scale);
                        spr->setPosition(win / 2);
                        spr->setColor(tint);
                        spr->setZOrder(z);
                        spr->setOpacity(0);
                        this->addChild(spr);
                        spr->runAction(CCFadeTo::create(0.5f, targetAlpha));
                        spr->runAction(CCRepeatForever::create(CCSequence::create(
                            CCScaleTo::create(10.0f, scale * 1.3f),
                            CCScaleTo::create(10.0f, scale),
                            nullptr
                        )));
                    };

                    setupBgSprite(sharpSprite, -11, 0, 255, {80, 80, 80});
                    setupBgSprite(finalSprite, -10, 0, 255, {255, 255, 255});
            }
        }
        
        m_fields->m_bgSprite = finalSprite;
        m_fields->m_sharpBgSprite = sharpSprite;
        this->updateAccentOverlay(levelID, tex != nullptr);
    }
    

    void cleanupDynamicSong() {
        DynamicSongManager::get()->exitLayer(DynSongLayer::LevelSelect);
        DynamicSongManager::get()->stopSong();
    }

    ccColor4B getAccentColor(int levelID) {
        static std::array<ccColor4B, 6> const palette = {
            ccColor4B{120, 210, 255, 255},
            ccColor4B{142, 255, 185, 255},
            ccColor4B{255, 210, 120, 255},
            ccColor4B{255, 150, 185, 255},
            ccColor4B{190, 165, 255, 255},
            ccColor4B{255, 240, 155, 255},
        };
        if (levelID <= 0) return {180, 180, 180, 255};
        return palette[static_cast<size_t>((levelID - 1) % static_cast<int>(palette.size()))];
    }

    void removeAccentOverlay() {
        if (m_fields->m_accentRoot) {
            m_fields->m_accentRoot->stopAllActions();
            m_fields->m_accentRoot->removeFromParent();
            m_fields->m_accentRoot = nullptr;
            m_fields->m_accentGlow = nullptr;
            m_fields->m_accentBorder = nullptr;
        }
        m_fields->m_accentLevelID = -9999;
        m_fields->m_accentHasTexture = false;
    }

    void updateAccentOverlay(int levelID, bool hasTexture) {
        if (m_fields->m_accentRoot &&
            m_fields->m_accentLevelID == levelID &&
            m_fields->m_accentHasTexture == hasTexture) {
            return;
        }

        this->removeAccentOverlay();

        auto win = CCDirector::sharedDirector()->getWinSize();
        float marginX = 20.f;
        float marginY = 16.f;
        float width = std::max(1.f, win.width - marginX * 2.f);
        float height = std::max(1.f, win.height - marginY * 2.f);
        CCPoint origin = ccp(marginX, marginY);

        auto accentRoot = CCNode::create();
        accentRoot->setID("paimon-levelselect-accent-root"_spr);
        accentRoot->setAnchorPoint(ccp(0.5f, 0.5f));
        accentRoot->setPosition(ccp(win.width * 0.5f, win.height * 0.5f));
        this->addChild(accentRoot, 2);

        ccColor4B color = getAccentColor(levelID);
        GLubyte glowAlpha = hasTexture ? 28 : 14;
        auto glow = CCLayerColor::create(ccc4(color.r, color.g, color.b, glowAlpha));
        glow->setContentSize(CCSize(width - 10.f, height - 10.f));
        glow->setAnchorPoint(ccp(0.f, 0.f));
        glow->setPosition(ccp(origin.x + 5.f, origin.y + 5.f));
        glow->setID("paimon-levelselect-accent-glow"_spr);
        accentRoot->addChild(glow, 0);

        auto border = CCDrawNode::create();
        border->setID("paimon-levelselect-accent-border"_spr);
        constexpr float chamfer = 9.f;
        CCPoint pts[8] = {
            ccp(origin.x + chamfer, origin.y),
            ccp(origin.x + width - chamfer, origin.y),
            ccp(origin.x + width, origin.y + chamfer),
            ccp(origin.x + width, origin.y + height - chamfer),
            ccp(origin.x + width - chamfer, origin.y + height),
            ccp(origin.x + chamfer, origin.y + height),
            ccp(origin.x, origin.y + height - chamfer),
            ccp(origin.x, origin.y + chamfer),
        };
        ccColor4F fill = {0.f, 0.f, 0.f, 0.f};
        ccColor4F line = {
            static_cast<float>(color.r) / 255.f,
            static_cast<float>(color.g) / 255.f,
            static_cast<float>(color.b) / 255.f,
            hasTexture ? 0.85f : 0.55f
        };
        border->drawPolygon(pts, 8, fill, 2.1f, line);
        accentRoot->addChild(border, 1);

        accentRoot->runAction(CCRepeatForever::create(CCSequence::create(
            CCScaleTo::create(1.2f, 1.006f),
            CCScaleTo::create(1.2f, 1.0f),
            nullptr
        )));
        glow->runAction(CCRepeatForever::create(CCSequence::create(
            CCFadeTo::create(1.1f, static_cast<GLubyte>(glowAlpha + 12)),
            CCFadeTo::create(1.1f, glowAlpha),
            nullptr
        )));

        m_fields->m_accentRoot = accentRoot;
        m_fields->m_accentGlow = glow;
        m_fields->m_accentBorder = border;
        m_fields->m_accentLevelID = levelID;
        m_fields->m_accentHasTexture = hasTexture;
    }

    $override
    void onBack(CCObject* sender) {
        cleanupDynamicSong();
        LevelSelectLayer::onBack(sender);
    }
    
    $override
    void keyBackClicked() {
        cleanupDynamicSong();
        LevelSelectLayer::keyBackClicked();
    }
};

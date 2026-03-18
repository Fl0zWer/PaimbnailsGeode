#include <Geode/modify/LevelSelectLayer.hpp>
#include <Geode/modify/GameManager.hpp>
#include <Geode/modify/FMODAudioEngine.hpp>
#include <Geode/utils/cocos.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/BoomScrollLayer.hpp>
#include <Geode/binding/GJGroundLayer.hpp>
#include <Geode/binding/FMODAudioEngine.hpp> // acceso directo a FMOD
#include "../features/thumbnails/services/ThumbnailLoader.hpp"
#include "../features/dynamic-songs/services/DynamicSongManager.hpp"
#include "../features/profile-music/services/ProfileMusicManager.hpp"
#include "../utils/Shaders.hpp"

using namespace geode::prelude;
using namespace Shaders;

// hook a GameManager para que no pise nuestra cancion dinamica ni la de perfil
class $modify(PaimonGameManager, GameManager) {
    static void onModify(auto& self) {
        // Este hook puede bloquear el original; dejamos que otros pre-hooks corran antes.
        (void)self.setHookPriorityPre("GameManager::fadeInMenuMusic", geode::Priority::Last);
    }

    $override
    void fadeInMenuMusic() {
        auto* dsm = DynamicSongManager::get();
        // Solo bloquear si dynamic song esta activa Y estamos en un layer valido
        if (dsm->m_isDynamicSongActive && dsm->isInValidLayer()) {
            return;
        }
        // Bloquear si ProfileMusic esta reproduciendose
        if (ProfileMusicManager::get().isPlaying()) {
            return;
        }
        GameManager::fadeInMenuMusic();
    }
};

// hook a FMODAudioEngine::playMusic para bloquear que GD reinicie la musica
// durante transiciones cuando nuestra dynamic song esta activa.
// Nuestras propias llamadas usan s_selfPlayMusic como bypass.
class $modify(PaimonFMODAudioEngine, FMODAudioEngine) {
    static void onModify(auto& self) {
        // Igual que fadeInMenuMusic: supresion condicional justo antes del original.
        (void)self.setHookPriorityPre("FMODAudioEngine::playMusic", geode::Priority::Last);
    }

    $override
    void playMusic(gd::string path, bool shouldLoop, float fadeInTime, int channel) {
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
        int m_currentLevelID = 0;
        float m_pageCheckTimer = 0.f;
        float m_smoothedPeak = 0.f;
        int m_verifyFrameCounter = 0;  // contador para verificacion periodica (~1s)
    };

    $override
    bool init(int p0) {
        if (!LevelSelectLayer::init(p0)) return false;

        // Registrar que estamos en LevelSelectLayer
        DynamicSongManager::get()->enterLayer(DynSongLayer::LevelSelect);

        // dynamic song + background: setup inicial
        // en GD normal: pagina 0 = Stereo Madness (id 1)
        int levelID = p0 + 1;
        m_fields->m_currentLevelID = levelID;
        
        // fuerzo update inmediato en init para saltarme el comportamiento de musica por defecto
        if (Mod::get()->getSettingValue<bool>("dynamic-song")) {
             auto level = GameLevelManager::sharedState()->getMainLevel(levelID, false);
             if (level) {
                 DynamicSongManager::get()->playSong(level);
             }
        }
        
        // pongo el background inicial
        this->updateThumbnailBackground(levelID);
        
        // oculto el background por defecto del juego
        CCArray* children = this->getChildren();
        if (children) {
            for (auto* node : CCArrayExt<CCNode*>(children)) {
                if (!node) continue;
                if (node->getZOrder() < -1) {
                    node->setVisible(false);
                }
                
                // oculto el ground layer si hace falta
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
        
        // Re-registrar layer (PlayLayer lo limpia con forceKill)
        auto* dsm = DynamicSongManager::get();
        dsm->enterLayer(DynSongLayer::LevelSelect);
        
        if (Mod::get()->getSettingValue<bool>("dynamic-song")) {
            // Si la dynamic song ya esta activa para el nivel actual,
            // no relanzar — evita stutter al volver de InfoLayer
            if (dsm->m_isDynamicSongActive) {
                return;
            }
            // Primera entrada o viniendo de PlayLayer (forceKill limpio todo)
            this->scheduleOnce(schedule_selector(PaimonLevelSelectLayer::forcePlayMusic), 0.5f);
        }
    }

    $override
    void onExit() {
        // Registrar salida del layer y detener dynamic song
        DynamicSongManager::get()->exitLayer(DynSongLayer::LevelSelect);
        DynamicSongManager::get()->stopSong();
        LevelSelectLayer::onExit();
    }

    void forcePlayMusic(float dt) {
         // paro lo que este sonando para dejar el estado limpio
         // DynamicSongManager::get()->stopSong(); // en verdad playSong ya maneja transiciones

         int levelID = m_fields->m_currentLevelID;
         if (levelID <= 0) levelID = 1;
         
         auto level = GameLevelManager::sharedState()->getMainLevel(levelID, false);
         if (level) {
             // fuerzo play
             DynamicSongManager::get()->playSong(level);
         }
    }

    void checkPageLoop(float dt) {
        if (!m_scrollLayer) return;

        // deteccion de posicion mejorada:
        // calculo la pagina a mano mirando la posicion real del scroll
        // esto es mas preciso que m_page, que puede ir con lag al scrollear

        CCLayer* pagesLayer = m_scrollLayer->m_extendedLayer;
        if (!pagesLayer) return;

        float x = pagesLayer->getPositionX();
        float width = m_scrollLayer->getContentSize().width;
        
        // indice de pagina basado en pos X
        // X suele ser negativa si scrolleas a la derecha:
        // pagina 0 = 0, pagina 1 = -width, etc.
        // asi que page ≈ round(-x / width)

        int page = 0;
        if (width > 0) {
            page = static_cast<int>(std::round(-x / width));
        }
        
        // “circulo virtual”: 2 secciones vacias despues de los 22 niveles
        // ciclo: [1..22] [empty] [empty] y repite
        // tamano del ciclo = 22 + 2 = 24

        const int totalLevels = 22;
        const int emptySections = 2; // dos secciones vacias antes de volver a level 1
        const int cycleSize = totalLevels + emptySections;
        
        // normalizo la pagina a 0..23 (tambien si hay paginas negativas)
        // esto crea un bucle infinito para elegir background
        int cycleIndex = (page % cycleSize + cycleSize) % cycleSize;
        
        int levelID = -1;
        

        // 0..21 representan niveles 1..22
        if (cycleIndex < totalLevels) {
            levelID = cycleIndex + 1;
        } 
        
        // Paimon: actualizacion de cancion dinamica + fondo
        if (m_fields->m_currentLevelID != levelID) {
            m_fields->m_currentLevelID = levelID;

            // lanzo la dynamic song del nivel nuevo
            if (Mod::get()->getSettingValue<bool>("dynamic-song")) {
                if (levelID != -1) {
                    if (auto level = GameLevelManager::sharedState()->getMainLevel(levelID, false)) {
                        DynamicSongManager::get()->playSong(level);
                    }
                }
            }

            // actualizo el background a juego
            this->updateThumbnailBackground(levelID);
        }
        // Verificacion periodica (~1s a 60fps): detectar si otro mod cambio la musica
        if (Mod::get()->getSettingValue<bool>("dynamic-song")) {
            // Asegurar que estamos registrados como layer activo
            auto* dsm = DynamicSongManager::get();
            if (!dsm->isInValidLayer()) {
                dsm->enterLayer(DynSongLayer::LevelSelect);
            }

            m_fields->m_verifyFrameCounter++;
            if (m_fields->m_verifyFrameCounter >= 60) {
                m_fields->m_verifyFrameCounter = 0;
                // Solo verificar si no estamos en medio de una transicion
                if (dsm->m_isDynamicSongActive && !dsm->isTransitioning() && !dsm->verifyPlayback()) {
                    dsm->exitLayer(DynSongLayer::LevelSelect);
                    dsm->onPlaybackHijacked();
                }
            }
        }
        // logica del efecto “pulso” con la musica
        if (m_fields->m_bgSprite && Mod::get()->getSettingValue<bool>("dynamic-song")) {
             // con FMOD miro el master channel group
             auto engine = FMODAudioEngine::sharedEngine();
             if (engine->m_system) {
                 FMOD::ChannelGroup* masterGroup = nullptr;
                 engine->m_system->getMasterChannelGroup(&masterGroup);
                 
                 if (masterGroup) {
                     FMOD::DSP* headDSP = nullptr;
                     masterGroup->getDSP(FMOD_CHANNELCONTROL_DSP_HEAD, &headDSP);
                     
                     if (headDSP) {
                         headDSP->setMeteringEnabled(false, true); // habilito el metering de salida

                         FMOD_DSP_METERING_INFO meteringInfo = {};
                         headDSP->getMeteringInfo(nullptr, &meteringInfo);
                         
                         float peak = 0.f;
                         if (meteringInfo.numchannels > 0) {
                             for (int i=0; i<meteringInfo.numchannels; i++) {
                                 if (meteringInfo.peaklevel[i] > peak) peak = meteringInfo.peaklevel[i];
                             }
                         }
                         
                         // suavizado: ataque rapido, release lento
                         if (peak > m_fields->m_smoothedPeak) {
                             m_fields->m_smoothedPeak = peak;
                         } else {
                             m_fields->m_smoothedPeak -= dt * 1.5f; // velocidad de “decay”
                             if (m_fields->m_smoothedPeak < 0.f) m_fields->m_smoothedPeak = 0.f;
                         }
                         
                         // bajo un poco la sensibilidad (~30 %)
                         float val = m_fields->m_smoothedPeak * 0.7f;

                         // brillo: base 80 -> pico 255
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
        // este hook solo aplica a los 22 niveles main (1–22)
        bool isMainLevel = (levelID >= 1 && levelID <= 22);

        if (!isMainLevel) {
             // las secciones vacias se quedan con fondo negro puro
             this->applyBackground(nullptr, levelID); 
             return;
        }

        // nombre de archivo de la mini
        std::string fileName = fmt::format("{}.png", levelID);
        
        // Ref<> mantiene vivo el layer hasta que el callback termine
        Ref<LevelSelectLayer> self = this;

        ThumbnailLoader::get().requestLoad(levelID, fileName, [self, levelID](CCTexture2D* tex, bool success) {
            // por si el usuario se fue a otra pagina mientras cargaba
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
    
    // logica applyGroundColor eliminada (BG lo manejamos nosotros)
    
    void applyBackground(CCTexture2D* tex, int levelID = -1) {
        auto win = CCDirector::sharedDirector()->getWinSize();
        CCSprite* finalSprite = nullptr; // capa blur
        CCSprite* sharpSprite = nullptr; // capa nitida
    
        // limpio los fondos anteriores
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
            finalSprite = Shaders::createBlurredSprite(tex, texSize, 4.0f, true); // radio 4.0 directo para blur fuerte
            
            if (finalSprite) {
                    float scaleX = win.width / finalSprite->getContentSize().width;
                    float scaleY = win.height / finalSprite->getContentSize().height;
                    float scale = std::max(scaleX, scaleY);

                    // helper: configura y anima un sprite de fondo
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
        
        m_fields->m_bgSprite = finalSprite; // blur principal
        m_fields->m_sharpBgSprite = sharpSprite;
    }
    

    void cleanupDynamicSong() {
        DynamicSongManager::get()->exitLayer(DynSongLayer::LevelSelect);
        DynamicSongManager::get()->stopSong();
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

#include <Geode/modify/LevelSelectLayer.hpp>
#include <Geode/modify/GameManager.hpp>
#include <Geode/utils/cocos.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/BoomScrollLayer.hpp>
#include <Geode/binding/GJGroundLayer.hpp>
#include <Geode/binding/FMODAudioEngine.hpp> // acceso directo a FMOD
#include "../managers/ThumbnailLoader.hpp"
#include "../managers/DynamicSongManager.hpp"
#include "../managers/ProfileMusicManager.hpp"
#include "../utils/Shaders.hpp"

using namespace geode::prelude;
using namespace Shaders;

// hook a GameManager para que no pise nuestra cancion dinamica ni la de perfil
class $modify(PaimonGameManager, GameManager) {
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
    };

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

    void onEnter() {
        LevelSelectLayer::onEnter();
        
        // me aseguro de que la dynamic song suene al entrar al layer
        // (tambien cuando vuelves de PlayLayer/LevelInfo)
        if (Mod::get()->getSettingValue<bool>("dynamic-song")) {
             // subo el delay a 0.5s para ganarle seguro al GameManager
             this->scheduleOnce(schedule_selector(PaimonLevelSelectLayer::forcePlayMusic), 0.5f);
        }
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
        if (m_fields->m_bgSprite) {
            m_fields->m_bgSprite->stopAllActions();
            m_fields->m_bgSprite->runAction(CCSequence::create(CCFadeOut::create(0.5f), CCRemoveSelf::create(), nullptr));
            m_fields->m_bgSprite = nullptr;
        }
        if (m_fields->m_sharpBgSprite) {
            m_fields->m_sharpBgSprite->stopAllActions();
            m_fields->m_sharpBgSprite->runAction(CCSequence::create(CCFadeOut::create(0.5f), CCRemoveSelf::create(), nullptr));
            m_fields->m_sharpBgSprite = nullptr;
        }

        if (tex) {
            sharpSprite = CCSprite::createWithTexture(tex);
            CCSize texSize = tex->getContentSize();
            finalSprite = Shaders::createBlurredSprite(tex, texSize, 4.0f, true); // radio 4.0 directo para blur fuerte
            
            if (finalSprite) {
                    // escalo al tamano de la ventana
                    float scaleX = win.width / finalSprite->getContentSize().width;
                    float scaleY = win.height / finalSprite->getContentSize().height;
                    float scale = std::max(scaleX, scaleY);
                    
                    // capa nitida de fondo
                    sharpSprite->setScale(scale);
                    sharpSprite->setPosition(win / 2);
                    sharpSprite->setColor({80, 80, 80}); // base algo oscura
                    sharpSprite->setZOrder(-11);
                    sharpSprite->setOpacity(0);
                    this->addChild(sharpSprite);
                    sharpSprite->runAction(CCFadeIn::create(0.5f));

                    // capa blur por encima
                    finalSprite->setScale(scale);
                    finalSprite->setPosition(win / 2);
                    finalSprite->setZOrder(-10);
                    finalSprite->setOpacity(0); // empieza transparente, deja ver la nitida
                    this->addChild(finalSprite);
                    // y hago fade-in hasta 255 para ir tapando con blur
                    finalSprite->runAction(CCFadeTo::create(0.5f, 255)); 
                    
                    // efecto de zoom suave (mismo en ambas capas)
                    auto zoomAction = CCRepeatForever::create(CCSequence::create(
                        CCScaleTo::create(10.0f, scale * 1.3f),
                        CCScaleTo::create(10.0f, scale),
                        nullptr
                    ));
                    sharpSprite->runAction(zoomAction);
                    
                    auto zoomAction2 = CCRepeatForever::create(CCSequence::create(
                        CCScaleTo::create(10.0f, scale * 1.3f),
                        CCScaleTo::create(10.0f, scale),
                        nullptr
                    ));
                    finalSprite->runAction(zoomAction2);
            }
        }
        
        m_fields->m_bgSprite = finalSprite; // blur principal
        m_fields->m_sharpBgSprite = sharpSprite;
    }
    

    void onBack(CCObject* sender) {
        DynamicSongManager::get()->exitLayer(DynSongLayer::LevelSelect);
        DynamicSongManager::get()->stopSong();
        LevelSelectLayer::onBack(sender);
    }
    
    void keyBackClicked() {
        DynamicSongManager::get()->exitLayer(DynSongLayer::LevelSelect);
        DynamicSongManager::get()->stopSong();
        LevelSelectLayer::keyBackClicked();
    }
};

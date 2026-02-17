#include "DynamicSongManager.hpp"
#include <Geode/binding/FMODAudioEngine.hpp>
#include <Geode/binding/MusicDownloadManager.hpp>
#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/LevelTools.hpp>
#include <random>

using namespace geode::prelude;

DynamicSongManager* DynamicSongManager::get() {
    static DynamicSongManager instance;
    return &instance;
}

void DynamicSongManager::playSong(GJGameLevel* level) {
    if (!Mod::get()->getSettingValue<bool>("dynamic-song")) return;
    if (!level) return;

    // salto si esta desactivado o el nivel es invalido

    std::string songPath;
    if (level->m_songID > 0) {
        if (MusicDownloadManager::sharedState()->isSongDownloaded(level->m_songID)) {
            songPath = MusicDownloadManager::sharedState()->pathForSong(level->m_songID);
        }
    } else {
        std::string filename = LevelTools::getAudioFileName(level->m_audioTrack);
        songPath = CCFileUtils::sharedFileUtils()->fullPathForFilename(filename.c_str(), false);
        
        // fallback: si fullpath esta vacio, pruebo nombre raw (a veces va si esta en root)
        if (songPath.empty()) {
            songPath = filename;
        }
    }

    if (!songPath.empty()) {
        auto engine = FMODAudioEngine::sharedEngine();
        
        // guardo posicion del menu
        // solo guardo si no estoy ya en modo dinamico
        if (!m_isDynamicSongActive) {
            // asumo que lo que suena es musica del menu (o escena anterior)
            // pero cuidado. si no suena musica, la pos es 0.
            if (engine->isMusicPlaying(0)) {
                m_savedMenuPos = engine->getMusicTimeMS(0);
                geode::log::info("Saved Menu Music Position: {} ms", m_savedMenuPos);
            }
            m_isDynamicSongActive = true;
        }

        // debug log
        geode::log::info("Playing Dynamic Song: {}", songPath);

        // reproduzco en canal 0 (main)
        engine->playMusic(songPath, true, 1.0f, 0);

        Loader::get()->queueInMainThread([]() {
            auto engine = FMODAudioEngine::sharedEngine();
            unsigned int length = engine->getMusicLengthMS(0); 
            if (length > 10000) {
                unsigned int minStart = static_cast<unsigned int>(length * 0.15f);
                unsigned int maxStart = static_cast<unsigned int>(length * 0.85f);
                
                if (maxStart > minStart) {
                    static std::random_device rd;
                    static std::mt19937 gen(rd());
                    std::uniform_int_distribution<unsigned int> dist(minStart, maxStart);
                    
                    unsigned int seekTime = dist(gen);
                    engine->setMusicTimeMS(seekTime, true, 0);
                }
            }
        });
    }
}

void DynamicSongManager::stopSong() {
    if (!Mod::get()->getSettingValue<bool>("dynamic-song")) return;
    
    // restauro musica del menu
    if (m_isDynamicSongActive) {
        auto gm = GameManager::get();
        auto engine = FMODAudioEngine::sharedEngine();

        // 1. fade vuelta a musica del menu
        // puedo usar engine->playmusic directo pa controlar posicion inicio
        std::string menuTrack = gm->getMenuMusicFile();
        engine->playMusic(menuTrack, true, 1.0f, 0); // duracion del fade coincide

        // 2. busco posicion guardada
        if (m_savedMenuPos > 0) {
            engine->setMusicTimeMS(m_savedMenuPos, true, 0);
        }
        
        m_isDynamicSongActive = false;
        geode::log::info("Restored Menu Music to: {} ms", m_savedMenuPos);
    } else {
        // fallback
        GameManager::get()->fadeInMenuMusic();
    }
}

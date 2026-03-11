#include "DynamicSongManager.hpp"
#include <Geode/binding/FMODAudioEngine.hpp>
#include <Geode/binding/MusicDownloadManager.hpp>
#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/LevelTools.hpp>
#include <random>
#include <thread>
#include <chrono>
#include <cmath>

using namespace geode::prelude;

DynamicSongManager* DynamicSongManager::get() {
    static DynamicSongManager instance;
    return &instance;
}

void DynamicSongManager::enterLayer(DynSongLayer layer) {
    m_currentLayer = layer;
}

void DynamicSongManager::exitLayer(DynSongLayer layer) {
    if (m_currentLayer == layer) {
        m_currentLayer = DynSongLayer::None;
    }
}

bool DynamicSongManager::isCrossfadeEnabled() const {
    return Mod::get()->getSettingValue<bool>("profile-music-crossfade");
}

float DynamicSongManager::getFadeDurationMs() const {
    return static_cast<float>(Mod::get()->getSettingValue<double>("profile-music-fade-duration")) * 1000.0f;
}

// Helper: Carga la pista de Menu en m_backgroundMusicChannel con volumen deseado.
// Esto es VITAL porque si bloqueamos `fadeInMenuMusic` de GD, este canal queda con
// la pista equivocada (ej: pista de nivel o nada)
void DynamicSongManager::loadMenuTrack(float startVolume) {
    auto engine = FMODAudioEngine::sharedEngine();
    auto gm = GameManager::get();
    if (!engine || !gm) return;

    std::string menuTrack = gm->getMenuMusicFile();
    // Iniciar con fade 0 para evitar que FMOD trate de animar el volumen
    engine->playMusic(menuTrack, true, 0.0f, 0);

    if (engine->m_backgroundMusicChannel) {
        engine->m_backgroundMusicChannel->setVolume(startVolume);
    }

    if (m_savedMenuPos > 0) {
        engine->setMusicTimeMS(m_savedMenuPos, true, 0);
    }
}

void DynamicSongManager::restoreBgChannel() {
    // El canal BG fue destruido por silenceBgChannel(), hay que recrearlo
    auto engine = FMODAudioEngine::sharedEngine();
    if (!engine) return;
    float targetVol = engine->m_musicVolume;
    loadMenuTrack(targetVol);
    log::info("[DynamicSong] BG channel recreado con vol:{:.2f}", targetVol);
}

void DynamicSongManager::silenceBgChannel() {
    auto engine = FMODAudioEngine::sharedEngine();
    if (!engine) return;

    // Guardar posicion del menu ANTES de destruir el canal
    if (engine->m_backgroundMusicChannel) {
        bool isPlaying = false;
        engine->m_backgroundMusicChannel->isPlaying(&isPlaying);
        if (isPlaying) {
            m_savedMenuPos = engine->getMusicTimeMS(0);
        }
        // Destruir el canal completamente para que mods externos no lo reactiven
        engine->m_backgroundMusicChannel->stop();
    }
    log::info("[DynamicSong] BG channel detenido completamente (pos guardada: {}ms)", m_savedMenuPos);
}

void DynamicSongManager::playSong(GJGameLevel* level) {
    if (!Mod::get()->getSettingValue<bool>("dynamic-song")) return;
    if (!level) return;

    if (!isInValidLayer()) return;

    // Si ProfileMusic habia destruido el canal, limpiar el flag
    if (m_stoppedByProfile) {
        m_stoppedByProfile = false;
    }

    std::string songPath;
    if (level->m_songID > 0) {
        if (MusicDownloadManager::sharedState()->isSongDownloaded(level->m_songID)) {
            songPath = MusicDownloadManager::sharedState()->pathForSong(level->m_songID);
        }
    } else {
        std::string filename = LevelTools::getAudioFileName(level->m_audioTrack);
        songPath = CCFileUtils::sharedFileUtils()->fullPathForFilename(filename.c_str(), false);
        if (songPath.empty()) songPath = filename;
    }

    if (songPath.empty()) return;

    // Guardar path para poder recrear la cancion tras ProfileMusic
    m_lastSongPath = songPath;

    auto engine = FMODAudioEngine::sharedEngine();
    if (!engine || !engine->m_system) return;

    if (!m_isDynamicSongActive) {
        if (engine->isMusicPlaying(0)) {
            m_savedMenuPos = engine->getMusicTimeMS(0);
        }
        m_bgVolumeBeforeFade = engine->m_musicVolume;
        m_isDynamicSongActive = true;
    }

    stopCurrentAudio();

    FMOD::Sound* newSound = nullptr;
    if (engine->m_system->createSound(songPath.c_str(), FMOD_CREATESTREAM | FMOD_LOOP_NORMAL, nullptr, &newSound) != FMOD_OK || !newSound) {
        return;
    }
    m_currentSound = newSound;

    FMOD::Channel* newChannel = nullptr;
    if (engine->m_system->playSound(m_currentSound, nullptr, true, &newChannel) != FMOD_OK || !newChannel) {
        m_currentSound->release();
        m_currentSound = nullptr;
        return;
    }
    m_musicChannel = newChannel;

    // Seek aleatorio
    unsigned int lengthMs = 0;
    m_currentSound->getLength(&lengthMs, FMOD_TIMEUNIT_MS);
    if (lengthMs > 10000) {
        unsigned int minStart = static_cast<unsigned int>(lengthMs * 0.15f);
        unsigned int maxStart = static_cast<unsigned int>(lengthMs * 0.85f);
        if (maxStart > minStart) {
            static std::random_device rd;
            static std::mt19937 gen(rd());
            std::uniform_int_distribution<unsigned int> dist(minStart, maxStart);
            unsigned int seekTime = dist(gen);
            m_musicChannel->setPosition(seekTime, FMOD_TIMEUNIT_MS);
        }
    }

    float gameVolume = engine->m_musicVolume;

    if (isCrossfadeEnabled()) {
        m_musicChannel->setVolume(0.0f);
        m_musicChannel->setPaused(false);
        m_isFadingIn = true;
        m_isFadingOut = false;
        fadeInDynamicSong(gameVolume);
    } else {
        silenceBgChannel();
        m_musicChannel->setVolume(gameVolume);
        m_musicChannel->setPaused(false);
    }
}

void DynamicSongManager::stopSong() {
    if (!m_isDynamicSongActive) return;

    if (m_isFadingOut) return;

    if (m_musicChannel && isCrossfadeEnabled()) {
        fadeOutAndRestore();
    } else {
        stopCurrentAudio();
        // Restaurar menu directamente
        loadMenuTrack(FMODAudioEngine::sharedEngine()->m_musicVolume);
        m_isDynamicSongActive = false;
    }
}

void DynamicSongManager::fadeInDynamicSong(float targetVolume) {
    executeFadeStep(0, FADE_STEPS, 0.0f, targetVolume, m_bgVolumeBeforeFade, 0.0f, false);
}

void DynamicSongManager::fadeOutAndRestore() {
    m_isFadingOut = true;
    m_isFadingIn = false;
    
    // Cargar track de menu silenciosamente ANTES de hacer fadeIn
    loadMenuTrack(0.0f);
    
    auto engine = FMODAudioEngine::sharedEngine();
    float targetBgVolume = engine ? engine->m_musicVolume : m_bgVolumeBeforeFade;
    m_bgVolumeBeforeFade = targetBgVolume;

    float currentDynVol = 0.0f;
    if (m_musicChannel) m_musicChannel->getVolume(&currentDynVol);

    executeFadeStep(0, FADE_STEPS, currentDynVol, 0.0f, 0.0f, targetBgVolume, true);
}

void DynamicSongManager::executeFadeStep(int step, int totalSteps,
    float dynFrom, float dynTo, float bgFrom, float bgTo, bool restoreAfter) {
    if (step > totalSteps) {
        if (restoreAfter) {
            stopCurrentAudio();
            restoreBgChannel(); // Asegura 100%
            m_isFadingOut = false;
            m_isDynamicSongActive = false;
        } else {
            silenceBgChannel(); // Asegura 0%
            if (m_musicChannel) m_musicChannel->setVolume(dynTo);
            m_isFadingIn = false;
        }
        return;
    }
    
    float t = static_cast<float>(step) / static_cast<float>(totalSteps);
    float eT = (t < 0.5f) ? (2.f*t*t) : (1.f - std::pow(-2.f*t+2.f, 2.f)/2.f);
    float dV = dynFrom + (dynTo - dynFrom) * eT;
    float bV = bgFrom + (bgTo - bgFrom) * eT;
    
    if (m_musicChannel) m_musicChannel->setVolume(std::max(0.f, std::min(1.f, dV)));
    
    auto engine = FMODAudioEngine::sharedEngine();
    if (engine && engine->m_backgroundMusicChannel)
        engine->m_backgroundMusicChannel->setVolume(std::max(0.f, std::min(1.f, bV)));
        
    float stepMs = getFadeDurationMs() / static_cast<float>(totalSteps);
    int next = step + 1;
    
    Loader::get()->queueInMainThread([this, next, totalSteps, dynFrom, dynTo,
                                       bgFrom, bgTo, restoreAfter, stepMs]() {
        std::thread([this, next, totalSteps, dynFrom, dynTo,
                     bgFrom, bgTo, restoreAfter, stepMs]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(stepMs)));
            Loader::get()->queueInMainThread([this, next, totalSteps, dynFrom, dynTo,
                                               bgFrom, bgTo, restoreAfter]() {
                if (restoreAfter && !m_isFadingOut) return;
                if (!restoreAfter && !m_isFadingIn) return;
                executeFadeStep(next, totalSteps, dynFrom, dynTo, bgFrom, bgTo, restoreAfter);
            });
        }).detach();
    });
}

void DynamicSongManager::fadeOutForLevelStart() {
    if (!m_isDynamicSongActive && !m_musicChannel) return;

    m_isFadingIn = false;
    m_isDynamicSongActive = false;
    m_currentLayer = DynSongLayer::None;

    if (!m_musicChannel) return;

    float currentVol = 0.0f;
    m_musicChannel->getVolume(&currentVol);

    if (!isCrossfadeEnabled() || currentVol <= 0.0f) {
        stopCurrentAudio();
        return;
    }

    m_isFadingOut = true;
    executeLevelStartFade(0, FADE_STEPS, currentVol);
}

void DynamicSongManager::executeLevelStartFade(int step, int totalSteps, float volFrom) {
    if (step > totalSteps || !m_isFadingOut) {
        stopCurrentAudio();
        m_isFadingOut = false;
        return;
    }

    float t = static_cast<float>(step) / static_cast<float>(totalSteps);
    float eT = (t < 0.5f) ? (2.f * t * t) : (1.f - std::pow(-2.f * t + 2.f, 2.f) / 2.f);
    float vol = volFrom * (1.0f - eT);

    if (m_musicChannel) m_musicChannel->setVolume(std::max(0.f, std::min(1.f, vol)));

    float stepMs = getFadeDurationMs() / static_cast<float>(totalSteps);
    int next = step + 1;

    Loader::get()->queueInMainThread([this, next, totalSteps, volFrom, stepMs]() {
        std::thread([this, next, totalSteps, volFrom, stepMs]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(stepMs)));
            Loader::get()->queueInMainThread([this, next, totalSteps, volFrom]() {
                if (!m_isFadingOut) return;
                executeLevelStartFade(next, totalSteps, volFrom);
            });
        }).detach();
    });
}

void DynamicSongManager::forceKill() {
    // Restaurar siempre el volumen del Background Channel.
    // Esto es crucial porque nuestro crossfade pone el volumen a 0, 
    // y si no lo restauramos aqui, la musica del nivel en PlayLayer sonara en 0.
    restoreBgChannel();

    // Si esta haciendo fade-out hacia un nivel, NO matar el canal abruptamente
    // para que la transicion de LevelInfoLayer al nivel sea suave (sin cortes)
    if (m_isFadingOut) {
        m_isDynamicSongActive = false;
        m_currentLayer = DynSongLayer::None;
        return;
    }

    m_isFadingIn = false;
    m_isFadingOut = false;
    stopCurrentAudio();

    m_isDynamicSongActive = false;
    m_currentLayer = DynSongLayer::None;
    m_stoppedByProfile = false;
}

// ─── Stop/Replay para ProfileMusic y otros sistemas ──────────────────
void DynamicSongManager::stopDynamicForProfileMusic() {
    // Destruir completamente el canal dinamico para que ningun mod externo
    // pueda reactivarlo y causar musica doble.
    m_isFadingIn = false;
    // No cancelamos m_isFadingOut porque podria ser un fade hacia nivel

    stopCurrentAudio();
    m_stoppedByProfile = true;
    // m_isDynamicSongActive se mantiene true para que sepamos que hay que recrear

    log::info("[DynamicSong] Canal destruido por ProfileMusic (lastSong: {})", m_lastSongPath);
}

void DynamicSongManager::replayLastSong() {
    m_stoppedByProfile = false;

    if (m_lastSongPath.empty() || !m_isDynamicSongActive || !isInValidLayer()) {
        // No hay cancion que recrear o ya no estamos en un layer valido
        // Restaurar BG en su lugar
        m_isDynamicSongActive = false;
        restoreBgChannel();
        log::info("[DynamicSong] No hay cancion para replay, BG restaurado");
        return;
    }

    auto engine = FMODAudioEngine::sharedEngine();
    if (!engine || !engine->m_system) {
        m_isDynamicSongActive = false;
        return;
    }

    // Recrear el canal desde cero con la ultima cancion
    FMOD::Sound* newSound = nullptr;
    if (engine->m_system->createSound(m_lastSongPath.c_str(), FMOD_CREATESTREAM | FMOD_LOOP_NORMAL, nullptr, &newSound) != FMOD_OK || !newSound) {
        m_isDynamicSongActive = false;
        restoreBgChannel();
        log::warn("[DynamicSong] No se pudo recrear el sonido, BG restaurado");
        return;
    }
    m_currentSound = newSound;

    FMOD::Channel* newChannel = nullptr;
    if (engine->m_system->playSound(m_currentSound, nullptr, true, &newChannel) != FMOD_OK || !newChannel) {
        m_currentSound->release();
        m_currentSound = nullptr;
        m_isDynamicSongActive = false;
        restoreBgChannel();
        return;
    }
    m_musicChannel = newChannel;

    // Seek aleatorio (mismo patron que playSong)
    unsigned int lengthMs = 0;
    m_currentSound->getLength(&lengthMs, FMOD_TIMEUNIT_MS);
    if (lengthMs > 10000) {
        unsigned int minStart = static_cast<unsigned int>(lengthMs * 0.15f);
        unsigned int maxStart = static_cast<unsigned int>(lengthMs * 0.85f);
        if (maxStart > minStart) {
            static std::random_device rd;
            static std::mt19937 gen(rd());
            std::uniform_int_distribution<unsigned int> dist(minStart, maxStart);
            m_musicChannel->setPosition(dist(gen), FMOD_TIMEUNIT_MS);
        }
    }

    float gameVolume = engine->m_musicVolume;

    if (isCrossfadeEnabled()) {
        m_musicChannel->setVolume(0.0f);
        m_musicChannel->setPaused(false);
        m_isFadingIn = true;
        m_isFadingOut = false;
        fadeInDynamicSong(gameVolume);
    } else {
        // BG ya esta destruido, solo poner volumen y reproducir
        m_musicChannel->setVolume(gameVolume);
        m_musicChannel->setPaused(false);
    }

    log::info("[DynamicSong] Canal recreado desde replay (vol:{:.2f})", gameVolume);
}

void DynamicSongManager::stopCurrentAudio() {
    if (m_musicChannel) { m_musicChannel->stop(); m_musicChannel = nullptr; }
    if (m_currentSound) { m_currentSound->release(); m_currentSound = nullptr; }
    // No limpiar m_stoppedByProfile aqui — lo maneja replayLastSong
}

float DynamicSongManager::getDynamicVolume() const {
    if (!m_musicChannel) return 0.0f;
    float vol = 0.0f;
    m_musicChannel->getVolume(&vol);
    return vol;
}

void DynamicSongManager::setDynamicVolume(float vol) {
    if (m_musicChannel) {
        m_musicChannel->setVolume(std::max(0.0f, std::min(1.0f, vol)));
    }
}

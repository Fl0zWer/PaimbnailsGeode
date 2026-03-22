#include "DynamicSongManager.hpp"
#include "../../../utils/MainThreadDelay.hpp"
#include <Geode/binding/FMODAudioEngine.hpp>
#include <Geode/binding/MusicDownloadManager.hpp>
#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/LevelTools.hpp>
#include <random>
#include <cmath>
#include <sstream>
#include <set>

using namespace geode::prelude;

// Helper: extrae el FMOD::Channel* subyacente del ChannelGroup de musica de fondo.
// m_backgroundMusicChannel es ChannelGroup*, y getCurrentSound/getPosition/setPosition
// solo existen en Channel*.
static FMOD::Channel* getMainBgChannel(FMODAudioEngine* engine) {
    if (!engine || !engine->m_backgroundMusicChannel) return nullptr;
    int numCh = 0;
    engine->m_backgroundMusicChannel->getNumChannels(&numCh);
    if (numCh <= 0) return nullptr;
    FMOD::Channel* ch = nullptr;
    if (engine->m_backgroundMusicChannel->getChannel(0, &ch) != FMOD_OK) return nullptr;
    return ch;
}

DynamicSongManager* DynamicSongManager::get() {
    static DynamicSongManager instance;
    return &instance;
}

DynamicSongManager::~DynamicSongManager() {
    if (m_lifetimeToken) {
        m_lifetimeToken->store(false, std::memory_order_release);
    }
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

// ─── Canal principal ─────────────────────────────────────────────────
// Reproduce una cancion directamente en m_backgroundMusicChannel de GD
// usando engine->playMusic(). Esto asegura que siempre usamos el canal
// principal y evitamos conflictos con otros mods.
void DynamicSongManager::playOnMainChannel(const std::string& songPath, float startVolume) {
    auto engine = FMODAudioEngine::sharedEngine();
    if (!engine) return;

    // playMusic reemplaza lo que sea que este en m_backgroundMusicChannel
    s_selfPlayMusic = true;
    engine->playMusic(songPath, true, 0.0f, 0);
    s_selfPlayMusic = false;

    if (engine->m_backgroundMusicChannel) {
        engine->m_backgroundMusicChannel->setVolume(startVolume);
    }
}

// Helper: Carga la pista de Menu en m_backgroundMusicChannel con volumen deseado.
void DynamicSongManager::loadMenuTrack(float startVolume) {
    auto engine = FMODAudioEngine::sharedEngine();
    auto gm = GameManager::get();
    if (!engine || !gm) return;

    // No cargar musica de menu si esta desactivada o el volumen esta en 0
    if (gm->getGameVariable("0122")) return;
    if (engine->m_musicVolume <= 0.0f) return;

    std::string menuTrack = gm->getMenuMusicFile();
    s_selfPlayMusic = true;
    engine->playMusic(menuTrack, true, 0.0f, 0);
    s_selfPlayMusic = false;

    if (engine->m_backgroundMusicChannel) {
        engine->m_backgroundMusicChannel->setVolume(startVolume);
    }

    if (m_savedMenuPos > 0) {
        engine->setMusicTimeMS(m_savedMenuPos, true, 0);
    }
}

void DynamicSongManager::restoreBgChannel() {
    auto engine = FMODAudioEngine::sharedEngine();
    if (!engine) return;
    float targetVol = engine->m_musicVolume;
    loadMenuTrack(targetVol);
    m_expectedSongPath.clear();
    log::info("[DynamicSong] Menu restaurado con vol:{:.2f}", targetVol);
}

// ─── Seek aleatorio ──────────────────────────────────────────────────
void DynamicSongManager::applyRandomSeek() {
    auto engine = FMODAudioEngine::sharedEngine();
    auto* bgCh = getMainBgChannel(engine);
    if (!bgCh) return;

    FMOD::Sound* currentSound = nullptr;
    bgCh->getCurrentSound(&currentSound);
    if (!currentSound) return;

    unsigned int lengthMs = 0;
    currentSound->getLength(&lengthMs, FMOD_TIMEUNIT_MS);
    if (lengthMs > 10000) {
        unsigned int minStart = static_cast<unsigned int>(lengthMs * 0.15f);
        unsigned int maxStart = static_cast<unsigned int>(lengthMs * 0.85f);
        if (maxStart > minStart) {
            static std::random_device rd;
            static std::mt19937 gen(rd());
            std::uniform_int_distribution<unsigned int> dist(minStart, maxStart);
            bgCh->setPosition(dist(gen), FMOD_TIMEUNIT_MS);
        }
    }
}

// ─── Rotacion de canciones por nivel ──────────────────────────────────

std::vector<std::string> DynamicSongManager::getAllSongPaths(GJGameLevel* level) {
    std::vector<std::string> paths;
    std::set<int> seenIds;

    auto mdm = MusicDownloadManager::sharedState();

    // Cancion principal primero
    if (level->m_songID > 0) {
        if (mdm->isSongDownloaded(level->m_songID)) {
            paths.push_back(mdm->pathForSong(level->m_songID));
            seenIds.insert(level->m_songID);
        }
    } else {
        std::string filename = LevelTools::getAudioFileName(level->m_audioTrack);
        std::string fullPath = CCFileUtils::sharedFileUtils()->fullPathForFilename(filename.c_str(), false);
        if (fullPath.empty()) fullPath = filename;
        if (!fullPath.empty()) paths.push_back(fullPath);
    }

    // Canciones adicionales desde m_songIDs (comma-separated)
    std::string songIdsStr = level->m_songIDs;
    if (!songIdsStr.empty()) {
        std::stringstream ss(songIdsStr);
        std::string token;
        while (std::getline(ss, token, ',')) {
            // trim espacios
            auto start = token.find_first_not_of(" \t");
            auto end = token.find_last_not_of(" \t");
            if (start == std::string::npos) continue;
            token = token.substr(start, end - start + 1);
            if (token.empty()) continue;

            auto songIdResult = geode::utils::numFromString<int>(token);
            if (!songIdResult) continue;
            int songId = songIdResult.unwrap();

            if (songId <= 0 || seenIds.count(songId)) continue;
            seenIds.insert(songId);

            if (mdm->isSongDownloaded(songId)) {
                paths.push_back(mdm->pathForSong(songId));
            }
        }
    }

    return paths;
}

std::string DynamicSongManager::getNextRotationSong(GJGameLevel* level) {
    auto allPaths = getAllSongPaths(level);

    // Si tiene 0 o 1 cancion, no hay rotacion
    if (allPaths.size() <= 1) {
        return allPaths.empty() ? "" : allPaths[0];
    }

    int levelId = level->m_levelID;

    // Si no hay cache o se agoto, recrear la lista completa
    auto it = m_songRotationCache.find(levelId);
    if (it == m_songRotationCache.end() || it->second.empty()) {
        if (m_songRotationCache.size() >= MAX_ROTATION_CACHE_LEVELS) {
            m_songRotationCache.clear();
        }
        m_songRotationCache[levelId] = allPaths;
        it = m_songRotationCache.find(levelId);
        log::info("[DynamicSong] Rotacion reiniciada para nivel {} ({} canciones)", levelId, allPaths.size());
    }

    // Tomar la primera cancion pendiente y removerla
    std::string nextSong = it->second.front();
    it->second.erase(it->second.begin());

    log::info("[DynamicSong] Rotacion nivel {}: reproduciendo cancion, quedan {} pendientes",
              levelId, it->second.size());

    return nextSong;
}

// ──────────────────────────────────────────────────────────────────────

void DynamicSongManager::playSong(GJGameLevel* level) {
    if (!Mod::get()->getSettingValue<bool>("dynamic-song")) return;
    if (!level) return;
    log::debug("[DynamicSong] playSong: levelID={}", level->m_levelID.value());

    if (!isInValidLayer()) return;

    // Respetar el toggle nativo de musica de menu de GD (variable 0122)
    if (GameManager::get()->getGameVariable("0122")) return;

    // Respetar el volumen de musica de GD: si esta en 0, no reproducir nada
    auto engineCheck = FMODAudioEngine::sharedEngine();
    if (!engineCheck || engineCheck->m_musicVolume <= 0.0f) return;

    // Si ProfileMusic habia destruido el canal, limpiar el flag
    if (m_stoppedByProfile) {
        m_stoppedByProfile = false;
    }

    // Cancelar fades pendientes antes de iniciar uno nuevo
    if (m_isFadingIn || m_isFadingOut) {
        m_isFadingIn = false;
        m_isFadingOut = false;
    }

    // Si ya estamos reproduciendo la MISMA cancion (retry de onEnter),
    // no avanzar la rotacion — reusar el mismo path.
    std::string songPath;
    int levelId = level->m_levelID.value();

    auto engine = FMODAudioEngine::sharedEngine();
    if (!engine || !engine->m_system) return;

    if (m_isDynamicSongActive && !m_lastSongPath.empty()
        && levelId == m_currentPlayingLevelID) {
        // Misma cancion — verificar que sigue sonando correctamente
        if (verifyPlayback()) {
            return; // ya esta sonando, no hacer nada
        }
        // Verificacion fallo, pero si el canal principal tiene audio reproduciendose
        // no reiniciar — probablemente es un falso positivo por timing
        auto engine2 = FMODAudioEngine::sharedEngine();
        if (engine2 && engine2->m_backgroundMusicChannel) {
            bool playing = false;
            engine2->m_backgroundMusicChannel->isPlaying(&playing);
            if (playing) {
                return; // canal activo, no tocar
            }
        }
        // Canal realmente muerto — reintentar con la misma cancion
        songPath = m_lastSongPath;
    } else {
        // Nivel distinto o primera vez — obtener cancion nueva
        songPath = getNextRotationSong(level);
    }

    if (songPath.empty()) return;

    // Determinar si necesitamos crossfade cancion→cancion
    bool needsSongTransition = m_isDynamicSongActive && !m_lastSongPath.empty()
                               && levelId != m_currentPlayingLevelID
                               && isCrossfadeEnabled();

    // Guardar path y level ID
    m_lastSongPath = songPath;
    m_expectedSongPath = songPath;
    m_currentPlayingLevelID = levelId;

    float gameVolume = engine->m_musicVolume;

    if (needsSongTransition) {
        // ═══ Dip fade cancion→cancion ═══
        // Fade-out main → al llegar a 0 cargar nueva cancion → fade-in main
        float currentVol = 0.0f;
        if (engine->m_backgroundMusicChannel) {
            engine->m_backgroundMusicChannel->getVolume(&currentVol);
        }

        // Guardar la cancion pendiente para cargar al terminar el fade-out
        m_pendingSongPath = songPath;
        m_pendingTargetVolume = gameVolume;
        m_isFadingOut = true;
        m_isFadingIn = false;

        // Fase 1: fade-out del main (restoreMenu=false → al llegar a 0, carga pendingSong)
        executeDipFadeOut(0, FADE_STEPS, currentVol, 0.0f, false);

    } else if (!m_isDynamicSongActive) {
        // ═══ Primera cancion — fade-in desde menu ═══
        if (engine->isMusicPlaying(0)) {
            m_savedMenuPos = engine->getMusicTimeMS(0);
        }
        m_bgVolumeBeforeFade = engine->m_musicVolume;
        m_isDynamicSongActive = true;

        // Reproducir la cancion en el canal principal
        playOnMainChannel(songPath, 0.0f);
        applyRandomSeek();

        if (isCrossfadeEnabled()) {
            m_isFadingIn = true;
            m_isFadingOut = false;
            fadeInMainChannel(gameVolume);
        } else {
            if (engine->m_backgroundMusicChannel) {
                engine->m_backgroundMusicChannel->setVolume(gameVolume);
            }
        }

    } else {
        // ═══ Ya activa, sin crossfade — reemplazo directo ═══
        m_isDynamicSongActive = true;
        playOnMainChannel(songPath, gameVolume);
        applyRandomSeek();
    }
}

void DynamicSongManager::stopSong() {
    log::debug("[DynamicSong] stopSong: active={}", m_isDynamicSongActive);
    if (!m_isDynamicSongActive) return;

    if (m_isFadingOut) return;

    if (isCrossfadeEnabled()) {
        fadeOutAndRestore();
    } else {
        // Pre-cargar menu antes de cambiar para evitar gap de silencio
        float targetVol = FMODAudioEngine::sharedEngine()->m_musicVolume;
        loadMenuTrack(targetVol);
        m_isDynamicSongActive = false;
        m_currentPlayingLevelID = 0;
        m_expectedSongPath.clear();
    }
}

void DynamicSongManager::fadeInMainChannel(float targetVolume) {
    // Fade-in: canal principal sube de 0 a targetVolume
    executeDipFadeIn(0, FADE_STEPS, 0.0f, targetVolume);
}

void DynamicSongManager::fadeOutAndRestore() {
    m_isFadingOut = true;
    m_isFadingIn = false;
    
    auto engine = FMODAudioEngine::sharedEngine();
    if (!engine) return;

    float currentVol = 0.0f;
    if (engine->m_backgroundMusicChannel) {
        engine->m_backgroundMusicChannel->getVolume(&currentVol);
    }

    // Dip fade: bajar volumen → al llegar a 0, cargar menu → subir volumen
    // restoreMenu=true indica que al terminar fade-out se carga el menu
    m_pendingSongPath.clear(); // no hay cancion pendiente, es restore a menu
    m_pendingTargetVolume = engine->m_musicVolume;
    executeDipFadeOut(0, FADE_STEPS, currentVol, 0.0f, true);
}

// ─── Dip Fade: fade-out del canal principal ──────────────────────────
// Al llegar a volTo (0), carga la cancion pendiente o el menu, y luego fade-in.
void DynamicSongManager::executeDipFadeOut(int step, int totalSteps,
    float volFrom, float volTo, bool restoreMenu) {
    if (step > totalSteps || !m_isFadingOut) {
        // Fade-out terminado — ahora cargar el audio nuevo y fade-in
        auto engine = FMODAudioEngine::sharedEngine();
        if (engine && engine->m_backgroundMusicChannel) {
            engine->m_backgroundMusicChannel->setVolume(0.0f);
        }

        if (restoreMenu) {
            // Restaurar menu
            loadMenuTrack(0.0f);
            m_isDynamicSongActive = false;
            m_currentPlayingLevelID = 0;
            m_expectedSongPath.clear();
        } else {
            // Cargar cancion pendiente
            if (!m_pendingSongPath.empty()) {
                playOnMainChannel(m_pendingSongPath, 0.0f);
                applyRandomSeek();
            }
        }

        // Iniciar fade-in
        m_isFadingOut = false;
        m_isFadingIn = true;
        executeDipFadeIn(0, totalSteps, 0.0f, m_pendingTargetVolume);
        return;
    }
    
    float t = static_cast<float>(step) / static_cast<float>(totalSteps);
    float eT = (t < 0.5f) ? (2.f*t*t) : (1.f - std::pow(-2.f*t+2.f, 2.f)/2.f);
    float vol = volFrom + (volTo - volFrom) * eT;
    
    auto engine = FMODAudioEngine::sharedEngine();
    if (engine && engine->m_backgroundMusicChannel) {
        engine->m_backgroundMusicChannel->setVolume(std::max(0.f, std::min(1.f, vol)));
    }
        
    float stepDelay = getFadeDurationMs() / static_cast<float>(totalSteps) / 1000.f;
    int next = step + 1;
    auto lifetimeToken = m_lifetimeToken;

    paimon::scheduleMainThreadDelay(stepDelay, [lifetimeToken, next, totalSteps, volFrom, volTo, restoreMenu]() {
        if (!lifetimeToken || !lifetimeToken->load(std::memory_order_acquire)) return;

        auto manager = DynamicSongManager::get();
        if (!manager->m_isFadingOut) return;
        manager->executeDipFadeOut(next, totalSteps, volFrom, volTo, restoreMenu);
    });
}

// ─── Dip Fade: fade-in del canal principal ───────────────────────────
void DynamicSongManager::executeDipFadeIn(int step, int totalSteps,
    float volFrom, float volTo) {
    if (step > totalSteps || !m_isFadingIn) {
        // Fade-in terminado: fijar volumen final
        auto engine = FMODAudioEngine::sharedEngine();
        if (engine && engine->m_backgroundMusicChannel) {
            engine->m_backgroundMusicChannel->setVolume(volTo);
        }
        m_isFadingIn = false;
        return;
    }

    float t = static_cast<float>(step) / static_cast<float>(totalSteps);
    float eT = (t < 0.5f) ? (2.f*t*t) : (1.f - std::pow(-2.f*t+2.f, 2.f)/2.f);
    float vol = volFrom + (volTo - volFrom) * eT;

    auto engine = FMODAudioEngine::sharedEngine();
    if (engine && engine->m_backgroundMusicChannel) {
        engine->m_backgroundMusicChannel->setVolume(std::max(0.f, std::min(1.f, vol)));
    }

    float stepDelay = getFadeDurationMs() / static_cast<float>(totalSteps) / 1000.f;
    int next = step + 1;
    auto lifetimeToken = m_lifetimeToken;

    paimon::scheduleMainThreadDelay(stepDelay, [lifetimeToken, next, totalSteps, volFrom, volTo]() {
        if (!lifetimeToken || !lifetimeToken->load(std::memory_order_acquire)) return;

        auto manager = DynamicSongManager::get();
        if (!manager->m_isFadingIn) return;
        manager->executeDipFadeIn(next, totalSteps, volFrom, volTo);
    });
}

void DynamicSongManager::fadeOutForLevelStart() {
    if (!m_isDynamicSongActive) return;

    m_isFadingIn = false;
    m_isFadingOut = false;
    m_isDynamicSongActive = false;
    m_currentLayer = DynSongLayer::None;
    m_expectedSongPath.clear();

    auto engine = FMODAudioEngine::sharedEngine();
    if (!engine || !engine->m_backgroundMusicChannel) return;

    float currentVol = 0.0f;
    engine->m_backgroundMusicChannel->getVolume(&currentVol);

    if (!isCrossfadeEnabled() || currentVol <= 0.0f) {
        // Sin crossfade: parar inmediatamente, GD cargara su propia musica
        engine->m_backgroundMusicChannel->setVolume(0.0f);
        return;
    }

    m_isFadingOut = true;
    executeLevelStartFade(0, FADE_STEPS, currentVol);
}

void DynamicSongManager::executeLevelStartFade(int step, int totalSteps, float volFrom) {
    if (step > totalSteps || !m_isFadingOut) {
        auto engine = FMODAudioEngine::sharedEngine();
        if (engine && engine->m_backgroundMusicChannel) {
            engine->m_backgroundMusicChannel->setVolume(0.0f);
        }
        m_isFadingOut = false;
        return;
    }

    float t = static_cast<float>(step) / static_cast<float>(totalSteps);
    float eT = (t < 0.5f) ? (2.f * t * t) : (1.f - std::pow(-2.f * t + 2.f, 2.f) / 2.f);
    float vol = volFrom * (1.0f - eT);

    auto engine = FMODAudioEngine::sharedEngine();
    if (engine && engine->m_backgroundMusicChannel) {
        engine->m_backgroundMusicChannel->setVolume(std::max(0.f, std::min(1.f, vol)));
    }

    float stepDelay = getFadeDurationMs() / static_cast<float>(totalSteps) / 1000.f;
    int next = step + 1;
    auto lifetimeToken = m_lifetimeToken;

    paimon::scheduleMainThreadDelay(stepDelay, [lifetimeToken, next, totalSteps, volFrom]() {
        if (!lifetimeToken || !lifetimeToken->load(std::memory_order_acquire)) return;

        auto manager = DynamicSongManager::get();
        if (!manager->m_isFadingOut) return;
        manager->executeLevelStartFade(next, totalSteps, volFrom);
    });
}

void DynamicSongManager::forceKill() {
    log::info("[DynamicSong] forceKill");
    m_isFadingIn = false;
    m_isFadingOut = false;

    m_isDynamicSongActive = false;
    m_currentLayer = DynSongLayer::None;
    m_stoppedByProfile = false;
    m_currentPlayingLevelID = 0;
    m_expectedSongPath.clear();

    // Restaurar volumen del canal principal — GD/PlayLayer cargaran su propia musica
    auto engine = FMODAudioEngine::sharedEngine();
    if (engine && engine->m_backgroundMusicChannel) {
        engine->m_backgroundMusicChannel->setVolume(engine->m_musicVolume);
    }
}

// ─── Stop/Replay para ProfileMusic y otros sistemas ──────────────────
void DynamicSongManager::stopDynamicForProfileMusic() {
    // Guardar posicion actual para restaurarla al salir del perfil
    auto engine = FMODAudioEngine::sharedEngine();
    if (engine && engine->m_backgroundMusicChannel) {
        unsigned int posMs = 0;
        auto* bgCh = getMainBgChannel(engine);
        if (bgCh && bgCh->getPosition(&posMs, FMOD_TIMEUNIT_MS) == FMOD_OK) {
            m_savedDynamicPosMs = posMs;
        }
        // Detener el canal principal — ProfileMusic lo usara
        engine->m_backgroundMusicChannel->stop();
    }

    m_isFadingIn = false;
    m_isFadingOut = false;

    // El canal principal queda bajo control de ProfileMusic
    m_stoppedByProfile = true;
    // m_isDynamicSongActive se mantiene true para que sepamos que hay que recrear

    log::info("[DynamicSong] Detenido por ProfileMusic (lastSong: {}, pos: {}ms)", m_lastSongPath, m_savedDynamicPosMs);
}

void DynamicSongManager::replayLastSong() {
    m_stoppedByProfile = false;

    // Respetar el toggle nativo de musica de menu de GD
    if (GameManager::get()->getGameVariable("0122")) {
        m_isDynamicSongActive = false;
        m_expectedSongPath.clear();
        return;
    }

    // Respetar el volumen de musica de GD
    auto engineChk = FMODAudioEngine::sharedEngine();
    if (engineChk && engineChk->m_musicVolume <= 0.0f) {
        m_isDynamicSongActive = false;
        m_expectedSongPath.clear();
        return;
    }

    if (m_lastSongPath.empty() || !m_isDynamicSongActive || !isInValidLayer()) {
        m_isDynamicSongActive = false;
        m_expectedSongPath.clear();
        restoreBgChannel();
        log::info("[DynamicSong] No hay cancion para replay, menu restaurado");
        return;
    }

    auto engine = FMODAudioEngine::sharedEngine();
    if (!engine || !engine->m_system) {
        m_isDynamicSongActive = false;
        return;
    }

    // Reproducir la cancion en el canal principal
    playOnMainChannel(m_lastSongPath, 0.0f);
    m_expectedSongPath = m_lastSongPath;

    // Restaurar posicion guardada; si no hay, seek aleatorio
    if (engine->m_backgroundMusicChannel) {
        auto* bgCh = getMainBgChannel(engine);
        FMOD::Sound* currentSound = nullptr;
        unsigned int lengthMs = 0;
        if (bgCh) {
            bgCh->getCurrentSound(&currentSound);
        }
        if (currentSound) {
            currentSound->getLength(&lengthMs, FMOD_TIMEUNIT_MS);
        }

        if (m_savedDynamicPosMs > 0 && m_savedDynamicPosMs < lengthMs) {
            if (bgCh) bgCh->setPosition(m_savedDynamicPosMs, FMOD_TIMEUNIT_MS);
            m_savedDynamicPosMs = 0;
        } else {
            applyRandomSeek();
            m_savedDynamicPosMs = 0;
        }
    }

    float gameVolume = engine->m_musicVolume;

    if (isCrossfadeEnabled()) {
        if (engine->m_backgroundMusicChannel) {
            engine->m_backgroundMusicChannel->setVolume(0.0f);
        }
        m_isFadingIn = true;
        m_isFadingOut = false;
        fadeInMainChannel(gameVolume);
    } else {
        if (engine->m_backgroundMusicChannel) {
            engine->m_backgroundMusicChannel->setVolume(gameVolume);
        }
    }

    log::info("[DynamicSong] Replay en canal principal (vol:{:.2f})", gameVolume);
}

// ─── Volumen del canal principal (interfaz para ProfileMusic) ────────
float DynamicSongManager::getDynamicVolume() const {
    auto engine = FMODAudioEngine::sharedEngine();
    if (!engine || !engine->m_backgroundMusicChannel) return 0.0f;
    float vol = 0.0f;
    engine->m_backgroundMusicChannel->getVolume(&vol);
    return vol;
}

void DynamicSongManager::setDynamicVolume(float vol) {
    auto engine = FMODAudioEngine::sharedEngine();
    if (engine && engine->m_backgroundMusicChannel) {
        engine->m_backgroundMusicChannel->setVolume(std::max(0.0f, std::min(1.0f, vol)));
    }
}

// ─── Verificacion de playback ────────────────────────────────────────
bool DynamicSongManager::verifyPlayback() {
    if (!m_isDynamicSongActive || m_expectedSongPath.empty()) return false;
    if (!isInValidLayer()) return false;

    auto engine = FMODAudioEngine::sharedEngine();
    if (!engine || !engine->m_backgroundMusicChannel) return false;

    // Verificar que el canal esta reproduciendo
    bool isPlaying = false;
    engine->m_backgroundMusicChannel->isPlaying(&isPlaying);
    if (!isPlaying) return false;

    // Verificar que la cancion es la esperada
    auto* bgCh = getMainBgChannel(engine);
    if (!bgCh) return false;

    FMOD::Sound* currentSound = nullptr;
    bgCh->getCurrentSound(&currentSound);
    if (!currentSound) return false;

    char nameBuffer[512] = {};
    currentSound->getName(nameBuffer, sizeof(nameBuffer));
    std::string currentName(nameBuffer);

    // Comparar: el nombre del sonido deberia contener el path esperado
    // FMOD puede devolver el path completo o solo el nombre de archivo
    if (currentName.empty()) return false;
    
    // Extraer solo el nombre de archivo de ambos para comparacion robusta
    auto getFileName = [](const std::string& path) -> std::string {
        auto pos = path.find_last_of("/\\");
        return (pos != std::string::npos) ? path.substr(pos + 1) : path;
    };

    std::string expectedFile = getFileName(m_expectedSongPath);
    std::string currentFile = getFileName(currentName);

    return expectedFile == currentFile;
}

void DynamicSongManager::onPlaybackHijacked() {
    log::info("[DynamicSong] Musica cambiada externamente, cediendo control");
    m_isDynamicSongActive = false;
    m_isFadingIn = false;
    m_isFadingOut = false;
    m_currentPlayingLevelID = 0;
    m_expectedSongPath.clear();
    m_currentLayer = DynSongLayer::None;
}

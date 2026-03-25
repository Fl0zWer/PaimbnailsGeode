#include "DynamicSongManager.hpp"
#include "../../../utils/AudioInterop.hpp"
#include "../../audio/services/AudioContextCoordinator.hpp"
#include <Geode/binding/FMODAudioEngine.hpp>
#include <Geode/binding/MusicDownloadManager.hpp>
#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/LevelTools.hpp>
#include <random>
#include <cmath>
#include <sstream>
#include <set>

using namespace geode::prelude;

// ─── Helper: FMOD::Channel* del ChannelGroup de musica de fondo ─────
static FMOD::Channel* getMainBgChannel(FMODAudioEngine* engine) {
    if (!engine || !engine->m_backgroundMusicChannel) return nullptr;
    int numCh = 0;
    engine->m_backgroundMusicChannel->getNumChannels(&numCh);
    if (numCh <= 0) return nullptr;
    FMOD::Channel* ch = nullptr;
    if (engine->m_backgroundMusicChannel->getChannel(0, &ch) != FMOD_OK) return nullptr;
    return ch;
}

// ─── DynSongFadeNode: fade per-frame via CCScheduler ────────────────
// Registrado directamente con el scheduler (no necesita estar en scene tree).
// Una sola llamada a cancel() detiene todo.
class DynSongFadeNode : public cocos2d::CCNode {
public:
    static DynSongFadeNode* create() {
        auto* ret = new DynSongFadeNode();
        if (ret && ret->init()) { ret->autorelease(); return ret; }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }

    void startFade(float fromVol, float toVol, float durationSec) {
        // Cancelar fade anterior si habia uno
        if (m_active) cancel();

        m_fromVol = fromVol;
        m_toVol = toVol;
        m_duration = std::max(durationSec, 0.016f);
        m_elapsed = 0.0f;
        m_active = true;

        auto* scheduler = cocos2d::CCDirector::sharedDirector()->getScheduler();
        scheduler->scheduleSelector(
            schedule_selector(DynSongFadeNode::onFadeTick),
            this, 0.0f, kCCRepeatForever, 0.0f, false
        );
    }

    void cancel() {
        if (!m_active) return;
        m_active = false;
        auto* scheduler = cocos2d::CCDirector::sharedDirector()->getScheduler();
        scheduler->unscheduleSelector(schedule_selector(DynSongFadeNode::onFadeTick), this);
    }

    bool isActive() const { return m_active; }

private:
    float m_fromVol = 0.f, m_toVol = 0.f;
    float m_duration = 0.f, m_elapsed = 0.f;
    bool m_active = false;

    void onFadeTick(float dt) {
        if (!m_active) return;

        m_elapsed += dt;
        float t = std::clamp(m_elapsed / m_duration, 0.0f, 1.0f);

        // Ease-in-out cuadratico
        float eT = (t < 0.5f) ? (2.f * t * t) : (1.f - std::pow(-2.f * t + 2.f, 2.f) / 2.f);
        float vol = m_fromVol + (m_toVol - m_fromVol) * eT;

        auto* engine = FMODAudioEngine::sharedEngine();
        if (engine && engine->m_backgroundMusicChannel) {
            engine->m_backgroundMusicChannel->setVolume(std::clamp(vol, 0.0f, 1.0f));
        }

        if (t >= 1.0f) {
            m_active = false;
            auto* sched = cocos2d::CCDirector::sharedDirector()->getScheduler();
            sched->unscheduleSelector(schedule_selector(DynSongFadeNode::onFadeTick), this);
            DynamicSongManager::get()->onFadeComplete();
        }
    }
};

// ─── Singleton ──────────────────────────────────────────────────────
DynamicSongManager* DynamicSongManager::get() {
    static DynamicSongManager instance;
    return &instance;
}

DynamicSongManager::~DynamicSongManager() {
    if (m_fadeNode) {
        m_fadeNode->cancel();
        m_fadeNode->release();
        m_fadeNode = nullptr;
    }
}

// ─── Layer control ──────────────────────────────────────────────────
void DynamicSongManager::enterLayer(DynSongLayer layer) {
    m_currentLayer = layer;
}

void DynamicSongManager::exitLayer(DynSongLayer layer) {
    if (m_currentLayer == layer) {
        m_currentLayer = DynSongLayer::None;
    }
}

// ─── Fade helpers ───────────────────────────────────────────────────
float DynamicSongManager::getFadeDurationSec() const {
    if (Mod::get()->getSettingValue<bool>("profile-music-crossfade")) {
        return static_cast<float>(Mod::get()->getSettingValue<double>("profile-music-fade-duration"));
    }
    return 0.15f; // siempre fade, pero corto si crossfade esta desactivado
}

void DynamicSongManager::fadeVolume(float from, float to, float durationSec, PostFadeAction action) {
    if (!m_fadeNode) {
        m_fadeNode = DynSongFadeNode::create();
        m_fadeNode->retain();
    }
    m_postFadeAction = action;
    m_fadeNode->startFade(from, to, durationSec);
}

void DynamicSongManager::cancelFade() {
    if (m_fadeNode && m_fadeNode->isActive()) {
        m_fadeNode->cancel();
    }
    m_postFadeAction = PostFadeAction::None;
}

void DynamicSongManager::onFadeComplete() {
    auto* engine = FMODAudioEngine::sharedEngine();
    float targetVol = engine ? engine->m_musicVolume : 1.0f;

    switch (m_postFadeAction) {
    case PostFadeAction::PlayPending: {
        // Dip-fade completado: cargar cancion pendiente, fade in
        playOnMainChannel(m_pendingSongPath, 0.0f);
        applyRandomSeek();
        m_activeSongPath = m_pendingSongPath;
        m_pendingSongPath.clear();
        m_state = DynState::FadingIn;
        fadeVolume(0.0f, targetVol, getFadeDurationSec(), PostFadeAction::None);
        break;
    }
    case PostFadeAction::RestoreMenu: {
        // Fade-out completado: cargar menu, set Idle, fade cosmetico del menu
        loadMenuTrack(0.0f);
        m_activeSongPath.clear();
        m_currentPlayingLevelID = 0;
        m_currentLayer = DynSongLayer::None;
        m_state = DynState::Idle;
        paimon::setDynamicSongInteropActive(false);
        AudioContextCoordinator::get().clearDynamicAudio();
        // Fade cosmetico del menu (estado ya es Idle, hooks dejan pasar)
        fadeVolume(0.0f, targetVol, getFadeDurationSec(), PostFadeAction::None);
        break;
    }
    case PostFadeAction::Cleanup:
        // fadeOutForLevelStart completado
        m_state = DynState::Idle;
        break;

    case PostFadeAction::None:
        // Fade-in completado
        if (m_state == DynState::FadingIn) {
            m_state = DynState::Playing;
        }
        break;
    }

    m_postFadeAction = PostFadeAction::None;
}

// ─── Canal principal ────────────────────────────────────────────────
void DynamicSongManager::playOnMainChannel(const std::string& songPath, float startVolume) {
    auto engine = FMODAudioEngine::sharedEngine();
    if (!engine) return;

    s_selfPlayMusic = true;
    engine->playMusic(songPath, true, 0.0f, 0);
    s_selfPlayMusic = false;

    if (engine->m_backgroundMusicChannel) {
        engine->m_backgroundMusicChannel->setVolume(startVolume);
    }

    AudioContextCoordinator::get().claimDynamicAudio();
}

void DynamicSongManager::loadMenuTrack(float startVolume) {
    auto engine = FMODAudioEngine::sharedEngine();
    auto gm = GameManager::get();
    if (!engine || !gm) return;
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
        m_savedMenuPos = 0;
    }
}

// ─── Seek aleatorio ─────────────────────────────────────────────────
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

// ─── Rotacion de canciones por nivel ────────────────────────────────
std::vector<std::string> DynamicSongManager::getAllSongPaths(GJGameLevel* level) {
    std::vector<std::string> paths;
    std::set<int> seenIds;
    auto mdm = MusicDownloadManager::sharedState();

    // Cancion principal
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

    // Canciones adicionales (m_songIDs comma-separated)
    std::string songIdsStr = level->m_songIDs;
    if (!songIdsStr.empty()) {
        std::stringstream ss(songIdsStr);
        std::string token;
        while (std::getline(ss, token, ',')) {
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
    if (allPaths.size() <= 1) {
        return allPaths.empty() ? "" : allPaths[0];
    }

    int levelId = level->m_levelID;
    auto it = m_songRotationCache.find(levelId);
    if (it == m_songRotationCache.end() || it->second.empty()) {
        if (m_songRotationCache.size() >= MAX_ROTATION_CACHE_LEVELS) {
            m_songRotationCache.erase(m_songRotationCache.begin());
        }
        m_songRotationCache[levelId] = allPaths;
        it = m_songRotationCache.find(levelId);
    }

    std::string nextSong = it->second.front();
    it->second.erase(it->second.begin());
    return nextSong;
}

// ─── playSong ───────────────────────────────────────────────────────
void DynamicSongManager::playSong(GJGameLevel* level) {
    if (!Mod::get()->getSettingValue<bool>("dynamic-song")) return;
    if (!level) return;
    if (!isInValidLayer()) return;
    if (GameManager::get()->getGameVariable("0122")) return;

    auto* engine = FMODAudioEngine::sharedEngine();
    if (!engine || engine->m_musicVolume <= 0.0f) return;

    int levelId = level->m_levelID.value();
    float targetVol = engine->m_musicVolume;

    // Mismo nivel ya activo: verificar y no reiniciar
    if (isActive() && levelId == m_currentPlayingLevelID && !m_activeSongPath.empty()) {
        if (m_state == DynState::FadingIn || m_state == DynState::Playing) {
            if (m_state != DynState::Playing || verifyPlayback()) {
                paimon::setDynamicSongInteropActive(true);
                return;
            }
            // Canal aun reproduce algo? No reiniciar (falso positivo)
            if (engine->m_backgroundMusicChannel) {
                bool playing = false;
                engine->m_backgroundMusicChannel->isPlaying(&playing);
                if (playing) return;
            }
        }
    }

    // Obtener cancion
    std::string songPath;
    if (isActive() && levelId == m_currentPlayingLevelID && !m_activeSongPath.empty()) {
        songPath = m_activeSongPath; // reusar para retry
    } else {
        songPath = getNextRotationSong(level);
    }
    if (songPath.empty()) return;

    // Limpiar suspension si habia
    if (m_state == DynState::Suspended) {
        m_state = DynState::Idle;
    }

    cancelFade();

    m_activeSongPath = songPath;
    m_currentPlayingLevelID = levelId;
    paimon::setDynamicSongInteropActive(true);

    if (m_state == DynState::Idle) {
        // Primera cancion: guardar posicion menu, cargar, fade in
        if (engine->isMusicPlaying(0)) {
            m_savedMenuPos = engine->getMusicTimeMS(0);
        }
        playOnMainChannel(songPath, 0.0f);
        applyRandomSeek();
        m_state = DynState::FadingIn;
        fadeVolume(0.0f, targetVol, getFadeDurationSec(), PostFadeAction::None);
    } else {
        // Song-to-song: dip fade (bajar, cargar nueva, subir)
        float currentVol = getDynamicVolume();
        m_pendingSongPath = songPath;
        m_state = DynState::FadingOut;
        fadeVolume(std::max(currentVol, 0.01f), 0.0f, getFadeDurationSec(), PostFadeAction::PlayPending);
    }

    AudioContextCoordinator::get().claimDynamicAudio();
}

// ─── stopSong ───────────────────────────────────────────────────────
void DynamicSongManager::stopSong() {
    if (!isActive()) return;

    cancelFade();

    if (m_state == DynState::Suspended) {
        // Sin audio activo, restaurar menu directo
        loadMenuTrack(FMODAudioEngine::sharedEngine()->m_musicVolume);
        m_activeSongPath.clear();
        m_currentPlayingLevelID = 0;
        m_state = DynState::Idle;
        paimon::setDynamicSongInteropActive(false);
        AudioContextCoordinator::get().clearDynamicAudio();
        return;
    }

    // Limpiar ownership inmediato para que hooks dejen pasar musica del menu
    // (mismo patron que fadeOutForLevelStart)
    m_activeSongPath.clear();
    m_currentPlayingLevelID = 0;
    m_currentLayer = DynSongLayer::None;
    paimon::setDynamicSongInteropActive(false);
    AudioContextCoordinator::get().clearDynamicAudio();

    float currentVol = getDynamicVolume();
    m_state = DynState::FadingOut;
    fadeVolume(std::max(currentVol, 0.01f), 0.0f, getFadeDurationSec(), PostFadeAction::RestoreMenu);
}

// ─── fadeOutForLevelStart ───────────────────────────────────────────
void DynamicSongManager::fadeOutForLevelStart() {
    if (!isActive()) return;

    cancelFade();

    // Limpiar ownership inmediatamente (hooks dejan pasar musica de gameplay)
    m_activeSongPath.clear();
    m_currentPlayingLevelID = 0;
    m_currentLayer = DynSongLayer::None;
    paimon::setDynamicSongInteropActive(false);
    AudioContextCoordinator::get().clearDynamicAudio();

    auto* engine = FMODAudioEngine::sharedEngine();
    if (!engine || !engine->m_backgroundMusicChannel) {
        m_state = DynState::Idle;
        return;
    }

    float currentVol = 0.0f;
    engine->m_backgroundMusicChannel->getVolume(&currentVol);

    if (currentVol <= 0.01f) {
        m_state = DynState::Idle;
        return;
    }

    m_state = DynState::FadingOut;
    fadeVolume(currentVol, 0.0f, getFadeDurationSec(), PostFadeAction::Cleanup);
}

// ─── forceKill (unico corte brusco, para gameplay y shutdown) ───────
void DynamicSongManager::forceKill() {
    cancelFade();

    m_state = DynState::Idle;
    m_currentLayer = DynSongLayer::None;
    m_activeSongPath.clear();
    m_pendingSongPath.clear();
    m_currentPlayingLevelID = 0;
    m_savedMenuPos = 0;
    m_savedDynamicPosMs = 0;
    paimon::setDynamicSongInteropActive(false);
    AudioContextCoordinator::get().clearDynamicAudio();

    auto engine = FMODAudioEngine::sharedEngine();
    if (engine && engine->m_backgroundMusicChannel) {
        engine->m_backgroundMusicChannel->setVolume(engine->m_musicVolume);
    }
}

// ─── Suspension/reanudacion para profile music ──────────────────────
void DynamicSongManager::suspendPlaybackForExternalAudio() {
    cancelFade();

    auto engine = FMODAudioEngine::sharedEngine();
    if (engine && engine->m_backgroundMusicChannel) {
        auto* bgCh = getMainBgChannel(engine);
        if (bgCh) {
            unsigned int posMs = 0;
            if (bgCh->getPosition(&posMs, FMOD_TIMEUNIT_MS) == FMOD_OK) {
                m_savedDynamicPosMs = posMs;
            }
        }
        engine->m_backgroundMusicChannel->stop();
    }

    m_state = DynState::Suspended;
    paimon::setDynamicSongInteropActive(false);
    AudioContextCoordinator::get().clearDynamicAudio();
}

void DynamicSongManager::resumeSuspendedPlayback() {
    if (m_state != DynState::Suspended) return;

    // Verificar precondiciones
    if (GameManager::get()->getGameVariable("0122")) {
        m_state = DynState::Idle;
        m_activeSongPath.clear();
        paimon::setDynamicSongInteropActive(false);
        AudioContextCoordinator::get().clearDynamicAudio();
        return;
    }

    auto* engine = FMODAudioEngine::sharedEngine();
    if (!engine || engine->m_musicVolume <= 0.0f) {
        m_state = DynState::Idle;
        m_activeSongPath.clear();
        paimon::setDynamicSongInteropActive(false);
        AudioContextCoordinator::get().clearDynamicAudio();
        return;
    }

    if (m_activeSongPath.empty() || !isInValidLayer()) {
        m_state = DynState::Idle;
        m_activeSongPath.clear();
        paimon::setDynamicSongInteropActive(false);
        AudioContextCoordinator::get().clearDynamicAudio();
        // Restaurar menu como fallback
        loadMenuTrack(engine->m_musicVolume);
        return;
    }

    // Cargar cancion y restaurar posicion
    playOnMainChannel(m_activeSongPath, 0.0f);
    paimon::setDynamicSongInteropActive(true);

    auto* bgCh = getMainBgChannel(engine);
    if (bgCh) {
        FMOD::Sound* currentSound = nullptr;
        bgCh->getCurrentSound(&currentSound);
        unsigned int lengthMs = 0;
        if (currentSound) currentSound->getLength(&lengthMs, FMOD_TIMEUNIT_MS);

        if (m_savedDynamicPosMs > 0 && m_savedDynamicPosMs < lengthMs) {
            bgCh->setPosition(m_savedDynamicPosMs, FMOD_TIMEUNIT_MS);
        } else {
            applyRandomSeek();
        }
    }
    m_savedDynamicPosMs = 0;

    float targetVol = engine->m_musicVolume;
    m_state = DynState::FadingIn;
    fadeVolume(0.0f, targetVol, getFadeDurationSec(), PostFadeAction::None);
}

// ─── Volumen del canal principal ────────────────────────────────────
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
        engine->m_backgroundMusicChannel->setVolume(std::clamp(vol, 0.0f, 1.0f));
    }
}

// ─── Verificacion de playback ───────────────────────────────────────
bool DynamicSongManager::verifyPlayback() {
    if (!isActive() || m_activeSongPath.empty()) return false;
    if (!isInValidLayer()) return false;

    auto engine = FMODAudioEngine::sharedEngine();
    if (!engine || !engine->m_backgroundMusicChannel) return false;

    bool isPlaying = false;
    engine->m_backgroundMusicChannel->isPlaying(&isPlaying);
    if (!isPlaying) return false;

    auto* bgCh = getMainBgChannel(engine);
    if (!bgCh) return false;

    FMOD::Sound* currentSound = nullptr;
    bgCh->getCurrentSound(&currentSound);
    if (!currentSound) return false;

    char nameBuffer[512] = {};
    currentSound->getName(nameBuffer, sizeof(nameBuffer));
    std::string currentName(nameBuffer);
    if (currentName.empty()) return false;

    auto getFileName = [](const std::string& path) -> std::string {
        auto pos = path.find_last_of("/\\");
        return (pos != std::string::npos) ? path.substr(pos + 1) : path;
    };

    return getFileName(m_activeSongPath) == getFileName(currentName);
}

void DynamicSongManager::onPlaybackHijacked() {
    cancelFade();
    m_state = DynState::Idle;
    m_currentLayer = DynSongLayer::None;
    m_activeSongPath.clear();
    m_pendingSongPath.clear();
    m_currentPlayingLevelID = 0;
    paimon::setDynamicSongInteropActive(false);
    AudioContextCoordinator::get().clearDynamicAudio();
}

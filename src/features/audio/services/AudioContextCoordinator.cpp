#include "AudioContextCoordinator.hpp"

#include <Geode/binding/FMODAudioEngine.hpp>
#include <Geode/binding/GameLevelManager.hpp>
#include <Geode/binding/GameManager.hpp>

using namespace geode::prelude;

AudioContextCoordinator& AudioContextCoordinator::get() {
    static AudioContextCoordinator instance;
    return instance;
}

void AudioContextCoordinator::activateLevelSelect(int levelID, bool playImmediately) {
    m_gameplayActive = false;
    m_levelSelectLevelID = levelID;
    m_dynamicContextLayer = DynSongLayer::LevelSelect;

    auto* dsm = DynamicSongManager::get();
    dsm->enterLayer(DynSongLayer::LevelSelect);

    if (playImmediately && !m_profileOpen) {
        playDynamicForCurrentContext();
    }
}

void AudioContextCoordinator::deactivateLevelSelect(bool stopSong) {
    auto* dsm = DynamicSongManager::get();
    dsm->exitLayer(DynSongLayer::LevelSelect);

    if (m_dynamicContextLayer == DynSongLayer::LevelSelect) {
        m_dynamicContextLayer = DynSongLayer::None;
    }

    if (stopSong && !m_profileOpen) {
        dsm->stopSong();
    }
}

void AudioContextCoordinator::activateLevelInfo(GJGameLevel* level, bool playImmediately) {
    m_gameplayActive = false;
    m_levelInfoLevel = level;
    m_dynamicContextLayer = DynSongLayer::LevelInfo;

    auto* dsm = DynamicSongManager::get();
    dsm->enterLayer(DynSongLayer::LevelInfo);

    if (playImmediately && !m_profileOpen) {
        playDynamicForCurrentContext();
    }
}

void AudioContextCoordinator::deactivateLevelInfo(bool returnsToLevelSelect) {
    auto* dsm = DynamicSongManager::get();
    dsm->exitLayer(DynSongLayer::LevelInfo);

    if (returnsToLevelSelect) {
        m_dynamicContextLayer = DynSongLayer::LevelSelect;
        dsm->enterLayer(DynSongLayer::LevelSelect);
        return;
    }

    if (m_dynamicContextLayer == DynSongLayer::LevelInfo) {
        m_dynamicContextLayer = DynSongLayer::None;
    }

    if (!m_profileOpen && dsm->m_isDynamicSongActive) {
        dsm->stopSong();
    }
}

void AudioContextCoordinator::beginGameplayTransition() {
    m_gameplayActive = true;
    m_dynamicContextLayer = DynSongLayer::None;
    DynamicSongManager::get()->fadeOutForLevelStart();
}

void AudioContextCoordinator::notifyGameplayStarted() {
    m_gameplayActive = true;
    m_dynamicContextLayer = DynSongLayer::None;
    m_levelInfoLevel = nullptr;
    DynamicSongManager::get()->forceKill();
}

void AudioContextCoordinator::activateProfile(int accountID) {
    ++m_profileSessionToken;
    m_profileOpen = true;
    m_profileAccountID = accountID;
    m_gameplayActive = false;
    ProfileMusicManager::get().playProfileMusic(accountID);
}

void AudioContextCoordinator::activateProfile(int accountID, ProfileMusicManager::ProfileMusicConfig const& config) {
    ++m_profileSessionToken;
    m_profileOpen = true;
    m_profileAccountID = accountID;
    m_gameplayActive = false;
    ProfileMusicManager::get().playProfileMusic(accountID, config);
}

void AudioContextCoordinator::clearProfileContext() {
    m_profileOpen = false;
    m_profileAccountID = 0;
}

bool AudioContextCoordinator::shouldSuspendDynamicForProfileMusic() const {
    auto* dsm = DynamicSongManager::get();
    return dsm->m_isDynamicSongActive && !dsm->hasSuspendedPlayback();
}

void AudioContextCoordinator::suspendDynamicForProfileMusicIfNeeded() {
    if (shouldSuspendDynamicForProfileMusic()) {
        DynamicSongManager::get()->suspendPlaybackForExternalAudio();
    }
}

bool AudioContextCoordinator::restoreSuspendedDynamicSong() {
    auto* dsm = DynamicSongManager::get();
    if (!dsm->hasSuspendedPlayback()) {
        return false;
    }

    dsm->resumeSuspendedPlayback();
    return true;
}

void AudioContextCoordinator::restoreAfterProfileMusicStop(bool hadProfileAudio, uint32_t sessionToken) {
    if (sessionToken != m_profileSessionToken) {
        return;
    }

    if (restoreSuspendedDynamicSong()) {
        return;
    }

    if (playDynamicForCurrentContext(true)) {
        return;
    }

    if (hadProfileAudio) {
        restoreMenuMusic();
    }
}

void AudioContextCoordinator::handleProfileClosedAfterForceStop(bool hadProfileAudio, uint32_t sessionToken) {
    if (sessionToken != m_profileSessionToken) {
        return;
    }

    clearProfileContext();
    restoreAfterProfileMusicStop(hadProfileAudio, sessionToken);
}

bool AudioContextCoordinator::playDynamicForCurrentContext(bool ignoreProfileGate) {
    if ((m_profileOpen && !ignoreProfileGate) || m_gameplayActive) {
        return false;
    }

    if (!Mod::get()->getSettingValue<bool>("dynamic-song")) {
        return false;
    }

    auto* dsm = DynamicSongManager::get();
    switch (m_dynamicContextLayer) {
    case DynSongLayer::LevelInfo:
        if (m_levelInfoLevel) {
            dsm->enterLayer(DynSongLayer::LevelInfo);
            dsm->playSong(m_levelInfoLevel);
            return true;
        }
        return false;

    case DynSongLayer::LevelSelect:
        if (m_levelSelectLevelID > 0) {
            if (auto* level = GameLevelManager::sharedState()->getMainLevel(m_levelSelectLevelID, false)) {
                dsm->enterLayer(DynSongLayer::LevelSelect);
                dsm->playSong(level);
                return true;
            }
        }
        return false;

    case DynSongLayer::None:
    default:
        return false;
    }
}

void AudioContextCoordinator::restoreMenuMusic() {
    auto* engine = FMODAudioEngine::sharedEngine();
    auto* gm = GameManager::get();
    if (!engine || !gm) return;
    if (gm->getGameVariable("0122")) return;
    if (engine->m_musicVolume <= 0.0f) return;

    std::string menuTrack = gm->getMenuMusicFile();
    DynamicSongManager::s_selfPlayMusic = true;
    engine->playMusic(menuTrack, true, 0.0f, 0);
    DynamicSongManager::s_selfPlayMusic = false;

    if (engine->m_backgroundMusicChannel) {
        engine->m_backgroundMusicChannel->setVolume(engine->m_musicVolume);
    }
}
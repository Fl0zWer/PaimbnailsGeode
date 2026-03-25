#include "AudioContextCoordinator.hpp"

#include <Geode/binding/FMODAudioEngine.hpp>
#include <Geode/binding/GameLevelManager.hpp>
#include <Geode/binding/GameManager.hpp>

using namespace geode::prelude;

AudioContextCoordinator& AudioContextCoordinator::get() {
    static AudioContextCoordinator instance;
    return instance;
}

void AudioContextCoordinator::claimDynamicAudio() {
    m_mainAudioOwner = MainAudioOwner::Dynamic;
    m_mainAudioOwnerToken = 0;
}

void AudioContextCoordinator::clearDynamicAudio() {
    if (m_mainAudioOwner == MainAudioOwner::Dynamic) {
        m_mainAudioOwner = MainAudioOwner::None;
        m_mainAudioOwnerToken = 0;
    }
}

void AudioContextCoordinator::claimProfileAudio(uint32_t sessionToken) {
    m_mainAudioOwner = MainAudioOwner::Profile;
    m_mainAudioOwnerToken = sessionToken;
}

void AudioContextCoordinator::claimPreviewAudio(uint32_t sessionToken) {
    m_mainAudioOwner = MainAudioOwner::Preview;
    m_mainAudioOwnerToken = sessionToken;
}

void AudioContextCoordinator::releaseProfileLikeAudio(uint32_t sessionToken) {
    if ((m_mainAudioOwner == MainAudioOwner::Profile || m_mainAudioOwner == MainAudioOwner::Preview) &&
        m_mainAudioOwnerToken == sessionToken) {
        m_mainAudioOwner = MainAudioOwner::None;
        m_mainAudioOwnerToken = 0;
    }
}

bool AudioContextCoordinator::isCurrentProfileSession(uint32_t sessionToken) const {
    return sessionToken == m_profileSessionToken;
}

bool AudioContextCoordinator::isAudioOwnedByProfileSession(uint32_t sessionToken) const {
    return (m_mainAudioOwner == MainAudioOwner::Profile || m_mainAudioOwner == MainAudioOwner::Preview) &&
           m_mainAudioOwnerToken == sessionToken;
}

void AudioContextCoordinator::activateLevelSelect(int levelID, bool playImmediately) {
    m_gameplayActive = false;
    m_levelSelectLevelID = levelID;
    m_dynamicContextLayer = DynSongLayer::LevelSelect;

    if (m_profileOpen) {
        clearProfileContext();
    }

    auto* dsm = DynamicSongManager::get();
    dsm->enterLayer(DynSongLayer::LevelSelect);

    if (playImmediately) {
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

    // Si el perfil quedo abierto por error, limpiarlo — el usuario ya esta en otro layer
    if (m_profileOpen) {
        clearProfileContext();
    }

    auto* dsm = DynamicSongManager::get();
    dsm->enterLayer(DynSongLayer::LevelInfo);

    if (playImmediately) {
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

    if (!m_profileOpen) {
        if (dsm->isActive()) {
            dsm->stopSong(); // stopSong handles menu music restoration
        } else {
            restoreMenuMusic();
        }
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
    suspendDynamicForProfileMusicIfNeeded();
    ProfileMusicManager::get().playProfileMusic(accountID);
}

void AudioContextCoordinator::activateProfile(int accountID, ProfileMusicManager::ProfileMusicConfig const& config) {
    ++m_profileSessionToken;
    m_profileOpen = true;
    m_profileAccountID = accountID;
    m_gameplayActive = false;
    suspendDynamicForProfileMusicIfNeeded();
    ProfileMusicManager::get().playProfileMusic(accountID, config);
}

void AudioContextCoordinator::updateProfileMusicConfig(int accountID, ProfileMusicManager::ProfileMusicConfig const& config) {
    // Actualizar config sin incrementar session token (evita desincronizacion por doble activacion)
    m_profileOpen = true;
    m_profileAccountID = accountID;
    suspendDynamicForProfileMusicIfNeeded();
    ProfileMusicManager::get().playProfileMusic(accountID, config);
}

void AudioContextCoordinator::clearProfileContext() {
    m_profileOpen = false;
    m_profileAccountID = 0;
}

bool AudioContextCoordinator::shouldSuspendDynamicForProfileMusic() const {
    auto* dsm = DynamicSongManager::get();
    return dsm->isActive() && !dsm->hasSuspendedPlayback();
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

    // Verificar que la restauracion realmente inicio audio.
    // resumeSuspendedPlayback puede abortar por guards internos (menu music
    // desactivado, volumen 0, layer invalido) sin cargar nada.
    return dsm->isActive();
}

void AudioContextCoordinator::restoreAfterProfileMusicStop(bool hadProfileAudio, uint32_t sessionToken) {
    if (!isCurrentProfileSession(sessionToken)) {
        return;
    }

    releaseProfileLikeAudio(sessionToken);

    if (restoreSuspendedDynamicSong()) {
        return;
    }

    if (playDynamicForCurrentContext(true)) {
        return;
    }

    restoreMenuMusic();
}

void AudioContextCoordinator::handleProfileClosedAfterForceStop(bool hadProfileAudio, uint32_t sessionToken) {
    if (!isCurrentProfileSession(sessionToken)) {
        return;
    }

    clearProfileContext();
    releaseProfileLikeAudio(sessionToken);
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
    m_mainAudioOwner = MainAudioOwner::Menu;
    m_mainAudioOwnerToken = 0;

    if (engine->m_backgroundMusicChannel) {
        engine->m_backgroundMusicChannel->setVolume(engine->m_musicVolume);
    }
}

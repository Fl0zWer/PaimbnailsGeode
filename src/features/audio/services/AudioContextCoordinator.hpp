#pragma once

#include <Geode/Geode.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include "../../dynamic-songs/services/DynamicSongManager.hpp"
#include "../../profile-music/services/ProfileMusicManager.hpp"

class AudioContextCoordinator {
public:
    static AudioContextCoordinator& get();

    void activateLevelSelect(int levelID, bool playImmediately = true);
    void deactivateLevelSelect(bool stopSong = true);

    void activateLevelInfo(GJGameLevel* level, bool playImmediately = true);
    void deactivateLevelInfo(bool returnsToLevelSelect);

    void beginGameplayTransition();
    void notifyGameplayStarted();

    void activateProfile(int accountID);
    void activateProfile(int accountID, ProfileMusicManager::ProfileMusicConfig const& config);
    void clearProfileContext();
    void restoreAfterProfileMusicStop(bool hadProfileAudio, uint32_t sessionToken);
    void handleProfileClosedAfterForceStop(bool hadProfileAudio, uint32_t sessionToken);
    uint32_t getCurrentProfileSessionToken() const { return m_profileSessionToken; }
    bool shouldSuspendDynamicForProfileMusic() const;
    void suspendDynamicForProfileMusicIfNeeded();

    int getCurrentLevelSelectID() const { return m_levelSelectLevelID; }
    DynSongLayer getDynamicContextLayer() const { return m_dynamicContextLayer; }

private:
    int m_levelSelectLevelID = 0;
    geode::Ref<GJGameLevel> m_levelInfoLevel = nullptr;
    DynSongLayer m_dynamicContextLayer = DynSongLayer::None;
    bool m_profileOpen = false;
    int m_profileAccountID = 0;
    uint32_t m_profileSessionToken = 0;
    bool m_gameplayActive = false;

    bool playDynamicForCurrentContext(bool ignoreProfileGate = false);
    bool restoreSuspendedDynamicSong();
    void restoreMenuMusic();
};
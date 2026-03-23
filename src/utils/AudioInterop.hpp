#pragma once

#include <Geode/Geode.hpp>

namespace paimon {

inline std::string const& audioOwnedFlag() {
    static const std::string flag = geode::Mod::get()->getID() + "/audio-owned";
    return flag;
}

inline std::string const& profileMusicFlag() {
    static const std::string flag = geode::Mod::get()->getID() + "/profile-music-active";
    return flag;
}

inline std::string const& dynamicSongFlag() {
    static const std::string flag = geode::Mod::get()->getID() + "/dynamic-song-active";
    return flag;
}

inline bool& profileMusicInteropState() {
    static bool active = false;
    return active;
}

inline bool& dynamicSongInteropState() {
    static bool active = false;
    return active;
}

inline void syncAudioInteropFlags() {
    auto* director = cocos2d::CCDirector::sharedDirector();
    auto* scene = director ? director->getRunningScene() : nullptr;
    if (!scene) {
        return;
    }

    auto profileActive = profileMusicInteropState();
    auto dynamicActive = dynamicSongInteropState();
    scene->setUserFlag(profileMusicFlag(), profileActive);
    scene->setUserFlag(dynamicSongFlag(), dynamicActive);
    scene->setUserFlag(audioOwnedFlag(), profileActive || dynamicActive);
}

inline void setProfileMusicInteropActive(bool active) {
    profileMusicInteropState() = active;
    syncAudioInteropFlags();
}

inline void setDynamicSongInteropActive(bool active) {
    dynamicSongInteropState() = active;
    syncAudioInteropFlags();
}

inline bool isProfileMusicInteropActive() {
    return profileMusicInteropState();
}

inline bool isDynamicSongInteropActive() {
    return dynamicSongInteropState();
}

inline bool isAudioOwnedByPaimon() {
    return profileMusicInteropState() || dynamicSongInteropState();
}

} // namespace paimon
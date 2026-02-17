#pragma once
#include <Geode/Geode.hpp>

class DynamicSongManager {
public:
    bool m_isDynamicSongActive = false;
    unsigned int m_savedMenuPos = 0;

    static DynamicSongManager* get();
    void playSong(GJGameLevel* level);
    void stopSong();
};

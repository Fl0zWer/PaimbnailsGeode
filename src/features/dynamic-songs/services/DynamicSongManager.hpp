#pragma once
#include <Geode/Geode.hpp>
#include <Geode/binding/FMODAudioEngine.hpp>

// Layers donde la dynamic song puede sonar
enum class DynSongLayer {
    None,           // no deberia sonar
    LevelSelect,    // selector de niveles oficiales
    LevelInfo,      // info de nivel (online/custom)
};

class DynamicSongManager {
public:
    bool m_isDynamicSongActive = false;
    unsigned int m_savedMenuPos = 0;

    static DynamicSongManager* get();

    // Control de layer
    void enterLayer(DynSongLayer layer);
    void exitLayer(DynSongLayer layer);
    DynSongLayer getCurrentLayer() const { return m_currentLayer; }
    bool isInValidLayer() const { return m_currentLayer != DynSongLayer::None; }

    void playSong(GJGameLevel* level);
    void stopSong();
    void fadeOutForLevelStart();
    
    // Limpieza forzada desde PlayLayer::init
    void forceKill();

    // Detener/recrear el canal dinamico (para ProfileMusic u otros popups)
    // Cuando ProfileMusic toma control, el canal se destruye completamente
    // para evitar que mods externos lo reactiven y causen musica doble.
    void stopDynamicForProfileMusic();
    void replayLastSong();
    bool wasDynamicStoppedByProfile() const { return m_stoppedByProfile; }

    // Acceso al volumen del canal dinamico para crossfade desde ProfileMusic
    float getDynamicVolume() const;
    void setDynamicVolume(float vol);

private:
    DynSongLayer m_currentLayer = DynSongLayer::None;

    // Canal y sonido FMOD separados
    FMOD::Channel* m_musicChannel = nullptr;
    FMOD::Sound* m_currentSound = nullptr;

    // Crossfade
    static constexpr int FADE_STEPS = 20;
    bool m_isFadingIn = false;
    bool m_isFadingOut = false;
    float m_bgVolumeBeforeFade = 1.0f;
    bool m_stoppedByProfile = false;  // true si ProfileMusic destruyo el canal
    std::string m_lastSongPath;       // path del ultimo song para replay tras ProfileMusic

    bool isCrossfadeEnabled() const;
    float getFadeDurationMs() const;

    // BG channel helpers (NUNCA pausar, solo volumen)
    void loadMenuTrack(float startVolume);
    void restoreBgChannel();
    void silenceBgChannel();

    void fadeInDynamicSong(float targetVolume);
    void fadeOutAndRestore();
    void executeFadeStep(int step, int totalSteps, float dynFrom, float dynTo,
                         float bgFrom, float bgTo, bool restoreAfter);
    void stopCurrentAudio();
    void executeLevelStartFade(int step, int totalSteps, float volFrom);
};

#pragma once
#include <Geode/Geode.hpp>
#include <Geode/binding/FMODAudioEngine.hpp>
#include <unordered_map>
#include <memory>
#include <atomic>
#include <vector>
#include <string>

// Layers donde la dynamic song puede sonar
enum class DynSongLayer {
    None,           // no deberia sonar
    LevelSelect,    // selector de niveles oficiales
    LevelInfo,      // info de nivel (online/custom)
};

class DynamicSongManager {
public:
    ~DynamicSongManager();

    bool m_isDynamicSongActive = false;
    unsigned int m_savedMenuPos = 0;

    // Flag para permitir nuestras propias llamadas a playMusic
    static inline bool s_selfPlayMusic = false;

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

    // Detener/recrear la cancion dinamica (para ProfileMusic u otros popups)
    void stopDynamicForProfileMusic();
    void replayLastSong();
    bool wasDynamicStoppedByProfile() const { return m_stoppedByProfile; }

    // Acceso al volumen del canal principal para crossfade desde ProfileMusic
    float getDynamicVolume() const;
    void setDynamicVolume(float vol);

    // Verificacion de playback — detecta si otro mod cambio la musica
    bool verifyPlayback();

    // Estado de transicion (para evitar false-positives en verificacion)
    bool isTransitioning() const { return m_isTransitioning || m_isFadingIn || m_isFadingOut; }

    // Ceder control si otro mod cambio la musica
    void onPlaybackHijacked();

private:
    DynSongLayer m_currentLayer = DynSongLayer::None;

    // Crossfade song-to-song: canal temporal para la cancion saliente
    FMOD::Channel* m_fadeOutChannel = nullptr;
    FMOD::Sound* m_fadeOutSound = nullptr;

    // Crossfade
    static constexpr int FADE_STEPS = 20;
    bool m_isFadingIn = false;
    bool m_isFadingOut = false;
    bool m_isTransitioning = false;   // true durante crossfade cancion→cancion
    float m_bgVolumeBeforeFade = 1.0f;
    bool m_stoppedByProfile = false;
    std::string m_lastSongPath;       // path del ultimo song para replay tras ProfileMusic
    std::string m_expectedSongPath;   // path esperado en el canal principal (para verificacion)
    int m_currentPlayingLevelID = 0;
    unsigned int m_savedDynamicPosMs = 0;
    std::shared_ptr<std::atomic<bool>> m_lifetimeToken = std::make_shared<std::atomic<bool>>(true);

    bool isCrossfadeEnabled() const;
    float getFadeDurationMs() const;

    // Canal principal: usa engine->playMusic() -> m_backgroundMusicChannel
    void playOnMainChannel(const std::string& songPath, float startVolume);
    void loadMenuTrack(float startVolume);
    void restoreBgChannel();

    // Crossfade helpers
    void fadeInMainChannel(float targetVolume);
    void fadeOutAndRestore();
    void executeFadeStep(int step, int totalSteps, float mainFrom, float mainTo,
                         float fadeOutFrom, float fadeOutTo, bool restoreAfter);
    void executeSongTransition(int step, int totalSteps,
                               float newFrom, float newTo, float oldFrom, float oldTo);
    void executeLevelStartFade(int step, int totalSteps, float volFrom);
    void cleanupFadeOutChannel();

    // Seek aleatorio en el canal principal
    void applyRandomSeek();

    // ─── Rotacion de canciones por nivel ──────────────────────────
    std::unordered_map<int, std::vector<std::string>> m_songRotationCache;
    std::vector<std::string> getAllSongPaths(GJGameLevel* level);
    std::string getNextRotationSong(GJGameLevel* level);
};

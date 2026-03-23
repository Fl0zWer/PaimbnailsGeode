#pragma once
#include <Geode/Geode.hpp>
#include <Geode/binding/FMODAudioEngine.hpp>
#include <unordered_map>
#include <memory>
#include <atomic>
#include <cstdint>
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

    // Suspender/recrear la cancion dinamica cuando otro flujo toma el canal principal.
    void suspendPlaybackForExternalAudio();
    void resumeSuspendedPlayback();
    bool hasSuspendedPlayback() const { return m_playbackSuspendedExternally; }

    // Acceso al volumen del canal principal para crossfade desde ProfileMusic
    float getDynamicVolume() const;
    void setDynamicVolume(float vol);

    // Verificacion de playback — detecta si otro mod cambio la musica
    bool verifyPlayback();

    // Estado de transicion (para evitar false-positives en verificacion)
    bool isTransitioning() const { return m_isFadingIn || m_isFadingOut; }

    // Ceder control si otro mod cambio la musica
    void onPlaybackHijacked();

private:
    DynSongLayer m_currentLayer = DynSongLayer::None;

    // Dip-fade (solo usa el canal principal, sin canales temporales)
    static constexpr int FADE_STEPS = 20;
    bool m_isFadingIn = false;
    bool m_isFadingOut = false;
    uint32_t m_fadeGeneration = 0;
    float m_bgVolumeBeforeFade = 1.0f;
    bool m_playbackSuspendedExternally = false;
    std::string m_lastSongPath;       // path del ultimo song para restaurar tras una suspension externa
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

    // Dip-fade helpers (solo canal principal, sin canales temporales)
    void fadeInMainChannel(float targetVolume, uint32_t generation);
    void fadeOutAndRestore(uint32_t generation);
    void executeDipFadeOut(int step, int totalSteps, float volFrom, float volTo, bool restoreMenu, uint32_t generation);
    void executeDipFadeIn(int step, int totalSteps, float volFrom, float volTo, uint32_t generation);
    void executeLevelStartFade(int step, int totalSteps, float volFrom, uint32_t generation);

    // Dip fade para transicion cancion→cancion
    std::string m_pendingSongPath;      // path pendiente para cargar cuando vol llegue a 0
    float m_pendingTargetVolume = 0.0f; // volumen objetivo tras cargar la nueva cancion

    // Seek aleatorio en el canal principal
    void applyRandomSeek();

    // ─── Rotacion de canciones por nivel ──────────────────────────
    std::unordered_map<int, std::vector<std::string>> m_songRotationCache;
    static constexpr size_t MAX_ROTATION_CACHE_LEVELS = 256;
    std::vector<std::string> getAllSongPaths(GJGameLevel* level);
    std::string getNextRotationSong(GJGameLevel* level);
};

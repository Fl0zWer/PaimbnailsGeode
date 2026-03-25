#pragma once
#include <Geode/Geode.hpp>
#include <Geode/binding/FMODAudioEngine.hpp>
#include <unordered_map>
#include <vector>
#include <string>

// Layers donde la dynamic song puede sonar
enum class DynSongLayer {
    None,           // no deberia sonar
    LevelSelect,    // selector de niveles oficiales
    LevelInfo,      // info de nivel (online/custom)
};

// Estado de la maquina de estados
enum class DynState {
    Idle,       // sin dynamic song activa, menu (o nada) suena
    FadingIn,   // cancion cargada, volumen subiendo 0 -> target
    Playing,    // cancion a volumen completo, estado estable
    FadingOut,  // volumen bajando, m_postFadeAction decide que pasa al llegar a 0
    Suspended,  // pausado para audio externo (profile music)
};

class DynSongFadeNode; // definido en .cpp

class DynamicSongManager {
public:
    ~DynamicSongManager();
    static DynamicSongManager* get();

    // --- Queries ---
    bool isActive() const { return m_state != DynState::Idle; }
    bool isFading() const { return m_state == DynState::FadingIn || m_state == DynState::FadingOut; }
    bool isInValidLayer() const { return m_currentLayer != DynSongLayer::None; }
    DynSongLayer getCurrentLayer() const { return m_currentLayer; }
    DynState getState() const { return m_state; }
    bool hasSuspendedPlayback() const { return m_state == DynState::Suspended; }

    // --- Control de layer ---
    void enterLayer(DynSongLayer layer);
    void exitLayer(DynSongLayer layer);

    // --- Playback ---
    void playSong(GJGameLevel* level);
    void stopSong();
    void fadeOutForLevelStart();
    void forceKill();

    // --- Coordinacion con profile music ---
    void suspendPlaybackForExternalAudio();
    void resumeSuspendedPlayback();

    // --- Volumen del canal principal (interfaz para ProfileMusic) ---
    float getDynamicVolume() const;
    void setDynamicVolume(float vol);

    // --- Verificacion de playback ---
    bool verifyPlayback();
    void onPlaybackHijacked();

    // --- Hook bypass ---
    static inline bool s_selfPlayMusic = false;

    // Callback del fade node (publico porque DynSongFadeNode lo llama)
    void onFadeComplete();

private:
    DynState m_state = DynState::Idle;
    DynSongLayer m_currentLayer = DynSongLayer::None;

    std::string m_activeSongPath;
    int m_currentPlayingLevelID = 0;
    unsigned int m_savedMenuPos = 0;
    unsigned int m_savedDynamicPosMs = 0;

    DynSongFadeNode* m_fadeNode = nullptr;
    enum class PostFadeAction { None, PlayPending, RestoreMenu, Cleanup };
    PostFadeAction m_postFadeAction = PostFadeAction::None;
    std::string m_pendingSongPath;

    // Rotacion de canciones por nivel
    std::unordered_map<int, std::vector<std::string>> m_songRotationCache;
    static constexpr size_t MAX_ROTATION_CACHE_LEVELS = 256;

    float getFadeDurationSec() const;
    void playOnMainChannel(const std::string& songPath, float startVolume);
    void loadMenuTrack(float startVolume);
    void applyRandomSeek();

    void fadeVolume(float from, float to, float durationSec, PostFadeAction action);
    void cancelFade();

    std::vector<std::string> getAllSongPaths(GJGameLevel* level);
    std::string getNextRotationSong(GJGameLevel* level);
};

#pragma once

#include <Geode/Geode.hpp>
#include <Geode/utils/function.hpp>
#include <Geode/binding/FMODAudioEngine.hpp>
#include <string>
#include <vector>
#include <filesystem>

class ProfileMusicManager {
public:
    // Configuracion de musica del perfil
    struct ProfileMusicConfig {
        int songID = 0;           // ID de la cancion en Newgrounds
        int startMs = 0;          // Milisegundo de inicio
        int endMs = 20000;        // Milisegundo de fin (max 20 segundos)
        float volume = 0.7f;      // Volumen (0.0 - 1.0)
        bool enabled = true;      // Si esta habilitada
        std::string songName;     // Nombre de la cancion
        std::string artistName;   // Nombre del artista
        std::string updatedAt;    // Timestamp de ultima actualizacion (del servidor, para cache validation)
    };

    // callbacks con CopyableFunction pa Geode v5
    using ConfigCallback = geode::CopyableFunction<void(bool success, const ProfileMusicConfig& config)>;
    using UploadCallback = geode::CopyableFunction<void(bool success, std::string const& message)>;
    using DownloadCallback = geode::CopyableFunction<void(bool success, std::string const& localPath)>;
    using WaveformCallback = geode::CopyableFunction<void(bool success, std::vector<float> const& peaks, int durationMs)>;
    using SongInfoCallback = geode::CopyableFunction<void(bool success, std::string const& name, std::string const& artist, int durationMs)>;

    static ProfileMusicManager& get() {
        static ProfileMusicManager instance;
        return instance;
    }

    // configuracion

    void getProfileMusicConfig(int accountID, ConfigCallback callback);

    void uploadProfileMusic(int accountID, std::string const& username, const ProfileMusicConfig& config, UploadCallback callback);

    void deleteProfileMusic(int accountID, std::string const& username, UploadCallback callback);

    // reproduccion

    void playProfileMusic(int accountID);

    void playProfileMusic(int accountID, ProfileMusicConfig const& config);

    void pauseProfileMusic();

    void resumeProfileMusic();

    void stopProfileMusic();

    bool isPlaying() const { return m_isPlaying; }

    bool isPaused() const { return m_isPaused; }

    bool isFadingOut() const { return m_isFadingOut; }

    int getCurrentPlayingProfile() const { return m_currentProfileID; }

    float getCurrentAmplitude() const;

    void applyCaveEffect();

    void removeCaveEffect();

    void forceRemoveCaveEffect();

    void forceStop();

    // waveform y vista previa

    void getWaveformPeaks(int songID, WaveformCallback callback);

    void getSongInfo(int songID, SongInfoCallback callback);

    void downloadSongForPreview(int songID, DownloadCallback callback);

    void playPreview(std::string const& filePath, int startMs, int endMs);

    void stopPreview();

    // cache

    bool isCached(int accountID);

    const ProfileMusicConfig* getCachedConfig(int accountID) const;

    std::filesystem::path getCachePath(int accountID);

    void clearCache();

    void invalidateCache(int accountID);

    // ajustes

    bool isEnabled() const;

    float getGlobalVolume() const;

private:
    ProfileMusicManager();
    ~ProfileMusicManager() = default;

    ProfileMusicManager(const ProfileMusicManager&) = delete;
    ProfileMusicManager& operator=(const ProfileMusicManager&) = delete;

    // estado de reproduccion
    bool m_isPlaying = false;
    bool m_isPaused = false;
    int m_currentProfileID = 0;
    std::string m_currentAudioPath;

    // canal aparte para la musica del perfil
    FMOD::Channel* m_musicChannel = nullptr;
    FMOD::Sound* m_currentSound = nullptr;

    // datos pendientes mientras arranca en async
    int m_pendingStartMs = 0;
    int m_pendingEndMs = 0;

    // crossfade
    static constexpr int FADE_STEPS = 20;               // Pasos de interpolacion
    bool m_isFadingIn = false;
    bool m_isFadingOut = false;
    float m_bgVolumeBeforeFade = 1.0f;  // Volumen original de la musica de fondo
    unsigned int m_savedBgPosMs = 0;     // Posicion del BG al abrir perfil (restaurar al salir)

    bool isCrossfadeEnabled() const;
    float getFadeDurationMs() const;

    void fadeInProfileMusic(float targetVolume);
    void fadeOutAndStop();
    void executeFadeStep(int step, int totalSteps, float fromVol, float toVol,
                         float bgFromVol, float bgToVol, bool stopAfter);
    void executeCrossfadeWithDynamic(int step, int totalSteps, float profileFrom, float profileTo, float dynFrom);

    // configs guardadas en memoria
    std::map<int, ProfileMusicConfig> m_configCache;

    // carpeta del cache
    std::filesystem::path getCacheDir();

    // ruta del .meta del audio cacheado
    std::filesystem::path getMetaPath(int accountID);

    // guarda metadata junto al audio para detectar cambios
    void saveMetaFile(int accountID, ProfileMusicConfig const& config);

    // revisa si el cache sigue sirviendo para la config actual
    bool isCacheValid(int accountID, ProfileMusicConfig const& config);

    // baja el fragmento desde el servidor
    void downloadMusicFragment(int accountID, DownloadCallback callback);

    // saca el waveform y la duracion de un archivo
    std::vector<float> analyzeWaveform(std::string const& filePath, int numPeaks, int& outDurationMs);

    // recorta un fragmento como WAV
    std::vector<uint8_t> extractAudioFragment(std::string const& filePath, int startMs, int endMs);

    // apoyo interno
    void playAudioFile(std::string const& path, bool loop, int startMs = 0, int endMs = 0);
    void playProfileMusicWithConfig(int accountID, ProfileMusicConfig const& config);
    void checkSoundReady();
    void finishPlayback();
    void stopCurrentAudio();
    void reloadBgMusic(float startVolume);

    // efecto cueva
    FMOD::DSP* m_lowpassDSP = nullptr;
    bool m_caveEffectActive = false;
    bool m_caveTransitioning = false;
    float m_originalFrequency = 0.0f;
    float m_originalVolume = 0.0f;
    void executeCaveTransitionStep(int step, int totalSteps, float cutoffFrom, float cutoffTo,
                                    float freqFrom, float freqTo, float volFrom, float volTo, bool applying);
};


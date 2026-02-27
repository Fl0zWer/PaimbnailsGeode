#pragma once

#include <Geode/Geode.hpp>
#include <Geode/binding/FMODAudioEngine.hpp>
#include <functional>
#include <string>
#include <vector>
#include <filesystem>

using namespace geode::prelude;

/**
 * ProfileMusicManager - Gestiona la música personalizada de perfiles
 * Descarga, cachea y reproduce fragmentos de audio de Newgrounds
 */
class ProfileMusicManager {
public:
    // Configuración de música del perfil
    struct ProfileMusicConfig {
        int songID = 0;           // ID de la canción en Newgrounds
        int startMs = 0;          // Milisegundo de inicio
        int endMs = 20000;        // Milisegundo de fin (máx 20 segundos)
        float volume = 0.7f;      // Volumen (0.0 - 1.0)
        bool enabled = true;      // Si está habilitada
        std::string songName;     // Nombre de la canción
        std::string artistName;   // Nombre del artista
    };

    // Callbacks
    using ConfigCallback = std::function<void(bool success, const ProfileMusicConfig& config)>;
    using UploadCallback = std::function<void(bool success, const std::string& message)>;
    using DownloadCallback = std::function<void(bool success, const std::string& localPath)>;
    using WaveformCallback = std::function<void(bool success, const std::vector<float>& peaks, int durationMs)>;
    using SongInfoCallback = std::function<void(bool success, const std::string& name, const std::string& artist, int durationMs)>;

    static ProfileMusicManager& get() {
        static ProfileMusicManager instance;
        return instance;
    }

    // === CONFIGURACIÓN ===

    /**
     * Obtiene la configuración de música de un perfil desde el servidor
     */
    void getProfileMusicConfig(int accountID, ConfigCallback callback);

    /**
     * Sube la configuración de música del perfil al servidor
     * El servidor descargará y cortará el audio de Newgrounds
     */
    void uploadProfileMusic(int accountID, const std::string& username, const ProfileMusicConfig& config, UploadCallback callback);

    /**
     * Elimina la música del perfil
     */
    void deleteProfileMusic(int accountID, const std::string& username, UploadCallback callback);

    // === REPRODUCCIÓN ===

    /**
     * Reproduce la música del perfil de un usuario
     * Descarga el fragmento si no está en cache
     */
    void playProfileMusic(int accountID);

    /**
     * Pausa la música del perfil actual
     */
    void pauseProfileMusic();

    /**
     * Reanuda la música pausada
     */
    void resumeProfileMusic();

    /**
     * Detiene completamente la música del perfil
     */
    void stopProfileMusic();

    /**
     * Verifica si hay música reproduciéndose
     */
    bool isPlaying() const { return m_isPlaying; }

    /**
     * Verifica si la música está pausada
     */
    bool isPaused() const { return m_isPaused; }

    /**
     * Verifica si hay un fade-out en curso
     */
    bool isFadingOut() const { return m_isFadingOut; }

    /**
     * Obtiene el accountID del perfil que está sonando
     */
    int getCurrentPlayingProfile() const { return m_currentProfileID; }

    /**
     * Obtiene la amplitud actual del canal de musica del perfil (0.0 - 1.0)
     * Para usar en efectos visuales (pulso de brillo, etc)
     */
    float getCurrentAmplitude() const;

    /**
     * Aplica efecto "cueva" a la música del perfil: lowpass filter + pitch más lento.
     * Usado cuando se abre InfoLayer (comentarios) para distinguirlo del perfil.
     * Transición suave con fade gradual del DSP.
     */
    void applyCaveEffect();

    /**
     * Quita el efecto "cueva" y restaura la reproducción normal.
     * Transición suave con fade gradual del DSP.
     */
    void removeCaveEffect();

    /**
     * Fuerza la detención inmediata de toda la reproducción.
     * Ignora fade-out en curso y limpia todo el estado.
     * Usar cuando se necesita garantizar que no hay audio activo.
     */
    void forceStop();

    // === WAVEFORM / VISUALIZACIÓN ===

    /**
     * Obtiene los picos de audio de una canción de Newgrounds para visualización
     * Descarga la canción temporalmente y analiza su waveform
     */
    void getWaveformPeaks(int songID, WaveformCallback callback);

    /**
     * Obtiene información de una canción de Newgrounds (nombre, artista, duración)
     */
    void getSongInfo(int songID, SongInfoCallback callback);

    /**
     * Descarga una canción de Newgrounds para preview
     */
    void downloadSongForPreview(int songID, DownloadCallback callback);

    /**
     * Reproduce un preview de la canción desde un punto específico
     */
    void playPreview(const std::string& filePath, int startMs, int endMs);

    /**
     * Detiene el preview
     */
    void stopPreview();

    // === CACHE ===

    /**
     * Verifica si el fragmento de música de un perfil está en cache
     */
    bool isCached(int accountID);

    /**
     * Obtiene la ruta del archivo cacheado
     */
    std::filesystem::path getCachePath(int accountID);

    /**
     * Limpia el cache de música de perfiles
     */
    void clearCache();

    // === SETTINGS ===

    /**
     * Verifica si la música de perfiles está habilitada globalmente
     */
    bool isEnabled() const;

    /**
     * Obtiene el volumen de música del juego (para aplicar a la música de perfil)
     */
    float getGlobalVolume() const;

private:
    ProfileMusicManager();
    ~ProfileMusicManager() = default;

    ProfileMusicManager(const ProfileMusicManager&) = delete;
    ProfileMusicManager& operator=(const ProfileMusicManager&) = delete;

    // Estado de reproducción
    bool m_isPlaying = false;
    bool m_isPaused = false;
    int m_currentProfileID = 0;
    std::string m_currentAudioPath;

    // FMOD channel para música de perfil (usamos canal separado)
    FMOD::Channel* m_musicChannel = nullptr;
    FMOD::Sound* m_currentSound = nullptr;

    // Parámetros pendientes para reproducción asíncrona
    int m_pendingStartMs = 0;
    int m_pendingEndMs = 0;

    // Crossfade / transición suave
    static constexpr int FADE_STEPS = 20;               // Pasos de interpolación
    bool m_isFadingIn = false;
    bool m_isFadingOut = false;
    float m_bgVolumeBeforeFade = 1.0f;  // Volumen original de la música de fondo

    bool isCrossfadeEnabled() const;
    float getFadeDurationMs() const;

    void fadeInProfileMusic(float targetVolume);
    void fadeOutAndStop();
    void executeFadeStep(int step, int totalSteps, float fromVol, float toVol,
                         float bgFromVol, float bgToVol, bool stopAfter);
    void executeCrossfadeWithDynamic(int step, int totalSteps, float profileFrom, float profileTo, float dynFrom);

    // Cache de configuraciones
    std::map<int, ProfileMusicConfig> m_configCache;

    // Path del directorio de cache
    std::filesystem::path getCacheDir();

    // Descarga el fragmento de audio del servidor
    void downloadMusicFragment(int accountID, DownloadCallback callback);

    // Analiza waveform de un archivo de audio y devuelve duración
    std::vector<float> analyzeWaveform(const std::string& filePath, int numPeaks, int& outDurationMs);

    // Extrae un fragmento de audio como WAV
    std::vector<uint8_t> extractAudioFragment(const std::string& filePath, int startMs, int endMs);

    // Helpers
    void playAudioFile(const std::string& path, bool loop, int startMs = 0, int endMs = 0);
    void checkSoundReady();
    void finishPlayback();
    void stopCurrentAudio();

    // Efecto cueva (lowpass + pitch)
    FMOD::DSP* m_lowpassDSP = nullptr;
    bool m_caveEffectActive = false;
    bool m_caveTransitioning = false;
    float m_originalFrequency = 0.0f;
    float m_originalVolume = 0.0f;
    void executeCaveTransitionStep(int step, int totalSteps, float cutoffFrom, float cutoffTo,
                                    float freqFrom, float freqTo, float volFrom, float volTo, bool applying);
};




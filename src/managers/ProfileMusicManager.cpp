#include "ProfileMusicManager.hpp"
#include "DynamicSongManager.hpp"
#include "../utils/HttpClient.hpp"
#include <Geode/binding/MusicDownloadManager.hpp>
#include <Geode/binding/SongInfoObject.hpp>
#include <Geode/loader/Mod.hpp>
#include <Geode/utils/string.hpp>
#include <fstream>
#include <cmath>
#include <thread>
#include <chrono>

using namespace geode::prelude;

ProfileMusicManager::ProfileMusicManager() {
    std::error_code ec;
    std::filesystem::create_directories(getCacheDir(), ec);
}

std::filesystem::path ProfileMusicManager::getCacheDir() {
    return Mod::get()->getSaveDir() / "profile_music";
}

std::filesystem::path ProfileMusicManager::getCachePath(int accountID) {
    return getCacheDir() / fmt::format("{}.mp3", accountID);
}

bool ProfileMusicManager::isCached(int accountID) {
    std::error_code ec;
    return std::filesystem::exists(getCachePath(accountID), ec);
}

bool ProfileMusicManager::isEnabled() const {
    return Mod::get()->getSettingValue<bool>("profile-music-enabled");
}

bool ProfileMusicManager::isCrossfadeEnabled() const {
    return Mod::get()->getSettingValue<bool>("profile-music-crossfade");
}

float ProfileMusicManager::getFadeDurationMs() const {
    float seconds = static_cast<float>(Mod::get()->getSettingValue<double>("profile-music-fade-duration"));
    return seconds * 1000.0f;
}

float ProfileMusicManager::getGlobalVolume() const {
    // Usar el volumen de musica configurado en los ajustes del juego
    auto engine = FMODAudioEngine::sharedEngine();
    if (engine) {
        return engine->m_musicVolume;
    }
    return 1.0f;
}

void ProfileMusicManager::getProfileMusicConfig(int accountID, ConfigCallback callback) {
    // Siempre consultar al servidor para obtener la config mas reciente.
    // El cache en RAM causaba que musica actualizada no se detectara hasta
    // reiniciar el juego (stale config bug).
    // Agregar timestamp para cache-busting de Cloudflare/CDN.
    auto now = std::chrono::system_clock::now().time_since_epoch();
    auto ts = std::chrono::duration_cast<std::chrono::seconds>(now).count();
    std::string endpoint = fmt::format("/api/profile-music/{}?t={}", accountID, ts);
    log::info("[ProfileMusic] Fetching config from: {}", endpoint);

    HttpClient::get().get(endpoint, [this, accountID, callback](bool success, std::string const& response) {
        Loader::get()->queueInMainThread([this, accountID, callback, success, response]() {
            if (!success) {
                log::error("[ProfileMusic] Failed to fetch config for account {}: {}", accountID, response);
                callback(false, ProfileMusicConfig{});
                return;
            }

            log::info("[ProfileMusic] Received response for account {}: {}", accountID, response.substr(0, 200));

            auto parsed = matjson::parse(response);
            if (!parsed.isOk()) {
                log::error("[ProfileMusic] Failed to parse JSON for account {}", accountID);
                callback(false, ProfileMusicConfig{});
                return;
            }

            auto root = parsed.unwrap();
            if (root.contains("error") || !root.contains("songID")) {
                log::warn("[ProfileMusic] No music config found for account {}", accountID);
                callback(false, ProfileMusicConfig{});
                return;
            }

            ProfileMusicConfig config;
            config.songID = root["songID"].asInt().unwrapOr(0);
            config.startMs = root["startMs"].asInt().unwrapOr(0);
            config.endMs = root["endMs"].asInt().unwrapOr(20000);
            config.volume = static_cast<float>(root["volume"].asDouble().unwrapOr(0.7));
            config.enabled = root["enabled"].asBool().unwrapOr(true);
            config.songName = root["songName"].asString().unwrapOr("");
            config.artistName = root["artistName"].asString().unwrapOr("");
            config.updatedAt = root["updatedAt"].asString().unwrapOr("");

            log::info("[ProfileMusic] Config loaded for account {}: songID={}, enabled={}", accountID, config.songID, config.enabled);

            m_configCache[accountID] = config;
            callback(true, config);
        });
    });
}

void ProfileMusicManager::uploadProfileMusic(int accountID, std::string const& username, const ProfileMusicConfig& config, UploadCallback callback) {
    // Primero descargar la cancion completa localmente
    downloadSongForPreview(config.songID, [this, accountID, username, config, callback](bool success, std::string const& localPath) {
        if (!success || localPath.empty()) {
            Loader::get()->queueInMainThread([callback]() {
                callback(false, "Could not download song. Press the Download button first.");
            });
            return;
        }

        // Extraer el fragmento de audio en un thread separado
        std::thread([this, localPath, accountID, username, config, callback]() {
            // Extraer solo el fragmento usando FMOD
            auto fragmentData = extractAudioFragment(localPath, config.startMs, config.endMs);

            if (fragmentData.empty()) {
                Loader::get()->queueInMainThread([callback]() {
                    callback(false, "Could not extract audio fragment");
                });
                return;
            }

            log::info("[ProfileMusic] Extracted fragment: {} bytes ({}ms - {}ms)",
                fragmentData.size(), config.startMs, config.endMs);

            // Convertir a base64
            static char const* base64Chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
            std::string base64;
            size_t size = fragmentData.size();
            base64.reserve(((size + 2) / 3) * 4);

            for (size_t i = 0; i < size; i += 3) {
                unsigned int n = static_cast<unsigned char>(fragmentData[i]) << 16;
                if (i + 1 < size) n |= static_cast<unsigned char>(fragmentData[i + 1]) << 8;
                if (i + 2 < size) n |= static_cast<unsigned char>(fragmentData[i + 2]);

                base64 += base64Chars[(n >> 18) & 0x3F];
                base64 += base64Chars[(n >> 12) & 0x3F];
                base64 += (i + 1 < size) ? base64Chars[(n >> 6) & 0x3F] : '=';
                base64 += (i + 2 < size) ? base64Chars[n & 0x3F] : '=';
            }

            log::info("[ProfileMusic] Uploading fragment: {} bytes ({} base64 chars)",
                size, base64.size());

            // Crear payload con el fragmento ya recortado
            matjson::Value payload;
            payload["accountID"] = accountID;
            payload["username"] = username;
            payload["songID"] = config.songID;
            payload["startMs"] = config.startMs;
            payload["endMs"] = config.endMs;
            payload["volume"] = config.volume;
            payload["songName"] = config.songName;
            payload["artistName"] = config.artistName;
            payload["audioData"] = base64;

            std::string jsonData = payload.dump();

            Loader::get()->queueInMainThread([this, jsonData, accountID, config, callback]() {
                HttpClient::get().postWithAuth("/api/profile-music/upload", jsonData, [this, accountID, config, callback](bool success, std::string const& response) {
                    Loader::get()->queueInMainThread([this, accountID, config, callback, success, response]() {
                        if (success) {
                            // Invalidar todo el cache para este account (RAM + disco + meta)
                            // para forzar re-descarga del nuevo fragmento
                            invalidateCache(accountID);
                            callback(true, "Music uploaded successfully");
                        } else {
                            callback(false, response);
                        }
                    });
                });
            });
        }).detach();
    });
}

std::vector<uint8_t> ProfileMusicManager::extractAudioFragment(std::string const& filePath, int startMs, int endMs) {
    std::vector<uint8_t> result;

    // Leer el archivo MP3 completo
    std::ifstream file(filePath, std::ios::binary | std::ios::ate);
    if (!file) {
        log::error("[ProfileMusic] Cannot open file: {}", filePath);
        return result;
    }

    std::streamsize fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> mp3Data(fileSize);
    if (!file.read(reinterpret_cast<char*>(mp3Data.data()), fileSize)) {
        log::error("[ProfileMusic] Cannot read file");
        return result;
    }
    file.close();

    // Obtener duracion total del archivo usando FMOD
    auto engine = FMODAudioEngine::sharedEngine();
    if (!engine || !engine->m_system) {
        log::error("[ProfileMusic] FMOD not available");
        return result;
    }

    FMOD::Sound* sound = nullptr;
    FMOD_RESULT fmResult = engine->m_system->createSound(filePath.c_str(), FMOD_OPENONLY | FMOD_ACCURATETIME, nullptr, &sound);
    if (fmResult != FMOD_OK || !sound) {
        log::error("[ProfileMusic] Cannot get duration");
        return result;
    }

    unsigned int totalDurationMs = 0;
    sound->getLength(&totalDurationMs, FMOD_TIMEUNIT_MS);
    sound->release();

    if (totalDurationMs == 0) {
        log::error("[ProfileMusic] Invalid duration");
        return result;
    }

    // Validar rangos
    if (endMs > static_cast<int>(totalDurationMs) || endMs <= 0) endMs = totalDurationMs;
    if (startMs < 0) startMs = 0;
    if (startMs >= endMs) {
        log::error("[ProfileMusic] Invalid time range");
        return result;
    }

    int durationMs = endMs - startMs;

    // Calcular posicion aproximada en bytes basada en el tiempo
    // MP3 tiene bitrate variable pero podemos aproximar
    double startRatio = static_cast<double>(startMs) / totalDurationMs;
    double endRatio = static_cast<double>(endMs) / totalDurationMs;

    size_t startByte = static_cast<size_t>(startRatio * fileSize);
    size_t endByte = static_cast<size_t>(endRatio * fileSize);

    // Buscar el inicio de un frame MP3 valido cerca de startByte
    // Un frame MP3 comienza con sync word 0xFF 0xFB (o 0xFF 0xFA, 0xFF 0xF3, etc.)
    size_t frameStart = startByte;
    for (size_t i = startByte; i < std::min(startByte + 4096, mp3Data.size() - 1); i++) {
        if (mp3Data[i] == 0xFF && (mp3Data[i + 1] & 0xE0) == 0xE0) {
            frameStart = i;
            break;
        }
    }

    // Buscar el final de un frame cerca de endByte
    size_t frameEnd = endByte;
    for (size_t i = endByte; i < std::min(endByte + 4096, mp3Data.size() - 1); i++) {
        if (mp3Data[i] == 0xFF && (mp3Data[i + 1] & 0xE0) == 0xE0) {
            frameEnd = i;
            break;
        }
    }

    if (frameEnd <= frameStart) {
        frameEnd = mp3Data.size();
    }

    size_t fragmentSize = frameEnd - frameStart;

    log::info("[ProfileMusic] Extracting MP3 fragment: {}ms-{}ms, bytes {}-{} ({} bytes, original {} bytes)",
        startMs, endMs, frameStart, frameEnd, fragmentSize, fileSize);

    // Copiar el fragmento MP3
    result.resize(fragmentSize);
    std::memcpy(result.data(), &mp3Data[frameStart], fragmentSize);

    log::info("[ProfileMusic] Created MP3 fragment: {} bytes (compression ratio: {:.1f}x vs WAV)",
        result.size(), (durationMs * 44100.0 * 4 / 1000) / result.size());

    return result;
}

void ProfileMusicManager::deleteProfileMusic(int accountID, std::string const& username, UploadCallback callback) {
    matjson::Value payload;
    payload["accountID"] = accountID;
    payload["username"] = username;

    std::string jsonData = payload.dump();

    HttpClient::get().postWithAuth("/api/profile-music/delete", jsonData, [this, accountID, callback](bool success, std::string const& response) {
        Loader::get()->queueInMainThread([this, accountID, callback, success, response]() {
            if (success) {
                invalidateCache(accountID);
                callback(true, "Music deleted successfully");
            } else {
                callback(false, response);
            }
        });
    });
}

void ProfileMusicManager::downloadMusicFragment(int accountID, DownloadCallback callback) {
    // Agregar timestamp como query param para cache-busting (evita que CDN/Cloudflare
    // devuelva un archivo mp3 viejo cacheado en su edge)
    auto now = std::chrono::system_clock::now().time_since_epoch();
    auto ts = std::chrono::duration_cast<std::chrono::seconds>(now).count();
    std::string url = fmt::format("{}/profile-music/{}.mp3?t={}", HttpClient::get().getServerURL(), accountID, ts);

    log::info("[ProfileMusic] Downloading music from: {}", url);

    // Usar descarga binaria sin validacion de imagen (es un MP3, no una imagen)
    HttpClient::get().downloadFromUrlRaw(url, [this, accountID, callback](bool success, std::vector<uint8_t> const& data, int, int) {
        if (!success || data.empty()) {
            log::error("[ProfileMusic] Failed to download music for account {}", accountID);
            Loader::get()->queueInMainThread([callback]() {
                callback(false, "");
            });
            return;
        }

        auto cachePath = getCachePath(accountID);
        std::error_code ec;
        std::filesystem::create_directories(cachePath.parent_path(), ec);

        std::ofstream file(cachePath, std::ios::binary);
        if (file) {
            file.write(reinterpret_cast<char const*>(data.data()), data.size());
            file.close();
            log::info("[ProfileMusic] Downloaded and cached music for account {} ({} bytes)", accountID, data.size());
            Loader::get()->queueInMainThread([callback, cachePath]() {
                callback(true, geode::utils::string::pathToString(cachePath));
            });
        } else {
            log::error("[ProfileMusic] Failed to write cache file for account {}", accountID);
            Loader::get()->queueInMainThread([callback]() {
                callback(false, "");
            });
        }
    });
}

void ProfileMusicManager::playProfileMusic(int accountID) {
    log::info("[ProfileMusic] playProfileMusic called for account {}", accountID);

    if (!isEnabled()) {
        log::info("[ProfileMusic] Profile music is disabled in settings");
        return;
    }

    if (m_isPlaying && m_currentProfileID == accountID && !m_isPaused && !m_isFadingOut) {
        log::info("[ProfileMusic] Already playing music for this account");
        return;
    }

    forceStop();
    m_currentProfileID = accountID;

    // Consultar la config del servidor y delegar al overload con config
    getProfileMusicConfig(accountID, [this, accountID](bool success, ProfileMusicConfig const& config) {
        if (!success || config.songID <= 0 || !config.enabled) {
            log::info("[ProfileMusic] No valid config from server for account {}", accountID);
            return;
        }
        if (m_currentProfileID != accountID) {
            log::info("[ProfileMusic] Profile changed while fetching config, aborting");
            return;
        }
        playProfileMusicWithConfig(accountID, config);
    });
}

void ProfileMusicManager::playProfileMusic(int accountID, ProfileMusicConfig const& config) {
    log::info("[ProfileMusic] playProfileMusic (with config) called for account {}", accountID);

    if (!isEnabled()) {
        log::info("[ProfileMusic] Profile music is disabled in settings");
        return;
    }

    if (m_isPlaying && m_currentProfileID == accountID && !m_isPaused && !m_isFadingOut) {
        log::info("[ProfileMusic] Already playing music for this account");
        return;
    }

    forceStop();
    m_currentProfileID = accountID;
    playProfileMusicWithConfig(accountID, config);
}

void ProfileMusicManager::playProfileMusicWithConfig(int accountID, ProfileMusicConfig const& config) {
    auto cachePath = getCachePath(accountID);
    std::error_code cacheEc;
    bool cacheExists = std::filesystem::exists(cachePath, cacheEc) && !cacheEc;

    // Verificar si el cache en disco corresponde a la config actual del servidor
    if (cacheExists && isCacheValid(accountID, config)) {
        log::info("[ProfileMusic] Cache is valid and up-to-date, playing from cache: {}",
            geode::utils::string::pathToString(cachePath));
        playAudioFile(geode::utils::string::pathToString(cachePath), true, 0, 0);
        return;
    }

    // Cache inexistente o desactualizado — borrar y re-descargar
    if (cacheExists) {
        log::info("[ProfileMusic] Cache is stale (config changed), removing old file");
        std::filesystem::remove(cachePath, cacheEc);
    }

    log::info("[ProfileMusic] Downloading fresh music fragment...");
    downloadMusicFragment(accountID, [this, accountID, config](bool dlSuccess, std::string const& path) {
        if (dlSuccess && m_currentProfileID == accountID) {
            // Guardar .meta con la config actual para futuras validaciones
            saveMetaFile(accountID, config);
            log::info("[ProfileMusic] Download successful, playing: {}", path);
            playAudioFile(path, true, 0, 0);
        } else {
            log::error("[ProfileMusic] Download failed for account {}", accountID);
        }
    });
}

void ProfileMusicManager::playAudioFile(std::string const& path, bool loop, int startMs, int endMs) {
    // Limpiar audio anterior de ProfileMusic SIN tocar DynamicSongManager
    m_isFadingIn = false;
    m_isFadingOut = false;
    if (m_musicChannel) { m_musicChannel->stop(); m_musicChannel = nullptr; }
    if (m_currentSound) { m_currentSound->release(); m_currentSound = nullptr; }
    m_isPlaying = false;
    m_isPaused = false;

    auto engine = FMODAudioEngine::sharedEngine();
    if (!engine || !engine->m_system) return;

    FMOD::System* system = engine->m_system;

    // Usar FMOD_CREATESTREAM para carga asincrona (no bloquea el hilo principal)
    // y FMOD_NONBLOCKING para que no haya tiron
    FMOD_MODE mode = FMOD_CREATESTREAM | FMOD_NONBLOCKING;
    if (loop) {
        mode |= FMOD_LOOP_NORMAL;
    }

    FMOD_RESULT result = system->createSound(path.c_str(), mode, nullptr, &m_currentSound);
    if (result != FMOD_OK || !m_currentSound) {
        log::error("[ProfileMusic] Failed to create sound from: {}", path);
        return;
    }

    // Guardar los parametros para usarlos cuando el sonido este listo
    m_currentAudioPath = path;
    m_pendingStartMs = startMs;
    m_pendingEndMs = endMs;

    // Programar la verificacion del estado del sonido
    Loader::get()->queueInMainThread([this]() {
        checkSoundReady();
    });
}

void ProfileMusicManager::checkSoundReady() {
    if (!m_currentSound) return;

    FMOD_OPENSTATE openState;
    unsigned int percentBuffered;
    bool starving, diskBusy;

    FMOD_RESULT result = m_currentSound->getOpenState(&openState, &percentBuffered, &starving, &diskBusy);

    if (result != FMOD_OK) {
        log::error("[ProfileMusic] Failed to get sound state");
        stopCurrentAudio();
        return;
    }

    if (openState == FMOD_OPENSTATE_READY) {
        // El sonido esta listo, ahora podemos reproducirlo
        finishPlayback();
    } else if (openState == FMOD_OPENSTATE_ERROR) {
        log::error("[ProfileMusic] Sound failed to load");
        stopCurrentAudio();
    } else {
        // Todavia cargando, verificar de nuevo en el proximo frame
        Loader::get()->queueInMainThread([this]() {
            checkSoundReady();
        });
    }
}

void ProfileMusicManager::finishPlayback() {
    if (!m_currentSound) return;

    auto engine = FMODAudioEngine::sharedEngine();
    if (!engine || !engine->m_system) {
        stopCurrentAudio();
        return;
    }

    float gameVolume = engine->m_musicVolume;
    m_bgVolumeBeforeFade = gameVolume;
    bool useCrossfade = isCrossfadeEnabled();

    // Verificar si DynamicSong esta activa (realmente sonando)
    auto* dsm = DynamicSongManager::get();
    bool dynamicIsActive = dsm->m_isDynamicSongActive && !dsm->isDynamicChannelPaused();

    // Si no hay dynamic y no hay crossfade, pausar BG inmediatamente
    if (!dynamicIsActive && !useCrossfade) {
        if (engine->m_backgroundMusicChannel) {
            engine->m_backgroundMusicChannel->setPaused(true);
        }
    }

    // Obtener duracion del archivo
    unsigned int lengthMs = 0;
    m_currentSound->getLength(&lengthMs, FMOD_TIMEUNIT_MS);

    int startMs = m_pendingStartMs;
    int endMs = m_pendingEndMs;

    if (startMs == 0 && endMs == 0) {
        endMs = lengthMs;
    } else {
        if (endMs > static_cast<int>(lengthMs) || endMs <= 0) endMs = lengthMs;
        if (startMs >= endMs) startMs = 0;
    }

    // Configurar loop points
    m_currentSound->setLoopPoints(
        static_cast<unsigned int>(startMs), FMOD_TIMEUNIT_MS,
        static_cast<unsigned int>(endMs), FMOD_TIMEUNIT_MS
    );

    FMOD_RESULT result = engine->m_system->playSound(m_currentSound, nullptr, true, &m_musicChannel);
    if (result != FMOD_OK || !m_musicChannel) {
        log::error("[ProfileMusic] Failed to play sound");
        m_currentSound->release();
        m_currentSound = nullptr;
        return;
    }

    m_musicChannel->setPosition(static_cast<unsigned int>(startMs), FMOD_TIMEUNIT_MS);

    if (dynamicIsActive) {
        // ═══ Crossfade DynamicSong ↓ ProfileMusic ↑ (no tocar BG) ═══
        float dynVol = dsm->getDynamicVolume();

        m_musicChannel->setVolume(0.0f);
        m_musicChannel->setPaused(false);

        m_isPlaying = true;
        m_isPaused = false;
        m_isFadingIn = true;

        log::info("[ProfileMusic] Crossfade Dynamic->Profile (dynVol:{:.2f}, target:{:.2f})",
            dynVol, gameVolume);

        // Crossfade dedicado: baja dynamic, sube profile
        executeCrossfadeWithDynamic(0, FADE_STEPS, 0.0f, gameVolume, dynVol);

    } else if (useCrossfade) {
        // ═══ Crossfade normal con BG ═══
        m_musicChannel->setVolume(0.0f);
        m_musicChannel->setPaused(false);

        m_isPlaying = true;
        m_isPaused = false;
        m_isFadingIn = true;

        log::info("[ProfileMusic] Crossfade BG->Profile for account {} (target:{:.2f})",
            m_currentProfileID, gameVolume);

        fadeInProfileMusic(gameVolume);

    } else {
        // ═══ Sin crossfade ═══
        m_musicChannel->setVolume(gameVolume);
        m_musicChannel->setPaused(false);

        m_isPlaying = true;
        m_isPaused = false;

        log::info("[ProfileMusic] Playing profile music for account {} (vol:{:.2f})",
            m_currentProfileID, gameVolume);
    }
}

void ProfileMusicManager::fadeInProfileMusic(float targetVolume) {
    // Crossfade: musica de fondo baja de m_bgVolumeBeforeFade -> 0, perfil sube de 0 -> targetVolume
    executeFadeStep(0, FADE_STEPS, 0.0f, targetVolume, m_bgVolumeBeforeFade, 0.0f, false);
}

void ProfileMusicManager::executeCrossfadeWithDynamic(int step, int totalSteps,
    float profileFrom, float profileTo, float dynFrom) {

    // Single-thread crossfade: one thread handles all steps instead of spawning 20+.
    float stepMs = getFadeDurationMs() / static_cast<float>(totalSteps);

    std::thread([this, step, totalSteps, profileFrom, profileTo, dynFrom, stepMs]() {
        for (int s = step; s <= totalSteps; s++) {
            if (s > step) {
                std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(stepMs)));
            }

            int currentStep = s;
            Loader::get()->queueInMainThread([this, currentStep, totalSteps, profileFrom, profileTo, dynFrom]() {
                if (!m_isFadingIn) return;

                auto* dsm = DynamicSongManager::get();

                if (currentStep >= totalSteps) {
                    if (m_musicChannel) m_musicChannel->setVolume(profileTo);
                    dsm->setDynamicVolume(0.0f);
                    dsm->pauseDynamicChannel();
                    m_isFadingIn = false;
                    log::info("[ProfileMusic] Crossfade complete, dynamic paused, profile at {:.2f}", profileTo);
                    return;
                }

                float t = static_cast<float>(currentStep) / static_cast<float>(totalSteps);
                float eT = (t < 0.5f) ? (2.0f * t * t) : (1.0f - std::pow(-2.0f * t + 2.0f, 2.0f) / 2.0f);

                float profileVol = profileFrom + (profileTo - profileFrom) * eT;
                float dynVol = dynFrom * (1.0f - eT);

                if (m_musicChannel) m_musicChannel->setVolume(std::max(0.0f, std::min(1.0f, profileVol)));
                dsm->setDynamicVolume(dynVol);
            });
        }
    }).detach();
}

void ProfileMusicManager::fadeOutAndStop() {
    if (!m_musicChannel) {
        // No hay canal
        auto* dsm = DynamicSongManager::get();
        if (dsm->isDynamicChannelPaused()) {
            dsm->resumeDynamicChannel();
        } else {
            auto engine = FMODAudioEngine::sharedEngine();
            if (engine && engine->m_backgroundMusicChannel) {
                engine->m_backgroundMusicChannel->setVolume(engine->m_musicVolume);
                engine->m_backgroundMusicChannel->setPaused(false);
            }
        }
        return;
    }

    // Si DynamicSong estaba pausada, no hacer crossfade con BG
    // Simplemente parar profile music y reanudar dynamic song
    auto* dsm = DynamicSongManager::get();
    if (dsm->isDynamicChannelPaused()) {
        if (m_musicChannel) { m_musicChannel->stop(); m_musicChannel = nullptr; }
        if (m_currentSound) { m_currentSound->release(); m_currentSound = nullptr; }
        m_isPlaying = false;
        m_isPaused = false;
        m_isFadingOut = false;
        m_isFadingIn = false;
        m_currentProfileID = 0;
        m_currentAudioPath.clear();
        dsm->resumeDynamicChannel();
        log::info("[ProfileMusic] Stopped and resumed DynamicSong (no BG crossfade needed)");
        return;
    }

    m_isFadingOut = true;
    m_isFadingIn = false;

    // Releer el volumen objetivo del juego (por si cambio durante la reproduccion)
    auto engine = FMODAudioEngine::sharedEngine();
    float targetBgVolume = m_bgVolumeBeforeFade;
    if (engine) {
        targetBgVolume = engine->m_musicVolume;
    }
    m_bgVolumeBeforeFade = targetBgVolume;

    // Obtener volumen actual de la musica de perfil
    float currentProfileVol = 0.0f;
    m_musicChannel->getVolume(&currentProfileVol);

    log::info("[ProfileMusic] Starting fade-out (profile vol: {:.2f}, bg target: {:.2f})",
        currentProfileVol, targetBgVolume);

    // Despausar la musica de fondo con volumen 0 para empezar el fade-in
    if (engine && engine->m_backgroundMusicChannel) {
        engine->m_backgroundMusicChannel->setVolume(0.0f);
        engine->m_backgroundMusicChannel->setPaused(false);
    }

    // Crossfade: perfil baja de currentProfileVol -> 0, fondo sube de 0 -> targetBgVolume
    executeFadeStep(0, FADE_STEPS, currentProfileVol, 0.0f, 0.0f, targetBgVolume, true);
}

void ProfileMusicManager::executeFadeStep(int step, int totalSteps, float profileFrom, float profileTo,
                                           float bgFrom, float bgTo, bool stopAfter) {
    // Single-thread fade: launch ONE thread that sleeps between steps and queues
    // volume updates to the main thread. Replaces the old pattern that spawned
    // a new detached thread per step (20+ threads per fade).
    float fadeDurationMs = getFadeDurationMs();
    float stepDelayMs = fadeDurationMs / static_cast<float>(totalSteps);

    std::thread([this, step, totalSteps, profileFrom, profileTo, bgFrom, bgTo, stopAfter, stepDelayMs]() {
        for (int s = step; s <= totalSteps; s++) {
            // sleep between steps (skip first)
            if (s > step) {
                std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(stepDelayMs)));
            }

            int currentStep = s;
            Loader::get()->queueInMainThread([this, currentStep, totalSteps, profileFrom, profileTo,
                                               bgFrom, bgTo, stopAfter]() {
                // check cancellation
                if (stopAfter && !m_isFadingOut) return;
                if (!stopAfter && !m_isFadingIn) return;

                if (currentStep > totalSteps) return; // safety

                if (currentStep == totalSteps) {
                    // Fade terminado
                    if (stopAfter) {
                        if (m_musicChannel) {
                            m_musicChannel->stop();
                            m_musicChannel = nullptr;
                        }
                        if (m_currentSound) {
                            m_currentSound->release();
                            m_currentSound = nullptr;
                        }
                        m_isPlaying = false;
                        m_isPaused = false;
                        m_isFadingOut = false;
                        m_currentProfileID = 0;
                        m_currentAudioPath.clear();

                        auto* dsm = DynamicSongManager::get();
                        if (dsm->isDynamicChannelPaused()) {
                            dsm->resumeDynamicChannel();
                        } else {
                            auto engine = FMODAudioEngine::sharedEngine();
                            if (engine && engine->m_backgroundMusicChannel) {
                                engine->m_backgroundMusicChannel->setVolume(bgTo);
                            }
                        }
                        log::info("[ProfileMusic] Fade-out complete, music restored");
                    } else {
                        if (bgFrom != 0.0f || bgTo != 0.0f) {
                            auto engine = FMODAudioEngine::sharedEngine();
                            if (engine && engine->m_backgroundMusicChannel) {
                                engine->m_backgroundMusicChannel->setPaused(true);
                                engine->m_backgroundMusicChannel->setVolume(m_bgVolumeBeforeFade);
                            }
                        }
                        if (m_musicChannel) {
                            m_musicChannel->setVolume(profileTo);
                        }
                        m_isFadingIn = false;
                        log::info("[ProfileMusic] Fade-in complete, profile music playing at {:.2f}", profileTo);
                    }
                    return;
                }

                // Interpolate
                float t = static_cast<float>(currentStep) / static_cast<float>(totalSteps);
                float easedT = (t < 0.5f) ? (2.0f * t * t) : (1.0f - std::pow(-2.0f * t + 2.0f, 2.0f) / 2.0f);

                float profileVol = profileFrom + (profileTo - profileFrom) * easedT;
                float bgVol = bgFrom + (bgTo - bgFrom) * easedT;

                if (m_musicChannel) {
                    m_musicChannel->setVolume(std::max(0.0f, std::min(1.0f, profileVol)));
                }
                auto engine = FMODAudioEngine::sharedEngine();
                if (engine && engine->m_backgroundMusicChannel) {
                    engine->m_backgroundMusicChannel->setVolume(std::max(0.0f, std::min(1.0f, bgVol)));
                }
            });
        }
    }).detach();
}

void ProfileMusicManager::stopCurrentAudio() {
    // Cancelar cualquier fade en curso
    m_isFadingIn = false;
    m_isFadingOut = false;
    m_caveTransitioning = false;

    // Limpiar efecto cueva si estaba activo
    if (m_caveEffectActive && m_musicChannel && m_lowpassDSP) {
        m_musicChannel->removeDSP(m_lowpassDSP);
    }
    m_caveEffectActive = false;
    m_originalFrequency = 0.0f;
    m_originalVolume = 0.0f;

    if (m_musicChannel) {
        m_musicChannel->stop();
        m_musicChannel = nullptr;
    }
    if (m_currentSound) {
        m_currentSound->release();
        m_currentSound = nullptr;
    }

    m_isPlaying = false;
    m_isPaused = false;

    auto* dsm = DynamicSongManager::get();
    if (dsm->isDynamicChannelPaused()) {
        // DynamicSong estaba pausada — reanudarla en vez de restaurar BG
        // resumeDynamicChannel() tambien silencia el BG de nuevo
        dsm->resumeDynamicChannel();
    } else {
        // No hay dynamic song — restaurar la musica de fondo normal
        auto engine = FMODAudioEngine::sharedEngine();
        if (engine && engine->m_backgroundMusicChannel) {
            engine->m_backgroundMusicChannel->setVolume(engine->m_musicVolume);
            engine->m_backgroundMusicChannel->setPaused(false);
        }
    }
}

void ProfileMusicManager::pauseProfileMusic() {
    if (m_musicChannel && m_isPlaying) {
        m_musicChannel->setPaused(true);
        m_isPaused = true;
        log::info("[ProfileMusic] Paused");
    }
}

void ProfileMusicManager::resumeProfileMusic() {
    if (m_musicChannel && m_isPaused) {
        m_musicChannel->setPaused(false);
        m_isPaused = false;
        log::info("[ProfileMusic] Resumed");
    }
}

void ProfileMusicManager::stopProfileMusic() {
    // Si ya hay un fade-out en curso, no hacer nada (evitar reiniciar o cortar)
    if (m_isFadingOut) {
        log::info("[ProfileMusic] Fade-out already in progress, ignoring stop request");
        return;
    }

    // Si no hay nada reproduciendose, no tocar nada
    // (evita que stopCurrentAudio restaure el BG channel innecesariamente)
    if (!m_isPlaying && !m_musicChannel) {
        return;
    }

    if (m_isPlaying && m_musicChannel && isCrossfadeEnabled()) {
        // Usar fade-out suave
        log::info("[ProfileMusic] Starting fade-out transition");
        fadeOutAndStop();
    } else {
        // Sin crossfade o sin reproduccion activa: parar inmediatamente
        stopCurrentAudio();
        m_currentProfileID = 0;
        m_currentAudioPath.clear();
        log::info("[ProfileMusic] Stopped immediately");
    }
}

void ProfileMusicManager::getSongInfo(int songID, SongInfoCallback callback) {
    auto mdm = MusicDownloadManager::sharedState();
    if (!mdm) {
        callback(false, "", "", 0);
        return;
    }

    auto songInfo = mdm->getSongInfoObject(songID);
    if (songInfo) {
        std::string name = songInfo->m_songName;
        std::string artist = songInfo->m_artistName;
        // Duration will be determined when loading the audio file
        // For now we return 0 and let the waveform analysis determine it
        callback(true, name, artist, 0);
        return;
    }

    // Request song info from GD servers
    mdm->getSongInfo(songID, true);

    // Use a delayed callback via Loader
    Loader::get()->queueInMainThread([songID, callback]() {
        // Wait a bit and then check again
        std::thread([songID, callback]() {
            std::this_thread::sleep_for(std::chrono::seconds(2));

            Loader::get()->queueInMainThread([songID, callback]() {
                auto mdm = MusicDownloadManager::sharedState();
                auto songInfo = mdm->getSongInfoObject(songID);
                if (songInfo) {
                    std::string name = songInfo->m_songName;
                    std::string artist = songInfo->m_artistName;
                    callback(true, name, artist, 0);
                } else {
                    callback(false, "", "", 0);
                }
            });
        }).detach();
    });
}

void ProfileMusicManager::downloadSongForPreview(int songID, DownloadCallback callback) {
    auto mdm = MusicDownloadManager::sharedState();
    if (!mdm) {
        callback(false, "");
        return;
    }

    if (mdm->isSongDownloaded(songID)) {
        std::string path = mdm->pathForSong(songID);
        callback(true, path);
        return;
    }

    mdm->downloadSong(songID);

    // Use a thread to poll for download completion
    std::thread([songID, callback]() {
        auto mdm = MusicDownloadManager::sharedState();
        int attempts = 0;
        const int maxAttempts = 30; // 30 seconds max

        while (attempts < maxAttempts) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            attempts++;

            if (mdm->isSongDownloaded(songID)) {
                std::string path = mdm->pathForSong(songID);
                Loader::get()->queueInMainThread([callback, path]() {
                    callback(true, path);
                });
                return;
            }
        }

        Loader::get()->queueInMainThread([callback]() {
            callback(false, "");
        });
    }).detach();
}

void ProfileMusicManager::getWaveformPeaks(int songID, WaveformCallback callback) {
    downloadSongForPreview(songID, [this, callback](bool success, std::string const& path) {
        if (!success || path.empty()) {
            callback(false, {}, 0);
            return;
        }

        std::thread([this, path, callback]() {
            int durationMs = 0;
            auto peaks = analyzeWaveform(path, 200, durationMs);
            Loader::get()->queueInMainThread([callback, peaks, durationMs]() {
                callback(!peaks.empty(), peaks, durationMs);
            });
        }).detach();
    });
}

std::vector<float> ProfileMusicManager::analyzeWaveform(std::string const& filePath, int numPeaks, int& outDurationMs) {
    std::vector<float> peaks(numPeaks, 0.0f);
    outDurationMs = 0;

    auto engine = FMODAudioEngine::sharedEngine();
    if (!engine || !engine->m_system) return peaks;

    FMOD::System* system = engine->m_system;
    FMOD::Sound* sound = nullptr;

    FMOD_RESULT result = system->createSound(filePath.c_str(), FMOD_DEFAULT | FMOD_OPENONLY, nullptr, &sound);
    if (result != FMOD_OK || !sound) {
        return peaks;
    }

    // Get duration in milliseconds
    unsigned int lengthMs = 0;
    sound->getLength(&lengthMs, FMOD_TIMEUNIT_MS);
    outDurationMs = static_cast<int>(lengthMs);

    unsigned int length = 0;
    sound->getLength(&length, FMOD_TIMEUNIT_PCMBYTES);

    FMOD_SOUND_FORMAT format;
    int channels, bits;
    sound->getFormat(nullptr, &format, &channels, &bits);

    if (length == 0 || bits == 0) {
        sound->release();
        return peaks;
    }

    unsigned int bytesPerSample = bits / 8;
    unsigned int totalSamples = length / (bytesPerSample * channels);
    unsigned int samplesPerPeak = totalSamples / numPeaks;

    if (samplesPerPeak == 0) samplesPerPeak = 1;

    std::vector<char> buffer(length);
    unsigned int read = 0;
    result = sound->readData(buffer.data(), length, &read);

    if (result != FMOD_OK && result != FMOD_ERR_FILE_EOF) {
        sound->release();
        return peaks;
    }

    for (int i = 0; i < numPeaks && static_cast<unsigned int>(i) * samplesPerPeak < totalSamples; i++) {
        float maxSample = 0.0f;

        unsigned int startSample = i * samplesPerPeak;
        unsigned int endSample = std::min(startSample + samplesPerPeak, totalSamples);

        for (unsigned int s = startSample; s < endSample; s++) {
            for (int c = 0; c < channels; c++) {
                unsigned int byteIndex = (s * channels + c) * bytesPerSample;
                if (byteIndex + bytesPerSample > read) continue;

                float sample = 0.0f;
                if (bits == 16) {
                    int16_t* ptr = reinterpret_cast<int16_t*>(&buffer[byteIndex]);
                    sample = std::abs(static_cast<float>(*ptr) / 32768.0f);
                } else if (bits == 8) {
                    sample = std::abs((static_cast<float>(buffer[byteIndex]) - 128.0f) / 128.0f);
                } else if (bits == 32) {
                    float* ptr = reinterpret_cast<float*>(&buffer[byteIndex]);
                    sample = std::abs(*ptr);
                }

                if (sample > maxSample) maxSample = sample;
            }
        }

        peaks[i] = maxSample;
    }

    sound->release();

    float maxPeak = 0.0f;
    for (float p : peaks) {
        if (p > maxPeak) maxPeak = p;
    }

    if (maxPeak > 0.0f) {
        for (float& p : peaks) {
            p /= maxPeak;
        }
    }

    return peaks;
}

void ProfileMusicManager::playPreview(std::string const& filePath, int startMs, int endMs) {
    stopPreview();

    auto engine = FMODAudioEngine::sharedEngine();
    if (!engine || !engine->m_system) return;

    // Usar el volumen de musica configurado en los ajustes del juego
    float gameVolume = engine->m_musicVolume;
    if (engine->m_backgroundMusicChannel) {
        // Pausar la musica de fondo del juego para el preview
        engine->m_backgroundMusicChannel->setPaused(true);
    }

    FMOD::System* system = engine->m_system;

    FMOD_RESULT result = system->createSound(filePath.c_str(), FMOD_DEFAULT | FMOD_LOOP_NORMAL, nullptr, &m_currentSound);
    if (result != FMOD_OK || !m_currentSound) {
        // Restaurar musica si falla
        if (engine->m_backgroundMusicChannel) {
            engine->m_backgroundMusicChannel->setPaused(false);
        }
        return;
    }

    unsigned int lengthMs;
    m_currentSound->getLength(&lengthMs, FMOD_TIMEUNIT_MS);

    if (endMs > static_cast<int>(lengthMs)) endMs = lengthMs;
    if (startMs >= endMs) startMs = 0;

    m_currentSound->setLoopPoints(startMs, FMOD_TIMEUNIT_MS, endMs, FMOD_TIMEUNIT_MS);

    result = system->playSound(m_currentSound, nullptr, false, &m_musicChannel);
    if (result != FMOD_OK || !m_musicChannel) {
        m_currentSound->release();
        m_currentSound = nullptr;
        // Restaurar musica si falla
        if (engine->m_backgroundMusicChannel) {
            engine->m_backgroundMusicChannel->setPaused(false);
        }
        return;
    }

    m_musicChannel->setPosition(startMs, FMOD_TIMEUNIT_MS);

    // Usar el volumen del juego
    m_musicChannel->setVolume(gameVolume);

    m_isPlaying = true;
    m_isPaused = false;
}

void ProfileMusicManager::stopPreview() {
    stopCurrentAudio();
}

void ProfileMusicManager::clearCache() {
    std::error_code ec;
    auto cacheDir = getCacheDir();

    if (std::filesystem::exists(cacheDir, ec)) {
        for (auto& entry : std::filesystem::directory_iterator(cacheDir, ec)) {
            std::filesystem::remove(entry.path(), ec);
        }
    }

    m_configCache.clear();
    log::info("[ProfileMusic] Cache cleared");
}

void ProfileMusicManager::invalidateCache(int accountID) {
    // Limpiar config de RAM
    m_configCache.erase(accountID);

    std::error_code ec;

    // Borrar archivo .mp3 cacheado
    auto cachePath = getCachePath(accountID);
    if (std::filesystem::exists(cachePath, ec)) {
        std::filesystem::remove(cachePath, ec);
    }

    // Borrar archivo .meta asociado
    auto metaPath = getMetaPath(accountID);
    if (std::filesystem::exists(metaPath, ec)) {
        std::filesystem::remove(metaPath, ec);
    }

    log::info("[ProfileMusic] Cache invalidated for account {}", accountID);
}

std::filesystem::path ProfileMusicManager::getMetaPath(int accountID) {
    return getCacheDir() / fmt::format("{}.meta", accountID);
}

void ProfileMusicManager::saveMetaFile(int accountID, ProfileMusicConfig const& config) {
    auto metaPath = getMetaPath(accountID);
    std::error_code ec;
    std::filesystem::create_directories(metaPath.parent_path(), ec);

    // Guardar songID|startMs|endMs|updatedAt para comparacion.
    // updatedAt permite detectar re-uploads de la misma cancion con el mismo rango.
    std::ofstream file(metaPath);
    if (file) {
        file << config.songID << "|" << config.startMs << "|" << config.endMs << "|" << config.updatedAt;
        file.close();
        log::info("[ProfileMusic] Saved meta for account {}: songID={}, {}ms-{}ms, updatedAt={}",
            accountID, config.songID, config.startMs, config.endMs, config.updatedAt);
    }
}

bool ProfileMusicManager::isCacheValid(int accountID, ProfileMusicConfig const& config) {
    auto metaPath = getMetaPath(accountID);
    std::error_code ec;
    if (!std::filesystem::exists(metaPath, ec)) {
        // No hay .meta — cache es legacy o invalido
        log::info("[ProfileMusic] No meta file for account {}, cache is invalid", accountID);
        return false;
    }

    std::ifstream file(metaPath);
    if (!file) return false;

    std::string content;
    std::getline(file, content);
    file.close();

    // Formato: songID|startMs|endMs|updatedAt
    std::string expected = fmt::format("{}|{}|{}|{}", config.songID, config.startMs, config.endMs, config.updatedAt);

    // Si updatedAt esta vacio (servidor no lo provee), el cache nunca sera valido
    // para forzar siempre la re-descarga — esto es intencional como fallback seguro.
    if (config.updatedAt.empty()) {
        log::info("[ProfileMusic] Server does not provide updatedAt, cache considered invalid (safe fallback)");
        return false;
    }

    bool valid = (content == expected);
    if (!valid) {
        log::info("[ProfileMusic] Cache meta mismatch for account {}: cached='{}', server='{}'",
            accountID, content, expected);
    }
    return valid;
}

void ProfileMusicManager::applyCaveEffect() {
    if (m_caveEffectActive && !m_caveTransitioning) return;
    if (!m_musicChannel) return;

    auto engine = FMODAudioEngine::sharedEngine();
    if (!engine || !engine->m_system) return;

    // Crear lowpass DSP si no existe
    if (!m_lowpassDSP) {
        FMOD_RESULT res = engine->m_system->createDSPByType(FMOD_DSP_TYPE_LOWPASS, &m_lowpassDSP);
        if (res != FMOD_OK || !m_lowpassDSP) {
            log::error("[ProfileMusic] Failed to create lowpass DSP");
            return;
        }
    }

    // Empezar con cutoff alto (sin efecto) y transicionar a bajo
    m_lowpassDSP->setParameterFloat(FMOD_DSP_LOWPASS_CUTOFF, 22000.0f);
    m_lowpassDSP->setParameterFloat(FMOD_DSP_LOWPASS_RESONANCE, 1.0f);
    m_musicChannel->addDSP(0, m_lowpassDSP);

    // Guardar estado original
    m_musicChannel->getFrequency(&m_originalFrequency);
    m_musicChannel->getVolume(&m_originalVolume);

    m_caveEffectActive = true;
    m_caveTransitioning = true;

    log::info("[ProfileMusic] Cave effect: starting smooth transition IN (freq:{:.0f}, vol:{:.2f})",
        m_originalFrequency, m_originalVolume);

    // Transicion suave: cutoff 22000→800, freq 1.0→0.92, vol 1.0→0.6
    static constexpr int CAVE_STEPS = 15;
    executeCaveTransitionStep(0, CAVE_STEPS,
        22000.0f, 800.0f,
        m_originalFrequency, m_originalFrequency * 0.92f,
        m_originalVolume, m_originalVolume * 0.6f,
        true);
}

void ProfileMusicManager::removeCaveEffect() {
    if (!m_caveEffectActive) return;
    if (!m_musicChannel) {
        // Canal ya no existe, solo limpiar estado
        if (m_lowpassDSP) {
            m_lowpassDSP->release();
            m_lowpassDSP = nullptr;
        }
        m_caveEffectActive = false;
        m_caveTransitioning = false;
        return;
    }

    m_caveTransitioning = true;

    // Leer valores actuales del canal
    float currentFreq = 0.0f;
    float currentVol = 0.0f;
    m_musicChannel->getFrequency(&currentFreq);
    m_musicChannel->getVolume(&currentVol);

    // Volumen objetivo = volumen del juego
    auto engine = FMODAudioEngine::sharedEngine();
    float targetVol = engine ? engine->m_musicVolume : m_originalVolume;
    float targetFreq = (m_originalFrequency > 0.0f) ? m_originalFrequency : currentFreq / 0.92f;

    log::info("[ProfileMusic] Cave effect: starting smooth transition OUT (freq:{:.0f}->{:.0f}, vol:{:.2f}->{:.2f})",
        currentFreq, targetFreq, currentVol, targetVol);

    static constexpr int CAVE_STEPS = 15;
    executeCaveTransitionStep(0, CAVE_STEPS,
        800.0f, 22000.0f,
        currentFreq, targetFreq,
        currentVol, targetVol,
        false);
}

void ProfileMusicManager::executeCaveTransitionStep(int step, int totalSteps,
    float cutoffFrom, float cutoffTo,
    float freqFrom, float freqTo,
    float volFrom, float volTo, bool applying) {

    if (!m_caveTransitioning || !m_musicChannel) {
        // Cancelado o canal desaparecio
        if (!applying && m_lowpassDSP && m_musicChannel) {
            m_musicChannel->removeDSP(m_lowpassDSP);
        }
        m_caveTransitioning = false;
        if (!applying) m_caveEffectActive = false;
        return;
    }

    if (step > totalSteps) {
        m_caveTransitioning = false;
        if (!applying) {
            // Transicion OUT completada: quitar DSP
            if (m_lowpassDSP && m_musicChannel) {
                m_musicChannel->removeDSP(m_lowpassDSP);
            }
            if (m_musicChannel) {
                m_musicChannel->setFrequency(freqTo);
                m_musicChannel->setVolume(volTo);
            }
            m_caveEffectActive = false;
            m_originalFrequency = 0.0f;
            m_originalVolume = 0.0f;
            log::info("[ProfileMusic] Cave effect fully removed");
        } else {
            // Transicion IN completada
            if (m_lowpassDSP) {
                m_lowpassDSP->setParameterFloat(FMOD_DSP_LOWPASS_CUTOFF, cutoffTo);
                m_lowpassDSP->setParameterFloat(FMOD_DSP_LOWPASS_RESONANCE, 2.0f);
            }
            if (m_musicChannel) {
                m_musicChannel->setFrequency(freqTo);
                m_musicChannel->setVolume(volTo);
            }
            log::info("[ProfileMusic] Cave effect fully applied");
        }
        return;
    }

    float t = static_cast<float>(step) / static_cast<float>(totalSteps);
    // Ease in-out cuadratica
    float eT = (t < 0.5f) ? (2.0f * t * t) : (1.0f - std::pow(-2.0f * t + 2.0f, 2.0f) / 2.0f);

    float cutoff = cutoffFrom + (cutoffTo - cutoffFrom) * eT;
    float freq = freqFrom + (freqTo - freqFrom) * eT;
    float vol = volFrom + (volTo - volFrom) * eT;

    if (m_lowpassDSP) {
        m_lowpassDSP->setParameterFloat(FMOD_DSP_LOWPASS_CUTOFF, cutoff);
        float resonance = applying ? (1.0f + eT * 1.0f) : (2.0f - eT * 1.0f);
        m_lowpassDSP->setParameterFloat(FMOD_DSP_LOWPASS_RESONANCE, resonance);
    }
    if (m_musicChannel) {
        m_musicChannel->setFrequency(freq);
        m_musicChannel->setVolume(std::max(0.0f, std::min(1.0f, vol)));
    }

    int next = step + 1;
    float stepMs = 400.0f / static_cast<float>(totalSteps); // 400ms total transition

    Loader::get()->queueInMainThread([this, next, totalSteps, cutoffFrom, cutoffTo,
                                       freqFrom, freqTo, volFrom, volTo, applying, stepMs]() {
        std::thread([this, next, totalSteps, cutoffFrom, cutoffTo,
                     freqFrom, freqTo, volFrom, volTo, applying, stepMs]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(stepMs)));
            Loader::get()->queueInMainThread([this, next, totalSteps, cutoffFrom, cutoffTo,
                                               freqFrom, freqTo, volFrom, volTo, applying]() {
                executeCaveTransitionStep(next, totalSteps, cutoffFrom, cutoffTo,
                    freqFrom, freqTo, volFrom, volTo, applying);
            });
        }).detach();
    });
}

void ProfileMusicManager::forceStop() {
    log::info("[ProfileMusic] forceStop called (fadingOut:{}, fadingIn:{}, playing:{})",
        m_isFadingOut, m_isFadingIn, m_isPlaying);

    // Cancelar todo: fades, transiciones de cueva, etc.
    m_isFadingIn = false;
    m_isFadingOut = false;
    m_caveTransitioning = false;

    // Limpiar efecto cueva
    if (m_caveEffectActive && m_musicChannel && m_lowpassDSP) {
        m_musicChannel->removeDSP(m_lowpassDSP);
    }
    m_caveEffectActive = false;
    m_originalFrequency = 0.0f;
    m_originalVolume = 0.0f;

    // Parar canal y sonido
    if (m_musicChannel) {
        m_musicChannel->stop();
        m_musicChannel = nullptr;
    }
    if (m_currentSound) {
        m_currentSound->release();
        m_currentSound = nullptr;
    }

    m_isPlaying = false;
    m_isPaused = false;
    m_currentProfileID = 0;
    m_currentAudioPath.clear();

    log::info("[ProfileMusic] forceStop complete, all state cleared");
}

float ProfileMusicManager::getCurrentAmplitude() const {
    if (!m_isPlaying || !m_musicChannel) return 0.f;

    // usar FMOD DSP metering pa leer la amplitud real
    FMOD::DSP* headDSP = nullptr;
    auto result = m_musicChannel->getDSP(FMOD_CHANNELCONTROL_DSP_HEAD, &headDSP);
    if (result != FMOD_OK || !headDSP) return 0.f;

    headDSP->setMeteringEnabled(false, true);

    FMOD_DSP_METERING_INFO meteringInfo = {};
    result = headDSP->getMeteringInfo(nullptr, &meteringInfo);
    if (result != FMOD_OK) return 0.f;

    float peak = 0.f;
    for (int i = 0; i < meteringInfo.numchannels; i++) {
        if (meteringInfo.peaklevel[i] > peak) peak = meteringInfo.peaklevel[i];
    }
    return peak;
}






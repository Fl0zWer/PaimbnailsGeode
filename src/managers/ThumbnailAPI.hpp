#pragma once

#include <Geode/Geode.hpp>
#include "../utils/HttpClient.hpp"
#include "../utils/ThumbnailTypes.hpp"
#include "../managers/LocalThumbs.hpp"
#include "../managers/PendingQueue.hpp"
#include "../managers/ProfileThumbs.hpp"
#include <functional>
#include <string>

/**
 * thumbnailapi - manager singleton operaciones miniaturas
 * maneja subida/bajada con servidor, cache, y fallback a local
 */
class ThumbnailAPI {
public:
    using UploadCallback = std::function<void(bool success, const std::string& message)>;
    using DownloadCallback = std::function<void(bool success, cocos2d::CCTexture2D* texture)>;
    using DownloadDataCallback = std::function<void(bool success, const std::vector<uint8_t>& data)>;
    using ExistsCallback = std::function<void(bool exists)>;
    using ModeratorCallback = std::function<void(bool isModerator, bool isAdmin)>;
    using QueueCallback = std::function<void(bool success, const std::vector<PendingItem>& items)>;
    using ActionCallback = std::function<void(bool success, const std::string& message)>;

    using ThumbnailInfo = ::ThumbnailInfo;
    using ThumbnailListCallback = std::function<void(bool success, const std::vector<ThumbnailInfo>& thumbnails)>;

    static ThumbnailAPI& get() {
        static ThumbnailAPI instance;
        return instance;
    }

    // funciones principales de la API
    
    /**
     * obtener lista miniaturas nivel
     * @param levelId Level ID
     * @param callback Callback with list of thumbnails
     */
    void getThumbnails(int levelId, ThumbnailListCallback callback);

    /**
     * obtener informacion completa miniaturas nivel
     * @param levelId Level ID
     * @param callback Callback with raw json response
     */
    void getThumbnailInfo(int levelId, ActionCallback callback);

    /**
     * obtener url miniatura nivel
     * @param levelId Level ID
     * @return URL string
     */
    std::string getThumbnailURL(int levelId);

    /**
     * subir miniatura a servidor
     * @param levelId Level ID
     * @param pngData PNG image data
     * @param username Username of uploader
     * @param callback Callback with success status and message
     */
    void uploadThumbnail(int levelId, const std::vector<uint8_t>& pngData, const std::string& username, UploadCallback callback);

    // subir miniatura GIF (solo mod/admin)
    void uploadGIF(int levelId, const std::vector<uint8_t>& gifData, const std::string& username, UploadCallback callback);

    // subir sugerencia (no moderador) a /suggestions
    void uploadSuggestion(int levelId, const std::vector<uint8_t>& pngData, const std::string& username, UploadCallback callback);
    // subir propuesta de update (no moderador) a /updates
    void uploadUpdate(int levelId, const std::vector<uint8_t>& pngData, const std::string& username, UploadCallback callback);
    // subir imagen de perfil por accountID
    void uploadProfile(int accountID, const std::vector<uint8_t>& pngData, const std::string& username, UploadCallback callback);
    // subir GIF de perfil por accountID
    void uploadProfileGIF(int accountID, const std::vector<uint8_t>& gifData, const std::string& username, UploadCallback callback);
    // descargar imagen de perfil por accountID
    void downloadProfile(int accountID, const std::string& username, DownloadCallback callback);

    // descargar imagen desde una URL cualquiera
    void downloadFromUrl(const std::string& url, DownloadCallback callback);
    // descargar solo los datos binarios de una imagen desde una URL
    void downloadFromUrlData(const std::string& url, DownloadDataCallback callback);


    
    // subir config de perfil
    void uploadProfileConfig(int accountID, const ProfileConfig& config, ActionCallback callback);
    // bajar config de perfil
    void downloadProfileConfig(int accountID, std::function<void(bool success, const ProfileConfig& config)> callback);

    // descargar sugerencia desde /suggestions
    void downloadSuggestion(int levelId, DownloadCallback callback);
    // descargar imagen de sugerencia por nombre de archivo
    void downloadSuggestionImage(const std::string& filename, DownloadCallback callback);
    // descargar update desde /updates
    void downloadUpdate(int levelId, DownloadCallback callback);
    // descargar reportada (la mini actual del servidor)
    void downloadReported(int levelId, DownloadCallback callback);


    // sistema de votos
    void getRating(int levelId, const std::string& username, const std::string& thumbnailId, std::function<void(bool success, float average, int count, int userVote)> callback);
    void submitVote(int levelId, int stars, const std::string& username, const std::string& thumbnailId, ActionCallback callback);

    /**
     * descargar miniatura desde servidor (con cache)
     * @param levelId Level ID
     * @param callback Callback with success status and texture
     */
    void downloadThumbnail(int levelId, DownloadCallback callback);
    
    /**
     * verificar si existe miniatura en servidor
     * @param levelId Level ID
     * @param callback Callback with exists status
     */
    void checkExists(int levelId, ExistsCallback callback);
    
    /**
     * verificar si usuario es moderador
     * @param username Username to check
     * @param callback Callback with moderator status
     */
    void checkModerator(const std::string& username, ModeratorCallback callback);
    // chequeo de moderador “seguro” con accountID > 0 obligatorio
    void checkModeratorAccount(const std::string& username, int accountID, ModeratorCallback callback);
    
    /**
     * confirmar estado mod de cualquier usuario (publico)
     * no hace chequeo seguridad usuario actual.
     */
    void checkUserStatus(const std::string& username, ModeratorCallback callback);

    /**
     * obtener textura miniatura (prueba cache, local, luego servidor)
     * @param levelId Level ID
     * @param callback Callback with texture (or nullptr if not found)
     */
    void getThumbnail(int levelId, DownloadCallback callback);
    
    /**
     * sincronizar cola verificacion con servidor
     * @param category Category to sync (Verify, Update, Report)
     * @param callback Callback with items from server
     */
    void syncVerificationQueue(PendingCategory category, QueueCallback callback);
    
    /**
     * reclamar item cola verificacion (marcar en revision)
     * @param levelId Level ID
     * @param category Category
     * @param username Moderator username
     * @param callback Callback with success status
     */
    void claimQueueItem(int levelId, PendingCategory category, const std::string& username, ActionCallback callback);
    
    /**
     * aceptar item cola verificacion
     * @param levelId Level ID
     * @param category Category
     * @param username Moderator username
     * @param callback Callback with success status
     */
    void acceptQueueItem(int levelId, PendingCategory category, const std::string& username, ActionCallback callback, const std::string& targetFilename = "");
    
    /**
     * rechazar item cola verificacion
     * @param levelId Level ID
     * @param category Category
     * @param username Moderator username
     * @param reason Rejection reason
     * @param callback Callback with success status
     */
    void rejectQueueItem(int levelId, PendingCategory category, const std::string& username, const std::string& reason, ActionCallback callback);
    
    /**
     * enviar reporte al servidor
     * @param levelId Level ID
     * @param username Reporter username
     * @param note Report reason
     * @param callback Callback with success status
     */
    void submitReport(int levelId, const std::string& username, const std::string& note, ActionCallback callback);
    
    /**
     * aÃ±adir moderador (solo admin)
     * @param username Username to add
     * @param adminUser Admin username
     * @param callback Callback with success status
     */
    void addModerator(const std::string& username, const std::string& adminUser, ActionCallback callback);
    
    /**
     * quitar moderador (solo admin) - reutiliza endpoint add-moderator con action=remove
     * @param username Username to remove
     * @param adminUser Admin username
     * @param callback Callback with success status
     */
    void removeModerator(const std::string& username, const std::string& adminUser, ActionCallback callback);
    
    /**
     * borrar miniatura servidor (solo moderador)
     * @param levelId Level ID
     * @param username Moderator username
     * @param callback Callback with success status
     */
    void deleteThumbnail(int levelId, const std::string& username, int accountID, ActionCallback callback);
    
    // configuración
    void setServerEnabled(bool enabled);
    bool isServerEnabled() const { return m_serverEnabled; }
    
    // estadísticas
    int getUploadCount() const { return m_uploadCount; }
    int getDownloadCount() const { return m_downloadCount; }

    // helper pa convertir datos a CCTexture2D
    cocos2d::CCTexture2D* webpToTexture(const std::vector<uint8_t>& webpData);

private:
    ThumbnailAPI();
    ~ThumbnailAPI() = default;
    
    ThumbnailAPI(const ThumbnailAPI&) = delete;
    ThumbnailAPI& operator=(const ThumbnailAPI&) = delete;

    bool m_serverEnabled = true;
    int m_uploadCount = 0;
    int m_downloadCount = 0;
    
    // helper para cargar desde almacenamiento local
    cocos2d::CCTexture2D* loadFromLocal(int levelId);
};


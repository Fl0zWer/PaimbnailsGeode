#pragma once

#include <Geode/Geode.hpp>
#include <Geode/utils/function.hpp>
#include "../utils/HttpClient.hpp"
#include "../utils/ThumbnailTypes.hpp"
#include "../features/thumbnails/services/LocalThumbs.hpp"
#include "../features/moderation/services/PendingQueue.hpp"
#include "../features/profiles/services/ProfileThumbs.hpp"
#include "../features/thumbnails/services/ThumbnailTransportClient.hpp"
#include "../features/thumbnails/services/ThumbnailSubmissionService.hpp"
#include "../features/moderation/services/ModerationService.hpp"
#include "../features/profiles/services/ProfileImageService.hpp"
#include <string>
#include <optional>
#include <chrono>

class ThumbnailAPI {
public:
    using UploadCallback = geode::CopyableFunction<void(bool success, std::string const& message)>;
    using DownloadCallback = geode::CopyableFunction<void(bool success, cocos2d::CCTexture2D* texture)>;
    using DownloadDataCallback = geode::CopyableFunction<void(bool success, std::vector<uint8_t> const& data)>;
    using ExistsCallback = geode::CopyableFunction<void(bool exists)>;
    using ModeratorCallback = geode::CopyableFunction<void(bool isModerator, bool isAdmin)>;
    using QueueCallback = geode::CopyableFunction<void(bool success, std::vector<PendingItem> const& items)>;
    using ActionCallback = geode::CopyableFunction<void(bool success, std::string const& message)>;

    using ThumbnailInfo = ::ThumbnailInfo;
    using ThumbnailListCallback = geode::CopyableFunction<void(bool success, std::vector<ThumbnailInfo> const& thumbnails)>;

    static ThumbnailAPI& get() {
        static ThumbnailAPI instance;
        return instance;
    }

    // funciones principales de la API
    
    void getThumbnails(int levelId, ThumbnailListCallback callback);

    void getThumbnailInfo(int levelId, ActionCallback callback);

    std::string getThumbnailURL(int levelId);

    void uploadThumbnail(int levelId, std::vector<uint8_t> const& pngData, std::string const& username, UploadCallback callback);

    // subir miniatura GIF (solo mod/admin)
    void uploadGIF(int levelId, std::vector<uint8_t> const& gifData, std::string const& username, UploadCallback callback);

    // subir sugerencia (no moderador) a /suggestions
    void uploadSuggestion(int levelId, std::vector<uint8_t> const& pngData, std::string const& username, UploadCallback callback);
    // subir propuesta de update (no moderador) a /updates
    void uploadUpdate(int levelId, std::vector<uint8_t> const& pngData, std::string const& username, UploadCallback callback);
    // subir imagen de perfil por accountID
    void uploadProfile(int accountID, std::vector<uint8_t> const& pngData, std::string const& username, UploadCallback callback);
    // subir GIF de perfil por accountID
    void uploadProfileGIF(int accountID, std::vector<uint8_t> const& gifData, std::string const& username, UploadCallback callback);
    // descargar imagen de perfil por accountID
    void downloadProfile(int accountID, std::string const& username, DownloadCallback callback);

    // subir imagen de foto de perfil (profileimg) por accountID
    void uploadProfileImg(int accountID, std::vector<uint8_t> const& imgData, std::string const& username, std::string const& contentType, UploadCallback callback);
    // subir GIF de foto de perfil (profileimg) por accountID
    void uploadProfileImgGIF(int accountID, std::vector<uint8_t> const& gifData, std::string const& username, UploadCallback callback);
    // descargar foto de perfil (profileimg) por accountID
    void downloadProfileImg(int accountID, DownloadCallback callback, bool isSelf = false);

    // descargar imagen desde una URL cualquiera
    void downloadFromUrl(std::string const& url, DownloadCallback callback);
    // descargar solo los datos binarios de una imagen desde una URL
    void downloadFromUrlData(std::string const& url, DownloadDataCallback callback);

    
    // subir config de perfil
    void uploadProfileConfig(int accountID, ProfileConfig const& config, ActionCallback callback);
    // bajar config de perfil
    void downloadProfileConfig(int accountID, geode::CopyableFunction<void(bool success, ProfileConfig const& config)> callback);

    // descargar sugerencia desde /suggestions
    void downloadSuggestion(int levelId, DownloadCallback callback);
    // descargar imagen de sugerencia por nombre de archivo
    void downloadSuggestionImage(std::string const& filename, DownloadCallback callback);
    // descargar update desde /updates
    void downloadUpdate(int levelId, DownloadCallback callback);
    // descargar reportada (la mini actual del servidor)
    void downloadReported(int levelId, DownloadCallback callback);
    // descargar background de perfil pendiente (para moderadores en centro de verificacion)
    void downloadPendingProfile(int accountID, DownloadCallback callback);

    // sistema de votos
    void getRating(int levelId, std::string const& username, std::string const& thumbnailId, geode::CopyableFunction<void(bool success, float average, int count, int userVote)> callback);
    void submitVote(int levelId, int stars, std::string const& username, std::string const& thumbnailId, ActionCallback callback);

    void downloadThumbnail(int levelId, DownloadCallback callback);
    
    void checkExists(int levelId, ExistsCallback callback);
    
    void checkModerator(std::string const& username, ModeratorCallback callback);
    // chequeo de moderador “seguro” con accountID > 0 obligatorio
    void checkModeratorAccount(std::string const& username, int accountID, ModeratorCallback callback);
    
    void checkUserStatus(std::string const& username, ModeratorCallback callback);

    void getThumbnail(int levelId, DownloadCallback callback);
    
    void syncVerificationQueue(PendingCategory category, QueueCallback callback);
    
    void claimQueueItem(int levelId, PendingCategory category, std::string const& username, ActionCallback callback, std::string const& type = "");
    
    void acceptQueueItem(int levelId, PendingCategory category, std::string const& username, ActionCallback callback, std::string const& targetFilename = "", std::string const& type = "");
    
    void rejectQueueItem(int levelId, PendingCategory category, std::string const& username, std::string const& reason, ActionCallback callback, std::string const& type = "");
    
    void submitReport(int levelId, std::string const& username, std::string const& note, ActionCallback callback);
    
    void addModerator(std::string const& username, std::string const& adminUser, ActionCallback callback);
    
    void removeModerator(std::string const& username, std::string const& adminUser, ActionCallback callback);

    void getTopCreators(ActionCallback callback);

    void getTopThumbnails(ActionCallback callback);
    
    void deleteThumbnail(int levelId, std::string const& username, int accountID, ActionCallback callback);
    
    // configuracion
    void setServerEnabled(bool enabled);

    // paso datos a CCTexture2D
    cocos2d::CCTexture2D* webpToTexture(std::vector<uint8_t> const& webpData);

private:
    ThumbnailAPI();
    ~ThumbnailAPI() = default;
    
    ThumbnailAPI(const ThumbnailAPI&) = delete;
    ThumbnailAPI& operator=(const ThumbnailAPI&) = delete;

    // estado residual mantenido por compatibilidad — la logica real esta en los servicios
    bool m_serverEnabled = true;
    int m_uploadCount = 0;
};

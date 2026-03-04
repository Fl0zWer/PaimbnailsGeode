#pragma once

#include <Geode/Geode.hpp>
#include <Geode/utils/web.hpp>
#include <Geode/utils/function.hpp>
#include "ThumbnailTypes.hpp"
#include <string>
#include <vector>
#include <memory>

class HttpClient {
public:
    // Geode v5: CopyableFunction reemplaza std::function — misma semantica copiable,
    // pero usa std23::function internamente para mejor compatibilidad ABI.
    using UploadCallback = geode::CopyableFunction<void(bool success, std::string const& message)>;
    using DownloadCallback = geode::CopyableFunction<void(bool success, std::vector<uint8_t> const& data, int width, int height)>;
    using CheckCallback = geode::CopyableFunction<void(bool exists)>;
    using ModeratorCallback = geode::CopyableFunction<void(bool isModerator, bool isAdmin)>;
    using GenericCallback = geode::CopyableFunction<void(bool success, std::string const& response)>;
    using BanListCallback = geode::CopyableFunction<void(bool success, std::string const& jsonData)>;
    using BanUserCallback = geode::CopyableFunction<void(bool success, std::string const& message)>;
    using ModeratorsListCallback = geode::CopyableFunction<void(bool success, std::vector<std::string> const& moderators)>;

    static HttpClient& get() {
        static HttpClient instance;
        return instance;
    }

    std::string getServerURL() const { return m_serverURL; }
    void setServerURL(std::string const& url);

    // mod code
    std::string getModCode() const { return m_modCode; }
    void setModCode(std::string const& code);

    // limpia tasks
    void cleanTasks();


    // sube thumb png
    void uploadThumbnail(int levelId, std::vector<uint8_t> const& pngData, std::string const& username, UploadCallback callback);

    // sube gif (mod/admin)
    void uploadGIF(int levelId, std::vector<uint8_t> const& gifData, std::string const& username, UploadCallback callback);

    // lista thumbs
    void getThumbnails(int levelId, GenericCallback callback);

    // info thumb
    void getThumbnailInfo(int levelId, GenericCallback callback);

    // sube suggestion
    void uploadSuggestion(int levelId, std::vector<uint8_t> const& pngData, std::string const& username, UploadCallback callback);
    // sube update
    void uploadUpdate(int levelId, std::vector<uint8_t> const& pngData, std::string const& username, UploadCallback callback);
    // descarga suggestion
    void downloadSuggestion(int levelId, DownloadCallback callback);
    // descarga update
    void downloadUpdate(int levelId, DownloadCallback callback);
    // descarga reportada
    void downloadReported(int levelId, DownloadCallback callback);

    // sube profile img
    void uploadProfile(int accountID, std::vector<uint8_t> const& pngData, std::string const& username, UploadCallback callback);
    // sube profile gif (mod/admin/donator)
    void uploadProfileGIF(int accountID, std::vector<uint8_t> const& gifData, std::string const& username, UploadCallback callback);
    // descarga profile
    void downloadProfile(int accountID, std::string const& username, DownloadCallback callback);
    // descarga desde url (valida magic bytes de imagen)
    void downloadFromUrl(std::string const& url, DownloadCallback callback);
    // descarga desde url sin validar magic bytes (para audio, etc.)
    void downloadFromUrlRaw(std::string const& url, DownloadCallback callback);

    // sube imagen de perfil (profileimg)
    void uploadProfileImg(int accountID, std::vector<uint8_t> const& imgData, std::string const& username, std::string const& contentType, UploadCallback callback);
    // sube gif de perfil (profileimg)
    void uploadProfileImgGIF(int accountID, std::vector<uint8_t> const& gifData, std::string const& username, UploadCallback callback);
    // descarga imagen de perfil (profileimg)
    void downloadProfileImg(int accountID, DownloadCallback callback, bool isSelf = false);

    // sube config profile
    void uploadProfileConfig(int accountID, std::string const& jsonConfig, GenericCallback callback);
    // descarga config profile
    void downloadProfileConfig(int accountID, GenericCallback callback);

    // descarga thumb (respeta setting priority)
    void downloadThumbnail(int levelId, DownloadCallback callback);
    void downloadThumbnail(int levelId, bool isGif, DownloadCallback callback);
    
    // existe thumb?
    void checkThumbnailExists(int levelId, CheckCallback callback);
    
    // es mod?
    void checkModerator(std::string const& username, ModeratorCallback callback);
    // es mod por accountid (mas seguro)
    void checkModeratorAccount(std::string const& username, int accountID, ModeratorCallback callback);

    // reportes
    void submitReport(int levelId, std::string const& username, std::string const& note, GenericCallback callback);

    // lista baneados
    void getBanList(BanListCallback callback);

    // banear user
    void banUser(std::string const& username, std::string const& reason, BanUserCallback callback);
    // unban
    void unbanUser(std::string const& username, BanUserCallback callback);

    // lista mods
    void getModerators(ModeratorsListCallback callback);
    
    // votos
    void getRating(int levelId, std::string const& username, std::string const& thumbnailId, GenericCallback callback);
    void submitVote(int levelId, int stars, std::string const& username, std::string const& thumbnailId, GenericCallback callback);

    // get/post generico
    void get(std::string const& endpoint, GenericCallback callback);
    void post(std::string const& endpoint, std::string const& data, GenericCallback callback);
    // post autenticado (incluye X-Mod-Code para operaciones privilegiadas)
    void postWithAuth(std::string const& endpoint, std::string const& data, GenericCallback callback);

    // pet shop
    void getPetShopList(GenericCallback callback);
    void downloadPetShopItem(std::string const& itemId, std::string const& format,
        geode::CopyableFunction<void(bool, std::vector<uint8_t> const&)> callback);
    void uploadPetShopItem(std::string const& name, std::string const& creator,
        std::vector<uint8_t> const& imageData, std::string const& format,
        UploadCallback callback);

private:
    HttpClient();
    ~HttpClient() = default;
    
    HttpClient(HttpClient const&) = delete;
    HttpClient& operator=(HttpClient const&) = delete;

    std::string m_serverURL;
    std::string m_apiKey;
    std::string m_modCode;
    
    // cache exists pa no spamear
    struct ExistsCacheEntry {
        bool exists;
        time_t timestamp;
    };
    std::map<int, ExistsCacheEntry> m_existsCache;
    static constexpr int EXISTS_CACHE_DURATION = 300; // 5 min

    // request async
    void performRequest(
        std::string const& url,
        std::string const& method,
        std::string const& postData,
        std::vector<std::string> const& headers,
        geode::CopyableFunction<void(bool, std::string const&)> callback
    );
    
    // descarga binary (sin a string)
    void performBinaryRequest(
        std::string const& url,
        std::vector<std::string> const& headers,
        geode::CopyableFunction<void(bool, std::vector<uint8_t> const&)> callback
    );

    // sube archivo
    void performUpload(
        std::string const& url,
        std::string const& fieldName,
        std::string const& filename,
        std::vector<uint8_t> const& data,
        std::vector<std::pair<std::string, std::string>> const& formFields,
        std::vector<std::string> const& headers,
        geode::CopyableFunction<void(bool, std::string const&)> callback,
        std::string const& contentType = "image/png"
    );
};


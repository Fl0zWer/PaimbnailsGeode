#pragma once

#include <Geode/Geode.hpp>
#include <Geode/utils/web.hpp>
#include "ThumbnailTypes.hpp"
#include <string>
#include <functional>
#include <vector>
#include <memory>

class HttpClient {
public:
    using UploadCallback = std::function<void(bool success, const std::string& message)>;
    using DownloadCallback = std::function<void(bool success, const std::vector<uint8_t>& data, int width, int height)>;
    using CheckCallback = std::function<void(bool exists)>;
    using ModeratorCallback = std::function<void(bool isModerator, bool isAdmin)>;
    using GenericCallback = std::function<void(bool success, const std::string& response)>;
    using BanListCallback = std::function<void(bool success, const std::string& jsonData)>;
    using BanUserCallback = std::function<void(bool success, const std::string& message)>;
    using ModeratorsListCallback = std::function<void(bool success, const std::vector<std::string>& moderators)>;

    static HttpClient& get() {
        static HttpClient instance;
        return instance;
    }

    std::string getServerURL() const { return m_serverURL; }
    void setServerURL(const std::string& url);
    
    // mod code
    std::string getModCode() const { return m_modCode; }
    void setModCode(const std::string& code);
    
    // limpia tasks
    void cleanTasks();


    // sube thumb png
    void uploadThumbnail(int levelId, const std::vector<uint8_t>& pngData, const std::string& username, UploadCallback callback);

    // sube gif (mod/admin)
    void uploadGIF(int levelId, const std::vector<uint8_t>& gifData, const std::string& username, UploadCallback callback);

    // lista thumbs
    void getThumbnails(int levelId, GenericCallback callback);

    // info thumb
    void getThumbnailInfo(int levelId, GenericCallback callback);

    // sube suggestion
    void uploadSuggestion(int levelId, const std::vector<uint8_t>& pngData, const std::string& username, UploadCallback callback);
    // sube update
    void uploadUpdate(int levelId, const std::vector<uint8_t>& pngData, const std::string& username, UploadCallback callback);
    // descarga suggestion
    void downloadSuggestion(int levelId, DownloadCallback callback);
    // descarga update
    void downloadUpdate(int levelId, DownloadCallback callback);
    // descarga reportada
    void downloadReported(int levelId, DownloadCallback callback);

    // sube profile img
    void uploadProfile(int accountID, const std::vector<uint8_t>& pngData, const std::string& username, UploadCallback callback);
    // sube profile gif (mod/admin/donator)
    void uploadProfileGIF(int accountID, const std::vector<uint8_t>& gifData, const std::string& username, UploadCallback callback);
    // descarga profile
    void downloadProfile(int accountID, const std::string& username, DownloadCallback callback);
    // descarga desde url
    void downloadFromUrl(const std::string& url, DownloadCallback callback);
    
    // sube config profile
    void uploadProfileConfig(int accountID, const std::string& jsonConfig, GenericCallback callback);
    // descarga config profile
    void downloadProfileConfig(int accountID, GenericCallback callback);

    // descarga thumb (respeta setting priority)
    void downloadThumbnail(int levelId, DownloadCallback callback);
    void downloadThumbnail(int levelId, bool isGif, DownloadCallback callback);
    
    // existe thumb?
    void checkThumbnailExists(int levelId, CheckCallback callback);
    
    // es mod?
    void checkModerator(const std::string& username, ModeratorCallback callback);
    // es mod por accountid (mas seguro)
    void checkModeratorAccount(const std::string& username, int accountID, ModeratorCallback callback);

    // reportes
    void submitReport(int levelId, const std::string& username, const std::string& note, GenericCallback callback);

    // lista baneados
    void getBanList(BanListCallback callback);

    // banear user
    void banUser(const std::string& username, const std::string& reason, BanUserCallback callback);
    // unban
    void unbanUser(const std::string& username, BanUserCallback callback);

    // lista mods
    void getModerators(ModeratorsListCallback callback);
    
    // votos
    void getRating(int levelId, const std::string& username, const std::string& thumbnailId, GenericCallback callback);
    void submitVote(int levelId, int stars, const std::string& username, const std::string& thumbnailId, GenericCallback callback);
    
    // get/post generico
    void get(const std::string& endpoint, GenericCallback callback);
    void post(const std::string& endpoint, const std::string& data, GenericCallback callback);
    // post autenticado (incluye X-Mod-Code para operaciones privilegiadas)
    void postWithAuth(const std::string& endpoint, const std::string& data, GenericCallback callback);

private:
    HttpClient();
    ~HttpClient() = default;
    
    HttpClient(const HttpClient&) = delete;
    HttpClient& operator=(const HttpClient&) = delete;

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
        const std::string& url,
        const std::string& method,
        const std::string& postData,
        const std::vector<std::string>& headers,
        std::function<void(bool, const std::string&)> callback
    );
    
    // descarga binary (sin a string)
    void performBinaryRequest(
        const std::string& url,
        const std::vector<std::string>& headers,
        std::function<void(bool, const std::vector<uint8_t>&)> callback
    );

    // sube archivo
    void performUpload(
        const std::string& url,
        const std::string& fieldName,
        const std::string& filename,
        const std::vector<uint8_t>& data,
        const std::vector<std::pair<std::string, std::string>>& formFields,
        const std::vector<std::string>& headers,
        std::function<void(bool, const std::string&)> callback,
        const std::string& contentType = "image/png"
    );
};


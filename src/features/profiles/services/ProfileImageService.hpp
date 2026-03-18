#pragma once

#include <Geode/Geode.hpp>
#include <Geode/utils/function.hpp>
#include "ProfileThumbs.hpp"
#include <string>
#include <vector>

/**
 * ProfileImageService — subida/bajada de imagenes y configuracion de perfil.
 * Extraido de ThumbnailAPI.
 */
class ProfileImageService {
public:
    using UploadCallback   = geode::CopyableFunction<void(bool success, std::string const& message)>;
    using DownloadCallback = geode::CopyableFunction<void(bool success, cocos2d::CCTexture2D* texture)>;
    using ActionCallback   = geode::CopyableFunction<void(bool success, std::string const& message)>;

    static ProfileImageService& get() {
        static ProfileImageService instance;
        return instance;
    }

    void setServerEnabled(bool enabled) { m_serverEnabled = enabled; }

    // background de perfil (banner)
    void uploadProfile(int accountID, std::vector<uint8_t> const& pngData,
                       std::string const& username, UploadCallback callback);
    void uploadProfileGIF(int accountID, std::vector<uint8_t> const& gifData,
                          std::string const& username, UploadCallback callback);
    void downloadProfile(int accountID, std::string const& username, DownloadCallback callback);

    // foto de perfil (profileimg)
    void uploadProfileImg(int accountID, std::vector<uint8_t> const& imgData,
                          std::string const& username, std::string const& contentType,
                          UploadCallback callback);
    void uploadProfileImgGIF(int accountID, std::vector<uint8_t> const& gifData,
                             std::string const& username, UploadCallback callback);
    void downloadProfileImg(int accountID, DownloadCallback callback, bool isSelf = false);

    // perfil pendiente (moderadores en centro de verificacion)
    void downloadPendingProfile(int accountID, DownloadCallback callback);

    // configuracion de perfil
    void uploadProfileConfig(int accountID, ProfileConfig const& config, ActionCallback callback);
    void downloadProfileConfig(int accountID,
        geode::CopyableFunction<void(bool, ProfileConfig const&)> callback);

private:
    ProfileImageService() = default;
    ProfileImageService(ProfileImageService const&) = delete;
    ProfileImageService& operator=(ProfileImageService const&) = delete;

    bool m_serverEnabled = true;
    int  m_uploadCount   = 0;
};

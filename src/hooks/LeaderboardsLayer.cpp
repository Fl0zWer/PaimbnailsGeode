#include <Geode/modify/LeaderboardsLayer.hpp>
#include "../utils/DynamicPopupRegistry.hpp"
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include "../utils/PaimonButtonHighlighter.hpp"
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/GJAccountManager.hpp>
#include <Geode/binding/LeaderboardsLayer.hpp>
#include <Geode/loader/Mod.hpp>
#include "../utils/Localization.hpp"
#include "../utils/PaimonNotification.hpp"
#include <Geode/utils/cocos.hpp>
#include <Geode/utils/string.hpp>
#include <fstream>
#include "../features/profiles/services/ProfileThumbs.hpp"
#include "../utils/FileDialog.hpp"
#include "../utils/SpriteHelper.hpp"
#include "../features/community/ui/CommunityHubLayer.hpp"
#include "../managers/ThumbnailAPI.hpp"
#include "../features/backgrounds/services/LayerBackgroundManager.hpp"
#include "../features/transitions/services/TransitionManager.hpp"

using namespace geode::prelude;

class ProfilePreviewPopup : public geode::Popup {
protected:
    std::vector<uint8_t> m_data;
    std::string m_username;
    geode::CopyableFunction<void()> m_callback;

    bool init() {
        if (!Popup::init(360.f, 180.f)) return false;

        this->setTitle("Preview Profile");

        // crear textura desde datos
        auto* image = new CCImage();
        if (!image->initWithImageData(const_cast<uint8_t*>(m_data.data()), m_data.size())) {
            CC_SAFE_DELETE(image);
            return false;
        }
        auto* texture = new CCTexture2D();
        if (!texture->initWithImage(image)) {
            CC_SAFE_DELETE(image);
            CC_SAFE_DELETE(texture);
            return false;
        }
        image->release();
        texture->autorelease();

        // recoger config actual
        ProfileConfig config;
        config.backgroundType = Mod::get()->getSavedValue<std::string>("scorecell-background-type", "thumbnail");
        config.blurIntensity = Mod::get()->getSavedValue<float>("scorecell-background-blur", 3.0f);
        config.darkness = Mod::get()->getSavedValue<float>("scorecell-background-darkness", 0.2f);
        config.useGradient = false;
        config.colorA = {255, 255, 255};
        config.colorB = {255, 255, 255};
        config.separatorColor = {0, 0, 0};
        config.separatorOpacity = 50;

        // crear nodo de preview
        CCSize previewSize = {340, 50}; // tamano aprox de celda de puntuacion
        auto previewNode = ProfileThumbs::get().createProfileNode(texture, config, previewSize);
        
        if (previewNode) {
            previewNode->setPosition(m_mainLayer->getContentSize() / 2 + CCPoint{0, 10});
            m_mainLayer->addChild(previewNode);
        }

        // anadir boton subir
        auto uploadBtn = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("Upload"),
            this,
            menu_selector(ProfilePreviewPopup::onUpload)
        );
        auto menu = CCMenu::create();
        menu->addChild(uploadBtn);
        menu->setPosition({m_mainLayer->getContentSize().width / 2, 30});
        m_mainLayer->addChild(menu);

        paimon::markDynamicPopup(this);
        return true;
    }

    void onUpload(CCObject*) {
        if (m_callback) m_callback();
        this->onClose(nullptr);
    }

public:
    static ProfilePreviewPopup* create(std::vector<uint8_t> const& data, std::string const& username, geode::CopyableFunction<void()> callback) {
        auto ret = new ProfilePreviewPopup();
        ret->m_data = data;
        ret->m_username = username;
        ret->m_callback = callback;
        if (ret && ret->init()) {
            ret->autorelease();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }
};

class $modify(PaimonLeaderboardsLayer, LeaderboardsLayer) {
    static void onModify(auto& self) {
        (void)self.setHookPriorityAfterPost("LeaderboardsLayer::init", "geode.node-ids");
    }

    $override
    bool init(LeaderboardType type, LeaderboardStat stat) {
        if (!LeaderboardsLayer::init(type, stat)) return false;
        
        LayerBackgroundManager::get().applyBackground(this, "leaderboards");

        createPaimonButtons();
        updateTabColors(type);
        
        return true;
    }

    void updateTabColors(LeaderboardType type) {
        // usar node IDs oficiales de geode.node-ids para tabs
        // cada tab es un menu separado: top-100-menu, global-menu, creators-menu, friends-menu
        std::vector<std::string> tabIDs = {"top-100-menu", "global-menu", "creators-menu", "friends-menu"};

        // reset todos los tabs
        for (auto const& tabID : tabIDs) {
            if (auto menu = this->getChildByID(tabID)) {
                if (auto btn = menu->getChildByType<CCMenuItemSpriteExtra>(0)) {
                    btn->setColor({255, 255, 255});
                }
            }
        }

        // mapeo tipo -> ID del menu tab
        std::string activeID;
        switch (type) {
            case LeaderboardType::Default: activeID = "top-100-menu"; break;
            case LeaderboardType::Global: activeID = "global-menu"; break;
            case LeaderboardType::Creator: activeID = "creators-menu"; break;
            case LeaderboardType::Friends: activeID = "friends-menu"; break;
            default: break;
        }

        // resaltar tab activo
        if (!activeID.empty()) {
            if (auto menu = this->getChildByID(activeID)) {
                if (auto btn = menu->getChildByType<CCMenuItemSpriteExtra>(0)) {
                    btn->setColor({0, 255, 0});
                }
            }
        }
    }

    $override
    void onTop(CCObject* sender) {
        LeaderboardsLayer::onTop(sender);
        updateTabColors(LeaderboardType::Default); 
    }

    $override
    void onGlobal(CCObject* sender) {
        LeaderboardsLayer::onGlobal(sender);
        updateTabColors(LeaderboardType::Global); // Global (Relative/yo)
    }

    $override
    void onCreators(CCObject* sender) {
        LeaderboardsLayer::onCreators(sender);
        updateTabColors(LeaderboardType::Creator);
    }

    // onFriends removed as it might not exist or be hookable
    
    void createPaimonButtons() {
        auto menu = CCMenu::create();
        menu->setPosition({30, 100}); // Y un poco ajustada pa que quepan ambos
        this->addChild(menu);

        // boton moderador
        auto modSprite = paimon::SpriteHelper::safeCreateWithFrameName("GJ_infoIcon_001.png");
        if (modSprite) {
            modSprite->setScale(0.8f);
        }

        auto modBtn = CCMenuItemSpriteExtra::create(
            modSprite,
            this,
            menu_selector(PaimonLeaderboardsLayer::onOpenModerators)
        );
        modBtn->setPosition({0, 0});
        menu->addChild(modBtn);
        
        // boton subir banner (debajo del de moderador)
        auto uploadSprite = paimon::SpriteHelper::safeCreateWithFrameName("GJ_plusBtn_001.png");
        if (uploadSprite) {
            uploadSprite->setScale(0.7f);
        }
        
        auto uploadBtn = CCMenuItemSpriteExtra::create(
            uploadSprite,
            this,
            menu_selector(PaimonLeaderboardsLayer::onUploadBanner)
        );
        uploadBtn->setPosition({0, -35}); // debajo del boton mod
        menu->addChild(uploadBtn);
    }

    void onOpenModerators(CCObject*) {
        TransitionManager::get().pushScene(CommunityHubLayer::scene());
    }

    void onUploadBanner(CCObject*) {
        bool canUploadGIF = Mod::get()->getSavedValue<bool>("is-verified-vip", false)
                         || Mod::get()->getSavedValue<bool>("is-verified-moderator", false)
                         || Mod::get()->getSavedValue<bool>("is-verified-admin", false);

        this->setTouchEnabled(false);
        Ref<PaimonLeaderboardsLayer> self = this;
        pt::openImageFileDialog([self, canUploadGIF](std::optional<std::filesystem::path> result) {
            self->setTouchEnabled(true);

            if (result.has_value()) {
                auto path = result.value();
                if (!path.empty()) {
                    auto ext = geode::utils::string::toLower(
                        geode::utils::string::pathToString(path.extension())
                    );

                    if (ext == ".gif") {
                        if (!canUploadGIF) {
                             PaimonNotify::create("GIFs are restricted to Mods/Admins/Donators", NotificationIcon::Error)->show();
                             return;
                        }
                        self->processProfileGIF(path);
                    } else {
                        self->processProfileImage(path);
                    }
                }
            }
        });
    }

    void processProfileGIF(std::filesystem::path path) {
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            PaimonNotify::create("Failed to read GIF file", NotificationIcon::Error)->show();
            return;
        }
        std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        
        if (data.size() > 10 * 1024 * 1024) {
             PaimonNotify::create("GIF too large (max 10MB)", NotificationIcon::Error)->show();
             return;
        }

        int accountID = GJAccountManager::get()->m_accountID;
        std::string username = GJAccountManager::get()->m_username;

        PaimonNotify::create(Localization::get().getString("capture.uploading").c_str(), NotificationIcon::Info)->show();

        ThumbnailAPI::get().uploadProfileGIF(accountID, data, username, [](bool success, std::string const& msg) {
            if (success) {
                PaimonNotify::create(Localization::get().getString("capture.upload_success").c_str(), NotificationIcon::Success)->show();
            } else {
                PaimonNotify::create((Localization::get().getString("capture.upload_error") + ": " + msg).c_str(), NotificationIcon::Error)->show();
            }
        });
    }

    void processProfileImage(std::filesystem::path path) {
        // cargar imagen pa verificar que es valida
        std::vector<uint8_t> data;
        auto* img = new CCImage();
        if (!img->initWithImageFile(geode::utils::string::pathToString(path).c_str())) { 
            img->release();
            PaimonNotify::create(Localization::get().getString("profile.image_open_error").c_str(), NotificationIcon::Error)->show(); 
            return; 
        }
        img->release();

        // leer bytes crudos pa subir
        std::ifstream file(path, std::ios::binary);
        if (!file) return;
        std::vector<uint8_t> rawData((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        if (rawData.size() > 7 * 1024 * 1024) {
             PaimonNotify::create("Image too large (max 7MB)", NotificationIcon::Error)->show();
             return;
        }

        int accountID = GJAccountManager::get()->m_accountID;
        std::string username = GJAccountManager::get()->m_username;

        auto popup = ProfilePreviewPopup::create(rawData, username, [rawData, accountID, username]() {
            PaimonNotify::create(Localization::get().getString("capture.uploading").c_str(), NotificationIcon::Info)->show();
            ThumbnailAPI::get().uploadProfile(accountID, rawData, username, [](bool success, std::string const& msg) {
                if (success) {
                    bool isPending = (msg.find("pending") != std::string::npos || msg.find("verification") != std::string::npos);
                    if (isPending) {
                        PaimonNotify::create("Background submitted! Pending moderator verification.", NotificationIcon::Warning)->show();
                    } else {
                        PaimonNotify::create(Localization::get().getString("capture.upload_success").c_str(), NotificationIcon::Success)->show();
                    }
                } else {
                    PaimonNotify::create((Localization::get().getString("capture.upload_error") + ": " + msg).c_str(), NotificationIcon::Error)->show();
                }
            });
        });
        if (popup) popup->show();
    }
    

    $override
    void onExit() {
        ProfileThumbs::get().clearAllCache();
        log::info("[LeaderboardsLayer] Profile cache cleared on exit");
        
        LeaderboardsLayer::onExit();
    }
};


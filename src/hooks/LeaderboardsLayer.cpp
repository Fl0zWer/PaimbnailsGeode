#include <Geode/modify/LeaderboardsLayer.hpp>
#include <Geode/loader/Event.hpp>
#include <Geode/utils/file.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include "../utils/PaimonButtonHighlighter.hpp"
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/GJAccountManager.hpp>
#include <Geode/binding/LeaderboardsLayer.hpp>
#include <Geode/loader/Mod.hpp>
#include "../utils/Localization.hpp"
#include <Geode/utils/cocos.hpp>
#include <fstream>
#include <algorithm>
#include <random>
#include "../managers/ProfileThumbs.hpp"
#include "../utils/FileDialog.hpp"
#include "../layers/ModeratorsLayer.hpp"
#include "../managers/ThumbnailAPI.hpp"

using namespace geode::prelude;
using namespace cocos2d;

class ProfilePreviewPopup : public geode::Popup {
protected:
    std::vector<uint8_t> m_data;
    std::string m_username;
    std::function<void()> m_callback;

    bool init() {
        if (!Popup::init(360.f, 180.f)) return false;

        this->setTitle("Preview Profile");

        // crear textura desde datos
        auto image = new CCImage();
        if (!image->initWithImageData(const_cast<uint8_t*>(m_data.data()), m_data.size())) {
            image->release();
            return false;
        }
        auto texture = new CCTexture2D();
        texture->initWithImage(image);
        image->release();
        texture->autorelease();

        // recoger config actual
        ProfileConfig config;
        try {
            config.backgroundType = Mod::get()->getSavedValue<std::string>("scorecell-background-type", "thumbnail");
            config.blurIntensity = Mod::get()->getSavedValue<float>("scorecell-background-blur", 3.0f);
            config.darkness = Mod::get()->getSavedValue<float>("scorecell-background-darkness", 0.2f);
            config.useGradient = false; 
            config.colorA = {255, 255, 255};
            config.colorB = {255, 255, 255};
            config.separatorColor = {0, 0, 0};
            config.separatorOpacity = 50;
        } catch (...) {}

        // crear nodo de preview
        CCSize previewSize = {340, 50}; // tamano aprox de celda de puntuacion
        auto previewNode = ProfileThumbs::get().createProfileNode(texture, config, previewSize);
        
        if (previewNode) {
            previewNode->setPosition(m_mainLayer->getContentSize() / 2 + CCPoint{0, 10});
            m_mainLayer->addChild(previewNode);
        }

        // añadir boton subir
        auto uploadBtn = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("Upload"),
            this,
            menu_selector(ProfilePreviewPopup::onUpload)
        );
        auto menu = CCMenu::create();
        menu->addChild(uploadBtn);
        menu->setPosition({m_mainLayer->getContentSize().width / 2, 30});
        m_mainLayer->addChild(menu);

        return true;
    }

    void onUpload(CCObject*) {
        if (m_callback) m_callback();
        this->onClose(nullptr);
    }

public:
    static ProfilePreviewPopup* create(const std::vector<uint8_t>& data, const std::string& username, std::function<void()> callback) {
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
    bool init(LeaderboardType type, LeaderboardStat stat) {
        if (!LeaderboardsLayer::init(type, stat)) return false;
        
        createPaimonButtons();
        updateTabColors(type);
        
        return true;
    }

    void updateTabColors(LeaderboardType type) {
        // buscar el menu que contiene las pestañas
        CCMenu* tabMenu = nullptr;
        CCArrayExt<CCNode*> children(this->getChildren());
        for (auto child : children) {
            if (auto menu = typeinfo_cast<CCMenu*>(child)) {
                // heuristica: el menu de tabs suele tener los botones Top, Global, etc
                if (menu->getChildrenCount() >= 3) {
                    tabMenu = menu;
                    break;
                }
            }
        }

        if (!tabMenu) return;

        // reset
        for (auto item : CCArrayExt<CCNode*>(tabMenu->getChildren())) {
            if (auto btn = typeinfo_cast<CCMenuItemSpriteExtra*>(item)) {
                btn->setColor({255, 255, 255});
            }
        }

        // resaltar boton activo (mapeo aproximado)
        // LeaderboardType::Default (Top 100) -> 0?
        // LeaderboardType::Friends -> ?
        // LeaderboardType::Relative (Global conmigo) -> ?
        // LeaderboardType::Creators -> ?
        
        int tag = -1;
        // NOTE: estos valores son suposiciones pa tabs estandar
        // puede hacer falta ajustar segun checks reales de Geode/GD
        // Default=Top100, Relative=Global, Creators=Creators, Friends=Friends
        switch (type) {
            case LeaderboardType::Default: tag = 1; break; // Top (1)
            case LeaderboardType::Global: tag = 2; break; // Global (2)
            case LeaderboardType::Creator: tag = 3; break; // Creators (3)
            case LeaderboardType::Friends: tag = 4; break; // Friends (4)
            default: break;
        }
        
        // en GD suelen ser tags: Top=1, Global=2, Creators=3, Friends=4
        // comprobar si el hijo tiene ese tag
        
        if (tag != -1) {
             if (auto btn = tabMenu->getChildByTag(tag)) {
                if (auto spriteBtn = typeinfo_cast<CCMenuItemSpriteExtra*>(btn)) {
                    spriteBtn->setColor({0, 255, 0});
                }
            }
        }
    }

    void onTop(CCObject* sender) {
        LeaderboardsLayer::onTop(sender);
        updateTabColors(LeaderboardType::Default); 
    }

    void onGlobal(CCObject* sender) {
        LeaderboardsLayer::onGlobal(sender);
        updateTabColors(LeaderboardType::Global); // Global (Relative/yo)
    }

    void onCreators(CCObject* sender) {
        LeaderboardsLayer::onCreators(sender);
        updateTabColors(LeaderboardType::Creator);
    }

    // onFriends removed as it might not exist or be hookable
    
    void createPaimonButtons() {
        auto winSize = CCDirector::sharedDirector()->getWinSize();
        
        // menu pa botones
        auto menu = CCMenu::create();
        menu->setPosition({30, 100}); // Y un poco ajustada pa que quepan ambos
        this->addChild(menu);

        // boton moderador
        auto modSprite = CCSprite::createWithSpriteFrameName("GJ_infoIcon_001.png");
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
        auto uploadSprite = CCSprite::createWithSpriteFrameName("GJ_plusBtn_001.png");
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
        ModeratorsLayer::create()->show();
    }

    void onUploadBanner(CCObject*) {
        // comprobar permisos pa GIF
        bool isMod = false;
        bool isAdmin = false;
        try {
            isMod = Mod::get()->getSavedValue<bool>("is-verified-moderator", false);
            isAdmin = Mod::get()->getSavedValue<bool>("is-verified-admin", false);
        } catch(...) {}
        
        bool canUploadGIF = isMod || isAdmin;

        auto result = pt::openImageFileDialog();

        if (result.has_value()) {
            auto path = result.value();
            if (!path.empty()) {
                std::string ext = path.extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

                if (ext == ".gif") {
                    if (!canUploadGIF) {
                         Notification::create("GIFs are restricted to Mods/Admins/Donators", NotificationIcon::Error)->show();
                         return;
                    }
                    this->processProfileGIF(path);
                } else {
                    this->processProfileImage(path);
                }
            }
        }
    }

    void processProfileGIF(std::filesystem::path path) {
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            Notification::create("Failed to read GIF file", NotificationIcon::Error)->show();
            return;
        }
        std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        
        if (data.size() > 10 * 1024 * 1024) {
             Notification::create("GIF too large (max 10MB)", NotificationIcon::Error)->show();
             return;
        }

        int accountID = GJAccountManager::get()->m_accountID;
        std::string username = GJAccountManager::get()->m_username;

        Notification::create(Localization::get().getString("capture.uploading").c_str(), NotificationIcon::Info)->show();

        ThumbnailAPI::get().uploadProfileGIF(accountID, data, username, [this](bool success, const std::string& msg) {
            if (success) {
                Notification::create(Localization::get().getString("capture.upload_success").c_str(), NotificationIcon::Success)->show();
            } else {
                Notification::create((Localization::get().getString("capture.upload_error") + ": " + msg).c_str(), NotificationIcon::Error)->show();
            }
        });
    }

    void processProfileImage(std::filesystem::path path) {
        // cargar imagen pa verificar que es valida
        std::vector<uint8_t> data;
        CCImage img;
        if (!img.initWithImageFile(path.generic_string().c_str())) { 
            Notification::create(Localization::get().getString("profile.image_open_error").c_str(), NotificationIcon::Error)->show(); 
            return; 
        }

        // leer bytes crudos pa subir
        std::ifstream file(path, std::ios::binary);
        if (!file) return;
        std::vector<uint8_t> rawData((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

        if (rawData.size() > 7 * 1024 * 1024) {
             Notification::create("Image too large (max 7MB)", NotificationIcon::Error)->show();
             return;
        }

        int accountID = GJAccountManager::get()->m_accountID;
        std::string username = GJAccountManager::get()->m_username;

        // Show preview popup before uploading? Or just upload?
        // User asked for "se pueda elegir todo tipo de imagenes compatible"
        // Let's just upload for now, or use the ProfilePreviewPopup defined above if we want preview.
        // The ProfilePreviewPopup logic I see in the file seems unused or for local preview.
        // Let's use it!
        
        ProfilePreviewPopup::create(rawData, username, [rawData, accountID, username]() {
            Notification::create(Localization::get().getString("capture.uploading").c_str(), NotificationIcon::Info)->show();
            ThumbnailAPI::get().uploadProfile(accountID, rawData, username, [](bool success, const std::string& msg) {
                if (success) {
                    Notification::create(Localization::get().getString("capture.upload_success").c_str(), NotificationIcon::Success)->show();
                } else {
                    Notification::create((Localization::get().getString("capture.upload_error") + ": " + msg).c_str(), NotificationIcon::Error)->show();
                }
            });
        })->show();
    }
    


    void onExit() {
        ProfileThumbs::get().clearAllCache();
        log::info("[LeaderboardsLayer] Profile cache cleared on exit");
        
        LeaderboardsLayer::onExit();
    }
};


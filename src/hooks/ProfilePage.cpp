#include <Geode/binding/GJAccountManager.hpp>
#include <Geode/binding/GJCommentListLayer.hpp>
#include <Geode/modify/ProfilePage.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/utils/cocos.hpp>
#include <Geode/utils/string.hpp>
#include <Geode/loader/Event.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/loader/Mod.hpp>
#include <Geode/binding/GameManager.hpp>
#include "../utils/Localization.hpp"
#include "../utils/Debug.hpp"
#include <chrono>
#include <fstream>
#include "../utils/FileDialog.hpp"
#include "../managers/ProfileThumbs.hpp"
#include "../managers/ThumbsRegistry.hpp"
#include "../managers/ThumbnailAPI.hpp"
#include "../layers/CapturePreviewPopup.hpp"
#include "../layers/VerificationQueuePopup.hpp"
#include "../layers/AddModeratorPopup.hpp"
#include "../layers/BanUserPopup.hpp"
#include "../utils/Assets.hpp"
#include "../layers/ButtonEditOverlay.hpp"
#include "../managers/ButtonLayoutManager.hpp"
#include "../utils/PaimonButtonHighlighter.hpp"
#include "../utils/HttpClient.hpp"
#include "../managers/ProfileMusicManager.hpp"
#include "../layers/ProfileMusicPopup.hpp"
#include "../utils/Shaders.hpp"
#include "../utils/ImageLoadHelper.hpp"

using namespace geode::prelude;
using namespace cocos2d;

// Caché de texturas de profileimg para carga instantánea entre popups.
// Usa CCTexture2D* raw con retain manual. NO se usa Ref<> porque durante el
// shutdown del proceso el destructor estático haría release() cuando el
// CCPoolManager ya está muerto → EXCEPTION_ACCESS_VIOLATION.
// Las texturas se "leakean" intencionalmente al cerrar (el OS libera la memoria).
static std::unordered_map<int, CCTexture2D*> s_profileImgCache;

// --- helpers de caché de disco para profileimg ---
static std::filesystem::path getProfileImgCacheDir() {
    return Mod::get()->getSaveDir() / "profileimg_cache";
}

static std::filesystem::path getProfileImgCachePath(int accountID) {
    return getProfileImgCacheDir() / fmt::format("{}.dat", accountID);
}


static CCTexture2D* loadProfileImgFromDisk(int accountID) {
    auto path = getProfileImgCachePath(accountID);
    if (!std::filesystem::exists(path)) return nullptr;

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return nullptr;

    auto size = file.tellg();
    if (size <= 0) return nullptr;
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> data(static_cast<size_t>(size));
    if (!file.read(reinterpret_cast<char*>(data.data()), size)) return nullptr;
    file.close();

    CCImage img;
    if (!img.initWithImageData(const_cast<uint8_t*>(data.data()), data.size())) return nullptr;

    auto* tex = new CCTexture2D();
    if (!tex->initWithImage(&img)) {
        tex->release();
        return nullptr;
    }
    tex->autorelease();
    return tex;
}

static void saveProfileImgToDisk(int accountID, const std::vector<uint8_t>& data) {
    try {
        auto cacheDir = getProfileImgCacheDir();
        std::error_code ec;
        std::filesystem::create_directories(cacheDir, ec);
        auto cachePath = getProfileImgCachePath(accountID);
        std::ofstream cacheFile(cachePath, std::ios::binary);
        if (cacheFile) {
            cacheFile.write(reinterpret_cast<const char*>(data.data()), data.size());
            cacheFile.close();
        }
    } catch (...) {}
}

class $modify(PaimonProfilePage, ProfilePage) {
    struct Fields {
        CCMenu* m_extraMenu = nullptr;
        CCMenuItemSpriteExtra* m_gearBtn = nullptr;
        CCMenuItemSpriteExtra* m_verifyModBtn = nullptr;
        CCMenuItemSpriteExtra* m_addProfileBtn = nullptr;
        CCMenuItemSpriteExtra* m_editModeBtn = nullptr;
        CCMenuItemSpriteExtra* m_banBtn = nullptr;
        CCMenuItemSpriteExtra* m_musicBtn = nullptr;       // Botón para configurar música (solo propio perfil)
        CCMenuItemSpriteExtra* m_musicPauseBtn = nullptr;  // Botón para pausar música (todos los perfiles)
        CCMenuItemSpriteExtra* m_addProfileImgBtn = nullptr; // Botón para añadir imagen de perfil
        CCMenu* m_pauseMenu = nullptr;  // Menú del botón de pausa (para incluir en editor)
        CCClippingNode* m_profileClip = nullptr;
        CCLayerColor* m_profileSeparator = nullptr;
        CCNode* m_profileGradient = nullptr;
        CCNode* m_profileBorder = nullptr;
        CCClippingNode* m_profileImgClip = nullptr;   // Clip de la imagen de perfil (fondo normal del popup)
        CCNode* m_profileImgBorder = nullptr;          // Borde de la imagen de perfil
        bool m_isApprovedMod = false;
        bool m_isAdmin = false;
        bool m_musicPlaying = false;  // Estado de reproducción de música
    };

    bool canShowModerationControls() {
        // muestro controles si esta verificado como mod o admin
        return m_fields->m_isApprovedMod || m_fields->m_isAdmin;
    }

    std::string getViewedUsername() {
        // lo leo de los labels (los bindings aqui no exponen un campo interno)
        if (this->m_mainLayer) {
            if (auto* lbl = typeinfo_cast<CCLabelBMFont*>(this->m_mainLayer->getChildByIDRecursive("username-label"))) {
                if (lbl->getString()) return std::string(lbl->getString());
            }
            if (auto* lbl2 = typeinfo_cast<CCLabelBMFont*>(this->m_mainLayer->getChildByIDRecursive("username"))) {
                if (lbl2->getString()) return std::string(lbl2->getString());
            }
        }
        return "";
    }

    void refreshBanButtonVisibility() {
        if (!m_fields->m_banBtn) return;

        // nunca mostrar en tu propio perfil
        if (this->m_ownProfile) {
            m_fields->m_banBtn->setVisible(false);
            m_fields->m_banBtn->setEnabled(false);
            return;
        }

        bool show = canShowModerationControls();
        m_fields->m_banBtn->setVisible(show);
        m_fields->m_banBtn->setEnabled(show);

        // si se ve, tambien lo desactivo si el perfil es mod/admin
        // uso /api/moderators pa mantenerlo consistente
        auto targetName = getViewedUsername();
        if (show && !targetName.empty()) {
            auto targetLower = geode::utils::string::toLower(targetName);
            Ref<ProfilePage> self = this;
            HttpClient::get().get("/api/moderators", [self, targetLower](bool ok, const std::string& resp) {
                if (!ok) return;
                // compruebo que sigo vivo (por si acaso)
                if (!self || !self->getParent()) return;

                try {
                    auto parsed = matjson::parse(resp);
                    if (!parsed.isOk()) return;
                    auto root = parsed.unwrap();
                    auto mods = root["moderators"]; // [{ username, currentBanner }]
                    if (!mods.isArray()) return;
                    for (auto const& v : mods.asArray().unwrap()) {
                        if (!v.isObject()) continue;
                        auto u = v["username"]; 
                        if (!u.isString()) continue;
                        auto nameLower = geode::utils::string::toLower(u.asString().unwrap());
                        if (nameLower == targetLower) {
                            // ya estamos en el main thread
                            // uso getchildbyidrecursive porque m_fields puede fallar si el nodo muere
                            if (auto banBtn = typeinfo_cast<CCMenuItemSpriteExtra*>(self->getChildByIDRecursive("ban-user-button"))) {
                                banBtn->setEnabled(false);
                                banBtn->setOpacity(120);
                            }
                            return;
                        }
                    }
                } catch (...) {
                }
            });
        }
    }

    void onBanUser(CCObject*) {
        if (!canShowModerationControls()) {
            Notification::create(Localization::get().getString("ban.profile.mod_only"), NotificationIcon::Warning)->show();
            return;
        }
        if (this->m_ownProfile) {
            Notification::create(Localization::get().getString("ban.profile.self_ban"), NotificationIcon::Warning)->show();
            return;
        }

        // nombre del user en el perfil
        std::string target = getViewedUsername();
        if (target.empty()) {
            Notification::create(Localization::get().getString("ban.profile.read_error"), NotificationIcon::Error)->show();
            return;
        }
        
        BanUserPopup::create(target)->show();
    }

    void addOrUpdateProfileThumbOnPage(int accountID) {
        // visibilidad: si esta verificado o es mi perfil
        bool has = ProfileThumbs::get().has(accountID);
        log::debug("[ProfilePage] addOrUpdateProfileThumbOnPage - accountID: {}, has: {}, ownProfile: {}", 
            accountID, has, this->m_ownProfile);
        
        bool isVerified = ThumbsRegistry::get().isVerified(ThumbKind::Profile, accountID);
        
        if (!(isVerified || this->m_ownProfile)) {
            log::debug("[ProfilePage] Profile thumbnail hidden (not verified, not own profile, mod mode off)");
            return;
        }

        CCTexture2D* tex = nullptr;
        auto cached = ProfileThumbs::get().getCachedProfile(accountID);
        if (cached && cached->texture) {
            tex = cached->texture;
        } else {
            tex = ProfileThumbs::get().loadTexture(accountID);
        }

        if (!tex) {
            if (this->m_ownProfile) {
                log::info("[ProfilePage] No local profile for current user, downloading...");
                // retain pa que sobreviva
                this->retain();
                std::string username = GJAccountManager::get()->m_username;
                ThumbnailAPI::get().downloadProfile(accountID, username, [this, accountID](bool success, CCTexture2D* texture) {
                    if (success && texture) {
                        // retengo la textura
                        texture->retain();
                        ThumbnailAPI::get().downloadProfileConfig(accountID, [this, accountID, texture](bool s, const ProfileConfig& c) {
                            Loader::get()->queueInMainThread([this, accountID, texture, s, c]() {
                                try {
                                    ProfileThumbs::get().cacheProfile(accountID, texture, {255,255,255}, {255,255,255}, 0.5f);
                                    if (s) ProfileThumbs::get().cacheProfileConfig(accountID, c);
                                    
                                    // solo actualizo si sigue en la escena
                                    if (this->getParent()) {
                                        this->addOrUpdateProfileThumbOnPage(accountID);
                                    }
                                } catch(...) {}
                                
                                // suelto la textura y esto
                                texture->release();
                                this->release();
                            });
                        });
                    } else {
                        this->release();
                    }
                });
            } else {
                log::debug("[ProfilePage] No profile thumbnail found for account {}", accountID);
            }
            return;
        }
        
        log::info("[ProfilePage] Displaying profile thumbnail for account {}", accountID);

        auto f = m_fields.self();
        if (f->m_profileClip) { f->m_profileClip->removeFromParent(); f->m_profileClip = nullptr; }
        if (f->m_profileSeparator) { f->m_profileSeparator->removeFromParent(); f->m_profileSeparator = nullptr; }
        if (f->m_profileGradient) { f->m_profileGradient->removeFromParent(); f->m_profileGradient = nullptr; }
        if (f->m_profileBorder) { f->m_profileBorder->removeFromParent(); f->m_profileBorder = nullptr; }

        auto sprite = CCSprite::createWithTexture(tex);
        if (!sprite) return;

        // tamaño layer ref
        auto layer = this->m_mainLayer ? this->m_mainLayer : static_cast<CCNode*>(this);
        auto cs = layer->getContentSize();
        if (cs.width <= 1.f || cs.height <= 1.f) cs = this->getContentSize();

        // thumb esquina der
        float thumbSize = 120.f; // fijo pa esquina

        float scaleX = thumbSize / sprite->getContentWidth();
        sprite->setScale(scaleX); // scale uniforme

        // mask redondeada
        auto roundedMask = CCScale9Sprite::create("square02b_001.png"); // rounded square sprite
        if (!roundedMask) {
            // fallback sprite
            roundedMask = CCScale9Sprite::create("square02_001.png");
        }
        roundedMask->setContentSize({thumbSize, thumbSize});
        roundedMask->setColor({255, 255, 255});
        roundedMask->setOpacity(255);

        auto clip = CCClippingNode::create();
        clip->setStencil(roundedMask);
        clip->setContentSize({thumbSize, thumbSize});
        clip->setAnchorPoint({1, 1}); // anchor top-right
        
        // posicion arriba a la derecha con un pelin de padding
        float rightX = cs.width - 10.f;
        float topY = cs.height - 10.f;
        clip->setPosition({rightX, topY});
        clip->setZOrder(10); // por encima
        try {
            clip->setID("paimon-profilepage-clip"_spr);
        } catch (...) {
            log::warn("[ProfilePage] Failed to set clip ID");
        }

        sprite->setPosition(clip->getContentSize() * 0.5f);
        clip->addChild(sprite);
        layer->addChild(clip);
        f->m_profileClip = clip;

        // borde suave alrededor
        auto border = CCScale9Sprite::create("square02b_001.png");
        if (!border) border = CCScale9Sprite::create("square02_001.png");
        border->setContentSize({thumbSize + 4.f, thumbSize + 4.f});
        border->setColor({0, 0, 0});
        border->setOpacity(120);
        border->setAnchorPoint({1, 1});
        border->setPosition({rightX, topY});
        border->setZOrder(9); // detras del clip
        try {
            border->setID("paimon-profilepage-border"_spr);
        } catch (...) {
            log::warn("[ProfilePage] Failed to set border ID");
        }
        layer->addChild(border);
        f->m_profileBorder = border;

        // remove old background if any
        if (f->m_profileGradient) { f->m_profileGradient->removeFromParent(); f->m_profileGradient = nullptr; }

        ProfileConfig config = ProfileThumbs::get().getProfileConfig(accountID);
        
        // auto-enable thumbnail background if we have a texture/gif and config is default
        if (config.backgroundType == "gradient" && (tex || !config.gifKey.empty())) {
            config.backgroundType = "thumbnail";
        }

        // define size for the banner
        CCSize bannerSize = {cs.width, std::min(cs.height * 0.5f, 200.f)};
        
        // pass true to onlybackground to avoid adding the thumbnail overlay and separator
        CCNode* bg = ProfileThumbs::get().createProfileNode(tex, config, bannerSize, true);
        
        if (bg) {
            bg->setAnchorPoint({0,1});
            bg->setPosition({0, cs.height});
            bg->setZOrder(3);
            bg->setID("paimon-profilepage-bg"_spr);
            layer->addChild(bg);
            f->m_profileGradient = bg;
        }
    }

    void addOrUpdateProfileImgOnPage(int accountID, bool isSelf = false) {
        auto f = m_fields.self();

        // limpiar anteriores
        if (f->m_profileImgClip) { f->m_profileImgClip->removeFromParent(); f->m_profileImgClip = nullptr; }
        if (f->m_profileImgBorder) { f->m_profileImgBorder->removeFromParent(); f->m_profileImgBorder = nullptr; }

        // 1) si hay caché en memoria, mostrar de inmediato
        auto it = s_profileImgCache.find(accountID);
        if (it != s_profileImgCache.end() && it->second) {
            this->displayProfileImg(accountID, it->second);
        } else {
            // 2) si hay caché en disco, cargar y mostrar
            if (auto* diskTex = loadProfileImgFromDisk(accountID)) {
                diskTex->retain();
                // liberar textura anterior si existe
                auto prev = s_profileImgCache.find(accountID);
                if (prev != s_profileImgCache.end() && prev->second) prev->second->release();
                s_profileImgCache[accountID] = diskTex;
                this->displayProfileImg(accountID, diskTex);
            }
        }

        // descargar del servidor en segundo plano (actualizar caché)
        this->retain();
        ThumbnailAPI::get().downloadProfileImg(accountID, [this, accountID](bool success, CCTexture2D* texture) {
            if (!this->getParent()) { this->release(); return; }

            if (success && texture) {
                texture->retain();
                // liberar textura anterior si existe
                auto prev = s_profileImgCache.find(accountID);
                if (prev != s_profileImgCache.end() && prev->second) prev->second->release();
                s_profileImgCache[accountID] = texture;
                this->displayProfileImg(accountID, texture);
            }

            this->release();
        }, isSelf);
    }

    static bool isBrownColor(const ccColor3B& c) {
        return (c.r >= 0x70 && c.g >= 0x20 && c.g <= 0xA0 && c.b <= 0x70 && c.r > c.g && c.g >= c.b);
    }

    static bool isDarkBgColor(const ccColor3B& c) {
        return (c.r <= 0x60 && c.g <= 0x50 && c.b <= 0x40 && (c.r + c.g + c.b) > 0);
    }

    // Tinta un CCScale9Sprite completo (centro + bordes) a un color.
    // CCScale9Sprite tiene un CCSpriteBatchNode interno (_scale9Image) con 9 sprites hijos.
    // setColor() en el CCScale9Sprite NO siempre propaga a esos sprites internos,
    // así que los tintamos directamente.
    static void tintScale9(CCScale9Sprite* s9, const ccColor3B& color, GLubyte opacity) {
        if (!s9) return;

        // Activar cascade para que hijos hereden
        s9->setCascadeColorEnabled(true);
        s9->setCascadeOpacityEnabled(true);
        s9->setColor(color);
        s9->setOpacity(opacity);

        // Tintar hijos directos del batch node interno
        // El _scale9Image es el primer (y generalmente único) hijo del CCScale9Sprite
        auto s9Children = s9->getChildren();
        if (!s9Children) return;
        for (unsigned int i = 0; i < s9Children->count(); i++) {
            auto batchNode = typeinfo_cast<CCSpriteBatchNode*>(s9Children->objectAtIndex(i));
            if (!batchNode) continue;
            auto batchChildren = batchNode->getChildren();
            if (!batchChildren) continue;
            for (unsigned int j = 0; j < batchChildren->count(); j++) {
                auto spr = typeinfo_cast<CCSprite*>(batchChildren->objectAtIndex(j));
                if (spr) {
                    spr->setColor(color);
                    spr->setOpacity(opacity);
                }
            }
        }
    }

    // Estiliza los paneles del ProfilePage para integrarse con la imagen de fondo.
    // Solo actúa si hay imagen de perfil activa (m_profileImgClip != nullptr).
    void styleProfileInternalBgs(CCNode* layer) {
        if (!layer) return;

        bool hasProfileImg = (m_fields->m_profileImgClip != nullptr);

        // icon-background: tintarlo a negro si hay imagen
        if (auto* iconBg = typeinfo_cast<CCScale9Sprite*>(layer->getChildByIDRecursive("icon-background"))) {
            if (hasProfileImg) {
                tintScale9(iconBg, {0, 0, 0}, 140);
            }
        }

        // Bordes de GJCommentListLayer: opacity 0 si hay imagen, 255 si no
        std::function<void(CCNode*)> applyBorders = [&](CCNode* node) {
            if (!node) return;
            for (auto* child : CCArrayExt<CCNode*>(node->getChildren())) {
                if (typeinfo_cast<GJCommentListLayer*>(child)) {
                    for (const char* bId : {"left-border", "right-border", "top-border", "bottom-border"}) {
                        if (auto* border = child->getChildByID(bId)) {
                            if (auto* rgba = typeinfo_cast<CCNodeRGBA*>(border)) {
                                rgba->setOpacity(hasProfileImg ? GLubyte(0) : GLubyte(255));
                            }
                        }
                    }
                }
                applyBorders(child);
            }
        };
        applyBorders(layer);
    }

    // Hook: se llama cuando GD termina de cargar la info del usuario y construye los paneles.
    // Es el momento exacto en que se crea el GJCommentListLayer — aplicamos estilos aquí
    // para que los bordes nunca sean visibles ni un frame.
    void getUserInfoFinished(GJUserScore* score) {
        ProfilePage::getUserInfoFinished(score);
        if (m_fields->m_profileImgClip) {
            if (auto* layer = this->m_mainLayer) {
                styleProfileInternalBgs(layer);
            }
        }
    }

    void displayProfileImg(int accountID, CCTexture2D* tex) {
        if (!tex) return;

        auto texSize = tex->getContentSize();
        if (texSize.width <= 0.f || texSize.height <= 0.f) return;

        auto f = m_fields.self();
        if (f->m_profileImgClip) { f->m_profileImgClip->removeFromParent(); f->m_profileImgClip = nullptr; }

        auto layer = this->m_mainLayer;
        if (!layer) return;
        auto layerSize = layer->getContentSize();

        // buscar popup bg por node-id (geode.node-ids lo asigna como "background")
        CCSize popupSize = CCSize(440.f, 290.f);
        CCPoint popupCenter = ccp(layerSize.width * 0.5f, layerSize.height * 0.5f);

        if (auto bg = layer->getChildByID("background")) {
            popupSize = bg->getScaledContentSize();
            popupCenter = bg->getPosition();
        } else {
            // fallback: primer CCScale9Sprite hijo directo
            for (auto* child : CCArrayExt<CCNode*>(layer->getChildren())) {
                if (typeinfo_cast<CCScale9Sprite*>(child)) {
                    popupSize = child->getScaledContentSize();
                    popupCenter = child->getPosition();
                    break;
                }
            }
        }

        float padding = 3.f;
        CCSize imgArea = CCSize(popupSize.width - padding * 2.f, popupSize.height - padding * 2.f);

        // stencil con forma del popup
        auto stencil = CCScale9Sprite::create("GJ_square01.png");
        if (!stencil) return;
        stencil->setContentSize(imgArea);
        stencil->setAnchorPoint(ccp(0.5f, 0.5f));
        stencil->setPosition(ccp(imgArea.width * 0.5f, imgArea.height * 0.5f));

        auto clip = CCClippingNode::create();
        clip->setStencil(stencil);
        clip->setAlphaThreshold(0.05f);
        clip->setContentSize(imgArea);
        clip->setAnchorPoint(ccp(0.5f, 0.5f));
        clip->setPosition(popupCenter);

        // imagen normal como fondo del popup
        auto imgSprite = CCSprite::createWithTexture(tex);
        if (!imgSprite) return;

        float scaleX = imgArea.width / imgSprite->getContentWidth();
        float scaleY = imgArea.height / imgSprite->getContentHeight();
        imgSprite->setScale(std::max(scaleX, scaleY));
        imgSprite->setAnchorPoint(ccp(0.5f, 0.5f));
        imgSprite->setPosition(ccp(imgArea.width * 0.5f, imgArea.height * 0.5f));
        clip->addChild(imgSprite);

        // overlay oscuro suave
        auto dark = CCLayerColor::create(ccc4(0, 0, 0, 70));
        dark->setContentSize(imgArea);
        dark->setAnchorPoint(ccp(0, 0));
        dark->setPosition(ccp(0, 0));
        dark->setID("paimon-profileimg-dark-overlay"_spr);
        clip->addChild(dark);

        layer->addChild(clip, Mod::get()->getSettingValue<int64_t>("profile-img-zlayer"));
        f->m_profileImgClip = clip;

        // aplicar estilos a nodos ya existentes
        styleProfileInternalBgs(layer);

        // schedule permanente: se ejecuta cada 0.15s mientras el popup esté abierto.
        // Necesario porque GJCommentListLayer se recrea al cambiar tab / recargar comentarios
        // y sus bordes vuelven a aparecer — el tick los vuelve a ocultar inmediatamente.
        this->schedule(schedule_selector(PaimonProfilePage::tickStyleBgs), 0.15f);
    }

    void tickStyleBgs(float) {
        if (!m_fields->m_profileImgClip) {
            this->unschedule(schedule_selector(PaimonProfilePage::tickStyleBgs));
            // restaurar bordes a visibles ya que no hay imagen activa
            if (auto* layer = this->m_mainLayer) {
                styleProfileInternalBgs(layer);
            }
            return;
        }
        if (auto* layer = this->m_mainLayer) {
            styleProfileInternalBgs(layer);
        }
    }



    bool init(int accountID, bool ownProfile) {
        if (!ProfilePage::init(accountID, ownProfile)) return false;
        try {
            // cargo layouts de botones al iniciar
            ButtonLayoutManager::get().load();

            // siempre creo mi menu vertical compacto
            auto extraMenu = CCMenu::create();
            extraMenu->setID("paimon-profile-extra-menu"_spr);
            // pongo el menu al centro; los items se ponen con offsets
            auto layer = this->m_mainLayer ? this->m_mainLayer : static_cast<CCNode*>(this);
            auto cs = layer->getContentSize();
            extraMenu->setPosition({ cs.width / 2.f, cs.height / 2.f });
            this->addChild(extraMenu, 20);
            m_fields->m_extraMenu = extraMenu;

            // empiezo siempre como no moderador
            m_fields->m_isApprovedMod = false;
            m_fields->m_isAdmin = false;
            PaimonDebug::log("[ProfilePage] Inicializando perfil - status moderador: false");

            // oculto el menu por defecto, solo lo muestro si esta verificado
            extraMenu->setVisible(false);

            // boton de ban (solo mods/admins verificados, nunca en tu propio perfil)
            {
                auto banSpr = ButtonSprite::create("X", 40, true, "bigFont.fnt", "GJ_button_06.png", 30.f, 0.6f);
                banSpr->setScale(0.5f);
                auto banBtn = CCMenuItemSpriteExtra::create(banSpr, this, menu_selector(PaimonProfilePage::onBanUser));
                banBtn->setID("ban-user-button"_spr);

                // lo pongo por arriba a la derecha en mi sistema de coordenadas
                banBtn->setPosition({ -195.f, 75.f });

                extraMenu->addChild(banBtn);
                m_fields->m_banBtn = banBtn;
                // empiezo oculto hasta que la verificacion diga que soy mod/admin
                refreshBanButtonVisibility();
            }

            // si ya estaba verificado de antes, muestro controles ya
            try {
                bool wasVerified = Mod::get()->getSavedValue<bool>("is-verified-moderator", false);
                if (wasVerified) {
                    m_fields->m_isApprovedMod = true;
                    extraMenu->setVisible(true);
                    refreshBanButtonVisibility();
                } else if (ownProfile) {
                    // compruebo server si no tengo data local y es mi perfil
                    auto gm = GameManager::get();
                    if (gm && !gm->m_playerName.empty()) {
                        std::string username = gm->m_playerName;
                        // uso thumbnailapi pa chequear estado seguro
                        ThumbnailAPI::get().checkModerator(username, [this](bool isApproved, bool isAdmin) {
                            Loader::get()->queueInMainThread([this, isApproved, isAdmin]() {
                                if (isApproved) {
                                    m_fields->m_isApprovedMod = true;
                                    m_fields->m_isAdmin = isAdmin;
                                    
                                    // guardo estado aprobado
                                    Mod::get()->setSavedValue("is-verified-moderator", true);
                                    
                                    if (m_fields->m_extraMenu) {
                                        m_fields->m_extraMenu->setVisible(true);
                                    }
                                    refreshBanButtonVisibility();
                                    if (m_fields->m_extraMenu) {
                                        ButtonLayoutManager::get().applyLayoutToMenu("ProfilePage", m_fields->m_extraMenu);
                                    }
                                }
                            });
                        });
                    }
                }
            } catch (...) {
            }

            // boton de perfil (solo para mi perfil)
            if (ownProfile) {
                PaimonDebug::log("[ProfilePage] Cargando perfil propio - configurando botones");
                
                /* quitado: boton sync movido a leaderboardslayer
                auto addSpr = Assets::loadButtonSprite(
                    "profile-add",
                    "frame:GJ_plus2Btn_001.png",
                    [](){
                        auto s = CCSprite::createWithSpriteFrameName("GJ_plus2Btn_001.png");
                        if (!s) s = CCSprite::createWithSpriteFrameName("GJ_plusBtn_001.png");
                        if (!s) s = CCSprite::createWithSpriteFrameName("GJ_button_01.png");
                        return s;
                    }
                );
                addSpr->setScale(1.0f);
                auto addBtn = CCMenuItemSpriteExtra::create(addSpr, this, menu_selector(PaimonProfilePage::onAddProfileThumb));
                addBtn->setID("add-profile-thumb-button"_spr);

                // calculo defaults y persisto una vez
                ButtonLayout defAdd;
                defAdd.position = cocos2d::CCPoint(-195.f, 8.5f);
                defAdd.scale = 1.0f;
                defAdd.opacity = 1.0f;
                ButtonLayoutManager::get().setDefaultLayoutIfAbsent("ProfilePage", "add-profile-thumb-button", defAdd);

                // cargo y aplico layout guardado o default
                auto savedLayout = ButtonLayoutManager::get().getLayout("ProfilePage", "add-profile-thumb-button");
                if (savedLayout) {
                    addBtn->setPosition(savedLayout->position);
                    addBtn->setScale(savedLayout->scale);
                    addBtn->setOpacity(static_cast<GLubyte>(savedLayout->opacity * 255));
                } else {
                    addBtn->setPosition(defAdd.position);
                }

                extraMenu->addChild(addBtn);
                m_fields->m_addProfileBtn = addBtn;
                
 // animo la entrada del boton
                addBtn->setScale(0.f);
                addBtn->runAction(CCSequence::create(
                    CCDelayTime::create(0.05f),
                    CCEaseBounceOut::create(CCScaleTo::create(0.5f, savedLayout ? savedLayout->scale : 1.0f)),
                    nullptr
                ));
                */

                // boton verificar mod (solo en mi perfil)
                auto verifySpr = Assets::loadButtonSprite(
                    "verify-mod",
                    "frame:GJ_completesIcon_001.png",
                    [](){
                        auto s = CCSprite::createWithSpriteFrameName("GJ_completesIcon_001.png");
                        if (!s) s = CCSprite::createWithSpriteFrameName("GJ_starsIcon_001.png");
                        if (!s) s = CCSprite::createWithSpriteFrameName("GJ_checkOn_001.png");
                        return s;
                    }
                );
                
                // scale por tamaño
                float targetSize = 35.0f;
                float currentSize = std::max(verifySpr->getContentWidth(), verifySpr->getContentHeight());
                if (currentSize > 0) {
                    verifySpr->setScale(targetSize / currentSize);
                } else {
                    verifySpr->setScale(0.85f);
                }
                
                auto verifyBtn = CCMenuItemSpriteExtra::create(verifySpr, this, menu_selector(PaimonProfilePage::onVerifyModeratorStatus));
                verifyBtn->setID("verify-moderator-button"_spr);

                ButtonLayout defVerify;
                defVerify.position = cocos2d::CCPoint(-195.f, 45.5f);
                defVerify.scale = 0.85f;
                defVerify.opacity = 1.0f;
                ButtonLayoutManager::get().setDefaultLayoutIfAbsent("ProfilePage", "verify-moderator-button", defVerify);

                auto savedLayout = ButtonLayoutManager::get().getLayout("ProfilePage", "verify-moderator-button");
                if (savedLayout) {
                    verifyBtn->setPosition(savedLayout->position);
                    verifyBtn->setScale(savedLayout->scale);
                    verifyBtn->setOpacity(static_cast<GLubyte>(savedLayout->opacity * 255));
                } else {
                    verifyBtn->setPosition(defVerify.position);
                }
                
                extraMenu->addChild(verifyBtn);
                m_fields->m_verifyModBtn = verifyBtn;
                
                // animo la entrada del boton
                verifyBtn->setScale(0.f);
                verifyBtn->runAction(CCSequence::create(
                    CCDelayTime::create(0.15f),
                    CCEaseBounceOut::create(CCScaleTo::create(0.5f, savedLayout ? savedLayout->scale : 0.85f)),
                    nullptr
                ));

                // estado mod guardado
                bool wasPreviouslyVerified = false;
                try {
                    wasPreviouslyVerified = Mod::get()->getSavedValue<bool>("is-verified-moderator", false);
                } catch(...) {
                    log::warn("[ProfilePage] No se pudo cargar estado de verificacion previo");
                }
                
                if (wasPreviouslyVerified) {
                    log::info("[ProfilePage] Usuario previamente verificado - restaurando boton gear");
                    m_fields->m_isApprovedMod = true;
                    
                    // btn gear si no hay
                    if (!m_fields->m_gearBtn) {
                        auto gearSpr = Assets::loadButtonSprite(
                            "profile-gear",
                            "frame:GJ_optionsBtn02_001.png",
                            [](){
                                auto s = CCSprite::createWithSpriteFrameName("GJ_optionsBtn02_001.png");
                                if (!s) s = CCSprite::createWithSpriteFrameName("GJ_optionsBtn_001.png");
                                if (!s) s = CCSprite::create();
                                return s;
                            }
                        );
                        
                        // scale por tamaño
                        float gearTargetSz = 35.0f;
                        float gearCurrentSz = std::max(gearSpr->getContentWidth(), gearSpr->getContentHeight());
                        if (gearCurrentSz > 0) {
                            gearSpr->setScale(gearTargetSz / gearCurrentSz);
                        } else {
                            gearSpr->setScale(1.0f);
                        }
                        
                        auto gearBtn = CCMenuItemSpriteExtra::create(gearSpr, this, menu_selector(PaimonProfilePage::onOpenThumbsCenter));
                        gearBtn->setID("thumbs-gear-button"_spr);

                        ButtonLayout defGear;
                        defGear.position = cocos2d::CCPoint(-195.f, -28.5f);
                        defGear.scale = 1.0f;
                        defGear.opacity = 1.0f;
                        ButtonLayoutManager::get().setDefaultLayoutIfAbsent("ProfilePage", "thumbs-gear-button"_spr, defGear);

                        savedLayout = ButtonLayoutManager::get().getLayout("ProfilePage", "thumbs-gear-button"_spr);
                        float finalScale = savedLayout ? savedLayout->scale : defGear.scale;
                        if (savedLayout) {
                            gearBtn->setPosition(savedLayout->position);
                            gearBtn->setScale(savedLayout->scale);
                            gearBtn->setOpacity(static_cast<GLubyte>(savedLayout->opacity * 255));
                        } else {
                            gearBtn->setPosition(defGear.position);
                        }
                        
                        // importante: actualizar m_basescale para que el hover no resetee la escala
                        gearBtn->m_baseScale = finalScale;

                        // animo la entrada del boton (mismo efecto q profilepage original)
                        gearBtn->setScale(0.f);
                        gearBtn->runAction(CCSequence::create(
                            CCDelayTime::create(0.1f),
                            CCEaseBounceOut::create(CCScaleTo::create(0.5f, finalScale)),
                            nullptr
                        ));
                        
                        extraMenu->addChild(gearBtn);
                        m_fields->m_gearBtn = gearBtn;
                        log::info("[ProfilePage] Boton gear restaurado exitosamente");
                    }
                } else {
                    log::info("[ProfilePage] Boton de verificacion anadido - esperando accion del usuario");
                }

                // anado boton toggle edit mode si el setting esta activado
                bool showEditor = Mod::get()->getSettingValue<bool>("show-button-editor");
                if (showEditor) {
                    // mismo patron q otros botones: _spr primero, luego assets override, luego sprite frames
                    CCSprite* editSpr = nullptr;
                    if (!editSpr) editSpr = CCSprite::create("paim_botonMove.png"_spr);
                    if (!editSpr) {
                        editSpr = Assets::loadButtonSprite(
                            "botonMove",
                            "",
                            [](){
                                // usamos _spr para manejar mejor los recursos
                                CCSprite* s = CCSprite::create("paim_botonMove.png"_spr);
                                if (!s) s = CCSprite::createWithSpriteFrameName("edit_eTabBtn_001.png");
                                if (!s) s = CCSprite::createWithSpriteFrameName("GJ_editBtn_001.png");
                                if (!s) s = CCSprite::createWithSpriteFrameName("GJ_button_04.png");
                                return s;
                            }
                        );
                    }
                    
                    // arreglo: escalar segun el tamanio
                    float editTargetSz = 30.0f;
                    float editCurrentSz = std::max(editSpr->getContentWidth(), editSpr->getContentHeight());
                    if (editCurrentSz > 0) {
                        editSpr->setScale(editTargetSz / editCurrentSz);
                    } else {
                        editSpr->setScale(0.3f);
                    }
                    
                    auto editBtn = CCMenuItemSpriteExtra::create(editSpr, this, menu_selector(PaimonProfilePage::onToggleEditMode));
                    editBtn->setID("button-edit-toggle"_spr);

                    ButtonLayout defEdit;
                    defEdit.position = cocos2d::CCPoint(-195.f, -55.5f);
                    defEdit.scale = 0.3f;
                    defEdit.opacity = 1.0f;
                    ButtonLayoutManager::get().setDefaultLayoutIfAbsent("ProfilePage", "button-edit-toggle", defEdit);

                    // migramos el default viejo de 0.7 al nuevo
                    if (auto oldDef = ButtonLayoutManager::get().getDefaultLayout("ProfilePage", "button-edit-toggle")) {
                        if (oldDef->scale > 0.31f) {
                            ButtonLayoutManager::get().setDefaultLayout("ProfilePage", "button-edit-toggle", defEdit);
                        }
                    }

                    savedLayout = ButtonLayoutManager::get().getLayout("ProfilePage", "button-edit-toggle");
                    float editFinalScale = savedLayout ? savedLayout->scale : defEdit.scale;
                    if (savedLayout) {
                        editBtn->setPosition(savedLayout->position);
                        editBtn->setScale(savedLayout->scale);
                        editBtn->setOpacity(static_cast<GLubyte>(savedLayout->opacity * 255));
                    } else {
                        editBtn->setPosition(defEdit.position);
                        editBtn->setScale(defEdit.scale);
                    }

                    // importante: actualizar m_basescale para que el hover no resetee la escala
                    editBtn->m_baseScale = editFinalScale;

                    extraMenu->addChild(editBtn);
                    m_fields->m_editModeBtn = editBtn;
                    
                    // animar entrada del boton
                    editBtn->setScale(0.f);
                    editBtn->runAction(CCSequence::create(
                        CCDelayTime::create(0.2f),
                        CCEaseBounceOut::create(CCScaleTo::create(0.5f, savedLayout ? savedLayout->scale : 0.3f)),
                        nullptr
                    ));
                }

                // === BOTÓN DE MÚSICA (solo en perfil propio) ===
                {
                    auto musicSpr = CCSprite::createWithSpriteFrameName("GJ_audioBtn_001.png");
                    if (!musicSpr) musicSpr = CCSprite::createWithSpriteFrameName("GJ_musicOnBtn_001.png");
                    if (!musicSpr) musicSpr = CCSprite::create();

                    float musicTargetSz = 30.0f;
                    float musicCurrentSz = std::max(musicSpr->getContentWidth(), musicSpr->getContentHeight());
                    if (musicCurrentSz > 0) {
                        musicSpr->setScale(musicTargetSz / musicCurrentSz);
                    }

                    auto musicBtn = CCMenuItemSpriteExtra::create(musicSpr, this, menu_selector(PaimonProfilePage::onConfigureProfileMusic));
                    musicBtn->setID("profile-music-button"_spr);

                    ButtonLayout defMusic;
                    defMusic.position = cocos2d::CCPoint(-195.f, -82.5f);
                    defMusic.scale = 0.8f;
                    defMusic.opacity = 1.0f;
                    ButtonLayoutManager::get().setDefaultLayoutIfAbsent("ProfilePage", "profile-music-button", defMusic);

                    auto musicLayout = ButtonLayoutManager::get().getLayout("ProfilePage", "profile-music-button");
                    float musicFinalScale = musicLayout ? musicLayout->scale : defMusic.scale;
                    if (musicLayout) {
                        musicBtn->setPosition(musicLayout->position);
                        musicBtn->setScale(musicLayout->scale);
                        musicBtn->setOpacity(static_cast<GLubyte>(musicLayout->opacity * 255));
                    } else {
                        musicBtn->setPosition(defMusic.position);
                        musicBtn->setScale(defMusic.scale);
                    }

                    musicBtn->m_baseScale = musicFinalScale;
                    extraMenu->addChild(musicBtn);
                    m_fields->m_musicBtn = musicBtn;

                    // Animar entrada
                    musicBtn->setScale(0.f);
                    musicBtn->runAction(CCSequence::create(
                        CCDelayTime::create(0.25f),
                        CCEaseBounceOut::create(CCScaleTo::create(0.5f, musicFinalScale)),
                        nullptr
                    ));
                }

                // === BOTÓN DE IMAGEN DE PERFIL (solo en perfil propio) ===
                {
                    auto imgSpr = CCSprite::createWithSpriteFrameName("GJ_duplicateBtn_001.png");
                    if (!imgSpr) imgSpr = CCSprite::createWithSpriteFrameName("GJ_editBtn_001.png");
                    if (!imgSpr) imgSpr = CCSprite::create();

                    float imgTargetSz = 30.0f;
                    float imgCurrentSz = std::max(imgSpr->getContentWidth(), imgSpr->getContentHeight());
                    if (imgCurrentSz > 0) {
                        imgSpr->setScale(imgTargetSz / imgCurrentSz);
                    }

                    auto imgBtn = CCMenuItemSpriteExtra::create(imgSpr, this, menu_selector(PaimonProfilePage::onAddProfileImg));
                    imgBtn->setID("add-profileimg-button"_spr);

                    ButtonLayout defImg;
                    defImg.position = cocos2d::CCPoint(-175.f, -109.5f);
                    defImg.scale = 0.8f;
                    defImg.opacity = 1.0f;
                    ButtonLayoutManager::get().setDefaultLayoutIfAbsent("ProfilePage", "add-profileimg-button", defImg);

                    auto imgLayout = ButtonLayoutManager::get().getLayout("ProfilePage", "add-profileimg-button");
                    float imgFinalScale = imgLayout ? imgLayout->scale : defImg.scale;
                    if (imgLayout) {
                        imgBtn->setPosition(imgLayout->position);
                        imgBtn->setScale(imgLayout->scale);
                        imgBtn->setOpacity(static_cast<GLubyte>(imgLayout->opacity * 255));
                    } else {
                        imgBtn->setPosition(defImg.position);
                        imgBtn->setScale(defImg.scale);
                    }

                    imgBtn->m_baseScale = imgFinalScale;
                    extraMenu->addChild(imgBtn);
                    m_fields->m_addProfileImgBtn = imgBtn;

                    // Animar entrada
                    imgBtn->setScale(0.f);
                    imgBtn->runAction(CCSequence::create(
                        CCDelayTime::create(0.30f),
                        CCEaseBounceOut::create(CCScaleTo::create(0.5f, imgFinalScale)),
                        nullptr
                    ));
                }
            }

            // === BOTÓN DE PAUSA (visible en todos los perfiles) ===
            // Crear menú separado para el botón de pausa que siempre es visible
            auto pauseMenu = CCMenu::create();
            pauseMenu->setID("paimon-profile-pause-menu"_spr);
            pauseMenu->setPosition({ cs.width / 2.f, cs.height / 2.f });
            this->addChild(pauseMenu, 21);
            m_fields->m_pauseMenu = pauseMenu;

            {
                auto pauseSpr = CCSprite::createWithSpriteFrameName("GJ_pauseBtn_001.png");
                if (!pauseSpr) pauseSpr = CCSprite::createWithSpriteFrameName("GJ_stopMusicBtn_001.png");
                if (!pauseSpr) pauseSpr = CCSprite::create();

                float targetSize = 25.0f;
                float currentSize = std::max(pauseSpr->getContentWidth(), pauseSpr->getContentHeight());
                if (currentSize > 0) {
                    pauseSpr->setScale(targetSize / currentSize);
                }

                auto pauseBtn = CCMenuItemSpriteExtra::create(pauseSpr, this, menu_selector(PaimonProfilePage::onToggleProfileMusic));
                pauseBtn->setID("profile-music-pause-button"_spr);

                ButtonLayout defPause;
                defPause.position = cocos2d::CCPoint(195.f, -100.f);  // Lado derecho
                defPause.scale = 0.7f;
                defPause.opacity = 1.0f;
                ButtonLayoutManager::get().setDefaultLayoutIfAbsent("ProfilePage", "profile-music-pause-button", defPause);

                auto pauseLayout = ButtonLayoutManager::get().getLayout("ProfilePage", "profile-music-pause-button");
                float pauseFinalScale = pauseLayout ? pauseLayout->scale : defPause.scale;
                if (pauseLayout) {
                    pauseBtn->setPosition(pauseLayout->position);
                    pauseBtn->setScale(pauseLayout->scale);
                    pauseBtn->setOpacity(static_cast<GLubyte>(pauseLayout->opacity * 255));
                } else {
                    pauseBtn->setPosition(defPause.position);
                    pauseBtn->setScale(defPause.scale);
                }

                pauseBtn->m_baseScale = pauseFinalScale;
                pauseBtn->setVisible(false); // Oculto hasta que se verifique que hay música
                pauseMenu->addChild(pauseBtn);
                m_fields->m_musicPauseBtn = pauseBtn;
            }

            // Verificar si este perfil tiene música y reproducirla
            checkAndPlayProfileMusic(accountID);

            // Cargar imagen de perfil (visible para todos)
            addOrUpdateProfileImgOnPage(accountID, ownProfile);

            // nada de layout automatico, las posiciones son fijas

            // pedir assets del server si hay
            if (m_fields->m_extraMenu) {
                ButtonLayoutManager::get().applyLayoutToMenu("ProfilePage", m_fields->m_extraMenu);
            }
            try {
                auto gm = GameManager::get();
                // evitar conversion ambigua en android
                std::string username;
                if (ownProfile && gm) {
                    // convertir a std::string expliciatmente
                    username = gm->m_playerName.c_str();
                }
                if (!username.empty()) {
                    log::info("Profile assets download disabled for {}", username);
                    // descarga del server desactivada
                } else {
                    log::info("no hay username, pasamos de descarga remota");
                }
            } catch (...) {
                log::warn("fallo pidiendo assets");
            }
        } catch (...) {}
        return true;
    }

    void onOpenAddModerator(CCObject*) {
        if (auto* popup = AddModeratorPopup::create(nullptr)) popup->show();
    }

    void onVerifyModeratorStatus(CCObject*) {
        log::info("[ProfilePage] Botón de verificación manual presionado");
        
        // get player username from gamemanager
        auto gm = GameManager::get();
        if (!gm) {
            log::error("[ProfilePage] No se pudo obtener GameManager");
            return;
        }
        
        std::string username = gm->m_playerName;
        if (username.empty()) {
            log::error("[ProfilePage] Username vacío - no se puede verificar");
            FLAlertLayer::create(Localization::get().getString("general.error").c_str(), Localization::get().getString("profile.username_error").c_str(), Localization::get().getString("general.ok").c_str())->show();
            return;
        }

        log::info("[ProfilePage] Iniciando verificación manual para usuario: {}", username);
        
        // indicator de carga
        auto loadingCircle = LoadingCircle::create();
        loadingCircle->setParentLayer(this);
        loadingCircle->setFade(true);
        loadingCircle->show();

        // checkeo con thumbnailapi
        ThumbnailAPI::get().checkModerator(username, [this, loadingCircle, username](bool isApproved, bool isAdmin) {
            log::info("[ProfilePage] Verificación manual completada para '{}': isApproved={}, isAdmin={}", username, isApproved, isAdmin);
            Loader::get()->queueInMainThread([this, loadingCircle, isApproved, isAdmin]() {
                loadingCircle->fadeAndRemove();
                
                m_fields->m_isApprovedMod = isApproved;
                m_fields->m_isAdmin = isAdmin;
                refreshBanButtonVisibility();

                // si es admin, anado boton de anadir mod
                if (isAdmin && m_fields->m_extraMenu) {
                    if (!m_fields->m_extraMenu->getChildByID("add-moderator-button")) {
                        auto addModSpr = Assets::loadButtonSprite(
                            "add-moderator",
                            "frame:GJ_plus2Btn_001.png",
                            [](){
                                auto s = CCSprite::createWithSpriteFrameName("GJ_plus2Btn_001.png");
                                if (!s) s = CCSprite::createWithSpriteFrameName("GJ_plusBtn_001.png");
                                if (!s) s = CCSprite::createWithSpriteFrameName("GJ_button_01.png");
                                return s;
                            }
                        );
                        addModSpr->setScale(0.85f);
                        auto addModBtn = CCMenuItemSpriteExtra::create(addModSpr, this, menu_selector(PaimonProfilePage::onOpenAddModerator));
                        addModBtn->setID("add-moderator-button"_spr);
                        // default position un poco mas arriba del verify
                        addModBtn->setPosition({ -195.f, 72.5f });
                        m_fields->m_extraMenu->addChild(addModBtn);
                        addModBtn->setScale(0.f);
                        auto layout = ButtonLayoutManager::get().getLayout("ProfilePage", "add-moderator-button"_spr);
                        float targetScale = layout ? layout->scale : 0.85f;
                        addModBtn->runAction(CCEaseBounceOut::create(CCScaleTo::create(0.5f, targetScale)));
                    }
                }
                
                if (m_fields->m_extraMenu) {
                    ButtonLayoutManager::get().applyLayoutToMenu("ProfilePage", m_fields->m_extraMenu);
                }

                if (isApproved) {
                    log::info("[ProfilePage] Usuario aprobado - guardando estado y mostrando botón gear");
                    
                    // salvo estado de mod
                    Mod::get()->setSavedValue("is-verified-moderator", true);
                
                    // nuevo: salvo archivo de verificacion con timestamp
                    try {
                        auto modDataPath = Mod::get()->getSaveDir() / "moderator_verification.dat";
                        std::ofstream modFile(modDataPath, std::ios::binary);
                        if (modFile) {
                            // escribo timestamp actual
                            auto now = std::chrono::system_clock::now();
                            auto timestamp = std::chrono::system_clock::to_time_t(now);
                            modFile.write(reinterpret_cast<const char*>(&timestamp), sizeof(timestamp));
                            modFile.close();
                            log::info("[ProfilePage] Archivo de verificación de moderador guardado: {}", modDataPath.generic_string());
                        } else {
                            log::error("[ProfilePage] No se pudo crear archivo de verificación");
                        }
                    } catch (const std::exception& e) {
                        log::error("[ProfilePage] Error al guardar archivo de verificación: {}", e.what());
                    }
                    
                    // anado boton gear para centro de thumbs
                    if (m_fields->m_extraMenu && !m_fields->m_gearBtn) {
                        log::info("[ProfilePage] Añadiendo botón gear al menú");
                        auto gearSpr = Assets::loadButtonSprite(
                            "profile-gear",
                            "frame:GJ_optionsBtn02_001.png",
                            [](){
                                auto s = CCSprite::createWithSpriteFrameName("GJ_optionsBtn02_001.png");
                                if (!s) s = CCSprite::createWithSpriteFrameName("GJ_optionsBtn_001.png");
                                if (!s) s = CCSprite::createWithSpriteFrameName("GJ_button_01.png");
                                return s;
                            }
                        );
                        gearSpr->setScale(0.9f);
                        auto gearBtn = CCMenuItemSpriteExtra::create(gearSpr, this, menu_selector(PaimonProfilePage::onOpenThumbsCenter));
                        gearBtn->setID("thumbs-gear-button"_spr);
                        gearBtn->setPosition({ -195.f, -18.5f });
                        m_fields->m_extraMenu->addChild(gearBtn);
                        m_fields->m_gearBtn = gearBtn;
                    } else {
                        log::info("[ProfilePage] Botón gear ya existe o menú no disponible");
                    }
                    
                    FLAlertLayer::create(
                        Localization::get().getString("profile.verified").c_str(),
                        Localization::get().getString("profile.verified_msg").c_str(),
                        Localization::get().getString("general.ok").c_str()
                    )->show();
                } else {
                    log::warn("[ProfilePage] Usuario NO aprobado como moderador");
                    FLAlertLayer::create(
                        Localization::get().getString("profile.not_verified").c_str(),
                        Localization::get().getString("profile.not_verified_msg").c_str(),
                        Localization::get().getString("general.ok").c_str()
                    )->show();
                }
            });
        });
    }

    void onOpenThumbsCenter(CCObject*) {
        // aseguro que el user es mod antes de abrir panel
        if (!m_fields->m_isApprovedMod) {
            log::warn("[ProfilePage] Usuario NO es moderador, bloqueando acceso al centro de verificación");
            FLAlertLayer::create(
                Localization::get().getString("profile.access_denied").c_str(),
                Localization::get().getString("profile.moderators_only").c_str(),
                Localization::get().getString("general.ok").c_str()
            )->show();
            return;
        }
        
        // abro centro de verificacion con categorias
        log::info("[ProfilePage] Abriendo centro de verificación para moderador");
        if (auto p = VerificationQueuePopup::create()) p->show();
    }

    void onToggleEditMode(CCObject*) {
        if (!m_fields->m_extraMenu) return;

        PaimonButtonHighlighter::highlightAll();

        // Recoger menus extra para incluir en el editor
        std::vector<cocos2d::CCMenu*> extraMenus;
        if (m_fields->m_pauseMenu) {
            extraMenus.push_back(m_fields->m_pauseMenu);
        }

        auto overlay = ButtonEditOverlay::create("ProfilePage", m_fields->m_extraMenu, extraMenus);
        if (auto scene = CCDirector::sharedDirector()->getRunningScene()) {
            scene->addChild(overlay, 1000);
        }
    }

    void onAddProfileThumb(CCObject*) {
        this->setTouchEnabled(false);
        auto result = pt::openImageFileDialog();
        this->setTouchEnabled(true);

        if (result.has_value()) {
            auto path = result.value();
            if (!path.empty()) {
                 this->processProfileImage(path);
            } else {
                 Notification::create(Localization::get().getString("profile.no_image_selected").c_str(), NotificationIcon::Warning)->show();
            }
        }
    }

    void onAddProfileImg(CCObject*) {
        this->setTouchEnabled(false);
        auto result = pt::openImageFileDialog();
        this->setTouchEnabled(true);

        if (result.has_value()) {
            auto path = result.value();
            if (!path.empty()) {
                this->processProfileImg(path);
            } else {
                Notification::create(Localization::get().getString("profile.no_image_selected").c_str(), NotificationIcon::Warning)->show();
            }
        }
    }

    void processProfileImg(std::filesystem::path path) {
        if (ImageLoadHelper::isGIF(path)) {
            // Solo VIP, Moderadores y Admins pueden subir GIFs de perfil
            bool isVip = Mod::get()->getSavedValue<bool>("is-verified-vip", false);
            bool isMod = Mod::get()->getSavedValue<bool>("is-verified-moderator", false);
            bool isAdmin = Mod::get()->getSavedValue<bool>("is-verified-admin", false);
            if (!isVip && !isMod && !isAdmin) {
                Notification::create("Profile GIFs are a VIP feature", NotificationIcon::Warning)->show();
                return;
            }

            // GIF: subir directamente
            auto imgData = ImageLoadHelper::readBinaryFile(path, 10);
            if (imgData.empty()) {
                Notification::create(Localization::get().getString("profile.image_open_error").c_str(), NotificationIcon::Error)->show();
                return;
            }

            int accountID = this->m_accountID;
            std::string username = GJAccountManager::get()->m_username;

            auto loading = LoadingCircle::create();
            loading->setParentLayer(this);
            loading->setFade(true);
            loading->show();

            this->retain();

            ThumbnailAPI::get().uploadProfileImgGIF(accountID, imgData, username, [this, accountID, imgData, loading](bool success, const std::string& msg) {
                if (loading) loading->fadeAndRemove();

                if (success) {
                    Notification::create("Profile image uploaded!", NotificationIcon::Success)->show();

                    saveProfileImgToDisk(accountID, imgData);

                    CCImage img;
                    if (img.initWithImageData(const_cast<uint8_t*>(imgData.data()), imgData.size())) {
                        auto tex = new CCTexture2D();
                        if (tex->initWithImage(&img)) {
                            tex->autorelease();
                            tex->retain(); // retain manual para cache raw
                            auto prev = s_profileImgCache.find(accountID);
                            if (prev != s_profileImgCache.end() && prev->second) prev->second->release();
                            s_profileImgCache[accountID] = tex;
                            this->displayProfileImg(accountID, tex);
                        } else {
                            tex->release();
                        }
                    }
                } else {
                    Notification::create("Upload failed: " + msg, NotificationIcon::Error)->show();
                }

                this->release();
            });
            return;
        }

        // Imagen estática: usar helper pa cargar y convertir
        auto loaded = ImageLoadHelper::loadStaticImage(path, 10);
        if (!loaded.success) {
            std::string errKey = loaded.error;
            // si el error es una key de localizacion, traducir
            if (errKey == "image_open_error" || errKey == "invalid_image_data" || errKey == "texture_error") {
                Notification::create(Localization::get().getString("profile." + errKey).c_str(), NotificationIcon::Error)->show();
            } else {
                Notification::create(errKey.c_str(), NotificationIcon::Error)->show();
            }
            return;
        }

        // retain textura pa que sobreviva al popup
        loaded.texture->retain();

        int accountID = this->m_accountID;

        auto popup = CapturePreviewPopup::create(
            loaded.texture,
            accountID,
            loaded.buffer,
            loaded.width,
            loaded.height,
            [this, accountID](bool ok, int id, std::shared_ptr<uint8_t> buf, int w, int h, std::string mode, std::string replaceId) {
                if (ok && buf) {
                    // convertir buffer RGBA a PNG para subir
                    CCImage pngImg;
                    pngImg.initWithImageData(buf.get(), w * h * 4, CCImage::kFmtRawData, w, h);

                    auto tempPath = Mod::get()->getSaveDir() / ("profileimg_temp_" + std::to_string(accountID) + ".png");
                    pngImg.saveToFile(geode::utils::string::pathToString(tempPath).c_str(), false);

                    std::ifstream pngFile(tempPath, std::ios::binary);
                    if (pngFile) {
                        std::vector<uint8_t> pngData((std::istreambuf_iterator<char>(pngFile)), std::istreambuf_iterator<char>());
                        pngFile.close();

                        std::string username = GJAccountManager::get()->m_username;

                        auto loading = LoadingCircle::create();
                        loading->setParentLayer(this);
                        loading->setFade(true);
                        loading->show();

                        this->retain();

                        ThumbnailAPI::get().uploadProfileImg(accountID, pngData, username, "image/png", [this, accountID, pngData, loading, buf, w, h](bool success, const std::string& msg) {
                            if (loading) loading->fadeAndRemove();

                            if (success) {
                                bool isPending = (msg.find("pending") != std::string::npos || msg.find("verification") != std::string::npos);

                                if (isPending) {
                                    Notification::create("Image submitted! Pending moderator verification.", NotificationIcon::Warning)->show();
                                } else {
                                    Notification::create("Profile image uploaded!", NotificationIcon::Success)->show();
                                }

                                saveProfileImgToDisk(accountID, pngData);

                                CCImage finalImg;
                                if (finalImg.initWithImageData(buf.get(), w * h * 4, CCImage::kFmtRawData, w, h)) {
                                    auto finalTex = new CCTexture2D();
                                    if (finalTex->initWithImage(&finalImg)) {
                                        finalTex->autorelease();
                                        finalTex->retain(); // retain manual para cache raw
                                        auto prev = s_profileImgCache.find(accountID);
                                        if (prev != s_profileImgCache.end() && prev->second) prev->second->release();
                                        s_profileImgCache[accountID] = finalTex;
                                        this->displayProfileImg(accountID, finalTex);
                                    } else {
                                        finalTex->release();
                                    }
                                }
                            } else {
                                Notification::create("Upload failed: " + msg, NotificationIcon::Error)->show();
                            }

                            this->release();
                        });
                    }

                    try { std::filesystem::remove(tempPath); } catch (...) {}
                }
            }
        );
        if (popup) popup->show();
    }

    void processProfileImage(std::filesystem::path path) {
        if (ImageLoadHelper::isGIF(path)) {
            // Solo VIP, Moderadores y Admins pueden subir GIFs de perfil
            bool isVip = Mod::get()->getSavedValue<bool>("is-verified-vip", false);
            bool isMod = Mod::get()->getSavedValue<bool>("is-verified-moderator", false);
            bool isAdmin = Mod::get()->getSavedValue<bool>("is-verified-admin", false);
            if (!isVip && !isMod && !isAdmin) {
                Notification::create("Profile GIFs are a VIP feature", NotificationIcon::Warning)->show();
                return;
            }

            auto gifData = ImageLoadHelper::readBinaryFile(path, 10);
            if (gifData.empty()) {
                Notification::create(Localization::get().getString("profile.image_open_error").c_str(), NotificationIcon::Error)->show();
                return;
            }

            int accountID = this->m_accountID;
            std::string username = GJAccountManager::get()->m_username;

            auto loading = LoadingCircle::create();
            loading->setParentLayer(this);
            loading->setFade(true);
            loading->show();

            this->retain();

            ThumbnailAPI::get().uploadProfileGIF(accountID, gifData, username, [this, accountID, gifData, loading](bool success, const std::string& msg) {
                if (loading) loading->fadeAndRemove();
                
                if (success) {
                    bool isPending = (msg.find("pending") != std::string::npos || msg.find("verification") != std::string::npos);

                    std::string gifKey = fmt::format("profile_{}", accountID);
                    ProfileThumbs::get().cacheProfileGIF(accountID, gifKey, {255,255,255}, {255,255,255}, 0.6f);
                    
                    CCImage img;
                    if (img.initWithImageData(const_cast<uint8_t*>(gifData.data()), gifData.size())) {
                        auto tex = new CCTexture2D();
                        if (tex->initWithImage(&img)) {
                            tex->autorelease();
                            auto entry = ProfileThumbs::get().getCachedProfile(accountID);
                            if (entry) {
                                if (entry->texture) entry->texture->release();
                                entry->texture = tex;
                                entry->texture->retain();
                            }
                        }
                    }

                    if (isPending) {
                        Notification::create("GIF submitted! Pending moderator verification.", NotificationIcon::Warning)->show();
                    } else {
                        Notification::create(Localization::get().getString("profile.saved").c_str(), NotificationIcon::Success)->show();
                    }
                    this->addOrUpdateProfileThumbOnPage(accountID);
                } else {
                    Notification::create("Upload failed: " + msg, NotificationIcon::Error)->show();
                }
                
                this->release();
            });
            return;
        }

        // imagen estática: usar helper pa cargar
        auto loaded = ImageLoadHelper::loadStaticImage(path, 7);
        if (!loaded.success) {
            std::string errKey = loaded.error;
            if (errKey == "image_open_error" || errKey == "invalid_image_data" || errKey == "texture_error") {
                Notification::create(Localization::get().getString("profile." + errKey).c_str(), NotificationIcon::Error)->show();
            } else {
                Notification::create(errKey.c_str(), NotificationIcon::Error)->show();
            }
            return;
        }

        loaded.texture->retain();

        int accountID = this->m_accountID;

        auto popup = CapturePreviewPopup::create(
            loaded.texture,
            accountID,
            loaded.buffer,
            loaded.width,
            loaded.height,
            [this, accountID](bool ok, int id, std::shared_ptr<uint8_t> buf, int w, int h, std::string mode, std::string replaceId){
                if (ok) {
                    std::vector<uint8_t> rgb(w * h * 3);
                    if (buf) {
                        for(int i = 0; i < w * h; i++) {
                            rgb[i*3 + 0] = buf.get()[i*4 + 0];
                            rgb[i*3 + 1] = buf.get()[i*4 + 1];
                            rgb[i*3 + 2] = buf.get()[i*4 + 2];
                        }
                    }
                    
                    ProfileThumbs::get().saveRGB(accountID, rgb.data(), w, h);
                    ThumbsRegistry::get().mark(ThumbKind::Profile, accountID, false);
                    Mod::get()->setSavedValue("my-account-id", accountID);
                    Notification::create(Localization::get().getString("profile.saved").c_str(), NotificationIcon::Success)->show();
                    
                    this->addOrUpdateProfileThumbOnPage(accountID);
                }
            }
        );
        if (popup) popup->show();
    }

    // === FUNCIONES DE MÚSICA DE PERFIL ===

    void onConfigureProfileMusic(CCObject*) {
        // Solo disponible en tu propio perfil
        if (!this->m_ownProfile) {
            Notification::create("You can only configure music on your own profile", NotificationIcon::Warning)->show();
            return;
        }

        // Solo VIP, Moderadores y Admins pueden configurar música
        bool isVip = Mod::get()->getSavedValue<bool>("is-verified-vip", false);
        bool isMod = Mod::get()->getSavedValue<bool>("is-verified-moderator", false);
        bool isAdmin = Mod::get()->getSavedValue<bool>("is-verified-admin", false);
        if (!isVip && !isMod && !isAdmin) {
            Notification::create("Profile music is a VIP feature", NotificationIcon::Warning)->show();
            return;
        }

        // Abrir popup de configuración de música
        if (auto popup = ProfileMusicPopup::create(this->m_accountID)) {
            popup->show();
        }
    }

    void onToggleProfileMusic(CCObject*) {
        auto& musicManager = ProfileMusicManager::get();

        if (musicManager.isPlaying()) {
            if (musicManager.isPaused()) {
                musicManager.resumeProfileMusic();
                m_fields->m_musicPlaying = true;
                updatePauseButtonSprite(true);
            } else {
                musicManager.pauseProfileMusic();
                m_fields->m_musicPlaying = false;
                updatePauseButtonSprite(false);
            }
        } else {
            // Si no está sonando, intentar reproducir
            musicManager.playProfileMusic(this->m_accountID);
            m_fields->m_musicPlaying = true;
            updatePauseButtonSprite(true);
        }
    }

    void updatePauseButtonSprite(bool isPlaying) {
        if (!m_fields->m_musicPauseBtn) return;

        // Cambiar el sprite según el estado
        CCSprite* newSpr = nullptr;
        if (isPlaying) {
            newSpr = CCSprite::createWithSpriteFrameName("GJ_pauseBtn_001.png");
            if (!newSpr) newSpr = CCSprite::createWithSpriteFrameName("GJ_stopMusicBtn_001.png");
        } else {
            newSpr = CCSprite::createWithSpriteFrameName("GJ_playBtn_001.png");
            if (!newSpr) newSpr = CCSprite::createWithSpriteFrameName("GJ_playMusicBtn_001.png");
        }

        if (newSpr) {
            float targetSize = 25.0f;
            float currentSize = std::max(newSpr->getContentWidth(), newSpr->getContentHeight());
            if (currentSize > 0) {
                newSpr->setScale(targetSize / currentSize);
            }
            m_fields->m_musicPauseBtn->setSprite(newSpr);
        }
    }

    void checkAndPlayProfileMusic(int accountID) {
        // Verificar si la música de perfiles está habilitada
        if (!ProfileMusicManager::get().isEnabled()) {
            return;
        }

        // Obtener configuración de música del perfil
        ProfileMusicManager::get().getProfileMusicConfig(accountID, [this, accountID](bool success, const ProfileMusicManager::ProfileMusicConfig& config) {
            Loader::get()->queueInMainThread([this, success, config, accountID]() {
                if (!success || config.songID <= 0 || !config.enabled) {
                    // No hay música configurada o está deshabilitada
                    if (m_fields->m_musicPauseBtn) {
                        m_fields->m_musicPauseBtn->setVisible(false);
                    }
                    return;
                }

                // Hay música, mostrar botón de pausa
                if (m_fields->m_musicPauseBtn) {
                    m_fields->m_musicPauseBtn->setVisible(true);

                    // Animar entrada del botón
                    m_fields->m_musicPauseBtn->setScale(0.f);
                    auto pauseLayout = ButtonLayoutManager::get().getLayout("ProfilePage", "profile-music-pause-button");
                    float finalScale = pauseLayout ? pauseLayout->scale : 0.7f;
                    m_fields->m_musicPauseBtn->runAction(CCSequence::create(
                        CCDelayTime::create(0.3f),
                        CCEaseBounceOut::create(CCScaleTo::create(0.5f, finalScale)),
                        nullptr
                    ));
                }

                // Reproducir música automáticamente
                ProfileMusicManager::get().playProfileMusic(accountID);
                m_fields->m_musicPlaying = true;
                updatePauseButtonSprite(true);
            });
        });
    }

    void keyBackClicked() override {
        // Iniciar fade-out suave al presionar back
        ProfileMusicManager::get().stopProfileMusic();
        ProfilePage::keyBackClicked();
    }

    void onClose(CCObject* sender) {
        this->unschedule(schedule_selector(PaimonProfilePage::tickStyleBgs));
        ProfileMusicManager::get().stopProfileMusic();
        ProfilePage::onClose(sender);
    }

    void onExit() override {
        this->unschedule(schedule_selector(PaimonProfilePage::tickStyleBgs));
        if (!ProfileMusicManager::get().isFadingOut()) {
            ProfileMusicManager::get().stopProfileMusic();
        }
        ProfilePage::onExit();
    }
};

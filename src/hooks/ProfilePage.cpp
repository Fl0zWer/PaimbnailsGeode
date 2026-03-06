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
#include <cmath>
#include <thread>
#include <fstream>
#include "../utils/FileDialog.hpp"
#include "../managers/ProfileThumbs.hpp"
#include "../managers/ThumbsRegistry.hpp"
#include "../managers/ThumbnailAPI.hpp"
#include "../layers/CapturePreviewPopup.hpp"
#include "../layers/VerificationCenterLayer.hpp"
#include "../layers/AddModeratorPopup.hpp"
#include "../layers/BanUserPopup.hpp"
#include "../utils/Assets.hpp"
#include "../utils/PaimonButtonHighlighter.hpp"
#include "../utils/HttpClient.hpp"
#include "../managers/ProfileMusicManager.hpp"
#include "../managers/TransitionManager.hpp"
#include "../managers/ProfilePicCustomizer.hpp"
#include "../layers/ProfileMusicPopup.hpp"
#include "../layers/RateProfilePopup.hpp"
#include "../layers/ProfileReviewsPopup.hpp"
#include "../utils/Shaders.hpp"
#include "../utils/ImageLoadHelper.hpp"
#include <Geode/ui/LoadingSpinner.hpp>
#include "../utils/PaimonNotification.hpp"
#include "../utils/ShapeStencil.hpp"
#include "BadgeCache.hpp"

using namespace geode::prelude;
using namespace cocos2d;

// CCScale9Sprite::create crashea si el sprite no existe (no retorna nullptr).
static CCScale9Sprite* safeCreateScale9(char const* file) {
    auto* tex = CCTextureCache::sharedTextureCache()->addImage(file, false);
    if (!tex) return nullptr;
    return CCScale9Sprite::create(file);
}

// cache de texturas de profileimg para carga instantanea entre popups.
// Usa Ref<> para manejo automatico de refcount, con guardia de shutdown
// para evitar release() cuando el CCPoolManager ya este destruido.
static std::unordered_map<int, geode::Ref<CCTexture2D>> s_profileImgCache;
static bool s_profileImgShutdown = false;

// Limpiar el cache de profileimg durante el cierre del juego.
// Los destructores estaticos se ejecutan en orden indefinido y
// CCPoolManager puede ya estar muerto — usamos take() para sacar
// los Ref<> sin llamar release().
$on_game(Exiting) {
    s_profileImgShutdown = true;
    for (auto& [id, ref] : s_profileImgCache) {
        (void)ref.take();
    }
    s_profileImgCache.clear();
}

// Acceso externo al cache de profileimg (usado por InfoLayer hook).
CCTexture2D* getProfileImgCachedTexture(int accountID) {
    auto it = s_profileImgCache.find(accountID);
    if (it != s_profileImgCache.end()) return it->second;
    return nullptr;
}

// --- helpers de cache de disco para profileimg ---
static std::filesystem::path getProfileImgCacheDir() {
    return Mod::get()->getSaveDir() / "profileimg_cache";
}

static std::filesystem::path getProfileImgCachePath(int accountID) {
    return getProfileImgCacheDir() / fmt::format("{}.dat", accountID);
}

// Limpia todo el cache de profileimg (RAM + disco)
void clearProfileImgCache() {
    s_profileImgCache.clear();
    std::error_code ec;
    auto dir = getProfileImgCacheDir();
    if (std::filesystem::exists(dir, ec)) {
        std::filesystem::remove_all(dir, ec);
    }
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

static void saveProfileImgToDisk(int accountID, std::vector<uint8_t> const& data) {
    auto cacheDir = getProfileImgCacheDir();
    std::error_code ec;
    std::filesystem::create_directories(cacheDir, ec);
    auto cachePath = getProfileImgCachePath(accountID);
    std::ofstream cacheFile(cachePath, std::ios::binary);
    if (cacheFile) {
        cacheFile.write(reinterpret_cast<char const*>(data.data()), data.size());
        cacheFile.close();
    }
}

class $modify(PaimonProfilePage, ProfilePage) {
    static void onModify(auto& self) {
        // Late = ejecutar despues de otros mods (NodeIDs, BetterProfiles, etc.)
        // para que los IDs de nodos ya esten asignados cuando modificamos el perfil
        (void)self.setHookPriorityPost("ProfilePage::loadPageFromUserInfo", geode::Priority::Late);
    }

    struct Fields {
        Ref<CCMenuItemSpriteExtra> m_gearBtn = nullptr;
        Ref<CCMenuItemSpriteExtra> m_addModBtn = nullptr;       // Boton para anadir moderadores (solo admins)
        Ref<CCMenuItemSpriteExtra> m_addProfileBtn = nullptr;
        Ref<CCMenuItemSpriteExtra> m_banBtn = nullptr;
        Ref<CCMenuItemSpriteExtra> m_musicBtn = nullptr;       // Boton para configurar musica (solo propio perfil)
        Ref<CCMenuItemSpriteExtra> m_musicPauseBtn = nullptr;  // Boton para pausar musica (todos los perfiles)
        Ref<CCMenuItemSpriteExtra> m_addProfileImgBtn = nullptr; // Boton para anadir imagen de perfil
        Ref<CCClippingNode> m_profileClip = nullptr;
        Ref<CCLayerColor> m_profileSeparator = nullptr;
        Ref<CCNode> m_profileGradient = nullptr;
        Ref<CCNode> m_profileBorder = nullptr;
        Ref<CCClippingNode> m_profileImgClip = nullptr;   // Clip de la imagen de perfil (fondo normal del popup)
        Ref<CCNode> m_profileImgBorder = nullptr;          // Borde de la imagen de perfil
        bool m_isApprovedMod = false;
        bool m_isAdmin = false;
        bool m_musicPlaying = false;  // Estado de reproduccion de musica
        bool m_menuMusicPaused = false; // Si pausamos la musica del menu al abrir
    };

    bool canShowModerationControls() {
        // muestro controles si esta verificado como mod o admin
        return m_fields->m_isApprovedMod || m_fields->m_isAdmin;
    }

    // Helper: obtener left-menu de forma segura (estandar de Geode NodeIDs)
    CCMenu* getLeftMenu() {
        if (!this->m_mainLayer) return nullptr;
        auto node = this->m_mainLayer->getChildByID("left-menu");
        return node ? typeinfo_cast<CCMenu*>(node) : nullptr;
    }

    // Helper: obtener socials-menu de forma segura (estandar de Geode NodeIDs)
    CCMenu* getSocialsMenu() {
        if (!this->m_mainLayer) return nullptr;
        auto node = this->m_mainLayer->getChildByID("socials-menu");
        return node ? typeinfo_cast<CCMenu*>(node) : nullptr;
    }

    // Helper: escalar un sprite para que encaje en un tamano cuadrado
    static void scaleToFit(CCNode* spr, float targetSize) {
        if (!spr) return;
        float curSize = std::max(spr->getContentWidth(), spr->getContentHeight());
        if (curSize > 0) spr->setScale(targetSize / curSize);
    }

    // Helper: crear boton gear si no existe aun (mods + admins)
    void ensureGearButton(CCMenu* menu) {
        if (!menu || m_fields->m_gearBtn) return;
        if (menu->getChildByID("thumbs-gear-button"_spr)) return;

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
        scaleToFit(gearSpr, 26.f);
        auto gearBtn = CCMenuItemSpriteExtra::create(gearSpr, this, menu_selector(PaimonProfilePage::onOpenThumbsCenter));
        gearBtn->setID("thumbs-gear-button"_spr);
        menu->addChild(gearBtn);
        m_fields->m_gearBtn = gearBtn;
    }

    // Helper: crear boton add-moderator si no existe aun (solo admins)
    void ensureAddModeratorButton(CCMenu* menu) {
        if (!menu || m_fields->m_addModBtn) return;
        if (menu->getChildByID("add-moderator-button"_spr)) return;

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
        scaleToFit(addModSpr, 26.f);
        auto addModBtn = CCMenuItemSpriteExtra::create(addModSpr, this, menu_selector(PaimonProfilePage::onOpenAddModerator));
        addModBtn->setID("add-moderator-button"_spr);
        menu->addChild(addModBtn);
        m_fields->m_addModBtn = addModBtn;
    }

    // ── Verificador periodico de integridad de botones ──
    // Se ejecuta cada 0.5s para asegurar que todos los botones existen,
    // estan visibles y en el estado correcto (soluciona el bug donde
    // el boton de ban u otros desaparecen intermitentemente).
    void verifyButtonIntegrity(float dt) {
        if (!this->m_mainLayer) return;
        auto* leftMenu = getLeftMenu();
        if (!leftMenu) return;

        bool needsLayout = false;

        // 1. Boton de ban: debe existir siempre, visibilidad segun rango + no propio perfil
        if (!m_fields->m_banBtn || !m_fields->m_banBtn->getParent()) {
            // Recrear el boton de ban si se perdio
            auto banSpr = ButtonSprite::create("X", 40, true, "bigFont.fnt", "GJ_button_06.png", 30.f, 0.6f);
            banSpr->setScale(0.5f);
            auto banBtn = CCMenuItemSpriteExtra::create(banSpr, this, menu_selector(PaimonProfilePage::onBanUser));
            banBtn->setID("ban-user-button"_spr);
            banBtn->setVisible(false);
            leftMenu->addChild(banBtn);
            m_fields->m_banBtn = banBtn;
            needsLayout = true;
            log::debug("[ProfilePage] Boton de ban recreado por verificador de integridad");
        }

        // Actualizar visibilidad del ban
        {
            bool shouldShow = !this->m_ownProfile && (m_fields->m_isApprovedMod || m_fields->m_isAdmin);
            if (m_fields->m_banBtn->isVisible() != shouldShow) {
                m_fields->m_banBtn->setVisible(shouldShow);
                m_fields->m_banBtn->setEnabled(shouldShow);
                needsLayout = true;
            }
        }

        // 2. Boton de reviews: debe existir siempre
        if (!leftMenu->getChildByID("profile-reviews-btn"_spr)) {
            auto reviewIcon = CCSprite::createWithSpriteFrameName("GJ_chatBtn_001.png");
            if (!reviewIcon) reviewIcon = CCSprite::createWithSpriteFrameName("GJ_plainBtn_001.png");
            if (reviewIcon) {
                scaleToFit(reviewIcon, 26.f);
                auto reviewBtn = CCMenuItemSpriteExtra::create(reviewIcon, this, menu_selector(PaimonProfilePage::onProfileReviews));
                reviewBtn->setID("profile-reviews-btn"_spr);
                leftMenu->addChild(reviewBtn);
                needsLayout = true;
                log::debug("[ProfilePage] Boton de reviews recreado por verificador de integridad");
            }
        }

        // 3. Gear: debe existir si es mod/admin en perfil propio
        if (this->m_ownProfile && (m_fields->m_isApprovedMod || m_fields->m_isAdmin)) {
            if (!m_fields->m_gearBtn || !m_fields->m_gearBtn->getParent()) {
                m_fields->m_gearBtn = nullptr;
                ensureGearButton(leftMenu);
                needsLayout = true;
                log::debug("[ProfilePage] Boton gear recreado por verificador de integridad");
            }
        }

        // 4. Add moderator: debe existir si es admin en perfil propio
        if (this->m_ownProfile && m_fields->m_isAdmin) {
            if (!m_fields->m_addModBtn || !m_fields->m_addModBtn->getParent()) {
                m_fields->m_addModBtn = nullptr;
                ensureAddModeratorButton(leftMenu);
                needsLayout = true;
                log::debug("[ProfilePage] Boton add-mod recreado por verificador de integridad");
            }
        }

        if (needsLayout) {
            leftMenu->updateLayout();
        }
    }

    // ── Badge de moderador/admin en el perfil ──
    // (fusionado desde BadgeProfilePage para evitar doble $modify sobre ProfilePage)

    void onPaimonBadge(CCObject* sender) {
        if (auto node = typeinfo_cast<CCNode*>(sender)) {
            showBadgeInfoPopup(node);
        }
    }

    void addModeratorBadge(bool isMod, bool isAdmin) {
        // busco el menu del username
        auto menu = this->getChildByIDRecursive("username-menu");
        if (!menu) return;

        // si ya esta, no duplico
        if (menu->getChildByID("paimon-moderator-badge"_spr)) return;
        if (menu->getChildByID("paimon-admin-badge"_spr)) return;

        CCSprite* badgeSprite = nullptr;
        std::string badgeID;

        if (isAdmin) {
            badgeSprite = CCSprite::create("paim_Admin.png"_spr);
            badgeID = "paimon-admin-badge"_spr;
        } else if (isMod) {
            badgeSprite = CCSprite::create("paim_Moderador.png"_spr);
            badgeID = "paimon-moderator-badge"_spr;
        }

        if (!badgeSprite) return;

        log::info("Adding badge (Clickable) - Admin: {}, Mod: {}", isAdmin, isMod);

        float targetHeight = 20.0f;
        float scale = targetHeight / badgeSprite->getContentSize().height;
        badgeSprite->setScale(scale);

        auto btn = CCMenuItemSpriteExtra::create(
            badgeSprite,
            this,
            menu_selector(PaimonProfilePage::onPaimonBadge)
        );
        btn->setID(badgeID);

        if (auto menuNode = typeinfo_cast<CCMenu*>(menu)) {
            menuNode->addChild(btn);
            menuNode->updateLayout();
        }
    }

    std::string getViewedUsername() {
        // acceso directo al campo m_userName del GJUserScore
        if (this->m_score && !this->m_score->m_userName.empty()) {
            return this->m_score->m_userName;
        }
        // fallback: leer de labels si m_score no esta disponible aun
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
            HttpClient::get().get("/api/moderators", [self, targetLower](bool ok, std::string const& resp) {
                if (!ok) return;
                // compruebo que sigo vivo (por si acaso)
                if (!self || !self->getParent()) return;

                auto parsed = matjson::parse(resp);
                if (!parsed.isOk()) return;
                auto root = parsed.unwrap();
                auto mods = root["moderators"]; // [{ username, currentBanner }]
                if (!mods.isArray()) return;
                auto modsArr = mods.asArray();
                if (!modsArr.isOk()) return;
                for (auto const& v : modsArr.unwrap()) {
                    if (!v.isObject()) continue;
                    auto u = v["username"];
                    if (!u.isString()) continue;
                    auto nameLower = geode::utils::string::toLower(u.asString().unwrapOr(""));
                    if (nameLower == targetLower) {
                        // ya estamos en el main thread
                        if (auto banBtn = typeinfo_cast<CCMenuItemSpriteExtra*>(self->getChildByIDRecursive("ban-user-button"))) {
                            banBtn->setEnabled(false);
                            banBtn->setOpacity(120);
                        }
                        return;
                    }
                }
            });
        }
    }

    void onBanUser(CCObject*) {
        if (!canShowModerationControls()) {
            PaimonNotify::create(Localization::get().getString("ban.profile.mod_only"), NotificationIcon::Warning)->show();
            return;
        }
        if (this->m_ownProfile) {
            PaimonNotify::create(Localization::get().getString("ban.profile.self_ban"), NotificationIcon::Warning)->show();
            return;
        }

        // nombre del user en el perfil
        std::string target = getViewedUsername();
        if (target.empty()) {
            PaimonNotify::create(Localization::get().getString("ban.profile.read_error"), NotificationIcon::Error)->show();
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
                // Ref<> mantiene vivo el ProfilePage hasta que termine la cadena de callbacks
                Ref<ProfilePage> self = this;
                std::string username = GJAccountManager::get()->m_username;
                ThumbnailAPI::get().downloadProfile(accountID, username, [self, accountID](bool success, CCTexture2D* texture) {
                    if (success && texture) {
                        // retengo la textura
                        texture->retain();
                        ThumbnailAPI::get().downloadProfileConfig(accountID, [self, accountID, texture](bool s, ProfileConfig const& c) {
                            Loader::get()->queueInMainThread([self, accountID, texture, s, c]() {
                                ProfileThumbs::get().cacheProfile(accountID, texture, {255,255,255}, {255,255,255}, 0.5f);
                                if (s) ProfileThumbs::get().cacheProfileConfig(accountID, c);

                                // solo actualizo si sigue en la escena
                                if (self->getParent()) {
                                    static_cast<PaimonProfilePage*>(self.data())->addOrUpdateProfileThumbOnPage(accountID);
                                }

                                texture->release();
                            });
                        });
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

        // tamano layer ref
        auto layer = this->m_mainLayer ? this->m_mainLayer : static_cast<CCNode*>(this);
        auto cs = layer->getContentSize();
        if (cs.width <= 1.f || cs.height <= 1.f) cs = this->getContentSize();

        // leer config de personalizacion de la foto circular
        auto picCfg = ProfilePicCustomizer::get().getConfig();
        float thumbSize = picCfg.size;

        float imgScaleX = thumbSize / sprite->getContentWidth();
        float imgScaleY = thumbSize / sprite->getContentHeight();
        float imgScale = std::max(imgScaleX, imgScaleY);
        sprite->setScale(imgScale);

        // mask con forma personalizable (geometrica o sprite)
        auto shapeMask = createShapeStencil(picCfg.stencilSprite, thumbSize);
        if (!shapeMask) shapeMask = createShapeStencil("circle", thumbSize);
        if (!shapeMask) return;

        auto clip = CCClippingNode::create();
        clip->setStencil(shapeMask);
        clip->setAlphaThreshold(-1.0f);
        clip->setContentSize({thumbSize, thumbSize});
        clip->setAnchorPoint({1, 1}); // anchor top-right
        
        // posicion arriba a la derecha con offset personalizable
        float rightX = cs.width - 10.f + picCfg.offsetX;
        float topY = cs.height - 10.f + picCfg.offsetY;
        clip->setPosition({rightX, topY});
        clip->setZOrder(10);
        // aplicar escala X/Y independiente (aplanar/alargar)
        clip->setScaleX(picCfg.scaleX);
        clip->setScaleY(picCfg.scaleY);
        clip->setID("paimon-profilepage-clip"_spr);

        sprite->setPosition(clip->getContentSize() * 0.5f);
        clip->addChild(sprite);
        layer->addChild(clip);
        f->m_profileClip = clip;

        // marco/borde con la MISMA FORMA que el stencil
        CCNode* borderNode = nullptr;
        if (picCfg.frameEnabled) {
            float frameSize = thumbSize + picCfg.frame.thickness * 2;
            borderNode = createShapeBorder(
                picCfg.stencilSprite, frameSize,
                picCfg.frame.thickness,
                picCfg.frame.color,
                static_cast<GLubyte>(picCfg.frame.opacity)
            );
        }
        if (!borderNode) {
            // borde por defecto con la misma forma del stencil
            borderNode = createShapeBorder(
                picCfg.stencilSprite, thumbSize + 4.f,
                2.f, {0, 0, 0}, 120
            );
        }
        if (borderNode) {
            borderNode->setAnchorPoint({1, 1});
            float borderOffset = borderNode->getContentSize().width / 2 - thumbSize / 2;
            borderNode->setPosition({rightX + borderOffset, topY + borderOffset});
            borderNode->setScaleX(picCfg.scaleX);
            borderNode->setScaleY(picCfg.scaleY);
            borderNode->setZOrder(9);
            borderNode->setID("paimon-profilepage-border"_spr);
            layer->addChild(borderNode);
            f->m_profileBorder = borderNode;
        }

        // decoraciones (assets del juego colocados alrededor de la foto)
        for (auto const& deco : picCfg.decorations) {
            CCSprite* decoSpr = CCSprite::create(deco.spriteName.c_str());
            if (!decoSpr) decoSpr = CCSprite::createWithSpriteFrameName(deco.spriteName.c_str());
            if (!decoSpr) continue;

            decoSpr->setScale(deco.scale);
            decoSpr->setRotation(deco.rotation);
            decoSpr->setColor(deco.color);
            decoSpr->setOpacity(static_cast<GLubyte>(deco.opacity));
            decoSpr->setFlipX(deco.flipX);
            decoSpr->setFlipY(deco.flipY);
            decoSpr->setZOrder(11 + deco.zOrder);

            // posicion relativa al centro de la foto
            float centerFotoX = rightX - (thumbSize * picCfg.scaleX) / 2;
            float centerFotoY = topY - (thumbSize * picCfg.scaleY) / 2;
            float dx = centerFotoX + deco.posX * (thumbSize * picCfg.scaleX / 2);
            float dy = centerFotoY + deco.posY * (thumbSize * picCfg.scaleY / 2);
            decoSpr->setPosition({dx, dy});
            decoSpr->setID("paimon-profilepage-deco"_spr);
            layer->addChild(decoSpr);
        }

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

        // 1) si hay cache en memoria, mostrar de inmediato
        auto it = s_profileImgCache.find(accountID);
        if (it != s_profileImgCache.end() && it->second) {
            this->displayProfileImg(accountID, it->second);
        } else {
            // 2) si hay cache en disco, cargar y mostrar
            if (auto* diskTex = loadProfileImgFromDisk(accountID)) {
                // Ref<> hace retain en la asignacion y release del anterior automaticamente
                s_profileImgCache[accountID] = diskTex;
                this->displayProfileImg(accountID, diskTex);
            }
        }

        // descargar del servidor en segundo plano (actualizar cache)
        // Ref<> mantiene vivo el ProfilePage hasta que termine el callback
        Ref<ProfilePage> self = this;
        ThumbnailAPI::get().downloadProfileImg(accountID, [self, accountID](bool success, CCTexture2D* texture) {
            if (!self->getParent()) return;

            if (success && texture) {
                // Ref<> hace retain en la asignacion y release del anterior automaticamente
                s_profileImgCache[accountID] = texture;
                static_cast<PaimonProfilePage*>(self.data())->displayProfileImg(accountID, texture);
            }
        }, isSelf);
    }

    static bool isBrownColor(ccColor3B const& c) {
        return (c.r >= 0x70 && c.g >= 0x20 && c.g <= 0xA0 && c.b <= 0x70 && c.r > c.g && c.g >= c.b);
    }

    static bool isDarkBgColor(ccColor3B const& c) {
        return (c.r <= 0x60 && c.g <= 0x50 && c.b <= 0x40 && (c.r + c.g + c.b) > 0);
    }

    // Tinta un CCScale9Sprite completo (centro + bordes) a un color.
    // CCScale9Sprite tiene un CCSpriteBatchNode interno (_scale9Image) con 9 sprites hijos.
    // setColor() en el CCScale9Sprite NO siempre propaga a esos sprites internos,
    // asi que los tintamos directamente.
    static void tintScale9(CCScale9Sprite* s9, ccColor3B const& color, GLubyte opacity) {
        if (!s9) return;

        // Activar cascade para que hijos hereden
        s9->setCascadeColorEnabled(true);
        s9->setCascadeOpacityEnabled(true);
        s9->setColor(color);
        s9->setOpacity(opacity);

        // Tintar hijos directos del batch node interno
        // El _scale9Image es el primer (y generalmente unico) hijo del CCScale9Sprite
        auto s9Children = s9->getChildren();
        if (!s9Children) return;
        for (auto* batchNode : CCArrayExt<CCSpriteBatchNode*>(s9Children)) {
            if (!batchNode) continue;
            auto batchChildren = batchNode->getChildren();
            if (!batchChildren) continue;
            for (auto* spr : CCArrayExt<CCSprite*>(batchChildren)) {
                if (spr) {
                    spr->setColor(color);
                    spr->setOpacity(opacity);
                }
            }
        }
    }

    // Oculta "icon-background", GJCommentListLayer (bordes, fondos, decorativos)
    // y los fondos internos de cada CommentCell (CCLayerColor, CCLayer con CCScale9Sprite).
    void styleProfileInternalBgs(CCNode* root) {
        if (!root) return;

        auto walk = [&](auto const& self, CCNode* parent) -> void {
            if (!parent) return;
            auto* children = parent->getChildren();
            if (!children) return;
            for (auto* child : CCArrayExt<CCNode*>(children)) {
                if (!child) continue;

                // Ocultar icon-background por node ID
                if (child->getID() == "icon-background") {
                    child->setVisible(false);
                }

                // GJCommentListLayer: opacidad 0 + ocultar bordes y fondos
                if (auto* commentList = typeinfo_cast<GJCommentListLayer*>(child)) {
                    commentList->setOpacity(0);

                    auto* listChildren = commentList->getChildren();
                    if (listChildren) {
                        for (auto* lc : CCArrayExt<CCNode*>(listChildren)) {
                            if (!lc) continue;
                            auto id = lc->getID();
                            // Bordes con node ID conocido
                            if (id == "left-border" || id == "right-border" ||
                                id == "top-border" || id == "bottom-border") {
                                lc->setVisible(false);
                            }
                            // Nodos sin ID: fondos/separadores decorativos
                            if (id.empty()) {
                                lc->setVisible(false);
                            }
                        }
                    }

                    // Recorrer CommentCells para ocultar fondos internos
                    hideCommentCellBgs(commentList);
                }

                self(self, child);
            }
        };

        walk(walk, root);
        walk(walk, static_cast<CCNode*>(this));
    }

    // Recorre la jerarquia de un GJCommentListLayer hasta encontrar CommentCells
    // y oculta sus fondos internos (CCLayerColor y CCScale9Sprite de fondo).
    void hideCommentCellBgs(CCNode* listNode) {
        if (!listNode) return;

        auto findCells = [&](auto const& self, CCNode* node) -> void {
            if (!node) return;
            auto* children = node->getChildren();
            if (!children) return;
            for (auto* child : CCArrayExt<CCNode*>(children)) {
                if (!child) continue;

                // CommentCell: ocultar CCLayerColor (fondo de celda) y
                // el CCLayer hijo que contiene el CCScale9Sprite de background
                if (typeinfo_cast<CommentCell*>(child)) {
                    auto* cellChildren = child->getChildren();
                    if (!cellChildren) continue;
                    for (auto* cc : CCArrayExt<CCNode*>(cellChildren)) {
                        if (!cc) continue;

                        // CCLayerColor directo = fondo de la celda
                        if (typeinfo_cast<CCLayerColor*>(cc)) {
                            cc->setVisible(false);
                        }

                        // CCLayer hijo: contiene texto Y CCScale9Sprite de fondo.
                        // Solo ocultar el CCScale9Sprite, NO el CCLayer (tiene el texto).
                        if (typeinfo_cast<CCLayer*>(cc) && !typeinfo_cast<CCLayerColor*>(cc)) {
                            auto* layerKids = cc->getChildren();
                            if (!layerKids) continue;
                            for (auto* lk : CCArrayExt<CCNode*>(layerKids)) {
                                if (lk && typeinfo_cast<CCScale9Sprite*>(lk)) {
                                    lk->setVisible(false);
                                }
                            }
                        }
                    }
                }

                self(self, child);
            }
        };

        findCells(findCells, listNode);
    }

    // Hook: se llama cuando GD termina de cargar la info del usuario y construye los paneles.
    // Aplicamos opacidad 0 a icon-background inmediatamente para evitar parpadeo.
    void getUserInfoFinished(GJUserScore* score) {
        ProfilePage::getUserInfoFinished(score);
        if (auto* layer = this->m_mainLayer) {
            styleProfileInternalBgs(layer);
        }
    }

    void onProfileReviews(CCObject*) {
        if (auto popup = ProfileReviewsPopup::create(this->m_accountID)) {
            popup->show();
        }
    }

    void onRateProfile(CCObject*) {
        // no calificar tu propio perfil
        if (this->m_ownProfile) {
            PaimonNotify::create("You can't rate your own profile!", NotificationIcon::Warning)->show();
            return;
        }

        std::string targetName = getViewedUsername();
        if (targetName.empty()) targetName = "Unknown";

        if (auto popup = RateProfilePopup::create(this->m_accountID, targetName)) {
            popup->show();
        }
    }

    // ── Helper: limpiar todos los botones paimon de un menu antes de re-crearlos ──
    // Evita duplicados si loadPageFromUserInfo se llama multiples veces
    void cleanPaimonButtons(CCMenu* menu) {
        if (!menu) return;
        static std::string const paimonBtnIDs[] = {
            "profile-reviews-btn"_spr,
            "ban-user-button"_spr,
            "thumbs-gear-button"_spr,
            "add-moderator-button"_spr,
        };
        for (auto const& id : paimonBtnIDs) {
            while (auto* btn = menu->getChildByID(id)) {
                btn->removeFromParent();
            }
        }
        m_fields->m_gearBtn = nullptr;
        m_fields->m_banBtn = nullptr;
    }

    void cleanPaimonSocialsButtons(CCMenu* menu) {
        if (!menu) return;
        static std::string const paimonSocialIDs[] = {
            "profile-music-button"_spr,
            "add-profileimg-button"_spr,
            "profile-music-pause-button"_spr,
        };
        for (auto const& id : paimonSocialIDs) {
            while (auto* btn = menu->getChildByID(id)) {
                btn->removeFromParent();
            }
        }
        m_fields->m_musicBtn = nullptr;
        m_fields->m_musicPauseBtn = nullptr;
        m_fields->m_addProfileImgBtn = nullptr;
    }

    // ── Helper: obtener posicion/tamano del popup de perfil ──
    // Usamos el nodo "background" (asignado por node-ids) para centrar los menus
    CCPoint getPopupCenter() {
        if (!this->m_mainLayer) return CCDirector::sharedDirector()->getWinSize() / 2;
        if (auto bg = this->m_mainLayer->getChildByID("background")) {
            return bg->getPosition();
        }
        for (auto* child : CCArrayExt<CCNode*>(this->m_mainLayer->getChildren())) {
            if (typeinfo_cast<CCScale9Sprite*>(child)) {
                return child->getPosition();
            }
        }
        return this->m_mainLayer->getContentSize() / 2;
    }

    CCSize getPopupSize() {
        if (!this->m_mainLayer) return {440.f, 290.f};
        if (auto bg = this->m_mainLayer->getChildByID("background")) {
            return bg->getScaledContentSize();
        }
        for (auto* child : CCArrayExt<CCNode*>(this->m_mainLayer->getChildren())) {
            if (typeinfo_cast<CCScale9Sprite*>(child)) {
                return child->getScaledContentSize();
            }
        }
        return {440.f, 290.f};
    }

    // Hook: se llama cuando GD construye los paneles de iconos del perfil.
    // Geode node-ids asigna IDs aqui; es el momento mas fiable para ocultar icon-background.
    void loadPageFromUserInfo(GJUserScore* score) {
        ProfilePage::loadPageFromUserInfo(score);
        if (auto* layer = this->m_mainLayer) {
            styleProfileInternalBgs(layer);
        }

        if (!this->m_mainLayer) return;

        // ── Referencia al popup ──
        auto popCenter = getPopupCenter();
        auto popSize = getPopupSize();

        // ── Obtener o crear left-menu ──
        auto leftMenuNode = this->m_mainLayer->getChildByID("left-menu");
        CCMenu* menu = leftMenuNode ? typeinfo_cast<CCMenu*>(leftMenuNode) : nullptr;

        if (!menu) {
            menu = CCMenu::create();
            menu->setID("left-menu");
            menu->setZOrder(10);
            this->m_mainLayer->addChild(menu);

            // Solo posicionar si creamos nosotros el menu (fallback)
            float menuX = popCenter.x - popSize.width / 2 + 18.f;
            float menuY = popCenter.y;
            menu->setPosition({menuX, menuY});
            menu->setContentSize({40.f, popSize.height * 0.75f});
            menu->setAnchorPoint({0.5f, 0.5f});
            menu->ignoreAnchorPointForPosition(false);

            menu->setLayout(
                ColumnLayout::create()
                    ->setGap(8.f)
                    ->setAxisAlignment(AxisAlignment::Center)
                    ->setAxisReverse(false)
                    ->setCrossAxisAlignment(AxisAlignment::Center)
            );
        }

        // ── Limpiar botones paimon anteriores para evitar duplicados ──
        cleanPaimonButtons(menu);

        // ── Boton de reviews (siempre visible) ──
        {
            auto reviewIcon = CCSprite::createWithSpriteFrameName("GJ_chatBtn_001.png");
            if (!reviewIcon) reviewIcon = CCSprite::createWithSpriteFrameName("GJ_plainBtn_001.png");
            if (reviewIcon) {
                scaleToFit(reviewIcon, 26.f);
                auto reviewBtn = CCMenuItemSpriteExtra::create(reviewIcon, this, menu_selector(PaimonProfilePage::onProfileReviews));
                reviewBtn->setID("profile-reviews-btn"_spr);
                menu->addChild(reviewBtn);
            }
        }

        // ── Boton de calificar perfil en bottom-menu (solo perfiles ajenos) ──
        if (!this->m_ownProfile) {
            if (auto bottomMenu = this->m_mainLayer->getChildByIDRecursive("bottom-menu")) {
                if (!bottomMenu->getChildByID("rate-profile-btn"_spr)) {
                    auto bg = CCScale9Sprite::create("GJ_button_04.png");
                    if (!bg) bg = CCScale9Sprite::create("GJ_button_01.png");
                    bg->setContentSize({30.f, 30.f});

                    auto starIcon = CCSprite::createWithSpriteFrameName("GJ_starsIcon_001.png");
                    if (!starIcon) starIcon = CCSprite::createWithSpriteFrameName("GJ_bigStar_001.png");
                    if (starIcon) {
                        scaleToFit(starIcon, 18.f);
                        starIcon->setPosition({15.f, 15.f});
                        bg->addChild(starIcon);
                    }

                    auto starBtn = CCMenuItemSpriteExtra::create(bg, this, menu_selector(PaimonProfilePage::onRateProfile));
                    starBtn->setID("rate-profile-btn"_spr);

                    auto* btmMenu = typeinfo_cast<CCMenu*>(bottomMenu);
                    if (btmMenu) {
                        btmMenu->addChild(starBtn);
                        btmMenu->updateLayout();
                    }
                }
            }
        }

        // ── Boton de ban (solo mods/admins, no perfil propio) ──
        {
            auto banSpr = ButtonSprite::create("X", 40, true, "bigFont.fnt", "GJ_button_06.png", 30.f, 0.6f);
            banSpr->setScale(0.5f);
            auto banBtn = CCMenuItemSpriteExtra::create(banSpr, this, menu_selector(PaimonProfilePage::onBanUser));
            banBtn->setID("ban-user-button"_spr);
            banBtn->setVisible(false);
            menu->addChild(banBtn);
            m_fields->m_banBtn = banBtn;
        }
        refreshBanButtonVisibility();

        // ── Botones de moderacion (solo perfil propio, segun rango verificado) ──
        if (this->m_ownProfile) {
            // Si ya esta verificado como mod o admin → mostrar gear (centro de verificacion)
            if (m_fields->m_isApprovedMod || m_fields->m_isAdmin) {
                ensureGearButton(menu);
            }
            // Si es admin → mostrar boton de anadir moderador
            if (m_fields->m_isAdmin) {
                ensureAddModeratorButton(menu);
            }
        }

        // ── Recalcular layout del left-menu ──
        menu->updateLayout();

        // ── Botones en socials-menu ──
        auto* socialsMenu = getSocialsMenu();
        bool createdSocialsMenu = false;
        if (!socialsMenu) {
            auto newSocialsMenu = CCMenu::create();
            newSocialsMenu->setID("socials-menu");
            newSocialsMenu->setZOrder(10);
            this->m_mainLayer->addChild(newSocialsMenu);
            socialsMenu = newSocialsMenu;
            createdSocialsMenu = true;

            // Solo posicionar si creamos nosotros el menu (fallback)
            float socialsX = popCenter.x + popSize.width / 2 - 18.f;
            float socialsY = popCenter.y;
            socialsMenu->setPosition({socialsX, socialsY});
            socialsMenu->setContentSize({40.f, popSize.height * 0.7f});
            socialsMenu->setAnchorPoint({0.5f, 0.5f});
            socialsMenu->ignoreAnchorPointForPosition(false);

            socialsMenu->setLayout(
                ColumnLayout::create()
                    ->setGap(8.f)
                    ->setAxisAlignment(AxisAlignment::Center)
                    ->setAxisReverse(false)
                    ->setCrossAxisAlignment(AxisAlignment::Center)
            );
        }

        cleanPaimonSocialsButtons(socialsMenu);

        // ── Anadir nuestros botones DESPUeS de los botones nativos de GD ──
        // Los botones de YouTube, Twitter, Twitch etc. ya estan en el socials-menu
        // por el hook de GD; nosotros solo anadimos al final.

        if (this->m_ownProfile) {
            {
                auto musicSpr = CCSprite::createWithSpriteFrameName("GJ_audioBtn_001.png");
                if (!musicSpr) musicSpr = CCSprite::createWithSpriteFrameName("GJ_musicOnBtn_001.png");
                if (!musicSpr) musicSpr = CCSprite::create();
                scaleToFit(musicSpr, 22.f);
                auto musicBtn = CCMenuItemSpriteExtra::create(musicSpr, this, menu_selector(PaimonProfilePage::onConfigureProfileMusic));
                musicBtn->setID("profile-music-button"_spr);
                socialsMenu->addChild(musicBtn);
                m_fields->m_musicBtn = musicBtn;
            }
            {
                auto imgSpr = CCSprite::createWithSpriteFrameName("GJ_duplicateBtn_001.png");
                if (!imgSpr) imgSpr = CCSprite::createWithSpriteFrameName("GJ_editBtn_001.png");
                if (!imgSpr) imgSpr = CCSprite::create();
                scaleToFit(imgSpr, 22.f);
                auto imgBtn = CCMenuItemSpriteExtra::create(imgSpr, this, menu_selector(PaimonProfilePage::onAddProfileImg));
                imgBtn->setID("add-profileimg-button"_spr);
                socialsMenu->addChild(imgBtn);
                m_fields->m_addProfileImgBtn = imgBtn;
            }
        }

        {
            auto pauseSpr = CCSprite::createWithSpriteFrameName("GJ_pauseBtn_001.png");
            if (!pauseSpr) pauseSpr = CCSprite::createWithSpriteFrameName("GJ_stopMusicBtn_001.png");
            if (!pauseSpr) pauseSpr = CCSprite::create();
            scaleToFit(pauseSpr, 20.f);
            auto pauseBtn = CCMenuItemSpriteExtra::create(pauseSpr, this, menu_selector(PaimonProfilePage::onToggleProfileMusic));
            pauseBtn->setID("profile-music-pause-button"_spr);
            pauseBtn->setVisible(false);
            socialsMenu->addChild(pauseBtn);
            m_fields->m_musicPauseBtn = pauseBtn;
        }

        socialsMenu->updateLayout();

        // ── Badge de moderador/admin en el username ──
        if (score) {
            std::string badgeUsername = score->m_userName;

            if (g_moderatorCache.contains(badgeUsername)) {
                auto [isMod, isAdmin] = g_moderatorCache[badgeUsername];
                if (isMod || isAdmin) {
                    this->addModeratorBadge(isMod, isAdmin);
                }
            }

            Ref<ProfilePage> badgeSafeRef = this;
            ThumbnailAPI::get().checkUserStatus(badgeUsername, [badgeSafeRef, badgeUsername](bool isMod, bool isAdmin) {
                moderatorCacheInsert(badgeUsername, isMod, isAdmin);
                Loader::get()->queueInMainThread([badgeSafeRef, badgeUsername, isMod, isAdmin]() {
                    if (!badgeSafeRef->getParent()) return;
                    if (isMod || isAdmin) {
                        static_cast<PaimonProfilePage*>(badgeSafeRef.data())->addModeratorBadge(isMod, isAdmin);
                    }
                });
            });
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
    }

    // Tick periodico: reaplica opacidad 0 a icon-background por si GD lo recrea
    // (e.g. al cambiar tab de comentarios).
    void tickStyleBgs(float) {
        if (auto* layer = this->m_mainLayer) {
            styleProfileInternalBgs(layer);
        }
    }



    bool init(int accountID, bool ownProfile) {
        if (!ProfilePage::init(accountID, ownProfile)) return false;

            // empiezo siempre como no moderador
            m_fields->m_isApprovedMod = false;
            m_fields->m_isAdmin = false;
            PaimonDebug::log("[ProfilePage] Inicializando perfil - status moderador: false");

            // estado mod guardado
            bool wasVerified = Mod::get()->getSavedValue<bool>("is-verified-moderator", false);
            bool wasAdmin = Mod::get()->getSavedValue<bool>("is-verified-admin", false);
            if (wasVerified) {
                m_fields->m_isApprovedMod = true;
                m_fields->m_isAdmin = wasAdmin;
            }

            // Verificacion automatica con el server (siempre si es perfil propio)
            if (ownProfile) {
                auto gm = GameManager::get();
                if (gm && !gm->m_playerName.empty()) {
                    std::string username = gm->m_playerName;
                    Ref<ProfilePage> self = this;
                    ThumbnailAPI::get().checkModerator(username, [self](bool isApproved, bool isAdmin) {
                        Loader::get()->queueInMainThread([self, isApproved, isAdmin]() {
                            if (!self->getParent()) return;
                            auto* page = static_cast<PaimonProfilePage*>(self.data());
                            page->m_fields->m_isApprovedMod = isApproved;
                            page->m_fields->m_isAdmin = isAdmin;

                            // Guardar estado persistente
                            Mod::get()->setSavedValue("is-verified-moderator", isApproved);
                            Mod::get()->setSavedValue("is-verified-admin", isAdmin);

                            if (isApproved) {
                                // Guardar archivo de verificacion con timestamp
                                auto modDataPath = Mod::get()->getSaveDir() / "moderator_verification.dat";
                                std::ofstream modFile(modDataPath, std::ios::binary);
                                if (modFile) {
                                    auto now = std::chrono::system_clock::now();
                                    auto timestamp = std::chrono::system_clock::to_time_t(now);
                                    modFile.write(reinterpret_cast<char const*>(&timestamp), sizeof(timestamp));
                                    modFile.close();
                                }
                            }

                            page->refreshBanButtonVisibility();

                            // Anadir/quitar botones segun rango
                            if (auto* leftMenu = page->getLeftMenu()) {
                                if (isApproved || isAdmin) {
                                    page->ensureGearButton(leftMenu);
                                }
                                if (isAdmin) {
                                    page->ensureAddModeratorButton(leftMenu);
                                }
                                leftMenu->updateLayout();
                            }
                        });
                    });
                }
            }

            // Marcar que el perfil esta abierto para saber si debemos restaurar BG al cerrar
            m_fields->m_menuMusicPaused = true;

            // Verificar si este perfil tiene musica y reproducirla
            checkAndPlayProfileMusic(accountID);

            // Cargar imagen de perfil (visible para todos)
            addOrUpdateProfileImgOnPage(accountID, ownProfile);

            // schedule permanente: mantiene icon-background con opacidad 0
            this->schedule(schedule_selector(PaimonProfilePage::tickStyleBgs), 0.01f);

            // schedule de verificacion de integridad de botones cada 0.5s
            this->schedule(schedule_selector(PaimonProfilePage::verifyButtonIntegrity), 0.5f);

        return true;
    }

    void onOpenAddModerator(CCObject*) {
        if (auto* popup = AddModeratorPopup::create(nullptr)) popup->show();
    }


    void onOpenThumbsCenter(CCObject*) {
        // aseguro que el user es mod o admin antes de abrir panel
        if (!m_fields->m_isApprovedMod && !m_fields->m_isAdmin) {
            log::warn("[ProfilePage] Usuario NO es moderador ni admin, bloqueando acceso al centro de verificacion");
            FLAlertLayer::create(
                Localization::get().getString("profile.access_denied").c_str(),
                Localization::get().getString("profile.moderators_only").c_str(),
                Localization::get().getString("general.ok").c_str()
            )->show();
            return;
        }
        
        // abro centro de verificacion con categorias
        log::info("[ProfilePage] Abriendo centro de verificacion para moderador");
        auto scene = VerificationCenterLayer::scene();
        if (scene) {
            TransitionManager::get().pushScene(scene);
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
                 PaimonNotify::create(Localization::get().getString("profile.no_image_selected").c_str(), NotificationIcon::Warning)->show();
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
                PaimonNotify::create(Localization::get().getString("profile.no_image_selected").c_str(), NotificationIcon::Warning)->show();
            }
        }
    }

    void processProfileImg(std::filesystem::path path) {
        if (ImageLoadHelper::isGIF(path)) {
            // GIF: subir directamente
            auto imgData = ImageLoadHelper::readBinaryFile(path, 10);
            if (imgData.empty()) {
                PaimonNotify::create(Localization::get().getString("profile.image_open_error").c_str(), NotificationIcon::Error)->show();
                return;
            }

            int accountID = this->m_accountID;
            std::string username = GJAccountManager::get()->m_username;

            auto gifSpinner = geode::LoadingSpinner::create(30.f);
            gifSpinner->setPosition(CCDirector::sharedDirector()->getWinSize() / 2);
            gifSpinner->setID("paimon-loading-spinner"_spr);
            this->addChild(gifSpinner, 100);
            Ref<geode::LoadingSpinner> loading = gifSpinner;

            Ref<ProfilePage> imgGifSafeRef = this;

            ThumbnailAPI::get().uploadProfileImgGIF(accountID, imgData, username, [imgGifSafeRef, accountID, imgData, loading](bool success, std::string const& msg) {
                if (loading) loading->removeFromParent();

                if (success) {
                    PaimonNotify::create("Profile image uploaded!", NotificationIcon::Success)->show();

                    saveProfileImgToDisk(accountID, imgData);

                    CCImage img;
                    if (img.initWithImageData(const_cast<uint8_t*>(imgData.data()), imgData.size())) {
                        auto tex = new CCTexture2D();
                        if (tex->initWithImage(&img)) {
                            tex->autorelease();
                            // Ref<> hace retain/release automaticamente
                            s_profileImgCache[accountID] = tex;
                            static_cast<PaimonProfilePage*>(imgGifSafeRef.data())->displayProfileImg(accountID, tex);
                        } else {
                            tex->release();
                        }
                    }
                } else {
                    PaimonNotify::create("Upload failed: " + msg, NotificationIcon::Error)->show();
                }
            });
            return;
        }

        // Imagen estatica: usar helper pa cargar y convertir
        auto loaded = ImageLoadHelper::loadStaticImage(path, 10);
        if (!loaded.success) {
            std::string errKey = loaded.error;
            // si el error es una key de localizacion, traducir
            if (errKey == "image_open_error" || errKey == "invalid_image_data" || errKey == "texture_error") {
                PaimonNotify::create(Localization::get().getString("profile." + errKey).c_str(), NotificationIcon::Error)->show();
            } else {
                PaimonNotify::create(errKey.c_str(), NotificationIcon::Error)->show();
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

                        auto pngSpinner = geode::LoadingSpinner::create(30.f);
                        pngSpinner->setPosition(CCDirector::sharedDirector()->getWinSize() / 2);
                        pngSpinner->setID("paimon-loading-spinner"_spr);
                        this->addChild(pngSpinner, 100);
                        Ref<geode::LoadingSpinner> loading = pngSpinner;

                        Ref<ProfilePage> imgUploadRef = this;

                        ThumbnailAPI::get().uploadProfileImg(accountID, pngData, username, "image/png", [imgUploadRef, accountID, pngData, loading, buf, w, h](bool success, std::string const& msg) {
                            if (loading) loading->removeFromParent();

                            if (success) {
                                bool isPending = (msg.find("pending") != std::string::npos || msg.find("verification") != std::string::npos);

                                if (isPending) {
                                    PaimonNotify::create("Image submitted! Pending moderator verification.", NotificationIcon::Warning)->show();
                                } else {
                                    PaimonNotify::create("Profile image uploaded!", NotificationIcon::Success)->show();
                                }

                                saveProfileImgToDisk(accountID, pngData);

                                CCImage finalImg;
                                if (finalImg.initWithImageData(buf.get(), w * h * 4, CCImage::kFmtRawData, w, h)) {
                                    auto finalTex = new CCTexture2D();
                                    if (finalTex->initWithImage(&finalImg)) {
                                        finalTex->autorelease();
                                        // Ref<> hace retain/release automaticamente
                                        s_profileImgCache[accountID] = finalTex;
                                        static_cast<PaimonProfilePage*>(imgUploadRef.data())->displayProfileImg(accountID, finalTex);
                                    } else {
                                        finalTex->release();
                                    }
                                }
                            } else {
                                PaimonNotify::create("Upload failed: " + msg, NotificationIcon::Error)->show();
                            }
                        });
                    }

                    std::error_code rmEc;
                    std::filesystem::remove(tempPath, rmEc);
                }
            }
        );
        if (popup) popup->show();
    }

    void processProfileImage(std::filesystem::path path) {
        if (ImageLoadHelper::isGIF(path)) {
            auto gifData = ImageLoadHelper::readBinaryFile(path, 10);
            if (gifData.empty()) {
                PaimonNotify::create(Localization::get().getString("profile.image_open_error").c_str(), NotificationIcon::Error)->show();
                return;
            }

            int accountID = this->m_accountID;
            std::string username = GJAccountManager::get()->m_username;

            auto gifSpinner2 = geode::LoadingSpinner::create(30.f);
            gifSpinner2->setPosition(CCDirector::sharedDirector()->getWinSize() / 2);
            gifSpinner2->setID("paimon-loading-spinner"_spr);
            this->addChild(gifSpinner2, 100);
            Ref<geode::LoadingSpinner> loading = gifSpinner2;

            Ref<ProfilePage> gifUploadRef = this;

            ThumbnailAPI::get().uploadProfileGIF(accountID, gifData, username, [gifUploadRef, accountID, gifData, loading](bool success, std::string const& msg) {
                if (loading) loading->removeFromParent();
                
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
                        PaimonNotify::create("GIF submitted! Pending moderator verification.", NotificationIcon::Warning)->show();
                    } else {
                        PaimonNotify::create(Localization::get().getString("profile.saved").c_str(), NotificationIcon::Success)->show();
                    }
                    static_cast<PaimonProfilePage*>(gifUploadRef.data())->addOrUpdateProfileThumbOnPage(accountID);
                } else {
                    PaimonNotify::create("Upload failed: " + msg, NotificationIcon::Error)->show();
                }
            });
            return;
        }

        // imagen estatica: usar helper pa cargar
        auto loaded = ImageLoadHelper::loadStaticImage(path, 7);
        if (!loaded.success) {
            std::string errKey = loaded.error;
            if (errKey == "image_open_error" || errKey == "invalid_image_data" || errKey == "texture_error") {
                PaimonNotify::create(Localization::get().getString("profile." + errKey).c_str(), NotificationIcon::Error)->show();
            } else {
                PaimonNotify::create(errKey.c_str(), NotificationIcon::Error)->show();
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
                    PaimonNotify::create(Localization::get().getString("profile.saved").c_str(), NotificationIcon::Success)->show();
                    
                    this->addOrUpdateProfileThumbOnPage(accountID);
                }
            }
        );
        if (popup) popup->show();
    }

    // === FUNCIONES DE MuSICA DE PERFIL ===

    void onConfigureProfileMusic(CCObject*) {
        // Solo disponible en tu propio perfil
        if (!this->m_ownProfile) {
            PaimonNotify::create("You can only configure music on your own profile", NotificationIcon::Warning)->show();
            return;
        }

        // Abrir popup de configuracion de musica
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
            // Si no esta sonando, intentar reproducir
            musicManager.playProfileMusic(this->m_accountID);
            m_fields->m_musicPlaying = true;
            updatePauseButtonSprite(true);
        }
    }

    void updatePauseButtonSprite(bool isPlaying) {
        if (!m_fields->m_musicPauseBtn) return;

        // Cambiar el sprite segun el estado
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
        // Verificar si la musica de perfiles esta habilitada
        if (!ProfileMusicManager::get().isEnabled()) {
            m_fields->m_menuMusicPaused = false;
            return;
        }

        Ref<ProfilePage> self = this;
        // Obtener configuracion de musica del perfil
        ProfileMusicManager::get().getProfileMusicConfig(accountID, [self, accountID](bool success, const ProfileMusicManager::ProfileMusicConfig& config) {
            Loader::get()->queueInMainThread([self, success, config, accountID]() {
                if (!self->getParent()) return;
                auto* page = static_cast<PaimonProfilePage*>(self.data());

                if (!success || config.songID <= 0 || !config.enabled) {
                    if (page->m_fields->m_musicPauseBtn) {
                        page->m_fields->m_musicPauseBtn->setVisible(false);
                    }
                    page->m_fields->m_menuMusicPaused = false;
                    return;
                }

                // Hay musica, mostrar boton de pausa y recalcular layout
                if (page->m_fields->m_musicPauseBtn) {
                    page->m_fields->m_musicPauseBtn->setVisible(true);
                    if (auto* socialsMenu = page->getSocialsMenu()) {
                        socialsMenu->updateLayout();
                    }
                }

                // Reproducir musica automaticamente (usar overload con config
                // para evitar segunda consulta al servidor)
                ProfileMusicManager::get().playProfileMusic(accountID, config);
                page->m_fields->m_musicPlaying = true;
                page->updatePauseButtonSprite(true);
            });
        });
    }

    void keyBackClicked() override {
        ProfileMusicManager::get().stopProfileMusic();
        resumeMenuMusicIfNeeded();
        ProfilePage::keyBackClicked();
    }

    void onClose(CCObject* sender) {
        this->unschedule(schedule_selector(PaimonProfilePage::tickStyleBgs));
        ProfileMusicManager::get().stopProfileMusic();
        resumeMenuMusicIfNeeded();
        ProfilePage::onClose(sender);
    }

    void onExit() override {
        this->unschedule(schedule_selector(PaimonProfilePage::tickStyleBgs));
        if (!ProfileMusicManager::get().isFadingOut()) {
            ProfileMusicManager::get().stopProfileMusic();
        }
        resumeMenuMusicIfNeeded();
        ProfilePage::onExit();
    }

    // Restaura la musica del menu principal con fade-in suave
    // si no hubo musica de perfil (el ProfileMusicManager no toco el BG).
    void resumeMenuMusicIfNeeded() {
        if (!m_fields->m_menuMusicPaused) return;
        m_fields->m_menuMusicPaused = false;

        // Si ProfileMusicManager esta haciendo fade-out, el restaurara el BG con crossfade
        if (ProfileMusicManager::get().isFadingOut()) return;
        // Si hay musica de perfil activa, stopProfileMusic ya inicio el crossfade
        if (ProfileMusicManager::get().isPlaying()) return;

        // No hubo musica de perfil — restaurar BG con fade-in suave
        auto engine = FMODAudioEngine::sharedEngine();
        if (!engine || !engine->m_backgroundMusicChannel) return;

        bool isPaused = false;
        engine->m_backgroundMusicChannel->getPaused(&isPaused);
        if (isPaused) {
            // Si estaba pausado (ej: crossfade previo lo pauso), despausar con volumen 0 y subir
            engine->m_backgroundMusicChannel->setVolume(0.0f);
            engine->m_backgroundMusicChannel->setPaused(false);
        }

        // Fade-in gradual del BG
        float targetVol = engine->m_musicVolume;
        float currentVol = 0.0f;
        engine->m_backgroundMusicChannel->getVolume(&currentVol);
        if (currentVol >= targetVol * 0.95f) return; // ya esta a volumen normal

        // Ref<> mantiene vivo el objeto mientras el thread ejecuta
        Ref<ProfilePage> fadeSafeRef = this;
        fadeMenuMusicStep(fadeSafeRef, 0, 15, currentVol, targetVol);
    }

    void fadeMenuMusicStep(Ref<ProfilePage> safeRef, int step, int totalSteps, float fromVol, float toVol) {
        // Single-thread fade: one thread loops all steps instead of spawning 15+ threads.
        float stepMs = 500.0f / static_cast<float>(totalSteps);

        std::thread([safeRef, step, totalSteps, fromVol, toVol, stepMs]() {
            for (int s = step; s <= totalSteps; s++) {
                if (s > step) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(stepMs)));
                }

                int currentStep = s;
                Loader::get()->queueInMainThread([safeRef, currentStep, totalSteps, fromVol, toVol]() {
                    if (currentStep >= totalSteps) {
                        auto engine = FMODAudioEngine::sharedEngine();
                        if (engine && engine->m_backgroundMusicChannel) {
                            engine->m_backgroundMusicChannel->setVolume(toVol);
                        }
                        // Ref<> se destruye automaticamente al salir del scope
                        return;
                    }

                    float t = static_cast<float>(currentStep) / static_cast<float>(totalSteps);
                    float eT = (t < 0.5f) ? (2.0f * t * t) : (1.0f - std::pow(-2.0f * t + 2.0f, 2.0f) / 2.0f);
                    float vol = fromVol + (toVol - fromVol) * eT;

                    auto engine = FMODAudioEngine::sharedEngine();
                    if (engine && engine->m_backgroundMusicChannel) {
                        engine->m_backgroundMusicChannel->setVolume(std::max(0.0f, std::min(1.0f, vol)));
                    }
                });
            }
        }).detach();
    }
};

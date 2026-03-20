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
#include <vector>
#include <fstream>
#include <mutex>
#include <atomic>
#include <list>
#include "../utils/FileDialog.hpp"
#include "../managers/ThumbnailAPI.hpp"
#include "../features/capture/ui/CapturePreviewPopup.hpp"
#include "../features/moderation/ui/VerificationCenterLayer.hpp"
#include "../features/moderation/ui/AddModeratorPopup.hpp"
#include "../features/moderation/ui/BanUserPopup.hpp"
#include "../utils/Assets.hpp"
#include "../utils/PaimonButtonHighlighter.hpp"
#include "../utils/ImageConverter.hpp"
#include "../utils/HttpClient.hpp"
#include "../features/profile-music/services/ProfileMusicManager.hpp"
#include "../features/dynamic-songs/services/DynamicSongManager.hpp"
#include "../features/transitions/services/TransitionManager.hpp"
#include "../features/profile-music/ui/ProfileMusicPopup.hpp"
#include "../features/profiles/ui/RateProfilePopup.hpp"
#include "../features/profiles/ui/ProfileReviewsPopup.hpp"
#include "../utils/Shaders.hpp"
#include "../utils/ImageLoadHelper.hpp"
#include <Geode/ui/LoadingSpinner.hpp>
#include "../utils/PaimonNotification.hpp"
#include "../features/moderation/services/ModeratorCache.hpp"
#include "../utils/SpriteHelper.hpp"
#include "../framework/compat/SceneLocators.hpp"
#include "../utils/GIFDecoder.hpp"

using namespace geode::prelude;

// CCScale9Sprite::create crashea si el sprite no existe (no retorna nullptr).
// Usar paimon::SpriteHelper::safeCreateScale9() del header compartido.

// cache de texturas de profileimg para carga instantanea entre popups.
// Usa Ref<> para manejo automatico de refcount, con guardia de shutdown
// para evitar release() cuando el CCPoolManager ya este destruido.
static std::mutex s_profileImgMutex;
static std::unordered_map<int, geode::Ref<CCTexture2D>> s_profileImgCache;
static std::list<int> s_profileImgLru;
static std::unordered_map<int, std::list<int>::iterator> s_profileImgLruMap;
static constexpr size_t MAX_PROFILEIMG_CACHE_SIZE = 64;
static std::atomic<bool> s_profileImgShutdown{false};

static void touchProfileImgCache(int accountID) {
    auto it = s_profileImgLruMap.find(accountID);
    if (it != s_profileImgLruMap.end()) {
        s_profileImgLru.erase(it->second);
    }
    s_profileImgLru.push_back(accountID);
    s_profileImgLruMap[accountID] = std::prev(s_profileImgLru.end());
}

static void cacheProfileImgTexture(int accountID, CCTexture2D* texture) {
    if (!texture) return;
    std::lock_guard<std::mutex> lock(s_profileImgMutex);
    s_profileImgCache[accountID] = texture;
    touchProfileImgCache(accountID);
    while (s_profileImgCache.size() > MAX_PROFILEIMG_CACHE_SIZE && !s_profileImgLru.empty()) {
        int removeID = s_profileImgLru.front();
        s_profileImgLru.pop_front();
        s_profileImgLruMap.erase(removeID);
        s_profileImgCache.erase(removeID);
    }
}

// Limpiar el cache de profileimg durante el cierre del juego.
// Los destructores estaticos se ejecutan en orden indefinido y
// CCPoolManager puede ya estar muerto â€” usamos take() para sacar
// los Ref<> sin llamar release().
$on_game(Exiting) {
    s_profileImgShutdown.store(true, std::memory_order_release);
    std::lock_guard<std::mutex> lock(s_profileImgMutex);
    for (auto& [id, ref] : s_profileImgCache) {
        (void)ref.take();
    }
    s_profileImgCache.clear();
    s_profileImgLru.clear();
    s_profileImgLruMap.clear();
}

// Acceso externo al cache de profileimg (usado por InfoLayer hook).
CCTexture2D* getProfileImgCachedTexture(int accountID) {
    if (s_profileImgShutdown.load(std::memory_order_acquire)) return nullptr;
    std::lock_guard<std::mutex> lock(s_profileImgMutex);
    auto it = s_profileImgCache.find(accountID);
    if (it != s_profileImgCache.end()) {
        touchProfileImgCache(accountID);
        return it->second;
    }
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
    std::lock_guard<std::mutex> lock(s_profileImgMutex);
    s_profileImgCache.clear();
    s_profileImgLru.clear();
    s_profileImgLruMap.clear();
    std::error_code ec;
    auto dir = getProfileImgCacheDir();
    if (std::filesystem::exists(dir, ec)) {
        std::filesystem::remove_all(dir, ec);
    }
}

static CCTexture2D* loadProfileImgFromDisk(int accountID) {
    auto path = getProfileImgCachePath(accountID);
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) return nullptr;

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return nullptr;

    auto size = file.tellg();
    if (size <= 0) return nullptr;
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> data(static_cast<size_t>(size));
    if (!file.read(reinterpret_cast<char*>(data.data()), size)) return nullptr;
    file.close();

    if (GIFDecoder::isGIF(data.data(), data.size())) {
        auto gif = GIFDecoder::decode(data.data(), data.size());
        if (gif.frames.empty()) return nullptr;
        auto const& frame = gif.frames.front();
        if (frame.pixels.empty() || frame.width <= 0 || frame.height <= 0) return nullptr;

        auto* tex = new CCTexture2D();
        if (!tex->initWithData(
            frame.pixels.data(),
            kCCTexture2DPixelFormat_RGBA8888,
            frame.width,
            frame.height,
            CCSize(static_cast<float>(frame.width), static_cast<float>(frame.height))
        )) {
            tex->release();
            return nullptr;
        }
        tex->autorelease();
        return tex;
    }

    auto loaded = ImageLoadHelper::loadWithSTBFromMemory(data.data(), data.size());
    if (!loaded.success || !loaded.texture) {
        return nullptr;
    }
    loaded.texture->autorelease();
    return loaded.texture;
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
        // La pagina depende de node IDs estables para menus, fondos y overlays.
        (void)self.setHookPriorityAfterPost("ProfilePage::loadPageFromUserInfo", "geode.node-ids");
    }

    struct Fields {
        Ref<CCMenuItemSpriteExtra> m_gearBtn = nullptr;
        Ref<CCMenuItemSpriteExtra> m_addModBtn = nullptr;       // Boton para anadir moderadores (solo admins)
        Ref<CCMenuItemSpriteExtra> m_banBtn = nullptr;
        Ref<CCMenuItemSpriteExtra> m_musicBtn = nullptr;       // Boton para configurar musica (solo propio perfil)
        Ref<CCMenuItemSpriteExtra> m_musicPauseBtn = nullptr;  // Boton para pausar musica (todos los perfiles)
        Ref<CCMenuItemSpriteExtra> m_addProfileImgBtn = nullptr; // Boton para anadir imagen de perfil
        Ref<CCClippingNode> m_profileImgClip = nullptr;   // Clip de la imagen de perfil (fondo normal del popup)
        Ref<CCNode> m_profileImgBorder = nullptr;          // Borde de la imagen de perfil
        bool m_isApprovedMod = false;
        bool m_isAdmin = false;
        bool m_musicPlaying = false;  // Estado de reproduccion de musica
        bool m_menuMusicPaused = false; // Si pausamos la musica del menu al abrir
        // estado del fade de musica (reemplaza std::thread)
        int m_fadeStep = 0;
        int m_fadeTotalSteps = 0;
        float m_fadeFromVol = 0.0f;
        float m_fadeToVol = 0.0f;
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
                auto s = paimon::SpriteHelper::safeCreateWithFrameName("GJ_optionsBtn02_001.png");
                if (!s) s = paimon::SpriteHelper::safeCreateWithFrameName("GJ_optionsBtn_001.png");
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
                auto s = paimon::SpriteHelper::safeCreateWithFrameName("GJ_plus2Btn_001.png");
                if (!s) s = paimon::SpriteHelper::safeCreateWithFrameName("GJ_plusBtn_001.png");
                if (!s) s = paimon::SpriteHelper::safeCreateWithFrameName("GJ_button_01.png");
                return s;
            }
        );
        scaleToFit(addModSpr, 26.f);
        auto addModBtn = CCMenuItemSpriteExtra::create(addModSpr, this, menu_selector(PaimonProfilePage::onOpenAddModerator));
        addModBtn->setID("add-moderator-button"_spr);
        menu->addChild(addModBtn);
        m_fields->m_addModBtn = addModBtn;
    }

    // â”€â”€ Verificador periodico de integridad de botones â”€â”€
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
            auto reviewIcon = paimon::SpriteHelper::safeCreateWithFrameName("GJ_chatBtn_001.png");
            if (!reviewIcon) reviewIcon = paimon::SpriteHelper::safeCreateWithFrameName("GJ_plainBtn_001.png");
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

    // â”€â”€ Badge de moderador/admin en el perfil â”€â”€
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
                cacheProfileImgTexture(accountID, diskTex);
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
                cacheProfileImgTexture(accountID, texture);
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
    $override
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

    // â”€â”€ Helper: limpiar todos los botones paimon de un menu antes de re-crearlos â”€â”€
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

    // â”€â”€ Helper: obtener posicion/tamano del popup de perfil â”€â”€
    // Usamos el nodo "background" (asignado por node-ids) para centrar los menus
    CCPoint getPopupCenter() {
        if (!this->m_mainLayer) return CCDirector::sharedDirector()->getWinSize() / 2;
        auto geo = paimon::compat::InfoLayerLocator::findPopupGeometry(this->m_mainLayer);
        if (geo.found) return geo.center;
        return this->m_mainLayer->getContentSize() / 2;
    }

    CCSize getPopupSize() {
        if (!this->m_mainLayer) return {440.f, 290.f};
        auto geo = paimon::compat::InfoLayerLocator::findPopupGeometry(this->m_mainLayer);
        if (geo.found) return geo.size;
        return {440.f, 290.f};
    }

    // Hook: se llama cuando GD construye los paneles de iconos del perfil.
    // Geode node-ids asigna IDs aqui; es el momento mas fiable para ocultar icon-background.
    $override
    void loadPageFromUserInfo(GJUserScore* score) {
        ProfilePage::loadPageFromUserInfo(score);
        if (auto* layer = this->m_mainLayer) {
            styleProfileInternalBgs(layer);
        }

        if (!this->m_mainLayer) return;

        // â”€â”€ Referencia al popup â”€â”€
        auto popCenter = getPopupCenter();
        auto popSize = getPopupSize();

        // â”€â”€ Obtener o crear left-menu â”€â”€
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

        // â”€â”€ Limpiar botones paimon anteriores para evitar duplicados â”€â”€
        cleanPaimonButtons(menu);

        // â”€â”€ Boton de reviews (siempre visible) â”€â”€
        {
            auto reviewIcon = paimon::SpriteHelper::safeCreateWithFrameName("GJ_chatBtn_001.png");
            if (!reviewIcon) reviewIcon = paimon::SpriteHelper::safeCreateWithFrameName("GJ_plainBtn_001.png");
            if (reviewIcon) {
                scaleToFit(reviewIcon, 26.f);
                auto reviewBtn = CCMenuItemSpriteExtra::create(reviewIcon, this, menu_selector(PaimonProfilePage::onProfileReviews));
                reviewBtn->setID("profile-reviews-btn"_spr);
                menu->addChild(reviewBtn);
            }
        }

        // â”€â”€ Boton de calificar perfil en bottom-menu (solo perfiles ajenos) â”€â”€
        if (!this->m_ownProfile) {
            if (auto bottomMenu = this->m_mainLayer->getChildByIDRecursive("bottom-menu")) {
                if (!bottomMenu->getChildByID("rate-profile-btn"_spr)) {
                    auto bg = paimon::SpriteHelper::safeCreateScale9("GJ_button_04.png");
                    if (!bg) bg = paimon::SpriteHelper::safeCreateScale9("GJ_button_01.png");
                    if (bg) {
                    bg->setContentSize({30.f, 30.f});

                    auto starIcon = paimon::SpriteHelper::safeCreateWithFrameName("GJ_starsIcon_001.png");
                    if (!starIcon) starIcon = paimon::SpriteHelper::safeCreateWithFrameName("GJ_bigStar_001.png");
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
        }

        // â”€â”€ Boton de ban (solo mods/admins, no perfil propio) â”€â”€
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

        // â”€â”€ Botones de moderacion (solo perfil propio, segun rango verificado) â”€â”€
        if (this->m_ownProfile) {
            // Si ya esta verificado como mod o admin â†’ mostrar gear (centro de verificacion)
            if (m_fields->m_isApprovedMod || m_fields->m_isAdmin) {
                ensureGearButton(menu);
            }
            // Si es admin â†’ mostrar boton de anadir moderador
            if (m_fields->m_isAdmin) {
                ensureAddModeratorButton(menu);
            }
        }

        // â”€â”€ Recalcular layout del left-menu â”€â”€
        menu->updateLayout();

        // â”€â”€ Botones en socials-menu â”€â”€
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

        // â”€â”€ Anadir nuestros botones DESPUeS de los botones nativos de GD â”€â”€
        // Los botones de YouTube, Twitter, Twitch etc. ya estan en el socials-menu
        // por el hook de GD; nosotros solo anadimos al final.

        if (this->m_ownProfile) {
            {
                auto musicSpr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_musicOnBtn_001.png");
                if (!musicSpr) musicSpr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_playMusicBtn_001.png");
                if (!musicSpr) musicSpr = CCSprite::create();
                scaleToFit(musicSpr, 22.f);
                auto musicBtn = CCMenuItemSpriteExtra::create(musicSpr, this, menu_selector(PaimonProfilePage::onConfigureProfileMusic));
                musicBtn->setID("profile-music-button"_spr);
                socialsMenu->addChild(musicBtn);
                m_fields->m_musicBtn = musicBtn;
            }
            {
                auto imgSpr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_duplicateBtn_001.png");
                if (!imgSpr) imgSpr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_editBtn_001.png");
                if (!imgSpr) imgSpr = CCSprite::create();
                scaleToFit(imgSpr, 22.f);
                auto imgBtn = CCMenuItemSpriteExtra::create(imgSpr, this, menu_selector(PaimonProfilePage::onAddProfileImg));
                imgBtn->setID("add-profileimg-button"_spr);
                socialsMenu->addChild(imgBtn);
                m_fields->m_addProfileImgBtn = imgBtn;
            }
        }

        {
            auto pauseSpr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_pauseBtn_001.png");
            if (!pauseSpr) pauseSpr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_stopMusicBtn_001.png");
            if (!pauseSpr) pauseSpr = CCSprite::create();
            scaleToFit(pauseSpr, 20.f);
            auto pauseBtn = CCMenuItemSpriteExtra::create(pauseSpr, this, menu_selector(PaimonProfilePage::onToggleProfileMusic));
            pauseBtn->setID("profile-music-pause-button"_spr);
            pauseBtn->setVisible(false);
            socialsMenu->addChild(pauseBtn);
            m_fields->m_musicPauseBtn = pauseBtn;
        }

        socialsMenu->updateLayout();

        // â”€â”€ Badge de moderador/admin en el username â”€â”€
        if (score) {
            std::string badgeUsername = score->m_userName;

            bool isMod = false;
            bool isAdmin = false;
            if (moderatorCacheGet(badgeUsername, isMod, isAdmin)) {
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

        auto popupGeo = paimon::compat::InfoLayerLocator::findPopupGeometry(layer);
        if (popupGeo.found) {
            popupSize = popupGeo.size;
            popupCenter = popupGeo.center;
        }

        float padding = 3.f;
        CCSize imgArea = CCSize(popupSize.width - padding * 2.f, popupSize.height - padding * 2.f);

        // stencil con esquinas redondeadas — usa CCDrawNode para evitar
        // conflictos con HappyTextures/TextureLdr
        float clipW = imgArea.width;
        float clipH = imgArea.height;
        float r = 6.f;
        int segs = 8;
        auto stencil = CCDrawNode::create();
        std::vector<CCPoint> verts;
        // esquina inferior-izquierda
        for (int i = 0; i <= segs; i++) {
            float a = static_cast<float>(M_PI + (M_PI / 2.0) * i / segs);
            verts.push_back(ccp(r + r * cosf(a), r + r * sinf(a)));
        }
        // esquina inferior-derecha
        for (int i = 0; i <= segs; i++) {
            float a = static_cast<float>(3.0 * M_PI / 2.0 + (M_PI / 2.0) * i / segs);
            verts.push_back(ccp(clipW - r + r * cosf(a), r + r * sinf(a)));
        }
        // esquina superior-derecha
        for (int i = 0; i <= segs; i++) {
            float a = static_cast<float>((M_PI / 2.0) * i / segs);
            verts.push_back(ccp(clipW - r + r * cosf(a), clipH - r + r * sinf(a)));
        }
        // esquina superior-izquierda
        for (int i = 0; i <= segs; i++) {
            float a = static_cast<float>(M_PI / 2.0 + (M_PI / 2.0) * i / segs);
            verts.push_back(ccp(r + r * cosf(a), clipH - r + r * sinf(a)));
        }
        ccColor4F white = {1,1,1,1};
        stencil->drawPolygon(verts.data(), static_cast<int>(verts.size()), white, 0, white);

        auto clip = CCClippingNode::create();
        clip->setStencil(stencil);
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

    $override
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
                            bool effectiveMod = isApproved || isAdmin;
                            page->m_fields->m_isApprovedMod = effectiveMod;
                            page->m_fields->m_isAdmin = isAdmin;

                            // Guardar estado persistente
                            Mod::get()->setSavedValue("is-verified-moderator", effectiveMod);
                            Mod::get()->setSavedValue("is-verified-admin", isAdmin);

                            if (effectiveMod) {
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
                                if (effectiveMod) {
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

    void onAddProfileImg(CCObject*) {
        this->setTouchEnabled(false);
        Ref<ProfilePage> self = this;
        pt::openImageFileDialog([self](std::optional<std::filesystem::path> result) {
            self->setTouchEnabled(true);
            if (result.has_value()) {
                auto path = result.value();
                if (!path.empty()) {
                    static_cast<PaimonProfilePage*>(self.data())->processProfileImg(path);
                } else {
                    PaimonNotify::create(Localization::get().getString("profile.no_image_selected").c_str(), NotificationIcon::Warning)->show();
                }
            }
        });
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

                    if (GIFDecoder::isGIF(imgData.data(), imgData.size())) {
                        auto gif = GIFDecoder::decode(imgData.data(), imgData.size());
                        if (!gif.frames.empty()) {
                            auto const& frame = gif.frames.front();
                            if (!frame.pixels.empty() && frame.width > 0 && frame.height > 0) {
                                auto* tex = new CCTexture2D();
                                if (tex->initWithData(
                                    frame.pixels.data(),
                                    kCCTexture2DPixelFormat_RGBA8888,
                                    frame.width,
                                    frame.height,
                                    CCSize(static_cast<float>(frame.width), static_cast<float>(frame.height))
                                )) {
                                    tex->autorelease();
                                    cacheProfileImgTexture(accountID, tex);
                                    static_cast<PaimonProfilePage*>(imgGifSafeRef.data())->displayProfileImg(accountID, tex);
                                } else {
                                    tex->release();
                                }
                            }
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
        CC_SAFE_RETAIN(loaded.texture);

        int accountID = this->m_accountID;
        Ref<ProfilePage> previewCbRef = this;

        auto popup = CapturePreviewPopup::create(
            loaded.texture,
            accountID,
            loaded.buffer,
            loaded.width,
            loaded.height,
            [previewCbRef, accountID](bool ok, int id, std::shared_ptr<uint8_t> buf, int w, int h, std::string mode, std::string replaceId) {
                auto* page = static_cast<PaimonProfilePage*>(previewCbRef.data());
                if (!page || !page->getParent()) return;
                if (ok && buf) {
                    // convertir buffer RGBA a PNG en memoria (Unicode-safe, sin archivo temporal)
                    std::vector<uint8_t> pngData;
                    if (!ImageConverter::rgbaToPngBuffer(buf.get(), w, h, pngData)) {
                        return;
                    }

                        std::string username = GJAccountManager::get()->m_username;

                        auto pngSpinner = geode::LoadingSpinner::create(30.f);
                        pngSpinner->setPosition(CCDirector::sharedDirector()->getWinSize() / 2);
                        pngSpinner->setID("paimon-loading-spinner"_spr);
                        page->addChild(pngSpinner, 100);
                        Ref<geode::LoadingSpinner> loading = pngSpinner;

                        Ref<ProfilePage> imgUploadRef = previewCbRef;

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
                                    auto finalTex = geode::Ref<CCTexture2D>(new CCTexture2D());
                                    if (finalTex->initWithImage(&finalImg)) {
                                        // Ref<> hace retain/release automaticamente
                                        cacheProfileImgTexture(accountID, finalTex);
                                        static_cast<PaimonProfilePage*>(imgUploadRef.data())->displayProfileImg(accountID, finalTex);
                                    }
                                }
                            } else {
                                PaimonNotify::create("Upload failed: " + msg, NotificationIcon::Error)->show();
                            }
                        });
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
            newSpr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_pauseBtn_001.png");
            if (!newSpr) newSpr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_stopMusicBtn_001.png");
        } else {
            newSpr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_playBtn_001.png");
            if (!newSpr) newSpr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_playMusicBtn_001.png");
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

        auto& musicMgr = ProfileMusicManager::get();

        // â”€â”€ Optimistic: si hay config en cache y audio en disco, reproducir de inmediato â”€â”€
        auto* cached = musicMgr.getCachedConfig(accountID);
        if (cached && cached->songID > 0 && cached->enabled && musicMgr.isCached(accountID)) {
            if (m_fields->m_musicPauseBtn) {
                m_fields->m_musicPauseBtn->setVisible(true);
                if (auto* sm = getSocialsMenu()) sm->updateLayout();
            }
            musicMgr.playProfileMusic(accountID, *cached);
            m_fields->m_musicPlaying = true;
            updatePauseButtonSprite(true);
        }

        // â”€â”€ Siempre verificar config fresca del servidor en segundo plano â”€â”€
        Ref<ProfilePage> self = this;
        auto cachedCopy = cached ? std::optional<ProfileMusicManager::ProfileMusicConfig>(*cached)
                                 : std::nullopt;
        musicMgr.getProfileMusicConfig(accountID, [self, accountID, cachedCopy](bool success, const ProfileMusicManager::ProfileMusicConfig& config) {
            Loader::get()->queueInMainThread([self, success, config, accountID, cachedCopy]() {
                if (!self->getParent()) return;
                auto* page = static_cast<PaimonProfilePage*>(self.data());

                if (!success || config.songID <= 0 || !config.enabled) {
                    // Servidor dice que no hay musica â€” detener si estaba sonando desde cache
                    if (page->m_fields->m_musicPlaying) {
                        ProfileMusicManager::get().stopProfileMusic();
                        page->m_fields->m_musicPlaying = false;
                    }
                    if (page->m_fields->m_musicPauseBtn) {
                        page->m_fields->m_musicPauseBtn->setVisible(false);
                    }
                    page->m_fields->m_menuMusicPaused = false;
                    return;
                }

                // Si la config del servidor es igual a la cacheada y ya estamos sonando, no hacer nada
                bool configChanged = !cachedCopy.has_value()
                    || cachedCopy->songID != config.songID
                    || cachedCopy->startMs != config.startMs
                    || cachedCopy->endMs != config.endMs
                    || cachedCopy->updatedAt != config.updatedAt;

                if (!configChanged && page->m_fields->m_musicPlaying) {
                    return; // ya sonando con config correcta
                }

                // Hay musica (nueva o actualizada), mostrar boton y reproducir
                if (page->m_fields->m_musicPauseBtn) {
                    page->m_fields->m_musicPauseBtn->setVisible(true);
                    if (auto* socialsMenu = page->getSocialsMenu()) {
                        socialsMenu->updateLayout();
                    }
                }

                ProfileMusicManager::get().playProfileMusic(accountID, config);
                page->m_fields->m_musicPlaying = true;
                page->updatePauseButtonSprite(true);
            });
        });
    }

    $override
    void keyBackClicked() {
        ProfileMusicManager::get().stopProfileMusic();
        resumeMenuMusicIfNeeded();
        ProfilePage::keyBackClicked();
    }

    $override
    void onClose(CCObject* sender) {
        this->unschedule(schedule_selector(PaimonProfilePage::tickStyleBgs));
        this->unschedule(schedule_selector(PaimonProfilePage::verifyButtonIntegrity));
        ProfileMusicManager::get().stopProfileMusic();
        resumeMenuMusicIfNeeded();
        ProfilePage::onClose(sender);
    }

    $override
    void onExit() {
        this->unschedule(schedule_selector(PaimonProfilePage::tickStyleBgs));
        this->unschedule(schedule_selector(PaimonProfilePage::verifyButtonIntegrity));
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

        // Si DynamicSong esta activa, ella maneja su propio audio â€” no tocar BG
        if (DynamicSongManager::get()->m_isDynamicSongActive) return;

        // Si ProfileMusicManager esta haciendo fade-out, el restaurara el BG con crossfade
        if (ProfileMusicManager::get().isFadingOut()) return;
        // Si hay musica de perfil activa, stopProfileMusic ya inicio el crossfade
        if (ProfileMusicManager::get().isPlaying()) return;

        // No hubo musica de perfil â€” restaurar BG con fade-in suave
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
        // Fade progresivo usando el scheduler de Cocos2d en vez de std::thread
        // (norma Geode: prohibido std::thread, usar scheduler nativo o Task)
        float stepDelay = 500.0f / static_cast<float>(totalSteps) / 1000.0f; // a segundos

        // Aplicar el paso actual
        if (step >= totalSteps) {
            auto engine = FMODAudioEngine::sharedEngine();
            if (engine && engine->m_backgroundMusicChannel) {
                engine->m_backgroundMusicChannel->setVolume(toVol);
            }
            return;
        }

        float t = static_cast<float>(step) / static_cast<float>(totalSteps);
        float eT = (t < 0.5f) ? (2.0f * t * t) : (1.0f - std::pow(-2.0f * t + 2.0f, 2.0f) / 2.0f);
        float vol = fromVol + (toVol - fromVol) * eT;

        auto engine = FMODAudioEngine::sharedEngine();
        if (engine && engine->m_backgroundMusicChannel) {
            engine->m_backgroundMusicChannel->setVolume(std::max(0.0f, std::min(1.0f, vol)));
        }

        // Guardar estado en Fields para el callback del scheduler
        m_fields->m_fadeStep = step + 1;
        m_fields->m_fadeTotalSteps = totalSteps;
        m_fields->m_fadeFromVol = fromVol;
        m_fields->m_fadeToVol = toVol;

        // Programar el siguiente paso con scheduleOnce
        this->scheduleOnce(
            schedule_selector(PaimonProfilePage::fadeStepTick),
            stepDelay
        );
    }

    void fadeStepTick(float) {
        if (!this->getParent()) return;
        Ref<ProfilePage> safeRef = this;
        this->fadeMenuMusicStep(
            safeRef,
            m_fields->m_fadeStep,
            m_fields->m_fadeTotalSteps,
            m_fields->m_fadeFromVol,
            m_fields->m_fadeToVol
        );
    }
};

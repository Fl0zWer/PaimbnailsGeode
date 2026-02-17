#include <Geode/Geode.hpp>
#include <Geode/modify/ProfilePage.hpp>
#include <Geode/modify/CommentCell.hpp>
#include "../managers/ThumbnailAPI.hpp"
#include <Geode/binding/FLAlertLayer.hpp>

using namespace geode::prelude;

// cacheo el rol: user -> {mod, admin}
static std::map<std::string, std::pair<bool, bool>> g_moderatorCache;

// guardo profilepage por si hiciera falta (ahora no lo uso)
static ProfilePage* s_activeProfilePage = nullptr;

void showBadgeInfoPopup(CCNode* sender) {
    std::string title = "Unknown Rank";
    std::string desc = "No description available.";
    
    if (sender->getID() == "paimon-admin-badge") {
        title = "Paimbnails Admin";
        desc = "A <cj>Paimbnails Admin</c> is a developer or manager of the <cg>Paimbnails</c> mod. They have full control over the mod's infrastructure and content.";
    } else if (sender->getID() == "paimon-moderator-badge") {
        title = "Paimbnails Moderator";
        desc = "A <cj>Paimbnails Moderator</c> is a trusted user who helps review and manage thumbnails for the <cg>Paimbnails</c> mod. They ensure that content follows the guidelines.";
    }
    
    FLAlertLayer::create(title.c_str(), desc.c_str(), "OK")->show();
}

class $modify(BadgeCommentCell, CommentCell) {
    void onPaimonBadge(CCObject* sender) {
        if (auto node = typeinfo_cast<CCNode*>(sender)) {
            showBadgeInfoPopup(node);
        }
    }

    void loadFromComment(GJComment* comment) {
        CommentCell::loadFromComment(comment);
        
        if (!comment) return;
        std::string username = comment->m_userName;
        
        // primero mirar la cache
        if (g_moderatorCache.contains(username)) {
            auto [isMod, isAdmin] = g_moderatorCache[username];
            if (isMod || isAdmin) {
                this->addBadgeToComment(isMod, isAdmin);
            }
            return;
        }

        // si no hay cache, lo pido al server
        this->retain();
        ThumbnailAPI::get().checkUserStatus(username, [this, username](bool isMod, bool isAdmin) {
            g_moderatorCache[username] = {isMod, isAdmin};
            
            Loader::get()->queueInMainThread([this, username, isMod, isAdmin]() {
                // me aseguro de que sigue siendo el mismo user
                if (this->getParent() && this->m_comment && this->m_comment->m_userName == username) {
                     if (isMod || isAdmin) {
                        this->addBadgeToComment(isMod, isAdmin);
                    }
                }
                this->release();
            });
        });
    }

    void addBadgeToComment(bool isMod, bool isAdmin) {
        // pillo el menu del username
        auto menu = this->getChildByIDRecursive("username-menu");
        if (!menu) return;
        
        // si ya esta, no duplico
        if (menu->getChildByID("paimon-moderator-badge")) return;
        if (menu->getChildByID("paimon-admin-badge")) return;

        CCSprite* badgeSprite = nullptr;
        std::string badgeID;

        if (isAdmin) {
            badgeSprite = CCSprite::create("paim_Admin.png"_spr);
            badgeID = "paimon-admin-badge";
        } else if (isMod) {
            badgeSprite = CCSprite::create("paim_Moderador.png"_spr);
            badgeID = "paimon-moderator-badge";
        }

        if (!badgeSprite) return;

        // lo escalo pa que no moleste
        float targetHeight = 15.5f;
        float scale = targetHeight / badgeSprite->getContentSize().height;
        badgeSprite->setScale(scale);

        auto btn = CCMenuItemSpriteExtra::create(
            badgeSprite,
            this,
            menu_selector(BadgeCommentCell::onPaimonBadge)
        );
        btn->setID(badgeID);
        
        auto menuNode = static_cast<CCMenu*>(menu);
        
        // lo meto antes del porcentaje si existe
        if (auto percentage = this->getChildByIDRecursive("percentage-label")) {
            menuNode->insertBefore(btn, percentage);
        } else {
            menuNode->addChild(btn);
        }
        
        menuNode->updateLayout();
    }
};

class $modify(BadgeProfilePage, ProfilePage) {
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
        if (menu->getChildByID("paimon-moderator-badge")) return;
        if (menu->getChildByID("paimon-admin-badge")) return;

        CCSprite* badgeSprite = nullptr;
        std::string badgeID;

        if (isAdmin) {
            badgeSprite = CCSprite::create("paim_Admin.png"_spr);
            badgeID = "paimon-admin-badge";
        } else if (isMod) {
            badgeSprite = CCSprite::create("paim_Moderador.png"_spr);
            badgeID = "paimon-moderator-badge";
        }

        if (!badgeSprite) return;

        // log solo pa confirmar
        log::info("Adding badge (Clickable) - Admin: {}, Mod: {}", isAdmin, isMod);

        // lo escalo pa que encaje
        float targetHeight = 20.0f;
        float scale = targetHeight / badgeSprite->getContentSize().height;
        badgeSprite->setScale(scale);

        auto btn = CCMenuItemSpriteExtra::create(
            badgeSprite,
            this,
            menu_selector(BadgeProfilePage::onPaimonBadge)
        );
        btn->setID(badgeID);
        
        static_cast<CCMenu*>(menu)->addChild(btn);
        static_cast<CCMenu*>(menu)->updateLayout();
    }

    void loadPageFromUserInfo(GJUserScore* score) {
        ProfilePage::loadPageFromUserInfo(score);
        
        // ya no trackeo el profilepage global por seguridad
        // s_activeProfilePage = this;

        std::string username = score->m_userName;
        
        // miro cache primero (pa feedback rapido)
        bool cachedStatus = false;
        if (g_moderatorCache.contains(username)) {
            auto [isMod, isAdmin] = g_moderatorCache[username];
            if (isMod || isAdmin) {
                this->addModeratorBadge(isMod, isAdmin);
                cachedStatus = true;
            }
        }

        // igual lo pido al server pa tenerlo actualizado
        // retain pa no morirme en el async
        this->retain();
        ThumbnailAPI::get().checkUserStatus(username, [this, username](bool isMod, bool isAdmin) {
            // actualizo cache
            g_moderatorCache[username] = {isMod, isAdmin};
            
            Loader::get()->queueInMainThread([this, username, isMod, isAdmin]() {
                // compruebo que sigo vivo
                if (this->getParent()) {
                    if (isMod || isAdmin) {
                       this->addModeratorBadge(isMod, isAdmin);
                    }
                }
                
                // suelto el retain
                this->release();
            });
        });
    }

    void onExit() {
        // el puntero global ya no se usa
        // if (s_activeProfilePage == this) s_activeProfilePage = nullptr;
        ProfilePage::onExit();
    }
};

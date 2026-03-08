#include <Geode/Geode.hpp>
#include <Geode/modify/CommentCell.hpp>
#include "../managers/ThumbnailAPI.hpp"
#include <Geode/binding/FLAlertLayer.hpp>
#include <list>
#include "BadgeCache.hpp"

using namespace geode::prelude;

// definiciones de las variables y funciones declaradas en BadgeCache.hpp
std::map<std::string, std::pair<bool, bool>> g_moderatorCache;
std::list<std::string> g_moderatorCacheOrder;

void moderatorCacheInsert(std::string const& username, bool isMod, bool isAdmin) {
    // si ya existe, actualizo sin cambiar posicion
    if (g_moderatorCache.contains(username)) {
        g_moderatorCache[username] = {isMod, isAdmin};
        return;
    }
    // purgo la mitad mas antigua si superamos el limite
    while (g_moderatorCache.size() >= MAX_MODERATOR_CACHE && !g_moderatorCacheOrder.empty()) {
        auto const& oldest = g_moderatorCacheOrder.front();
        g_moderatorCache.erase(oldest);
        g_moderatorCacheOrder.pop_front();
    }
    g_moderatorCache[username] = {isMod, isAdmin};
    g_moderatorCacheOrder.push_back(username);
}

// funcion compartida para mostrar info del badge, accesible desde ProfilePage.cpp
void showBadgeInfoPopup(CCNode* sender) {
    std::string title = "Unknown Rank";
    std::string desc = "No description available.";
    
    if (sender->getID() == "paimon-admin-badge"_spr) {
        title = "Paimbnails Admin";
        desc = "A <cj>Paimbnails Admin</c> is a developer or manager of the <cg>Paimbnails</c> mod. They have full control over the mod's infrastructure and content.";
    } else if (sender->getID() == "paimon-moderator-badge"_spr) {
        title = "Paimbnails Moderator";
        desc = "A <cj>Paimbnails Moderator</c> is a trusted user who helps review and manage thumbnails for the <cg>Paimbnails</c> mod. They ensure that content follows the guidelines.";
    }
    
    FLAlertLayer::create(title.c_str(), desc.c_str(), "OK")->show();
}

class $modify(BadgeCommentCell, CommentCell) {
    static void onModify(auto& self) {
        (void)self.setHookPriorityPost("CommentCell::loadFromComment", geode::Priority::Late);
    }

    void onPaimonBadge(CCObject* sender) {
        if (auto node = typeinfo_cast<CCNode*>(sender)) {
            showBadgeInfoPopup(node);
        }
    }

    void loadFromComment(GJComment* comment) {
        CommentCell::loadFromComment(comment);
        
        if (!comment) return;
        std::string username = comment->m_userName;
        
        // primero busco en cache
        if (g_moderatorCache.contains(username)) {
            auto [isMod, isAdmin] = g_moderatorCache[username];
            if (isMod || isAdmin) {
                this->addBadgeToComment(isMod, isAdmin);
            }
            return;
        }

        // si no hay cache lo pido al server
        // uso Ref<> en vez de retain/release manual para evitar leak si el callback no llega
        Ref<CommentCell> safeRef = this;
        ThumbnailAPI::get().checkUserStatus(username, [safeRef, username](bool isMod, bool isAdmin) {
            moderatorCacheInsert(username, isMod, isAdmin);

            Loader::get()->queueInMainThread([safeRef, username, isMod, isAdmin]() {
                auto* self = static_cast<BadgeCommentCell*>(safeRef.data());
                // me aseguro de que sigue siendo el mismo usuario
                if (self->getParent() && self->m_comment && self->m_comment->m_userName == username) {
                     if (isMod || isAdmin) {
                        self->addBadgeToComment(isMod, isAdmin);
                    }
                }
            });
        });
    }

    void addBadgeToComment(bool isMod, bool isAdmin) {
        // busco el menu del username
        auto menu = this->getChildByIDRecursive("username-menu");
        if (!menu) return;
        
        // si ya existe no lo duplico
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

        // lo escalo para que no sea muy grande
        float targetHeight = 15.5f;
        float scale = targetHeight / badgeSprite->getContentSize().height;
        badgeSprite->setScale(scale);

        auto btn = CCMenuItemSpriteExtra::create(
            badgeSprite,
            this,
            menu_selector(BadgeCommentCell::onPaimonBadge)
        );
        btn->setID(badgeID);
        
        auto menuNode = typeinfo_cast<CCMenu*>(menu);
        if (!menuNode) return;

        // lo meto antes del porcentaje si existe
        if (auto percentage = this->getChildByIDRecursive("percentage-label")) {
            menuNode->insertBefore(btn, percentage);
        } else {
            menuNode->addChild(btn);
        }

        menuNode->updateLayout();
    }
};

// BadgeProfilePage se fusiono en PaimonProfilePage (ProfilePage.cpp)
// para evitar undefined behavior con doble $modify sobre la misma clase.


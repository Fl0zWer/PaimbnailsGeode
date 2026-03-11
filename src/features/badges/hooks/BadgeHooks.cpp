#include <Geode/Geode.hpp>
#include <Geode/modify/CommentCell.hpp>
#include "../../../managers/ThumbnailAPI.hpp"
#include <Geode/binding/FLAlertLayer.hpp>
#include <list>
#include <mutex>
#include "../../moderation/services/ModeratorCache.hpp"

using namespace geode::prelude;

// === Compatibilidad con BadgeCache.hpp (legacy) ===
// Las funciones libres se mantienen como wrappers que delegan a ModeratorCache,
// para que ProfilePage.cpp y cualquier otro consumidor sigan compilando
// sin necesidad de reescritura inmediata.

std::map<std::string, std::pair<bool, bool>> g_moderatorCache;
std::list<std::string> g_moderatorCacheOrder;

void moderatorCacheInsert(std::string const& username, bool isMod, bool isAdmin) {
    ModeratorCache::get().insert(username, isMod, isAdmin);
}

bool moderatorCacheGet(std::string const& username, bool& isMod, bool& isAdmin) {
    auto status = ModeratorCache::get().lookup(username);
    if (!status) return false;
    isMod = status->isMod;
    isAdmin = status->isAdmin;
    return true;
}

// funcion compartida para mostrar info del badge, accesible desde ProfilePage.cpp
void showBadgeInfoPopup(CCNode* sender) {
    if (!sender) return;

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

    $override
    void loadFromComment(GJComment* comment) {
        CommentCell::loadFromComment(comment);
        
        if (!comment) return;
        std::string username = comment->m_userName;
        
        // primero busco en cache
        bool isMod = false;
        bool isAdmin = false;
        if (moderatorCacheGet(username, isMod, isAdmin)) {
            if (isMod || isAdmin) {
                this->addBadgeToComment(isMod, isAdmin);
            }
            return;
        }

        // si no hay cache lo pido al server
        // WeakRef evita tocar celdas recicladas sin forzarles lifetime extra.
        WeakRef<BadgeCommentCell> weakSelf = this;
        ThumbnailAPI::get().checkUserStatus(username, [weakSelf, username](bool isMod, bool isAdmin) {
            moderatorCacheInsert(username, isMod, isAdmin);

            Loader::get()->queueInMainThread([weakSelf, username, isMod, isAdmin]() {
                auto self = weakSelf.lock();
                if (!self || !self->getParent() || !self->m_comment) return;

                // evita aplicar badge en una celda reciclada para otro comentario.
                if (std::string(self->m_comment->m_userName) != username) return;

                if (isMod || isAdmin) {
                    self->addBadgeToComment(isMod, isAdmin);
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


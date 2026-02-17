#include "AddModeratorPopup.hpp"
#include "../managers/ThumbnailAPI.hpp"
#include "../utils/HttpClient.hpp"
#include "../utils/Localization.hpp"
#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/utils/cocos.hpp>
#include <Geode/ui/GeodeUI.hpp>

using namespace geode::prelude;
using namespace cocos2d;

AddModeratorPopup* AddModeratorPopup::create(std::function<void(bool, const std::string&)> callback) {
    auto ret = new AddModeratorPopup();
    if (ret && ret->init(callback)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool AddModeratorPopup::init(std::function<void(bool, const std::string&)> callback) {
    if (!Popup::init(380.f, 290.f)) return false;
    
    m_callback = callback;
    
    this->setTitle(Localization::get().getString("addmod.title").c_str());

    auto content = m_mainLayer->getContentSize();

    // panel oscuro para la lista de moderadores
    auto panel = CCScale9Sprite::create("square02_001.png");
    panel->setColor({0, 0, 0});
    panel->setOpacity(70);
    panel->setContentSize(CCSizeMake(content.width - 30.f, 140.f));
    panel->setPosition({content.width / 2, content.height / 2 + 30.f});
    panel->setID("moderator-list-panel"_spr);
    m_mainLayer->addChild(panel);

    // contenedor de la lista (CCNode para no robar touches al input)
    m_listContainer = CCNode::create();
    m_listContainer->setPosition({0, 0});

    // scroll view para la lista
    m_scroll = cocos2d::extension::CCScrollView::create();
    m_scroll->setViewSize(panel->getContentSize());
    m_scroll->setPosition(panel->getPosition() - panel->getContentSize() / 2);
    m_scroll->setDirection(kCCScrollViewDirectionVertical);
    m_scroll->setContainer(m_listContainer);
    m_scroll->setID("moderator-scroll"_spr);
    m_mainLayer->addChild(m_scroll, 5);

    // etiqueta "nuevo moderador"
    auto addLabel = CCLabelBMFont::create(
        Localization::get().getString("addmod.enter_username_label").c_str(), 
        "bigFont.fnt"
    );
    addLabel->setScale(0.4f);
    addLabel->setPosition({content.width / 2, content.height / 2 - 55.f});
    addLabel->setID("add-label"_spr);
    m_mainLayer->addChild(addLabel);

    // input de username (TextInput de Geode, maneja touch correctamente)
    m_usernameInput = TextInput::create(content.width - 40.f, Localization::get().getString("addmod.enter_username"));
    m_usernameInput->setPosition({content.width / 2, content.height / 2 - 80.f});
    m_usernameInput->setFilter("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-");
    m_usernameInput->setMaxCharCount(20);
    m_usernameInput->setID("username-input"_spr);
    m_mainLayer->addChild(m_usernameInput, 11);

    // botones de cancelar y agregar
    auto cancelSpr = ButtonSprite::create(
        Localization::get().getString("general.cancel").c_str(),
        "goldFont.fnt", "GJ_button_01.png", 0.8f
    );
    cancelSpr->setScale(0.8f);
    auto cancelBtn = CCMenuItemSpriteExtra::create(
        cancelSpr, this, menu_selector(AddModeratorPopup::onClose)
    );
    cancelBtn->setPosition({content.width / 2 - 80.f, 28.f});
    cancelBtn->setID("cancel-btn"_spr);
    m_buttonMenu->addChild(cancelBtn);

    auto addSpr = ButtonSprite::create(
        Localization::get().getString("addmod.add_btn").c_str(),
        "goldFont.fnt", "GJ_button_01.png", 0.8f
    );
    addSpr->setScale(0.8f);
    auto addBtn = CCMenuItemSpriteExtra::create(
        addSpr, this, menu_selector(AddModeratorPopup::onAdd)
    );
    addBtn->setPosition({content.width / 2 + 80.f, 28.f});
    addBtn->setID("add-btn"_spr);
    m_buttonMenu->addChild(addBtn);
    
    // cargo la lista de moderadores actuales
    this->fetchAndShowModerators();

    return true;
}

void AddModeratorPopup::fetchAndShowModerators() {
    m_listContainer->removeAllChildren();
    auto loadLbl = CCLabelBMFont::create(
        Localization::get().getString("addmod.loading_mods").c_str(), 
        "goldFont.fnt"
    );
    loadLbl->setScale(0.4f);
    auto viewSize = m_scroll->getViewSize();
    loadLbl->setPosition({viewSize.width / 2, viewSize.height / 2});
    m_listContainer->addChild(loadLbl);
    m_listContainer->setContentSize(viewSize);

    WeakRef<AddModeratorPopup> self = this;
    HttpClient::get().getModerators([self](bool success, const std::vector<std::string>& moderators) {
        auto popup = self.lock();
        if (!popup) return;

        if (success) {
            popup->m_moderatorNames = moderators;
        } else {
            popup->m_moderatorNames.clear();
        }

        if (popup->getParent()) {
            popup->rebuildList();
        }
    });
}

void AddModeratorPopup::rebuildList() {
    m_listContainer->removeAllChildren();

    auto viewSize = m_scroll->getViewSize();

    if (m_moderatorNames.empty()) {
        auto lbl = CCLabelBMFont::create(
            Localization::get().getString("addmod.no_mods").c_str(), 
            "goldFont.fnt"
        );
        lbl->setScale(0.4f);
        lbl->setPosition({viewSize.width / 2, viewSize.height / 2});
        m_listContainer->addChild(lbl);
        m_listContainer->setContentSize(viewSize);
        return;
    }

    constexpr float cellHeight = 40.f;
    constexpr float cellGap = 8.f;

    // calculo la altura total necesaria antes de layout
    float totalH = (cellHeight * m_moderatorNames.size()) + (cellGap * (m_moderatorNames.size() - 1));
    if (totalH < viewSize.height) totalH = viewSize.height;

    m_listContainer->setContentSize({viewSize.width, totalH});

    // construyo celdas manualmente (posicion absoluta, sin autoScale que comprima)
    float yPos = totalH - cellHeight / 2;
    for (const auto& modName : m_moderatorNames) {
        auto cell = CCNode::create();
        cell->setContentSize({viewSize.width - 10.f, cellHeight});
        cell->setAnchorPoint({0.5f, 0.5f});
        cell->setPosition({viewSize.width / 2, yPos});
        cell->setID("mod-cell"_spr);

        // fondo de celda
        auto bg = CCScale9Sprite::create("square02_001.png");
        bg->setColor({0, 0, 0});
        bg->setOpacity(55);
        bg->setContentSize(cell->getContentSize());
        bg->setPosition(cell->getContentSize() / 2);
        cell->addChild(bg);

        // nombre del moderador
        auto name = CCLabelBMFont::create(modName.c_str(), "chatFont.fnt");
        name->setScale(0.6f);
        name->setAnchorPoint({0, 0.5f});
        name->setPosition({12.f, cell->getContentHeight() / 2});
        cell->addChild(name);

        // menu para boton de quitar
        auto btnMenu = CCMenu::create();
        btnMenu->setContentSize({80.f, cellHeight});
        btnMenu->setPosition({cell->getContentWidth() - 50.f, cell->getContentHeight() / 2});
        btnMenu->setLayout(RowLayout::create()->setGap(4.f));
        cell->addChild(btnMenu);

        // boton rojo de quitar
        auto removeSpr = ButtonSprite::create(
            Localization::get().getString("addmod.remove_btn").c_str(), 
            50, true, "goldFont.fnt", "GJ_button_06.png", 28.f, 0.5f
        );
        removeSpr->setScale(0.75f);
        auto removeBtn = CCMenuItemSpriteExtra::create(
            removeSpr, this, menu_selector(AddModeratorPopup::onRemove)
        );
        removeBtn->setUserObject(CCString::create(modName));
        removeBtn->setID("remove-btn"_spr);
        btnMenu->addChild(removeBtn);

        btnMenu->updateLayout();
        m_listContainer->addChild(cell);

        yPos -= (cellHeight + cellGap);
    }

    m_listContainer->setPosition({0, viewSize.height - totalH});
}

void AddModeratorPopup::onAdd(CCObject*) {
    std::string username = m_usernameInput->getString();
    
    if (username.empty()) {
        Notification::create(
            Localization::get().getString("addmod.enter_username").c_str(), 
            NotificationIcon::Warning
        )->show();
        return;
    }
    
    auto gm = GameManager::get();
    std::string adminUser = gm->m_playerName;
    
    m_loadingCircle = LoadingCircle::create();
    if (m_loadingCircle) {
        m_loadingCircle->setParentLayer(this);
        m_loadingCircle->show();
    }
    
    this->retain();
    
    ThumbnailAPI::get().addModerator(username, adminUser, [this, username](bool success, const std::string& message) {
        if (m_loadingCircle && m_loadingCircle->getParent()) {
            m_loadingCircle->fadeAndRemove();
        }
        m_loadingCircle = nullptr;

        if (success) {
            Notification::create(
                Localization::get().getString("addmod.success_msg").c_str(),
                NotificationIcon::Success
            )->show();
            
            m_usernameInput->setString("");
            
            if (m_callback) m_callback(true, username);
            
            this->fetchAndShowModerators();
        } else {
            createQuickPopup(
                Localization::get().getString("addmod.error_title").c_str(),
                message.empty() ? Localization::get().getString("addmod.error_msg") : message,
                "OK", nullptr, nullptr
            );
        }
        
        this->release();
    });
}

void AddModeratorPopup::onRemove(CCObject* sender) {
    auto btn = typeinfo_cast<CCMenuItemSpriteExtra*>(sender);
    if (!btn) return;
    auto strObj = typeinfo_cast<CCString*>(btn->getUserObject());
    if (!strObj) return;
    
    std::string username = strObj->getCString();

    Ref<AddModeratorPopup> self = this;
    createQuickPopup(
        Localization::get().getString("addmod.remove_confirm_title").c_str(),
        fmt::format(
            fmt::runtime(Localization::get().getString("addmod.remove_confirm_msg")), 
            username
        ),
        Localization::get().getString("general.cancel").c_str(),
        Localization::get().getString("addmod.remove_btn").c_str(),
        [self, username](auto, bool btn2) {
            if (!btn2) return;

            auto gm = GameManager::get();
            std::string adminUser = gm->m_playerName;

            self->m_loadingCircle = LoadingCircle::create();
            if (self->m_loadingCircle) {
                self->m_loadingCircle->setParentLayer(self);
                self->m_loadingCircle->show();
            }

            ThumbnailAPI::get().removeModerator(username, adminUser, [self, username](bool success, const std::string& message) {
                if (self->m_loadingCircle && self->m_loadingCircle->getParent()) {
                    self->m_loadingCircle->fadeAndRemove();
                }
                self->m_loadingCircle = nullptr;

                if (success) {
                    Notification::create(
                        Localization::get().getString("addmod.remove_success").c_str(),
                        NotificationIcon::Success
                    )->show();

                    if (self->m_callback) self->m_callback(true, username);

                    self->fetchAndShowModerators();
                } else {
                    Notification::create(
                        message.empty() 
                            ? Localization::get().getString("addmod.remove_error").c_str() 
                            : message.c_str(),
                        NotificationIcon::Error
                    )->show();
                }
            });
        }
    );
}

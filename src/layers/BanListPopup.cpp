#include "BanListPopup.hpp"

#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/cocos/extensions/GUI/CCScrollView/CCScrollView.h>
#include <Geode/utils/cocos.hpp>

#include "../utils/HttpClient.hpp"
#include "../utils/Localization.hpp"

using namespace geode::prelude;
using namespace cocos2d;

BanListPopup* BanListPopup::create() {
    auto ret = new BanListPopup();
    if (ret && ret->init()) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool BanListPopup::init() {
    if (!Popup::init(360.f, 260.f)) return false;

    this->setTitle(Localization::get().getString("ban.list.title"));

    auto content = this->m_mainLayer->getContentSize();

    auto panel = CCScale9Sprite::create("square02_001.png");
    panel->setColor({0, 0, 0});
    panel->setOpacity(70);
    panel->setContentSize(CCSizeMake(content.width - 20.f, content.height - 60.f));
    panel->setPosition({content.width / 2, content.height / 2 - 10.f});
    this->m_mainLayer->addChild(panel);

    m_listMenu = CCMenu::create();
    m_listMenu->setPosition({0, 0});

    m_scroll = cocos2d::extension::CCScrollView::create();
    m_scroll->setViewSize(panel->getContentSize());
    m_scroll->setPosition(panel->getPosition() - panel->getContentSize() / 2);
    m_scroll->setDirection(kCCScrollViewDirectionVertical);
    m_scroll->setContainer(m_listMenu);
    this->m_mainLayer->addChild(m_scroll, 5);

    // estado de carga inicial
    {
        auto lbl = CCLabelBMFont::create(Localization::get().getString("ban.list.loading").c_str(), "goldFont.fnt");
        lbl->setScale(0.45f);
        lbl->setPosition({panel->getContentSize().width / 2, panel->getContentSize().height / 2});
        m_listMenu->addChild(lbl);
        m_listMenu->setContentSize(panel->getContentSize());
    }

    // obtengo lista de baneados
    // uso weakref para evitar crashes y memory management manual
    WeakRef<BanListPopup> self = this;
    HttpClient::get().getBanList([self](bool success, const std::string& jsonData) {
        // si el popup murio, no hago nada
        auto popup = self.lock();
        if (!popup) return;

        std::vector<std::string> users;

        if (success) {
            try {
                auto parsed = matjson::parse(jsonData);
                if (parsed.isOk()) {
                    auto root = parsed.unwrap();
                    if (root.isObject()) {
                        auto banned = root["banned"];
                        if (banned.isArray()) {
                            for (auto const& v : banned.asArray().unwrap()) {
                                if (v.isString()) {
                                    users.push_back(v.asString().unwrap());
                                }
                            }
                        }

                        if (root.contains("details") && root["details"].isObject()) {
                            for (auto const& val : root["details"]) {
                                if (val.isObject()) {
                                    auto keyOpt = val.getKey();
                                    if (!keyOpt) continue;
                                    std::string key = *keyOpt;

                                    BanDetail d;
                                    if (val.contains("reason") && val["reason"].isString()) 
                                        d.reason = val["reason"].asString().unwrap();
                                    if (val.contains("bannedBy") && val["bannedBy"].isString()) 
                                        d.bannedBy = val["bannedBy"].asString().unwrap();
                                    if (val.contains("date") && val["date"].isString()) 
                                        d.date = val["date"].asString().unwrap();
                                    
                                    popup->m_banDetails[key] = d;
                                }
                            }
                        }
                    }
                }
            } catch (...) {
                // ignoro errores de parseo; muestro vacio
            }
        }

        if (popup->getParent()) {
            popup->rebuildList(users);
        }
    });

    return true;
}

void BanListPopup::rebuildList(const std::vector<std::string>& users) {
    m_listMenu->removeAllChildren();
    
    // uso columnlayout para la lista
    m_listMenu->setLayout(ColumnLayout::create()->setGap(5.f)->setAxisReverse(true)->setAxisAlignment(AxisAlignment::End));

    auto viewSize = m_scroll->getViewSize();

    if (users.empty()) {
        auto lbl = CCLabelBMFont::create(Localization::get().getString("ban.list.empty").c_str(), "goldFont.fnt");
        lbl->setScale(0.5f);
        lbl->setPosition({viewSize.width / 2, viewSize.height / 2});
        m_listMenu->addChild(lbl);
        return;
    }

    for (const auto& user : users) {
        auto cellContainer = CCNode::create();
        cellContainer->setContentSize({viewSize.width - 20.f, 30.f});
        cellContainer->setID("user-cell");

        // fondo semitransparente
        auto bg = CCScale9Sprite::create("square02_001.png");
        bg->setColor({0, 0, 0});
        bg->setOpacity(55);
        bg->setContentSize(cellContainer->getContentSize());
        bg->setPosition(cellContainer->getContentSize() / 2);
        cellContainer->addChild(bg);

        // nombre del usuario
        auto name = CCLabelBMFont::create(user.c_str(), "chatFont.fnt");
        name->setScale(0.5f);
        name->setAnchorPoint({0, 0.5f});
        name->setPosition({10.f, cellContainer->getContentHeight() / 2});
        cellContainer->addChild(name);

        // menu interno para botones
        auto btnMenu = CCMenu::create();
        btnMenu->setContentSize({100.f, 30.f});
        btnMenu->setPosition({cellContainer->getContentWidth() - 60.f, cellContainer->getContentHeight() / 2});
        btnMenu->setLayout(RowLayout::create()->setGap(10.f));
        cellContainer->addChild(btnMenu);

        // boton de info
        auto infoSpr = CCSprite::createWithSpriteFrameName("GJ_infoIcon_001.png");
        infoSpr->setScale(0.6f);
        auto infoBtn = CCMenuItemSpriteExtra::create(infoSpr, this, menu_selector(BanListPopup::onInfo));
        infoBtn->setUserObject(CCString::create(user));
        btnMenu->addChild(infoBtn);

        // boton de unban
        auto unbanSpr = ButtonSprite::create(Localization::get().getString("ban.list.unban_btn").c_str(), 50, true, "goldFont.fnt", "GJ_button_05.png", 30.f, 0.6f);
        unbanSpr->setScale(0.7f);
        auto unbanBtn = CCMenuItemSpriteExtra::create(unbanSpr, this, menu_selector(BanListPopup::onUnban));
        unbanBtn->setUserObject(CCString::create(user));
        btnMenu->addChild(unbanBtn);
        
        btnMenu->updateLayout();
        
        m_listMenu->addChild(cellContainer);
    }
    
    // fuerzo layout y height
    m_listMenu->updateLayout();
    
    float totalH = m_listMenu->getContentHeight();
     if (totalH < viewSize.height) {
        m_listMenu->setContentHeight(viewSize.height);
        m_listMenu->updateLayout();
    }
    
    // m_contentlayer es de scrolllayer, ccscrollview usa getcontainer()
    // pero como asigno m_listmenu como contenedor
    m_listMenu->setPosition({0,viewSize.height - m_listMenu->getContentHeight()});
}

void BanListPopup::onInfo(CCObject* sender) {
    auto btn = typeinfo_cast<CCMenuItemSpriteExtra*>(sender);
    if (!btn) return;
    auto strObj = typeinfo_cast<CCString*>(btn->getUserObject());
    if (!strObj) return;
    
    std::string username = strObj->getCString();
    std::string body = Localization::get().getString("ban.info.no_info");
    
    if (m_banDetails.count(username)) {
        auto& d = m_banDetails[username];
        body = fmt::format(
            "{}: <cy>{}</c>\n"
            "{}: <cg>{}</c>\n"
            "{}: <cl>{}</c>",
            Localization::get().getString("ban.info.reason"), d.reason.empty() ? "N/A" : d.reason,
            Localization::get().getString("ban.info.by"), d.bannedBy.empty() ? "N/A" : d.bannedBy,
            Localization::get().getString("ban.info.date"), d.date.empty() ? "N/A" : d.date
        );
    }
    
    geode::createQuickPopup(Localization::get().getString("ban.info.title").c_str(), body, "OK", nullptr, nullptr);
}

void BanListPopup::onUnban(CCObject* sender) {
    auto btn = typeinfo_cast<CCMenuItemSpriteExtra*>(sender);
    if (!btn) return;
    auto strObj = typeinfo_cast<CCString*>(btn->getUserObject());
    if (!strObj) return;
    
    std::string username = strObj->getCString();

    Ref<BanListPopup> self = this;
    geode::createQuickPopup(
        Localization::get().getString("ban.unban.title").c_str(),
        fmt::format(fmt::runtime(Localization::get().getString("ban.unban.confirm")), username),
        "Cancel", Localization::get().getString("ban.list.unban_btn").c_str(),
        [self, username](auto, bool btn2) {
            if (btn2) {
                HttpClient::get().unbanUser(username, [self](bool success, const std::string& msg) {
                    if (success) {
                        Notification::create(Localization::get().getString("ban.unban.success"), NotificationIcon::Success)->show();
                        self->onClose(nullptr);
                    } else {
                        Notification::create(Localization::get().getString("ban.unban.error"), NotificationIcon::Error)->show();
                    }
                });
            }
        }
    );
}

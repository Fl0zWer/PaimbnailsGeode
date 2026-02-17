#include "VerificationQueuePopup.hpp"
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include "../utils/PaimonButtonHighlighter.hpp"
#include <Geode/utils/cocos.hpp>
#include <Geode/loader/Mod.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/LevelInfoLayer.hpp>
#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/ProfilePage.hpp>
#include "../managers/LocalThumbs.hpp"
#include "../managers/ThumbnailAPI.hpp"
#include "../managers/ThumbnailLoader.hpp"
#include <Geode/cocos/extensions/GUI/CCScrollView/CCScrollView.h>
#include <algorithm>
#include "../utils/Localization.hpp"
#include "BanListPopup.hpp"
#include "../utils/AnimatedGIFSprite.hpp"

using namespace geode::prelude;
using namespace cocos2d;

// forward, popup banner (def en LevelInfoLayer)
class BannerViewPopup : public geode::Popup {
protected:
    bool init(CCNode* content) {
        if (!Popup::init(420.f, 320.f)) return false;

        this->setTitle("Banner Preview");
        
        if (content) {
            content->setPosition(m_mainLayer->getContentSize() / 2);
            m_mainLayer->addChild(content);
            
            // limitar tamaño
            float maxW = 380.f;
            float maxH = 250.f;
            
            if (content->getContentWidth() > 0 && content->getContentHeight() > 0) {
                float scaleX = maxW / content->getContentWidth();
                float scaleY = maxH / content->getContentHeight();
                float scale = std::min(scaleX, scaleY);
                content->setScale(scale);
            }
        }
        
        auto btn = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("Close"),
            this,
            menu_selector(BannerViewPopup::onClose)
        );
        auto menu = CCMenu::create();
        menu->addChild(btn);
        menu->setPosition({m_mainLayer->getContentSize().width / 2, 25.f});
        m_mainLayer->addChild(menu);
        
        return true;
    }
public:
    static BannerViewPopup* create(CCNode* content) {
        auto ret = new BannerViewPopup();
        if (ret && ret->init(content)) {
            ret->autorelease();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }
};

extern CCNode* createThumbnailViewPopup(int32_t levelID, bool canAcceptUpload, const std::vector<Suggestion>& suggestions = {});

VerificationQueuePopup* VerificationQueuePopup::create() {
    auto ret = new VerificationQueuePopup();
    if (ret && ret->init()) { ret->autorelease(); return ret; }
    CC_SAFE_DELETE(ret); return nullptr;
}

bool VerificationQueuePopup::init() {
    if (!Popup::init(470.f, 300.f)) return false;

    this->setTitle(Localization::get().getString("queue.title"));
    auto content = this->m_mainLayer->getContentSize();

    // pestañas
    m_tabsMenu = CCMenu::create();
    m_tabsMenu->setPosition({content.width/2, content.height - 45.f});
    auto mkTab = [&](const char* title, SEL_MenuHandler sel){
        auto spr = ButtonSprite::create(title, 90, true, "bigFont.fnt", "GJ_button_01.png", 30.f, 0.6f);
        spr->setScale(0.8f);
        return CCMenuItemSpriteExtra::create(spr, this, sel);
    };
    auto t1 = mkTab(Localization::get().getString("queue.verify_tab").c_str(), menu_selector(VerificationQueuePopup::onTabVerify));
    auto t2 = mkTab(Localization::get().getString("queue.update_tab").c_str(), menu_selector(VerificationQueuePopup::onTabUpdate));
    auto t3 = mkTab(Localization::get().getString("queue.report_tab").c_str(), menu_selector(VerificationQueuePopup::onTabReport));
    auto t4 = mkTab("Banners", menu_selector(VerificationQueuePopup::onTabBanner));
    t1->setTag((int)PendingCategory::Verify);
    t2->setTag((int)PendingCategory::Update);
    t3->setTag((int)PendingCategory::Report);
    t4->setTag((int)PendingCategory::Banner);
    m_tabsMenu->addChild(t1); m_tabsMenu->addChild(t2); m_tabsMenu->addChild(t3); m_tabsMenu->addChild(t4);

    // btn baneados
    {
        auto btnSpr = ButtonSprite::create(Localization::get().getString("queue.banned_btn").c_str(), 80, true, "bigFont.fnt", "GJ_button_05.png", 30.f, 0.6f);
        btnSpr->setScale(0.8f);
        auto btn = CCMenuItemSpriteExtra::create(btnSpr, this, menu_selector(VerificationQueuePopup::onViewBans));
        btn->setID("banned-users-btn"_spr);
        m_tabsMenu->addChild(btn);
    }

    m_tabsMenu->setLayout(RowLayout::create()->setGap(5.f)->setAxisAlignment(AxisAlignment::Center));
    this->m_mainLayer->addChild(m_tabsMenu);

    // fondo de lista y scroll
    auto panel = CCScale9Sprite::create("square02_001.png");
    panel->setColor({0,0,0}); panel->setOpacity(70);
    panel->setContentSize(CCSizeMake(content.width - 20.f, content.height - 95.f));
    panel->setPosition({content.width/2, content.height/2 - 15.f});
    this->m_mainLayer->addChild(panel);

    m_listMenu = CCMenu::create(); m_listMenu->setPosition({0,0});
    m_scroll = cocos2d::extension::CCScrollView::create();
    m_scroll->setViewSize(panel->getContentSize());
    m_scroll->setPosition(panel->getPosition() - panel->getContentSize()/2);
    m_scroll->setDirection(kCCScrollViewDirectionVertical);
    m_scroll->setContainer(m_listMenu);
    this->m_mainLayer->addChild(m_scroll, 5);

    switchTo(PendingCategory::Verify);
    return true;
}

void VerificationQueuePopup::onViewBans(cocos2d::CCObject*) {
    if (auto popup = BanListPopup::create()) {
        popup->show();
    }
}

void VerificationQueuePopup::switchTo(PendingCategory cat) {
    m_current = cat;
    // resaltar tab activa
    for (auto* n : CCArrayExt<CCNode*>(m_tabsMenu->getChildren())) {
        auto* it = static_cast<CCMenuItemSpriteExtra*>(n);
        bool active = it->getTag() == (int)cat;
        it->setScale(active ? 0.85f : 0.75f);
    }
    
    m_listMenu->removeAllChildren();
    
    // texto de carga centrado
    auto loadLbl = CCLabelBMFont::create("Loading...", "goldFont.fnt");
    loadLbl->setScale(0.6f);
    loadLbl->setPosition(m_scroll->getViewSize() / 2);
    m_listMenu->addChild(loadLbl);
    
    // sincronizar con el servidor antes de reconstruir la lista
    this->retain();
    ThumbnailAPI::get().syncVerificationQueue(cat, [this, cat](bool success, const std::vector<PendingItem>& items) {
        if (!success) {
            log::warn("[VerificationQueuePopup] Failed to sync queue from server, using local data");
            m_items = PendingQueue::get().list(cat);
        } else {
            m_items = items;
        }
        if (this->getParent()) {
            rebuildList();
        }
        this->release();
    });
}

void VerificationQueuePopup::rebuildList() {
    try {
        m_listMenu->removeAllChildren();
        auto contentSize = m_scroll->getViewSize();
        
        auto const& items = m_items;
        
        // username pa reclamo
        std::string currentUsername;
        if (auto gm = GameManager::get()) {
            currentUsername = gm->m_playerName;
        }

        if (items.empty()) {
            auto lbl = CCLabelBMFont::create(Localization::get().getString("queue.no_items").c_str(), "goldFont.fnt");
            lbl->setScale(0.5f); lbl->setPosition({contentSize.width/2, contentSize.height/2});
            m_listMenu->addChild(lbl);
            m_listMenu->setContentSize(contentSize);
            return;
        }
        float rowH = 50.f;
        float totalH = rowH * items.size() + 20.f;
        m_listMenu->setContentSize(CCSizeMake(contentSize.width, std::max(contentSize.height, totalH)));
        float left = 14.f; float right = contentSize.width - 80.f;
        for (size_t i=0;i<items.size();++i) {
            auto const& r = items[i];
            float y = m_listMenu->getContentSize().height - 30.f - (float)i * rowH;
            auto rowBg = CCScale9Sprite::create("square02_001.png");
            rowBg->setColor({0,0,0}); rowBg->setOpacity(60);
            rowBg->setContentSize(CCSizeMake(contentSize.width - 39.f, 44.f));
            rowBg->setAnchorPoint({0,0.5f}); rowBg->setPosition({left - 5.f, y});
            m_listMenu->addChild(rowBg);

            // etiqueta de ID
            std::string idText = (m_current == PendingCategory::Banner) 
                ? fmt::format("Account ID: {}", r.levelID)
                : fmt::format(fmt::runtime(Localization::get().getString("queue.level_id")), r.levelID);
            auto idLbl = CCLabelBMFont::create(idText.c_str(), "goldFont.fnt");
            idLbl->setScale(0.45f); idLbl->setAnchorPoint({0,0.5f}); idLbl->setPosition({left, y + 2.f});
            m_listMenu->addChild(idLbl);
            
            // estado reclamo
            bool isClaimed = !r.claimedBy.empty();
            bool claimedByMe = isClaimed && (r.claimedBy == currentUsername);
            bool canInteract = claimedByMe; // solo si reclamo yo

            // nombre reclamador
            if (isClaimed) {
                std::string claimText = claimedByMe ? Localization::get().getString("queue.claimed_by_you") : fmt::format(fmt::runtime(Localization::get().getString("queue.claimed_by_user")), r.claimedBy);
                auto claimLbl = CCLabelBMFont::create(claimText.c_str(), "chatFont.fnt");
                claimLbl->setScale(0.4f);
                claimLbl->setColor(claimedByMe ? ccColor3B{100, 255, 100} : ccColor3B{255, 100, 100});
                claimLbl->setAnchorPoint({0, 0.5f});
                claimLbl->setPosition({left + 90.f, y + 2.f});
                m_listMenu->addChild(claimLbl);
            }

            // popup Ver en vez de thumb inline

            auto btnMenu = CCMenu::create(); btnMenu->setPosition({0,0});
            
            // btn reclamo: yo=verde, otro=disabled, ninguno=default
            const char* claimBtnImg = claimedByMe ? "GJ_button_02.png" : "GJ_button_04.png";
            auto claimSpr = ButtonSprite::create("✋", 35, true, "bigFont.fnt", claimBtnImg, 30.f, 0.6f);
            claimSpr->setScale(0.45f);
            auto claimBtn = CCMenuItemSpriteExtra::create(claimSpr, this, menu_selector(VerificationQueuePopup::onClaimLevel));
            claimBtn->setTag(r.levelID);
            claimBtn->setID(fmt::format("claim-btn-{}", r.levelID));
            
            if (isClaimed && !claimedByMe) {
                claimBtn->setEnabled(false);
                claimSpr->setColor({100, 100, 100});
            }
            
            PaimonButtonHighlighter::registerButton(claimBtn);
            btnMenu->addChild(claimBtn);
            
            // atenuar btns
            auto setupBtn = [&](CCMenuItemSpriteExtra* btn, ButtonSprite* spr) {
                if (!canInteract) {
                    btn->setEnabled(false);
                    spr->setColor({100, 100, 100});
                    spr->setOpacity(150);
                }
                PaimonButtonHighlighter::registerButton(btn);
                btnMenu->addChild(btn);
            };

            // abrir
            auto openSpr = ButtonSprite::create(Localization::get().getString("queue.open_button").c_str(), 70, true, "bigFont.fnt", "GJ_button_01.png", 30.f, 0.6f); openSpr->setScale(0.45f);
            auto openBtn = CCMenuItemSpriteExtra::create(openSpr, this, menu_selector(VerificationQueuePopup::onOpenLevel)); openBtn->setTag(r.levelID);
            openBtn->setID(fmt::format("open-btn-{}", r.levelID));
            if (m_current == PendingCategory::Banner) {
                 openBtn->setTarget(this, menu_selector(VerificationQueuePopup::onOpenProfile));
            }
            setupBtn(openBtn, openSpr);

            // btn ver thumb
            auto viewTitle = Localization::get().getString("queue.view_btn");
            if (viewTitle.empty()) viewTitle = "Ver"; // respaldo
            auto viewSpr = ButtonSprite::create(viewTitle.c_str(), 90, true, "bigFont.fnt", "GJ_button_01.png", 30.f, 0.6f); viewSpr->setScale(0.45f);
            auto viewBtn = CCMenuItemSpriteExtra::create(viewSpr, this, menu_selector(VerificationQueuePopup::onViewThumb)); viewBtn->setTag(r.levelID);
            viewBtn->setID(fmt::format("view-btn-{}", r.levelID));
            if (m_current == PendingCategory::Banner) {
                 viewBtn->setTarget(this, menu_selector(VerificationQueuePopup::onViewBanner));
            }
            setupBtn(viewBtn, viewSpr);

            // aceptar solo verify/update/banner
            if (m_current != PendingCategory::Report) {
                auto accSpr = ButtonSprite::create(Localization::get().getString("queue.accept_button").c_str(), 70, true, "bigFont.fnt", "GJ_button_01.png", 30.f, 0.6f); accSpr->setScale(0.45f);
                auto accBtn = CCMenuItemSpriteExtra::create(accSpr, this, menu_selector(VerificationQueuePopup::onAccept)); accBtn->setTag(r.levelID);
                accBtn->setID(fmt::format("accept-btn-{}", r.levelID));
                setupBtn(accBtn, accSpr);
            }
            // tab reporte: btn ver texto
            if (m_current == PendingCategory::Report) {
                auto viewSpr = ButtonSprite::create(Localization::get().getString("queue.view_report").c_str(), 90, true, "bigFont.fnt", "GJ_button_05.png", 30.f, 0.6f);
                viewSpr->setScale(0.45f);
                auto viewBtn = CCMenuItemSpriteExtra::create(viewSpr, this, menu_selector(VerificationQueuePopup::onViewReport));
                viewBtn->setTag(r.levelID);
                viewBtn->setID(fmt::format("view-report-btn-{}", r.levelID));
                // nota pa manejador
                viewBtn->setUserObject(CCString::createWithFormat("%s", r.note.c_str()));
                setupBtn(viewBtn, viewSpr);
            }
            // rechazar siempre
            auto rejSpr = ButtonSprite::create(Localization::get().getString("queue.reject_button").c_str(), 80, true, "bigFont.fnt", "GJ_button_06.png", 30.f, 0.6f); rejSpr->setScale(0.45f);
            auto rejBtn = CCMenuItemSpriteExtra::create(rejSpr, this, menu_selector(VerificationQueuePopup::onReject)); rejBtn->setTag(r.levelID);
            rejBtn->setID(fmt::format("reject-btn-{}", r.levelID));
            // categoria en tag2
            rejBtn->setUserObject(CCString::createWithFormat("%d", static_cast<int>(m_current)));
            setupBtn(rejBtn, rejSpr);

            btnMenu->alignItemsHorizontallyWithPadding(8.f);
            btnMenu->setPosition({right - 55.f, y});
            m_listMenu->addChild(btnMenu);
        }
    } catch (const std::exception& e) {
        log::error("Exception in VerificationQueuePopup::rebuildList: {}", e.what());
    } catch (...) {
        log::error("Unknown exception in VerificationQueuePopup::rebuildList");
    }
}

CCSprite* VerificationQueuePopup::createThumbnailSprite(int levelID) {
    // load thumb por categoria
    
    std::string subdir = (m_current == PendingCategory::Update) ? "updates" : "sugeridos";
    auto cachedPath = geode::Mod::get()->getSaveDir() / "thumbnails" / subdir / fmt::format("{}.webp", levelID);
    
    if (std::filesystem::exists(cachedPath)) {
        auto texture = CCTextureCache::sharedTextureCache()->addImage(cachedPath.generic_string().c_str(), false);
        if (texture) {
            return CCSprite::createWithTexture(texture);
        }
    }
    
    // no cache -> download async
    auto placeholder = CCSprite::create("GJ_square01.png");
    placeholder->setColor({100, 100, 100});
    placeholder->setOpacity(150);
    
    // download suggestion/update
    WeakRef<VerificationQueuePopup> self = this;
    if (m_current == PendingCategory::Update) {
        ThumbnailAPI::get().downloadUpdate(levelID, [self, levelID, placeholder](bool success, CCTexture2D* texture) {
            if (auto popup = self.lock()) {
                if (success && texture && placeholder->getParent()) {
                    placeholder->setTexture(texture);
                    placeholder->setColor({255, 255, 255});
                    placeholder->setOpacity(255);
                }
            }
        });
    } else {
        ThumbnailAPI::get().downloadSuggestion(levelID, [self, levelID, placeholder](bool success, CCTexture2D* texture) {
            if (auto popup = self.lock()) {
                if (success && texture && placeholder->getParent()) {
                    placeholder->setTexture(texture);
                    placeholder->setColor({255, 255, 255});
                    placeholder->setOpacity(255);
                }
            }
        });
    }
    
    return placeholder;
}

CCSprite* VerificationQueuePopup::createServerThumbnailSprite(int levelID) {
    // load thumb oficial
    // cache local primero
    auto localTex = LocalThumbs::get().loadTexture(levelID);
    if (localTex) {
        return CCSprite::createWithTexture(localTex);
    }
    
    // try cache dir
    auto cacheDir = geode::Mod::get()->getSaveDir() / "cache";
    std::vector<std::string> extensions = {".webp", ".png", ".gif"};
    
    for (const auto& ext : extensions) {
        auto cachePath = cacheDir / (fmt::format("{}", levelID) + ext);
        if (std::filesystem::exists(cachePath)) {
            auto texture = CCTextureCache::sharedTextureCache()->addImage(cachePath.generic_string().c_str(), false);
            if (texture) {
                return CCSprite::createWithTexture(texture);
            }
        }
    }
    
    // no cache -> download
    auto placeholder = CCSprite::create("GJ_square01.png");
    placeholder->setColor({80, 80, 80});
    placeholder->setOpacity(150);
    
    // reporte vs update
    WeakRef<VerificationQueuePopup> self = this;
    if (m_current == PendingCategory::Report) {
        ThumbnailAPI::get().downloadReported(levelID, [self, levelID, placeholder](bool success, CCTexture2D* texture) {
            if (auto popup = self.lock()) {
               if (success && texture && placeholder->getParent()) {
                   placeholder->setTexture(texture);
                   placeholder->setColor({255, 255, 255});
                   placeholder->setOpacity(255);
               }
            }
        });
    } else {
        ThumbnailAPI::get().downloadThumbnail(levelID, [this, levelID, placeholder](bool success, CCTexture2D* texture) {
            if (success && texture && placeholder->getParent()) {
                placeholder->setTexture(texture);
                placeholder->setColor({255, 255, 255});
                placeholder->setOpacity(255);
            }
            this->release();
        });
    }
    
    return placeholder;
}

void VerificationQueuePopup::onTabVerify(CCObject*) { switchTo(PendingCategory::Verify); }
void VerificationQueuePopup::onTabUpdate(CCObject*) { switchTo(PendingCategory::Update); }
void VerificationQueuePopup::onTabReport(CCObject*) { switchTo(PendingCategory::Report); }
void VerificationQueuePopup::onTabBanner(CCObject*) { switchTo(PendingCategory::Banner); }


void VerificationQueuePopup::onOpenLevel(CCObject* sender) {
    int lvl = static_cast<CCNode*>(sender)->getTag();
    
    // marcar banderas antes de abrir el nivel
    Mod::get()->setSavedValue("open-from-thumbs", true);
    Mod::get()->setSavedValue("open-from-report", m_current == PendingCategory::Report);
    Mod::get()->setSavedValue("open-from-verification-queue", true);
    Mod::get()->setSavedValue("verification-queue-category", static_cast<int>(m_current));
    Mod::get()->setSavedValue("verification-queue-levelid", lvl);
    
    // nivel desde cache
    auto glm = GameLevelManager::get();
    GJGameLevel* level = nullptr;
    
    // buscar en niveles online descargados previamente
    auto onlineLevels = glm->m_onlineLevels;
    if (onlineLevels) {
        level = static_cast<GJGameLevel*>(onlineLevels->objectForKey(std::to_string(lvl)));
    }
    
    if (level && !level->m_levelName.empty()) {
        // nivel en cache
        log::info("[VerificationQueuePopup] Opening level {} from cache: {}", lvl, level->m_levelName);
        
        CCDirector::sharedDirector()->replaceScene(
            CCTransitionFade::create(0.5f, LevelInfoLayer::scene(level, false))
        );
    } else {
        // no cache, download
        log::info("[VerificationQueuePopup] Downloading level {} before opening...", lvl);
        
        Notification::create("Downloading level...", NotificationIcon::Loading)->show();
        
        m_downloadCheckCount = 0; // reiniciar contador
        this->retain();
        glm->downloadLevel(lvl, false, 0);
        
        // schedule hasta q nivel listo
        this->schedule(schedule_selector(VerificationQueuePopup::checkLevelDownloaded), 0.1f);
    }
}

void VerificationQueuePopup::checkLevelDownloaded(float dt) {
    // check nivel descargado
    int lvl = -1;
    try {
        lvl = Mod::get()->getSavedValue<int>("verification-queue-levelid", -1);
    } catch (...) {
        log::error("[VerificationQueuePopup] Error obteniendo verification-queue-levelid");
    }
    
    if (lvl <= 0) {
        this->unschedule(schedule_selector(VerificationQueuePopup::checkLevelDownloaded));
        this->release();
        return;
    }
    
    // timeout 5s
    m_downloadCheckCount++;
    if (m_downloadCheckCount > 50) {
        log::warn("[VerificationQueuePopup] Timeout esperando descarga del nivel {}", lvl);
        Notification::create("Error: timed out downloading level", NotificationIcon::Error)->show();
        this->unschedule(schedule_selector(VerificationQueuePopup::checkLevelDownloaded));
        this->release();
        return;
    }
    
    try {
        auto glm = GameLevelManager::get();
        if (!glm) {
            log::error("[VerificationQueuePopup] GameLevelManager es nulo");
            return;
        }
        
        auto onlineLevels = glm->m_onlineLevels;
        if (!onlineLevels) {
            return; // seguir esperando
        }
        
        auto level = static_cast<GJGameLevel*>(onlineLevels->objectForKey(std::to_string(lvl)));
        
        if (!level) {
            return; // seguir esperando
        }
        
        // nivel con nombre
        // no tocar creatorName
        try {
            std::string levelName = level->m_levelName;
            if (levelName.empty()) {
                return; // seguir esperando
            }
            
            // nivel descargado exitosamente
            log::info("[VerificationQueuePopup] Nivel {} descargado: {}", lvl, levelName);
            
            this->unschedule(schedule_selector(VerificationQueuePopup::checkLevelDownloaded));
            m_downloadCheckCount = 0;
            
            // abrir el nivel
            CCDirector::sharedDirector()->replaceScene(
                CCTransitionFade::create(0.5f, LevelInfoLayer::scene(level, false))
            );
            
            this->release();
        } catch (const std::exception& e) {
            log::error("[VerificationQueuePopup] Error accediendo a propiedades del nivel: {}", e.what());
            return;
        } catch (...) {
            log::error("[VerificationQueuePopup] Error desconocido accediendo a propiedades del nivel");
            return;
        }
    } catch (const std::exception& e) {
        log::error("[VerificationQueuePopup] Exception en checkLevelDownloaded: {}", e.what());
        this->unschedule(schedule_selector(VerificationQueuePopup::checkLevelDownloaded));
        this->release();
    } catch (...) {
        log::error("[VerificationQueuePopup] Unknown exception en checkLevelDownloaded");
        this->unschedule(schedule_selector(VerificationQueuePopup::checkLevelDownloaded));
        this->release();
    }
}

void VerificationQueuePopup::onAccept(CCObject* sender) {


    int lvl = static_cast<CCNode*>(sender)->getTag();
    
    // username
    std::string username;
    int accountID = 0;
    try {
        if (auto gm = GameManager::get()) {
            username = gm->m_playerName;
            accountID = gm->m_playerUserID;
        }
    } catch(...) {}

    if (accountID <= 0) {
        Notification::create("Tienes que tener cuenta para subir", NotificationIcon::Error)->show();
        return;
    }

    // verificar mod online antes aceptar
    auto loading = LoadingCircle::create();
    loading->setParentLayer(this);
    loading->setFade(true);
    loading->show();

    this->retain();
    ThumbnailAPI::get().checkModeratorAccount(username, accountID, [this, lvl, username, loading](bool isMod, bool isAdmin){
        if (!(isMod || isAdmin)) {
            if(loading) loading->fadeAndRemove();
            Notification::create(Localization::get().getString("queue.accept_error").c_str(), NotificationIcon::Error)->show();
            log::warn("[VerificationQueuePopup] Usuario '{}' no es moderador - bloqueo de aceptación", username);
            this->release();
            return;
        }
        
        ThumbnailAPI::get().acceptQueueItem(lvl, m_current, username, [this, lvl, loading](bool success, const std::string& message) {
            if(loading) loading->fadeAndRemove();
            if (success) {
                log::info("Item aceptado en servidor para nivel {}", lvl);
                Notification::create(Localization::get().getString("queue.accepted").c_str(), NotificationIcon::Success)->show();
                if (this->getParent()) switchTo(m_current);
            } else {
                Notification::create(Localization::get().getString("queue.accept_error").c_str(), NotificationIcon::Error)->show();
            }
            this->release();
        });
    });
}

void VerificationQueuePopup::onReject(CCObject* sender) {


    int lvl = static_cast<CCNode*>(sender)->getTag();
    int catInt = static_cast<int>(reinterpret_cast<uintptr_t>(static_cast<CCNode*>(sender)->getUserData()));
    auto cat = static_cast<PendingCategory>(catInt);
    
    // username
    std::string username;
    try {
        if (auto gm = GameManager::get()) {
            username = gm->m_playerName;
        }
    } catch(...) {}
    
    auto loading = LoadingCircle::create();
    loading->setParentLayer(this);
    loading->setFade(true);
    loading->show();

    this->retain();
    ThumbnailAPI::get().checkModerator(username, [this, lvl, cat, username, loading](bool isMod, bool isAdmin){
        if (!(isMod || isAdmin)) {
            if (loading) loading->fadeAndRemove();
            Notification::create(Localization::get().getString("queue.reject_error").c_str(), NotificationIcon::Error)->show();
            log::warn("[VerificationQueuePopup] Usuario '{}' no es moderador - bloqueo de rechazo", username);
            this->release();
            return;
        }
        
        ThumbnailAPI::get().rejectQueueItem(lvl, cat, username, "Rechazado por moderador", [this, loading](bool success, const std::string& message) {
            if (loading) loading->fadeAndRemove();
            if (success) {
                Notification::create(Localization::get().getString("queue.rejected").c_str(), NotificationIcon::Warning)->show();
                if (this->getParent()) switchTo(m_current);
            } else {
                Notification::create(Localization::get().getString("queue.reject_error").c_str(), NotificationIcon::Error)->show();
            }
            this->release();
        });
    });
}

void VerificationQueuePopup::onClaimLevel(CCObject* sender) {
    int lvl = static_cast<CCNode*>(sender)->getTag();
    
    // username
    std::string username;
    try {
        if (auto gm = GameManager::get()) {
            username = gm->m_playerName;
        }
    } catch(...) {}
    
    // btn verde ya
    auto btn = static_cast<CCMenuItemSpriteExtra*>(sender);
    if (auto btnSprite = typeinfo_cast<ButtonSprite*>(btn->getNormalImage())) {
        btnSprite->updateBGImage("GJ_button_01.png"); // verde
    }
    
    Notification::create(Localization::get().getString("queue.claiming").c_str(), NotificationIcon::Info)->show();
    
    WeakRef<VerificationQueuePopup> self = this;
    ThumbnailAPI::get().claimQueueItem(lvl, m_current, username, [self, lvl, btn, username](bool success, const std::string& message) {
        if (auto popup = self.lock()) {
            if (success) {
                log::info("Level {} claimed by moderator", lvl);
                Notification::create(Localization::get().getString("queue.claimed").c_str(), NotificationIcon::Success)->show();
                
                // update local
                for (auto& item : popup->m_items) {
                    if (item.levelID == lvl) {
                        item.claimedBy = username;
                        break;
                    }
                }
                
                // refresh UI
                if (popup->getParent()) {
                    popup->rebuildList();
                }
            } else {
                log::error("Error claiming level {}: {}", lvl, message);
                Notification::create(fmt::format(fmt::runtime(Localization::get().getString("queue.claim_error")), message).c_str(), NotificationIcon::Error)->show();
                // revertir btn
                if (auto btnSprite = typeinfo_cast<ButtonSprite*>(btn->getNormalImage())) {
                    btnSprite->updateBGImage("GJ_button_04.png"); // gris
                }
            }
        }
    });
}

void VerificationQueuePopup::onViewReport(CCObject* sender) {
    int lvl = static_cast<CCNode*>(sender)->getTag();
    std::string note;
    if (auto obj = static_cast<CCNode*>(sender)->getUserObject()) {
        if (auto s = typeinfo_cast<CCString*>(obj)) note = s->getCString();
    }
    if (note.empty()) {
        // fallback: lista actual
        for (auto const& it : m_items) {
            if (it.levelID == lvl) { note = it.note; break; }
        }
    }
    if (note.empty()) note = "";
    FLAlertLayer::create(Localization::get().getString("queue.report_reason").c_str(), note.c_str(), Localization::get().getString("general.close").c_str())->show();
}

void VerificationQueuePopup::onViewThumb(CCObject* sender) {


    int lvl = static_cast<CCNode*>(sender)->getTag();
    
    // canAccept solo verify/update
    bool canAccept = (m_current == PendingCategory::Verify || m_current == PendingCategory::Update);
    
    // guardar categoria + from-report
    Mod::get()->setSavedValue("from-report-popup", m_current == PendingCategory::Report);
    Mod::get()->setSavedValue("verification-category", static_cast<int>(m_current));
    
    // item pa sugerencias
    std::vector<Suggestion> suggestions;
    for (const auto& item : m_items) {
        if (item.levelID == lvl) {
            suggestions = item.suggestions;
            break;
        }
    }

    // version con zoom
    auto pop = createThumbnailViewPopup(lvl, canAccept, suggestions);
    if (pop) {
        // show popup
        if (auto alertLayer = typeinfo_cast<FLAlertLayer*>(pop)) {
            alertLayer->show();
        }
    } else {
        log::error("[VerificationQueuePopup] Failed to create ThumbnailViewPopup for level {}", lvl);
        Notification::create(Localization::get().getString("queue.cant_open").c_str(), NotificationIcon::Error)->show();
    }
}

void VerificationQueuePopup::onOpenProfile(CCObject* sender) {
    int accountID = static_cast<CCNode*>(sender)->getTag();
    ProfilePage::create(accountID, false)->show();
}

void VerificationQueuePopup::onViewBanner(CCObject* sender) {
    int accountID = static_cast<CCNode*>(sender)->getTag();
    
    auto loading = Notification::create("Loading banner...", NotificationIcon::Loading);
    loading->show();
    
    this->retain();
    // banners en suggestions con accountID
    ThumbnailAPI::get().downloadSuggestion(accountID, [this, loading](bool success, CCTexture2D* texture) {
        loading->hide();
        if (success && texture) {
            auto sprite = CCSprite::createWithTexture(texture);
            BannerViewPopup::create(sprite)->show();
        } else {
            Notification::create("Failed to load banner", NotificationIcon::Error)->show();
        }
        this->release();
    });
}

#include "VerificationCenterLayer.hpp"
#include <Geode/ui/LoadingSpinner.hpp>
#include "../utils/PaimonNotification.hpp"
#include "../utils/PaimonButtonHighlighter.hpp"
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/utils/cocos.hpp>
#include <Geode/loader/Mod.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/LevelInfoLayer.hpp>
#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/ProfilePage.hpp>
#include "../managers/LocalThumbs.hpp"
#include "../managers/ThumbnailAPI.hpp"
#include "../managers/ThumbnailLoader.hpp"
#include <algorithm>
#include "../utils/Localization.hpp"
#include "BanListPopup.hpp"

using namespace geode::prelude;
using namespace cocos2d;

// forward del popup de thumbnails (definido en LevelInfoLayer.cpp)
extern CCNode* createThumbnailViewPopup(int32_t levelID, bool canAcceptUpload, const std::vector<Suggestion>& suggestions);

// ── factory ──────────────────────────────────────────────

VerificationCenterLayer* VerificationCenterLayer::create() {
    auto ret = new VerificationCenterLayer();
    if (ret && ret->init()) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

CCScene* VerificationCenterLayer::scene() {
    auto scene = CCScene::create();
    scene->addChild(VerificationCenterLayer::create());
    return scene;
}

// ── init ─────────────────────────────────────────────────

bool VerificationCenterLayer::init() {
    if (!CCLayer::init()) return false;

    this->setKeypadEnabled(true);

    auto winSize = CCDirector::sharedDirector()->getWinSize();

    // fondo GD estandar
    auto bg = CCSprite::create("GJ_gradientBG.png");
    auto bgSize = bg->getTextureRect().size;
    bg->setScaleX(winSize.width / bgSize.width);
    bg->setScaleY(winSize.height / bgSize.height);
    bg->setAnchorPoint({0, 0});
    bg->setColor({18, 18, 40});
    this->addChild(bg, -2);

    // bordes decorativos (estilo GD)
    auto bottomLeft = CCSprite::createWithSpriteFrameName("GJ_sideArt_001.png");
    if (bottomLeft) {
        bottomLeft->setAnchorPoint({0, 0});
        bottomLeft->setPosition({-2, -2});
        bottomLeft->setOpacity(100);
        this->addChild(bottomLeft, -1);
    }
    auto bottomRight = CCSprite::createWithSpriteFrameName("GJ_sideArt_001.png");
    if (bottomRight) {
        bottomRight->setAnchorPoint({1, 0});
        bottomRight->setPosition({winSize.width + 2, -2});
        bottomRight->setFlipX(true);
        bottomRight->setOpacity(100);
        this->addChild(bottomRight, -1);
    }

    // titulo
    auto title = CCLabelBMFont::create(
        Localization::get().getString("queue.title").c_str(), "goldFont.fnt"
    );
    title->setPosition({winSize.width / 2, winSize.height - 22.f});
    title->setScale(0.8f);
    this->addChild(title, 2);

    // ── pestanas ──
    m_tabsMenu = CCMenu::create();
    m_tabsMenu->setPosition({winSize.width / 2, winSize.height - 50.f});

    auto mkTab = [&](const char* label, SEL_MenuHandler sel, PendingCategory cat) {
        auto spr = ButtonSprite::create(label, 80, true, "bigFont.fnt", "GJ_button_01.png", 28.f, 0.55f);
        spr->setScale(0.75f);
        auto btn = CCMenuItemSpriteExtra::create(spr, this, sel);
        btn->setTag(static_cast<int>(cat));
        return btn;
    };

    m_tabsMenu->addChild(mkTab(
        Localization::get().getString("queue.verify_tab").c_str(),
        menu_selector(VerificationCenterLayer::onTabVerify), PendingCategory::Verify));
    m_tabsMenu->addChild(mkTab(
        Localization::get().getString("queue.update_tab").c_str(),
        menu_selector(VerificationCenterLayer::onTabUpdate), PendingCategory::Update));
    m_tabsMenu->addChild(mkTab(
        Localization::get().getString("queue.report_tab").c_str(),
        menu_selector(VerificationCenterLayer::onTabReport), PendingCategory::Report));
    m_tabsMenu->addChild(mkTab("Banners",
        menu_selector(VerificationCenterLayer::onTabBanner), PendingCategory::Banner));
    m_tabsMenu->addChild(mkTab("Profiles",
        menu_selector(VerificationCenterLayer::onTabProfileImg), PendingCategory::ProfileImg));

    // btn baneados
    {
        auto spr = ButtonSprite::create(
            Localization::get().getString("queue.banned_btn").c_str(),
            70, true, "bigFont.fnt", "GJ_button_05.png", 28.f, 0.55f);
        spr->setScale(0.75f);
        auto btn = CCMenuItemSpriteExtra::create(spr, this,
            menu_selector(VerificationCenterLayer::onViewBans));
        btn->setID("banned-users-btn"_spr);
        m_tabsMenu->addChild(btn);
    }

    m_tabsMenu->setLayout(RowLayout::create()->setGap(4.f)->setAxisAlignment(AxisAlignment::Center));
    this->addChild(m_tabsMenu, 2);

    // ── panel izquierdo: lista con scroll ──
    float listW = winSize.width * 0.52f;
    float listH = winSize.height - 100.f;
    float listX = 18.f;
    float listY = 35.f;

    // fondo lista
    auto listBg = CCScale9Sprite::create("square02_001.png");
    listBg->setColor({0, 0, 0});
    listBg->setOpacity(80);
    listBg->setContentSize({listW, listH});
    listBg->setAnchorPoint({0, 0});
    listBg->setPosition({listX, listY});
    this->addChild(listBg, 0);

    // contenedor pa scroll + scrollbar
    m_listContainer = CCNode::create();
    m_listContainer->setContentSize({listW, listH});
    m_listContainer->setAnchorPoint({0, 0});
    m_listContainer->setPosition({listX, listY});
    this->addChild(m_listContainer, 1);

    m_scrollLayer = ScrollLayer::create({listW - 14.f, listH});
    m_scrollLayer->setPosition({0, 0});
    m_listContainer->addChild(m_scrollLayer);

    m_scrollbar = Scrollbar::create(m_scrollLayer);
    m_scrollbar->setPosition({listW - 8.f, listH / 2});
    m_scrollbar->setContentSize({8.f, listH});
    m_listContainer->addChild(m_scrollbar, 10);

    // ── panel derecho: preview ──
    float previewX = listX + listW + 10.f;
    float previewW = winSize.width - previewX - 18.f;
    float previewH = listH;
    float previewY = listY;

    auto previewBg = CCScale9Sprite::create("square02_001.png");
    previewBg->setColor({0, 0, 0});
    previewBg->setOpacity(60);
    previewBg->setContentSize({previewW, previewH});
    previewBg->setAnchorPoint({0, 0});
    previewBg->setPosition({previewX, previewY});
    this->addChild(previewBg, 0);

    m_previewPanel = CCNode::create();
    m_previewPanel->setContentSize({previewW, previewH});
    m_previewPanel->setAnchorPoint({0, 0});
    m_previewPanel->setPosition({previewX, previewY});
    this->addChild(m_previewPanel, 1);

    // borde preview
    m_previewBorder = CCScale9Sprite::create("GJ_square07.png");
    static_cast<CCScale9Sprite*>(m_previewBorder)->setContentSize({previewW + 4.f, previewH + 4.f});
    m_previewBorder->setAnchorPoint({0, 0});
    m_previewBorder->setPosition({previewX - 2.f, previewY - 2.f});
    this->addChild(m_previewBorder, 2);

    // label "selecciona un item"
    m_previewLabel = CCLabelBMFont::create(
        Localization::get().getString("queue.select_item").c_str(), "bigFont.fnt");
    if (std::string(m_previewLabel->getString()).empty()) {
        m_previewLabel->setString("Select an item");
    }
    m_previewLabel->setScale(0.4f);
    m_previewLabel->setOpacity(120);
    m_previewLabel->setPosition({previewW / 2, previewH / 2});
    m_previewPanel->addChild(m_previewLabel, 5);

    // ── btn volver ──
    auto backMenu = CCMenu::create();
    auto backSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png");
    auto backBtn = CCMenuItemSpriteExtra::create(backSpr, this,
        menu_selector(VerificationCenterLayer::onBack));
    backBtn->setPosition({-winSize.width / 2 + 25.f, winSize.height / 2 - 25.f});
    backMenu->addChild(backBtn);
    backMenu->setPosition({winSize.width / 2, winSize.height / 2});
    this->addChild(backMenu, 5);

    // cargar primera tab
    switchTo(PendingCategory::Verify);
    return true;
}

// ── navegacion ───────────────────────────────────────────

void VerificationCenterLayer::onBack(CCObject*) {
    this->unschedule(schedule_selector(VerificationCenterLayer::checkLevelDownloaded));
    CCDirector::sharedDirector()->popSceneWithTransition(0.5f, kPopTransitionFade);
}

void VerificationCenterLayer::keyBackClicked() {
    onBack(nullptr);
}

// ── tabs ─────────────────────────────────────────────────

void VerificationCenterLayer::onTabVerify(CCObject*)    { switchTo(PendingCategory::Verify); }
void VerificationCenterLayer::onTabUpdate(CCObject*)    { switchTo(PendingCategory::Update); }
void VerificationCenterLayer::onTabReport(CCObject*)    { switchTo(PendingCategory::Report); }
void VerificationCenterLayer::onTabBanner(CCObject*)    { switchTo(PendingCategory::Banner); }
void VerificationCenterLayer::onTabProfileImg(CCObject*){ switchTo(PendingCategory::ProfileImg); }

void VerificationCenterLayer::switchTo(PendingCategory cat) {
    m_current = cat;
    m_selectedIndex = -1;
    clearPreview();

    // resaltar tab activa
    if (m_tabsMenu) {
        for (auto* n : CCArrayExt<CCNode*>(m_tabsMenu->getChildren())) {
            auto* it = static_cast<CCMenuItemSpriteExtra*>(n);
            bool active = it->getTag() == static_cast<int>(cat);
            it->setScale(active ? 0.82f : 0.68f);
        }
    }

    // loading
    auto content = m_scrollLayer->m_contentLayer;
    content->removeAllChildren();
    auto loadLbl = CCLabelBMFont::create("Loading...", "goldFont.fnt");
    loadLbl->setScale(0.5f);
    auto scrollSize = m_scrollLayer->getContentSize();
    content->setContentSize(scrollSize);
    loadLbl->setPosition(scrollSize / 2);
    content->addChild(loadLbl);

    // sync server
    WeakRef<VerificationCenterLayer> self = this;
    ThumbnailAPI::get().syncVerificationQueue(cat, [self, cat](bool success, const std::vector<PendingItem>& items) {
        auto layer = self.lock();
        if (!layer) return;

        if (!success) {
            log::warn("[VerificationCenter] Failed to sync from server, using local");
            layer->m_items = PendingQueue::get().list(cat);
        } else {
            layer->m_items = items;
        }
        if (layer->getParent()) {
            layer->rebuildList();
        }
    });
}

// ── lista ────────────────────────────────────────────────

void VerificationCenterLayer::rebuildList() {
    try {
        auto content = m_scrollLayer->m_contentLayer;
        content->removeAllChildren();

        auto scrollSize = m_scrollLayer->getContentSize();
        float rowH = 46.f;
        float listW = scrollSize.width;

        // username pa reclamo
        std::string currentUsername;
        if (auto gm = GameManager::get()) {
            currentUsername = gm->m_playerName;
        }

        if (m_items.empty()) {
            content->setContentSize(scrollSize);
            auto lbl = CCLabelBMFont::create(
                Localization::get().getString("queue.no_items").c_str(), "goldFont.fnt");
            lbl->setScale(0.45f);
            lbl->setPosition(scrollSize / 2);
            content->addChild(lbl);
            return;
        }

        float totalH = rowH * m_items.size() + 10.f;
        content->setContentSize({listW, std::max(scrollSize.height, totalH)});

        for (size_t i = 0; i < m_items.size(); ++i) {
            auto row = createRowForItem(m_items[i], listW, static_cast<int>(i));
            float y = content->getContentSize().height - 8.f - (float)i * rowH;
            row->setPosition({0, y - rowH});
            content->addChild(row);
        }

        m_scrollLayer->scrollToTop();

    } catch (const std::exception& e) {
        log::error("[VerificationCenter] rebuildList exception: {}", e.what());
    } catch (...) {
        log::error("[VerificationCenter] rebuildList unknown exception");
    }
}

CCNode* VerificationCenterLayer::createRowForItem(const PendingItem& item, float width, int index) {
    auto row = CCNode::create();
    row->setContentSize({width, 42.f});
    row->setAnchorPoint({0, 0});

    // fondo alterno
    auto rowBg = CCScale9Sprite::create("square02_001.png");
    rowBg->setColor(index % 2 == 0 ? ccColor3B{30, 30, 50} : ccColor3B{20, 20, 35});
    rowBg->setOpacity(100);
    rowBg->setContentSize({width - 4.f, 40.f});
    rowBg->setAnchorPoint({0, 0.5f});
    rowBg->setPosition({2.f, 21.f});
    rowBg->setTag(1000 + index); // pa highlight
    row->addChild(rowBg, -1);

    // username
    std::string currentUsername;
    if (auto gm = GameManager::get()) currentUsername = gm->m_playerName;
    bool isClaimed = !item.claimedBy.empty();
    bool claimedByMe = isClaimed && (item.claimedBy == currentUsername);

    // etiqueta ID
    std::string idText = (m_current == PendingCategory::Banner || m_current == PendingCategory::ProfileImg)
        ? fmt::format("Account: {}", item.levelID)
        : fmt::format("ID: {}", item.levelID);
    auto idLbl = CCLabelBMFont::create(idText.c_str(), "goldFont.fnt");
    idLbl->setScale(0.38f);
    idLbl->setAnchorPoint({0, 0.5f});
    idLbl->setPosition({8.f, 26.f});
    row->addChild(idLbl);

    // submitter
    if (!item.submittedBy.empty()) {
        auto subLbl = CCLabelBMFont::create(
            fmt::format("by {}", item.submittedBy).c_str(), "chatFont.fnt");
        subLbl->setScale(0.35f);
        subLbl->setAnchorPoint({0, 0.5f});
        subLbl->setPosition({8.f, 12.f});
        subLbl->setColor({180, 180, 200});
        row->addChild(subLbl);
    }

    // claim status
    if (isClaimed) {
        std::string claimText = claimedByMe
            ? Localization::get().getString("queue.claimed_by_you")
            : fmt::format(fmt::runtime(Localization::get().getString("queue.claimed_by_user")), item.claimedBy);
        auto claimLbl = CCLabelBMFont::create(claimText.c_str(), "chatFont.fnt");
        claimLbl->setScale(0.3f);
        claimLbl->setColor(claimedByMe ? ccColor3B{100, 255, 100} : ccColor3B{255, 100, 100});
        claimLbl->setAnchorPoint({1, 0.5f});
        claimLbl->setPosition({width - 12.f, 12.f});
        row->addChild(claimLbl);
    }

    // menu botones derecha
    auto btnMenu = CCMenu::create();
    btnMenu->setPosition({0, 0});
    btnMenu->setContentSize(row->getContentSize());

    bool canInteract = claimedByMe;

    auto setupBtn = [&](CCMenuItemSpriteExtra* btn, ButtonSprite* spr) {
        if (!canInteract) {
            btn->setEnabled(false);
            spr->setColor({100, 100, 100});
            spr->setOpacity(150);
        }
        PaimonButtonHighlighter::registerButton(btn);
        btnMenu->addChild(btn);
    };

    float btnX = width - 16.f;
    float btnY = 21.f;
    float btnGap = 30.f;

    // rechazar
    {
        auto spr = ButtonSprite::create("X", 22, true, "bigFont.fnt", "GJ_button_06.png", 22.f, 0.5f);
        spr->setScale(0.55f);
        auto btn = CCMenuItemSpriteExtra::create(spr, this,
            menu_selector(VerificationCenterLayer::onReject));
        btn->setTag(item.levelID);
        btn->setUserObject(CCString::createWithFormat("%d", static_cast<int>(m_current)));
        btn->setPosition({btnX, btnY});
        setupBtn(btn, spr);
        btnX -= btnGap;
    }

    // aceptar (no en reportes)
    if (m_current != PendingCategory::Report) {
        auto spr = ButtonSprite::create("OK", 22, true, "bigFont.fnt", "GJ_button_01.png", 22.f, 0.5f);
        spr->setScale(0.55f);
        auto btn = CCMenuItemSpriteExtra::create(spr, this,
            menu_selector(VerificationCenterLayer::onAccept));
        btn->setTag(item.levelID);
        setupBtn(btn, spr);
        btn->setPosition({btnX, btnY});
        btnX -= btnGap;
    }

    // ver reporte
    if (m_current == PendingCategory::Report) {
        auto spr = ButtonSprite::create("?", 22, true, "bigFont.fnt", "GJ_button_05.png", 22.f, 0.5f);
        spr->setScale(0.55f);
        auto btn = CCMenuItemSpriteExtra::create(spr, this,
            menu_selector(VerificationCenterLayer::onViewReport));
        btn->setTag(item.levelID);
        btn->setUserObject(CCString::createWithFormat("%s", item.note.c_str()));
        setupBtn(btn, spr);
        btn->setPosition({btnX, btnY});
        btnX -= btnGap;
    }

    // reclamar
    {
        const char* claimImg = claimedByMe ? "GJ_button_02.png" : "GJ_button_04.png";
        auto spr = ButtonSprite::create("C", 22, true, "bigFont.fnt", claimImg, 22.f, 0.5f);
        spr->setScale(0.55f);
        auto btn = CCMenuItemSpriteExtra::create(spr, this,
            menu_selector(VerificationCenterLayer::onClaimLevel));
        btn->setTag(item.levelID);
        if (isClaimed && !claimedByMe) {
            btn->setEnabled(false);
            spr->setColor({100, 100, 100});
        }
        PaimonButtonHighlighter::registerButton(btn);
        btnMenu->addChild(btn);
        btn->setPosition({btnX, btnY});
        btnX -= btnGap;
    }

    row->addChild(btnMenu, 5);

    // click en la fila -> seleccionar y preview
    // usamos un boton invisible que cubre el area izquierda
    auto selectSpr = CCSprite::create();
    selectSpr->setContentSize({btnX - 4.f, 40.f}); // area a la izquierda de los botones
    selectSpr->setOpacity(0);
    auto selectBtn = CCMenuItemSpriteExtra::create(selectSpr, this,
        menu_selector(VerificationCenterLayer::onSelectItem));
    selectBtn->setTag(index);
    selectBtn->setAnchorPoint({0, 0.5f});
    selectBtn->setPosition({2.f, btnY});
    selectBtn->setContentSize({btnX - 4.f, 40.f});
    btnMenu->addChild(selectBtn, -1);

    return row;
}

void VerificationCenterLayer::highlightRow(int index) {
    if (!m_scrollLayer) return;
    auto content = m_scrollLayer->m_contentLayer;
    if (!content) return;

    for (auto* child : CCArrayExt<CCNode*>(content->getChildren())) {
        for (auto* sub : CCArrayExt<CCNode*>(child->getChildren())) {
            if (auto bg = typeinfo_cast<CCScale9Sprite*>(sub)) {
                int bgIndex = bg->getTag() - 1000;
                if (bgIndex >= 0) {
                    if (bgIndex == index) {
                        bg->setColor({60, 80, 140});
                        bg->setOpacity(160);
                    } else {
                        bg->setColor(bgIndex % 2 == 0 ? ccColor3B{30, 30, 50} : ccColor3B{20, 20, 35});
                        bg->setOpacity(100);
                    }
                }
            }
        }
    }
}

// ── seleccion y preview ──────────────────────────────────

void VerificationCenterLayer::onSelectItem(CCObject* sender) {
    int index = static_cast<CCNode*>(sender)->getTag();
    if (index < 0 || index >= static_cast<int>(m_items.size())) return;
    m_selectedIndex = index;
    highlightRow(index);
    showPreviewForItem(index);
}

void VerificationCenterLayer::clearPreview() {
    if (!m_previewPanel) return;
    // quitar sprite viejo
    if (m_previewSprite) {
        m_previewSprite->removeFromParent();
        m_previewSprite = nullptr;
    }
    if (m_previewSpinner) {
        m_previewSpinner->removeFromParent();
        m_previewSpinner = nullptr;
    }
    // restaurar label
    if (m_previewLabel) {
        m_previewLabel->setVisible(true);
    }
}

void VerificationCenterLayer::setPreviewTexture(CCTexture2D* tex) {
    if (!tex || !m_previewPanel) return;

    clearPreview();
    if (m_previewLabel) m_previewLabel->setVisible(false);

    auto spr = CCSprite::createWithTexture(tex);
    if (!spr) return;

    auto panelSize = m_previewPanel->getContentSize();
    float maxW = panelSize.width - 16.f;
    float maxH = panelSize.height - 16.f;

    float scaleX = maxW / spr->getContentWidth();
    float scaleY = maxH / spr->getContentHeight();
    float scale = std::min(scaleX, scaleY);
    spr->setScale(scale);
    spr->setPosition(panelSize / 2);
    m_previewPanel->addChild(spr, 5);
    m_previewSprite = spr;
}

void VerificationCenterLayer::setPreviewSprite(CCSprite* spr) {
    if (!spr || !m_previewPanel) return;
    clearPreview();
    if (m_previewLabel) m_previewLabel->setVisible(false);

    auto panelSize = m_previewPanel->getContentSize();
    float maxW = panelSize.width - 16.f;
    float maxH = panelSize.height - 16.f;
    float scaleX = maxW / spr->getContentWidth();
    float scaleY = maxH / spr->getContentHeight();
    spr->setScale(std::min(scaleX, scaleY));
    spr->setPosition(panelSize / 2);
    m_previewPanel->addChild(spr, 5);
    m_previewSprite = spr;
}

void VerificationCenterLayer::showPreviewForItem(int index) {
    if (index < 0 || index >= static_cast<int>(m_items.size())) return;

    clearPreview();
    if (m_previewLabel) m_previewLabel->setVisible(false);

    auto panelSize = m_previewPanel->getContentSize();

    // spinner cargando
    m_previewSpinner = LoadingSpinner::create(40.f);
    m_previewSpinner->setPosition(panelSize / 2);
    m_previewPanel->addChild(m_previewSpinner, 10);

    int itemID = m_items[index].levelID;
    WeakRef<VerificationCenterLayer> self = this;
    int savedIndex = index;

    auto onLoaded = [self, savedIndex](bool success, CCTexture2D* tex) {
        auto layer = self.lock();
        if (!layer) return;
        if (layer->m_selectedIndex != savedIndex) return; // ya cambio seleccion

        if (layer->m_previewSpinner) {
            layer->m_previewSpinner->removeFromParent();
            layer->m_previewSpinner = nullptr;
        }

        if (success && tex) {
            layer->setPreviewTexture(tex);
        } else {
            if (layer->m_previewLabel) {
                layer->m_previewLabel->setString("No preview");
                layer->m_previewLabel->setVisible(true);
            }
        }
    };

    // descargar segun categoria
    switch (m_current) {
    case PendingCategory::Verify:
        ThumbnailAPI::get().downloadSuggestion(itemID, onLoaded);
        break;
    case PendingCategory::Update:
        ThumbnailAPI::get().downloadUpdate(itemID, onLoaded);
        break;
    case PendingCategory::Report:
        ThumbnailAPI::get().downloadReported(itemID, onLoaded);
        break;
    case PendingCategory::Banner:
        ThumbnailAPI::get().downloadSuggestion(itemID, onLoaded);
        break;
    case PendingCategory::ProfileImg:
        ThumbnailAPI::get().downloadProfileImg(itemID, onLoaded, true);
        break;
    }
}

// ── acciones ─────────────────────────────────────────────

void VerificationCenterLayer::onViewBans(CCObject*) {
    if (auto popup = BanListPopup::create()) popup->show();
}

void VerificationCenterLayer::onOpenLevel(CCObject* sender) {
    int lvl = static_cast<CCNode*>(sender)->getTag();

    Mod::get()->setSavedValue("open-from-thumbs", true);
    Mod::get()->setSavedValue("open-from-report", m_current == PendingCategory::Report);
    Mod::get()->setSavedValue("open-from-verification-queue", true);
    Mod::get()->setSavedValue("verification-queue-category", static_cast<int>(m_current));
    Mod::get()->setSavedValue("verification-queue-levelid", lvl);

    auto glm = GameLevelManager::get();
    GJGameLevel* level = nullptr;
    auto onlineLevels = glm->m_onlineLevels;
    if (onlineLevels) {
        level = static_cast<GJGameLevel*>(onlineLevels->objectForKey(std::to_string(lvl)));
    }

    if (level && !level->m_levelName.empty()) {
        CCDirector::sharedDirector()->replaceScene(
            CCTransitionFade::create(0.5f, LevelInfoLayer::scene(level, false)));
    } else {
        PaimonNotify::create("Downloading level...", NotificationIcon::Loading)->show();
        m_downloadCheckCount = 0;
        m_pendingLevelID = lvl;
        glm->downloadLevel(lvl, false, 0);
        this->schedule(schedule_selector(VerificationCenterLayer::checkLevelDownloaded), 0.1f);
    }
}

void VerificationCenterLayer::checkLevelDownloaded(float dt) {
    if (m_pendingLevelID <= 0) {
        this->unschedule(schedule_selector(VerificationCenterLayer::checkLevelDownloaded));
        return;
    }
    m_downloadCheckCount++;
    if (m_downloadCheckCount > 50) {
        PaimonNotify::create("Error: timed out downloading level", NotificationIcon::Error)->show();
        this->unschedule(schedule_selector(VerificationCenterLayer::checkLevelDownloaded));
        m_pendingLevelID = 0;
        return;
    }

    auto glm = GameLevelManager::get();
    if (!glm || !glm->m_onlineLevels) return;

    auto level = static_cast<GJGameLevel*>(glm->m_onlineLevels->objectForKey(std::to_string(m_pendingLevelID)));
    if (!level) return;

    std::string name = level->m_levelName;
    if (name.empty()) return;

    this->unschedule(schedule_selector(VerificationCenterLayer::checkLevelDownloaded));
    m_downloadCheckCount = 0;
    int lvl = m_pendingLevelID;
    m_pendingLevelID = 0;

    CCDirector::sharedDirector()->replaceScene(
        CCTransitionFade::create(0.5f, LevelInfoLayer::scene(level, false)));
}

void VerificationCenterLayer::onAccept(CCObject* sender) {
    int lvl = static_cast<CCNode*>(sender)->getTag();

    std::string username;
    int accountID = 0;
    if (auto gm = GameManager::get()) {
        username = gm->m_playerName;
        accountID = gm->m_playerUserID;
    }
    if (accountID <= 0) {
        PaimonNotify::create("Tienes que tener cuenta para subir", NotificationIcon::Error)->show();
        return;
    }

    auto spinner = LoadingSpinner::create(30.f);
    spinner->setPosition(m_previewPanel->getContentSize() / 2);
    spinner->setID("paimon-loading-spinner"_spr);
    m_previewPanel->addChild(spinner, 100);
    Ref<LoadingSpinner> loading = spinner;

    WeakRef<VerificationCenterLayer> self = this;
    auto cat = m_current;

    ThumbnailAPI::get().checkModeratorAccount(username, accountID, [self, lvl, username, loading, cat](bool isMod, bool isAdmin) {
        auto layer = self.lock();
        if (!layer) return;

        if (!(isMod || isAdmin)) {
            if (loading) loading->removeFromParent();
            PaimonNotify::create(Localization::get().getString("queue.accept_error").c_str(), NotificationIcon::Error)->show();
            return;
        }

        ThumbnailAPI::get().acceptQueueItem(lvl, cat, username, [self, lvl, loading, cat](bool success, const std::string& message) {
            auto layer = self.lock();
            if (loading) loading->removeFromParent();
            if (!layer) return;

            if (success) {
                PaimonNotify::create(Localization::get().getString("queue.accepted").c_str(), NotificationIcon::Success)->show();
                if (layer->getParent()) layer->switchTo(cat);
            } else {
                PaimonNotify::create(Localization::get().getString("queue.accept_error").c_str(), NotificationIcon::Error)->show();
            }
        });
    });
}

void VerificationCenterLayer::onReject(CCObject* sender) {
    int lvl = static_cast<CCNode*>(sender)->getTag();

    std::string username;
    if (auto gm = GameManager::get()) username = gm->m_playerName;

    auto spinner = LoadingSpinner::create(30.f);
    spinner->setPosition(m_previewPanel->getContentSize() / 2);
    spinner->setID("paimon-loading-spinner"_spr);
    m_previewPanel->addChild(spinner, 100);
    Ref<LoadingSpinner> loading = spinner;

    WeakRef<VerificationCenterLayer> self = this;
    auto cat = m_current;

    ThumbnailAPI::get().checkModerator(username, [self, lvl, cat, username, loading](bool isMod, bool isAdmin) {
        auto layer = self.lock();
        if (!layer) return;

        if (!(isMod || isAdmin)) {
            if (loading) loading->removeFromParent();
            PaimonNotify::create(Localization::get().getString("queue.reject_error").c_str(), NotificationIcon::Error)->show();
            return;
        }

        ThumbnailAPI::get().rejectQueueItem(lvl, cat, username, "Rechazado por moderador", [self, loading, cat](bool success, const std::string& message) {
            auto layer = self.lock();
            if (loading) loading->removeFromParent();
            if (!layer) return;

            if (success) {
                PaimonNotify::create(Localization::get().getString("queue.rejected").c_str(), NotificationIcon::Warning)->show();
                if (layer->getParent()) layer->switchTo(cat);
            } else {
                PaimonNotify::create(Localization::get().getString("queue.reject_error").c_str(), NotificationIcon::Error)->show();
            }
        });
    });
}

void VerificationCenterLayer::onClaimLevel(CCObject* sender) {
    int lvl = static_cast<CCNode*>(sender)->getTag();

    std::string username;
    if (auto gm = GameManager::get()) username = gm->m_playerName;

    PaimonNotify::create(Localization::get().getString("queue.claiming").c_str(), NotificationIcon::Info)->show();

    WeakRef<VerificationCenterLayer> self = this;
    auto cat = m_current;

    ThumbnailAPI::get().claimQueueItem(lvl, cat, username, [self, lvl, username, cat](bool success, const std::string& message) {
        auto layer = self.lock();
        if (!layer) return;

        if (success) {
            PaimonNotify::create(Localization::get().getString("queue.claimed").c_str(), NotificationIcon::Success)->show();
            for (auto& item : layer->m_items) {
                if (item.levelID == lvl) {
                    item.claimedBy = username;
                    break;
                }
            }
            if (layer->getParent()) layer->rebuildList();
        } else {
            PaimonNotify::create(
                fmt::format(fmt::runtime(Localization::get().getString("queue.claim_error")), message).c_str(),
                NotificationIcon::Error)->show();
        }
    });
}

void VerificationCenterLayer::onViewReport(CCObject* sender) {
    int lvl = static_cast<CCNode*>(sender)->getTag();
    std::string note;
    if (auto obj = static_cast<CCNode*>(sender)->getUserObject()) {
        if (auto s = typeinfo_cast<CCString*>(obj)) note = s->getCString();
    }
    if (note.empty()) {
        for (auto const& it : m_items) {
            if (it.levelID == lvl) { note = it.note; break; }
        }
    }
    FLAlertLayer::create(
        Localization::get().getString("queue.report_reason").c_str(),
        note.c_str(),
        Localization::get().getString("general.close").c_str())->show();
}

void VerificationCenterLayer::onViewThumb(CCObject* sender) {
    int lvl = static_cast<CCNode*>(sender)->getTag();
    bool canAccept = (m_current == PendingCategory::Verify || m_current == PendingCategory::Update);

    Mod::get()->setSavedValue("from-report-popup", m_current == PendingCategory::Report);
    Mod::get()->setSavedValue("verification-category", static_cast<int>(m_current));

    std::vector<Suggestion> suggestions;
    for (const auto& item : m_items) {
        if (item.levelID == lvl) {
            suggestions = item.suggestions;
            break;
        }
    }

    auto pop = createThumbnailViewPopup(lvl, canAccept, suggestions);
    if (pop) {
        if (auto alertLayer = typeinfo_cast<FLAlertLayer*>(pop)) {
            alertLayer->show();
        }
    } else {
        PaimonNotify::create(Localization::get().getString("queue.cant_open").c_str(), NotificationIcon::Error)->show();
    }
}

void VerificationCenterLayer::onOpenProfile(CCObject* sender) {
    int accountID = static_cast<CCNode*>(sender)->getTag();
    ProfilePage::create(accountID, false)->show();
}

void VerificationCenterLayer::onViewBanner(CCObject* sender) {
    int accountID = static_cast<CCNode*>(sender)->getTag();

    auto loading = PaimonNotify::create("Loading banner...", NotificationIcon::Loading);
    loading->show();

    WeakRef<VerificationCenterLayer> self = this;
    ThumbnailAPI::get().downloadSuggestion(accountID, [self, loading](bool success, CCTexture2D* texture) {
        loading->hide();
        auto layer = self.lock();
        if (!layer) return;
        if (success && texture) {
            layer->setPreviewTexture(texture);
        } else {
            PaimonNotify::create("Failed to load banner", NotificationIcon::Error)->show();
        }
    });
}

void VerificationCenterLayer::onViewProfileImg(CCObject* sender) {
    int accountID = static_cast<CCNode*>(sender)->getTag();

    auto loading = PaimonNotify::create("Loading profile image...", NotificationIcon::Loading);
    loading->show();

    WeakRef<VerificationCenterLayer> self = this;
    ThumbnailAPI::get().downloadProfileImg(accountID, [self, loading](bool success, CCTexture2D* texture) {
        loading->hide();
        auto layer = self.lock();
        if (!layer) return;
        if (success && texture) {
            layer->setPreviewTexture(texture);
        } else {
            PaimonNotify::create("Failed to load profile image", NotificationIcon::Error)->show();
        }
    }, true);
}

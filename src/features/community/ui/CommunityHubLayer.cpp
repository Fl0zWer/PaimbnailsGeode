#include "CommunityHubLayer.hpp"
#include "../../../utils/HttpClient.hpp"
#include "../../../utils/Localization.hpp"
#include "../../../utils/WebHelper.hpp"
#include "../../thumbnails/services/LocalThumbs.hpp"
#include "../../thumbnails/services/ThumbnailLoader.hpp"
#include "../../transitions/services/TransitionManager.hpp"
#include "../../backgrounds/services/LayerBackgroundManager.hpp"
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/binding/FMODAudioEngine.hpp>
#include <Geode/binding/GameLevelManager.hpp>
#include <Geode/utils/web.hpp>
#include <matjson.hpp>

using namespace geode::prelude;

CommunityHubLayer* CommunityHubLayer::create() {
    auto ret = new CommunityHubLayer();
    if (ret && ret->init()) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

CCScene* CommunityHubLayer::scene() {
    auto scene = CCScene::create();
    scene->addChild(CommunityHubLayer::create());
    return scene;
}

CommunityHubLayer::~CommunityHubLayer() {
    removeCaveEffect();
}

bool CommunityHubLayer::init() {
    if (!CCLayer::init()) return false;

    auto winSize = CCDirector::sharedDirector()->getWinSize();

    // fondo base
    if (!LayerBackgroundManager::get().applyBackground(this, "community_hub")) {
        auto bg = CCSprite::create("GJ_gradientBG.png");
        bg->setPosition(winSize / 2);
        bg->setScaleX(winSize.width / bg->getContentSize().width);
        bg->setScaleY(winSize.height / bg->getContentSize().height);
        bg->setColor({15, 12, 25});
        bg->setZOrder(-10);
        this->addChild(bg);
    }

    // titulo
    auto& loc = Localization::get();
    auto title = CCLabelBMFont::create(loc.getString("community.title").c_str(), "bigFont.fnt");
    title->setScale(0.65f);
    title->setPosition({winSize.width / 2, winSize.height - 20.f});
    this->addChild(title, 10);

    // volver
    auto menu = CCMenu::create();
    menu->setPosition(0, 0);
    menu->setZOrder(20);
    this->addChild(menu);

    auto backBtn = CCMenuItemSpriteExtra::create(
        CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png"),
        this,
        menu_selector(CommunityHubLayer::onBack)
    );
    backBtn->setPosition(25, winSize.height - 25);
    menu->addChild(backBtn);

    // tabs
    auto tabMenu = CCMenu::create();
    tabMenu->setPosition(0, 0);
    tabMenu->setZOrder(10);
    this->addChild(tabMenu);
    m_tabsMenu = tabMenu;

    auto createTab = [&](std::string const& text, char const* id, CCPoint pos) -> CCMenuItemToggler* {
        auto createBtn = [&](char const* frameName) -> CCNode* {
            auto sprite = cocos2d::extension::CCScale9Sprite::createWithSpriteFrameName(frameName);
            sprite->setContentSize({120.f, 28.f});
            auto label = CCLabelBMFont::create(text.c_str(), "goldFont.fnt");
            label->setScale(0.45f);
            label->setPosition(sprite->getContentSize() / 2);
            sprite->addChild(label);
            return sprite;
        };

        auto onSprite = createBtn("GJ_longBtn01_001.png");
        auto offSprite = createBtn("GJ_longBtn02_001.png");

        auto tab = CCMenuItemToggler::create(offSprite, onSprite, this, menu_selector(CommunityHubLayer::onTab));
        tab->setUserObject(CCString::create(id));
        tab->setPosition(pos);
        m_tabs.push_back(tab);
        return tab;
    };

    float topY = winSize.height - 45.f;
    float centerX = winSize.width / 2;

    auto modsTab = createTab(loc.getString("community.tab_mods"), "mods", {centerX - 130.f, topY});
    modsTab->toggle(true);
    tabMenu->addChild(modsTab);

    auto creatorsTab = createTab(loc.getString("community.tab_creators"), "creators", {centerX, topY});
    tabMenu->addChild(creatorsTab);

    auto thumbsTab = createTab(loc.getString("community.tab_thumbnails"), "thumbnails", {centerX + 130.f, topY});
    tabMenu->addChild(thumbsTab);

    // spinner
    m_loadingSpinner = geode::LoadingSpinner::create(40.f);
    m_loadingSpinner->setPosition(winSize / 2);
    m_loadingSpinner->setVisible(true);
    this->addChild(m_loadingSpinner, 100);

    this->setKeypadEnabled(true);
#if defined(GEODE_IS_WINDOWS)
    this->setMouseEnabled(true);
#endif
    CCDirector::sharedDirector()->getTouchDispatcher()->addTargetedDelegate(this, 0, false);

    applyCaveEffect();
    this->scheduleUpdate();

    loadTab(Tab::Moderators);
    return true;
}

void CommunityHubLayer::onEnterTransitionDidFinish() {
    CCLayer::onEnterTransitionDidFinish();
    applyCaveEffect();
}

void CommunityHubLayer::update(float dt) {
    // si otro mod toco el canal, lo vuelvo a poner
    if (!m_caveApplied) {
        applyCaveEffect();
    }
}

void CommunityHubLayer::applyCaveEffect() {
    auto engine = FMODAudioEngine::sharedEngine();
    if (!engine || !engine->m_system || !engine->m_backgroundMusicChannel) return;
    if (m_caveApplied) return;

    // bajo el volumen para que quede mas al fondo
    engine->m_backgroundMusicChannel->getVolume(&m_savedBgVolume);
    float caveVol = engine->m_musicVolume * 0.55f;
    engine->m_backgroundMusicChannel->setVolume(caveVol);

    // lowpass pa ese efecto de cueva
    if (!m_lowpassDSP) {
        engine->m_system->createDSPByType(FMOD_DSP_TYPE_LOWPASS, &m_lowpassDSP);
        if (m_lowpassDSP) {
            m_lowpassDSP->setParameterFloat(FMOD_DSP_LOWPASS_CUTOFF, 1200.f);
            m_lowpassDSP->setParameterFloat(FMOD_DSP_LOWPASS_RESONANCE, 2.0f);
        }
    }

    // reverb leve pa rematar
    if (!m_reverbDSP) {
        engine->m_system->createDSPByType(FMOD_DSP_TYPE_SFXREVERB, &m_reverbDSP);
        if (m_reverbDSP) {
            m_reverbDSP->setParameterFloat(FMOD_DSP_SFXREVERB_DECAYTIME, 2500.f);
            m_reverbDSP->setParameterFloat(FMOD_DSP_SFXREVERB_EARLYDELAY, 20.f);
            m_reverbDSP->setParameterFloat(FMOD_DSP_SFXREVERB_LATEDELAY, 40.f);
            m_reverbDSP->setParameterFloat(FMOD_DSP_SFXREVERB_HFREFERENCE, 3000.f);
            m_reverbDSP->setParameterFloat(FMOD_DSP_SFXREVERB_DRYLEVEL, -4.f);
            m_reverbDSP->setParameterFloat(FMOD_DSP_SFXREVERB_WETLEVEL, -8.f);
        }
    }

    if (m_lowpassDSP) engine->m_backgroundMusicChannel->addDSP(0, m_lowpassDSP);
    if (m_reverbDSP) engine->m_backgroundMusicChannel->addDSP(1, m_reverbDSP);
    m_caveApplied = true;
}

void CommunityHubLayer::removeCaveEffect() {
    auto engine = FMODAudioEngine::sharedEngine();
    if (engine && engine->m_backgroundMusicChannel) {
        if (m_lowpassDSP) engine->m_backgroundMusicChannel->removeDSP(m_lowpassDSP);
        if (m_reverbDSP) engine->m_backgroundMusicChannel->removeDSP(m_reverbDSP);
        // vuelvo al volumen normal
        engine->m_backgroundMusicChannel->setVolume(engine->m_musicVolume);
    }
    if (m_lowpassDSP) { m_lowpassDSP->release(); m_lowpassDSP = nullptr; }
    if (m_reverbDSP) { m_reverbDSP->release(); m_reverbDSP = nullptr; }
    m_caveApplied = false;
}

bool CommunityHubLayer::ccMouseScroll(float x, float y) {
#if !defined(GEODE_IS_WINDOWS)
    return false;
#else
    if (!m_scrollView) return false;

    auto winSize = CCDirector::sharedDirector()->getWinSize();
    CCPoint mousePos = CCDirector::sharedDirector()->getOpenGLView()->getMousePosition();
    mousePos.y = winSize.height - mousePos.y;

    CCRect scrollRect = m_scrollView->boundingBox();
    scrollRect.origin = m_scrollView->getParent()->convertToWorldSpace(scrollRect.origin);

    if (!scrollRect.containsPoint(mousePos)) return false;

    CCPoint offset = ccp(0, m_scrollView->m_contentLayer->getPositionY());
    CCSize viewSize = m_scrollView->getContentSize();
    CCSize contentSize = m_scrollView->m_contentLayer->getContentSize();

    float scrollAmount = y * 30.f;
    float newY = offset.y + scrollAmount;

    float minY = viewSize.height - contentSize.height;
    float maxY = 0.f;
    if (minY > maxY) minY = maxY;

    newY = std::max(minY, std::min(maxY, newY));
    m_scrollView->m_contentLayer->setPositionY(newY);
    return true;
#endif
}

void CommunityHubLayer::onBack(CCObject*) {
    removeCaveEffect();
    CCDirector::sharedDirector()->popSceneWithTransition(0.5f, PopTransition::kPopTransitionFade);
}

void CommunityHubLayer::keyBackClicked() {
    onBack(nullptr);
}

void CommunityHubLayer::onTab(CCObject* sender) {
    auto toggler = typeinfo_cast<CCMenuItemToggler*>(sender);
    if (!toggler) return;
    auto typeObj = typeinfo_cast<CCString*>(toggler->getUserObject());
    if (!typeObj) return;
    std::string type = typeObj->getCString();

    Tab newTab;
    if (type == "mods") newTab = Tab::Moderators;
    else if (type == "creators") newTab = Tab::TopCreators;
    else newTab = Tab::TopThumbnails;

    if (m_currentTab == newTab) {
        toggler->toggle(true);
        return;
    }
    m_currentTab = newTab;

    for (auto tab : m_tabs) {
        tab->toggle(tab == toggler);
    }

    clearList();
    if (m_loadingSpinner) m_loadingSpinner->setVisible(true);

    loadTab(newTab);
}

void CommunityHubLayer::clearList() {
    if (m_listContainer) {
        m_listContainer->removeFromParent();
        m_listContainer = nullptr;
    }
    m_scrollView = nullptr;
}

void CommunityHubLayer::loadTab(Tab tab) {
    switch (tab) {
        case Tab::Moderators: loadModerators(); break;
        case Tab::TopCreators: loadTopCreators(); break;
        case Tab::TopThumbnails: loadTopThumbnails(); break;
    }
}

// mods

void CommunityHubLayer::loadModerators() {
    m_modEntries.clear();
    m_modScores = CCArray::create();
    m_pendingProfiles = 0;

    WeakRef<CommunityHubLayer> self = this;
    HttpClient::get().get("/api/moderators", [self](bool success, std::string const& response) {
        auto layer = self.lock();
        if (!layer) return;

        if (!success) {
            if (layer->m_loadingSpinner) layer->m_loadingSpinner->setVisible(false);
            layer->buildModeratorsList();
            return;
        }

        auto res = matjson::parse(response);
        if (!res.isOk()) {
            if (layer->m_loadingSpinner) layer->m_loadingSpinner->setVisible(false);
            layer->buildModeratorsList();
            return;
        }

        auto json = res.unwrap();
        if (json.contains("moderators") && json["moderators"].isArray()) {
            auto arrRes = json["moderators"].asArray();
            if (arrRes.isOk()) {
                for (auto const& item : arrRes.unwrap()) {
                    ModEntry entry;
                    entry.username = item["username"].asString().unwrapOr("");
                    entry.role = item["role"].asString().unwrapOr("mod");
                    if (!entry.username.empty()) {
                        layer->m_modEntries.push_back(entry);
                    }
                }
            }
        }

        if (layer->m_modEntries.empty()) {
            if (layer->m_loadingSpinner) layer->m_loadingSpinner->setVisible(false);
            layer->buildModeratorsList();
            return;
        }

        layer->m_pendingProfiles = (int)layer->m_modEntries.size();
        for (auto const& entry : layer->m_modEntries) {
            layer->fetchGDBrowserProfile(entry.username, entry.role);
        }
    });
}

void CommunityHubLayer::fetchGDBrowserProfile(std::string const& username, std::string const& role) {
    std::string url = "https://gdbrowser.com/api/profile/" + username;

    WeakRef<CommunityHubLayer> self = this;
    auto req = web::WebRequest();

    WebHelper::dispatch(std::move(req), "GET", url, [self, username, role](web::WebResponse res) {
        std::string data = res.ok() ? res.string().unwrapOr("") : "";
        if (auto layer = self.lock()) {
            layer->onProfileFetched(username, data, role);
        }
    });
}

void CommunityHubLayer::onProfileFetched(std::string const& username, std::string const& jsonData, std::string const& role) {
    if (!jsonData.empty()) {
        auto res = matjson::parse(jsonData);
        if (res.isOk()) {
            auto json = res.unwrap();

            auto parseInt = [](matjson::Value const& val) -> int {
                if (val.isNumber()) return val.asInt().unwrapOr(0);
                if (val.isString()) return geode::utils::numFromString<int>(val.asString().unwrapOr("0")).unwrapOr(0);
                return 0;
            };

            auto score = GJUserScore::create();
            score->m_userName = json["username"].asString().unwrapOr(username);
            score->m_accountID = parseInt(json["accountID"]);
            if (json.contains("playerID")) score->m_userID = parseInt(json["playerID"]);

            score->m_stars = parseInt(json["stars"]);
            score->m_diamonds = parseInt(json["diamonds"]);
            score->m_secretCoins = parseInt(json["coins"]);
            score->m_userCoins = parseInt(json["userCoins"]);
            score->m_demons = parseInt(json["demons"]);
            score->m_creatorPoints = parseInt(json["cp"]);
            score->m_globalRank = parseInt(json["rank"]);
            score->m_moons = parseInt(json["moons"]);

            score->m_iconID = parseInt(json["icon"]);
            score->m_color1 = parseInt(json["col1"]);
            score->m_color2 = parseInt(json["col2"]);
            score->m_playerCube = score->m_iconID;
            score->m_iconType = IconType::Cube;
            score->m_glowEnabled = json["glow"].asBool().unwrapOr(false);

            // uso modBadge para distinguir admin y mod
            if (role == "admin") {
                score->m_modBadge = 2;
            } else {
                score->m_modBadge = 1;
            }

            GameLevelManager::get()->storeUserName(score->m_userID, score->m_accountID, score->m_userName);
            m_modScores->addObject(score);
        }
    }

    m_pendingProfiles--;
    if (m_pendingProfiles <= 0) {
        onAllProfilesFetched();
    }
}

void CommunityHubLayer::onAllProfilesFetched() {
    if (m_loadingSpinner) m_loadingSpinner->setVisible(false);

    // mantengo el orden del server, con admins arriba
    if (m_modScores && m_modScores->count() > 0) {
        auto toVec = std::vector<Ref<GJUserScore>>();
        for (auto* obj : CCArrayExt<GJUserScore*>(m_modScores)) {
            if (obj) toVec.push_back(obj);
        }

        auto toLower = [](std::string str) {
            std::transform(str.begin(), str.end(), str.begin(), ::tolower);
            return str;
        };

        std::stable_sort(toVec.begin(), toVec.end(), [&](Ref<GJUserScore> const& a, Ref<GJUserScore> const& b) {
            return a->m_modBadge > b->m_modBadge;
        });

        m_modScores->removeAllObjects();
        for (auto& s : toVec) {
            m_modScores->addObject(s.data());
        }
    }

    buildModeratorsList();
}

void CommunityHubLayer::buildModeratorsList() {
    clearList();
    auto winSize = CCDirector::sharedDirector()->getWinSize();
    auto& loc = Localization::get();

    if (!m_modScores || m_modScores->count() == 0) {
        m_listContainer = CCNode::create();
        this->addChild(m_listContainer, 5);
        auto lbl = CCLabelBMFont::create(loc.getString("community.no_data").c_str(), "chatFont.fnt");
        lbl->setScale(0.7f);
        lbl->setOpacity(150);
        lbl->setPosition(winSize / 2);
        m_listContainer->addChild(lbl);
        return;
    }

    m_listContainer = CCNode::create();
    this->addChild(m_listContainer, 5);

    // aca uso la lista nativa de GD para que se vea igual
    auto listView = CustomListView::create(
        m_modScores,
        BoomListType::Score,
        winSize.height - 85.f,
        360.f
    );

    auto listLayer = GJListLayer::create(
        listView,
        loc.getString("mods.title").c_str(),
        {0, 0, 0, 180},
        360.f,
        winSize.height - 85.f,
        0
    );

    listLayer->setPosition({winSize.width / 2 - 180.f, 15.f});
    m_listContainer->addChild(listLayer);
}

// creadores

void CommunityHubLayer::loadTopCreators() {
    m_creatorEntries.clear();

    WeakRef<CommunityHubLayer> self = this;
    HttpClient::get().getTopCreators([self](bool success, std::string const& response) {
        auto layer = self.lock();
        if (!layer) return;

        if (success) {
            auto res = matjson::parse(response);
            if (res.isOk()) {
                auto json = res.unwrap();
                if (json.contains("creators") && json["creators"].isArray()) {
                    auto arrRes = json["creators"].asArray();
                    if (arrRes.isOk()) {
                        for (auto const& item : arrRes.unwrap()) {
                            CreatorEntry entry;
                            entry.username = item["username"].asString().unwrapOr("Unknown");
                            entry.accountID = item["accountID"].asInt().unwrapOr(0);
                            entry.uploadCount = item["uploadCount"].asInt().unwrapOr(0);
                            entry.avgRating = (float)item["avgRating"].asDouble().unwrapOr(0.0);
                            layer->m_creatorEntries.push_back(entry);
                        }
                    }
                }
            }
        }

        if (layer->m_loadingSpinner) layer->m_loadingSpinner->setVisible(false);
        layer->buildCreatorsList();
    });
}

void CommunityHubLayer::buildCreatorsList() {
    clearList();
    auto winSize = CCDirector::sharedDirector()->getWinSize();
    auto& loc = Localization::get();

    m_listContainer = CCNode::create();
    this->addChild(m_listContainer, 5);

    if (m_creatorEntries.empty()) {
        auto lbl = CCLabelBMFont::create(loc.getString("community.no_data").c_str(), "chatFont.fnt");
        lbl->setScale(0.7f);
        lbl->setOpacity(150);
        lbl->setPosition(winSize / 2);
        m_listContainer->addChild(lbl);
        return;
    }

    float listW = 380.f;
    float cellH = 40.f;
    float listH = winSize.height - 90.f;
    float totalH = std::max(listH, cellH * (float)m_creatorEntries.size());

    auto scrollView = geode::ScrollLayer::create({listW, listH});
    scrollView->setPosition({winSize.width / 2 - listW / 2, 20.f});

    auto content = CCLayer::create();
    content->setContentSize({listW, totalH});
    content->setPosition({0, 0});
    scrollView->m_contentLayer->addChild(content);
    scrollView->m_contentLayer->setContentSize({listW, totalH});

    m_listContainer->addChild(scrollView);
    m_scrollView = scrollView;

    for (int i = 0; i < (int)m_creatorEntries.size(); i++) {
        auto& entry = m_creatorEntries[i];
        float y = totalH - (i + 0.5f) * cellH;

        auto cell = CCNode::create();
        cell->setContentSize({listW, cellH - 2.f});
        cell->setAnchorPoint({0.5f, 0.5f});
        cell->setPosition({listW / 2, y});
        content->addChild(cell);

        // fondo alternado
        auto cellBg = cocos2d::extension::CCScale9Sprite::createWithSpriteFrameName("square02b_001.png");
        if (!cellBg) cellBg = cocos2d::extension::CCScale9Sprite::createWithSpriteFrameName("square02_001.png");
        if (cellBg) {
            cellBg->setColor(i % 2 == 0 ? ccColor3B{18, 18, 28} : ccColor3B{22, 22, 32});
            cellBg->setOpacity(200);
            cellBg->setContentSize(cell->getContentSize());
            cellBg->setPosition(cell->getContentSize() / 2);
            cell->addChild(cellBg, 0);
        }

        float textX = 10.f;
        float cellMidY = (cellH - 2.f) / 2;

        // puesto
        auto numLbl = CCLabelBMFont::create(fmt::format("#{}", i + 1).c_str(), "chatFont.fnt");
        numLbl->setScale(0.5f);
        numLbl->setColor({255, 200, 50});
        numLbl->setAnchorPoint({0, 0.5f});
        numLbl->setPosition({textX, cellMidY});
        cell->addChild(numLbl, 10);

        // nombre
        float nameX = 45.f;
        auto nameLbl = CCLabelBMFont::create(entry.username.c_str(), "bigFont.fnt");
        nameLbl->setScale(0.4f);
        nameLbl->setAnchorPoint({0, 0.5f});
        nameLbl->setPosition({nameX, cellMidY + 6.f});
        float maxNameW = 180.f;
        if (nameLbl->getScaledContentSize().width > maxNameW) {
            nameLbl->setScale(nameLbl->getScale() * (maxNameW / nameLbl->getScaledContentSize().width));
        }
        cell->addChild(nameLbl, 10);

        // stats
        auto statsStr = fmt::format("{}: {}  |  {}: {:.1f}",
            loc.getString("community.uploads"), entry.uploadCount,
            loc.getString("community.avg_rating"), entry.avgRating);
        auto statsLbl = CCLabelBMFont::create(statsStr.c_str(), "chatFont.fnt");
        statsLbl->setScale(0.38f);
        statsLbl->setColor({180, 200, 220});
        statsLbl->setAnchorPoint({0, 0.5f});
        statsLbl->setPosition({nameX, cellMidY - 7.f});
        cell->addChild(statsLbl, 10);
    }

    // arranco arriba
    scrollView->m_contentLayer->setPositionY(listH - totalH);
}

// thumbs

void CommunityHubLayer::loadTopThumbnails() {
    m_thumbnailEntries.clear();

    WeakRef<CommunityHubLayer> self = this;
    HttpClient::get().getTopThumbnails([self](bool success, std::string const& response) {
        auto layer = self.lock();
        if (!layer) return;

        if (success) {
            auto res = matjson::parse(response);
            if (res.isOk()) {
                auto json = res.unwrap();
                if (json.contains("thumbnails") && json["thumbnails"].isArray()) {
                    auto arrRes = json["thumbnails"].asArray();
                    if (arrRes.isOk()) {
                        for (auto const& item : arrRes.unwrap()) {
                            ThumbnailEntry entry;
                            entry.levelId = item["levelId"].asInt().unwrapOr(0);
                            entry.rating = (float)item["rating"].asDouble().unwrapOr(0.0);
                            entry.count = item["count"].asInt().unwrapOr(0);
                            entry.uploadedBy = item["uploadedBy"].asString().unwrapOr("Unknown");
                            entry.accountID = item["accountID"].asInt().unwrapOr(0);
                            if (entry.levelId > 0) {
                                layer->m_thumbnailEntries.push_back(entry);
                            }
                        }
                    }
                }
            }
        }

        if (layer->m_loadingSpinner) layer->m_loadingSpinner->setVisible(false);
        layer->buildThumbnailsList();
    });
}

void CommunityHubLayer::buildThumbnailsList() {
    clearList();
    auto winSize = CCDirector::sharedDirector()->getWinSize();
    auto& loc = Localization::get();

    m_listContainer = CCNode::create();
    this->addChild(m_listContainer, 5);

    if (m_thumbnailEntries.empty()) {
        auto lbl = CCLabelBMFont::create(loc.getString("community.no_data").c_str(), "chatFont.fnt");
        lbl->setScale(0.7f);
        lbl->setOpacity(150);
        lbl->setPosition(winSize / 2);
        m_listContainer->addChild(lbl);
        return;
    }

    float listW = 380.f;
    float cellH = 55.f;
    float listH = winSize.height - 90.f;
    float totalH = std::max(listH, cellH * (float)m_thumbnailEntries.size());

    auto scrollView = geode::ScrollLayer::create({listW, listH});
    scrollView->setPosition({winSize.width / 2 - listW / 2, 20.f});

    auto content = CCLayer::create();
    content->setContentSize({listW, totalH});
    content->setPosition({0, 0});
    scrollView->m_contentLayer->addChild(content);
    scrollView->m_contentLayer->setContentSize({listW, totalH});

    m_listContainer->addChild(scrollView);
    m_scrollView = scrollView;

    for (int i = 0; i < (int)m_thumbnailEntries.size(); i++) {
        auto& entry = m_thumbnailEntries[i];
        float y = totalH - (i + 0.5f) * cellH;

        auto cell = CCNode::create();
        cell->setContentSize({listW, cellH - 2.f});
        cell->setAnchorPoint({0.5f, 0.5f});
        cell->setPosition({listW / 2, y});
        content->addChild(cell);

        // fondo alternado
        auto cellBg = cocos2d::extension::CCScale9Sprite::createWithSpriteFrameName("square02b_001.png");
        if (!cellBg) cellBg = cocos2d::extension::CCScale9Sprite::createWithSpriteFrameName("square02_001.png");
        if (cellBg) {
            cellBg->setColor(i % 2 == 0 ? ccColor3B{18, 18, 28} : ccColor3B{22, 22, 32});
            cellBg->setOpacity(200);
            cellBg->setContentSize(cell->getContentSize());
            cellBg->setPosition(cell->getContentSize() / 2);
            cell->addChild(cellBg, 0);
        }

        // preview del thumb
        float thumbSize = cellH - 8.f;
        auto thumbPlaceholder = CCLayerColor::create({30, 28, 40, 255});
        thumbPlaceholder->setContentSize({thumbSize * 1.6f, thumbSize});
        thumbPlaceholder->setPosition({4.f, (cellH - 2.f - thumbSize) / 2});
        cell->addChild(thumbPlaceholder, 1);

        int levelID = entry.levelId;
        auto localTex = LocalThumbs::get().loadTexture(levelID);
        if (localTex) {
            auto spr = CCSprite::createWithTexture(localTex);
            if (spr) {
                float tw = thumbSize * 1.6f;
                float th = thumbSize;
                spr->setScale(std::max(tw / spr->getContentSize().width, th / spr->getContentSize().height));
                spr->setPosition({tw / 2, th / 2});
                thumbPlaceholder->addChild(spr);
            }
        } else if (levelID > 0) {
            std::string fileName = fmt::format("{}.png", levelID);
            Ref<CCLayerColor> safePlaceholder = thumbPlaceholder;
            ThumbnailLoader::get().requestLoad(levelID, fileName, [safePlaceholder, thumbSize](CCTexture2D* tex, bool) {
                if (!safePlaceholder || !safePlaceholder->getParent() || !tex) return;

                auto spr = CCSprite::createWithTexture(tex);
                if (!spr) return;

                float tw = thumbSize * 1.6f;
                float th = thumbSize;
                spr->setScale(std::max(tw / spr->getContentSize().width, th / spr->getContentSize().height));
                spr->setPosition({tw / 2, th / 2});
                safePlaceholder->addChild(spr);
            });
        }

        float textX = thumbSize * 1.6f + 12.f;
        float cellMidY = (cellH - 2.f) / 2;

        // puesto
        auto numLbl = CCLabelBMFont::create(fmt::format("#{}", i + 1).c_str(), "chatFont.fnt");
        numLbl->setScale(0.4f);
        numLbl->setColor({255, 200, 50});
        numLbl->setAnchorPoint({0, 0.5f});
        numLbl->setPosition({textX, cellMidY + 14.f});
        cell->addChild(numLbl, 10);

        // nombre del nivel
        auto saved = GameLevelManager::get()->getSavedLevel(levelID);
        std::string levelName = saved ? std::string(saved->m_levelName) : fmt::format("{} {}", loc.getString("community.level"), levelID);
        auto nameLbl = CCLabelBMFont::create(levelName.c_str(), "bigFont.fnt");
        nameLbl->setScale(0.35f);
        nameLbl->setAnchorPoint({0, 0.5f});
        nameLbl->setPosition({textX, cellMidY + 2.f});
        float maxNameW = listW - textX - 80.f;
        if (nameLbl->getScaledContentSize().width > maxNameW) {
            nameLbl->setScale(nameLbl->getScale() * (maxNameW / nameLbl->getScaledContentSize().width));
        }
        cell->addChild(nameLbl, 10);

        // creador y rating
        auto infoStr = fmt::format("{} {} | {}: {:.1f} ({} {})",
            loc.getString("community.by"), entry.uploadedBy,
            loc.getString("community.rating"), entry.rating,
            entry.count, loc.getString("community.votes"));
        auto infoLbl = CCLabelBMFont::create(infoStr.c_str(), "chatFont.fnt");
        infoLbl->setScale(0.35f);
        infoLbl->setColor({180, 200, 220});
        infoLbl->setAnchorPoint({0, 0.5f});
        infoLbl->setPosition({textX, cellMidY - 10.f});
        cell->addChild(infoLbl, 10);
    }

    // arranco arriba
    scrollView->m_contentLayer->setPositionY(listH - totalH);
}

#include "LeaderboardLayer.hpp"
#include <Geode/modify/CreatorLayer.hpp>
#include "../managers/LocalThumbs.hpp"
#include "../managers/ThumbnailLoader.hpp"
#include "../managers/ThumbnailAPI.hpp"
#include "../utils/Localization.hpp"
#include "../utils/HttpClient.hpp"
#include <Geode/utils/web.hpp>
#include <matjson.hpp>
#include <Geode/binding/LevelInfoLayer.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include "../utils/Shaders.hpp"
#include <thread>

using namespace geode::prelude;
using namespace Shaders;

// shaders viven en Shaders.hpp

namespace {
    class LeaderboardPaimonSprite : public CCSprite {
    public:
        float m_intensity = 1.0f;
        float m_brightness = 1.0f;
        CCSize m_texSize = {0, 0};
        
        static LeaderboardPaimonSprite* create() {
            auto sprite = new LeaderboardPaimonSprite();
            if (sprite && sprite->init()) {
                sprite->autorelease();
                return sprite;
            }
            CC_SAFE_DELETE(sprite);
            return nullptr;
        }

        static LeaderboardPaimonSprite* createWithTexture(CCTexture2D* texture) {
            auto sprite = new LeaderboardPaimonSprite();
            if (sprite && sprite->initWithTexture(texture)) {
                sprite->autorelease();
                return sprite;
            }
            CC_SAFE_DELETE(sprite);
            return nullptr;
        }

        void draw() override {
            if (getShaderProgram()) {
                getShaderProgram()->use();
                getShaderProgram()->setUniformsForBuiltins();
                
                GLint intensityLoc = getShaderProgram()->getUniformLocationForName("u_intensity");
                if (intensityLoc != -1) {
                    getShaderProgram()->setUniformLocationWith1f(intensityLoc, m_intensity);
                }
                
                GLint brightLoc = getShaderProgram()->getUniformLocationForName("u_brightness");
                if (brightLoc != -1) {
                    getShaderProgram()->setUniformLocationWith1f(brightLoc, m_brightness);
                }

                GLint sizeLoc = getShaderProgram()->getUniformLocationForName("u_texSize");
                if (sizeLoc != -1) {
                    if (m_texSize.width == 0 && getTexture()) {
                        m_texSize = getTexture()->getContentSizeInPixels();
                    }
                    float w = m_texSize.width > 0 ? m_texSize.width : 1.0f;
                    float h = m_texSize.height > 0 ? m_texSize.height : 1.0f;
                    getShaderProgram()->setUniformLocationWith2f(sizeLoc, w, h);
                }
            }
            CCSprite::draw();
        }
    };
}

static LeaderboardPaimonSprite* createLeaderboardBlurredSprite(CCTexture2D* texture, CCSize const& targetSize, float blurRadius = 0.04f) {
    auto blurResult = Shaders::createBlurredSprite(texture, targetSize, blurRadius, true);
    if (!blurResult) return nullptr;
    auto finalSprite = LeaderboardPaimonSprite::createWithTexture(blurResult->getTexture());
    if (finalSprite) finalSprite->setFlipY(true);
    return finalSprite;
}

static void calculateLevelCellThumbScale(CCSprite* sprite, float bgWidth, float bgHeight, float widthFactor, float& outScaleX, float& outScaleY) {
    if (!sprite) return;
    
    const float contentWidth = sprite->getContentSize().width;
    const float contentHeight = sprite->getContentSize().height;
    const float desiredWidth = bgWidth * widthFactor;
    
    outScaleY = bgHeight / contentHeight;
    
    float minScaleX = outScaleY; 
    float desiredScaleX = desiredWidth / contentWidth;
    outScaleX = std::max(minScaleX, desiredScaleX);
}

LeaderboardLayer* LeaderboardLayer::create() {
    auto ret = new LeaderboardLayer();
    if (ret && ret->init()) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

CCScene* LeaderboardLayer::scene() {
    auto scene = CCScene::create();
    auto layer = LeaderboardLayer::create();
    scene->addChild(layer);
    return scene;
}

bool LeaderboardLayer::init() {
    if (!CCLayer::init()) return false;
    
    m_page = 0;
    m_allItems = nullptr;
    
    auto winSize = CCDirector::sharedDirector()->getWinSize();

    // fondo base
    auto bg = CCSprite::create("GJ_gradientBG.png");
    bg->setID("background");
    bg->setPosition(winSize / 2);
    bg->setScaleX(winSize.width / bg->getContentSize().width);
    bg->setScaleY(winSize.height / bg->getContentSize().height);
    bg->setColor({20, 20, 20}); // fondo oscurito
    bg->setZOrder(-10); // bien atrás
    this->addChild(bg);

    // fondo dinámico encima
    m_bgSprite = LeaderboardPaimonSprite::create(); 
    m_bgSprite->setPosition(winSize / 2);
    m_bgSprite->setVisible(false);
    m_bgSprite->setZOrder(-5); // encima del gradient
    this->addChild(m_bgSprite);

    // capa negra para transiciones
    m_bgOverlay = CCLayerColor::create({0, 0, 0, 0});
    m_bgOverlay->setContentSize(winSize);
    m_bgOverlay->setZOrder(-4); // encima del fondo blur
    this->addChild(m_bgOverlay);

    this->scheduleUpdate();

    // botón de volver
    auto menu = CCMenu::create();
    menu->setPosition(0, 0);
    this->addChild(menu);

    auto backBtn = CCMenuItemSpriteExtra::create(
        CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png"),
        this,
        menu_selector(LeaderboardLayer::onBack)
    );
    backBtn->setPosition(25, winSize.height - 25);
    menu->addChild(backBtn);

    // sin título, que quede limpio

    // pestañas de arriba
    auto tabMenu = CCMenu::create();
    tabMenu->setPosition(0, 0); // pos manual
    tabMenu->setZOrder(10);
    this->addChild(tabMenu);
    m_tabsMenu = tabMenu;

    auto createTab = [&](const char* text, const char* id, CCPoint pos) -> CCMenuItemToggler* {
        // manual pa no pelearse con ButtonSprite
        auto createBtn = [&](const char* frameName) -> CCNode* {
            auto sprite = cocos2d::extension::CCScale9Sprite::createWithSpriteFrameName(frameName);
            sprite->setContentSize({100.f, 30.f});
            
            auto label = CCLabelBMFont::create(text, "goldFont.fnt");
            label->setScale(0.6f);
            label->setPosition({sprite->getContentSize().width / 2, sprite->getContentSize().height / 2 + 2.f});
            sprite->addChild(label);
            
            return sprite;
        };

        auto onSprite = createBtn("GJ_longBtn01_001.png");
        auto offSprite = createBtn("GJ_longBtn02_001.png");
        
        auto tab = CCMenuItemToggler::create(offSprite, onSprite, this, menu_selector(LeaderboardLayer::onTab));
        tab->setUserObject(CCString::create(id));
        tab->setPosition(pos);
        m_tabs.push_back(tab);
        return tab;
    };

    // layout general
    float topY = winSize.height - 40.f;
    float centerX = winSize.width / 2;
    float btnSpacing = 105.f;
    // botones arriba: diario | semanal | siempre | creadores
    
    auto dailyBtn = createTab(Localization::get().getString("leaderboard.daily").c_str(), "daily", {centerX - btnSpacing * 1.5f, topY});
    dailyBtn->toggle(true); 
    tabMenu->addChild(dailyBtn);

    auto weeklyBtn = createTab(Localization::get().getString("leaderboard.weekly").c_str(), "weekly", {centerX - btnSpacing * 0.5f, topY});
    tabMenu->addChild(weeklyBtn);

    auto allTimeBtn = createTab(Localization::get().getString("leaderboard.all_time").c_str(), "alltime", {centerX + btnSpacing * 0.5f, topY});
    tabMenu->addChild(allTimeBtn);

    auto creatorsBtn = createTab(Localization::get().getString("leaderboard.creators").c_str(), "creators", {centerX + btnSpacing * 1.5f, topY});
    tabMenu->addChild(creatorsBtn);

    // spinner
    m_loadingSpinner = CCSprite::createWithSpriteFrameName("loadingCircle.png");
    if (!m_loadingSpinner) {
        m_loadingSpinner = CCSprite::create("loadingCircle.png");
    }
    if (m_loadingSpinner) {
        m_loadingSpinner->setPosition(winSize / 2);
        m_loadingSpinner->setScale(1.0f);
        m_loadingSpinner->setVisible(false);
        this->addChild(m_loadingSpinner, 100);
    }

    this->setKeypadEnabled(true);

    // carga inicial de la daily
    if (m_loadingSpinner) {
        m_loadingSpinner->setVisible(true);
        m_loadingSpinner->runAction(CCRepeatForever::create(CCRotateBy::create(1.0f, 360.f)));
    }

    loadLeaderboard("daily");

    return true;
}

void LeaderboardLayer::onBack(CCObject*) {
    CC_SAFE_RELEASE_NULL(m_allItems);
    if (GameLevelManager::get()->m_levelManagerDelegate == this) {
        GameLevelManager::get()->m_levelManagerDelegate = nullptr;
    }
    CCDirector::sharedDirector()->replaceScene(CCTransitionFade::create(0.5f, CreatorLayer::scene()));
}

void LeaderboardLayer::keyBackClicked() {
    onBack(nullptr);
}

void LeaderboardLayer::onTab(CCObject* sender) {
    auto toggler = static_cast<CCMenuItemToggler*>(sender);
    auto type = static_cast<CCString*>(toggler->getUserObject())->getCString();
    
    if (m_currentType == type) {
        toggler->toggle(true); // on
        return;
    }
    m_currentType = type;
    
    // reinicia la paginación
    m_page = 0;
    CC_SAFE_RELEASE_NULL(m_allItems);

    // marca las otras pestañas bien
    for (auto tab : m_tabs) {
        tab->toggle(tab == toggler);
    }

    // limpia la lista vieja
    if (this->getChildByTag(999)) {
        this->removeChildByTag(999);
    }
    m_scroll = nullptr;
    m_listMenu = nullptr;

    // muestra el spinner
    if (m_loadingSpinner) {
        m_loadingSpinner->setVisible(true);
        m_loadingSpinner->stopAllActions();
        m_loadingSpinner->runAction(CCRepeatForever::create(CCRotateBy::create(1.0f, 360.f)));
    }
    
    loadLeaderboard(type);
}

void LeaderboardLayer::loadLeaderboard(std::string type) {
    if (m_featuredLevel) {
        m_featuredLevel->release();
        m_featuredLevel = nullptr;
    }
    m_featuredExpiresAt = 0;

    if (type == "daily" || type == "weekly") {
        this->retain();
        HttpClient::get().get("/api/" + type + "/current", [this, type](bool success, const std::string& json) {
            if (success) {
                auto dataRes = matjson::parse(json);
                if (dataRes.isOk()) {
                    auto data = dataRes.unwrap();
                    if (data["success"].asBool().unwrapOr(false)) {
                        auto levelData = data["data"];
                        int levelID = levelData["levelID"].asInt().unwrapOr(0);
                        m_featuredExpiresAt = (long long)levelData["expiresAt"].asDouble().unwrapOr(0);

                        if (levelID > 0) {
                            auto level = GJGameLevel::create();
                            level->m_levelID = levelID;
                            level->m_levelName = Localization::get().getString("leaderboard.loading");
                            level->m_creatorName = "";

                            // niveles guardados primero
                            auto saved = GameLevelManager::get()->getSavedLevel(levelID);
                            if (saved) {
                                level->m_levelName = saved->m_levelName;
                                level->m_creatorName = saved->m_creatorName;
                                level->m_stars = saved->m_stars;
                                level->m_difficulty = saved->m_difficulty;
                                level->m_demon = saved->m_demon;
                                level->m_demonDifficulty = saved->m_demonDifficulty;
                                level->m_songID = saved->m_songID;
                                level->m_audioTrack = saved->m_audioTrack;
                                level->m_levelString = saved->m_levelString;
                            }

                            level->retain();
                            if (m_featuredLevel) m_featuredLevel->release();
                            m_featuredLevel = level;

                            // info completa server gd
                            auto searchObj = GJSearchObject::create(SearchType::MapPackOnClick, std::to_string(levelID));
                            auto glm = GameLevelManager::get();
                            glm->m_levelManagerDelegate = this;
                            glm->getOnlineLevels(searchObj);
                        }
                    }
                }
            }

            if (m_loadingSpinner) {
                m_loadingSpinner->setVisible(false);
                m_loadingSpinner->stopAllActions();
            }

            if (m_featuredLevel) {
                this->updateBackground(m_featuredLevel->m_levelID);
            } else {
                this->updateBackground(0);
            }

            this->createList(nullptr, type);
            this->release();
        });
    } else {
        fetchLeaderboardList(type);
    }
}

void LeaderboardLayer::fetchLeaderboardList(std::string type) {
    // uso HttpClient en hilo aparte
    this->retain();
    HttpClient::get().get("/api/leaderboard?type=" + type, [this, type](bool success, const std::string& response) {
        if (success) {
            onLeaderboardLoaded(type, response);
        } else {
            if (m_loadingSpinner) {
                m_loadingSpinner->setVisible(false);
                m_loadingSpinner->stopAllActions();
            }
            std::string msg = Localization::get().getString("leaderboard.load_error");
            FLAlertLayer::create(Localization::get().getString("leaderboard.error").c_str(), msg.c_str(), "OK")->show();
        }
        this->release();
    });
}

void LeaderboardLayer::onLeaderboardLoaded(std::string type, std::string json) {
    if (m_loadingSpinner) {
        m_loadingSpinner->setVisible(false);
        m_loadingSpinner->stopAllActions();
    }
    
    try {
        auto dataRes = matjson::parse(json);
        if (!dataRes) {
            FLAlertLayer::create(Localization::get().getString("leaderboard.error").c_str(), Localization::get().getString("leaderboard.parse_error").c_str(), "OK")->show();
            return;
        }
        auto data = dataRes.unwrap();

        if (!data.contains("success") || !data["success"].asBool().unwrapOr(false)) {
            FLAlertLayer::create(Localization::get().getString("leaderboard.error").c_str(), Localization::get().getString("leaderboard.server_error").c_str(), "OK")->show();
            return;
        }

        if (!data["data"].isArray()) {
             FLAlertLayer::create(Localization::get().getString("leaderboard.error").c_str(), Localization::get().getString("leaderboard.invalid_format").c_str(), "OK")->show();
             return;
        }

        auto items = data["data"].asArray().unwrap();
        auto ccArray = CCArray::create();

        std::vector<int> levelIDs;
        std::string searchIDs = "";
        bool needsSearch = false;

        for (auto& item : items) {
            if (type == "creators") {
                auto score = GJUserScore::create();
                score->m_userName = item["username"].asString().unwrapOr(std::string(Localization::get().getString("leaderboard.unknown")));
                score->m_stars = item["totalStars"].asInt().unwrapOr(0); 
                score->m_globalRank = item["totalVotes"].asInt().unwrapOr(0); 
                score->m_iconID = 1; // icono default
                score->m_color1 = 10;
                score->m_color2 = 11;
                score->m_iconType = IconType::Cube;
                ccArray->addObject(score);
            } else {
                int levelID = item["levelId"].asInt().unwrapOr(0);
                if (levelID > 0) levelIDs.push_back(levelID);

                auto level = GJGameLevel::create();
                level->m_levelID = levelID;
                level->m_levelName = Localization::get().getString("leaderboard.loading");
                level->m_creatorName = item["uploadedBy"].asString().unwrapOr(std::string(Localization::get().getString("leaderboard.unknown")));
                
                // el rating lo meto en userObject
                double rating = item["rating"].asDouble().unwrapOr(0.0);
                int count = item["count"].asInt().unwrapOr(0);
                auto ratingStr = CCString::createWithFormat("%.1f/5 (%d)", rating, count);
                level->setUserObject(ratingStr);

                // nivel guardado en caché
                auto savedLevel = GameLevelManager::get()->getSavedLevel(levelID);
                if (savedLevel && savedLevel->m_levelName.length() > 0) {
                    level->m_levelName = savedLevel->m_levelName;
                    level->m_creatorName = savedLevel->m_creatorName;
                    level->m_stars = savedLevel->m_stars;
                    level->m_difficulty = savedLevel->m_difficulty;
                    level->m_demon = savedLevel->m_demon;
                    level->m_demonDifficulty = savedLevel->m_demonDifficulty;
                    level->m_userID = savedLevel->m_userID;
                    level->m_accountID = savedLevel->m_accountID;
                } else {
                    if (!searchIDs.empty()) searchIDs += ",";
                    searchIDs += std::to_string(levelID);
                    needsSearch = true;
                }

                ccArray->addObject(level);
            }
        }

        if (m_allItems) m_allItems->release();
        m_allItems = ccArray;
        m_allItems->retain();

        if (needsSearch) {
            auto searchObj = GJSearchObject::create(SearchType::MapPackOnClick, searchIDs);
            auto glm = GameLevelManager::get();
            glm->m_levelManagerDelegate = this;
            glm->getOnlineLevels(searchObj);
        }

        // elijo un nivel random pa el fondo
        if (!levelIDs.empty()) {
            // random puro
            int idx = rand() % levelIDs.size();
            this->updateBackground(levelIDs[idx]);
        }

        refreshList();

    } catch (std::exception& e) {
        FLAlertLayer::create(Localization::get().getString("leaderboard.error").c_str(), Localization::get().getString("leaderboard.parse_error").c_str(), "OK")->show();
    }
}

void LeaderboardLayer::refreshList() {
    if (!m_allItems) {
        createList(nullptr, m_currentType);
        return;
    }

    // si no hay datos, tiro de todo; daily/weekly van aparte
    if (m_currentType == "daily" || m_currentType == "weekly") {
        createList(m_allItems, m_currentType);
        return;
    }

    int totalItems = m_allItems->count();
    int start = m_page * ITEMS_PER_PAGE;
    int end = std::min(start + ITEMS_PER_PAGE, totalItems);
    
    auto pageItems = CCArray::create();
    for (int i = start; i < end; i++) {
        pageItems->addObject(m_allItems->objectAtIndex(i));
    }
    
    createList(pageItems, m_currentType);
}

void LeaderboardLayer::onNextPage(CCObject*) {
    if (!m_allItems) return;
    int totalPages = (m_allItems->count() + ITEMS_PER_PAGE - 1) / ITEMS_PER_PAGE;
    if (m_page < totalPages - 1) {
        m_page++;
        refreshList();
    }
}

void LeaderboardLayer::onPrevPage(CCObject*) {
    if (m_page > 0) {
        m_page--;
        refreshList();
    }
}

void LeaderboardLayer::createList(CCArray* items, std::string type) {
    static int LIST_CONTAINER_TAG = 999;

    this->removeChildByTag(LIST_CONTAINER_TAG);

    auto winSize = CCDirector::sharedDirector()->getWinSize();
    
    // layout ancho centrado
    float width = 400.f;
    float height = 220.f;
    float xPos = winSize.width / 2 - width / 2;
    float yPos = winSize.height / 2 - height / 2 - 15.f;

    auto container = CCNode::create();
    container->setTag(LIST_CONTAINER_TAG);
    this->addChild(container);

    // panel redondeado de fondo
    auto panel = cocos2d::extension::CCScale9Sprite::createWithSpriteFrameName("square02b_001.png");
    if (!panel) {
        CCSpriteFrameCache::sharedSpriteFrameCache()->addSpriteFramesWithFile("GJ_GameSheet03.plist");
        panel = cocos2d::extension::CCScale9Sprite::createWithSpriteFrameName("square02b_001.png");
        if (!panel) panel = cocos2d::extension::CCScale9Sprite::createWithSpriteFrameName("square02_001.png");
    }
    
    if (panel) {
        panel->setColor({0, 0, 0});
        panel->setOpacity(80);
        panel->setContentSize({width + 10, height + 10});
        panel->setPosition({winSize.width / 2, winSize.height / 2 - 15.f});
        container->addChild(panel, -1);
    }

    m_listMenu = CCLayer::create();
    m_listMenu->setPosition({0, 0});

    m_scroll = cocos2d::extension::CCScrollView::create();
    m_scroll->setViewSize({width, height});
    m_scroll->setPosition({xPos, yPos});
    m_scroll->setDirection(cocos2d::extension::kCCScrollViewDirectionVertical);
    m_scroll->setContainer(m_listMenu);
    container->addChild(m_scroll);

    // controles de paginación
    if (m_allItems && m_allItems->count() > ITEMS_PER_PAGE && type != "daily" && type != "weekly") {
        auto menu = CCMenu::create();
        menu->setPosition({winSize.width / 2, winSize.height / 2 - 15.f});
        container->addChild(menu, 10);

        auto prevSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_03_001.png");
        prevSpr->setScale(0.6f);
        prevSpr->setOpacity(m_page == 0 ? 100 : 255);
        auto prevBtn = CCMenuItemSpriteExtra::create(prevSpr, this, menu_selector(LeaderboardLayer::onPrevPage));
        prevBtn->setPosition({-220.f, 0.f});
        if (m_page == 0) prevBtn->setEnabled(false);
        menu->addChild(prevBtn);
        
        auto nextSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_03_001.png");
        nextSpr->setFlipX(true);
        nextSpr->setScale(0.6f);
        auto nextBtn = CCMenuItemSpriteExtra::create(nextSpr, this, menu_selector(LeaderboardLayer::onNextPage));
        nextBtn->setPosition({220.f, 0.f});

        int totalPages = (m_allItems->count() + ITEMS_PER_PAGE - 1) / ITEMS_PER_PAGE;
        if (m_page >= totalPages - 1) {
            nextBtn->setEnabled(false);
            nextSpr->setOpacity(100);
        }
        menu->addChild(nextBtn);
        
        // texto "página X/Y"
        auto pageLbl = CCLabelBMFont::create(fmt::format("{}/{}", m_page + 1, totalPages).c_str(), "chatFont.fnt");
        pageLbl->setScale(0.6f);
        pageLbl->setOpacity(180);
        pageLbl->setPosition({0.f, -125.f});
        menu->addChild(pageLbl);
    }

    // helper para crear celdas
    // guardo self para callbacks
    LeaderboardLayer* self = this;
    auto createMinimalCell = [&, self](CCObject* obj, float w, float h, float y, bool isFeatured, int rank) {
        auto cell = CCNode::create();
        cell->setContentSize({w, h});
        cell->setAnchorPoint({0.5f, 0.5f});
        cell->setPosition({width / 2, y});
        m_listMenu->addChild(cell);

        // animación de entrada suave
        float delay = rank > 0 ? (rank - 1) * 0.02f : 0.f;
        cell->setScale(0.85f);
        cell->runAction(CCSequence::create(
            CCDelayTime::create(delay),
            CCEaseBackOut::create(CCScaleTo::create(0.25f, 1.0f)),
            nullptr
        ));

        // fondo de la celda, redondeado
        auto rowBg = cocos2d::extension::CCScale9Sprite::createWithSpriteFrameName("square02b_001.png");
        if (!rowBg) rowBg = cocos2d::extension::CCScale9Sprite::createWithSpriteFrameName("square02_001.png");

        if (rowBg) {
            rowBg->setColor({18, 18, 24}); // oscuro
            rowBg->setOpacity(isFeatured ? 255 : 240); // opaco
            rowBg->setContentSize(cell->getContentSize());
            rowBg->setPosition(cell->getContentSize() / 2);
            rowBg->setZOrder(0);
            cell->addChild(rowBg);
        }

        // menú para que la celda sea clickeable
        auto cellMenu = CCMenu::create();
        cellMenu->setPosition({0, 0});
        cellMenu->setContentSize(cell->getContentSize());
        cell->addChild(cellMenu, 50);

        // datos de nivel o creador
        std::string nameStr = "Unknown";
        std::string creatorStr = "";
        std::string ratingStr = "";
        int levelID = 0;
        
        if (type == "creators") {
            auto score = static_cast<GJUserScore*>(obj);
            nameStr = score->m_userName;
            creatorStr = fmt::format("{} uploads", score->m_stars);
        } else {
            auto level = static_cast<GJGameLevel*>(obj);
            nameStr = level->m_levelName;
            creatorStr = "by " + std::string(level->m_creatorName);
            levelID = level->m_levelID;
            auto ratingObj = static_cast<CCString*>(level->getUserObject());
            if (ratingObj) ratingStr = ratingObj->getCString();
        }

        // ===== miniatura =====
        float thumbW = 0.f;
        GJGameLevel* levelPtr = nullptr;

        if (type != "creators") {
            levelPtr = static_cast<GJGameLevel*>(obj);
            levelID = levelPtr->m_levelID;
            log::debug("[LeaderboardLayer] createMinimalCell: levelID={}, isFeatured={}", levelID, isFeatured);
        }

        if (levelID > 0) {
            float aspectRatio = 16.f / 9.f;
            float targetW = h * aspectRatio;

            // ancho maximo
            if (isFeatured) {
                targetW = std::min(targetW, w * 0.45f);
            } else {
                targetW = std::min(targetW, w * 0.35f);
            }

            thumbW = targetW;

            // crear thumb
            auto createThumb = [](CCNode* targetCell, CCTexture2D* tex, float tW, float tH) {
                if (!tex || !targetCell) return;

                // quitar thumb anterior
                targetCell->removeChildByTag(101);

                // contenedor thumb
                auto thumbContainer = CCNode::create();
                thumbContainer->setContentSize({tW, tH});
                thumbContainer->setAnchorPoint({0, 0});
                thumbContainer->setPosition({0, 0});
                thumbContainer->setTag(101);

                // sprite img
                auto sprite = CCSprite::createWithTexture(tex);
                if (sprite) {
                    float imgW = sprite->getContentSize().width;
                    float imgH = sprite->getContentSize().height;

                    // escala cubrir, ref altura
                    float scale = tH / imgH;

                    // si ancho menor, usar ancho
                    if (imgW * scale < tW) {
                        scale = tW / imgW;
                    }

                    sprite->setScale(scale);
                    float scaledW = imgW * scale;
                    float xPos = std::min(tW / 2, scaledW / 2); // no salir derecha
                    sprite->setPosition({xPos, tH / 2});
                    thumbContainer->addChild(sprite);
                }

                // gradiente borde derecho
                auto gradient = CCLayerGradient::create({0, 0, 0, 0}, {0, 0, 0, 100}, {1, 0});
                gradient->setContentSize({tW * 0.4f, tH});
                gradient->setPosition({tW * 0.6f, 0});
                thumbContainer->addChild(gradient, 5);

                targetCell->addChild(thumbContainer, 1);

                log::debug("[LeaderboardLayer] Thumbnail created: {}x{}", tW, tH);
            };

            // textura local primero
            auto texture = LocalThumbs::get().loadTexture(levelID);
            log::debug("[LeaderboardLayer] loadTexture result for {}: {}", levelID, texture ? "OK" : "NULL");
            if (texture) {
                createThumb(cell, texture, thumbW, h);
            } else {
                // placeholder carga
                auto placeholder = CCLayerColor::create({30, 30, 40, 255});
                placeholder->setContentSize({thumbW, h});
                placeholder->setTag(101);
                cell->addChild(placeholder, 1);

                auto spinner = CCSprite::createWithSpriteFrameName("loadingCircle.png");
                if (spinner) {
                    spinner->setScale(0.35f);
                    spinner->setPosition({thumbW / 2, h / 2});
                    spinner->setColor({70, 70, 80});
                    spinner->runAction(CCRepeatForever::create(CCRotateBy::create(1.0f, 360.f)));
                    placeholder->addChild(spinner);
                }

                // carga asincrona
                std::string fileName = fmt::format("{}.png", levelID);
                cell->retain();

                float capW = thumbW;
                float capH = h;

                log::debug("[LeaderboardLayer] Requesting async load for level {}", levelID);

                ThumbnailLoader::get().requestLoad(levelID, fileName, [cell, capW, capH, createThumb, levelID](CCTexture2D* tex, bool success) {
                    log::debug("[LeaderboardLayer] Async load callback for {}: tex={}, success={}", levelID, tex ? "OK" : "NULL", success);
                    geode::Loader::get()->queueInMainThread([cell, tex, capW, capH, createThumb] {
                        if (cell->getParent()) {
                            if (tex) {
                                createThumb(cell, tex, capW, capH);
                            }
                        }
                        cell->release();
                    });
                });
            }
        }

        // btn sobre thumb
        if (type != "creators" && levelPtr && thumbW > 0) {
            levelPtr->retain();

            // ccmenuitemspriteextra semi-transparente
            // opacidad 1 = casi invisible, clickeable
            auto hitArea = CCSprite::createWithSpriteFrameName("GJ_square01.png");
            if (!hitArea) {
                hitArea = CCSprite::create("square.png");
            }
            if (!hitArea) {
                // fallback sprite vacio
                hitArea = CCSprite::create();
                if (hitArea) {
                    hitArea->setTextureRect(CCRect(0, 0, 1, 1));
                }
            }

            if (hitArea) {
                // escala cubrir thumb
                float sprW = hitArea->getContentSize().width;
                float sprH = hitArea->getContentSize().height;
                if (sprW > 0 && sprH > 0) {
                    hitArea->setScaleX(thumbW / sprW);
                    hitArea->setScaleY(h / sprH);
                }
                hitArea->setOpacity(1); // invisible, clickeable
                hitArea->setColor({0, 0, 0}); // negro

                auto thumbBtn = CCMenuItemSpriteExtra::create(hitArea, self, menu_selector(LeaderboardLayer::onViewLevel));
                thumbBtn->setUserObject(levelPtr);
                thumbBtn->setPosition({thumbW / 2, h / 2});
                cellMenu->addChild(thumbBtn, 100);
            }
        }

        // ===== indicador de rango =====
        if (rank > 0 && !isFeatured) {
            ccColor3B rankColor = {200, 200, 200};
            float rankScale = 0.45f;

            if (rank == 1) { rankColor = {255, 215, 0}; rankScale = 0.55f; }
            else if (rank == 2) { rankColor = {220, 220, 230}; rankScale = 0.5f; }
            else if (rank == 3) { rankColor = {205, 127, 50}; rankScale = 0.5f; }

            // fondo rango sobre thumb
            auto rankBg = CCLayerColor::create({0, 0, 0, 150});
            rankBg->setContentSize({22.f, 14.f});
            if (rank >= 10) rankBg->setContentSize({28.f, 14.f});
            if (rank >= 100) rankBg->setContentSize({34.f, 14.f});

            rankBg->setPosition({4.f, h - 18.f});
            cell->addChild(rankBg, 20);

            auto rankLbl = CCLabelBMFont::create(fmt::format("#{}", rank).c_str(), "chatFont.fnt");
            rankLbl->setScale(rankScale);
            rankLbl->setColor(rankColor);
            rankLbl->setPosition(rankBg->getContentSize() / 2);
            rankBg->addChild(rankLbl);
        }

        // ===== textos =====
        float textPadding = 12.f;
        float textX = (levelID > 0) ? (thumbW + textPadding) : 15.f;
        float availableWidth = w - textX - 15.f; // margen derecho

        float scaleMult = isFeatured ? 1.3f : 1.0f;

        auto nameLbl = CCLabelBMFont::create(nameStr.c_str(), "bigFont.fnt");
        nameLbl->setScale(0.5f * scaleMult);
        nameLbl->setAnchorPoint({0.f, 0.5f});
        nameLbl->setPosition({textX, h / 2 + (isFeatured ? 15.f : 8.f)});
        if (nameLbl->getScaledContentSize().width > availableWidth) {
            nameLbl->setScale(nameLbl->getScale() * (availableWidth / nameLbl->getScaledContentSize().width));
        }
        cell->addChild(nameLbl, 10);

        auto creatorLbl = CCLabelBMFont::create(creatorStr.c_str(), "chatFont.fnt");
        creatorLbl->setScale(0.55f * scaleMult);
        creatorLbl->setColor({170, 170, 180});
        creatorLbl->setAnchorPoint({0.f, 0.5f});
        creatorLbl->setPosition({textX, h / 2 - (isFeatured ? 8.f : 6.f)});
        cell->addChild(creatorLbl, 10);

        // rating
        if (!ratingStr.empty() && !isFeatured) {
            auto ratingLbl = CCLabelBMFont::create(ratingStr.c_str(), "chatFont.fnt");
            ratingLbl->setScale(0.4f);
            ratingLbl->setColor({255, 200, 100});
            ratingLbl->setAnchorPoint({0.f, 0.5f});
            ratingLbl->setPosition({textX, h / 2 - 20.f});
            cell->addChild(ratingLbl, 10);
        }


        // ===== cuenta regresiva para destacado =====
        if (isFeatured && m_featuredExpiresAt > 0) {
            long long now = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            long long diff = m_featuredExpiresAt - now;

            if (diff > 0) {
                int hours = diff / (1000 * 60 * 60);
                int mins = (diff % (1000 * 60 * 60)) / (1000 * 60);

                auto timeLbl = CCLabelBMFont::create(fmt::format("Ends in {}h {}m", hours, mins).c_str(), "chatFont.fnt");
                timeLbl->setScale(0.45f);
                timeLbl->setColor({255, 180, 100});
                timeLbl->setAnchorPoint({1.f, 0.5f});
                timeLbl->setPosition({w - 15.f, h - 20.f});
                cell->addChild(timeLbl, 10);
            }
        }
    };

    // vista daily/weekly
    if (type == "daily" || type == "weekly") {
        if (m_featuredLevel) {
            float cardH = 150.f;
            float cardW = width - 20.f;
            m_listMenu->setContentSize({width, height});

            float y = height / 2;
            createMinimalCell(m_featuredLevel, cardW, cardH, y, true, 0);
        } else {
            auto lbl = CCLabelBMFont::create("No Featured Level", "chatFont.fnt");
            lbl->setScale(0.7f);
            lbl->setOpacity(150);
            lbl->setPosition({winSize.width / 2, winSize.height / 2 - 15.f});
            container->addChild(lbl);
        }
        return;
    }

    // lista normal
    if (!items || items->count() == 0) {
        auto lbl = CCLabelBMFont::create("No items found", "chatFont.fnt");
        lbl->setScale(0.7f);
        lbl->setOpacity(150);
        lbl->setPosition({winSize.width / 2, winSize.height / 2 - 15.f});
        container->addChild(lbl);
        return;
    }

    float rowH = 55.f;
    float rowSpacing = 4.f;
    float totalH = std::max(height, items->count() * (rowH + rowSpacing));
    m_listMenu->setContentSize({width, totalH});

    for (int i = 0; i < items->count(); ++i) {
        CCObject* obj = items->objectAtIndex(i);
        float y = totalH - (i + 1) * (rowH + rowSpacing) + rowH / 2 + rowSpacing / 2;
        createMinimalCell(obj, width - 15.f, rowH, y, false, i + 1 + (m_page * ITEMS_PER_PAGE));
    }

    m_scroll->setContentOffset({0, height - totalH});
}

void LeaderboardLayer::onViewLevel(CCObject* sender) {
    auto btn = static_cast<CCMenuItemSpriteExtra*>(sender);
    auto level = static_cast<GJGameLevel*>(btn->getUserObject());
    if (level) {
        // nivel + musica desde cache
        auto savedLevel = GameLevelManager::get()->getSavedLevel(level->m_levelID);
        GJGameLevel* levelToUse = level;

        if (savedLevel) {
            // nivel guardado con musica
            levelToUse = savedLevel;
        } else {
            // no nivel guardado -> copiar info que tengamos
            // levelinfolayer descarga resto
        }

        auto layer = LevelInfoLayer::create(levelToUse, false);
        auto scene = CCScene::create();
        scene->addChild(layer);
        CCDirector::sharedDirector()->replaceScene(CCTransitionFade::create(0.5f, scene));
    }
}

void LeaderboardLayer::update(float dt) {
    m_blurTime += dt;
    if (m_bgSprite) {
        // nuestro sprite?
        if (auto paimonSprite = typeinfo_cast<LeaderboardPaimonSprite*>(m_bgSprite)) {
             // desenfoque 0-1.5
             float intensity = 0.75f + std::sin(m_blurTime * 1.0f) * 0.75f;
             paimonSprite->m_intensity = intensity;
        }
    }
}

void LeaderboardLayer::applyBackground(CCTexture2D* texture) {
    if (!texture) return;

    log::info("[LeaderboardLayer] Applying background texture: {}", texture);

    auto winSize = CCDirector::sharedDirector()->getWinSize();
    
    // nuevo sprite blur
    // blur +40%
    auto newSprite = createLeaderboardBlurredSprite(texture, winSize, 0.095f);
    
    if (newSprite) {
        newSprite->setPosition(winSize / 2);
        newSprite->setZOrder(-5); // = m_bgSprite
        newSprite->setOpacity(0);
        
        // shader atmosfera
        auto shader = getOrCreateShader("paimon_atmosphere", vertexShaderCell, fragmentShaderAtmosphere);
        if (shader) {
            newSprite->setShaderProgram(shader);
            newSprite->m_intensity = 0.0f; // comenzar en 0
            newSprite->m_texSize = newSprite->getTexture()->getContentSizeInPixels();
        }
        
        this->addChild(newSprite);
        
        // transicion
        float duration = 0.5f;
        newSprite->runAction(CCFadeIn::create(duration));
        
        // anim atmosfera
        auto breathe = CCRepeatForever::create(CCSequence::create(
            CCScaleTo::create(6.0f, 1.05f),
            CCScaleTo::create(6.0f, 1.0f),
            nullptr
        ));
        newSprite->runAction(breathe);
        
        // fade sprite viejo
        if (m_bgSprite) {
            m_bgSprite->stopAllActions();
            m_bgSprite->runAction(CCSequence::create(
                CCFadeOut::create(duration),
                CCCallFunc::create(m_bgSprite, callfunc_selector(CCNode::removeFromParent)),
                nullptr
            ));
        }
        
        m_bgSprite = newSprite;
    }
    
    // fade overlay si hace falta
    if (m_bgOverlay) {
        m_bgOverlay->stopAllActions();
        m_bgOverlay->runAction(CCFadeTo::create(0.5f, 100)); // negro semi
    }
}

void LeaderboardLayer::updateBackground(int levelID) {
    log::info("[LeaderboardLayer] Updating background for levelID: {}", levelID);

    if (levelID <= 0) {
        // fade a default
        if (m_bgSprite) {
            m_bgSprite->stopAllActions();
            m_bgSprite->runAction(CCSequence::create(
                CCFadeOut::create(0.5f),
                CCCallFunc::create(m_bgSprite, callfunc_selector(CCNode::removeFromParent)),
                nullptr
            ));
            m_bgSprite = nullptr;
        }
        if (m_bgOverlay) {
            m_bgOverlay->stopAllActions();
            m_bgOverlay->runAction(CCFadeTo::create(0.5f, 0));
        }
        return;
    }

    auto texture = LocalThumbs::get().loadTexture(levelID);
    if (texture) {
        applyBackground(texture);
    } else {
        // solicita descarga
        std::string fileName = fmt::format("{}.png", levelID);
        Ref<LeaderboardLayer> self = this;
        ThumbnailLoader::get().requestLoad(levelID, fileName, [self, levelID](CCTexture2D* tex, bool) {
            if (tex) tex->retain();
            geode::Loader::get()->queueInMainThread([self, tex] {
                if (self->getParent() || self->isRunning()) {
                    if (tex) {
                        self->applyBackground(tex);
                    }
                }
                if (tex) tex->release();
            });
        });
    }
}

void LeaderboardLayer::loadLevelsFinished(CCArray* levels, const char* key) {
    if (!levels) return;

    for (int i = 0; i < levels->count(); ++i) {
        auto downloadedLevel = static_cast<GJGameLevel*>(levels->objectAtIndex(i));
        
        // actualiza destacado si coincide
        if (m_featuredLevel && m_featuredLevel->m_levelID == downloadedLevel->m_levelID) {
            m_featuredLevel->m_levelName = downloadedLevel->m_levelName;
            m_featuredLevel->m_creatorName = downloadedLevel->m_creatorName;
            m_featuredLevel->m_stars = downloadedLevel->m_stars;
            m_featuredLevel->m_difficulty = downloadedLevel->m_difficulty;
            m_featuredLevel->m_demon = downloadedLevel->m_demon;
            m_featuredLevel->m_demonDifficulty = downloadedLevel->m_demonDifficulty;
            m_featuredLevel->m_userID = downloadedLevel->m_userID;
            m_featuredLevel->m_accountID = downloadedLevel->m_accountID;
            m_featuredLevel->m_levelString = downloadedLevel->m_levelString;
            // info cancion
            m_featuredLevel->m_songID = downloadedLevel->m_songID;
            m_featuredLevel->m_audioTrack = downloadedLevel->m_audioTrack;
            m_featuredLevel->m_songIDs = downloadedLevel->m_songIDs;
            m_featuredLevel->m_sfxIDs = downloadedLevel->m_sfxIDs;
        }

        if (m_allItems) {
            // busca en m_allItems y actualiza
            for (int j = 0; j < m_allItems->count(); ++j) {
                auto item = m_allItems->objectAtIndex(j);
                // gjgamelevel? no gjuserscore
                if (auto level = typeinfo_cast<GJGameLevel*>(item)) {
                    if (level->m_levelID == downloadedLevel->m_levelID) {
                        level->m_levelName = downloadedLevel->m_levelName;
                        level->m_creatorName = downloadedLevel->m_creatorName;
                        level->m_stars = downloadedLevel->m_stars;
                        level->m_difficulty = downloadedLevel->m_difficulty;
                        level->m_demon = downloadedLevel->m_demon;
                        level->m_demonDifficulty = downloadedLevel->m_demonDifficulty;
                        level->m_userID = downloadedLevel->m_userID;
                        level->m_accountID = downloadedLevel->m_accountID;
                        // info cancion
                        level->m_songID = downloadedLevel->m_songID;
                        level->m_audioTrack = downloadedLevel->m_audioTrack;
                        level->m_songIDs = downloadedLevel->m_songIDs;
                        level->m_sfxIDs = downloadedLevel->m_sfxIDs;
                        break;
                    }
                }
            }
        }
    }
    
    refreshList();
}

void LeaderboardLayer::loadLevelsFailed(const char* key) {
    // ignorar fallos
}

void LeaderboardLayer::setupPageInfo(std::string, const char*) {
    // no necesario
}

void LeaderboardLayer::onReloadAllTime() {
    this->loadLeaderboard("alltime");
}

void LeaderboardLayer::onRecalculate(CCObject* sender) {
    WeakRef<LeaderboardLayer> self = this;
    createQuickPopup(
        "Confirm",
        "Recalculate <cy>All Time</c> Leaderboard?",
        "Cancel", "Yes",
        [self](FLAlertLayer*, bool btn2) {
            if (auto layer = self.lock()) {
                if (btn2) {
                    Notification::create("Recalculating...", NotificationIcon::Info)->show();
                    
                    HttpClient::get().post("/api/admin/recalculate-alltime", "{}", [self](bool success, const std::string& msg) {
                        if (auto l = self.lock()) {
                            if (success) {
                                Notification::create("Recalculation started", NotificationIcon::Success)->show();
                                
                                // recarga lista tras breve retraso para permitir proceso servidor
                                l->runAction(CCSequence::create(
                                    CCDelayTime::create(3.0f),
                                    CCCallFunc::create(l, callfunc_selector(LeaderboardLayer::onReloadAllTime)), 
                                    nullptr
                                ));
                                
                            } else {
                                Notification::create("Failed: " + msg, NotificationIcon::Error)->show();
                            }
                        }
                    });
                }
            }
        }
    );
}

void LeaderboardLayer::fetchGDBrowserLevel(int levelID) {
    std::string url = "https://gdbrowser.com/api/level/" + std::to_string(levelID);
    
    this->retain();
    std::thread([this, levelID, url]() {
        auto req = web::WebRequest();
        auto res = req.getSync(url);

        queueInMainThread([this, levelID, res = std::move(res)]() {
            if (res.ok()) {
                auto json = res.string().unwrapOr("{}");
                auto dataRes = matjson::parse(json);
                if (dataRes.isOk()) {
                    auto data = dataRes.unwrap();
                    if (m_featuredLevel && m_featuredLevel->m_levelID == levelID) {
                        m_featuredLevel->m_levelName = data["name"].asString().unwrapOr(m_featuredLevel->m_levelName);
                        m_featuredLevel->m_creatorName = data["author"].asString().unwrapOr(m_featuredLevel->m_creatorName);
                        m_featuredLevel->m_stars = data["stars"].asInt().unwrapOr(0);
                        m_featuredLevel->m_downloads = data["downloads"].asInt().unwrapOr(0);
                        m_featuredLevel->m_likes = data["likes"].asInt().unwrapOr(0);

                        std::string diffStr = data["difficulty"].asString().unwrapOr("NA");
                        bool isDemon = diffStr.find("Demon") != std::string::npos;
                        bool isAuto = diffStr == "Auto";

                        m_featuredLevel->m_demon = isDemon;
                        m_featuredLevel->m_autoLevel = isAuto;

                        if (isDemon) {
                            m_featuredLevel->m_difficulty = (GJDifficulty)50;
                            if (diffStr == "Easy Demon") m_featuredLevel->m_demonDifficulty = 3;
                            else if (diffStr == "Medium Demon") m_featuredLevel->m_demonDifficulty = 4;
                            else if (diffStr == "Hard Demon") m_featuredLevel->m_demonDifficulty = 5;
                            else if (diffStr == "Insane Demon") m_featuredLevel->m_demonDifficulty = 6;
                            else if (diffStr == "Extreme Demon") m_featuredLevel->m_demonDifficulty = 7;
                        } else if (isAuto) {
                            m_featuredLevel->m_difficulty = GJDifficulty::Auto;
                        } else {
                            if (diffStr == "Easy") m_featuredLevel->m_difficulty = GJDifficulty::Easy;
                            else if (diffStr == "Normal") m_featuredLevel->m_difficulty = GJDifficulty::Normal;
                            else if (diffStr == "Hard") m_featuredLevel->m_difficulty = GJDifficulty::Hard;
                            else if (diffStr == "Harder") m_featuredLevel->m_difficulty = GJDifficulty::Harder;
                            else if (diffStr == "Insane") m_featuredLevel->m_difficulty = GJDifficulty::Insane;
                            else m_featuredLevel->m_difficulty = GJDifficulty::NA;
                        }

                        this->refreshList();
                    }
                }
            }
            this->release();
        });
    }).detach();
}

LeaderboardLayer::~LeaderboardLayer() {
    if (GameLevelManager::get()->m_levelManagerDelegate == this) {
        GameLevelManager::get()->m_levelManagerDelegate = nullptr;
    }
    if (m_featuredLevel) {
        m_featuredLevel->release();
        m_featuredLevel = nullptr;
    }
    if (m_allItems) {
        m_allItems->release();
        m_allItems = nullptr;
    }
}

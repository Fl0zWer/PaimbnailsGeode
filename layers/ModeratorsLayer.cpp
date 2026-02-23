#include "ModeratorsLayer.hpp"
#include "../utils/Localization.hpp"
#include "../utils/HttpClient.hpp"
#include "../utils/Debug.hpp"
#include <Geode/binding/GJUserScore.hpp>
#include <Geode/binding/GJScoreCell.hpp>
#include <Geode/binding/CustomListView.hpp>
#include <Geode/binding/GJListLayer.hpp>
#include <Geode/binding/GameLevelManager.hpp>
#include <Geode/utils/web.hpp>
#include <Geode/utils/string.hpp>
#include <matjson.hpp>
#include <Geode/modify/GJScoreCell.hpp>
#include <thread>

using namespace geode::prelude;

ModeratorsLayer* ModeratorsLayer::s_instance = nullptr;

ModeratorsLayer* ModeratorsLayer::create() {
    auto ret = new ModeratorsLayer();
    if (ret && ret->init()) {
        ret->autorelease();
        s_instance = ret;
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

ModeratorsLayer::~ModeratorsLayer() {
    if (s_instance == this) {
        s_instance = nullptr;
    }
    if (m_scores) {
        m_scores->release();
    }
    if (GameLevelManager::get()->m_userInfoDelegate == this) {
        GameLevelManager::get()->m_userInfoDelegate = nullptr;
    }
}

bool ModeratorsLayer::isScoreInList(GJUserScore* score) {
    if (!m_scores || !score) return false;
    return m_scores->containsObject(score);
}

bool ModeratorsLayer::init() {
    if (!Popup::init(420.f, 280.f)) return false;
    
    this->setup();
    return true;
}

void ModeratorsLayer::setup() {
    // titulo por gjlistlayer
    this->fetchModerators();

    // ocultar fondo para no duplicar popup
    if (m_bgSprite) {
        m_bgSprite->setVisible(false);
    }
    
    // alternativa: iterar hijos
    auto children = this->getChildren();
    if (children) {
        for (auto* obj : CCArrayExt<CCObject*>(children)) {
            auto sprite = typeinfo_cast<CCScale9Sprite*>(obj);
            if (sprite) {
                sprite->setVisible(false);
            }
        }
    }

    if (m_closeBtn) {
        m_closeBtn->setPosition(m_closeBtn->getPosition() + ccp(13.f, 0.f));
    }
}

static void sortScoresByPriority(CCArray* scores, const std::vector<std::string>& priority) {
    if (!scores) return;
    auto toVec = std::vector<GJUserScore*>();
    toVec.reserve(scores->count());
    for (unsigned i = 0; i < scores->count(); ++i) {
        auto obj = typeinfo_cast<GJUserScore*>(scores->objectAtIndex(i));
        if (!obj) continue;
        obj->retain(); // no borrar al quitar del array
        toVec.push_back(obj);
    }

    auto toLower = [](std::string str) {
        std::transform(str.begin(), str.end(), str.begin(), ::tolower);
        return str;
    };

    auto indexOf = [&](const std::string& name) {
        std::string lowerName = toLower(name);
        for (size_t i = 0; i < priority.size(); ++i) {
            if (toLower(priority[i]) == lowerName) return static_cast<int>(i);
        }
        return static_cast<int>(priority.size());
    };

    std::stable_sort(toVec.begin(), toVec.end(), [&](GJUserScore* a, GJUserScore* b) {
        return indexOf(a->m_userName) < indexOf(b->m_userName);
    });

    scores->removeAllObjects();
    for (auto* s : toVec) {
        scores->addObject(s);
        s->release(); // libera extra
    }
}

void ModeratorsLayer::fetchModerators() {
    m_loadingCircle = LoadingCircle::create();
    m_loadingCircle->setParentLayer(this);
    m_loadingCircle->show();

    m_scores = CCArray::create();
    m_scores->retain();

    // weakref para evitar leaks/crashes
    WeakRef<ModeratorsLayer> self = this;
    HttpClient::get().getModerators([self](bool success, const std::vector<std::string>& moderators) {
        auto layer = self.lock();
        if (!layer) return;

        if (!success || moderators.empty()) {
            if (layer->m_loadingCircle) {
                layer->m_loadingCircle->fadeAndRemove();
                layer->m_loadingCircle = nullptr;
            }
            layer->createList();
            return;
        }

        layer->m_moderatorNames = moderators;
        layer->m_pendingRequests = moderators.size();
        for (const auto& mod : moderators) {
            layer->fetchGDBrowserProfile(mod);
        }
    });
}

void ModeratorsLayer::fetchGDBrowserProfile(const std::string& username) {
    std::string url = "https://gdbrowser.com/api/profile/" + username;
    
    WeakRef<ModeratorsLayer> self = this;

    // usar thread directo en lugar de taskholder
    std::thread([self, username, url]() {
        auto req = web::WebRequest();
        auto res = req.getSync(url);

        std::string data = res.ok() ? res.string().unwrapOr("") : "";
        if (!res.ok()) {
            log::error("Failed to fetch profile for {}", username);
        }
        
        queueInMainThread([self, username, data]() {
            if (auto layer = self.lock()) {
                layer->onProfileFetched(username, data);
            }
        });
    }).detach();
}

void ModeratorsLayer::onProfileFetched(const std::string& username, const std::string& jsonData) {
    if (!jsonData.empty()) {
        auto res = matjson::parse(jsonData);
        
        if (res.isOk()) {
            auto json = res.unwrap();
            
            auto parseInt = [](matjson::Value const& val) -> int {
                if (val.isNumber()) return val.asInt().unwrapOr(0);
                if (val.isString()) {
                    return geode::utils::numFromString<int>(val.asString().unwrapOr("0")).unwrapOr(0);
                }
                return 0;
            };

            try {
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
                score->m_modBadge = parseInt(json["modBadge"]); 
                
                GameLevelManager::get()->storeUserName(score->m_userID, score->m_accountID, score->m_userName);
                
                m_scores->addObject(score);
            } catch (...) {
                log::error("Failed to parse profile for {}", username);
            }
        }
    }
    
    m_pendingRequests--;
    if (m_pendingRequests <= 0) {
        this->onAllProfilesFetched();
    }
}

void ModeratorsLayer::onAllProfilesFetched() {
    if (m_loadingCircle) {
        m_loadingCircle->fadeAndRemove();
        m_loadingCircle = nullptr;
    }
    
    sortScoresByPriority(m_scores, m_moderatorNames);
    this->createList();
}

void ModeratorsLayer::createList() {
    auto winSize = CCDirector::sharedDirector()->getWinSize();
    
    if (!m_scores) {
        m_scores = CCArray::create();
        m_scores->retain();
    }

    // customlistview score
    m_listView = CustomListView::create(
        m_scores,
        BoomListType::Score, 
        220.f, 
        360.f
    );
    
    m_listLayer = GJListLayer::create(
        m_listView,
        "Paimbnails Mods",
        {0, 0, 0, 180}, 
        360.f, 
        220.f, 
        0
    );
    
    m_listLayer->setPosition(m_mainLayer->getContentSize() / 2 - m_listLayer->getScaledContentSize() / 2);
    m_mainLayer->addChild(m_listLayer);
}

void ModeratorsLayer::getUserInfoFinished(GJUserScore* score) {
    PaimonDebug::log("getUserInfoFinished: Received data for account {}", score->m_accountID);
    
    // procesa moderadores
    if (score->m_accountID == 17046382 || score->m_accountID == 23785880 || 
        score->m_accountID == 4315943 || score->m_accountID == 4098680 || 
        score->m_accountID == 25339555) {
        
        PaimonDebug::log("  m_userName: {}", score->m_userName);
        PaimonDebug::log("  m_playerCube: {}", score->m_playerCube);
        PaimonDebug::log("  m_iconType: {}", (int)score->m_iconType);
        PaimonDebug::log("  m_color1: {}, m_color2: {}", score->m_color1, score->m_color2);
        
        auto newScore = GJUserScore::create();
        newScore->m_userName = score->m_userName;
        newScore->m_userID = score->m_userID;
        newScore->m_accountID = score->m_accountID;
        
        // copiar todos los campos relacionados con iconos
        newScore->m_color1 = score->m_color1;
        newScore->m_color2 = score->m_color2;
        newScore->m_color3 = score->m_color3;
        newScore->m_special = score->m_special;
        newScore->m_iconType = score->m_iconType;
        newScore->m_playerCube = score->m_playerCube;
        newScore->m_playerShip = score->m_playerShip;
        newScore->m_playerBall = score->m_playerBall;
        newScore->m_playerUfo = score->m_playerUfo;
        newScore->m_playerWave = score->m_playerWave;
        newScore->m_playerRobot = score->m_playerRobot;
        newScore->m_playerSpider = score->m_playerSpider;
        newScore->m_playerSwing = score->m_playerSwing;
        newScore->m_playerJetpack = score->m_playerJetpack;
        newScore->m_playerStreak = score->m_playerStreak;
        newScore->m_playerExplosion = score->m_playerExplosion;
        newScore->m_glowEnabled = score->m_glowEnabled;
        
        // m_iconid por tipo icono
        switch (newScore->m_iconType) {
            case IconType::Cube: newScore->m_iconID = score->m_playerCube; break;
            case IconType::Ship: newScore->m_iconID = score->m_playerShip; break;
            case IconType::Ball: newScore->m_iconID = score->m_playerBall; break;
            case IconType::Ufo: newScore->m_iconID = score->m_playerUfo; break;
            case IconType::Wave: newScore->m_iconID = score->m_playerWave; break;
            case IconType::Robot: newScore->m_iconID = score->m_playerRobot; break;
            case IconType::Spider: newScore->m_iconID = score->m_playerSpider; break;
            case IconType::Swing: newScore->m_iconID = score->m_playerSwing; break;
            case IconType::Jetpack: newScore->m_iconID = score->m_playerJetpack; break;
            default: newScore->m_iconID = score->m_playerCube; break;
        }
        
        PaimonDebug::log("  Set m_iconID to: {}", newScore->m_iconID);
        
        // stats del server
        newScore->m_stars = score->m_stars;
        newScore->m_moons = score->m_moons;
        newScore->m_diamonds = score->m_diamonds;
        newScore->m_secretCoins = score->m_secretCoins;
        newScore->m_userCoins = score->m_userCoins;
        newScore->m_demons = score->m_demons;
        newScore->m_creatorPoints = score->m_creatorPoints;
        newScore->m_globalRank = score->m_globalRank;
        
        // insignia
        newScore->m_modBadge = 2; // elder mod

        // actualiza m_scores
        if (m_scores) {
            // quita entrada misma cuenta
            for (int i = m_scores->count() - 1; i >= 0; i--) {
                auto s = typeinfo_cast<GJUserScore*>(m_scores->objectAtIndex(i));
                if (s && s->m_accountID == score->m_accountID) {
                    m_scores->removeObjectAtIndex(i);
                }
            }
            // inserta pos correcta
            m_scores->insertObject(newScore, 0);
            // reordena
            sortScoresByPriority(m_scores, {"FlozWer", "Gabriv4", "Debihan", "SirExcelDj", "Robert55GD"});
        }

        PaimonDebug::log("Recreating list with updated data...");
        
        // reconstruye lista
        if (m_listLayer) {
            m_listLayer->removeFromParent();
            m_listLayer = nullptr;
        }

        m_listView = CustomListView::create(
            m_scores,
            BoomListType::Score, 
            220.f, 
            360.f
        );
        
        m_listLayer = GJListLayer::create(
            m_listView,
            Localization::get().getString("mods.title").c_str(),
            {0, 0, 0, 180}, 
            360.f, 
            220.f, 
            0
        );
        
        m_listLayer->setPosition(m_mainLayer->getContentSize() / 2 - m_listLayer->getScaledContentSize() / 2);
        m_mainLayer->addChild(m_listLayer);
        
        PaimonDebug::log("List recreated successfully");
    }
}

class $modify(GJScoreCell) {
    void loadFromScore(GJUserScore* score) {
        GJScoreCell::loadFromScore(score);

        if (ModeratorsLayer::s_instance && ModeratorsLayer::s_instance->isScoreInList(score)) {
            Ref<GJScoreCell> self = this;
            queueInMainThread([self]() {
                if (auto rankLabel = self->getChildByID("rank-label")) {
                    rankLabel->setVisible(false);
                }
            });
        }
    }
};

void ModeratorsLayer::getUserInfoFailed(int type) {
    PaimonDebug::warn("getUserInfoFailed: type {}", type);
}

void ModeratorsLayer::userInfoChanged(GJUserScore* score) {}

#include <Geode/modify/LevelInfoLayer.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/binding/LeaderboardsLayer.hpp>
#include "../utils/PaimonButtonHighlighter.hpp"
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/utils/cocos.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/BasedButtonSprite.hpp>
#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/GJAccountManager.hpp>
#include <fstream>
#include <vector>
#include <cmath>
#include <chrono>
#include <filesystem>
#include <algorithm>
#include <random>

#include "../managers/LocalThumbs.hpp"
#include "../managers/PendingQueue.hpp"
#include "../managers/ThumbnailAPI.hpp"
// #include "../layers/ThumbnailSelectionPopup.hpp"
#include "../managers/LevelColors.hpp"
#include "../managers/ThumbnailLoader.hpp"
#include "../managers/DynamicSongManager.hpp"
#include "../utils/AnimatedGIFSprite.hpp"
#include "../layers/RatePopup.hpp"

#include "../utils/FileDialog.hpp"
#include "../utils/Assets.hpp"
#include "../utils/Localization.hpp"
#include "../utils/ImageConverter.hpp"
#include "../utils/HttpClient.hpp"

#include <Geode/binding/FMODAudioEngine.hpp>
#include <Geode/binding/MusicDownloadManager.hpp>
#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/LevelTools.hpp>

#include "../utils/UIBorderHelper.hpp"
#include "../utils/Constants.hpp"
#include "../layers/ButtonEditOverlay.hpp"
#include "../managers/ButtonLayoutManager.hpp"
#include <Geode/binding/DailyLevelPage.hpp>
#include "../layers/SetDailyWeeklyPopup.hpp"

#include "../utils/Shaders.hpp"

using namespace geode::prelude;
using namespace cocos2d;
using namespace Shaders;



#include "../layers/ReportInputPopup.hpp"

// popup thumb zoom/pan
class LocalThumbnailViewPopup : public geode::Popup {
protected:
    int32_t m_levelID;
    bool m_canAcceptUpload = false;
    CCTexture2D* m_thumbnailTexture = nullptr;
    // CCNode* pa android (sin clipping)
    cocos2d::CCNode* m_clippingNode = nullptr;
    CCNode* m_thumbnailSprite = nullptr;
    float m_initialScale = 1.0f;
    float m_maxScale = 4.0f;
    float m_minScale = 0.5f;
    std::unordered_set<cocos2d::CCTouch*> m_touches;
    float m_initialDistance = 0.0f;
    float m_savedScale = 1.0f;
    CCPoint m_touchMidPoint = {0, 0};
    bool m_wasZooming = false;
    bool m_isExiting = false;
    int m_verificationCategory = -1; // -1 = not a verification entry, 0=Verify, 1=Update, 2=Report
    
    // area vista pa limites pan
    float m_viewWidth = 0.0f;
    float m_viewHeight = 0.0f;

    // votacion
    cocos2d::CCMenu* m_ratingMenu = nullptr;
    cocos2d::CCMenu* m_buttonMenu = nullptr;
    cocos2d::CCLabelBMFont* m_ratingLabel = nullptr;
    int m_userVote = 0;
    int m_initialUserVote = 0;
    bool m_isVoting = false;

    // galeria
    std::vector<ThumbnailAPI::ThumbnailInfo> m_thumbnails;
    bool m_isDownloading = false;

    void onPrev(CCObject*) {
        if (m_thumbnails.empty()) return;
        m_currentIndex--;
        if (m_currentIndex < 0) m_currentIndex = m_thumbnails.size() - 1;
        loadThumbnailAt(m_currentIndex);
    }

    void onNext(CCObject*) {
        if (m_thumbnails.empty()) return;
        m_currentIndex++;
        if (m_currentIndex >= m_thumbnails.size()) m_currentIndex = 0;
        loadThumbnailAt(m_currentIndex);
    }

    void onInfo(CCObject*) {
        std::string resStr = "Unknown";
        if (m_thumbnailTexture) {
             resStr = fmt::format("{} x {}", m_thumbnailTexture->getPixelsWide(), m_thumbnailTexture->getPixelsHigh());
        }

        std::string id = "Unknown";
        std::string type = "Static";
        std::string format = "Unknown";
        std::string creator = "Unknown";
        std::string date = "Unknown";

        if (m_currentIndex >= 0 && m_currentIndex < m_thumbnails.size()) {
             auto& item = m_thumbnails[m_currentIndex];
             id = item.id;
             type = item.type;
             format = item.format;
             if (!item.creator.empty()) creator = item.creator;
             if (!item.date.empty()) date = item.date;

             // fecha desde id si parece timestamp
             if (date == "Unknown" && id.length() >= 13) {
                 try {
                     long long timestamp = std::stoll(id);
                     if (timestamp > 1600000000000) { // validez
                         time_t timeSec = timestamp / 1000;
                         struct tm* t = localtime(&timeSec);
                         if (t) {
                             // dd/mm/aa hh:mm
                             date = fmt::format("{:02}/{:02}/{:02} {:02}:{:02}", 
                                t->tm_mday, t->tm_mon + 1, t->tm_year % 100,
                                t->tm_hour, t->tm_min);
                         }
                     }
                 } catch(...) {}
             }

             // yyyy-mm-dd -> dd/mm/aa
             if (date.length() >= 10 && date[4] == '-' && date[7] == '-') {
                 std::string year = date.substr(2, 2); 
                 std::string month = date.substr(5, 2);
                 std::string day = date.substr(8, 2);
                 
                 std::string timeStr = "";
                 // hora si string largo
                 if (date.length() >= 16) {
                    timeStr = " " + date.substr(11, 5);
                 }

                 date = fmt::format("{}/{}/{}{}", day, month, year, timeStr);
             }
        }

        std::string info = fmt::format(
            "<cg>ID:</c> {}\n"
            "<cy>Type:</c> {}\n"
            "<cl>Format:</c> {}\n"
            "<cp>Resolution:</c> {}\n"
            "<co>Creator:</c> {}\n"
            "<cb>Date:</c> {}",
            id, type, format, resStr, creator, date
        );
        FLAlertLayer::create("Thumbnail Info", info, "OK")->show();
    }

    void loadThumbnailAt(int index) {
        if (index < 0 || index >= m_thumbnails.size()) return;
        
        auto& thumb = m_thumbnails[index];
        std::string url = thumb.url;
        
        std::string username = "Unknown";
        if (auto gm = GameManager::get()) username = gm->m_playerName;
        
        // update rating ui
        this->retain();
        ThumbnailAPI::get().getRating(m_levelID, username, thumb.id, [this](bool success, float average, int count, int userVote) {
            if (success && m_ratingLabel) {
                m_ratingLabel->setString(fmt::format("{:.1f} ({})", average, count).c_str());
                if (count == 0) {
                    m_ratingLabel->setColor({255, 100, 100});
                } else {
                    m_ratingLabel->setColor({255, 255, 255});
                }
                m_userVote = userVote;
                m_initialUserVote = userVote;
            }
            this->release();
        });

        // download y mostrar
        this->retain();
        ThumbnailAPI::get().downloadFromUrl(url, [this](bool success, CCTexture2D* tex) {
            if (success && tex) {
                auto content = m_mainLayer->getContentSize();
                float maxWidth = content.width - 40.f;
                float maxHeight = content.height - 70.f;
                this->displayThumbnail(tex, maxWidth, maxHeight, content, false);
            }
            this->release();
        });
    }
    std::vector<Suggestion> m_suggestions;
    int m_currentIndex = 0;
    CCMenuItemSpriteExtra* m_leftArrow = nullptr;
    CCMenuItemSpriteExtra* m_rightArrow = nullptr;
    CCLabelBMFont* m_counterLabel = nullptr;
    
    // destructor, suelta recursos
    ~LocalThumbnailViewPopup() {
        log::info("[ThumbnailViewPopup] Destructor - liberando textura retenida");
        if (m_thumbnailTexture) {
            m_thumbnailTexture->release();
            m_thumbnailTexture = nullptr;
        }
        m_touches.clear();
    }
    
    public:
    void setSuggestions(const std::vector<Suggestion>& suggestions) {
        m_suggestions = suggestions;
        if (!m_suggestions.empty()) {
            m_currentIndex = 0;
            this->loadCurrentSuggestion();
        }
    }
    protected:

    void loadCurrentSuggestion() {
        if (m_suggestions.empty()) return;
        
        auto& suggestion = m_suggestions[m_currentIndex];
        log::info("[ThumbnailViewPopup] Loading suggestion {}/{} - {}", m_currentIndex + 1, m_suggestions.size(), suggestion.filename);
        
        // label contador
        if (m_counterLabel) {
            m_counterLabel->setString(fmt::format("{}/{}", m_currentIndex + 1, m_suggestions.size()).c_str());
        }
        
        // visibilidad flechas
        if (m_leftArrow) m_leftArrow->setVisible(m_suggestions.size() > 1);
        if (m_rightArrow) m_rightArrow->setVisible(m_suggestions.size() > 1);
        
        // download img
        std::string url = "https://paimon-thumbnails.paimonalcuadrado.workers.dev/" + suggestion.filename;
        
        LocalThumbnailViewPopup* popupPtr = this;
        this->retain();
        
        ThumbnailAPI::get().downloadFromUrl(url, [popupPtr](bool success, CCTexture2D* tex) {
             if (!popupPtr || !popupPtr->getParent() || !popupPtr->m_mainLayer) {
                 if (popupPtr) popupPtr->release();
                 return;
             }
             
             if (success && tex) {
                 auto content = popupPtr->m_mainLayer->getContentSize();
                 float maxWidth = content.width - 40.f;
                 float maxHeight = content.height - 70.f;
                 
                 popupPtr->displayThumbnail(tex, maxWidth, maxHeight, content, false);
             }
             popupPtr->release();
        });
    }

    void onNextSuggestion(CCObject*) {
        if (m_suggestions.empty()) return;
        m_currentIndex++;
        if (m_currentIndex >= m_suggestions.size()) {
            m_currentIndex = 0;
        }
        loadCurrentSuggestion();
    }

    void onPrevSuggestion(CCObject*) {
        if (m_suggestions.empty()) return;
        m_currentIndex--;
        if (m_currentIndex < 0) {
            m_currentIndex = m_suggestions.size() - 1;
        }
        loadCurrentSuggestion();
    }

    // ciclo vida
    void onExit() override {
        log::info("[ThumbnailViewPopup] onExit() comenzando");
        
        m_touches.clear();

        if (m_isExiting) {
            log::warn("[ThumbnailViewPopup] onExit() ya fue llamado, evitando re-entrada");
            return;
        }
        m_isExiting = true;
        
        // quitar hijos pa no crashear al destruir
        if (m_mainLayer) {
            m_mainLayer->removeAllChildren();
        }
        m_ratingMenu = nullptr;
        m_buttonMenu = nullptr;
        m_ratingLabel = nullptr;
        m_counterLabel = nullptr;
        m_leftArrow = nullptr;
        m_rightArrow = nullptr;
        
        // null ptrs, cocos limpia
        m_thumbnailSprite = nullptr;
        m_clippingNode = nullptr;
        
        // no release texture aqui, destructor
        
        log::info("[ThumbnailViewPopup] Llamando a parent onExit");
        
        // llamar al padre, el limpia la jerarquia de nodos
        Popup::onExit();
    }

    /*
    void registerWithTouchDispatcher() override {
        CCTouchDispatcher::get()->addTargetedDelegate(this, -128, true);
    }
    */
    
    void setupRating() {
        if (auto node = m_mainLayer->getChildByID("rating-container")) {
            node->removeFromParent();
        }

        auto contentSize = m_mainLayer->getContentSize();
        
        // container rating arriba
        auto ratingContainer = CCNode::create();
        ratingContainer->setID("rating-container");
        ratingContainer->setPosition({contentSize.width / 2.f, 237.f});
        m_mainLayer->addChild(ratingContainer, 100); // z-order alto
        
        // fondo rating, sheet cargada
        if (!CCSpriteFrameCache::sharedSpriteFrameCache()->spriteFrameByName("square02_001.png")) {
            CCSpriteFrameCache::sharedSpriteFrameCache()->addSpriteFramesWithFile("GJ_GameSheet03.plist");
        }
        
        auto bg = cocos2d::extension::CCScale9Sprite::createWithSpriteFrameName("square02_001.png");
        
        // fallback square02b
        if (!bg) {
             bg = cocos2d::extension::CCScale9Sprite::createWithSpriteFrameName("square02b_001.png");
        }

        if (bg) {
            bg->setContentSize({74.f, 16.f}); // reducido ~30% (105->74, 23->16)
            bg->setColor({0, 0, 0});
            bg->setOpacity(125);
            bg->setPosition({0.f, 0.f}); // centro del contenedor
            ratingContainer->addChild(bg, -1); // detras del contenido
        } else {
             // fallback drawnode o layercolor
             auto fallback = CCLayerColor::create({0, 0, 0, 125});
             fallback->setContentSize({74.f, 16.f});
             fallback->setPosition({-37.f, -8.f});
             ratingContainer->addChild(fallback, -1);
        }
        
        // icono estrella
        auto starSpr = CCSprite::createWithSpriteFrameName("GJ_starsIcon_001.png"); 
        if (!starSpr) starSpr = CCSprite::createWithSpriteFrameName("star_small01_001.png");
        starSpr->setScale(0.34f); // reducido 30% (0.48 -> 0.34)
        starSpr->setPosition({-20.f, 0.f}); // posicion ajustada
        ratingContainer->addChild(starSpr);
        
        // label promedio
        m_ratingLabel = CCLabelBMFont::create("...", "goldFont.fnt");
        m_ratingLabel->setScale(0.28f); // reducido 30% (0.4 -> 0.28)
        m_ratingLabel->setPosition({8.f, 3.f}); // posicion ajustada (3px arriba de 0.f)
        ratingContainer->addChild(m_ratingLabel);
        
        // get rating
        std::string username = "Unknown";
        if (auto gm = GameManager::get()) username = gm->m_playerName;
        
        std::string thumbnailId = "";
        if (m_currentIndex >= 0 && m_currentIndex < m_thumbnails.size()) {
            thumbnailId = m_thumbnails[m_currentIndex].id;
        }

        this->retain();
        ThumbnailAPI::get().getRating(m_levelID, username, thumbnailId, [this](bool success, float average, int count, int userVote) {
             if (success) {
                log::info("[ThumbnailViewPopup] Rating found for level {}: {:.1f} ({})", m_levelID, average, count);
                if (m_ratingLabel) {
                    m_ratingLabel->setString(fmt::format("{:.1f} ({})", average, count).c_str());
                    if (count == 0) {
                        m_ratingLabel->setColor({255, 100, 100}); // rojo si 0
                    } else {
                        m_ratingLabel->setColor({255, 255, 255});
                    }
                }
                m_userVote = userVote;
                m_initialUserVote = userVote;
            } else {
                 log::warn("[ThumbnailViewPopup] Failed to get rating for level {}", m_levelID);
            }
            this->release();
        });
    }

    void onRate(CCObject* sender) {
        std::string thumbnailId = "";
        if (m_currentIndex >= 0 && m_currentIndex < m_thumbnails.size()) {
            thumbnailId = m_thumbnails[m_currentIndex].id;
        }
        auto popup = RatePopup::create(m_levelID, thumbnailId);
        popup->m_onRateCallback = [this]() {
            this->setupRating();
        };
        popup->show();
    }

    bool init(float width, float height) { // no override, ocultando
         return Popup::init(width, height);
    }
    
    void setup(std::pair<int32_t, bool> const& data) {
        m_levelID = data.first;
        m_canAcceptUpload = data.second;
        
        this->setTitle("");

        bool openedFromReport = false;
        int verificationCategory = -1; // -1 = not a verification entry, 0=Verify, 1=Update, 2=Report
        try {
            openedFromReport = Mod::get()->getSavedValue<bool>("from-report-popup", false);
            verificationCategory = Mod::get()->getSavedValue<int>("verification-category", -1);
            if (openedFromReport) Mod::get()->setSavedValue("from-report-popup", false);
            if (verificationCategory >= 0) Mod::get()->setSavedValue("verification-category", -1);
        } catch (...) {}
        
        if (m_bgSprite) {
            m_bgSprite->setVisible(false);
        }
        
        auto content = this->m_mainLayer->getContentSize();
        
        // area thumb
        float maxWidth = content.width - 40.f;
        // altura -80
        float maxHeight = content.height - 80.f;

        if (m_closeBtn) {
             // pos btn cerrar
             float topY = (content.height / 2 + 5.f) + (maxHeight / 2);
             float leftX = (content.width - maxWidth) / 2;
             m_closeBtn->setPosition({leftX - 3.f, topY + 3.f});
             
             // btn info
             auto infoSpr = CCSprite::createWithSpriteFrameName("GJ_infoIcon_001.png");
             infoSpr->setScale(0.9f); 
             auto infoBtn = CCMenuItemSpriteExtra::create(infoSpr, this, menu_selector(LocalThumbnailViewPopup::onInfo));
             
             infoBtn->setPosition({380.000f, 246.000f});
             
             if (auto menu = m_closeBtn->getParent()) {
                 menu->addChild(infoBtn);
             }
        }
        
        log::info("[ThumbnailViewPopup] Content size: {}x{}, Max area: {}x{}", 
            content.width, content.height, maxWidth, maxHeight);
        
        // clipping pa thumb; android = nodo simple
#ifdef GEODE_IS_ANDROID
        m_clippingNode = nullptr;
        auto container = CCNode::create();
        container->setContentSize({maxWidth, maxHeight});
        container->setAnchorPoint({0.5f, 0.5f});
        container->setPosition({content.width / 2, content.height / 2 + 5.f});
        this->m_mainLayer->addChild(container, 1);
        // resto del codigo igual
        m_clippingNode = container; 
#else
        // stencil scale9
        auto stencil = CCScale9Sprite::create("square02_001.png");
        if (!stencil) {
            stencil = CCScale9Sprite::create();
        }
        stencil->setContentSize({maxWidth, maxHeight});
        // centrar stencil
        stencil->setPosition({maxWidth / 2, maxHeight / 2});
        
        auto clip = CCClippingNode::create(stencil);
        clip->setAlphaThreshold(0.1f);
        m_clippingNode = clip;

        m_clippingNode->setContentSize({maxWidth, maxHeight});
        m_clippingNode->setAnchorPoint({0.5f, 0.5f});
        m_clippingNode->setPosition({content.width / 2, content.height / 2 + 5.f});
        this->m_mainLayer->addChild(m_clippingNode, 1);
#endif
        
        // fondo negro 10% pa pan
        auto clippingBg = CCLayerColor::create({0, 0, 0, 255}); // negro total temporal pa probar visibilidad, ajustar opacidad luego si hace falta
        clippingBg->setOpacity(25); // 10% opacity (approx 25/255)
        clippingBg->setContentSize({maxWidth, maxHeight});
        // centrar layercolor
        clippingBg->ignoreAnchorPointForPosition(false);
        clippingBg->setAnchorPoint({0.5f, 0.5f});
        clippingBg->setPosition({maxWidth / 2, maxHeight / 2});
        
        if (m_clippingNode) {
            m_clippingNode->addChild(clippingBg, -1);
        }

        // borde area thumb
        auto border = CCScale9Sprite::create("GJ_square07.png");
        border->setContentSize({maxWidth + 4.f, maxHeight + 4.f});
        border->setPosition({content.width / 2, content.height / 2 + 5.f});
        this->m_mainLayer->addChild(border, 2);

    // flechas galeria
        auto menu = CCMenu::create();
        menu->setPosition({0, 0});
        this->m_mainLayer->addChild(menu, 10);

        auto prevSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png");
        m_leftArrow = CCMenuItemSpriteExtra::create(prevSpr, this, menu_selector(LocalThumbnailViewPopup::onPrev));
        m_leftArrow->setPosition({25.f, content.height / 2 + 5.f});
        m_leftArrow->setVisible(false);
        menu->addChild(m_leftArrow);

        auto nextSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png");
        nextSpr->setFlipX(true);
        m_rightArrow = CCMenuItemSpriteExtra::create(nextSpr, this, menu_selector(LocalThumbnailViewPopup::onNext));
        m_rightArrow->setPosition({content.width - 25.f, content.height / 2 + 5.f});
        m_rightArrow->setVisible(false);
        menu->addChild(m_rightArrow);


        // touch zoom/pan
        this->setTouchEnabled(true);
        
#ifndef GEODE_IS_ANDROID
        // scroll wheel zoom
        this->setMouseEnabled(true);
        this->setKeypadEnabled(true);
#endif
        
        // load thumb varias fuentes
        log::info("[ThumbnailViewPopup] === INICIANDO CARGA DE THUMBNAIL ===");
        log::info("[ThumbnailViewPopup] Level ID: {}", m_levelID);
        log::info("[ThumbnailViewPopup] Verification Category: {}", verificationCategory);
        this->retain();
        
        // guardar si viene verificacion
        m_verificationCategory = verificationCategory;
        
        // cola -> load desde esa categoria
        if (verificationCategory >= 0) {
            this->loadFromVerificationQueue(static_cast<PendingCategory>(verificationCategory), maxWidth, maxHeight, content, openedFromReport);
        } else {
            // probar cache/local primero
            this->tryLoadFromMultipleSources(maxWidth, maxHeight, content, openedFromReport);

            // pedir info thumb al server
            this->retain();
            ThumbnailAPI::get().getThumbnailInfo(m_levelID, [this](bool success, const std::string& response) {
                if (success) {
                    std::vector<ThumbnailAPI::ThumbnailInfo> thumbs;
                    try {
                        auto res = matjson::parse(response);
                        if (res.isOk()) {
                            auto json = res.unwrap();
                            // endpoint devuelve 1 objeto
                            ThumbnailAPI::ThumbnailInfo info;
                            bool found = false;
                            
                            if (json.contains("url")) {
                                info.url = json["url"].asString().unwrapOr("");
                                found = true;
                            } else {
                                // fallback url
                                info.url = ThumbnailAPI::get().getThumbnailURL(m_levelID);
                            }

                            // version, formato
                            if (json.contains("version") && json["version"].isObject()) {
                                auto verObj = json["version"];
                                if (verObj.contains("version")) info.id = verObj["version"].asString().unwrapOr("");
                                if (verObj.contains("format")) info.format = verObj["format"].asString().unwrapOr("png");
                            }

                            // quien subio, cuando
                            if (json.contains("metadata") && json["metadata"].isObject()) {
                                auto metaObj = json["metadata"];
                                if (metaObj.contains("uploadedBy")) info.creator = metaObj["uploadedBy"].asString().unwrapOr("Unknown");
                                if (metaObj.contains("uploadedAt")) info.date = metaObj["uploadedAt"].asString().unwrapOr("Unknown");
                            }

                            // gif o img
                            if (json.contains("fileInfo") && json["fileInfo"].isObject()) {
                                auto fileObj = json["fileInfo"];
                                if (fileObj.contains("contentType")) {
                                    std::string ct = fileObj["contentType"].asString().unwrapOr("");
                                    if (ct.find("gif") != std::string::npos) info.type = "gif";
                                    else info.type = "static";
                                } else {
                                    info.type = "static";
                                }
                            } else {
                                info.type = "static";
                            }
                            
                            if (found || !info.id.empty()) {
                                thumbs.push_back(info);
                            }
                        }
                    } catch(...) {}

                    if (!thumbs.empty()) {
                        m_thumbnails = thumbs;
                        if (m_thumbnails.size() > 1) {
                            if (m_leftArrow) m_leftArrow->setVisible(true);
                            if (m_rightArrow) m_rightArrow->setVisible(true);
                        }
                        // update rating
                        this->setupRating();
                    }
                }
                this->release();
            });
        }
        
        setupRating();
        // return true; // setup is void
    }
    
    void loadFromVerificationQueue(PendingCategory category, float maxWidth, float maxHeight, CCSize content, bool openedFromReport) {
        log::info("[ThumbnailViewPopup] Cargando desde cola de verificación - Categoría: {}", static_cast<int>(category));
        
        LocalThumbnailViewPopup* popupPtr = this;
        
        // bajar segun categoria
        if (category == PendingCategory::Verify) {
            // verify: /suggestions
            ThumbnailAPI::get().downloadSuggestion(m_levelID, [popupPtr, maxWidth, maxHeight, content, openedFromReport](bool success, CCTexture2D* tex) {
                if (!popupPtr || !popupPtr->getParent() || !popupPtr->m_mainLayer) {
                    log::warn("[ThumbnailViewPopup] Popup destruido antes de cargar suggestion");
                    return;
                }
                
                if (success && tex) {
                    log::info("[ThumbnailViewPopup] ✓ Suggestion cargada");
                    popupPtr->displayThumbnail(tex, maxWidth, maxHeight, content, openedFromReport);
                } else {
                    log::warn("[ThumbnailViewPopup] ✗ No se pudo cargar suggestion");
                    popupPtr->showNoThumbnail(content);
                }
                popupPtr->release();
            });
        } else if (category == PendingCategory::Update) {
            // update: /updates
            ThumbnailAPI::get().downloadUpdate(m_levelID, [popupPtr, maxWidth, maxHeight, content, openedFromReport](bool success, CCTexture2D* tex) {
                if (!popupPtr || !popupPtr->getParent() || !popupPtr->m_mainLayer) {
                    log::warn("[ThumbnailViewPopup] Popup destruido antes de cargar update");
                    return;
                }
                
                if (success && tex) {
                    log::info("[ThumbnailViewPopup] ✓ Update cargada");
                    popupPtr->displayThumbnail(tex, maxWidth, maxHeight, content, openedFromReport);
                } else {
                    log::warn("[ThumbnailViewPopup] ✗ No se pudo cargar update");
                    popupPtr->showNoThumbnail(content);
                }
                popupPtr->release();
            });
        } else if (category == PendingCategory::Report) {
            // report: thumb reportada
            ThumbnailAPI::get().downloadReported(m_levelID, [popupPtr, maxWidth, maxHeight, content, openedFromReport](bool success, CCTexture2D* tex) {
                if (!popupPtr || !popupPtr->getParent() || !popupPtr->m_mainLayer) {
                    log::warn("[ThumbnailViewPopup] Popup destruido antes de cargar reported");
                    return;
                }
                
                if (success && tex) {
                    log::info("[ThumbnailViewPopup] ✓ Reported cargada");
                    popupPtr->displayThumbnail(tex, maxWidth, maxHeight, content, openedFromReport);
                } else {
                    log::warn("[ThumbnailViewPopup] ✗ No se pudo cargar reported");
                    popupPtr->showNoThumbnail(content);
                }
                popupPtr->release();
            });
        } else {
            log::error("[ThumbnailViewPopup] Categoría de verificación desconocida: {}", static_cast<int>(category));
            this->showNoThumbnail(content);
            this->release();
        }
    }
    
    void tryLoadFromMultipleSources(float maxWidth, float maxHeight, CCSize content, bool openedFromReport) {
        // 1) local primero
        if (LocalThumbs::get().has(m_levelID)) {
            log::info("[ThumbnailViewPopup] ✓ Fuente 1: LocalThumbs ENCONTRADO");
            auto tex = LocalThumbs::get().loadTexture(m_levelID);
            if (tex) {
                log::info("[ThumbnailViewPopup] ✓ Textura cargada desde LocalThumbs");
                this->displayThumbnail(tex, maxWidth, maxHeight, content, openedFromReport);
                this->release();
                return;
            }
            log::warn("[ThumbnailViewPopup] ✗ LocalThumbs falló al cargar textura");
        } else {
            log::info("[ThumbnailViewPopup] ✗ Fuente 1: LocalThumbs - NO disponible");
        }
        
        // 2) cache mod
        if (tryLoadFromCache(maxWidth, maxHeight, content, openedFromReport)) {
            return;
        }
        
        // 3) loader
        loadFromThumbnailLoader(maxWidth, maxHeight, content, openedFromReport);
    }

    bool tryLoadFromCache(float maxWidth, float maxHeight, CCSize content, bool openedFromReport) {
        log::info("[ThumbnailViewPopup] Intentando Fuente 2: Cache directo");
        auto cachePath = geode::Mod::get()->getSaveDir() / "thumbnails" / fmt::format("{}.webp", m_levelID);
        if (std::filesystem::exists(cachePath)) {
            log::info("[ThumbnailViewPopup] ✓ Encontrado en cache: {}", cachePath.generic_string());
            auto tex = CCTextureCache::sharedTextureCache()->addImage(cachePath.generic_string().c_str(), false);
            if (tex) {
                log::info("[ThumbnailViewPopup] ✓ Textura cargada desde cache ({}x{})", 
                    tex->getPixelsWide(), tex->getPixelsHigh());
                this->displayThumbnail(tex, maxWidth, maxHeight, content, openedFromReport);
                this->release();
                return true;
            }
            log::warn("[ThumbnailViewPopup] ✗ Error al cargar textura desde cache");
        } else {
            log::info("[ThumbnailViewPopup] ✗ No existe en cache: {}", cachePath.generic_string());
        }
        return false;
    }

    void loadFromThumbnailLoader(float maxWidth, float maxHeight, CCSize content, bool openedFromReport) {
        log::info("[ThumbnailViewPopup] Intentando Fuente 3: ThumbnailLoader + Descarga");
        std::string fileName = fmt::format("{}.png", m_levelID);
        
        // guardar ptr callback
        LocalThumbnailViewPopup* popupPtr = this;
        this->retain(); // retain pa callback
        
        ThumbnailLoader::get().requestLoad(m_levelID, fileName, [popupPtr, maxWidth, maxHeight, content, openedFromReport](CCTexture2D* tex, bool) {
            log::info("[ThumbnailViewPopup] === CALLBACK THUMBNAILLOADER ===");
            
            // popup vivo?
            try {
                if (!popupPtr) {
                    log::warn("[ThumbnailViewPopup] popupPtr es null");
                    return;
                }
                
                // tiene parent?
                auto parent = popupPtr->getParent();
                if (!parent || !popupPtr->m_mainLayer) {
                    log::warn("[ThumbnailViewPopup] Popup ya no tiene parent o mainLayer válido - objeto destruido");
                    // release y salir
                    popupPtr->release();
                    return;
                }
                
                // usar textura
                if (tex) {
                    log::info("[ThumbnailViewPopup] ✓ Textura recibida ({}x{})", 
                        tex->getPixelsWide(), tex->getPixelsHigh());
                    popupPtr->displayThumbnail(tex, maxWidth, maxHeight, content, openedFromReport);
                } else {
                    log::warn("[ThumbnailViewPopup] ✗ ThumbnailLoader no devolvió textura");
                    log::info("[ThumbnailViewPopup] === TODAS LAS FUENTES FALLARON ===");
                    popupPtr->showNoThumbnail(content);
                }
                
                // release
                popupPtr->release();
            } catch (const std::exception& e) {
                log::error("[ThumbnailViewPopup] Exception en callback: {}", e.what());
                // release si seguro
                if (popupPtr) popupPtr->release();
            } catch (...) {
                log::error("[ThumbnailViewPopup] Exception desconocida en callback");
                if (popupPtr) popupPtr->release();
            }
        }, 10); // prioridad alta
    }
    
    void tryDirectServerDownload(float maxWidth, float maxHeight, CCSize content, bool openedFromReport) {
        log::info("[ThumbnailViewPopup] Intentando Fuente 3: Descarga directa del servidor");
        this->retain();
        
        // ptr pa callback
        LocalThumbnailViewPopup* popupPtr = this;
        
        HttpClient::get().downloadThumbnail(m_levelID, [popupPtr, maxWidth, maxHeight, content, openedFromReport](bool success, const std::vector<uint8_t>& data, int w, int h) {
            // popup vivo?
            try {
                if (!popupPtr) {
                    log::warn("[ThumbnailViewPopup] popupPtr es null (descarga servidor)");
                    return;
                }
                
                auto parent = popupPtr->getParent();
                if (!parent || !popupPtr->m_mainLayer) {
                    log::warn("[ThumbnailViewPopup] Popup ya no tiene parent válido (descarga servidor) - no hacer release");
                    // no release si no existe
                    return;
                }
            } catch (...) {
                log::error("[ThumbnailViewPopup] Exception al validar popup (descarga servidor)");
                return;
            }
            
            if (success && !data.empty()) {
                log::info("[ThumbnailViewPopup] ✓ Datos descargados del servidor ({} bytes)", data.size());
                
                // bytes -> textura
                auto image = new CCImage();
                if (image->initWithImageData(const_cast<uint8_t*>(data.data()), data.size())) {
                    auto tex = new CCTexture2D();
                    if (tex->initWithImage(image)) {
                        log::info("[ThumbnailViewPopup] ✓ Textura creada desde servidor ({}x{})",
                            tex->getPixelsWide(), tex->getPixelsHigh());
                        // displayThumbnail retiene
                        popupPtr->displayThumbnail(tex, maxWidth, maxHeight, content, openedFromReport);
                        // limpiar refs
                        tex->release();
                        image->release();
                        popupPtr->release();
                        return;
                    }
                    tex->release();
                }
                image->release();
                log::error("[ThumbnailViewPopup] ✗ Error creando textura desde datos del servidor");
            } else {
                log::warn("[ThumbnailViewPopup] ✗ Descarga del servidor falló");
            }
            
            // fallback mensaje
            log::info("[ThumbnailViewPopup] === TODAS LAS FUENTES FALLARON ===");
            popupPtr->showNoThumbnail(content);
            popupPtr->release();
        });
    }
    
    void displayThumbnail(CCTexture2D* tex, float maxWidth, float maxHeight, CCSize content, bool openedFromReport) {
        log::info("[ThumbnailViewPopup] === MOSTRANDO THUMBNAIL ===");
        log::info("[ThumbnailViewPopup] Textura: {}x{}", tex->getPixelsWide(), tex->getPixelsHigh());
        
        // check critico
        if (!this->getParent() || !m_mainLayer) {
            log::error("[ThumbnailViewPopup] Popup destruido antes de displayThumbnail!");
            return;
        }

        // quitar sprite viejo
        if (m_thumbnailSprite) {
            m_thumbnailSprite->removeFromParent();
            m_thumbnailSprite = nullptr;
        }
        
        if (!m_mainLayer) {
            log::error("[ThumbnailViewPopup] m_mainLayer es null!");
            return;
        }

        // limpiar estado
        if (m_thumbnailTexture) {
            m_thumbnailTexture->release();
            m_thumbnailTexture = nullptr;
        }
        if (m_thumbnailSprite) {
            m_thumbnailSprite->removeFromParent();
            m_thumbnailSprite = nullptr;
        }
        if (m_buttonMenu) {
            m_buttonMenu->removeFromParent();
            m_buttonMenu = nullptr;
        }
        if (m_leftArrow) {
            m_leftArrow->removeFromParent();
            m_leftArrow = nullptr;
        }
        if (m_rightArrow) {
            m_rightArrow->removeFromParent();
            m_rightArrow = nullptr;
        }
        if (m_counterLabel) {
            m_counterLabel->removeFromParent();
            m_counterLabel = nullptr;
        }
        
        m_thumbnailTexture = tex;
        tex->retain(); // retain pa no liberar
        
        CCSprite* sprite = nullptr;
        
        // textura estatica default
        sprite = CCSprite::createWithTexture(tex);
        
        // es gif?
        if (ThumbnailLoader::get().hasGIFData(m_levelID)) {
             auto path = ThumbnailLoader::get().getCachePath(m_levelID);
             this->retain();
             AnimatedGIFSprite::createAsync(path.generic_string(), [this, maxWidth, maxHeight](AnimatedGIFSprite* anim) {
                 if (anim && this->m_thumbnailSprite) {
                     auto oldSprite = this->m_thumbnailSprite;
                     auto parent = oldSprite->getParent();
                     if (parent) {
                         CCPoint pos = oldSprite->getPosition();
                         oldSprite->removeFromParent();
                         
                         anim->setAnchorPoint({0.5f, 0.5f});
                         float scaleX = maxWidth / anim->getContentWidth();
                         float scaleY = maxHeight / anim->getContentHeight();
                         float scale = std::max(scaleX, scaleY); // cover mode
                         anim->setScale(scale);
                         anim->setPosition(pos);
                         
                         parent->addChild(anim, 10);
                         this->m_thumbnailSprite = anim;
                     }
                 }
                 this->release();
             });
        }
        
        if (!sprite) {
            log::error("[ThumbnailViewPopup] No se pudo crear sprite con textura");
            return;
        }
        
        log::info("[ThumbnailViewPopup] Sprite creado correctamente");
        sprite->setAnchorPoint({0.5f, 0.5f});
        
        // guardar dims area pa limites pan
        m_viewWidth = maxWidth;
        m_viewHeight = maxHeight;

        // scale inicial cover (llenar area sin bordes vacios)
        float scaleX = maxWidth / sprite->getContentWidth();
        float scaleY = maxHeight / sprite->getContentHeight();
        float scale = std::max(scaleX, scaleY); // cover: llena todo el area, clipping recorta exceso

        sprite->setScale(scale);
        m_initialScale = scale;
        m_minScale = scale; // no zoom out mas del fit
        m_maxScale = std::max(4.0f, scale * 6.0f); // zoom max

        // centrar
        float centerX = content.width * 0.5f;
        float centerY = content.height * 0.5f + 5.f;
        sprite->setPosition({centerX, centerY});
        sprite->setID("thumbnail"_spr);

        // calidad textura
        ccTexParams params{GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE};
        tex->setTexParameters(&params);
        
        // add a clipping
        if (m_clippingNode) {
            m_clippingNode->addChild(sprite, 10);
            sprite->setPosition({maxWidth / 2, maxHeight / 2});
        } else {
            // fallback mainlayer
            this->m_mainLayer->addChild(sprite, 10);
            sprite->setPosition({centerX, centerY});
        }
        m_thumbnailSprite = sprite;
        
        // refresh
        sprite->setVisible(true);
        sprite->setOpacity(255);
        
        log::info("[ThumbnailViewPopup] ✓ Thumbnail agregado a mainLayer");
        log::info("[ThumbnailViewPopup] Posición: ({},{}), Scale: {}, Tamaño final: {}x{}", 
            centerX, centerY, scale, sprite->getContentWidth() * scale, sprite->getContentHeight() * scale);
        log::info("[ThumbnailViewPopup] Parent: {}, Visible: {}, Opacity: {}, Z-Order: {}",
            (void*)sprite->getParent(), sprite->isVisible(), sprite->getOpacity(), sprite->getZOrder());

        // flechas + contador
        if (!m_suggestions.empty()) {
            auto menu = CCMenu::create();
            menu->setPosition({0, 0});
            this->m_mainLayer->addChild(menu, 20);

            auto leftSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png");
            m_leftArrow = CCMenuItemSpriteExtra::create(leftSpr, this, menu_selector(LocalThumbnailViewPopup::onPrevSuggestion));
            m_leftArrow->setPosition({centerX - maxWidth/2 - 20.f, centerY});
            menu->addChild(m_leftArrow);

            auto rightSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png");
            rightSpr->setFlipX(true);
            m_rightArrow = CCMenuItemSpriteExtra::create(rightSpr, this, menu_selector(LocalThumbnailViewPopup::onNextSuggestion));
            m_rightArrow->setPosition({centerX + maxWidth/2 + 20.f, centerY});
            menu->addChild(m_rightArrow);

            m_counterLabel = CCLabelBMFont::create(fmt::format("{}/{}", m_currentIndex + 1, m_suggestions.size()).c_str(), "bigFont.fnt");
            m_counterLabel->setScale(0.5f);
            m_counterLabel->setPosition({centerX, centerY - maxHeight/2 - 15.f});
            this->m_mainLayer->addChild(m_counterLabel, 20);

            // visibilidad por conteo
            m_leftArrow->setVisible(m_suggestions.size() > 1);
            m_rightArrow->setVisible(m_suggestions.size() > 1);
        }
        
        // menu botones
        m_buttonMenu = CCMenu::create();
        auto buttonMenu = m_buttonMenu;
        
        // btn descarga
        auto downloadSprite = Assets::loadButtonSprite(
            "popup-download",
            "frame:GJ_downloadBtn_001.png",
            [](){
                if (auto spr = CCSprite::createWithSpriteFrameName("GJ_downloadBtn_001.png")) return spr;
                if (auto spr = CCSprite::createWithSpriteFrameName("GJ_arrow_03_001.png")) return spr;
                return CCSprite::createWithSpriteFrameName("GJ_button_01.png");
            }
        );
        downloadSprite->setScale(0.7f);
        auto downloadBtn = CCMenuItemSpriteExtra::create(downloadSprite, this, menu_selector(LocalThumbnailViewPopup::onDownloadBtn));
        // downloadBtn->setPosition({content.width - 65.f, 20.f});
        // buttonMenu->addChild(downloadBtn);

        // btn central: aceptar/eliminar/reportar
        CCMenuItemSpriteExtra* centerBtn = nullptr;
        
        // cola verify/update -> btn aceptar
        if (m_verificationCategory >= 0 && m_verificationCategory != 2) { // 0=Verify, 1=Update, 2=Report
            auto acceptSpr = ButtonSprite::create(Localization::get().getString("level.accept_button").c_str(), 80, true, "bigFont.fnt", "GJ_button_01.png", 30.f, 0.5f);
            acceptSpr->setScale(0.6f);
            centerBtn = CCMenuItemSpriteExtra::create(acceptSpr, this, menu_selector(LocalThumbnailViewPopup::onAcceptThumbBtn));
        } else if (openedFromReport) {
            auto delSpr = ButtonSprite::create(Localization::get().getString("level.delete_button").c_str(), 90, true, "bigFont.fnt", "GJ_button_06.png", 30.f, 0.5f);
            delSpr->setScale(0.6f);
            centerBtn = CCMenuItemSpriteExtra::create(delSpr, this, menu_selector(LocalThumbnailViewPopup::onDeleteReportedThumb));
        } else {
            auto reportSpr = ButtonSprite::create(Localization::get().getString("level.report_button").c_str(), 80, true, "bigFont.fnt", "GJ_button_06.png", 30.f, 0.5f);
            reportSpr->setScale(0.6f);
            centerBtn = CCMenuItemSpriteExtra::create(reportSpr, this, menu_selector(LocalThumbnailViewPopup::onReportBtn));
        }
        // centerBtn->setPosition({content.width / 2, 20.f});
        // buttonMenu->addChild(centerBtn);
        
        // btn aceptar extra
        CCMenuItemSpriteExtra* acceptBtn = nullptr;
        if (m_canAcceptUpload && m_verificationCategory < 0) {
            auto acceptSpr = ButtonSprite::create(Localization::get().getString("level.accept_button").c_str(), 80, true, "bigFont.fnt", "GJ_button_01.png", 30.f, 0.5f);
            acceptSpr->setScale(0.6f);
            acceptBtn = CCMenuItemSpriteExtra::create(acceptSpr, this, menu_selector(LocalThumbnailViewPopup::onAcceptThumbBtn));
            // acceptBtn->setPosition({content.width / 2 - 60.f, 20.f});
            // buttonMenu->addChild(acceptBtn);
            
            // central a la der
            // centerBtn->setPosition({content.width / 2 + 60.f, 20.f});
        }

        // add botones
        if (acceptBtn) buttonMenu->addChild(acceptBtn);
        if (centerBtn) buttonMenu->addChild(centerBtn);
        
        // btn rate
        auto rateSpr = CCSprite::createWithSpriteFrameName("GJ_starBtn_001.png");
        rateSpr->setScale(0.7f);
        auto rateBtn = CCMenuItemSpriteExtra::create(rateSpr, this, menu_selector(LocalThumbnailViewPopup::onRate));
        buttonMenu->addChild(rateBtn);
        
        buttonMenu->addChild(downloadBtn);

        // btn eliminar (mods)
        auto gm = GameManager::get();
        if (gm) {
            auto username = gm->m_playerName;
            auto accountID = gm->m_playerUserID;
            
            this->retain();
            ThumbnailAPI::get().checkModeratorAccount(username, accountID, [this](bool isMod, bool isAdmin) {
                if (isMod || isAdmin) {
                    auto spr = CCSprite::createWithSpriteFrameName("GJ_deleteBtn_001.png");
                    if (!spr) spr = CCSprite::createWithSpriteFrameName("GJ_trashBtn_001.png");
                    
                    if (spr) {
                        spr->setScale(0.6f);
                        auto btn = CCMenuItemSpriteExtra::create(
                            spr,
                            this,
                            menu_selector(LocalThumbnailViewPopup::onDeleteThumbnail)
                        );
                        
                        if (m_buttonMenu) {
                            m_buttonMenu->addChild(btn);
                            m_buttonMenu->updateLayout();
                        }
                    }
                }
                this->release();
            });
        }

        // layout fila
        buttonMenu->ignoreAnchorPointForPosition(false);
        buttonMenu->setAnchorPoint({0.5f, 0.5f});
        buttonMenu->setContentSize({content.width - 40.f, 60.f});
        buttonMenu->setPosition({content.width / 2, 46.f});

        auto layout = RowLayout::create();
        layout->setGap(15.f);
        layout->setAxisAlignment(AxisAlignment::Center);
        layout->setCrossAxisAlignment(AxisAlignment::Center);
        
        buttonMenu->setLayout(layout);
        buttonMenu->updateLayout();
        
        this->m_mainLayer->addChild(buttonMenu, 10);
    }
    
    void showNoThumbnail(CCSize content) {
        float centerX = content.width * 0.5f;
        float centerY = content.height * 0.5f + 10.f;
        float bgWidth = content.width - 60.f;
        float bgHeight = content.height - 80.f;
        
        // fondo negro
        auto bg = CCLayerColor::create({0, 0, 0, 200});
        bg->setContentSize({bgWidth, bgHeight});
        bg->setPosition({centerX - bgWidth / 2, centerY - bgHeight / 2});
        this->m_mainLayer->addChild(bg);
        
        // borde uiborderhelper
        UIBorderHelper::createBorder(centerX, centerY, bgWidth, bgHeight, this->m_mainLayer);
        
        // texto sad
        auto sadLabel = CCLabelBMFont::create(":(", "bigFont.fnt");
        sadLabel->setScale(3.0f);
        sadLabel->setOpacity(100);
        sadLabel->setPosition({centerX, centerY + 20.f});
        this->m_mainLayer->addChild(sadLabel, 2);
        
        // no thumbnail
        auto noThumbLabel = CCLabelBMFont::create(Localization::get().getString("level.no_thumbnail_text").c_str(), "goldFont.fnt");
        noThumbLabel->setScale(0.6f);
        noThumbLabel->setOpacity(150);
        noThumbLabel->setPosition({centerX, centerY - 20.f});
        this->m_mainLayer->addChild(noThumbLabel, 2);
    }
    
    void onDownloadBtn(CCObject*) {
        if (m_isDownloading) return;
        m_isDownloading = true;
        try {
            std::string savePath;

#if defined(GEODE_IS_ANDROID) || defined(GEODE_IS_IOS)
            auto saveDir = Mod::get()->getSaveDir() / "saved_thumbnails";
            if (!std::filesystem::exists(saveDir)) {
                std::filesystem::create_directories(saveDir);
            }
            savePath = (saveDir / fmt::format("thumb_{}.png", m_levelID)).string();
            Notification::create(Localization::get().getString("level.saving_mod_folder").c_str(), NotificationIcon::Info)->show();
#else
            auto pathOpt = pt::saveImageFileDialog(L"miniatura.png");
            if (!pathOpt) {
                log::info("User cancelled save dialog");
                m_isDownloading = false;
                return;
            }
            savePath = pathOpt->string();
#endif
            log::debug("Save path chosen: {}", savePath);

            // rgb localthumbs o cache de ThumbnailLoader (PNG/GIF)
            auto pathStr = LocalThumbs::get().getThumbPath(m_levelID);
            bool fromCache = false;
            if (!pathStr) {
                auto cachePath = ThumbnailLoader::get().getCachePath(m_levelID, ThumbnailLoader::get().hasGIFData(m_levelID));
                if (std::filesystem::exists(cachePath)) {
                    pathStr = cachePath.string();
                    fromCache = true;
                }
            }
            if (!pathStr) {
                log::error("Thumbnail path not found");
                Notification::create(Localization::get().getString("level.no_thumbnail").c_str(), NotificationIcon::Error)->show();
                m_isDownloading = false;
                return;
            }

            auto srcPath = *pathStr;
            this->retain();
            std::thread([this, srcPath, savePath, fromCache]() {
                try {
                    bool ok = false;
                    if (fromCache) {
                        std::error_code ec;
                        std::filesystem::copy(srcPath, savePath, std::filesystem::copy_options::overwrite_existing, ec);
                        ok = !ec;
                    } else {
                        std::vector<uint8_t> rgbData;
                        uint32_t width, height;
                        if (ImageConverter::loadRgbFile(srcPath, rgbData, width, height)) {
                            auto rgba = ImageConverter::rgbToRgba(rgbData, width, height);
                            auto img = new CCImage();
                            if (img->initWithImageData(rgba.data(), rgba.size(), CCImage::kFmtRawData, width, height)) {
                                ok = img->saveToFile(savePath.c_str(), false);
                            }
                            img->release();
                        }
                    }
                    Loader::get()->queueInMainThread([this, ok, savePath]() {
                        m_isDownloading = false;
                        if (ok) {
                            log::info("Image saved successfully to {}", savePath);
                            Notification::create(Localization::get().getString("level.saved").c_str(), NotificationIcon::Success)->show();
                        } else {
                            log::error("Failed to save image to {}", savePath);
                            Notification::create(Localization::get().getString("level.save_error").c_str(), NotificationIcon::Error)->show();
                        }
                        this->release();
                    });
                } catch (...) {
                    Loader::get()->queueInMainThread([this]() {
                        m_isDownloading = false;
                        log::error("Unknown error in save thread");
                        Notification::create(Localization::get().getString("level.save_error").c_str(), NotificationIcon::Error)->show();
                        this->release();
                    });
                }
            }).detach();

        } catch (std::exception& e) {
            m_isDownloading = false;
            log::error("Exception in onDownloadBtn: {}", e.what());
            Notification::create(Localization::get().getString("level.error_prefix") + std::string(e.what()), NotificationIcon::Error)->show();
        }
    }

    void onDeleteReportedThumb(CCObject*) {
        log::info("[ThumbnailViewPopup] Borrar miniatura reportada para levelID={}", m_levelID);
        
        // guardar levelID antes de trabajo async
        int levelID = m_levelID;
        
        // obtener nombre de usuario
        std::string username;
        int accountID = 0;
        try {
            auto* gm = GameManager::get();
            auto* am = GJAccountManager::get();
            if (gm) {
                username = gm->m_playerName;
                accountID = am ? am->m_accountID : 0;
            } else {
                log::warn("[ThumbnailViewPopup] GameManager::get() es null");
                username = "Unknown";
            }
        } catch(...) {
            log::error("[ThumbnailViewPopup] Excepción al acceder a GameManager");
            username = "Unknown";
        }
        
        // crear circulo de carga
        auto loading = LoadingCircle::create();
        loading->setParentLayer(this);
        loading->setFade(true);
        loading->show();
        loading->retain();

        // check mod antes borrar
        ThumbnailAPI::get().checkModerator(username, [levelID, username, accountID, loading](bool isMod, bool isAdmin) {
            if (!isMod && !isAdmin) {
                loading->fadeAndRemove();
                loading->release();
                Notification::create(Localization::get().getString("level.delete_moderator_only").c_str(), NotificationIcon::Error)->show();
                return;
            }
            
            // borrar server + update local
            // Notification::create(Localization::get().getString("level.deleting_server").c_str(), NotificationIcon::Info)->show();
            ThumbnailAPI::get().deleteThumbnail(levelID, username, accountID, [levelID, loading](bool success, const std::string& msg) {
                loading->fadeAndRemove();
                loading->release();

                if (success) {
                    // quitar de cola reportes
                    PendingQueue::get().accept(levelID, PendingCategory::Report);
                    Notification::create(Localization::get().getString("level.deleted_server").c_str(), NotificationIcon::Success)->show();
                    log::info("[ThumbnailViewPopup] Miniatura {} eliminada del servidor", levelID);
                } else {
                    Notification::create(Localization::get().getString("level.delete_error") + msg, NotificationIcon::Error)->show();
                    log::error("[ThumbnailViewPopup] Error al borrar miniatura: {}", msg);
                }
            });
        });
    }
    
    void onAcceptThumbBtn(CCObject*) {
        log::info("Aceptar thumbnail presionado en ThumbnailViewPopup para levelID={}", m_levelID);
        
        // cola verify -> aceptar en server
        if (m_verificationCategory >= 0) {
            log::info("Aceptando thumbnail desde cola de verificación (categoría: {})", m_verificationCategory);
            
            std::string username;
            try {
                auto* gm = GameManager::get();
                if (gm) {
                    username = gm->m_playerName;
                } else {
                    log::warn("[ThumbnailViewPopup] GameManager::get() es null");
                }
            } catch(...) {
                log::error("[ThumbnailViewPopup] Excepción al acceder a GameManager");
            }
            
            Notification::create(Localization::get().getString("level.accepting").c_str(), NotificationIcon::Info)->show();
            
            std::string targetFilename = "";
            if (!m_suggestions.empty() && m_currentIndex >= 0 && m_currentIndex < m_suggestions.size()) {
                targetFilename = m_suggestions[m_currentIndex].filename;
            }

            // aceptar item cola
            ThumbnailAPI::get().acceptQueueItem(
                m_levelID, 
                static_cast<PendingCategory>(m_verificationCategory), 
                username,
                [levelID = m_levelID, category = m_verificationCategory](bool success, const std::string& message) {
                    if (success) {
                        // quitar cola local
                        PendingQueue::get().accept(levelID, static_cast<PendingCategory>(category));
                        Notification::create(Localization::get().getString("level.accepted").c_str(), NotificationIcon::Success)->show();
                        log::info("[ThumbnailViewPopup] Miniatura aceptada para nivel {}", levelID);
                    } else {
                        Notification::create(Localization::get().getString("level.accept_error") + message, NotificationIcon::Error)->show();
                        log::error("[ThumbnailViewPopup] Error aceptando miniatura: {}", message);
                    }
                },
                targetFilename
            );
            
            return;
        }
        
        // no verify -> subir localthumbs
        log::info("Intentando aceptar desde LocalThumbs");
        
        // local -> png imageconverter
        auto pathOpt = LocalThumbs::get().getThumbPath(m_levelID);
        if (!pathOpt) {
            Notification::create(Localization::get().getString("level.no_local_thumb").c_str(), NotificationIcon::Error)->show();
            return;
        }
        
        std::vector<uint8_t> pngData;
        if (!ImageConverter::loadRgbFileToPng(*pathOpt, pngData)) {
            Notification::create(Localization::get().getString("level.png_error").c_str(), NotificationIcon::Error)->show();
            return;
        }

        // log size png
        size_t base64Size = ((pngData.size() + 2) / 3) * 4;
        log::info("PNG size: {} bytes ({:.2f} KB), Base64 size: ~{} bytes ({:.2f} KB)", 
                 pngData.size(), pngData.size() / 1024.0, base64Size, base64Size / 1024.0);

        // check mod verified
// [SERVER DISABLED]         bool isMod = ModeratorVerification::isVerifiedModerator();

        // verificar online antes subir
        std::string username;
        try {
            auto* gm = GameManager::get();
            if (gm) {
                username = gm->m_playerName;
            } else {
                log::warn("[ThumbnailViewPopup] GameManager::get() es null");
            }
        } catch(...) {
            log::error("[ThumbnailViewPopup] Excepción al acceder a GameManager");
        }
        
        // upload server off
        log::warn("[ThumbnailViewPopup] Server upload disabled - thumbnail saved locally only");
        Notification::create(Localization::get().getString("level.saved_local_server_disabled").c_str(), NotificationIcon::Info)->show();
        
        /* server code off
        Notification::create(Localization::get().getString("capture.verifying").c_str(), NotificationIcon::Info)->show();
        ModeratorVerification::verifyOnline(username, [this, pngData, username](bool approved) {
            if (approved) {
                log::info("[ThumbnailViewPopup] User verified as moderator, uploading level {}", m_levelID);
                Notification::create(Localization::get().getString("capture.uploading").c_str, NotificationIcon::Info)->show();
                ServerAPI::get().uploadThumbnailPNG(m_levelID, pngData.data(), static_cast<int>(pngData.size()),
                    [levelID = m_levelID](bool success, const std::string&){
                        if (success) {
                            PendingQueue::get().removeForLevel(levelID);
                            Notification::create(Localization::get().getString("capture.upload_success").c_str(), NotificationIcon::Success)->show();
                        } else {
                            Notification::create(Localization::get().getString("capture.upload_error").c_str(), NotificationIcon::Error)->show();
                        }
                    }
                );
            } else {
                log::info("[ThumbnailViewPopup] Non-moderator user - enqueueing as pending");
                auto mappedLocal = LocalThumbs::get().getFileName(m_levelID);
                ServerAPI::get().checkThumbnailExists(m_levelID, [levelID = m_levelID, username, mappedLocal](bool existsServer){
                    auto cat = (mappedLocal.has_value() || existsServer) ? PendingCategory::Update : PendingCategory::Verify;
                    PendingQueue::get().addOrBump(levelID, cat, username);
                    Notification::create(Localization::get().getString("capture.suggested").c_str(), NotificationIcon::Info)->show();
                });
            }
        });
        */
    }

    void onReportBtn(CCObject*) {
        
        // guardar levelID antes de trabajo async
        int levelID = m_levelID;
        
        auto popup = ReportInputPopup::create(levelID, [levelID](std::string reason) {
            std::string user;
            try {
                auto* gm = GameManager::get();
                if (gm) {
                    user = gm->m_playerName;
                }
            } catch(...) {}
            
            // enviar report server
            ThumbnailAPI::get().submitReport(levelID, user, reason, [levelID, reason](bool success, const std::string& message) {
                if (success) {
                    Notification::create(Localization::get().getString("report.sent_synced") + reason, NotificationIcon::Warning)->show();
                    log::info("[ThumbnailViewPopup] Reporte confirmado y enviado al servidor para nivel {}", levelID);
                } else {
                    Notification::create(Localization::get().getString("report.saved_local").c_str(), NotificationIcon::Info)->show();
                    log::warn("[ThumbnailViewPopup] Reporte guardado solo localmente para nivel {}", levelID);
                }
            });
        });
        
        if (popup) {
            popup->show();
        }
    }

    void onDeleteThumbnail(CCObject*) {
        int levelID = m_levelID;
        auto gm = GameManager::get();
        auto am = GJAccountManager::get();
        std::string username = gm ? gm->m_playerName : "";
        int accountID = am ? am->m_accountID : 0;
        
        std::string thumbnailId = "";
        if (m_currentIndex >= 0 && m_currentIndex < m_thumbnails.size()) {
            thumbnailId = m_thumbnails[m_currentIndex].id;
        }

        this->retain();
        ThumbnailAPI::get().getRating(levelID, username, thumbnailId, [this, levelID, username, accountID](bool success, float avg, int count, int userVote) {
            ThumbnailAPI::get().checkModerator(username, [this, levelID, username, accountID, count](bool isMod, bool isAdmin) {
                if (!isMod && !isAdmin) {
                     Notification::create("No tienes permisos", NotificationIcon::Error)->show();
                     this->release();
                     return;
                }
                
                if (count > 100 && !isAdmin) {
                    Notification::create("Solo administradores pueden borrar miniaturas con +100 votos", NotificationIcon::Error)->show();
                    this->release();
                    return;
                }
                
                geode::createQuickPopup(
                    "Borrar Miniatura",
                    "Estas seguro de que quieres borrar esta miniatura? Esto tambien eliminara los puntos de rating del creador.",
                    "Cancelar", "Borrar",
                    [this, levelID, username, accountID](auto, bool btn2) {
                        if (btn2) {
                            auto loading = LoadingCircle::create();
                            loading->setParentLayer(this);
                            loading->setFade(true);
                            loading->show();
                            loading->retain();

                            ThumbnailAPI::get().deleteThumbnail(levelID, username, accountID, [this, loading](bool success, std::string msg) {
                                loading->fadeAndRemove();
                                loading->release();

                                if (success) {
                                    Notification::create("Miniatura borrada", NotificationIcon::Success)->show();
                                    this->onClose(nullptr);
                                } else {
                                    Notification::create(msg.c_str(), NotificationIcon::Error)->show();
                                }
                                this->release();
                            });
                        } else {
                            this->release();
                        }
                    }
                );
            });
        });
    }
    
    // recentrar
    void onRecenter(CCObject*) {
        if (!m_thumbnailSprite) return;
        
        m_thumbnailSprite->stopAllActions();
        
        auto content = this->m_mainLayer->getContentSize();
        float centerX = content.width * 0.5f;
        float centerY = content.height * 0.5f + 10.f;

        // anim recentrado
        auto moveTo = CCMoveTo::create(0.3f, {centerX, centerY});
        auto scaleTo = CCScaleTo::create(0.3f, m_initialScale);
        auto easeMove = CCEaseSineOut::create(moveTo);
        auto easeScale = CCEaseSineOut::create(scaleTo);

        m_thumbnailSprite->runAction(easeMove);
        m_thumbnailSprite->runAction(easeScale);
        m_thumbnailSprite->setAnchorPoint({0.5f, 0.5f});
    }
    
    // clamp min/max
    static float clamp(float value, float min, float max) {
        return std::max(min, std::min(value, max));
    }
    
    // limitar pos sprite al area
    void clampSpritePosition() {
        if (!m_thumbnailSprite || m_viewWidth <= 0 || m_viewHeight <= 0) return;

        float scale = m_thumbnailSprite->getScale();
        float spriteW = m_thumbnailSprite->getContentWidth() * scale;
        float spriteH = m_thumbnailSprite->getContentHeight() * scale;

        CCPoint pos = m_thumbnailSprite->getPosition();
        CCPoint anchor = m_thumbnailSprite->getAnchorPoint();

        // bordes sprite
        float spriteLeft = pos.x - spriteW * anchor.x;
        float spriteRight = pos.x + spriteW * (1.0f - anchor.x);
        float spriteBottom = pos.y - spriteH * anchor.y;
        float spriteTop = pos.y + spriteH * (1.0f - anchor.y);

        float newX = pos.x;
        float newY = pos.y;

        // img mas chica -> centrar
        if (spriteW <= m_viewWidth) {
            newX = m_viewWidth / 2;
        } else {
            // img mas grande -> limites
            // no pasar borde izq
            if (spriteLeft > 0) {
                newX = spriteW * anchor.x;
            }
            // no pasar borde der
            if (spriteRight < m_viewWidth) {
                newX = m_viewWidth - spriteW * (1.0f - anchor.x);
            }
        }

        if (spriteH <= m_viewHeight) {
            newY = m_viewHeight / 2;
        } else {
            // limites
            if (spriteBottom > 0) {
                newY = spriteH * anchor.y;
            }
            if (spriteTop < m_viewHeight) {
                newY = m_viewHeight - spriteH * (1.0f - anchor.y);
            }
        }

        m_thumbnailSprite->setPosition({newX, newY});
    }

    // clamp animado al soltar
    void clampSpritePositionAnimated() {
        if (!m_thumbnailSprite || m_viewWidth <= 0 || m_viewHeight <= 0) return;

        float scale = m_thumbnailSprite->getScale();
        float spriteW = m_thumbnailSprite->getContentWidth() * scale;
        float spriteH = m_thumbnailSprite->getContentHeight() * scale;

        CCPoint pos = m_thumbnailSprite->getPosition();
        CCPoint anchor = m_thumbnailSprite->getAnchorPoint();

        float spriteLeft = pos.x - spriteW * anchor.x;
        float spriteRight = pos.x + spriteW * (1.0f - anchor.x);
        float spriteBottom = pos.y - spriteH * anchor.y;
        float spriteTop = pos.y + spriteH * (1.0f - anchor.y);

        float newX = pos.x;
        float newY = pos.y;
        bool needsAnimation = false;

        if (spriteW <= m_viewWidth) {
            if (std::abs(newX - m_viewWidth / 2) > 0.5f) {
                newX = m_viewWidth / 2;
                needsAnimation = true;
            }
        } else {
            if (spriteLeft > 0) {
                newX = spriteW * anchor.x;
                needsAnimation = true;
            }
            if (spriteRight < m_viewWidth) {
                newX = m_viewWidth - spriteW * (1.0f - anchor.x);
                needsAnimation = true;
            }
        }

        if (spriteH <= m_viewHeight) {
            if (std::abs(newY - m_viewHeight / 2) > 0.5f) {
                newY = m_viewHeight / 2;
                needsAnimation = true;
            }
        } else {
            if (spriteBottom > 0) {
                newY = spriteH * anchor.y;
                needsAnimation = true;
            }
            if (spriteTop < m_viewHeight) {
                newY = m_viewHeight - spriteH * (1.0f - anchor.y);
                needsAnimation = true;
            }
        }

        if (needsAnimation) {
            m_thumbnailSprite->stopAllActions();
            auto moveTo = CCMoveTo::create(0.15f, {newX, newY});
            auto ease = CCEaseBackOut::create(moveTo);
            m_thumbnailSprite->runAction(ease);
        }
    }

    // touch zoom/pan
    bool ccTouchBegan(CCTouch* touch, CCEvent* event) override {
        if (!this->isVisible()) return false;

        // touch en area?
        auto touchPos = touch->getLocation();
        auto nodePos = m_mainLayer->convertToNodeSpace(touchPos);
        auto size = m_mainLayer->getContentSize();
        CCRect rect = {0, 0, size.width, size.height};
        
        if (!rect.containsPoint(nodePos)) {
            return false;
        }

        // toca btn menu?
        auto isTouchOnMenu = [](CCMenu* menu, CCTouch* touch) -> bool {
            if (!menu || !menu->isVisible()) return false;
            auto point = menu->convertTouchToNodeSpace(touch);
            
            for (auto* obj : CCArrayExt<CCObject*>(menu->getChildren())) {
                auto item = typeinfo_cast<CCMenuItem*>(obj);
                if (item && item->isVisible() && item->isEnabled()) {
                    if (item->boundingBox().containsPoint(point)) {
                        return true;
                    }
                }
            }
            return false;
        };

        if (isTouchOnMenu(m_buttonMenu, touch)) return false;
        if (isTouchOnMenu(m_ratingMenu, touch)) return false;

#ifdef GEODE_IS_ANDROID
        // android: consumir touch, no hacer nada (evita crash)
        return true;
#endif

        if (m_touches.size() == 1) {
            // 2do touch -> zoom
            auto firstTouch = *m_touches.begin();
            // evitar doble procesamiento
            if (firstTouch == touch) return true;

            auto firstLoc = firstTouch->getLocation();
            auto secondLoc = touch->getLocation();
            
            m_touchMidPoint = (firstLoc + secondLoc) / 2.0f;
            m_savedScale = m_thumbnailSprite ? m_thumbnailSprite->getScale() : m_initialScale;
            m_initialDistance = firstLoc.getDistance(secondLoc);
            
            // anchor al medio del touch
            if (m_thumbnailSprite) {
                auto oldAnchor = m_thumbnailSprite->getAnchorPoint();
                auto worldPos = m_thumbnailSprite->convertToWorldSpace({0, 0});
                auto newAnchorX = (m_touchMidPoint.x - worldPos.x) / m_thumbnailSprite->getScaledContentWidth();
                auto newAnchorY = (m_touchMidPoint.y - worldPos.y) / m_thumbnailSprite->getScaledContentHeight();
                
                m_thumbnailSprite->setAnchorPoint({clamp(newAnchorX, 0, 1), clamp(newAnchorY, 0, 1)});
                m_thumbnailSprite->setPosition({
                    m_thumbnailSprite->getPositionX() + m_thumbnailSprite->getScaledContentWidth() * -(oldAnchor.x - clamp(newAnchorX, 0, 1)),
                    m_thumbnailSprite->getPositionY() + m_thumbnailSprite->getScaledContentHeight() * -(oldAnchor.y - clamp(newAnchorY, 0, 1))
                });
            }
        }
        
        m_touches.insert(touch);
        return true;
    }
    
    void ccTouchMoved(CCTouch* touch, CCEvent* event) override {
#ifdef GEODE_IS_ANDROID
        return;
#endif
        if (!m_thumbnailSprite) return;
        
        if (m_touches.size() == 1) {
            // pan 1 dedo
            auto delta = touch->getDelta();
            m_thumbnailSprite->setPosition({
                m_thumbnailSprite->getPositionX() + delta.x,
                m_thumbnailSprite->getPositionY() + delta.y
            });
            // limites durante arrastre
            clampSpritePosition();
        } else if (m_touches.size() == 2) {
            // pinch zoom
            m_wasZooming = true;
            
            auto it = m_touches.begin();
            auto firstTouch = *it;
            ++it;
            auto secondTouch = *it;
            
            auto firstLoc = firstTouch->getLocation();
            auto secondLoc = secondTouch->getLocation();
            auto center = (firstLoc + secondLoc) / 2.0f;
            auto distNow = firstLoc.getDistance(secondLoc);
            
            // nuevo zoom
            // evitar div 0
            if (m_initialDistance < 0.1f) m_initialDistance = 0.1f;
            if (distNow < 0.1f) distNow = 0.1f;

            auto mult = m_initialDistance / distNow;
            if (mult < 0.0001f) mult = 0.0001f;

            auto zoom = clamp(m_savedScale / mult, m_minScale, m_maxScale);
            m_thumbnailSprite->setScale(zoom);
            
            // pos por movimiento centro
            auto centerDiff = m_touchMidPoint - center;
            m_thumbnailSprite->setPosition(m_thumbnailSprite->getPosition() - centerDiff);
            m_touchMidPoint = center;

            // limites despues zoom
            clampSpritePosition();
        }
    }
    
    void ccTouchEnded(CCTouch* touch, CCEvent* event) override {
        m_touches.erase(touch);

#ifdef GEODE_IS_ANDROID
        return;
#endif
        
        if (!m_thumbnailSprite) return;
        
        // zoom terminado, 1 touch -> reset
        if (m_wasZooming && m_touches.size() == 1) {
            auto scale = m_thumbnailSprite->getScale();
            
            // clamp scale animado
            if (scale < m_minScale) {
                m_thumbnailSprite->runAction(
                    CCEaseSineInOut::create(CCScaleTo::create(0.3f, m_minScale))
                );
            } else if (scale > m_maxScale) {
                m_thumbnailSprite->runAction(
                    CCEaseSineInOut::create(CCScaleTo::create(0.3f, m_maxScale))
                );
            }
            
            m_wasZooming = false;
        }
        
        // al soltar -> limites animados
        if (m_touches.empty()) {
            clampSpritePositionAnimated();
        }
    }
    
    // scroll wheel zoom
    void scrollWheel(float x, float y) override {
#ifdef GEODE_IS_ANDROID
        return;
#endif
        // popup y sprite existen?
        if (!m_mainLayer || !m_thumbnailSprite) {
            return;
        }
        
        // sprite en scene?
        if (!m_thumbnailSprite->getParent()) {
            m_thumbnailSprite = nullptr;
            return;
        }

        // direccion zoom: scroll up = in, down = out
        float scrollAmount = y;
        if (std::abs(y) < 0.001f) {
            scrollAmount = -x; // scroll horizontal fallback
        }

        // factor zoom
        float zoomFactor = scrollAmount > 0 ? 1.12f : 0.89f;

        float currentScale = m_thumbnailSprite->getScale();
        float newScale = currentScale * zoomFactor;
        
        // clamp zoom
        newScale = clamp(newScale, m_minScale, m_maxScale);
        
        // sin cambio -> salir
        if (std::abs(newScale - currentScale) < 0.001f) {
            return;
        }

        m_thumbnailSprite->setScale(newScale);

        // limites pos despues zoom
        clampSpritePosition();
    }
    
    void ccTouchCancelled(CCTouch* touch, CCEvent* event) override {
        m_touches.erase(touch);
        m_wasZooming = false;
    }

public:
    static LocalThumbnailViewPopup* create(int32_t levelID, bool canAcceptUpload) {
        auto ret = new LocalThumbnailViewPopup();
        // ancho 400
        if (ret && ret->init(400.f, 280.f)) {
            ret->setup({levelID, canAcceptUpload});
            ret->autorelease();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }
};

// exportada, usada desde otros
CCNode* createThumbnailViewPopup(int32_t levelID, bool canAcceptUpload, const std::vector<Suggestion>& suggestions) {
    auto ret = LocalThumbnailViewPopup::create(levelID, canAcceptUpload);
    if (ret) {
        ret->setSuggestions(suggestions);
    }
    return ret;
}

class $modify(PaimonLevelInfoLayer, LevelInfoLayer) {
    struct Fields {
        CCMenuItemSpriteExtra* m_thumbnailButton = nullptr;
        CCNode* m_pixelBg = nullptr;
        bool m_fromThumbsList = false;
        bool m_fromReportSection = false;
        bool m_fromVerificationQueue = false;
        bool m_fromLeaderboards = false;
        LeaderboardType m_leaderboardType = LeaderboardType::Default;
        LeaderboardStat m_leaderboardStat = LeaderboardStat::Stars;
        CCMenuItemSpriteExtra* m_acceptThumbBtn = nullptr;
        CCMenuItemSpriteExtra* m_editModeBtn = nullptr;
        CCMenuItemSpriteExtra* m_uploadLocalBtn = nullptr;
        CCMenu* m_extraMenu = nullptr;
        bool m_thumbnailRequested = false; // evita cargas duplicadas
        
        // multi-thumb
        std::vector<ThumbnailAPI::ThumbnailInfo> m_thumbnails;
        int m_currentThumbnailIndex = 0;
        CCMenuItemSpriteExtra* m_prevBtn = nullptr;
        CCMenuItemSpriteExtra* m_nextBtn = nullptr;
        CCMenuItemSpriteExtra* m_rateBtn = nullptr;
        bool m_cycling = true;
        float m_cycleTimer = 0.0f;
    };
    
    void applyThumbnailBackground(CCTexture2D* tex, int32_t levelID) {
        if (!tex) return;
        
        log::info("[LevelInfoLayer] Aplicando fondo del thumbnail");
        
        // estilo + intensidad
        std::string bgStyle = "blur";
        int intensity = 5;
        try { 
            bgStyle = geode::Mod::get()->getSettingValue<std::string>("levelinfo-background-style"); 
            intensity = static_cast<int>(geode::Mod::get()->getSettingValue<int64_t>("levelinfo-effect-intensity"));
        } catch(...) {}
        
        intensity = std::max(1, std::min(10, intensity));
        auto win = CCDirector::sharedDirector()->getWinSize();

        // lambda efectos
        auto applyEffects = [this, bgStyle, intensity, win, tex](CCSprite*& sprite, bool isGIF) {
            if (!sprite) return;

            // scale + pos inicial
            float scaleX = win.width / sprite->getContentSize().width;
            float scaleY = win.height / sprite->getContentSize().height;
            float scale = std::max(scaleX, scaleY);
            sprite->setScale(scale);
            sprite->setPosition({win.width / 2.0f, win.height / 2.0f});
            sprite->setAnchorPoint({0.5f, 0.5f});

            if (bgStyle == "normal") {
                ccTexParams params{GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE};
                sprite->getTexture()->setTexParameters(&params);
            } 
            else if (bgStyle == "pixel") {
                if (isGIF) {
                     auto shader = getOrCreateShader("pixelate"_spr, vertexShaderCell, fragmentShaderPixelate);
                     if (shader) {
                         sprite->setShaderProgram(shader);
                         shader->use();
                         shader->setUniformsForBuiltins();
                         float intensityVal = (intensity - 1) / 9.0f;
                         if (auto ags = typeinfo_cast<AnimatedGIFSprite*>(sprite)) ags->m_intensity = intensityVal;
                         else shader->setUniformLocationWith1f(shader->getUniformLocationForName("u_intensity"), intensityVal);
                         shader->setUniformLocationWith2f(shader->getUniformLocationForName("u_screenSize"), win.width, win.height);
                     }
                } else {
                    // modo pixel
                    float t = (intensity - 1) / 9.0f; 
                    float pixelFactor = 0.5f - (t * 0.47f); 
                    int renderWidth = std::max(32, static_cast<int>(win.width * pixelFactor));
                    int renderHeight = std::max(32, static_cast<int>(win.height * pixelFactor));
                    
                    auto renderTex = CCRenderTexture::create(renderWidth, renderHeight);
                    if (renderTex) {
                        float renderScaleX = static_cast<float>(renderWidth) / tex->getContentSize().width;
                        float renderScaleY = static_cast<float>(renderHeight) / tex->getContentSize().height;
                        float renderScale = std::min(renderScaleX, renderScaleY);
                        
                        sprite->setScale(renderScale);
                        sprite->setPosition({renderWidth / 2.0f, renderHeight / 2.0f});
                        
                        renderTex->begin();
                        glClearColor(0, 0, 0, 0);
                        glClear(GL_COLOR_BUFFER_BIT);
                        sprite->visit();
                        renderTex->end();
                        
                        auto pixelTexture = renderTex->getSprite()->getTexture();
                        sprite = CCSprite::createWithTexture(pixelTexture);
                        
                        if (sprite) {
                            float finalScaleX = win.width / renderWidth;
                            float finalScaleY = win.height / renderHeight;
                            float finalScale = std::max(finalScaleX, finalScaleY);
                            
                            sprite->setScale(finalScale);
                            sprite->setFlipY(true);
                            sprite->setAnchorPoint({0.5f, 0.5f});
                            sprite->setPosition({win.width / 2.0f, win.height / 2.0f});
                            
                            ccTexParams params{GL_NEAREST, GL_NEAREST, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE};
                            pixelTexture->setTexParameters(&params);
                        }
                    }
                }
            }
            else if (bgStyle == "blur") {
                if (isGIF) {
                     auto shader = Shaders::getBlurSinglePassShader();
                     if (shader) {
                         sprite->setShaderProgram(shader);
                         shader->use();
                         shader->setUniformsForBuiltins();
                         float intensityVal = (intensity - 1) / 9.0f;
                         if (auto ags = typeinfo_cast<AnimatedGIFSprite*>(sprite)) ags->m_intensity = intensityVal;
                         else shader->setUniformLocationWith1f(shader->getUniformLocationForName("u_intensity"), intensityVal);
                         shader->setUniformLocationWith2f(shader->getUniformLocationForName("u_screenSize"), win.width, win.height);
                     }
                } else {
                    sprite = Shaders::createBlurredSprite(tex, win, static_cast<float>(intensity));
                    if (sprite) sprite->setPosition({win.width / 2.0f, win.height / 2.0f});
                }
            }
            else {
                // shaders ambos
                CCGLProgram* shader = nullptr;
                float val = 0.0f;
                bool useScreenSize = false;

                if (bgStyle == "grayscale") {
                    val = intensity / 10.0f;
                    shader = getOrCreateShader("grayscale"_spr, vertexShaderCell, fragmentShaderGrayscale);
                } else if (bgStyle == "sepia") {
                    val = intensity / 10.0f;
                    shader = getOrCreateShader("sepia"_spr, vertexShaderCell, fragmentShaderSepia);
                } else if (bgStyle == "vignette") {
                    val = intensity / 10.0f;
                    shader = getOrCreateShader("vignette"_spr, vertexShaderCell, fragmentShaderVignette);
                } else if (bgStyle == "scanlines") {
                    val = intensity / 10.0f;
                    shader = getOrCreateShader("scanlines"_spr, vertexShaderCell, fragmentShaderScanlines);
                    useScreenSize = true;
                } else if (bgStyle == "bloom") {
                    val = (intensity / 10.0f) * 2.25f;
                    shader = getOrCreateShader("bloom"_spr, vertexShaderCell, fragmentShaderBloom);
                    useScreenSize = true;
                } else if (bgStyle == "chromatic") {
                    val = (intensity / 10.0f) * 2.25f;
                    shader = getOrCreateShader("chromatic"_spr, vertexShaderCell, fragmentShaderChromatic);
                } else if (bgStyle == "radial-blur") {
                    val = (intensity / 10.0f) * 2.25f;
                    shader = getOrCreateShader("radial-blur"_spr, vertexShaderCell, fragmentShaderRadialBlur);
                } else if (bgStyle == "glitch") {
                    val = (intensity / 10.0f) * 2.25f;
                    shader = getOrCreateShader("glitch"_spr, vertexShaderCell, fragmentShaderGlitch);
                } else if (bgStyle == "posterize") {
                    val = intensity / 10.0f;
                    shader = getOrCreateShader("posterize"_spr, vertexShaderCell, fragmentShaderPosterize);
                }

                if (shader) {
                    sprite->setShaderProgram(shader);
                    shader->use();
                    shader->setUniformsForBuiltins();
                    if (auto ags = typeinfo_cast<AnimatedGIFSprite*>(sprite)) {
                        ags->m_intensity = val;
                    } else {
                        shader->setUniformLocationWith1f(shader->getUniformLocationForName("u_intensity"), val);
                    }
                    if (useScreenSize) {
                        shader->setUniformLocationWith2f(shader->getUniformLocationForName("u_screenSize"), win.width, win.height);
                    }
                }
            }
        };

        // 1. sprite estatico
        CCSprite* finalSprite = CCSprite::createWithTexture(tex);
        if (finalSprite) {
            applyEffects(finalSprite, false);
            
            if (m_fields->m_pixelBg) {
                m_fields->m_pixelBg->removeFromParent();
            } else if (auto old = this->getChildByID("paimon-levelinfo-pixel-bg"_spr)) {
                old->removeFromParent();
            }
            
            finalSprite->setZOrder(-1);
            finalSprite->setID("paimon-levelinfo-pixel-bg"_spr);
            this->addChild(finalSprite);
            m_fields->m_pixelBg = finalSprite;
        }

        // 2. gif? reemplazar
        if (ThumbnailLoader::get().hasGIFData(levelID)) {
             auto path = ThumbnailLoader::get().getCachePath(levelID);
             this->retain();
             AnimatedGIFSprite::createAsync(path.generic_string(), [this, applyEffects](AnimatedGIFSprite* anim) {
                 if (anim) {
                     // quitar fondo estatico
                     if (m_fields->m_pixelBg) {
                         m_fields->m_pixelBg->removeFromParent();
                     } else if (auto old = this->getChildByID("paimon-levelinfo-pixel-bg"_spr)) {
                         old->removeFromParent();
                     }
                     
                     // efectos al gif
                     CCSprite* spritePtr = anim; // el helper espera CCSprite*&
                     applyEffects(spritePtr, true);
                     
                     anim->setZOrder(-1);
                     anim->setID("paimon-levelinfo-pixel-bg"_spr);
                     
                     this->addChild(anim);
                     m_fields->m_pixelBg = anim;
                 }
                 this->release();
             });
        }

        // overlay
        auto overlay = CCLayerColor::create({0,0,0,70});
        overlay->setContentSize(win);
        overlay->setAnchorPoint({0,0});
        overlay->setPosition({0,0});
        overlay->setZOrder(-1);
        overlay->setID("paimon-levelinfo-pixel-overlay"_spr);
        this->addChild(overlay);
        
        log::info("[LevelInfoLayer] Fondo aplicado exitosamente (estilo: {}, intensidad: {})", bgStyle, intensity);
    }
    
    void onExit() {
        ThumbnailLoader::get().resumeQueue();
        LevelInfoLayer::onExit();
    }

    void onSetDailyWeekly(CCObject* sender) {
        if (m_level->m_levelID.value() <= 0) return;
        SetDailyWeeklyPopup::create(m_level->m_levelID.value())->show();
    }

    bool init(GJGameLevel* level, bool challenge) {
        // vinimos de leaderboards?
        if (auto scene = CCDirector::sharedDirector()->getRunningScene()) {
            if (auto layer = scene->getChildByType<LeaderboardsLayer>(0)) {
                m_fields->m_fromLeaderboards = true;
                // m_fields->m_leaderboardType = layer->m_type; // miembro oculto/quitado
                m_fields->m_leaderboardType = LeaderboardType::Default;
            }
        }

        if (!LevelInfoLayer::init(level, challenge)) return false;
        // thumbnailLoader::get().pauseQueue(); // removido para permitir carga en segundo plano
        
        try {
            if (!level || level->m_levelID <= 0) {
                log::debug("Level ID invalid, skipping thumbnail button");
                return true;
            }

            // Paimon: cancion dinamica
            DynamicSongManager::get()->playSong(level);

            // consumir el flag "abierto desde lista de miniaturas"
            bool fromThumbs = false;
            try {
                fromThumbs = Mod::get()->getSavedValue<bool>("open-from-thumbs", false);
                if (fromThumbs) Mod::get()->setSavedValue("open-from-thumbs", false);
            } catch(...) {}
            m_fields->m_fromThumbsList = fromThumbs;

            // abierto desde report?
            bool fromReport = false;
            try {
                fromReport = Mod::get()->getSavedValue<bool>("open-from-report", false);
                if (fromReport) Mod::get()->setSavedValue("open-from-report", false);
            } catch(...) {}
            m_fields->m_fromReportSection = fromReport;
            
            // vinimos de cola verify?
            bool fromVerificationQueue = false;
            int verificationQueueCategory = -1;
            int verificationQueueLevelID = -1;
            try {
                // cola verify? check level id
                verificationQueueLevelID = Mod::get()->getSavedValue<int>("verification-queue-levelid", -1);
                
                if (verificationQueueLevelID == level->m_levelID.value()) {
                    fromVerificationQueue = true;
                    verificationQueueCategory = Mod::get()->getSavedValue<int>("verification-queue-category", -1);
                    m_fields->m_fromVerificationQueue = true;
                    
                    // no limpiar, persistir en playlayer
                }
            } catch(...) {}
            
            // fondo pixel thumb
            bool isMainLevel = level->m_levelType == GJLevelType::Main;
            if (!isMainLevel && !m_fields->m_thumbnailRequested) {
                m_fields->m_thumbnailRequested = true;
                int32_t levelID = level->m_levelID.value();
                std::string fileName = fmt::format("{}.png", levelID);
                // retain pa carga async
                this->retain();
                auto selfPtr = this;
                ThumbnailLoader::get().requestLoad(levelID, fileName, [selfPtr, levelID](CCTexture2D* tex, bool success) {
                    // validar que el layer aun existe antes de usarlo
                    try {
                        if (!selfPtr || !selfPtr->getParent()) {
                            log::warn("[LevelInfoLayer] Layer invalidated before applying pixel background");
                            // no release si destruido
                            return;
                        }
                    } catch (...) {
                        log::error("[LevelInfoLayer] Exception validating layer before pixel background");
                        return;
                    }
                    if (tex) {
                        static_cast<PaimonLevelInfoLayer*>(selfPtr)->applyThumbnailBackground(tex, levelID);
                    } else {
                        log::warn("[LevelInfoLayer] No texture for pixel background");
                    }
                    selfPtr->release();
                }, 5);
            }

            // load layouts botones
            ButtonLayoutManager::get().load();
            
            // menu izq
            auto leftMenu = this->getChildByID("left-side-menu");
            if (!leftMenu) {
                log::warn("Left side menu not found");
                return true;
            }

            // ref menu pa buttoneditoverlay
            m_fields->m_extraMenu = static_cast<CCMenu*>(leftMenu);
            
            // sprite icono btn
            CCSprite* iconSprite = nullptr;

            // recurso custom primero
            iconSprite = CCSprite::create("paim_BotonMostrarThumbnails.png"_spr);

            // fallback camara
            if (!iconSprite) {
                iconSprite = CCSprite::createWithSpriteFrameName("GJ_messagesBtn_001.png");
            }
            
            // fallback info
            if (!iconSprite) {
                iconSprite = CCSprite::createWithSpriteFrameName("GJ_infoIcon_001.png");
            }
            
            if (!iconSprite) {
                log::error("Failed to create button sprite");
                return true;
            }
            
            // rotar 90
            iconSprite->setRotation(-90.0f);
            // reducir el icono un 20%
            iconSprite->setScale(0.8f);

            // CircleButtonSprite verde
            auto btnSprite = CircleButtonSprite::create(
                iconSprite,
                CircleBaseColor::Green,
                CircleBaseSize::Medium
            );

            if (!btnSprite) {
                log::error("Failed to create CircleButtonSprite");
                return true;
            }
            
            auto button = CCMenuItemSpriteExtra::create(
                btnSprite,
                this,
                menu_selector(PaimonLevelInfoLayer::onThumbnailButton)
            );
            PaimonButtonHighlighter::registerButton(button);
            
            if (!button) {
                log::error("Failed to create menu button");
                return true;
            }
            
            button->setID("thumbnail-view-button"_spr);
            m_fields->m_thumbnailButton = button;

            // thumbs galeria
            this->retain();
            ThumbnailAPI::get().getThumbnails(level->m_levelID.value(), [this](bool success, const std::vector<ThumbnailAPI::ThumbnailInfo>& thumbs) {
                // retain() = vivo
                
                if (success) {
                    m_fields->m_thumbnails = thumbs;
                }
                
                // vacio -> default
                if (m_fields->m_thumbnails.empty()) {
                     ThumbnailAPI::ThumbnailInfo mainThumb;
                     mainThumb.id = "0";
                     mainThumb.url = ThumbnailAPI::get().getThumbnailURL(m_level->m_levelID.value());
                     m_fields->m_thumbnails.push_back(mainThumb);
                }

                if (m_fields->m_thumbnails.size() > 1) {
                    this->setupGallery();
                    this->schedule(schedule_selector(PaimonLevelInfoLayer::updateGallery));
                } else {
                    this->setupGallery();
                    if (m_fields->m_prevBtn) m_fields->m_prevBtn->setVisible(false);
                    if (m_fields->m_nextBtn) m_fields->m_nextBtn->setVisible(false);
                }
                
                this->release();
            });

            // add primero pa layout default
            leftMenu->addChild(button);
            leftMenu->updateLayout();

            ButtonLayout defaultLayout;
            defaultLayout.position = button->getPosition();
            defaultLayout.scale = button->getScale();
            defaultLayout.opacity = 1.0f;
            ButtonLayoutManager::get().setDefaultLayoutIfAbsent("LevelInfoLayer", "thumbnail-view-button", defaultLayout);

            // load layout guardado
            auto savedLayout = ButtonLayoutManager::get().getLayout("LevelInfoLayer", "thumbnail-view-button");
            if (savedLayout) {
                button->setPosition(savedLayout->position);
                button->setScale(savedLayout->scale);
                button->setOpacity(static_cast<GLubyte>(savedLayout->opacity * 255));
                // update scale registrada
                PaimonButtonHighlighter::updateButtonScale(button);
            }

            // admin? -> btn daily/weekly
            if (auto gm = GameManager::get()) {
                auto username = gm->m_playerName;
                auto accountID = gm->m_playerUserID;
                
                this->retain();
                HttpClient::get().checkModeratorAccount(username, accountID, [this](bool isMod, bool isAdmin) {
                    if (isAdmin) {
                        // btn daily
                        // icono estrella/tiempo
                        CCSprite* iconSpr = CCSprite::createWithSpriteFrameName("GJ_timeIcon_001.png");

                        if (!iconSpr) {
                            iconSpr = CCSprite::createWithSpriteFrameName("GJ_starBtn_001.png");
                        }
                        
                        // reducir el icono un 20%
                        iconSpr->setScale(0.8f);

                        // CircleButtonSprite verde
                        auto btnSprite = CircleButtonSprite::create(
                            iconSpr,
                            CircleBaseColor::Green,
                            CircleBaseSize::Medium
                        );

                        if (!btnSprite) {
                            this->release();
                            return;
                        }

                        auto btn = CCMenuItemSpriteExtra::create(
                            btnSprite,
                            this,
                            menu_selector(PaimonLevelInfoLayer::onSetDailyWeekly)
                        );
                        btn->setID("set-daily-weekly-button"_spr);
                        
                        auto leftMenu = static_cast<CCMenu*>(this->getChildByID("left-side-menu"));
                        if (leftMenu) {
                            leftMenu->addChild(btn);
                            leftMenu->updateLayout();
                        }
                    }
                    this->release();
                });
            }

            log::info("Thumbnail button added successfully");
            
            // cola verify -> guardar categoria
            if (fromVerificationQueue && verificationQueueLevelID == level->m_levelID.value()) {
                log::info("Nivel abierto desde verificación (categoría: {}) - botón listo para usar", verificationQueueCategory);
                // categoria pa thumbnailviewpopup
                Mod::get()->setSavedValue("verification-category", verificationQueueCategory);
            }

            // botones de aceptar/subir ahora se muestran dentro de thumbnailviewpopup
            
        } catch (std::exception& e) {
            log::error("Exception in LevelInfoLayer::init: {}", e.what());
        } catch (...) {
            log::error("Unknown exception in LevelInfoLayer::init");
        }
        
        return true;
    }
    
    void onThumbnailButton(CCObject*) {
        log::info("Thumbnail button clicked");
        
        try {
            if (!m_level) {
                log::error("Level is null");
                return;
            }
            
            int32_t levelID = m_level->m_levelID.value();
            log::info("Opening thumbnail view for level ID: {}", levelID);
            
            // usar utilidad moderatorverification
            bool canAccept = false; // sin funcionalidad de server
            // contexto popup flag
            Mod::get()->setSavedValue("from-report-popup", m_fields->m_fromReportSection);
            auto popup = LocalThumbnailViewPopup::create(levelID, canAccept);
            if (popup) {
                popup->show();
            } else {
                log::error("Failed to create thumbnail view popup");
                Notification::create("Error al abrir miniatura", NotificationIcon::Error)->show();
            }
            
        } catch (std::exception& e) {
            log::error("Exception in onThumbnailButton: {}", e.what());
            Notification::create(Localization::get().getString("level.error_prefix") + std::string(e.what()), NotificationIcon::Error)->show();
        } catch (...) {
            log::error("Unknown exception in onThumbnailButton");
            Notification::create("Error desconocido", NotificationIcon::Error)->show();
        }
    }

    void onToggleEditMode(CCObject*) {
        if (!m_fields->m_extraMenu) return;

        // glow modo edicion
        PaimonButtonHighlighter::highlightAll();

        // overlay edicion
        auto overlay = ButtonEditOverlay::create("LevelInfoLayer", m_fields->m_extraMenu);
        if (auto scene = CCDirector::sharedDirector()->getRunningScene()) {
            scene->addChild(overlay, 1000);
        }
    }

    void onUploadLocalThumbnail(CCObject*) {
        log::info("[LevelInfoLayer] Upload local thumbnail button clicked");
        
        if (!m_level) {
            Notification::create(Localization::get().getString("level.error_prefix") + "nivel no encontrado", NotificationIcon::Error)->show();
            return;
        }
        
        // ptr nivel antes async
        auto* level = m_level;
        int32_t levelID = level->m_levelID.value();
        
        // existe thumb local?
        if (!LocalThumbs::get().has(levelID)) {
            Notification::create(Localization::get().getString("level.no_local_thumb").c_str(), NotificationIcon::Error)->show();
            return;
        }
        
        // obtener nombre de usuario
        std::string username;
        try {
            auto* gm = GameManager::get();
            if (gm) {
                username = gm->m_playerName;
            } else {
                log::warn("[LevelInfoLayer] GameManager::get() es null");
                username = "Unknown";
            }
        } catch(...) {
            log::error("[LevelInfoLayer] Excepción al acceder a GameManager");
            username = "Unknown";
        }
        
        // load local -> png
        auto pathOpt = LocalThumbs::get().getThumbPath(levelID);
        if (!pathOpt) {
            Notification::create("No se pudo encontrar la miniatura", NotificationIcon::Error)->show();
            return;
        }
        
        std::vector<uint8_t> pngData;
        if (!ImageConverter::loadRgbFileToPng(*pathOpt, pngData)) {
            Notification::create(Localization::get().getString("level.png_error").c_str(), NotificationIcon::Error)->show();
            return;
        }
        
        // retain self pa callbacks
        this->retain();

        // check mod
        ThumbnailAPI::get().checkModerator(username, [this, levelID, pngData, username](bool isMod, bool isAdmin) {
            if (isMod || isAdmin) {
                auto onFinish = [this, levelID](bool success, const std::string& msg) {
                    if (success) {
                        Notification::create(Localization::get().getString("capture.upload_success").c_str(), NotificationIcon::Success)->show();
                        try {
                            auto path = ThumbnailLoader::get().getCachePath(levelID);
                            if (std::filesystem::exists(path)) std::filesystem::remove(path);
                        } catch(...) {}
                        ThumbnailLoader::get().invalidateLevel(levelID);
                        ThumbnailLoader::get().requestLoad(levelID, "", [this, levelID](CCTexture2D* tex, bool success) {
                            if (success && tex) {
                                if (m_fields->m_pixelBg) {
                                    m_fields->m_pixelBg->removeFromParent();
                                    m_fields->m_pixelBg = nullptr;
                                }
                                this->applyThumbnailBackground(tex, levelID);
                            }
                            this->release();
                        });
                        return;
                    } else {
                        Notification::create(Localization::get().getString("capture.upload_error") + msg, NotificationIcon::Error)->show();
                    }
                    this->release();
                };

                // subir directo (sobrescribe si hay algo, el servidor hace enforcement)
                Notification::create(Localization::get().getString("capture.uploading").c_str(), NotificationIcon::Info)->show();
                ThumbnailAPI::get().uploadThumbnail(levelID, pngData, username, onFinish);
            } else {
                // user: existe thumb? suggestion vs update
                log::info("[LevelInfoLayer] Regular user upload for level {}", levelID);
                
                ThumbnailAPI::get().checkExists(levelID, [this, levelID, pngData, username](bool exists) {
                    if (exists) {
                        // existe -> update
                        log::info("[LevelInfoLayer] Uploading as update for level {}", levelID);
                        Notification::create(Localization::get().getString("capture.uploading_suggestion").c_str(), NotificationIcon::Info)->show();
                        
                        ThumbnailAPI::get().uploadUpdate(levelID, pngData, username, [this](bool success, const std::string& msg) {
                            if (success) {
                                Notification::create(Localization::get().getString("capture.suggested").c_str(), NotificationIcon::Success)->show();
                            } else {
                                Notification::create(Localization::get().getString("capture.upload_error") + msg, NotificationIcon::Error)->show();
                            }
                            this->release();
                        });
                    } else {
                        // si no existe -> enviar como sugerencia
                        log::info("[LevelInfoLayer] Uploading as suggestion for level {}", levelID);
                        Notification::create(Localization::get().getString("capture.uploading_suggestion").c_str(), NotificationIcon::Info)->show();
                        
                        ThumbnailAPI::get().uploadSuggestion(levelID, pngData, username, [this](bool success, const std::string& msg) {
                            if (success) {
                                Notification::create(Localization::get().getString("capture.suggested").c_str(), NotificationIcon::Success)->show();
                            } else {
                                Notification::create(Localization::get().getString("capture.upload_error") + msg, NotificationIcon::Error)->show();
                            }
                            this->release();
                        });
                    }
                });
            }
        });
    }

    void onBack(CCObject* sender) {
        DynamicSongManager::get()->stopSong();

        if (m_fields->m_fromVerificationQueue) {
            // limpiar los flags
            Mod::get()->setSavedValue("open-from-verification-queue", false);
            Mod::get()->setSavedValue("verification-queue-levelid", -1);
            Mod::get()->setSavedValue("verification-queue-category", -1);
            
            // reabrir popup
            Mod::get()->setSavedValue("reopen-verification-queue", true);
            
            // volver a menulayer
            CCDirector::sharedDirector()->replaceScene(CCTransitionFade::create(0.5f, MenuLayer::scene(false)));
            return;
        }

        // abrio desde leaderboards?
        if (m_fields->m_fromLeaderboards) {
            auto scene = LeaderboardsLayer::scene(m_fields->m_leaderboardType, m_fields->m_leaderboardStat);
            CCDirector::sharedDirector()->replaceScene(CCTransitionFade::create(0.5f, scene));
            return;
        }

        // sin anim daily
        
        LevelInfoLayer::onBack(sender);
    }

    void setupGallery() {
        // flechas
        auto menu = CCMenu::create();
        menu->setID("gallery-menu"_spr);

        if (m_fields->m_thumbnailButton) {
            menu->setPosition(m_fields->m_thumbnailButton->getPosition());
            this->addChild(menu, 100);
        }
    }
    
    void onRateBtn(CCObject* sender) {
        // abrir ratepopup con estrella pre-seleccionada? o solo abrirlo.
        // el usuario podria querer ratear directamente.
        // ratepopup maneja logica
        if (m_fields->m_currentThumbnailIndex < 0 || m_fields->m_currentThumbnailIndex >= m_fields->m_thumbnails.size()) return;
        
        auto& thumb = m_fields->m_thumbnails[m_fields->m_currentThumbnailIndex];
        RatePopup::create(m_level->m_levelID.value(), thumb.id)->show();
    }
    
    void updateGallery(float dt) {
        if (!m_fields->m_cycling || m_fields->m_thumbnails.size() <= 1) return;
        
        m_fields->m_cycleTimer += dt;
        if (m_fields->m_cycleTimer >= 3.0f) {
            m_fields->m_cycleTimer = 0.0f;
            m_fields->m_currentThumbnailIndex = (m_fields->m_currentThumbnailIndex + 1) % m_fields->m_thumbnails.size();
            this->loadThumbnail(m_fields->m_currentThumbnailIndex);
        }
    }
    
    void onPrevBtn(CCObject*) {
        m_fields->m_cycling = false; // detener auto-ciclado al interactuar
        m_fields->m_currentThumbnailIndex--;
        if (m_fields->m_currentThumbnailIndex < 0) m_fields->m_currentThumbnailIndex = m_fields->m_thumbnails.size() - 1;
        this->loadThumbnail(m_fields->m_currentThumbnailIndex);
    }
    
    void onNextBtn(CCObject*) {
        m_fields->m_cycling = false;
        m_fields->m_currentThumbnailIndex = (m_fields->m_currentThumbnailIndex + 1) % m_fields->m_thumbnails.size();
        this->loadThumbnail(m_fields->m_currentThumbnailIndex);
    }
    
    void loadThumbnail(int index) {
        if (index < 0 || index >= m_fields->m_thumbnails.size()) return;
        
        auto& thumb = m_fields->m_thumbnails[index];
        // load desde url
        ThumbnailAPI::get().downloadFromUrl(thumb.url, [this, index](bool success, CCTexture2D* tex) {
            if (success && tex) {
                // update sprite btn thumb
                if (m_fields->m_thumbnailButton) {
                    auto spr = (CCSprite*)m_fields->m_thumbnailButton->getNormalImage();
                    if (spr) {
                        spr->setTexture(tex);
                        spr->setTextureRect({0, 0, tex->getContentSize().width, tex->getContentSize().height});
                    }
                }
                // update bg
                int32_t levelID = (index == 0 && m_level) ? m_level->m_levelID.value() : 0;
                this->applyThumbnailBackground(tex, levelID);
            }
        });
    }
};



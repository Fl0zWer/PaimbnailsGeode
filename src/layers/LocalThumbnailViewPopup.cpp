#include "LocalThumbnailViewPopup.hpp"

#include <Geode/ui/Popup.hpp>
#include <Geode/ui/BasedButtonSprite.hpp>
#include <Geode/ui/LoadingSpinner.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/GJAccountManager.hpp>
#include <Geode/utils/cocos.hpp>
#include <Geode/utils/string.hpp>
#include <sstream>
#include <thread>
#include <fstream>

#include "../managers/LocalThumbs.hpp"
#include "../managers/PendingQueue.hpp"
#include "../managers/ThumbnailAPI.hpp"
#include "../managers/ThumbnailLoader.hpp"
#include "../utils/AnimatedGIFSprite.hpp"
#include "../utils/FileDialog.hpp"
#include "../utils/Assets.hpp"
#include "../utils/Localization.hpp"
#include "../utils/Constants.hpp"
#include "../utils/ImageConverter.hpp"
#include "../utils/HttpClient.hpp"
#include "../utils/UIBorderHelper.hpp"
#include "../utils/PaimonNotification.hpp"
#include "../layers/RatePopup.hpp"
#include "../layers/ReportInputPopup.hpp"
#include "../layers/ThumbnailSettingsPopup.hpp"

using namespace geode::prelude;
using namespace cocos2d;


void LocalThumbnailViewPopup::onPrev(CCObject*) {
    if (m_thumbnails.empty()) return;
    m_currentIndex--;
    if (m_currentIndex < 0) m_currentIndex = static_cast<int>(m_thumbnails.size()) - 1;
    loadThumbnailAt(m_currentIndex);
}

void LocalThumbnailViewPopup::onNext(CCObject*) {
    if (m_thumbnails.empty()) return;
    m_currentIndex++;
    if (m_currentIndex >= static_cast<int>(m_thumbnails.size())) m_currentIndex = 0;
    loadThumbnailAt(m_currentIndex);
}

void LocalThumbnailViewPopup::onInfo(CCObject*) {
    std::string resStr = "Unknown";
    if (m_thumbnailTexture) {
         resStr = fmt::format("{} x {}", m_thumbnailTexture->getPixelsWide(), m_thumbnailTexture->getPixelsHigh());
    }

    std::string id = "Unknown";
    std::string type = "Static";
    std::string format = "Unknown";
    std::string creator = "Unknown";
    std::string date = "Unknown";

    if (m_currentIndex >= 0 && m_currentIndex < static_cast<int>(m_thumbnails.size())) {
         auto& item = m_thumbnails[m_currentIndex];
         id = item.id;
         type = item.type;
         format = item.format;
         if (!item.creator.empty()) creator = item.creator;
         if (!item.date.empty()) date = item.date;

         // fecha desde id si parece timestamp
         if (date == "Unknown" && id.length() >= 13) {
             auto numResult = geode::utils::numFromString<long long>(id);
             if (numResult.isOk()) {
                 long long timestamp = numResult.unwrap();
                 if (timestamp > 1600000000000) {
                     time_t timeSec = timestamp / 1000;
                     struct tm t;
#ifdef _WIN32
                     if (localtime_s(&t, &timeSec) == 0) {
#else
                     if (localtime_r(&timeSec, &t) != nullptr) {
#endif
                         date = fmt::format("{:02}/{:02}/{:02} {:02}:{:02}",
                            t.tm_mday, t.tm_mon + 1, t.tm_year % 100,
                            t.tm_hour, t.tm_min);
                     }
                 }
             }
         }

         // yyyy-mm-dd -> dd/mm/aa
         if (date.length() >= 10 && date[4] == '-' && date[7] == '-') {
             std::string year = date.substr(2, 2);
             std::string month = date.substr(5, 2);
             std::string day = date.substr(8, 2);

             std::string timeStr = "";
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

void LocalThumbnailViewPopup::loadThumbnailAt(int index) {
    if (index < 0 || index >= static_cast<int>(m_thumbnails.size())) return;

    auto& thumb = m_thumbnails[index];
    std::string url = thumb.url;

    std::string username = "Unknown";
    if (auto gm = GameManager::get()) username = gm->m_playerName;

    // update rating ui
    Ref<LocalThumbnailViewPopup> self = this;
    ThumbnailAPI::get().getRating(m_levelID, username, thumb.id, [self](bool success, float average, int count, int userVote) {
        if (success && self->m_ratingLabel) {
            self->m_ratingLabel->setString(fmt::format("{:.1f} ({})", average, count).c_str());
            if (count == 0) {
                self->m_ratingLabel->setColor({255, 100, 100});
            } else {
                self->m_ratingLabel->setColor({255, 255, 255});
            }
            self->m_userVote = userVote;
            self->m_initialUserVote = userVote;
        }
    });

    // download y mostrar
    ThumbnailAPI::get().downloadFromUrl(url, [self](bool success, CCTexture2D* tex) {
        if (success && tex) {
            auto content = self->m_mainLayer->getContentSize();
            float maxWidth = content.width - 40.f;
            float maxHeight = content.height - 70.f;
            self->displayThumbnail(tex, maxWidth, maxHeight, content, false);
        }
    });
}

LocalThumbnailViewPopup::~LocalThumbnailViewPopup() {
    log::info("[ThumbnailViewPopup] Destructor - liberando textura retenida");
    if (m_thumbnailTexture) {
        m_thumbnailTexture->release();
        m_thumbnailTexture = nullptr;
    }
    m_touches.clear();
}

void LocalThumbnailViewPopup::setSuggestions(std::vector<Suggestion> const& suggestions) {
    m_suggestions = suggestions;
    if (!m_suggestions.empty()) {
        m_currentIndex = 0;
        this->loadCurrentSuggestion();
    }
}

void LocalThumbnailViewPopup::loadCurrentSuggestion() {
    if (m_suggestions.empty()) return;

    auto& suggestion = m_suggestions[m_currentIndex];
    log::info("[ThumbnailViewPopup] Loading suggestion {}/{} - {}", m_currentIndex + 1, m_suggestions.size(), suggestion.filename);

    if (m_counterLabel) {
        m_counterLabel->setString(fmt::format("{}/{}", m_currentIndex + 1, m_suggestions.size()).c_str());
    }

    if (m_leftArrow) m_leftArrow->setVisible(m_suggestions.size() > 1);
    if (m_rightArrow) m_rightArrow->setVisible(m_suggestions.size() > 1);

    std::string url = std::string(PaimonConstants::THUMBNAIL_CDN_URL) + suggestion.filename;

    LocalThumbnailViewPopup* popupPtr = this;
    this->retain();

    ThumbnailAPI::get().downloadFromUrl(url, [popupPtr](bool success, CCTexture2D* tex) {
         if (!popupPtr->getParent() || !popupPtr->m_mainLayer) {
             popupPtr->release();
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

void LocalThumbnailViewPopup::onNextSuggestion(CCObject*) {
    if (m_suggestions.empty()) return;
    m_currentIndex++;
    if (m_currentIndex >= static_cast<int>(m_suggestions.size())) {
        m_currentIndex = 0;
    }
    loadCurrentSuggestion();
}

void LocalThumbnailViewPopup::onPrevSuggestion(CCObject*) {
    if (m_suggestions.empty()) return;
    m_currentIndex--;
    if (m_currentIndex < 0) {
        m_currentIndex = static_cast<int>(m_suggestions.size()) - 1;
    }
    loadCurrentSuggestion();
}


void LocalThumbnailViewPopup::onExit() {
    log::info("[ThumbnailViewPopup] onExit() comenzando");

    m_touches.clear();

    if (m_isExiting) {
        log::warn("[ThumbnailViewPopup] onExit() ya fue llamado, evitando re-entrada");
        return;
    }
    m_isExiting = true;

    if (m_mainLayer) {
        m_mainLayer->removeAllChildren();
    }
    m_ratingMenu = nullptr;
    m_buttonMenu = nullptr;
    m_settingsMenu = nullptr;
    m_ratingLabel = nullptr;
    m_counterLabel = nullptr;
    m_leftArrow = nullptr;
    m_rightArrow = nullptr;

    m_thumbnailSprite = nullptr;
    m_clippingNode = nullptr;

    log::info("[ThumbnailViewPopup] Llamando a parent onExit");
    Popup::onExit();
}


void LocalThumbnailViewPopup::setupRating() {
    if (auto node = m_mainLayer->getChildByID("rating-container"_spr)) {
        node->removeFromParent();
    }

    auto contentSize = m_mainLayer->getContentSize();

    auto ratingContainer = CCNode::create();
    ratingContainer->setID("rating-container"_spr);
    ratingContainer->setPosition({contentSize.width / 2.f, 237.f});
    m_mainLayer->addChild(ratingContainer, 100);

    if (!CCSpriteFrameCache::sharedSpriteFrameCache()->spriteFrameByName("square02_001.png")) {
        CCSpriteFrameCache::sharedSpriteFrameCache()->addSpriteFramesWithFile("GJ_GameSheet03.plist");
    }

    auto bg = cocos2d::extension::CCScale9Sprite::createWithSpriteFrameName("square02_001.png");

    if (!bg) {
         bg = cocos2d::extension::CCScale9Sprite::createWithSpriteFrameName("square02b_001.png");
    }

    if (bg) {
        bg->setContentSize({74.f, 16.f});
        bg->setColor({0, 0, 0});
        bg->setOpacity(125);
        bg->setPosition({0.f, 0.f});
        ratingContainer->addChild(bg, -1);
    } else {
         auto fallback = CCLayerColor::create({0, 0, 0, 125});
         fallback->setContentSize({74.f, 16.f});
         fallback->setPosition({-37.f, -8.f});
         ratingContainer->addChild(fallback, -1);
    }

    auto starSpr = CCSprite::createWithSpriteFrameName("GJ_starsIcon_001.png");
    if (!starSpr) starSpr = CCSprite::createWithSpriteFrameName("star_small01_001.png");
    starSpr->setScale(0.34f);
    starSpr->setPosition({-20.f, 0.f});
    ratingContainer->addChild(starSpr);

    m_ratingLabel = CCLabelBMFont::create("...", "goldFont.fnt");
    m_ratingLabel->setScale(0.28f);
    m_ratingLabel->setPosition({8.f, 3.f});
    ratingContainer->addChild(m_ratingLabel);

    std::string username = "Unknown";
    if (auto gm = GameManager::get()) username = gm->m_playerName;

    std::string thumbnailId = "";
    if (m_currentIndex >= 0 && m_currentIndex < static_cast<int>(m_thumbnails.size())) {
        thumbnailId = m_thumbnails[m_currentIndex].id;
    }

    WeakRef<LocalThumbnailViewPopup> self = this;
    ThumbnailAPI::get().getRating(m_levelID, username, thumbnailId, [self](bool success, float average, int count, int userVote) {
        auto popup = self.lock();
        if (!popup) return;

        if (success) {
            log::info("[ThumbnailViewPopup] Rating found for level {}: {:.1f} ({})", popup->m_levelID, average, count);
            if (popup->m_ratingLabel) {
                popup->m_ratingLabel->setString(fmt::format("{:.1f} ({})", average, count).c_str());
                if (count == 0) {
                    popup->m_ratingLabel->setColor({255, 100, 100});
                } else {
                    popup->m_ratingLabel->setColor({255, 255, 255});
                }
            }
            popup->m_userVote = userVote;
            popup->m_initialUserVote = userVote;
        } else {
            log::warn("[ThumbnailViewPopup] Failed to get rating for level {}", popup->m_levelID);
        }
    });
}

void LocalThumbnailViewPopup::onRate(CCObject* sender) {
    std::string thumbnailId = "";
    if (m_currentIndex >= 0 && m_currentIndex < static_cast<int>(m_thumbnails.size())) {
        thumbnailId = m_thumbnails[m_currentIndex].id;
    }
    auto popup = RatePopup::create(m_levelID, thumbnailId);
    WeakRef<LocalThumbnailViewPopup> self = this;
    popup->m_onRateCallback = [self]() {
        auto view = self.lock();
        if (!view) return;
        view->setupRating();
    };
    popup->show();
}

// Init / Setup

bool LocalThumbnailViewPopup::init(float width, float height) {
     return Popup::init(width, height);
}

void LocalThumbnailViewPopup::setup(std::pair<int32_t, bool> const& data) {
    m_levelID = data.first;
    m_canAcceptUpload = data.second;

    this->setTitle("");

    bool openedFromReport = false;
    int verificationCategory = -1;
    openedFromReport = Mod::get()->getSavedValue<bool>("from-report-popup", false);
    verificationCategory = Mod::get()->getSavedValue<int>("verification-category", -1);
    if (openedFromReport) Mod::get()->setSavedValue("from-report-popup", false);
    if (verificationCategory >= 0) Mod::get()->setSavedValue("verification-category", -1);

    if (m_bgSprite) {
        m_bgSprite->setVisible(false);
    }

    auto content = this->m_mainLayer->getContentSize();

    float maxWidth = content.width - 40.f;
    float maxHeight = content.height - 80.f;

    if (m_closeBtn) {
         float topY = (content.height / 2 + 5.f) + (maxHeight / 2);
         float leftX = (content.width - maxWidth) / 2;
         m_closeBtn->setPosition({leftX - 3.f, topY + 3.f});

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

    auto stencil = CCScale9Sprite::create("square02_001.png");
    if (!stencil) {
        stencil = CCScale9Sprite::create();
    }
    stencil->setContentSize({maxWidth, maxHeight});
    stencil->setPosition({maxWidth / 2, maxHeight / 2});

    auto clip = CCClippingNode::create(stencil);
    clip->setAlphaThreshold(0.1f);
    m_clippingNode = clip;

    m_clippingNode->setContentSize({maxWidth, maxHeight});
    m_clippingNode->setAnchorPoint({0.5f, 0.5f});
    m_clippingNode->setPosition({content.width / 2, content.height / 2 + 5.f});
    this->m_mainLayer->addChild(m_clippingNode, 1);

    auto clippingBg = CCLayerColor::create({0, 0, 0, 255});
    clippingBg->setOpacity(25);
    clippingBg->setContentSize({maxWidth, maxHeight});
    clippingBg->ignoreAnchorPointForPosition(false);
    clippingBg->setAnchorPoint({0.5f, 0.5f});
    clippingBg->setPosition({maxWidth / 2, maxHeight / 2});

    if (m_clippingNode) {
        m_clippingNode->addChild(clippingBg, -1);
    }

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

    this->setTouchEnabled(true);

#if defined(GEODE_IS_WINDOWS)
    this->setMouseEnabled(true);
    this->setKeypadEnabled(true);
#endif

    log::info("[ThumbnailViewPopup] === INICIANDO CARGA DE THUMBNAIL ===");
    log::info("[ThumbnailViewPopup] Level ID: {}", m_levelID);
    log::info("[ThumbnailViewPopup] Verification Category: {}", verificationCategory);
    this->retain();
    m_verificationCategory = verificationCategory;

    if (verificationCategory >= 0) {
        this->loadFromVerificationQueue(static_cast<PendingCategory>(verificationCategory), maxWidth, maxHeight, content, openedFromReport);
        // balancea el retain de setup para esta rama
        this->release();
    } else {
        this->tryLoadFromMultipleSources(maxWidth, maxHeight, content, openedFromReport);

        WeakRef<LocalThumbnailViewPopup> self = this;
        ThumbnailAPI::get().getThumbnailInfo(m_levelID, [self](bool success, std::string const& response) {
            auto popup = self.lock();
            if (!popup) return;

            if (success) {
                std::vector<ThumbnailAPI::ThumbnailInfo> thumbs;
                auto res = matjson::parse(response);
                if (res.isOk()) {
                        auto json = res.unwrap();
                        ThumbnailAPI::ThumbnailInfo info;
                        bool found = false;

                        if (json.contains("url")) {
                            info.url = json["url"].asString().unwrapOr("");
                            found = true;
                        } else {
                            info.url = ThumbnailAPI::get().getThumbnailURL(popup->m_levelID);
                        }

                        if (json.contains("version") && json["version"].isObject()) {
                            auto verObj = json["version"];
                            if (verObj.contains("version")) info.id = verObj["version"].asString().unwrapOr("");
                            if (verObj.contains("format")) info.format = verObj["format"].asString().unwrapOr("png");
                        }

                        if (json.contains("metadata") && json["metadata"].isObject()) {
                            auto metaObj = json["metadata"];
                            if (metaObj.contains("uploadedBy")) info.creator = metaObj["uploadedBy"].asString().unwrapOr("Unknown");
                            if (metaObj.contains("uploadedAt")) info.date = metaObj["uploadedAt"].asString().unwrapOr("Unknown");
                        }

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

                if (!thumbs.empty()) {
                    popup->m_thumbnails = thumbs;
                    if (popup->m_thumbnails.size() > 1) {
                        if (popup->m_leftArrow) popup->m_leftArrow->setVisible(true);
                        if (popup->m_rightArrow) popup->m_rightArrow->setVisible(true);
                    }
                    popup->setupRating();
                }
            }
        });
    }

    setupRating();
}


// Carga desde multiples fuentes

void LocalThumbnailViewPopup::loadFromVerificationQueue(PendingCategory category, float maxWidth, float maxHeight, CCSize content, bool openedFromReport) {
    log::info("[ThumbnailViewPopup] Cargando desde cola de verificacion - Categoria: {}", static_cast<int>(category));

    this->retain();
    LocalThumbnailViewPopup* popupPtr = this;

    if (category == PendingCategory::Verify) {
        ThumbnailAPI::get().downloadSuggestion(m_levelID, [popupPtr, maxWidth, maxHeight, content, openedFromReport](bool success, CCTexture2D* tex) {
            if (!popupPtr->getParent() || !popupPtr->m_mainLayer) {
                log::warn("[ThumbnailViewPopup] Popup destruido antes de cargar suggestion");
                popupPtr->release();
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
        ThumbnailAPI::get().downloadUpdate(m_levelID, [popupPtr, maxWidth, maxHeight, content, openedFromReport](bool success, CCTexture2D* tex) {
            if (!popupPtr->getParent() || !popupPtr->m_mainLayer) {
                log::warn("[ThumbnailViewPopup] Popup destruido antes de cargar update");
                popupPtr->release();
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
        ThumbnailAPI::get().downloadReported(m_levelID, [popupPtr, maxWidth, maxHeight, content, openedFromReport](bool success, CCTexture2D* tex) {
            if (!popupPtr->getParent() || !popupPtr->m_mainLayer) {
                log::warn("[ThumbnailViewPopup] Popup destruido antes de cargar reported");
                popupPtr->release();
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
        log::error("[ThumbnailViewPopup] Categoria de verificacion desconocida: {}", static_cast<int>(category));
        this->showNoThumbnail(content);
        this->release();
    }
}

void LocalThumbnailViewPopup::tryLoadFromMultipleSources(float maxWidth, float maxHeight, CCSize content, bool openedFromReport) {
    if (LocalThumbs::get().has(m_levelID)) {
        log::info("[ThumbnailViewPopup] ✓ Fuente 1: LocalThumbs ENCONTRADO");
        auto tex = LocalThumbs::get().loadTexture(m_levelID);
        if (tex) {
            log::info("[ThumbnailViewPopup] ✓ Textura cargada desde LocalThumbs");
            this->displayThumbnail(tex, maxWidth, maxHeight, content, openedFromReport);
            this->release();
            return;
        }
        log::warn("[ThumbnailViewPopup] ✗ LocalThumbs fallo al cargar textura");
    } else {
        log::info("[ThumbnailViewPopup] ✗ Fuente 1: LocalThumbs - NO disponible");
    }

    if (tryLoadFromCache(maxWidth, maxHeight, content, openedFromReport)) {
        return;
    }

    loadFromThumbnailLoader(maxWidth, maxHeight, content, openedFromReport);
    // loadFromThumbnailLoader hace su propio retain, liberamos el de setup/caller
    this->release();
}

bool LocalThumbnailViewPopup::tryLoadFromCache(float maxWidth, float maxHeight, CCSize content, bool openedFromReport) {
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

void LocalThumbnailViewPopup::loadFromThumbnailLoader(float maxWidth, float maxHeight, CCSize content, bool openedFromReport) {
    log::info("[ThumbnailViewPopup] Intentando Fuente 3: ThumbnailLoader + Descarga");
    std::string fileName = fmt::format("{}.png", m_levelID);

    LocalThumbnailViewPopup* popupPtr = this;
    this->retain();

    ThumbnailLoader::get().requestLoad(m_levelID, fileName, [popupPtr, maxWidth, maxHeight, content, openedFromReport](CCTexture2D* tex, bool) {
        log::info("[ThumbnailViewPopup] === CALLBACK THUMBNAILLOADER ===");

        if (!popupPtr->getParent() || !popupPtr->m_mainLayer) {
            log::warn("[ThumbnailViewPopup] Popup ya no tiene parent o mainLayer valido");
            popupPtr->release();
            return;
        }

        if (tex) {
            log::info("[ThumbnailViewPopup] ✓ Textura recibida ({}x{})",
                tex->getPixelsWide(), tex->getPixelsHigh());
            popupPtr->displayThumbnail(tex, maxWidth, maxHeight, content, openedFromReport);
            popupPtr->release();
        } else {
            log::warn("[ThumbnailViewPopup] ✗ ThumbnailLoader fallo, intentando descarga directa del servidor");
            popupPtr->tryDirectServerDownload(maxWidth, maxHeight, content, openedFromReport);
            // tryDirectServerDownload hace su propio retain/release, liberamos el de loadFromThumbnailLoader
            popupPtr->release();
        }
    }, 10);
}

void LocalThumbnailViewPopup::tryDirectServerDownload(float maxWidth, float maxHeight, CCSize content, bool openedFromReport) {
    log::info("[ThumbnailViewPopup] Intentando Fuente 3: Descarga directa del servidor");
    this->retain();

    LocalThumbnailViewPopup* popupPtr = this;

    HttpClient::get().downloadThumbnail(m_levelID, [popupPtr, maxWidth, maxHeight, content, openedFromReport](bool success, std::vector<uint8_t> const& data, int w, int h) {
        if (!popupPtr->getParent() || !popupPtr->m_mainLayer) {
            log::warn("[ThumbnailViewPopup] Popup ya no tiene parent valido (descarga servidor)");
            popupPtr->release();
            return;
        }

        if (success && !data.empty()) {
            log::info("[ThumbnailViewPopup] ✓ Datos descargados del servidor ({} bytes)", data.size());

            auto image = new CCImage();
            if (image->initWithImageData(const_cast<uint8_t*>(data.data()), data.size())) {
                auto tex = new CCTexture2D();
                if (tex->initWithImage(image)) {
                    log::info("[ThumbnailViewPopup] ✓ Textura creada desde servidor ({}x{})",
                        tex->getPixelsWide(), tex->getPixelsHigh());
                    popupPtr->displayThumbnail(tex, maxWidth, maxHeight, content, openedFromReport);
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
            log::warn("[ThumbnailViewPopup] ✗ Descarga del servidor fallo");
        }

        log::info("[ThumbnailViewPopup] === TODAS LAS FUENTES FALLARON ===");
        popupPtr->showNoThumbnail(content);
        popupPtr->release();
    });
}


// Display / UI

void LocalThumbnailViewPopup::displayThumbnail(CCTexture2D* tex, float maxWidth, float maxHeight, CCSize content, bool openedFromReport) {
    log::info("[ThumbnailViewPopup] === MOSTRANDO THUMBNAIL ===");
    log::info("[ThumbnailViewPopup] Textura: {}x{}", tex->getPixelsWide(), tex->getPixelsHigh());

    if (!this->getParent() || !m_mainLayer) {
        log::error("[ThumbnailViewPopup] Popup destruido antes de displayThumbnail!");
        return;
    }

    if (m_thumbnailSprite) {
        m_thumbnailSprite->removeFromParent();
        m_thumbnailSprite = nullptr;
    }

    if (!m_mainLayer) {
        log::error("[ThumbnailViewPopup] m_mainLayer es null!");
        return;
    }

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
    if (m_settingsMenu) {
        m_settingsMenu->removeFromParent();
        m_settingsMenu = nullptr;
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
    tex->retain();

    CCSprite* sprite = nullptr;

    sprite = CCSprite::createWithTexture(tex);

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
                     float scale = std::max(scaleX, scaleY);
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

    m_viewWidth = maxWidth;
    m_viewHeight = maxHeight;

    float scaleX = maxWidth / sprite->getContentWidth();
    float scaleY = maxHeight / sprite->getContentHeight();
    float scale = std::max(scaleX, scaleY);

    sprite->setScale(scale);
    m_initialScale = scale;
    m_minScale = scale;
    m_maxScale = std::max(4.0f, scale * 6.0f);

    float centerX = content.width * 0.5f;
    float centerY = content.height * 0.5f + 5.f;
    sprite->setPosition({centerX, centerY});
    sprite->setID("thumbnail"_spr);

    ccTexParams params{GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE};
    tex->setTexParameters(&params);

    if (m_clippingNode) {
        m_clippingNode->addChild(sprite, 10);
        sprite->setPosition({maxWidth / 2, maxHeight / 2});
    } else {
        this->m_mainLayer->addChild(sprite, 10);
        sprite->setPosition({centerX, centerY});
    }
    m_thumbnailSprite = sprite;

    sprite->setVisible(true);
    sprite->setOpacity(255);

    log::info("[ThumbnailViewPopup] ✓ Thumbnail agregado a mainLayer");
    log::info("[ThumbnailViewPopup] Posicion: ({},{}), Scale: {}, Tamano final: {}x{}",
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

        m_leftArrow->setVisible(m_suggestions.size() > 1);
        m_rightArrow->setVisible(m_suggestions.size() > 1);
    }

    // menu botones
    m_buttonMenu = CCMenu::create();
    auto buttonMenu = m_buttonMenu;

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

    CCMenuItemSpriteExtra* centerBtn = nullptr;

    if (m_verificationCategory >= 0 && m_verificationCategory != 2) {
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

    CCMenuItemSpriteExtra* acceptBtn = nullptr;
    if (m_canAcceptUpload && m_verificationCategory < 0) {
        auto acceptSpr = ButtonSprite::create(Localization::get().getString("level.accept_button").c_str(), 80, true, "bigFont.fnt", "GJ_button_01.png", 30.f, 0.5f);
        acceptSpr->setScale(0.6f);
        acceptBtn = CCMenuItemSpriteExtra::create(acceptSpr, this, menu_selector(LocalThumbnailViewPopup::onAcceptThumbBtn));
    }

    if (acceptBtn) buttonMenu->addChild(acceptBtn);
    if (centerBtn) buttonMenu->addChild(centerBtn);

    auto rateSpr = CCSprite::createWithSpriteFrameName("GJ_starBtn_001.png");
    rateSpr->setScale(0.7f);
    auto rateBtn = CCMenuItemSpriteExtra::create(rateSpr, this, menu_selector(LocalThumbnailViewPopup::onRate));
    buttonMenu->addChild(rateBtn);

    buttonMenu->addChild(downloadBtn);

    // btn eliminar (mods)
    auto gm = GameManager::get();
    if (gm) {
        auto username = gm->m_playerName;
        int accountID = 0;
        if (auto* am = GJAccountManager::get()) accountID = am->m_accountID;

        WeakRef<LocalThumbnailViewPopup> self = this;
        ThumbnailAPI::get().checkModeratorAccount(username, accountID, [self](bool isMod, bool isAdmin) {
            auto popup = self.lock();
            if (!popup) return;

            if (isMod || isAdmin) {
                auto spr = CCSprite::createWithSpriteFrameName("GJ_deleteBtn_001.png");
                if (!spr) spr = CCSprite::createWithSpriteFrameName("GJ_trashBtn_001.png");

                if (spr) {
                    spr->setScale(0.6f);
                    auto btn = CCMenuItemSpriteExtra::create(
                        spr,
                        popup,
                        menu_selector(LocalThumbnailViewPopup::onDeleteThumbnail)
                    );

                    if (popup->m_buttonMenu) {
                        popup->m_buttonMenu->addChild(btn);
                        popup->m_buttonMenu->updateLayout();
                    }
                }
            }
        });
    }

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

    // btn engranaje settings
    m_settingsMenu = CCMenu::create();
    auto settingsMenu = m_settingsMenu;
    settingsMenu->setPosition({0, 0});
    this->m_mainLayer->addChild(settingsMenu, 15);

    auto gearSpr = CCSprite::createWithSpriteFrameName("GJ_optionsBtn_001.png");
    if (!gearSpr) gearSpr = CCSprite::createWithSpriteFrameName("GJ_optionsBtn02_001.png");
    if (gearSpr) {
        gearSpr->setScale(0.45f);
        auto gearBtn = CCMenuItemSpriteExtra::create(gearSpr, this, menu_selector(LocalThumbnailViewPopup::onSettings));
        gearBtn->setPosition({content.width - 22.f, 46.f});
        gearBtn->setID("settings-gear-btn"_spr);
        settingsMenu->addChild(gearBtn);
    }
}

void LocalThumbnailViewPopup::showNoThumbnail(CCSize content) {
    float centerX = content.width * 0.5f;
    float centerY = content.height * 0.5f + 10.f;
    float bgWidth = content.width - 60.f;
    float bgHeight = content.height - 80.f;

    auto bg = CCLayerColor::create({0, 0, 0, 200});
    bg->setContentSize({bgWidth, bgHeight});
    bg->setPosition({centerX - bgWidth / 2, centerY - bgHeight / 2});
    this->m_mainLayer->addChild(bg);

    UIBorderHelper::createBorder(centerX, centerY, bgWidth, bgHeight, this->m_mainLayer);

    auto sadLabel = CCLabelBMFont::create(":(", "bigFont.fnt");
    sadLabel->setScale(3.0f);
    sadLabel->setOpacity(100);
    sadLabel->setPosition({centerX, centerY + 20.f});
    this->m_mainLayer->addChild(sadLabel, 2);

    auto noThumbLabel = CCLabelBMFont::create(Localization::get().getString("level.no_thumbnail_text").c_str(), "goldFont.fnt");
    noThumbLabel->setScale(0.6f);
    noThumbLabel->setOpacity(150);
    noThumbLabel->setPosition({centerX, centerY - 20.f});
    this->m_mainLayer->addChild(noThumbLabel, 2);
}

// Acciones de botones


void LocalThumbnailViewPopup::onDownloadBtn(CCObject*) {
    if (m_isDownloading) return;
    m_isDownloading = true;

    Ref<LocalThumbnailViewPopup> safeRef = this;
    int levelID = m_levelID;

    auto doSave = [safeRef, levelID](std::string savePath) {
        log::debug("Save path chosen: {}", savePath);

        auto pathStr = LocalThumbs::get().getThumbPath(levelID);
        bool fromCache = false;
        if (!pathStr) {
            auto cachePath = ThumbnailLoader::get().getCachePath(levelID, ThumbnailLoader::get().hasGIFData(levelID));
            if (std::filesystem::exists(cachePath)) {
                pathStr = geode::utils::string::pathToString(cachePath);
                fromCache = true;
            }
        }
        if (!pathStr) {
            log::error("Thumbnail path not found");
            PaimonNotify::create(Localization::get().getString("level.no_thumbnail").c_str(), NotificationIcon::Error)->show();
            safeRef->m_isDownloading = false;
            return;
        }

        auto srcPath = *pathStr;
        std::thread([safeRef, srcPath, savePath, fromCache]() {
                bool ok = false;
                if (fromCache) {
                    std::error_code ec;
                    std::filesystem::copy(srcPath, savePath, std::filesystem::copy_options::overwrite_existing, ec);
                    ok = !ec;
                } else {
                    std::vector<uint8_t> rgbData;
                    uint32_t width = 0, height = 0;
                    bool loaded = ImageConverter::loadRgbFile(srcPath, rgbData, width, height);
                    if (loaded) {
                        auto rgba = ImageConverter::rgbToRgba(rgbData, width, height);
                        Loader::get()->queueInMainThread([safeRef, rgba = std::move(rgba), width, height, savePath]() {
                            bool imgOk = false;
                            auto img = new CCImage();
                            if (img->initWithImageData(const_cast<uint8_t*>(rgba.data()), rgba.size(), CCImage::kFmtRawData, width, height)) {
                                imgOk = img->saveToFile(savePath.c_str(), false);
                            }
                            img->release();
                            safeRef->m_isDownloading = false;
                            if (imgOk) {
                                log::info("Image saved successfully to {}", savePath);
                                PaimonNotify::create(Localization::get().getString("level.saved").c_str(), NotificationIcon::Success)->show();
                            } else {
                                log::error("Failed to save image to {}", savePath);
                                PaimonNotify::create(Localization::get().getString("level.save_error").c_str(), NotificationIcon::Error)->show();
                            }
                        });
                        return;
                    }
                }
                Loader::get()->queueInMainThread([safeRef, ok, savePath]() {
                    safeRef->m_isDownloading = false;
                    if (ok) {
                        log::info("Image saved successfully to {}", savePath);
                        PaimonNotify::create(Localization::get().getString("level.saved").c_str(), NotificationIcon::Success)->show();
                    } else {
                        log::error("Failed to save image to {}", savePath);
                        PaimonNotify::create(Localization::get().getString("level.save_error").c_str(), NotificationIcon::Error)->show();
                    }
                });
        }).detach();
    };

    pt::saveImageFileDialog("miniatura.png", [safeRef, levelID, doSave](std::optional<std::filesystem::path> result) {
        if (result.has_value() && !result.value().empty()) {
            doSave(geode::utils::string::pathToString(*result));
        } else {
            // No file chosen (cancelled or unsupported save dialog) - save to mod folder
            auto saveDir = Mod::get()->getSaveDir() / "saved_thumbnails";
            std::error_code ec;
            if (!std::filesystem::exists(saveDir, ec)) {
                std::filesystem::create_directories(saveDir, ec);
            }
            auto savePath = geode::utils::string::pathToString(saveDir / fmt::format("thumb_{}.png", levelID));
            PaimonNotify::create(Localization::get().getString("level.saving_mod_folder").c_str(), NotificationIcon::Info)->show();
            doSave(savePath);
        }
    });
}

void LocalThumbnailViewPopup::onDeleteReportedThumb(CCObject*) {
    log::info("[ThumbnailViewPopup] Borrar miniatura reportada para levelID={}", m_levelID);

    int levelID = m_levelID;

    std::string username;
    int accountID = 0;
    auto* gm = GameManager::get();
    auto* am = GJAccountManager::get();
    if (gm) {
        username = gm->m_playerName;
        accountID = am ? am->m_accountID : 0;
    } else {
        log::warn("[ThumbnailViewPopup] GameManager::get() es null");
        username = "Unknown";
    }

    auto spinner = geode::LoadingSpinner::create(30.f);
    spinner->setPosition(m_mainLayer->getContentSize() / 2);
    spinner->setID("paimon-loading-spinner"_spr);
    m_mainLayer->addChild(spinner, 100);
    Ref<geode::LoadingSpinner> loading = spinner;

    if (accountID <= 0) {
        if (loading) loading->removeFromParent();
        PaimonNotify::create(Localization::get().getString("level.account_required").c_str(), NotificationIcon::Error)->show();
        return;
    }

    ThumbnailAPI::get().checkModeratorAccount(username, accountID, [levelID, username, accountID, loading](bool isMod, bool isAdmin) {
        if (!isMod && !isAdmin) {
            if (loading) loading->removeFromParent();
            PaimonNotify::create(Localization::get().getString("level.delete_moderator_only").c_str(), NotificationIcon::Error)->show();
            return;
        }

        ThumbnailAPI::get().deleteThumbnail(levelID, username, accountID, [levelID, loading](bool success, std::string const& msg) {
            if (loading) loading->removeFromParent();

            if (success) {
                PendingQueue::get().accept(levelID, PendingCategory::Report);
                PaimonNotify::create(Localization::get().getString("level.deleted_server").c_str(), NotificationIcon::Success)->show();
                log::info("[ThumbnailViewPopup] Miniatura {} eliminada del servidor", levelID);
            } else {
                PaimonNotify::create(Localization::get().getString("level.delete_error") + msg, NotificationIcon::Error)->show();
                log::error("[ThumbnailViewPopup] Error al borrar miniatura: {}", msg);
            }
        });
    });
}

void LocalThumbnailViewPopup::onAcceptThumbBtn(CCObject*) {
    log::info("Aceptar thumbnail presionado en ThumbnailViewPopup para levelID={}", m_levelID);

    if (m_verificationCategory >= 0) {
        log::info("Aceptando thumbnail desde cola de verificacion (categoria: {})", m_verificationCategory);

        std::string username;
        auto* gm = GameManager::get();
        if (gm) {
            username = gm->m_playerName;
        } else {
            log::warn("[ThumbnailViewPopup] GameManager::get() es null");
        }

        PaimonNotify::create(Localization::get().getString("level.accepting").c_str(), NotificationIcon::Info)->show();

        std::string targetFilename = "";
        if (!m_suggestions.empty() && m_currentIndex >= 0 && m_currentIndex < static_cast<int>(m_suggestions.size())) {
            targetFilename = m_suggestions[m_currentIndex].filename;
        }

        ThumbnailAPI::get().acceptQueueItem(
            m_levelID,
            static_cast<PendingCategory>(m_verificationCategory),
            username,
            [levelID = m_levelID, category = m_verificationCategory](bool success, std::string const& message) {
                if (success) {
                    PendingQueue::get().accept(levelID, static_cast<PendingCategory>(category));
                    PaimonNotify::create(Localization::get().getString("level.accepted").c_str(), NotificationIcon::Success)->show();
                    log::info("[ThumbnailViewPopup] Miniatura aceptada para nivel {}", levelID);
                } else {
                    PaimonNotify::create(Localization::get().getString("level.accept_error") + message, NotificationIcon::Error)->show();
                    log::error("[ThumbnailViewPopup] Error aceptando miniatura: {}", message);
                }
            },
            targetFilename
        );

        return;
    }

    log::info("Intentando aceptar desde LocalThumbs");

    auto pathOpt = LocalThumbs::get().getThumbPath(m_levelID);
    if (!pathOpt) {
        PaimonNotify::create(Localization::get().getString("level.no_local_thumb").c_str(), NotificationIcon::Error)->show();
        return;
    }

    std::vector<uint8_t> pngData;
    if (!ImageConverter::loadRgbFileToPng(*pathOpt, pngData)) {
        PaimonNotify::create(Localization::get().getString("level.png_error").c_str(), NotificationIcon::Error)->show();
        return;
    }

    size_t base64Size = ((pngData.size() + 2) / 3) * 4;
    log::info("PNG size: {} bytes ({:.2f} KB), Base64 size: ~{} bytes ({:.2f} KB)",
             pngData.size(), pngData.size() / 1024.0, base64Size, base64Size / 1024.0);

    log::warn("[ThumbnailViewPopup] Server upload disabled - thumbnail saved locally only");
    PaimonNotify::create(Localization::get().getString("level.saved_local_server_disabled").c_str(), NotificationIcon::Info)->show();
}

void LocalThumbnailViewPopup::onReportBtn(CCObject*) {
    int levelID = m_levelID;

    auto popup = ReportInputPopup::create(levelID, [levelID](std::string reason) {
        std::string user;
        auto* gm = GameManager::get();
        if (gm) {
            user = gm->m_playerName;
        }

        ThumbnailAPI::get().submitReport(levelID, user, reason, [levelID, reason](bool success, std::string const& message) {
            if (success) {
                PaimonNotify::create(Localization::get().getString("report.sent_synced") + reason, NotificationIcon::Warning)->show();
                log::info("[ThumbnailViewPopup] Reporte confirmado y enviado al servidor para nivel {}", levelID);
            } else {
                PaimonNotify::create(Localization::get().getString("report.saved_local").c_str(), NotificationIcon::Info)->show();
                log::warn("[ThumbnailViewPopup] Reporte guardado solo localmente para nivel {}", levelID);
            }
        });
    });

    if (popup) {
        popup->show();
    }
}

void LocalThumbnailViewPopup::onDeleteThumbnail(CCObject*) {
    int levelID = m_levelID;
    auto gm = GameManager::get();
    auto am = GJAccountManager::get();
    std::string username = gm ? gm->m_playerName : "";
    int accountID = am ? am->m_accountID : 0;

    std::string thumbnailId = "";
    if (m_currentIndex >= 0 && m_currentIndex < static_cast<int>(m_thumbnails.size())) {
        thumbnailId = m_thumbnails[m_currentIndex].id;
    }

    WeakRef<LocalThumbnailViewPopup> self = this;
    ThumbnailAPI::get().getRating(levelID, username, thumbnailId, [self, levelID, username, accountID](bool /*success*/, float /*avg*/, int count, int /*userVote*/) {
        auto popup = self.lock();
        if (!popup) return;

        ThumbnailAPI::get().checkModeratorAccount(username, accountID, [self, levelID, username, accountID, count](bool isMod, bool isAdmin) {
            auto popup = self.lock();
            if (!popup) return;

            if (!isMod && !isAdmin) {
                 PaimonNotify::create(Localization::get().getString("level.no_permissions"), NotificationIcon::Error)->show();
                 return;
            }

            if (count > 100 && !isAdmin) {
                PaimonNotify::create(Localization::get().getString("level.admin_only_high_votes"), NotificationIcon::Error)->show();
                return;
            }

            geode::createQuickPopup(
                Localization::get().getString("level.confirm_delete_title").c_str(),
                Localization::get().getString("level.confirm_delete_msg").c_str(),
                Localization::get().getString("general.cancel").c_str(), Localization::get().getString("level.delete_button").c_str(),
                [self, levelID, username, accountID](auto, bool btn2) {
                    auto popup = self.lock();
                    if (!popup) return;

                    if (btn2) {
                        auto spinner2 = geode::LoadingSpinner::create(30.f);
                        spinner2->setPosition(popup->m_mainLayer->getContentSize() / 2);
                        spinner2->setID("paimon-loading-spinner"_spr);
                        popup->m_mainLayer->addChild(spinner2, 100);
                        Ref<geode::LoadingSpinner> loading = spinner2;

                        ThumbnailAPI::get().deleteThumbnail(levelID, username, accountID, [self, loading](bool success, std::string msg) {
                            if (loading) loading->removeFromParent();

                            auto popup = self.lock();
                            if (!popup) return;

                            if (success) {
                                PaimonNotify::create(Localization::get().getString("level.thumbnail_deleted"), NotificationIcon::Success)->show();
                                popup->onClose(nullptr);
                            } else {
                                PaimonNotify::create(msg.c_str(), NotificationIcon::Error)->show();
                            }
                        });
                    }
                }
            );
        });
    });
}

// ====================================================================
// Recentrar, Clamp, Touch
// ====================================================================

void LocalThumbnailViewPopup::onRecenter(CCObject*) {
    if (!m_thumbnailSprite) return;

    m_thumbnailSprite->stopAllActions();

    auto content = this->m_mainLayer->getContentSize();
    float centerX = content.width * 0.5f;
    float centerY = content.height * 0.5f + 10.f;

    auto moveTo = CCMoveTo::create(0.3f, {centerX, centerY});
    auto scaleTo = CCScaleTo::create(0.3f, m_initialScale);
    auto easeMove = CCEaseSineOut::create(moveTo);
    auto easeScale = CCEaseSineOut::create(scaleTo);

    m_thumbnailSprite->runAction(easeMove);
    m_thumbnailSprite->runAction(easeScale);
    m_thumbnailSprite->setAnchorPoint({0.5f, 0.5f});
}

float LocalThumbnailViewPopup::clamp(float value, float min, float max) {
    return std::max(min, std::min(value, max));
}

void LocalThumbnailViewPopup::clampSpritePosition() {
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

    if (spriteW <= m_viewWidth) {
        newX = m_viewWidth / 2;
    } else {
        if (spriteLeft > 0) {
            newX = spriteW * anchor.x;
        }
        if (spriteRight < m_viewWidth) {
            newX = m_viewWidth - spriteW * (1.0f - anchor.x);
        }
    }

    if (spriteH <= m_viewHeight) {
        newY = m_viewHeight / 2;
    } else {
        if (spriteBottom > 0) {
            newY = spriteH * anchor.y;
        }
        if (spriteTop < m_viewHeight) {
            newY = m_viewHeight - spriteH * (1.0f - anchor.y);
        }
    }

    m_thumbnailSprite->setPosition({newX, newY});
}

void LocalThumbnailViewPopup::clampSpritePositionAnimated() {
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

bool LocalThumbnailViewPopup::ccTouchBegan(CCTouch* touch, CCEvent* event) {
    if (!this->isVisible()) return false;

    auto touchPos = touch->getLocation();
    auto nodePos = m_mainLayer->convertToNodeSpace(touchPos);
    auto size = m_mainLayer->getContentSize();
    CCRect rect = {0, 0, size.width, size.height};

    if (!rect.containsPoint(nodePos)) {
        return false;
    }

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
    if (isTouchOnMenu(m_settingsMenu, touch)) return false;

    if (m_touches.size() == 1) {
        auto firstTouch = *m_touches.begin();
        if (firstTouch == touch) return true;

        auto firstLoc = firstTouch->getLocation();
        auto secondLoc = touch->getLocation();

        m_touchMidPoint = (firstLoc + secondLoc) / 2.0f;
        m_savedScale = m_thumbnailSprite ? m_thumbnailSprite->getScale() : m_initialScale;
        m_initialDistance = firstLoc.getDistance(secondLoc);

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

void LocalThumbnailViewPopup::ccTouchMoved(CCTouch* touch, CCEvent* event) {
    if (!m_thumbnailSprite) return;

    if (m_touches.size() == 1) {
        auto delta = touch->getDelta();
        m_thumbnailSprite->setPosition({
            m_thumbnailSprite->getPositionX() + delta.x,
            m_thumbnailSprite->getPositionY() + delta.y
        });
        clampSpritePosition();
    } else if (m_touches.size() == 2) {
        m_wasZooming = true;

        auto it = m_touches.begin();
        auto firstTouch = *it;
        ++it;
        auto secondTouch = *it;

        auto firstLoc = firstTouch->getLocation();
        auto secondLoc = secondTouch->getLocation();
        auto center = (firstLoc + secondLoc) / 2.0f;
        auto distNow = firstLoc.getDistance(secondLoc);

        if (m_initialDistance < 0.1f) m_initialDistance = 0.1f;
        if (distNow < 0.1f) distNow = 0.1f;

        auto mult = m_initialDistance / distNow;
        if (mult < 0.0001f) mult = 0.0001f;

        auto zoom = clamp(m_savedScale / mult, m_minScale, m_maxScale);
        m_thumbnailSprite->setScale(zoom);

        auto centerDiff = m_touchMidPoint - center;
        m_thumbnailSprite->setPosition(m_thumbnailSprite->getPosition() - centerDiff);
        m_touchMidPoint = center;

        clampSpritePosition();
    }
}

void LocalThumbnailViewPopup::ccTouchEnded(CCTouch* touch, CCEvent* event) {
    m_touches.erase(touch);

    if (!m_thumbnailSprite) return;

    if (m_wasZooming && m_touches.size() == 1) {
        auto scale = m_thumbnailSprite->getScale();

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

    if (m_touches.empty()) {
        clampSpritePositionAnimated();
    }
}

void LocalThumbnailViewPopup::scrollWheel(float x, float y) {
    if (!m_mainLayer || !m_thumbnailSprite) {
        return;
    }

    if (!m_thumbnailSprite->getParent()) {
        m_thumbnailSprite = nullptr;
        return;
    }

    float scrollAmount = y;
    if (std::abs(y) < 0.001f) {
        scrollAmount = -x;
    }

    float zoomFactor = scrollAmount > 0 ? 1.12f : 0.89f;

    float currentScale = m_thumbnailSprite->getScale();
    float newScale = currentScale * zoomFactor;

    newScale = clamp(newScale, m_minScale, m_maxScale);

    if (std::abs(newScale - currentScale) < 0.001f) {
        return;
    }

    m_thumbnailSprite->setScale(newScale);

    clampSpritePosition();
}

void LocalThumbnailViewPopup::ccTouchCancelled(CCTouch* touch, CCEvent* event) {
    m_touches.erase(touch);
    m_wasZooming = false;
}

// ====================================================================
// Factory
// ====================================================================

LocalThumbnailViewPopup* LocalThumbnailViewPopup::create(int32_t levelID, bool canAcceptUpload) {
    auto ret = new LocalThumbnailViewPopup();
    if (ret && ret->init(400.f, 280.f)) {
        ret->setup({levelID, canAcceptUpload});
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

CCNode* createThumbnailViewPopup(int32_t levelID, bool canAcceptUpload, std::vector<Suggestion> const& suggestions) {
    auto ret = LocalThumbnailViewPopup::create(levelID, canAcceptUpload);
    if (ret) {
        ret->setSuggestions(suggestions);
    }
    return ret;
}

// NOTE: onSettings() is implemented in src/hooks/LevelInfoLayer.cpp
// because it needs access to PaimonLevelInfoLayer ($modify type).

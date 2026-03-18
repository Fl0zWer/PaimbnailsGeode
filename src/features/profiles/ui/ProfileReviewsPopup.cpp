#include "ProfileReviewsPopup.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include <Geode/binding/GameManager.hpp>

using namespace geode::prelude;

ProfileReviewsPopup* ProfileReviewsPopup::create(int accountID) {
    auto ret = new ProfileReviewsPopup();
    if (ret && ret->init(accountID)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool ProfileReviewsPopup::init(int accountID) {
    if (!Popup::init(380.f, 260.f)) return false;

    m_accountID = accountID;
    this->setTitle("Profile Reviews");

    auto contentSize = m_mainLayer->getContentSize();
    float centerX = contentSize.width / 2.f;

    // --- average display ---
    m_averageLabel = CCLabelBMFont::create("...", "bigFont.fnt");
    m_averageLabel->setScale(0.5f);
    m_averageLabel->setPosition({centerX, contentSize.height - 48.f});
    m_averageLabel->setColor({255, 215, 0});
    m_mainLayer->addChild(m_averageLabel);

    m_countLabel = CCLabelBMFont::create("Loading...", "chatFont.fnt");
    m_countLabel->setScale(0.55f);
    m_countLabel->setPosition({centerX, contentSize.height - 63.f});
    m_countLabel->setColor({180, 180, 180});
    m_mainLayer->addChild(m_countLabel);

    // --- spinner ---
    m_spinner = LoadingSpinner::create(30.f);
    m_spinner->setPosition({centerX, contentSize.height / 2.f - 20.f});
    m_mainLayer->addChild(m_spinner, 10);

    loadReviews();
    paimon::markDynamicPopup(this);
    return true;
}

void ProfileReviewsPopup::loadReviews() {
    std::string username;
    if (auto gm = GameManager::get()) {
        username = gm->m_playerName;
    }

    std::string endpoint = fmt::format("/api/profile-ratings/{}?username={}", m_accountID, HttpClient::encodeQueryParam(username));

    uint64_t gen = ++m_requestGeneration;
    WeakRef<ProfileReviewsPopup> self = this;
    HttpClient::get().get(endpoint, [self, gen](bool ok, std::string const& resp) {
        auto popup = self.lock();
        if (!popup || gen != popup->m_requestGeneration) return;

        if (popup->m_spinner) {
            popup->m_spinner->removeFromParent();
            popup->m_spinner = nullptr;
        }

        if (!ok) {
            if (popup->m_averageLabel) popup->m_averageLabel->setString("0.0/5");
            if (popup->m_countLabel) popup->m_countLabel->setString("No ratings yet");
            return;
        }

        auto parsed = matjson::parse(resp);
            if (!parsed.isOk()) return;
            auto root = parsed.unwrap();

            float avg = 0.f;
            int count = 0;
            if (root["average"].isNumber()) avg = static_cast<float>(root["average"].asDouble().unwrapOr(0.0));
            if (root["count"].isNumber()) count = static_cast<int>(root["count"].asInt().unwrapOr(0));

            auto reviews = root["reviews"];

            popup->buildReviewList(avg, count, reviews);
    });
}

void ProfileReviewsPopup::buildReviewList(float average, int count, const matjson::Value& reviews) {
    auto contentSize = m_mainLayer->getContentSize();
    float centerX = contentSize.width / 2.f;

    // update header
    if (m_averageLabel) {
        m_averageLabel->setString(fmt::format("{:.1f}/5", average).c_str());
    }
    if (m_countLabel) {
        if (count == 0) {
            m_countLabel->setString("No ratings yet");
        } else {
            m_countLabel->setString(fmt::format("{} rating{}", count, count == 1 ? "" : "s").c_str());
        }
    }

    // scroll area
    float scrollW = contentSize.width - 30.f;
    float scrollH = contentSize.height - 90.f;
    float scrollX = 15.f;
    float scrollY = 15.f;

    // build content
    float cellHeight = 50.f;
    float cellPadding = 4.f;

    std::vector<CCNode*> cells;

    if (reviews.isArray()) {
        auto arr = reviews.asArray().unwrapOr(std::vector<matjson::Value>{});
        for (auto& item : arr) {
            if (!item.isObject()) continue;
            std::string user = item["username"].asString().unwrapOr("???");
            int stars = item["stars"].asInt().unwrapOr(0);
            std::string msg = item["message"].asString().unwrapOr("");

            auto cell = createReviewCell(user, stars, msg, scrollW);
            if (cell) cells.push_back(cell);
        }
    }

    if (cells.empty()) {
        auto noReviews = CCLabelBMFont::create("No reviews with messages yet", "chatFont.fnt");
        noReviews->setScale(0.6f);
        noReviews->setColor({150, 150, 150});
        noReviews->setPosition({centerX, contentSize.height / 2.f - 20.f});
        m_mainLayer->addChild(noReviews);
        return;
    }

    float totalH = 0.f;
    for (auto& c : cells) {
        totalH += c->getContentHeight() + cellPadding;
    }
    totalH = std::max(totalH, scrollH);

    auto container = CCLayer::create();
    container->setContentSize({scrollW, totalH});

    // stack cells top-to-bottom
    float yPos = totalH;
    for (auto& c : cells) {
        yPos -= c->getContentHeight() + cellPadding;
        c->setPosition({0.f, yPos});
        container->addChild(c);
    }

    auto scroll = geode::ScrollLayer::create({scrollW, scrollH});
    scroll->setPosition({scrollX, scrollY});
    scroll->m_contentLayer->addChild(container);
    scroll->m_contentLayer->setContentSize({scrollW, totalH});
    scroll->scrollToTop();
    m_mainLayer->addChild(scroll);
    m_scrollView = scroll;
}

CCNode* ProfileReviewsPopup::createReviewCell(std::string const& username, int stars, std::string const& message, float width) {
    float cellH = message.empty() ? 30.f : 46.f;

    auto cell = CCNode::create();
    cell->setContentSize({width, cellH});

    // dark bg
    auto bg = CCLayerColor::create(ccc4(0, 0, 0, 60));
    bg->setContentSize({width, cellH});
    bg->setPosition({0, 0});
    cell->addChild(bg, -1);

    // username
    auto nameLabel = CCLabelBMFont::create(username.c_str(), "goldFont.fnt");
    nameLabel->setScale(0.45f);
    nameLabel->setAnchorPoint({0, 0.5f});
    nameLabel->setPosition({8.f, cellH - 12.f});
    // limit width
    float maxNameW = width * 0.45f;
    if (nameLabel->getScaledContentWidth() > maxNameW) {
        nameLabel->setScale(maxNameW / nameLabel->getContentWidth());
    }
    cell->addChild(nameLabel);

    // stars on the right
    float starStartX = width - 8.f;
    for (int i = stars; i >= 1; i--) {
        auto starSpr = CCSprite::createWithSpriteFrameName("GJ_starsIcon_001.png");
        if (!starSpr) continue;
        starSpr->setScale(0.35f);
        starSpr->setColor(i <= stars ? ccc3(255, 255, 50) : ccc3(100, 100, 100));
        starSpr->setAnchorPoint({1, 0.5f});
        starSpr->setPosition({starStartX, cellH - 12.f});
        starStartX -= starSpr->getScaledContentWidth() + 1.f;
        cell->addChild(starSpr);
    }
    // also show unlit stars
    for (int i = stars + 1; i <= 5; i++) {
        auto starSpr = CCSprite::createWithSpriteFrameName("GJ_starsIcon_001.png");
        if (!starSpr) continue;
        starSpr->setScale(0.35f);
        starSpr->setColor(ccc3(80, 80, 80));
        starSpr->setAnchorPoint({1, 0.5f});
        starSpr->setPosition({starStartX, cellH - 12.f});
        starStartX -= starSpr->getScaledContentWidth() + 1.f;
        cell->addChild(starSpr);
    }

    // message
    if (!message.empty()) {
        auto msgLabel = CCLabelBMFont::create(message.c_str(), "chatFont.fnt");
        msgLabel->setScale(0.5f);
        msgLabel->setAnchorPoint({0, 0.5f});
        msgLabel->setPosition({10.f, 10.f});
        msgLabel->setColor({220, 220, 220});
        // limit width
        float maxMsgW = width - 20.f;
        if (msgLabel->getScaledContentWidth() > maxMsgW) {
            msgLabel->setScale(maxMsgW / msgLabel->getContentWidth());
        }
        cell->addChild(msgLabel);
    }

    return cell;
}

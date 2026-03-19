#include "PaiConfigLayer.hpp"
#include "../features/backgrounds/ui/SameAsPickerPopup.hpp"
#include "../features/pet/ui/PetConfigPopup.hpp"
#include "../features/profiles/ui/ProfilePicEditorPopup.hpp"
#include "../features/transitions/ui/TransitionConfigPopup.hpp"
#include "../utils/PaimonNotification.hpp"
#include "../features/transitions/services/TransitionManager.hpp"
#include "../utils/InfoButton.hpp"
#include "../utils/FileDialog.hpp"
#include "../utils/ShapeStencil.hpp"
#include "../features/thumbnails/services/LocalThumbs.hpp"
#include "../features/profiles/services/ProfilePicCustomizer.hpp"
#include "../managers/ThumbnailAPI.hpp"
#include "../features/thumbnails/services/ThumbnailLoader.hpp"
#include "../features/profiles/services/ProfileThumbs.hpp"
#include "../features/profile-music/services/ProfileMusicManager.hpp"
#include "../utils/AnimatedGIFSprite.hpp"
#include "../utils/SpriteHelper.hpp"
#include <Geode/ui/LoadingSpinner.hpp>
#include <Geode/binding/ButtonSprite.hpp>

// declarada en ProfilePage.cpp
extern void clearProfileImgCache();
#include <Geode/binding/CCMenuItemToggler.hpp>
#include <Geode/binding/Slider.hpp>
#include <filesystem>
#include <random>

using namespace geode::prelude;

// shaders disponibles pal fondo
static std::vector<std::pair<std::string, std::string>> BG_SHADERS = {
    {"none",       "None"},
    {"grayscale",  "Grayscale"},
    {"sepia",      "Sepia"},
    {"vignette",   "Vignette"},
    {"bloom",      "Bloom"},
    {"chromatic",  "Chromatic"},
    {"pixelate",   "Pixelate"},
    {"posterize",  "Posterize"},
    {"scanlines",  "Scanlines"},
};

PaiConfigLayer* PaiConfigLayer::create() {
    auto ret = new PaiConfigLayer();
    if (ret && ret->init()) { ret->autorelease(); return ret; }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

CCScene* PaiConfigLayer::scene() {
    auto scene = CCScene::create();
    scene->addChild(PaiConfigLayer::create());
    return scene;
}

bool PaiConfigLayer::init() {
    if (!CCLayer::init()) return false;
    this->setKeypadEnabled(true);
    this->setTouchEnabled(true);

    auto winSize = CCDirector::sharedDirector()->getWinSize();
    float cx = winSize.width / 2;
    float top = winSize.height;

    auto bg = CCSprite::create("GJ_gradientBG.png");
    if (bg) {
        bg->setScaleX(winSize.width / bg->getContentWidth());
        bg->setScaleY(winSize.height / bg->getContentHeight());
        bg->setAnchorPoint({0, 0});
        bg->setColor({25, 25, 45});
        this->addChild(bg, -2);
    }

    m_mainMenu = CCMenu::create();
    m_mainMenu->setID("main-menu"_spr);
    m_mainMenu->setPosition({0, 0});
    this->addChild(m_mainMenu, 10);

    auto title = CCLabelBMFont::create("Paimon Settings", "goldFont.fnt");
    title->setPosition({cx, top - 20});
    title->setScale(0.65f);
    this->addChild(title);

    auto backSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png");
    auto backBtn = CCMenuItemSpriteExtra::create(backSpr, this, menu_selector(PaiConfigLayer::onBack));
    backBtn->setPosition({25, top - 20});
    m_mainMenu->addChild(backBtn);

    // tabs principales
    float tabY = top - 46;
    std::vector<std::string> tabNames = {"Backgrounds", "Profile", "Extras"};
    float tabW = 120.f;
    float tabStartX = cx - tabW * 1.0f; // 3 tabs centered

    for (int i = 0; i < 3; i++) {
        auto spr = ButtonSprite::create(tabNames[i].c_str(), "bigFont.fnt", "GJ_button_04.png", .8f);
        spr->setScale(0.48f);
        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(PaiConfigLayer::onMainTabSwitch));
        btn->setTag(i);
        btn->setPosition({tabStartX + i * tabW, tabY});
        m_mainMenu->addChild(btn);
        m_mainTabBtns.push_back(btn);
    }

    auto sep = CCLayerColor::create({255, 255, 255, 40});
    sep->setContentSize({winSize.width - 30, 1});
    sep->setPosition({15, tabY - 14});
    this->addChild(sep, 5);

    m_bgTab = CCLayer::create();
    m_bgTab->setID("bg-tab"_spr);
    this->addChild(m_bgTab, 5);
    m_bgMenu = CCMenu::create();
    m_bgMenu->setID("bg-menu"_spr);
    m_bgMenu->setPosition({0, 0});
    this->addChild(m_bgMenu, 11);

    m_profileTab = CCLayer::create();
    m_profileTab->setID("profile-tab"_spr);
    m_profileTab->setVisible(false);
    this->addChild(m_profileTab, 5);
    m_profileMenu = CCMenu::create();
    m_profileMenu->setID("profile-menu"_spr);
    m_profileMenu->setPosition({0, 0});
    m_profileMenu->setVisible(false);
    this->addChild(m_profileMenu, 11);

    m_extrasTab = CCLayer::create();
    m_extrasTab->setID("extras-tab"_spr);
    m_extrasTab->setVisible(false);
    this->addChild(m_extrasTab, 5);
    m_extrasMenu = CCMenu::create();
    m_extrasMenu->setID("extras-menu"_spr);
    m_extrasMenu->setPosition({0, 0});
    m_extrasMenu->setVisible(false);
    this->addChild(m_extrasMenu, 11);

    buildBackgroundTab();
    buildProfileTab();
    buildExtrasTab();

    auto applySpr = ButtonSprite::create("Apply & Restart Menu", "goldFont.fnt", "GJ_button_01.png", .8f);
    applySpr->setScale(0.5f);
    auto applyBtn = CCMenuItemSpriteExtra::create(applySpr, this, menu_selector(PaiConfigLayer::onApply));
    applyBtn->setPosition({cx, 20});
    m_mainMenu->addChild(applyBtn);

    switchMainTab(0);
    updateLayerButtons();
    refreshForCurrentLayer();

    return true;
}

void PaiConfigLayer::buildBackgroundTab() {
    auto winSize = CCDirector::sharedDirector()->getWinSize();
    float cx = winSize.width / 2;
    float contentTop = winSize.height - 64;
    float contentBot = 42;
    float contentH = contentTop - contentBot;

    // sidebar de layers
    float sidebarW = 95;
    float sidebarX = sidebarW / 2 + 8;

    auto sidePanel = cocos2d::extension::CCScale9Sprite::create("square02_001.png");
    sidePanel->setContentSize({sidebarW, contentH});
    sidePanel->setColor({0, 0, 0});
    sidePanel->setOpacity(70);
    sidePanel->setPosition({sidebarX, contentBot + contentH / 2});
    m_bgTab->addChild(sidePanel, 0);

    auto& layers = LayerBackgroundManager::LAYER_OPTIONS;
    int layerCount = (int)layers.size();
    float layerBtnH = contentH / layerCount;
    float layerStartY = contentTop - layerBtnH / 2;

    for (int i = 0; i < layerCount; i++) {
        auto spr = ButtonSprite::create(layers[i].second.c_str(), "bigFont.fnt", "GJ_button_04.png", .65f);
        spr->setScale(0.32f);
        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(PaiConfigLayer::onLayerSelect));
        btn->setTag(i);
        btn->setPosition({sidebarX, layerStartY - i * layerBtnH});
        m_bgMenu->addChild(btn);
        m_layerBtns.push_back(btn);
    }

    auto sepLine = CCLayerColor::create({255, 255, 255, 50});
    sepLine->setContentSize({1, contentH});
    sepLine->setPosition({sidebarW + 12, contentBot});
    m_bgTab->addChild(sepLine, 5);

    // controles + preview
    float rightX = sidebarW + 20; // left edge of right area
    float rightW = winSize.width - rightX - 8;
    float rightCx = rightX + rightW / 2;

    float ctrlPanelH = 80;
    float ctrlPanelY = contentTop - ctrlPanelH / 2 - 2;

    auto ctrlPanel = cocos2d::extension::CCScale9Sprite::create("square02_001.png");
    ctrlPanel->setContentSize({rightW, ctrlPanelH});
    ctrlPanel->setColor({0, 0, 0});
    ctrlPanel->setOpacity(55);
    ctrlPanel->setPosition({rightCx, ctrlPanelY});
    m_bgTab->addChild(ctrlPanel, 0);

    float titleY = ctrlPanelY + ctrlPanelH / 2 - 10;
    auto bgTitle = CCLabelBMFont::create("Background", "goldFont.fnt");
    bgTitle->setScale(0.35f);
    bgTitle->setPosition({rightCx - 25, titleY});
    m_bgTab->addChild(bgTitle, 1);

    {
        auto iBtn = PaimonInfo::createInfoBtn("Background",
            "<cy>Custom Image</c>: Local PNG/JPG/GIF.\n"
            "<cy>Random</c>: Cached thumbnail.\n"
            "<cy>Same as/Set ID/Default</c>: Other sources.\n"
            "<cy>Dark</c>: Overlay + <cy>Adaptive</c> (Menu).", this, 0.49f);
        if (iBtn) {
            iBtn->setPosition({rightCx + 12, titleY});
            m_bgMenu->addChild(iBtn);
        }
    }

    // fila 1: acciones
    float row1 = titleY - 22;
    float btnSpacing = rightW / 5;
    float btnStart = rightX + btnSpacing / 2 + 5;
    makeBtn("Custom Image", {btnStart, row1}, menu_selector(PaiConfigLayer::onBgCustomImage), m_bgMenu, 0.38f);
    makeBtn("Random", {btnStart + btnSpacing, row1}, menu_selector(PaiConfigLayer::onBgRandom), m_bgMenu, 0.38f);
    makeBtn("Same as...", {btnStart + btnSpacing * 2, row1}, menu_selector(PaiConfigLayer::onBgSameAs), m_bgMenu, 0.38f);
    makeBtn("Default", {btnStart + btnSpacing * 3, row1}, menu_selector(PaiConfigLayer::onBgDefault), m_bgMenu, 0.36f);

    // fila 2: id + dark + intensidad
    float row2 = row1 - 22;

    auto inputBg = cocos2d::extension::CCScale9Sprite::create("square02_001.png");
    inputBg->setContentSize({65, 20});
    inputBg->setOpacity(80);
    inputBg->setPosition({rightX + 42, row2});
    m_bgTab->addChild(inputBg, 1);

    m_bgIdInput = TextInput::create(58, "Level ID");
    m_bgIdInput->setPosition({rightX + 42, row2});
    m_bgIdInput->setFilter("0123456789");
    m_bgIdInput->setScale(0.55f);
    m_bgTab->addChild(m_bgIdInput, 2);

    makeBtn("Set", {rightX + 95, row2}, menu_selector(PaiConfigLayer::onBgSetID), m_bgMenu, 0.34f);

    m_darkToggle = CCMenuItemToggler::createWithStandardSprites(this, menu_selector(PaiConfigLayer::onDarkMode), 0.38f);
    m_darkToggle->setPosition({rightX + 140, row2});
    m_bgMenu->addChild(m_darkToggle);

    auto darkLbl = CCLabelBMFont::create("Dark", "bigFont.fnt");
    darkLbl->setScale(0.18f);
    darkLbl->setPosition({rightX + 163, row2});
    m_bgTab->addChild(darkLbl, 1);

    m_darkSlider = Slider::create(this, menu_selector(PaiConfigLayer::onDarkIntensity), 0.3f);
    m_darkSlider->setPosition({rightX + 250, row2});
    m_bgTab->addChild(m_darkSlider, 1);

    auto intLbl = CCLabelBMFont::create("Intensity", "bigFont.fnt");
    intLbl->setScale(0.16f);
    intLbl->setPosition({rightX + 250, row2 + 10});
    m_bgTab->addChild(intLbl, 1);

    // adaptive solo aplica al menu
    float row3 = row2 - 18;
    m_adaptiveToggle = CCMenuItemToggler::createWithStandardSprites(this, menu_selector(PaiConfigLayer::onAdaptiveColors), 0.35f);
    m_adaptiveToggle->setPosition({rightX + 15, row3});
    m_bgMenu->addChild(m_adaptiveToggle);

    m_adaptiveLabel = CCLabelBMFont::create("Adaptive Colors", "bigFont.fnt");
    m_adaptiveLabel->setScale(0.2f);
    m_adaptiveLabel->setPosition({rightX + 75, row3});
    m_bgTab->addChild(m_adaptiveLabel, 1);

    // selector de shader
    auto shaderTitle = CCLabelBMFont::create("Shader:", "bigFont.fnt");
    shaderTitle->setScale(0.18f);
    shaderTitle->setPosition({rightX + 175, row3});
    m_bgTab->addChild(shaderTitle, 1);

    auto prevArrow = CCSprite::createWithSpriteFrameName("navArrowBtn_001.png");
    prevArrow->setFlipX(true);
    prevArrow->setScale(0.3f);
    auto prevBtn = CCMenuItemSpriteExtra::create(prevArrow, this, menu_selector(PaiConfigLayer::onShaderPrev));
    prevBtn->setPosition({rightX + 210, row3});
    m_bgMenu->addChild(prevBtn);

    m_shaderLabel = CCLabelBMFont::create("None", "bigFont.fnt");
    m_shaderLabel->setScale(0.2f);
    m_shaderLabel->setColor({100, 255, 100});
    m_shaderLabel->setPosition({rightX + 270, row3});
    m_bgTab->addChild(m_shaderLabel, 1);

    auto nextArrow = CCSprite::createWithSpriteFrameName("navArrowBtn_001.png");
    nextArrow->setScale(0.3f);
    auto nextBtn = CCMenuItemSpriteExtra::create(nextArrow, this, menu_selector(PaiConfigLayer::onShaderNext));
    nextBtn->setPosition({rightX + 330, row3});
    m_bgMenu->addChild(nextBtn);

    float previewH = contentH - ctrlPanelH - 12;
    float previewY = contentBot + previewH / 2 + 2;

    auto previewPanel = cocos2d::extension::CCScale9Sprite::create("square02_001.png");
    previewPanel->setContentSize({rightW, previewH});
    previewPanel->setColor({0, 0, 0});
    previewPanel->setOpacity(55);
    previewPanel->setPosition({rightCx, previewY});
    m_bgTab->addChild(previewPanel, 0);

    auto prevTitle = CCLabelBMFont::create("Preview", "goldFont.fnt");
    prevTitle->setScale(0.28f);
    prevTitle->setPosition({rightCx, previewY + previewH / 2 - 8});
    m_bgTab->addChild(prevTitle, 1);

    // texto con el estado actual
    m_bgStatusLabel = CCLabelBMFont::create("Default", "bigFont.fnt");
    m_bgStatusLabel->setScale(0.18f);
    m_bgStatusLabel->setColor({180, 180, 180});
    m_bgStatusLabel->setPosition({rightCx, previewY - previewH / 2 + 8});
    m_bgTab->addChild(m_bgStatusLabel, 1);

    float prevContW = rightW - 16;
    float prevContH = previewH - 24;
    m_bgPreview = CCNode::create();
    m_bgPreview->setPosition({rightCx - prevContW / 2, previewY - prevContH / 2 - 2});
    m_bgPreview->setContentSize({prevContW, prevContH});
    m_bgPreview->setAnchorPoint({0, 0});
    m_bgTab->addChild(m_bgPreview, 2);

    m_blockedOverlay = CCLayerColor::create({0, 0, 0, 180});
    m_blockedOverlay->setContentSize({rightW + 4, contentH});
    m_blockedOverlay->setPosition({rightX - 2, contentBot});
    m_blockedOverlay->setVisible(false);
    m_bgTab->addChild(m_blockedOverlay, 50);

    m_blockedLabel = CCLabelBMFont::create(
        "Level Info uses its own\nthumbnail background.\n\nChange in Mod Settings\n> Background Style.",
        "bigFont.fnt");
    m_blockedLabel->setScale(0.25f);
    m_blockedLabel->setAlignment(kCCTextAlignmentCenter);
    m_blockedLabel->setPosition({rightW / 2 + 2, contentH / 2});
    m_blockedOverlay->addChild(m_blockedLabel);
}

void PaiConfigLayer::buildProfileTab() {
    auto winSize = CCDirector::sharedDirector()->getWinSize();
    float cx = winSize.width / 2;
    float contentMid = (winSize.height - 64 + 40) / 2; // vertical center of content area

    float panelW = 420;
    float panelH = 150;
    auto panel = cocos2d::extension::CCScale9Sprite::create("square02_001.png");
    panel->setContentSize({panelW, panelH});
    panel->setColor({0, 0, 0});
    panel->setOpacity(60);
    panel->setPosition({cx, contentMid});
    m_profileTab->addChild(panel, 0);

    auto profTitle = CCLabelBMFont::create("Profile Picture", "goldFont.fnt");
    profTitle->setScale(0.4f);
    profTitle->setPosition({cx - 60, contentMid + panelH / 2 - 12});
    m_profileTab->addChild(profTitle, 1);

    {
        auto iBtn = PaimonInfo::createInfoBtn("Profile",
            "<cy>Set Image</c>: Pick a local image.\n"
            "<cy>Clear</c>: Remove custom image.\n"
            "<cy>Photo Shape</c>: Edit shape, border, effects.\n"
            "Preview updates in real-time.", this, 0.49f);
        if (iBtn) {
            iBtn->setPosition({cx - 14, contentMid + panelH / 2 - 12});
            m_profileMenu->addChild(iBtn);
        }
    }

    float leftX = cx - 100;
    float btnTop = contentMid + 28;

    auto imgSpr = ButtonSprite::create("Set Image", "goldFont.fnt", "GJ_button_02.png", .8f);
    imgSpr->setScale(0.45f);
    auto imgBtn = CCMenuItemSpriteExtra::create(imgSpr, this, menu_selector(PaiConfigLayer::onProfileImage));
    imgBtn->setPosition({leftX, btnTop});
    m_profileMenu->addChild(imgBtn);

    auto clearSpr = ButtonSprite::create("Clear Image", "goldFont.fnt", "GJ_button_06.png", .8f);
    clearSpr->setScale(0.45f);
    auto clearBtn = CCMenuItemSpriteExtra::create(clearSpr, this, menu_selector(PaiConfigLayer::onProfileImageClear));
    clearBtn->setPosition({leftX, btnTop - 30});
    m_profileMenu->addChild(clearBtn);

    auto shapeSpr = ButtonSprite::create("Photo Shape", "goldFont.fnt", "GJ_button_03.png", .8f);
    shapeSpr->setScale(0.45f);
    auto shapeBtn = CCMenuItemSpriteExtra::create(shapeSpr, this, menu_selector(PaiConfigLayer::onProfilePhoto));
    shapeBtn->setPosition({leftX, btnTop - 60});
    m_profileMenu->addChild(shapeBtn);

    float previewX = cx + 100;
    float previewY = contentMid;

    auto previewLabel = CCLabelBMFont::create("Preview", "goldFont.fnt");
    previewLabel->setScale(0.3f);
    previewLabel->setPosition({previewX, previewY + 48});
    m_profileTab->addChild(previewLabel, 1);

    // este preview se rehace entero cada vez
    m_profilePreview = CCNode::create();
    m_profilePreview->setPosition({previewX - 40, previewY - 40});
    m_profilePreview->setContentSize({80, 80});
    m_profilePreview->setAnchorPoint({0, 0});
    m_profileTab->addChild(m_profilePreview, 5);

    // base oscura del preview
    auto previewBg = cocos2d::extension::CCScale9Sprite::create("square02_001.png");
    previewBg->setContentSize({90, 90});
    previewBg->setColor({0, 0, 0});
    previewBg->setOpacity(80);
    previewBg->setPosition({previewX, previewY});
    m_profileTab->addChild(previewBg, -1);

    rebuildProfilePreview();
}

void PaiConfigLayer::buildExtrasTab() {
    auto winSize = CCDirector::sharedDirector()->getWinSize();
    float cx = winSize.width / 2;
    float contentMid = (winSize.height - 64 + 40) / 2;

    float panelW = 320;
    float panelH = 160;
    auto panel = cocos2d::extension::CCScale9Sprite::create("square02_001.png");
    panel->setContentSize({panelW, panelH});
    panel->setColor({0, 0, 0});
    panel->setOpacity(60);
    panel->setPosition({cx, contentMid});
    m_extrasTab->addChild(panel, 0);

    auto extTitle = CCLabelBMFont::create("Extras", "goldFont.fnt");
    extTitle->setScale(0.4f);
    extTitle->setPosition({cx, contentMid + panelH / 2 - 12});
    m_extrasTab->addChild(extTitle, 1);

    auto petSpr = ButtonSprite::create("Pet Config", "goldFont.fnt", "GJ_button_03.png", .8f);
    petSpr->setScale(0.55f);
    auto petBtn = CCMenuItemSpriteExtra::create(petSpr, this, menu_selector(PaiConfigLayer::onPetConfig));
    petBtn->setPosition({cx, contentMid + 20});
    m_extrasMenu->addChild(petBtn);

    auto betaLabel = CCLabelBMFont::create("BETA", "bigFont.fnt");
    betaLabel->setScale(0.25f);
    betaLabel->setColor({255, 80, 80});
    betaLabel->setPosition({cx + 58, contentMid + 36});
    betaLabel->setRotation(-15.f);
    m_extrasTab->addChild(betaLabel, 10);

    {
        auto iBtn = PaimonInfo::createInfoBtn("Pet",
            "A cute pet follows your cursor.\nThis feature is in <cr>BETA</c> — expect bugs!", this, 0.49f);
        if (iBtn) {
            iBtn->setPosition({cx + 48, contentMid + 20});
            m_extrasMenu->addChild(iBtn);
        }
    }

    auto transSpr = ButtonSprite::create("Transitions", "goldFont.fnt", "GJ_button_04.png", .8f);
    transSpr->setScale(0.55f);
    auto transBtn = CCMenuItemSpriteExtra::create(transSpr, this, menu_selector(PaiConfigLayer::onTransitions));
    transBtn->setID("transitions-config-btn"_spr);
    transBtn->setPosition({cx, contentMid - 20});
    m_extrasMenu->addChild(transBtn);

    {
        auto iBtn = PaimonInfo::createInfoBtn("Transitions",
            "Configure custom scene transition effects.\n"
            "Choose from 15+ built-in transitions or create your own\n"
            "with a custom command sequence (DSL).", this, 0.49f);
        if (iBtn) {
            iBtn->setPosition({cx + 55, contentMid - 20});
            m_extrasMenu->addChild(iBtn);
        }
    }

    auto clearSpr = ButtonSprite::create("Clear All Cache", "goldFont.fnt", "GJ_button_06.png", .8f);
    clearSpr->setScale(0.55f);
    auto clearBtn = CCMenuItemSpriteExtra::create(clearSpr, this, menu_selector(PaiConfigLayer::onClearAllCache));
    clearBtn->setPosition({cx, contentMid - 55});
    m_extrasMenu->addChild(clearBtn);

    {
        auto iBtn = PaimonInfo::createInfoBtn("Clear Cache",
            "<cr>Deletes ALL cached data:</c>\n"
            "- Downloaded thumbnails (RAM + disk)\n"
            "- Profile thumbnails & images\n"
            "- Profile music cache\n"
            "- GIF cache (RAM + disk)\n"
            "- Profile background settings\n\n"
            "This frees up space and fixes stale data.\n"
            "Everything will re-download as needed.", this, 0.49f);
        if (iBtn) {
            iBtn->setPosition({cx + 68, contentMid - 55});
            m_extrasMenu->addChild(iBtn);
        }
    }

    // texto de relleno por ahora
    auto comingSoon = CCLabelBMFont::create("More features coming soon...", "bigFont.fnt");
    comingSoon->setScale(0.2f);
    comingSoon->setColor({150, 150, 150});
    comingSoon->setPosition({cx, contentMid - panelH / 2 + 12});
    m_extrasTab->addChild(comingSoon, 1);
}

// tabs

void PaiConfigLayer::onMainTabSwitch(CCObject* sender) {
    int idx = static_cast<CCNode*>(sender)->getTag();
    switchMainTab(idx);
}

void PaiConfigLayer::switchMainTab(int idx) {
    m_currentMainTab = idx;

    m_bgTab->setVisible(idx == 0);
    m_bgMenu->setVisible(idx == 0);
    m_profileTab->setVisible(idx == 1);
    m_profileMenu->setVisible(idx == 1);
    m_extrasTab->setVisible(idx == 2);
    m_extrasMenu->setVisible(idx == 2);

    // repinto tabs
    for (int i = 0; i < (int)m_mainTabBtns.size(); i++) {
        auto spr = typeinfo_cast<ButtonSprite*>(m_mainTabBtns[i]->getNormalImage());
        if (!spr) continue;
        spr->setColor(i == idx ? ccColor3B{100, 255, 100} : ccColor3B{255, 255, 255});
    }

    // si entro a profile, rehago el preview
    if (idx == 1) rebuildProfilePreview();
}

// vista previa del profile

void PaiConfigLayer::rebuildProfilePreview() {
    if (!m_profilePreview) return;
    m_profilePreview->removeAllChildren();

    const float thumbSize = 70.f;
    auto pSize = m_profilePreview->getContentSize();
    float midX = pSize.width / 2;
    float midY = pSize.height / 2;

    std::string type = Mod::get()->getSavedValue<std::string>("profile-bg-type", "none");
    std::string path = Mod::get()->getSavedValue<std::string>("profile-bg-path", "");

    auto picCfg = ProfilePicCustomizer::get().getConfig();
    std::string shapeName = picCfg.stencilSprite;
    if (shapeName.empty()) shapeName = "circle";

    std::error_code ecExists;
    if (type != "custom" || path.empty() || !std::filesystem::exists(path, ecExists)) {
        // si no hay imagen, muestro placeholder
        auto placeholder = CCLayerColor::create({60, 60, 60, 200});
        placeholder->setContentSize({thumbSize, thumbSize});
        placeholder->setPosition({midX - thumbSize / 2, midY - thumbSize / 2});
        m_profilePreview->addChild(placeholder);

        auto noImg = CCLabelBMFont::create("No\nImage", "bigFont.fnt");
        noImg->setScale(0.2f);
        noImg->setColor({180, 180, 180});
        noImg->setAlignment(kCCTextAlignmentCenter);
        noImg->setPosition({midX, midY});
        m_profilePreview->addChild(noImg, 1);
        return;
    }

    // intento gif primero y si no, imagen normal
    bool isGif = path.ends_with(".gif") || path.ends_with(".GIF");
    CCNode* imageNode = nullptr;

    if (isGif) {
        auto gifSprite = AnimatedGIFSprite::create(path);
        if (gifSprite) {
            imageNode = gifSprite;
        }
    }

    if (!imageNode) {
        auto sprite = CCSprite::create(path.c_str());
        if (sprite) {
            imageNode = sprite;
        }
    }

    if (!imageNode) {
        auto errLbl = CCLabelBMFont::create("Error", "bigFont.fnt");
        errLbl->setScale(0.2f);
        errLbl->setColor({255, 80, 80});
        errLbl->setPosition({midX, midY});
        m_profilePreview->addChild(errLbl);
        return;
    }

    // recorte con la forma elegida
    auto stencil = createShapeStencil(shapeName, thumbSize);
    if (!stencil) stencil = createShapeStencil("circle", thumbSize);
    if (!stencil) return;

    auto clipper = CCClippingNode::create();
    clipper->setStencil(stencil);
    clipper->setAlphaThreshold(-1.0f);
    clipper->setContentSize({thumbSize, thumbSize});

    float scaleX = thumbSize / imageNode->getContentWidth();
    float scaleY = thumbSize / imageNode->getContentHeight();
    float scale = std::max(scaleX, scaleY);
    imageNode->setScale(scale);
    imageNode->setPosition({thumbSize / 2, thumbSize / 2});
    imageNode->setAnchorPoint({0.5f, 0.5f});
    clipper->addChild(imageNode);

    auto container = CCNode::create();
    container->setContentSize({thumbSize, thumbSize});
    container->setAnchorPoint({0.5f, 0.5f});
    container->setPosition({midX, midY});
    container->addChild(clipper);

    // borde opcional
    if (picCfg.frameEnabled) {
        float borderSize = thumbSize + picCfg.frame.thickness * 2;
        auto border = createShapeBorder(shapeName, borderSize,
            picCfg.frame.thickness, picCfg.frame.color,
            static_cast<GLubyte>(picCfg.frame.opacity));
        if (border) {
            border->setAnchorPoint({0.5f, 0.5f});
            border->setPosition({thumbSize / 2, thumbSize / 2});
            container->addChild(border, -1);
        }
    }

    m_profilePreview->addChild(container);
}

// navegacion

void PaiConfigLayer::keyBackClicked() { onBack(nullptr); }

void PaiConfigLayer::onBack(CCObject*) {
    CCDirector::sharedDirector()->popSceneWithTransition(0.3f, PopTransition::kPopTransitionFade);
}

// selector de layer

void PaiConfigLayer::onLayerSelect(CCObject* sender) {
    int idx = static_cast<CCNode*>(sender)->getTag();
    auto& layers = LayerBackgroundManager::LAYER_OPTIONS;
    if (idx >= 0 && idx < (int)layers.size()) {
        m_selectedKey = layers[idx].first;
        updateLayerButtons();
        refreshForCurrentLayer();
    }
}

void PaiConfigLayer::updateLayerButtons() {
    auto& layers = LayerBackgroundManager::LAYER_OPTIONS;
    for (int i = 0; i < (int)m_layerBtns.size(); i++) {
        auto spr = typeinfo_cast<ButtonSprite*>(m_layerBtns[i]->getNormalImage());
        if (!spr) continue;
        spr->setColor(layers[i].first == m_selectedKey ? ccColor3B{0, 255, 0} : ccColor3B{255, 255, 255});
    }
}

void PaiConfigLayer::refreshForCurrentLayer() {
    auto& mgr = LayerBackgroundManager::get();

    bool isLevelInfo = (m_selectedKey == "levelinfo");
    bool levelInfoBlocked = false;
    if (isLevelInfo) {
        std::string bgStyle = Mod::get()->getSettingValue<std::string>("levelinfo-background-style");
        levelInfoBlocked = (bgStyle != "normal");
    }
    if (m_blockedOverlay) m_blockedOverlay->setVisible(levelInfoBlocked);

    auto bgCfg = mgr.getConfig(m_selectedKey);
    if (m_darkToggle) m_darkToggle->toggle(bgCfg.darkMode);
    if (m_darkSlider) m_darkSlider->setValue(bgCfg.darkIntensity);

    bool isMenu = (m_selectedKey == "menu");
    if (m_adaptiveToggle) {
        m_adaptiveToggle->setVisible(isMenu);
        if (isMenu) {
            bool adaptive = Mod::get()->getSavedValue<bool>("bg-adaptive-colors", false);
            m_adaptiveToggle->toggle(adaptive);
        }
    }
    if (m_adaptiveLabel) m_adaptiveLabel->setVisible(isMenu);

    // sincronizo shader
    m_shaderIndex = 0;
    for (int i = 0; i < (int)BG_SHADERS.size(); i++) {
        if (BG_SHADERS[i].first == bgCfg.shader) { m_shaderIndex = i; break; }
    }
    updateShaderLabel();

    // sincronizo estado visible
    if (m_bgStatusLabel) {
        std::string status = "Default";
        if (bgCfg.type == "custom") status = "Custom Image";
        else if (bgCfg.type == "random") status = "Random";
        else if (bgCfg.type == "id") status = "Level ID: " + std::to_string(bgCfg.levelId);
        else if (bgCfg.type == "menu") status = "Same as Menu";
        if (bgCfg.shader != "none" && !bgCfg.shader.empty()) {
            // paso de key a nombre lindo
            for (auto& [k, v] : BG_SHADERS) {
                if (k == bgCfg.shader) { status += " + " + v; break; }
            }
        }
        m_bgStatusLabel->setString(status.c_str());
    }

    // refresco preview
    rebuildBgPreview();
}

void PaiConfigLayer::rebuildBgPreview() {
    if (!m_bgPreview) return;
    m_bgPreview->removeAllChildren();

    auto pSize = m_bgPreview->getContentSize();
    float pw = pSize.width;
    float ph = pSize.height;
    float midX = pw / 2;
    float midY = ph / 2;

    auto& mgr = LayerBackgroundManager::get();
    auto cfg = mgr.getConfig(m_selectedKey);

    auto addTextureToPreview = [&](CCTexture2D* tex) {
        if (!tex || !m_bgPreview) return;
        auto spr = CCSprite::createWithTexture(tex);
        if (!spr) return;

        float scX = pw / spr->getContentWidth();
        float scY = ph / spr->getContentHeight();
        spr->setScale(std::max(scX, scY));
        spr->setPosition({midX, midY});
        spr->setAnchorPoint({0.5f, 0.5f});

        auto stencil = paimon::SpriteHelper::createRectStencil(pw, ph);
        auto clipper = CCClippingNode::create();
        clipper->setStencil(stencil);
        clipper->setAlphaThreshold(0.5f);
        clipper->setContentSize({pw, ph});
        clipper->addChild(spr);
        m_bgPreview->addChild(clipper, 1);
    };

    auto addDarkOverlay = [&]() {
        if (!cfg.darkMode) return;
        auto darkOv = CCLayerColor::create({0, 0, 0, (GLubyte)(cfg.darkIntensity * 200)});
        darkOv->setContentSize({pw, ph});
        m_bgPreview->addChild(darkOv, 5);
    };

    auto showPlaceholder = [&](char const* text, ccColor3B color = {150, 150, 150}) {
        auto bg = CCLayerColor::create({40, 40, 60, 200});
        bg->setContentSize({pw, ph});
        m_bgPreview->addChild(bg, 0);
        auto lbl = CCLabelBMFont::create(text, "bigFont.fnt");
        lbl->setScale(0.2f);
        lbl->setColor(color);
        lbl->setAlignment(kCCTextAlignmentCenter);
        lbl->setPosition({midX, midY});
        m_bgPreview->addChild(lbl, 1);
        addDarkOverlay();
    };

    auto showLoading = [&]() {
        auto bg = CCLayerColor::create({35, 35, 55, 200});
        bg->setContentSize({pw, ph});
        m_bgPreview->addChild(bg, 0);
        auto spinner = LoadingSpinner::create(20.f);
        spinner->setPosition({midX, midY + 8});
        m_bgPreview->addChild(spinner, 1);
        auto lbl = CCLabelBMFont::create("Loading...", "bigFont.fnt");
        lbl->setScale(0.15f);
        lbl->setColor({200, 200, 100});
        lbl->setPosition({midX, midY - 12});
        m_bgPreview->addChild(lbl, 1);
    };

    if (cfg.type == "default") {
        showPlaceholder("Default GD\nBackground");
        return;
    }

    if (cfg.type == "custom") {
        std::error_code ecCustom;
        if (cfg.customPath.empty() || !std::filesystem::exists(cfg.customPath, ecCustom)) {
            showPlaceholder("File not\nfound", {255, 100, 100});
            return;
        }
        auto ext = geode::utils::string::pathToString(std::filesystem::path(cfg.customPath).extension());
        for (auto& c : ext) c = (char)std::tolower(c);

        if (ext == ".gif") {
            // en preview si muestro el GIF animado
            showLoading();
            std::string gifPath = cfg.customPath;
            std::string selectedKey = m_selectedKey;
            Ref<PaiConfigLayer> self = this;
            AnimatedGIFSprite::pinGIF(gifPath);
            AnimatedGIFSprite::createAsync(gifPath, [self, selectedKey, pw, ph, midX, midY](AnimatedGIFSprite* anim) {
                if (self->m_selectedKey != selectedKey || !self->m_bgPreview) {
                    return;
                }
                self->m_bgPreview->removeAllChildren();
                if (!anim) {
                    auto lbl = CCLabelBMFont::create("GIF Error", "bigFont.fnt");
                    lbl->setScale(0.2f);
                    lbl->setColor({255, 80, 80});
                    lbl->setPosition({midX, midY});
                    self->m_bgPreview->addChild(lbl, 1);
                    return;
                }
                float scX = pw / anim->getContentWidth();
                float scY = ph / anim->getContentHeight();
                anim->setScale(std::max(scX, scY));
                anim->setPosition({midX, midY});
                anim->setAnchorPoint({0.5f, 0.5f});

                auto stencil = paimon::SpriteHelper::createRectStencil(pw, ph);
                auto clipper = CCClippingNode::create();
                clipper->setStencil(stencil);
                clipper->setAlphaThreshold(0.5f);
                clipper->setContentSize({pw, ph});
                clipper->addChild(anim);
                self->m_bgPreview->addChild(clipper, 1);

                auto cfg2 = LayerBackgroundManager::get().getConfig(selectedKey);
                if (cfg2.darkMode) {
                    auto darkOv = CCLayerColor::create({0, 0, 0, (GLubyte)(cfg2.darkIntensity * 200)});
                    darkOv->setContentSize({pw, ph});
                    self->m_bgPreview->addChild(darkOv, 5);
                }
            });
            return;
        }

        CCTextureCache::sharedTextureCache()->removeTextureForKey(cfg.customPath.c_str());
        auto* tex = CCTextureCache::sharedTextureCache()->addImage(cfg.customPath.c_str(), false);
        if (tex) {
            addTextureToPreview(tex);
            addDarkOverlay();
        } else {
            showPlaceholder("Load\nerror", {255, 100, 100});
        }
        return;
    }

    // level ID: pruebo local y despues server
    if (cfg.type == "id" && cfg.levelId > 0) {
        // primero local
        auto* localTex = LocalThumbs::get().loadTexture(cfg.levelId);
        if (localTex) {
            addTextureToPreview(localTex);
            addDarkOverlay();
            return;
        }

        // si no esta, muestro loading y lo pido
        showLoading();
        int levelId = cfg.levelId;
        std::string selectedKey = m_selectedKey;
        Ref<PaiConfigLayer> self = this;

        ThumbnailAPI::get().getThumbnail(levelId, [self, selectedKey, pw, ph, midX, midY](bool success, CCTexture2D* tex) {
            if (self->m_selectedKey != selectedKey || !self->m_bgPreview) {
                return;
            }
            self->m_bgPreview->removeAllChildren();

            if (success && tex) {
                auto spr = CCSprite::createWithTexture(tex);
                if (spr) {
                    float scX = pw / spr->getContentWidth();
                    float scY = ph / spr->getContentHeight();
                    spr->setScale(std::max(scX, scY));
                    spr->setPosition({midX, midY});
                    spr->setAnchorPoint({0.5f, 0.5f});

                    auto stencil = paimon::SpriteHelper::createRectStencil(pw, ph);
                    auto clipper = CCClippingNode::create();
                    clipper->setStencil(stencil);
                    clipper->setAlphaThreshold(0.5f);
                    clipper->setContentSize({pw, ph});
                    clipper->addChild(spr);
                    self->m_bgPreview->addChild(clipper, 1);

                    auto cfg2 = LayerBackgroundManager::get().getConfig(selectedKey);
                    if (cfg2.darkMode) {
                        auto darkOv = CCLayerColor::create({0, 0, 0, (GLubyte)(cfg2.darkIntensity * 200)});
                        darkOv->setContentSize({pw, ph});
                        self->m_bgPreview->addChild(darkOv, 5);
                    }
                }
            } else {
                auto bgRect = CCLayerColor::create({40, 40, 60, 200});
                bgRect->setContentSize({pw, ph});
                self->m_bgPreview->addChild(bgRect, 0);
                auto lbl = CCLabelBMFont::create("Not found\non server", "bigFont.fnt");
                lbl->setScale(0.18f);
                lbl->setColor({255, 120, 80});
                lbl->setAlignment(kCCTextAlignmentCenter);
                lbl->setPosition({midX, midY});
                self->m_bgPreview->addChild(lbl, 1);
            }
        });
        return;
    }

    if (cfg.type == "random") {
        auto ids = LocalThumbs::get().getAllLevelIDs();
        if (!ids.empty()) {
            // saco uno random cada vez
            static std::mt19937 rng(std::random_device{}());
            std::uniform_int_distribution<size_t> dist(0, ids.size() - 1);
            auto* tex = LocalThumbs::get().loadTexture(ids[dist(rng)]);
            if (tex) {
                addTextureToPreview(tex);
                addDarkOverlay();
                return;
            }
        }
        showPlaceholder("Random\n(no cache)", {180, 180, 100});
        return;
    }

    // si hereda de otro layer, sigo la referencia
    bool isLayerRef = false;
    for (auto& [k, n] : LayerBackgroundManager::LAYER_OPTIONS) {
        if (cfg.type == k) { isLayerRef = true; break; }
    }
    if (isLayerRef || cfg.type == "menu") {
        // corto a 5 saltos por si alguien arma un loop
        LayerBgConfig resolvedCfg = mgr.getConfig(cfg.type == "menu" ? "menu" : cfg.type);
        int resolveHops = 5;
        while (resolveHops-- > 0) {
            if (resolvedCfg.type == "default") {
                showPlaceholder(("-> " + cfg.type + "\n(Default)").c_str(), {150, 200, 255});
                return;
            }
            bool isRef = false;
            for (auto& [k2, n2] : LayerBackgroundManager::LAYER_OPTIONS) {
                if (resolvedCfg.type == k2) { isRef = true; break; }
            }
            if (isRef) {
                resolvedCfg = mgr.getConfig(resolvedCfg.type);
                continue;
            }
            break;
        }

        // con eso ya tengo el config final
        std::error_code ecResolved;
        if (resolvedCfg.type == "custom" && !resolvedCfg.customPath.empty() && std::filesystem::exists(resolvedCfg.customPath, ecResolved)) {
            auto ext = geode::utils::string::pathToString(std::filesystem::path(resolvedCfg.customPath).extension());
            for (auto& c : ext) c = (char)std::tolower(c);
            if (ext == ".gif") {
                showLoading();
                std::string gifPath = resolvedCfg.customPath;
                std::string selectedKey = m_selectedKey;
                Ref<PaiConfigLayer> self = this;
                AnimatedGIFSprite::pinGIF(gifPath);
                AnimatedGIFSprite::createAsync(gifPath, [self, selectedKey, pw, ph, midX, midY, cfg](AnimatedGIFSprite* anim) {
                    if (self->m_selectedKey != selectedKey || !self->m_bgPreview) { return; }
                    self->m_bgPreview->removeAllChildren();
                    if (!anim) {
                        auto lbl = CCLabelBMFont::create("GIF Error", "bigFont.fnt");
                        lbl->setScale(0.2f); lbl->setColor({255, 80, 80}); lbl->setPosition({midX, midY});
                        self->m_bgPreview->addChild(lbl, 1);
                        return;
                    }
                    float scX2 = pw / anim->getContentWidth();
                    float scY2 = ph / anim->getContentHeight();
                    anim->setScale(std::max(scX2, scY2));
                    anim->setPosition({midX, midY});
                    anim->setAnchorPoint({0.5f, 0.5f});
                    auto stencil2 = paimon::SpriteHelper::createRectStencil(pw, ph);
                    auto clipper2 = CCClippingNode::create();
                    clipper2->setStencil(stencil2);
                    clipper2->setAlphaThreshold(0.5f);
                    clipper2->setContentSize({pw, ph});
                    clipper2->addChild(anim);
                    self->m_bgPreview->addChild(clipper2, 1);
                    if (cfg.darkMode) {
                        auto darkOv = CCLayerColor::create({0, 0, 0, (GLubyte)(cfg.darkIntensity * 200)});
                        darkOv->setContentSize({pw, ph});
                        self->m_bgPreview->addChild(darkOv, 5);
                    }
                });
                return;
            }
            CCTextureCache::sharedTextureCache()->removeTextureForKey(resolvedCfg.customPath.c_str());
            auto* resolvedTex = CCTextureCache::sharedTextureCache()->addImage(resolvedCfg.customPath.c_str(), false);
            if (resolvedTex) {
                addTextureToPreview(resolvedTex);
                addDarkOverlay();
                return;
            }
        } else if (resolvedCfg.type == "id" && resolvedCfg.levelId > 0) {
            auto* localTex = LocalThumbs::get().loadTexture(resolvedCfg.levelId);
            if (localTex) {
                addTextureToPreview(localTex);
                addDarkOverlay();
                return;
            }
        } else if (resolvedCfg.type == "random") {
            auto ids = LocalThumbs::get().getAllLevelIDs();
            if (!ids.empty()) {
                static std::mt19937 refRng(std::random_device{}());
                std::uniform_int_distribution<size_t> dist(0, ids.size() - 1);
                auto* randomTex = LocalThumbs::get().loadTexture(ids[dist(refRng)]);
                if (randomTex) {
                    addTextureToPreview(randomTex);
                    addDarkOverlay();
                    return;
                }
            }
        }
        showPlaceholder(("-> " + cfg.type).c_str(), {150, 200, 255});
        return;
    }

    // ultimo recurso
    showPlaceholder("Unknown\ntype", {200, 150, 150});
}

void PaiConfigLayer::onBgCustomImage(CCObject*) {
    Ref<PaiConfigLayer> self = this;
    std::string key = m_selectedKey;
    pt::openImageFileDialog([self, key](std::optional<std::filesystem::path> result) {
        if (result.has_value() && !result.value().empty()) {
            auto pathStr = geode::utils::string::pathToString(result.value());
            std::replace(pathStr.begin(), pathStr.end(), '\\', '/');
            auto cfg = LayerBackgroundManager::get().getConfig(key);
            cfg.type = "custom"; cfg.customPath = pathStr;
            LayerBackgroundManager::get().saveConfig(key, cfg);
            PaimonNotify::create("Custom image set!", NotificationIcon::Success)->show();
            self->refreshForCurrentLayer();
        }
    });
}

void PaiConfigLayer::onBgRandom(CCObject*) {
    auto cfg = LayerBackgroundManager::get().getConfig(m_selectedKey);
    cfg.type = "random";
    LayerBackgroundManager::get().saveConfig(m_selectedKey, cfg);
    PaimonNotify::create("Random background set!", NotificationIcon::Success)->show();
    refreshForCurrentLayer();
}

void PaiConfigLayer::onBgSetID(CCObject*) {
    if (!m_bgIdInput) return;
    std::string idStr = m_bgIdInput->getString();
    if (idStr.empty()) return;
    if (auto res = geode::utils::numFromString<int>(idStr)) {
        int levelId = res.unwrap();
        auto cfg = LayerBackgroundManager::get().getConfig(m_selectedKey);
        cfg.type = "id"; cfg.levelId = levelId;
        LayerBackgroundManager::get().saveConfig(m_selectedKey, cfg);
        PaimonNotify::create("Level ID set!", NotificationIcon::Success)->show();
        refreshForCurrentLayer(); // triggers preview download + status update
    } else {
        PaimonNotify::create("Invalid ID", NotificationIcon::Error)->show();
    }
}

void PaiConfigLayer::onBgSameAs(CCObject*) {
    std::string key = m_selectedKey;
    Ref<PaiConfigLayer> self = this;
    auto popup = SameAsPickerPopup::create(key, [self, key](std::string const& picked) {
        auto cfg = LayerBackgroundManager::get().getConfig(key);
        cfg.type = picked;
        LayerBackgroundManager::get().saveConfig(key, cfg);
        PaimonNotify::create(("Using same bg as " + picked + "!").c_str(), NotificationIcon::Success)->show();
        if (self->m_selectedKey == key) self->refreshForCurrentLayer();
    });
    if (popup) popup->show();
}

void PaiConfigLayer::onBgDefault(CCObject*) {
    LayerBgConfig cfg; cfg.type = "default";
    LayerBackgroundManager::get().saveConfig(m_selectedKey, cfg);
    PaimonNotify::create("Reverted to default!", NotificationIcon::Success)->show();
    refreshForCurrentLayer();
}

void PaiConfigLayer::onDarkMode(CCObject* sender) {
    auto toggle = typeinfo_cast<CCMenuItemToggler*>(sender);
    if (!toggle) return;
    auto cfg = LayerBackgroundManager::get().getConfig(m_selectedKey);
    cfg.darkMode = !toggle->isToggled();
    LayerBackgroundManager::get().saveConfig(m_selectedKey, cfg);
    rebuildBgPreview();
}

void PaiConfigLayer::onDarkIntensity(CCObject*) {
    if (!m_darkSlider) return;
    auto cfg = LayerBackgroundManager::get().getConfig(m_selectedKey);
    cfg.darkIntensity = m_darkSlider->getValue();
    LayerBackgroundManager::get().saveConfig(m_selectedKey, cfg);
    rebuildBgPreview();
}

void PaiConfigLayer::onAdaptiveColors(CCObject* sender) {
    auto toggle = typeinfo_cast<CCMenuItemToggler*>(sender);
    if (!toggle) return;
    Mod::get()->setSavedValue("bg-adaptive-colors", !toggle->isToggled());
    (void)Mod::get()->saveData();
}

void PaiConfigLayer::onShaderPrev(CCObject*) {
    m_shaderIndex--;
    if (m_shaderIndex < 0) m_shaderIndex = (int)BG_SHADERS.size() - 1;
    auto cfg = LayerBackgroundManager::get().getConfig(m_selectedKey);
    cfg.shader = BG_SHADERS[m_shaderIndex].first;
    LayerBackgroundManager::get().saveConfig(m_selectedKey, cfg);
    updateShaderLabel();
    refreshForCurrentLayer();
}

void PaiConfigLayer::onShaderNext(CCObject*) {
    m_shaderIndex++;
    if (m_shaderIndex >= (int)BG_SHADERS.size()) m_shaderIndex = 0;
    auto cfg = LayerBackgroundManager::get().getConfig(m_selectedKey);
    cfg.shader = BG_SHADERS[m_shaderIndex].first;
    LayerBackgroundManager::get().saveConfig(m_selectedKey, cfg);
    updateShaderLabel();
    refreshForCurrentLayer();
}

void PaiConfigLayer::updateShaderLabel() {
    if (!m_shaderLabel) return;
    if (m_shaderIndex >= 0 && m_shaderIndex < (int)BG_SHADERS.size()) {
        m_shaderLabel->setString(BG_SHADERS[m_shaderIndex].second.c_str());
        m_shaderLabel->setColor(m_shaderIndex == 0 ? ccColor3B{180, 180, 180} : ccColor3B{100, 255, 100});
    }
}

// acciones de profile

void PaiConfigLayer::onProfileImage(CCObject*) {
    this->setTouchEnabled(false);
    Ref<PaiConfigLayer> self = this;
    pt::openImageFileDialog([self](std::optional<std::filesystem::path> result) {
        self->setTouchEnabled(true);

        if (result.has_value()) {
            auto path = result.value();
            if (!path.empty()) {
                auto pathStr = geode::utils::string::pathToString(path);
                std::replace(pathStr.begin(), pathStr.end(), '\\', '/');
                Mod::get()->setSavedValue<std::string>("profile-bg-type", "custom");
                Mod::get()->setSavedValue<std::string>("profile-bg-path", pathStr);
                (void)Mod::get()->saveData();
                PaimonNotify::create("Profile image set!", NotificationIcon::Success)->show();
                self->rebuildProfilePreview();
            }
        }
    });
}

void PaiConfigLayer::onProfileImageClear(CCObject*) {
    Mod::get()->setSavedValue<std::string>("profile-bg-type", "none");
    Mod::get()->setSavedValue<std::string>("profile-bg-path", "");
    (void)Mod::get()->saveData();
    PaimonNotify::create("Profile image cleared!", NotificationIcon::Success)->show();
    rebuildProfilePreview();
}

void PaiConfigLayer::onProfilePhoto(CCObject*) {
    auto popup = ProfilePicEditorPopup::create();
    if (popup) popup->show();
}

// extras

void PaiConfigLayer::onPetConfig(CCObject*) {
    auto popup = PetConfigPopup::create();
    if (popup) popup->show();
}

void PaiConfigLayer::onTransitions(CCObject*) {
    auto popup = TransitionConfigPopup::create();
    if (popup) popup->show();
}

void PaiConfigLayer::onClearAllCache(CCObject*) {
    geode::createQuickPopup(
        "Clear All Cache",
        "This will <cr>delete all cached data</c>:\n"
        "thumbnails, profile images, profile music,\n"
        "GIFs, and profile background settings.\n\n"
        "Are you sure?",
        "Cancel", "Clear",
        [this](auto*, bool confirmed) {
            if (!confirmed) return;

            // 1) paro cualquier musica de perfil
            ProfileMusicManager::get().stopProfileMusic();
            ProfileMusicManager::get().stopPreview();

            // 2) cache RAM de thumbs
            ThumbnailLoader::get().clearPendingQueue();
            ThumbnailLoader::get().clearCache();

            // 3) cache de thumbs en disco
            ThumbnailLoader::get().clearDiskCache();

            // 4) cache RAM de perfiles
            ProfileThumbs::get().clearAllCache();

            // 5) cache de profileimg
            clearProfileImgCache();

            // 6) cache de profile music
            ProfileMusicManager::get().clearCache();

            // 7) gifs en RAM
            AnimatedGIFSprite::clearCache();

            // 8) gifs en disco
            {
                std::error_code ec;
                auto gifCacheDir = Mod::get()->getSaveDir() / "gif_cache";
                if (std::filesystem::exists(gifCacheDir, ec)) {
                    std::filesystem::remove_all(gifCacheDir, ec);
                }
            }

            // 9) settings de imagen de perfil
            Mod::get()->setSavedValue<std::string>("profile-bg-type", "none");
            Mod::get()->setSavedValue<std::string>("profile-bg-path", "");
            (void)Mod::get()->saveData();

            // 10) preview al dia
            rebuildProfilePreview();

            log::info("[PaiConfigLayer] All caches cleared by user");
            PaimonNotify::create("All caches cleared!", NotificationIcon::Success)->show();
        }
    );
}

void PaiConfigLayer::onApply(CCObject*) {
    TransitionManager::get().replaceScene(MenuLayer::scene(false));
}

// util

CCMenuItemSpriteExtra* PaiConfigLayer::makeBtn(char const* text, CCPoint pos,
    SEL_MenuHandler handler, CCNode* parent, float scale) {
    auto spr = ButtonSprite::create(text);
    spr->setScale(scale);
    auto btn = CCMenuItemSpriteExtra::create(spr, this, handler);
    btn->setPosition(pos);
    parent->addChild(btn);
    return btn;
}


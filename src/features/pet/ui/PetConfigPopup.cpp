#include "PetConfigPopup.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "PaimonShopPopup.hpp"
#include "../services/PetManager.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../../utils/FileDialog.hpp"
#include "../../../utils/ImageLoadHelper.hpp"
#include "../../../utils/InfoButton.hpp"
#include <Geode/binding/ButtonSprite.hpp>

using namespace geode::prelude;
using namespace cocos2d;

// ════════════════════════════════════════════════════════════
// create
// ════════════════════════════════════════════════════════════

PetConfigPopup* PetConfigPopup::create() {
    auto ret = new PetConfigPopup();
    if (ret && ret->init()) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

// ════════════════════════════════════════════════════════════
// init
// ════════════════════════════════════════════════════════════

bool PetConfigPopup::init() {
    if (!Popup::init(380.f, 270.f)) return false;

    this->setTitle("Pet / Mascot");

    auto content = m_mainLayer->getContentSize();

    // ── tab layers ──
    m_galleryTab = CCNode::create();
    m_galleryTab->setID("gallery-tab"_spr);
    m_galleryTab->setContentSize(content);
    m_mainLayer->addChild(m_galleryTab, 5);

    m_settingsTab = CCNode::create();
    m_settingsTab->setID("settings-tab"_spr);
    m_settingsTab->setContentSize(content);
    m_settingsTab->setVisible(false);
    m_mainLayer->addChild(m_settingsTab, 5);

    createTabButtons();
    buildGalleryTab();
    buildSettingsTab();

    paimon::markDynamicPopup(this);
    return true;
}

// ════════════════════════════════════════════════════════════
// tabs
// ════════════════════════════════════════════════════════════

void PetConfigPopup::createTabButtons() {
    auto content = m_mainLayer->getContentSize();
    float topY = content.height - 38.f;
    float cx = content.width / 2.f;

    auto menu = CCMenu::create();
    menu->setID("tab-buttons-menu"_spr);
    menu->setPosition({0, 0});
    m_mainLayer->addChild(menu, 10);

    auto spr1 = ButtonSprite::create("Gallery");
    spr1->setScale(0.5f);
    auto tab1 = CCMenuItemSpriteExtra::create(spr1, this, menu_selector(PetConfigPopup::onTabSwitch));
    tab1->setTag(0);
    tab1->setPosition({cx - 60.f, topY});
    menu->addChild(tab1);
    m_tabs.push_back(tab1);

    auto spr2 = ButtonSprite::create("Settings");
    spr2->setScale(0.5f);
    auto tab2 = CCMenuItemSpriteExtra::create(spr2, this, menu_selector(PetConfigPopup::onTabSwitch));
    tab2->setTag(1);
    tab2->setPosition({cx + 60.f, topY});
    menu->addChild(tab2);
    m_tabs.push_back(tab2);

    // initial state
    onTabSwitch(tab1);
}

void PetConfigPopup::onTabSwitch(CCObject* sender) {
    auto btn = typeinfo_cast<CCMenuItemSpriteExtra*>(sender);
    if (!btn) return;
    m_currentTab = btn->getTag();

    m_galleryTab->setVisible(m_currentTab == 0);
    m_settingsTab->setVisible(m_currentTab == 1);

    for (auto* tab : m_tabs) {
        auto spr = typeinfo_cast<ButtonSprite*>(tab->getNormalImage());
        if (!spr) continue;
        if (tab->getTag() == m_currentTab) {
            spr->setColor({0, 255, 0});
            spr->setOpacity(255);
        } else {
            spr->setColor({255, 255, 255});
            spr->setOpacity(150);
        }
    }
}

// ════════════════════════════════════════════════════════════
// gallery tab
// ════════════════════════════════════════════════════════════

void PetConfigPopup::buildGalleryTab() {
    auto content = m_mainLayer->getContentSize();
    float cx = content.width / 2.f;

    // auto-cleanup invalid/corrupt image files from gallery
    int cleaned = PetManager::get().cleanupInvalidImages();
    if (cleaned > 0) {
        log::info("[PetConfig] Cleaned up {} invalid image files from gallery", cleaned);
    }

    // preview area
    auto previewBg = cocos2d::extension::CCScale9Sprite::create("square02_001.png");
    previewBg->setContentSize({80, 80});
    previewBg->setOpacity(80);
    previewBg->setPosition({cx, content.height - 95.f});
    m_galleryTab->addChild(previewBg);

    m_selectedLabel = CCLabelBMFont::create("No pet selected", "bigFont.fnt");
    m_selectedLabel->setScale(0.25f);
    m_selectedLabel->setPosition({cx, content.height - 145.f});
    m_galleryTab->addChild(m_selectedLabel);

    // gallery scroll area
    m_galleryContainer = CCNode::create();
    m_galleryContainer->setID("gallery-container"_spr);
    m_galleryContainer->setPosition({0, 0});
    m_galleryTab->addChild(m_galleryContainer);

    // gallery menu
    m_galleryMenu = CCMenu::create();
    m_galleryMenu->setID("gallery-menu"_spr);
    m_galleryMenu->setPosition({0, 0});
    m_galleryTab->addChild(m_galleryMenu, 10);

    // add button
    auto addSpr = ButtonSprite::create("+ Add", "goldFont.fnt", "GJ_button_01.png", 0.7f);
    addSpr->setScale(0.55f);
    auto addBtn = CCMenuItemSpriteExtra::create(addSpr, this, menu_selector(PetConfigPopup::onAddImage));
    addBtn->setPosition({cx - 90.f, 25.f});
    m_galleryMenu->addChild(addBtn);

    // shop button
    auto shopSpr = ButtonSprite::create("Shop", "goldFont.fnt", "GJ_button_02.png", 0.7f);
    shopSpr->setScale(0.55f);
    auto shopBtn = CCMenuItemSpriteExtra::create(shopSpr, this, menu_selector(PetConfigPopup::onOpenShop));
    shopBtn->setPosition({cx - 15.f, 25.f});
    m_galleryMenu->addChild(shopBtn);

    // delete all button
    auto delAllSpr = ButtonSprite::create("Delete All", "goldFont.fnt", "GJ_button_06.png", 0.7f);
    delAllSpr->setScale(0.55f);
    auto delAllBtn = CCMenuItemSpriteExtra::create(delAllSpr, this, menu_selector(PetConfigPopup::onDeleteAllImages));
    delAllBtn->setPosition({cx + 75.f, 25.f});
    m_galleryMenu->addChild(delAllBtn);

    refreshGallery();
}

void PetConfigPopup::refreshGallery() {
    // clear old gallery items (keep addBtn)
    // remove non-button gallery children
    if (m_galleryContainer) {
        m_galleryContainer->removeAllChildren();
    }

    // remove old gallery buttons from menu (but not the add button)
    auto toRemove = std::vector<CCNode*>();
    if (m_galleryMenu && m_galleryMenu->getChildren()) {
        for (auto* child : CCArrayExt<CCNode*>(m_galleryMenu->getChildren())) {
            if (child->getTag() >= 100) toRemove.push_back(child);
        }
    }
    for (auto* n : toRemove) n->removeFromParent();

    auto& pet = PetManager::get();
    auto images = pet.getGalleryImages();
    auto content = m_mainLayer->getContentSize();
    float cx = content.width / 2.f;

    float startX = 35.f;
    float startY = content.height - 175.f;
    float cellSize = 48.f;
    float padding = 6.f;
    int cols = static_cast<int>((content.width - 30.f) / (cellSize + padding));
    if (cols < 1) cols = 1;

    for (int i = 0; i < (int)images.size(); i++) {
        float col = static_cast<float>(i % cols);
        float row = static_cast<float>(i / cols);
        float x = startX + col * (cellSize + padding) + cellSize / 2.f;
        float y = startY - row * (cellSize + padding);

        // background
        auto bg = cocos2d::extension::CCScale9Sprite::create("square02_001.png");
        bg->setContentSize({cellSize, cellSize});
        bg->setPosition({x, y});
        bool isSelected = (images[i] == pet.config().selectedImage);
        bg->setColor(isSelected ? ccc3(0, 200, 0) : ccc3(50, 50, 50));
        bg->setOpacity(isSelected ? 180 : 100);
        m_galleryContainer->addChild(bg);

        // thumbnail
        auto tex = pet.loadGalleryThumb(images[i]);
        if (tex) {
            auto thumbSpr = CCSprite::createWithTexture(tex);
            if (thumbSpr) {
                float maxDim = std::max(thumbSpr->getContentSize().width, thumbSpr->getContentSize().height);
                if (maxDim > 0) thumbSpr->setScale((cellSize - 8.f) / maxDim);
                thumbSpr->setPosition({x, y});
                m_galleryContainer->addChild(thumbSpr, 1);
            }
            tex->release();
        }

        // select button (invisible overlay)
        auto selectArea = CCSprite::create();
        selectArea->setContentSize({cellSize, cellSize});
        selectArea->setOpacity(0);
        auto selectBtn = CCMenuItemSpriteExtra::create(selectArea, this, menu_selector(PetConfigPopup::onSelectImage));
        selectBtn->setContentSize({cellSize, cellSize});
        selectBtn->setPosition({x, y});
        selectBtn->setTag(100 + i);
        // store filename as user data
        auto* nameStr = CCString::create(images[i]);
        selectBtn->setUserObject(nameStr);
        m_galleryMenu->addChild(selectBtn);

        // delete btn (small X)
        auto xSpr = CCSprite::createWithSpriteFrameName("GJ_deleteIcon_001.png");
        if (xSpr) {
            xSpr->setScale(0.35f);
            auto xBtn = CCMenuItemSpriteExtra::create(xSpr, this, menu_selector(PetConfigPopup::onDeleteImage));
            xBtn->setPosition({x + cellSize / 2.f - 5.f, y + cellSize / 2.f - 5.f});
            xBtn->setTag(500 + i);
            auto* nameStr2 = CCString::create(images[i]);
            xBtn->setUserObject(nameStr2);
            m_galleryMenu->addChild(xBtn);
        }
    }

    // update preview
    auto& cfg = pet.config();
    if (!cfg.selectedImage.empty()) {
        // remove old preview
        if (m_previewSprite) {
            m_previewSprite->removeFromParent();
            m_previewSprite = nullptr;
        }
        auto tex = pet.loadGalleryThumb(cfg.selectedImage);
        if (tex) {
            m_previewSprite = CCSprite::createWithTexture(tex);
            if (m_previewSprite) {
                float maxDim = std::max(m_previewSprite->getContentSize().width, m_previewSprite->getContentSize().height);
                if (maxDim > 0) m_previewSprite->setScale(70.f / maxDim);
                m_previewSprite->setPosition({cx, content.height - 95.f});
                m_galleryTab->addChild(m_previewSprite, 5);
            }
            tex->release();
        }
        m_selectedLabel->setString(cfg.selectedImage.c_str());
    } else {
        if (m_previewSprite) {
            m_previewSprite->removeFromParent();
            m_previewSprite = nullptr;
        }
        m_selectedLabel->setString("No pet selected");
    }
}

void PetConfigPopup::onAddImage(CCObject*) {
    this->setTouchEnabled(false);
    Ref<PetConfigPopup> self = this;
    pt::openImageFileDialog([self](std::optional<std::filesystem::path> result) {
        self->setTouchEnabled(true);

        if (result.has_value() && !result.value().empty()) {
            auto filename = PetManager::get().addToGallery(result.value());
            if (!filename.empty()) {
                PaimonNotify::create("Image added to gallery!", NotificationIcon::Success)->show();
                // auto-select if no image selected
                if (PetManager::get().config().selectedImage.empty()) {
                    PetManager::get().setImage(filename);
                }
                self->refreshGallery();
            } else {
                PaimonNotify::create("Failed to add image", NotificationIcon::Error)->show();
            }
        }
    });
}

void PetConfigPopup::onDeleteImage(CCObject* sender) {
    auto btn = typeinfo_cast<CCMenuItemSpriteExtra*>(sender);
    if (!btn) return;
    auto nameObj = typeinfo_cast<CCString*>(btn->getUserObject());
    if (!nameObj) return;

    std::string filename = nameObj->getCString();

    geode::createQuickPopup(
        "Delete Pet",
        "Are you sure you want to <cr>delete</c> this pet image?\n<cy>" + filename + "</c>",
        "Cancel", "Delete",
        [this, filename](auto*, bool confirmed) {
            if (!confirmed) return;
            PetManager::get().removeFromGallery(filename);
            PaimonNotify::create("Image removed", NotificationIcon::Info)->show();
            refreshGallery();
        }
    );
}

void PetConfigPopup::onDeleteAllImages(CCObject*) {
    auto images = PetManager::get().getGalleryImages();
    if (images.empty()) {
        PaimonNotify::create("Gallery is already empty", NotificationIcon::Info)->show();
        return;
    }

    std::string msg = fmt::format(
        "Are you sure you want to <cr>delete ALL</c> {} pet images?\nThis cannot be undone!",
        images.size()
    );

    geode::createQuickPopup(
        "Delete All Pets",
        msg,
        "Cancel", "Delete All",
        [this](auto*, bool confirmed) {
            if (!confirmed) return;

            // also clean up any invalid images
            int cleaned = PetManager::get().cleanupInvalidImages();

            PetManager::get().removeAllFromGallery();

            std::string note = "All pet images deleted!";
            if (cleaned > 0) {
                note += fmt::format(" ({} corrupted files removed)", cleaned);
            }
            PaimonNotify::create(note, NotificationIcon::Success)->show();
            refreshGallery();
        }
    );
}

void PetConfigPopup::onSelectImage(CCObject* sender) {
    auto btn = typeinfo_cast<CCMenuItemSpriteExtra*>(sender);
    if (!btn) return;
    auto nameObj = typeinfo_cast<CCString*>(btn->getUserObject());
    if (!nameObj) return;

    std::string filename = nameObj->getCString();
    PetManager::get().setImage(filename);
    PaimonNotify::create("Pet image set!", NotificationIcon::Success)->show();
    refreshGallery();
}

// ════════════════════════════════════════════════════════════
// settings tab
// ════════════════════════════════════════════════════════════

void PetConfigPopup::buildSettingsTab() {
    auto content = m_mainLayer->getContentSize();
    float scrollW = content.width - 16.f;
    float scrollH = content.height - 52.f;
    float totalH = 920.f;

    m_scrollLayer = ScrollLayer::create({scrollW, scrollH});
    m_scrollLayer->setPosition({8.f, 8.f});
    m_settingsTab->addChild(m_scrollLayer, 5);

    auto* sc = m_scrollLayer->m_contentLayer;
    sc->setContentSize({scrollW, totalH});

    auto navMenu = CCMenu::create();
    navMenu->setPosition({0, 0});
    navMenu->setContentSize({scrollW, totalH});
    sc->addChild(navMenu, 10);

    float cx = scrollW / 2.f;
    float y = totalH - 8.f;

    auto& cfg = PetManager::get().config();

    // helpers
    auto addTitle = [&](char const* text, char const* info = nullptr) {
        auto label = CCLabelBMFont::create(text, "goldFont.fnt");
        label->setScale(0.4f);
        label->setPosition({cx, y});
        sc->addChild(label);

        if (info) {
            auto btn = PaimonInfo::createInfoBtn(text, info, this, 0.45f);
            if (btn) {
                float halfW = label->getContentSize().width * 0.4f / 2.f;
                btn->setPosition({cx + halfW + 10.f, y});
                navMenu->addChild(btn);
            }
        }
    };

    auto addSlider = [&](Slider*& slider, CCLabelBMFont*& label,
                         float val, float minV, float maxV,
                         SEL_MenuHandler cb, char const* fmt_str) {
        float norm = (maxV > minV) ? (val - minV) / (maxV - minV) : 0.f;
        slider = Slider::create(this, cb, 0.55f);
        slider->setPosition({cx - 5.f, y});
        slider->setValue(norm);
        sc->addChild(slider);

        label = CCLabelBMFont::create(fmt::format(fmt::runtime(fmt_str), val).c_str(), "bigFont.fnt");
        label->setScale(0.22f);
        label->setPosition({cx + 78.f, y});
        sc->addChild(label);
    };

    auto addToggle = [&](char const* text, CCMenuItemToggler*& toggle,
                         bool value, SEL_MenuHandler cb, char const* info = nullptr) {
        auto lbl = CCLabelBMFont::create(text, "bigFont.fnt");
        lbl->setScale(0.26f);
        lbl->setAnchorPoint({0.f, 0.5f});
        lbl->setPosition({cx - 85.f, y});
        sc->addChild(lbl);

        if (info) {
            auto iBtn = PaimonInfo::createInfoBtn(text, info, this, 0.35f);
            if (iBtn) {
                float lblW = lbl->getContentSize().width * 0.26f;
                iBtn->setPosition({cx - 85.f + lblW + 7.f, y});
                navMenu->addChild(iBtn);
            }
        }

        toggle = CCMenuItemToggler::createWithStandardSprites(this, cb, 0.45f);
        toggle->setPosition({cx + 85.f, y});
        toggle->toggle(value);
        navMenu->addChild(toggle);
    };

    // ── Enable ──
    addTitle("General");
    y -= 18.f;
    addToggle("Enable Pet", m_enableToggle, cfg.enabled,
        menu_selector(PetConfigPopup::onEnableToggled),
        "Turns the pet mascot <cg>ON</c> or <cr>OFF</c>.\nWhen enabled, the pet sprite follows your cursor across all screens.");
    y -= 22.f;

    // ── Size & Movement ──
    addTitle("Size & Movement",
        "<cy>Scale</c>: size of the pet (0.1 = tiny, 3.0 = huge).\n"
        "<cy>Sensitivity</c>: how quickly the pet follows the cursor. Low = lazy, high = snappy.\n"
        "<cy>Opacity</c>: transparency of the pet (0 = invisible, 255 = fully opaque).");
    y -= 16.f;
    addSlider(m_scaleSlider, m_scaleLabel, cfg.scale, 0.1f, 3.f,
        menu_selector(PetConfigPopup::onScaleChanged), "{:.2f}");
    y -= 18.f;
    addSlider(m_sensitivitySlider, m_sensitivityLabel, cfg.sensitivity, 0.01f, 1.f,
        menu_selector(PetConfigPopup::onSensitivityChanged), "{:.2f}");
    y -= 18.f;
    addSlider(m_opacitySlider, m_opacityLabel, static_cast<float>(cfg.opacity), 0.f, 255.f,
        menu_selector(PetConfigPopup::onOpacityChanged), "{:.0f}");
    y -= 22.f;

    // ── Offset ──
    addTitle("Cursor Offset",
        "Offsets the pet position relative to the cursor.\n"
        "<cy>X</c>: horizontal offset (negative = left, positive = right).\n"
        "<cy>Y</c>: vertical offset (positive = above cursor).");
    y -= 16.f;
    addSlider(m_offsetXSlider, m_offsetXLabel, cfg.offsetX, -50.f, 50.f,
        menu_selector(PetConfigPopup::onOffsetXChanged), "{:.0f}");
    y -= 18.f;
    addSlider(m_offsetYSlider, m_offsetYLabel, cfg.offsetY, -50.f, 100.f,
        menu_selector(PetConfigPopup::onOffsetYChanged), "{:.0f}");
    y -= 22.f;

    // ── Bounce ──
    addTitle("Bounce & Animation",
        "Makes the pet bounce up and down as it moves.\n"
        "<cy>Height</c>: how high the bounce goes (pixels).\n"
        "<cy>Speed</c>: how fast it bounces (cycles per second).");
    y -= 16.f;
    addToggle("Bounce", m_bounceToggle, cfg.bounce,
        menu_selector(PetConfigPopup::onBounceToggled),
        "Enables a bouncing motion while the pet follows the cursor.");
    y -= 18.f;
    addSlider(m_bounceHeightSlider, m_bounceHeightLabel, cfg.bounceHeight, 0.f, 20.f,
        menu_selector(PetConfigPopup::onBounceHeightChanged), "{:.1f}");
    y -= 18.f;
    addSlider(m_bounceSpeedSlider, m_bounceSpeedLabel, cfg.bounceSpeed, 0.5f, 10.f,
        menu_selector(PetConfigPopup::onBounceSpeedChanged), "{:.1f}");
    y -= 22.f;

    // ── Idle Animation ──
    addTitle("Idle Animation",
        "Subtle breathing animation when the pet is idle (cursor not moving).\n"
        "<cy>Breath Scale</c>: how much the pet grows/shrinks.\n"
        "<cy>Breath Speed</c>: how fast the breathing cycles.");
    y -= 16.f;
    addToggle("Idle Breathing", m_idleToggle, cfg.idleAnimation,
        menu_selector(PetConfigPopup::onIdleToggled),
        "Enables a gentle breathing animation when idle.\nThe pet scales slightly up and down to look alive.");
    y -= 18.f;
    addSlider(m_breathScaleSlider, m_breathScaleLabel, cfg.idleBreathScale, 0.f, 0.15f,
        menu_selector(PetConfigPopup::onBreathScaleChanged), "{:.3f}");
    y -= 18.f;
    addSlider(m_breathSpeedSlider, m_breathSpeedLabel, cfg.idleBreathSpeed, 0.5f, 5.f,
        menu_selector(PetConfigPopup::onBreathSpeedChanged), "{:.1f}");
    y -= 22.f;

    // ── Rotation ──
    addTitle("Tilt & Rotation",
        "Controls how the pet tilts when moving.\n"
        "<cy>Flip on Direction</c>: mirrors the pet horizontally based on movement direction.\n"
        "<cy>Rotation Damping</c>: how smoothly the tilt returns to center.\n"
        "<cy>Max Tilt</c>: maximum rotation angle in degrees.");
    y -= 16.f;
    addToggle("Flip on Direction", m_flipToggle, cfg.flipOnDirection,
        menu_selector(PetConfigPopup::onFlipToggled),
        "Flips the pet sprite horizontally when it changes direction.\nMakes the pet always face the direction it's moving.");
    y -= 18.f;
    addSlider(m_rotDampSlider, m_rotDampLabel, cfg.rotationDamping, 0.f, 1.f,
        menu_selector(PetConfigPopup::onRotDampChanged), "{:.2f}");
    y -= 18.f;
    addSlider(m_maxTiltSlider, m_maxTiltLabel, cfg.maxTilt, 0.f, 45.f,
        menu_selector(PetConfigPopup::onMaxTiltChanged), "{:.0f}");
    y -= 22.f;

    // ── Trail ──
    addTitle("Trail Effect",
        "Leaves a fading trail behind the pet as it moves.\n"
        "<cy>Length</c>: how long the trail persists.\n"
        "<cy>Width</c>: thickness of the trail.");
    y -= 16.f;
    addToggle("Show Trail", m_trailToggle, cfg.showTrail,
        menu_selector(PetConfigPopup::onTrailToggled),
        "Displays a glowing motion trail behind the pet as it follows the cursor.");
    y -= 18.f;
    addSlider(m_trailLengthSlider, m_trailLengthLabel, cfg.trailLength, 5.f, 100.f,
        menu_selector(PetConfigPopup::onTrailLengthChanged), "{:.0f}");
    y -= 18.f;
    addSlider(m_trailWidthSlider, m_trailWidthLabel, cfg.trailWidth, 1.f, 20.f,
        menu_selector(PetConfigPopup::onTrailWidthChanged), "{:.1f}");
    y -= 22.f;

    // ── Squish ──
    addTitle("Squish on Land",
        "When the pet stops moving, it briefly squishes (flattens) like landing.\n"
        "<cy>Amount</c>: how much it squishes (0 = none, 0.5 = extreme).");
    y -= 16.f;
    addToggle("Squish Effect", m_squishToggle, cfg.squishOnLand,
        menu_selector(PetConfigPopup::onSquishToggled),
        "Adds a cartoon-like squash & stretch when the pet stops moving.\nMakes the pet feel more alive and bouncy.");
    y -= 18.f;
    addSlider(m_squishSlider, m_squishLabel, cfg.squishAmount, 0.f, 0.5f,
        menu_selector(PetConfigPopup::onSquishChanged), "{:.2f}");
    y -= 26.f;

    // ── Visible Layers ──
    addTitle("Visible Layers",
        "Choose which screens the pet appears on.\n"
        "If <cg>all</c> are selected, the pet shows <cg>everywhere</c>.\n"
        "Toggle specific layers off to hide the pet on those screens.");
    y -= 16.f;

    for (auto& layerName : PET_LAYER_OPTIONS) {
        bool isOn = cfg.visibleLayers.count(layerName) > 0;
        auto lbl = CCLabelBMFont::create(layerName.c_str(), "bigFont.fnt");
        lbl->setScale(0.22f);
        lbl->setAnchorPoint({0.f, 0.5f});
        lbl->setPosition({cx - 85.f, y});
        sc->addChild(lbl);

        auto toggle = CCMenuItemToggler::createWithStandardSprites(
            this, menu_selector(PetConfigPopup::onLayerToggled), 0.4f);
        toggle->setPosition({cx + 85.f, y});
        toggle->toggle(isOn);
        // store layer name in tag: use hash
        toggle->setTag(static_cast<int>(std::hash<std::string>{}(layerName) & 0x7FFFFFFF));
        auto* nameStr = CCString::create(layerName);
        toggle->setUserObject(nameStr);
        navMenu->addChild(toggle);
        y -= 16.f;
    }

    m_scrollLayer->moveToTop();

    // scroll indicator arrow
    auto scrollArrow = CCSprite::createWithSpriteFrameName("GJ_arrow_03_001.png");
    if (scrollArrow) {
        scrollArrow->setRotation(-90.f);
        scrollArrow->setScale(0.3f);
        scrollArrow->setOpacity(150);
        scrollArrow->setPosition({content.width / 2.f, 16.f});
        scrollArrow->setID("pet-scroll-arrow"_spr);
        m_settingsTab->addChild(scrollArrow, 20);

        auto bounce = CCRepeatForever::create(CCSequence::create(
            CCMoveBy::create(0.5f, {0, 3.f}),
            CCMoveBy::create(0.5f, {0, -3.f}), nullptr));
        scrollArrow->runAction(bounce);
        m_scrollArrow = scrollArrow;
        this->schedule(schedule_selector(PetConfigPopup::checkScrollPosition), 0.2f);
    }
}

void PetConfigPopup::checkScrollPosition(float dt) {
    if (!m_scrollArrow || !m_scrollLayer) return;
    float totalH = m_scrollLayer->m_contentLayer->getContentSize().height;
    float viewH = m_scrollLayer->getContentSize().height;
    float curY = m_scrollLayer->m_contentLayer->getPositionY();
    bool nearBottom = (curY <= -(totalH - viewH) + 20.f);

    if (nearBottom && m_scrollArrow->getOpacity() > 0) {
        m_scrollArrow->stopAllActions();
        m_scrollArrow->runAction(CCFadeTo::create(0.3f, 0));
    } else if (!nearBottom && m_scrollArrow->getOpacity() < 150) {
        m_scrollArrow->stopAllActions();
        m_scrollArrow->runAction(CCFadeTo::create(0.3f, 150));
        m_scrollArrow->runAction(CCRepeatForever::create(CCSequence::create(
            CCMoveBy::create(0.5f, {0, 3.f}),
            CCMoveBy::create(0.5f, {0, -3.f}), nullptr)));
    }
}

// ════════════════════════════════════════════════════════════
// apply live
// ════════════════════════════════════════════════════════════

void PetConfigPopup::applyLive() {
    auto& pet = PetManager::get();
    pet.applyConfigLive();

    // attach/detach based on enabled
    auto scene = CCDirector::sharedDirector()->getRunningScene();
    if (pet.config().enabled) {
        if (!pet.isAttached() && scene) {
            pet.attachToScene(scene);
        }
    } else {
        pet.detachFromScene();
    }
}

// ════════════════════════════════════════════════════════════
// slider callbacks (all follow same pattern: read -> map -> store -> apply)
// ════════════════════════════════════════════════════════════

// slider helpers: read slider -> map to range -> store in config -> update label -> apply
static float readSlider(Slider* s, float minV, float maxV) {
    float v = s->getThumb()->getValue();
    return minV + v * (maxV - minV);
}

void PetConfigPopup::onScaleChanged(CCObject*) {
    if (!m_scaleSlider) return;
    auto& c = PetManager::get().config();
    c.scale = readSlider(m_scaleSlider, 0.1f, 3.f);
    if (m_scaleLabel) m_scaleLabel->setString(fmt::format("{:.2f}", c.scale).c_str());
    applyLive();
}
void PetConfigPopup::onSensitivityChanged(CCObject*) {
    if (!m_sensitivitySlider) return;
    auto& c = PetManager::get().config();
    c.sensitivity = readSlider(m_sensitivitySlider, 0.01f, 1.f);
    if (m_sensitivityLabel) m_sensitivityLabel->setString(fmt::format("{:.2f}", c.sensitivity).c_str());
    applyLive();
}
void PetConfigPopup::onBounceHeightChanged(CCObject*) {
    if (!m_bounceHeightSlider) return;
    auto& c = PetManager::get().config();
    c.bounceHeight = readSlider(m_bounceHeightSlider, 0.f, 20.f);
    if (m_bounceHeightLabel) m_bounceHeightLabel->setString(fmt::format("{:.1f}", c.bounceHeight).c_str());
    applyLive();
}
void PetConfigPopup::onBounceSpeedChanged(CCObject*) {
    if (!m_bounceSpeedSlider) return;
    auto& c = PetManager::get().config();
    c.bounceSpeed = readSlider(m_bounceSpeedSlider, 0.5f, 10.f);
    if (m_bounceSpeedLabel) m_bounceSpeedLabel->setString(fmt::format("{:.1f}", c.bounceSpeed).c_str());
    applyLive();
}
void PetConfigPopup::onRotDampChanged(CCObject*) {
    if (!m_rotDampSlider) return;
    auto& c = PetManager::get().config();
    c.rotationDamping = readSlider(m_rotDampSlider, 0.f, 1.f);
    if (m_rotDampLabel) m_rotDampLabel->setString(fmt::format("{:.2f}", c.rotationDamping).c_str());
    applyLive();
}
void PetConfigPopup::onMaxTiltChanged(CCObject*) {
    if (!m_maxTiltSlider) return;
    auto& c = PetManager::get().config();
    c.maxTilt = readSlider(m_maxTiltSlider, 0.f, 45.f);
    if (m_maxTiltLabel) m_maxTiltLabel->setString(fmt::format("{:.0f}", c.maxTilt).c_str());
    applyLive();
}
void PetConfigPopup::onTrailLengthChanged(CCObject*) {
    if (!m_trailLengthSlider) return;
    auto& c = PetManager::get().config();
    c.trailLength = readSlider(m_trailLengthSlider, 5.f, 100.f);
    if (m_trailLengthLabel) m_trailLengthLabel->setString(fmt::format("{:.0f}", c.trailLength).c_str());
    applyLive();
}
void PetConfigPopup::onTrailWidthChanged(CCObject*) {
    if (!m_trailWidthSlider) return;
    auto& c = PetManager::get().config();
    c.trailWidth = readSlider(m_trailWidthSlider, 1.f, 20.f);
    if (m_trailWidthLabel) m_trailWidthLabel->setString(fmt::format("{:.1f}", c.trailWidth).c_str());
    applyLive();
}
void PetConfigPopup::onBreathScaleChanged(CCObject*) {
    if (!m_breathScaleSlider) return;
    auto& c = PetManager::get().config();
    c.idleBreathScale = readSlider(m_breathScaleSlider, 0.f, 0.15f);
    if (m_breathScaleLabel) m_breathScaleLabel->setString(fmt::format("{:.3f}", c.idleBreathScale).c_str());
    applyLive();
}
void PetConfigPopup::onBreathSpeedChanged(CCObject*) {
    if (!m_breathSpeedSlider) return;
    auto& c = PetManager::get().config();
    c.idleBreathSpeed = readSlider(m_breathSpeedSlider, 0.5f, 5.f);
    if (m_breathSpeedLabel) m_breathSpeedLabel->setString(fmt::format("{:.1f}", c.idleBreathSpeed).c_str());
    applyLive();
}
void PetConfigPopup::onSquishChanged(CCObject*) {
    if (!m_squishSlider) return;
    auto& c = PetManager::get().config();
    c.squishAmount = readSlider(m_squishSlider, 0.f, 0.5f);
    if (m_squishLabel) m_squishLabel->setString(fmt::format("{:.2f}", c.squishAmount).c_str());
    applyLive();
}

void PetConfigPopup::onOpacityChanged(CCObject*) {
    if (!m_opacitySlider) return;
    float v = m_opacitySlider->getThumb()->getValue();
    auto& cfg = PetManager::get().config();
    cfg.opacity = static_cast<int>(v * 255.f);
    cfg.opacity = std::max(0, std::min(255, cfg.opacity));
    if (m_opacityLabel) m_opacityLabel->setString(fmt::format("{}", cfg.opacity).c_str());
    applyLive();
}

void PetConfigPopup::onOffsetXChanged(CCObject*) {
    if (!m_offsetXSlider) return;
    float v = m_offsetXSlider->getThumb()->getValue();
    auto& cfg = PetManager::get().config();
    cfg.offsetX = -50.f + v * 100.f;
    if (m_offsetXLabel) m_offsetXLabel->setString(fmt::format("{:.0f}", cfg.offsetX).c_str());
    applyLive();
}

void PetConfigPopup::onOffsetYChanged(CCObject*) {
    if (!m_offsetYSlider) return;
    float v = m_offsetYSlider->getThumb()->getValue();
    auto& cfg = PetManager::get().config();
    cfg.offsetY = -50.f + v * 150.f;
    if (m_offsetYLabel) m_offsetYLabel->setString(fmt::format("{:.0f}", cfg.offsetY).c_str());
    applyLive();
}

// ════════════════════════════════════════════════════════════
// toggle callbacks
// ════════════════════════════════════════════════════════════

void PetConfigPopup::onEnableToggled(CCObject*) {
    PetManager::get().config().enabled = !m_enableToggle->isToggled();
    applyLive();
}

void PetConfigPopup::onFlipToggled(CCObject*) {
    PetManager::get().config().flipOnDirection = !m_flipToggle->isToggled();
    applyLive();
}

void PetConfigPopup::onTrailToggled(CCObject*) {
    PetManager::get().config().showTrail = !m_trailToggle->isToggled();
    applyLive();
}

void PetConfigPopup::onIdleToggled(CCObject*) {
    PetManager::get().config().idleAnimation = !m_idleToggle->isToggled();
    applyLive();
}

void PetConfigPopup::onBounceToggled(CCObject*) {
    PetManager::get().config().bounce = !m_bounceToggle->isToggled();
    applyLive();
}

void PetConfigPopup::onSquishToggled(CCObject*) {
    PetManager::get().config().squishOnLand = !m_squishToggle->isToggled();
    applyLive();
}

void PetConfigPopup::onLayerToggled(CCObject* sender) {
    auto toggle = typeinfo_cast<CCMenuItemToggler*>(sender);
    if (!toggle) return;
    auto* nameStr = typeinfo_cast<CCString*>(toggle->getUserObject());
    if (!nameStr) return;

    std::string layerName = nameStr->getCString();
    bool nowOn = !toggle->isToggled(); // before toggle flips

    auto& layers = PetManager::get().config().visibleLayers;
    if (nowOn) {
        layers.insert(layerName);
    } else {
        layers.erase(layerName);
    }
    applyLive();
}

void PetConfigPopup::onOpenShop(CCObject*) {
    auto shop = PaimonShopPopup::create();
    if (shop) shop->show();
}







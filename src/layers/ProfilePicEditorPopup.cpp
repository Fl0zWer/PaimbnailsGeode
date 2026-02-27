#include "ProfilePicEditorPopup.hpp"
#include "../utils/PaimonNotification.hpp"
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/CCMenuItemToggler.hpp>
#include <Geode/binding/Slider.hpp>
#include <Geode/binding/GJAccountManager.hpp>
#include <Geode/ui/Notification.hpp>
#include "../utils/ShapeStencil.hpp"
#include "../utils/AnimatedGIFSprite.hpp"
#include "../managers/ProfileThumbs.hpp"
#include <filesystem>
#include <fstream>

using namespace geode::prelude;
using namespace cocos2d;

// CCScale9Sprite::create crashea si el sprite no existe (no retorna nullptr).
// Esta función verifica primero que la textura sea válida.
static CCScale9Sprite* safeCreateScale9(const char* file) {
    // Intentar cargar la textura sin crear el Scale9
    auto* tex = CCTextureCache::sharedTextureCache()->addImage(file, false);
    if (!tex) return nullptr;
    auto spr = CCScale9Sprite::create(file);
    return spr;
}

ProfilePicEditorPopup* ProfilePicEditorPopup::create() {
    auto ret = new ProfilePicEditorPopup();
    if (ret && ret->init()) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool ProfilePicEditorPopup::init() {
    if (!Popup::init(460.f, 320.f)) return false;
    this->setTitle("Profile Photo Editor");

    // Cargar config actual
    m_editConfig = ProfilePicCustomizer::get().getConfig();

    auto winSize = m_mainLayer->getContentSize();
    float centerX = winSize.width / 2;

    // === Preview area (izquierda) ===
    m_previewContainer = CCNode::create();
    m_previewContainer->setContentSize({140, 140});
    m_previewContainer->setAnchorPoint({0.5f, 0.5f});
    m_previewContainer->setPosition({85, winSize.height / 2});
    m_mainLayer->addChild(m_previewContainer, 10);

    // Fondo del preview
    auto previewBg = safeCreateScale9("square02_001.png");
    if (!previewBg) previewBg = safeCreateScale9("GJ_square01.png");
    if (!previewBg) return true; // no deberia pasar
    previewBg->setContentSize({150, 150});
    previewBg->setColor({0, 0, 0});
    previewBg->setOpacity(80);
    previewBg->setAnchorPoint({0.5f, 0.5f});
    previewBg->setPosition({85, winSize.height / 2});
    m_mainLayer->addChild(previewBg, 5);

    auto previewLabel = CCLabelBMFont::create("Preview", "goldFont.fnt");
    previewLabel->setScale(0.4f);
    previewLabel->setPosition({85, winSize.height / 2 + 85});
    m_mainLayer->addChild(previewLabel, 10);

    // === Panel de controles (derecha) ===
    float panelX = 290;
    float panelW = 300;

    // Fondo del panel
    auto panelBg = safeCreateScale9("square02_001.png");
    if (!panelBg) panelBg = safeCreateScale9("GJ_square01.png");
    if (!panelBg) return true;
    panelBg->setContentSize({panelW, 230});
    panelBg->setColor({0, 0, 0});
    panelBg->setOpacity(50);
    panelBg->setAnchorPoint({0.5f, 0.5f});
    panelBg->setPosition({panelX, winSize.height / 2 + 5});
    m_mainLayer->addChild(panelBg, 5);

    // === Tabs ===
    createTabs();

    // === Crear contenido de cada tab ===
    m_frameTab = CCLayer::create();
    m_frameTab->setContentSize(winSize);
    m_frameTab->addChild(createFrameTab());
    m_mainLayer->addChild(m_frameTab, 10);

    m_shapeTab = CCLayer::create();
    m_shapeTab->setContentSize(winSize);
    m_shapeTab->addChild(createShapeTab());
    m_shapeTab->setVisible(false);
    m_mainLayer->addChild(m_shapeTab, 10);

    m_decoTab = CCLayer::create();
    m_decoTab->setContentSize(winSize);
    m_decoTab->addChild(createDecoTab());
    m_decoTab->setVisible(false);
    m_mainLayer->addChild(m_decoTab, 10);

    // === Botones Save / Reset abajo ===
    auto bottomMenu = CCMenu::create();
    bottomMenu->setPosition({0, 0});
    m_mainLayer->addChild(bottomMenu, 15);

    auto saveSpr = ButtonSprite::create("Save", "goldFont.fnt", "GJ_button_01.png", 0.8f);
    saveSpr->setScale(0.7f);
    auto saveBtn = CCMenuItemSpriteExtra::create(saveSpr, this, menu_selector(ProfilePicEditorPopup::onSave));
    saveBtn->setPosition({centerX - 50, 25});
    bottomMenu->addChild(saveBtn);

    auto resetSpr = ButtonSprite::create("Reset", "goldFont.fnt", "GJ_button_05.png", 0.8f);
    resetSpr->setScale(0.6f);
    auto resetBtn = CCMenuItemSpriteExtra::create(resetSpr, this, menu_selector(ProfilePicEditorPopup::onReset));
    resetBtn->setPosition({centerX + 50, 25});
    bottomMenu->addChild(resetBtn);

    // Build initial preview
    rebuildPreview();

    return true;
}

// === TABS ===

void ProfilePicEditorPopup::createTabs() {
    auto winSize = m_mainLayer->getContentSize();
    float panelX = 290;
    float topY = winSize.height - 40;

    auto tabMenu = CCMenu::create();
    tabMenu->setPosition({0, 0});
    m_mainLayer->addChild(tabMenu, 12);

    const char* tabNames[] = {"Frame", "Shape", "Decorate"};
    float positions[] = {panelX - 90, panelX, panelX + 90};

    for (int i = 0; i < 3; i++) {
        auto spr = ButtonSprite::create(tabNames[i]);
        spr->setScale(0.45f);
        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(ProfilePicEditorPopup::onTabBtn));
        btn->setTag(i);
        btn->setPosition({positions[i], topY});
        tabMenu->addChild(btn);
        m_tabBtns.push_back(btn);
    }

    switchTab(0);
}

void ProfilePicEditorPopup::onTabBtn(CCObject* sender) {
    int tag = static_cast<CCMenuItemSpriteExtra*>(sender)->getTag();
    switchTab(tag);
}

void ProfilePicEditorPopup::switchTab(int tab) {
    m_currentTab = tab;
    if (m_frameTab) m_frameTab->setVisible(tab == 0);
    if (m_shapeTab) m_shapeTab->setVisible(tab == 1);
    if (m_decoTab) m_decoTab->setVisible(tab == 2);

    for (auto btn : m_tabBtns) {
        auto spr = typeinfo_cast<ButtonSprite*>(btn->getNormalImage());
        if (!spr) continue;
        if (btn->getTag() == tab) {
            spr->setColor({0, 255, 0});
            spr->setOpacity(255);
        } else {
            spr->setColor({255, 255, 255});
            spr->setOpacity(150);
        }
    }
}

// === FRAME TAB ===

CCNode* ProfilePicEditorPopup::createFrameTab() {
    auto node = CCNode::create();
    float panelX = 290;
    float startY = m_mainLayer->getContentSize().height - 70;

    auto menu = CCMenu::create();
    menu->setPosition({0, 0});
    node->addChild(menu);

    // Toggle "Enable Border"
    auto frameToggle = CCMenuItemToggler::createWithStandardSprites(
        this, menu_selector(ProfilePicEditorPopup::onFrameToggle), 0.6f);
    frameToggle->toggle(m_editConfig.frameEnabled);
    frameToggle->setPosition({panelX - 100, startY});
    menu->addChild(frameToggle);

    auto enableLbl = CCLabelBMFont::create("Enable Border", "bigFont.fnt");
    enableLbl->setScale(0.35f);
    enableLbl->setAnchorPoint({0, 0.5f});
    enableLbl->setPosition({panelX - 85, startY});
    node->addChild(enableLbl);

    // Thickness slider
    float sliderY = startY - 35;

    auto thkLbl = CCLabelBMFont::create("Thickness", "bigFont.fnt");
    thkLbl->setScale(0.3f);
    thkLbl->setPosition({panelX - 60, sliderY + 12});
    node->addChild(thkLbl);

    m_thicknessSlider = Slider::create(this, menu_selector(ProfilePicEditorPopup::onThicknessChanged), 0.5f);
    m_thicknessSlider->setPosition({panelX + 20, sliderY});
    m_thicknessSlider->setValue(m_editConfig.frame.thickness / 20.f);
    node->addChild(m_thicknessSlider);

    m_thicknessLabel = CCLabelBMFont::create(
        fmt::format("{:.0f}", m_editConfig.frame.thickness).c_str(), "bigFont.fnt");
    m_thicknessLabel->setScale(0.3f);
    m_thicknessLabel->setPosition({panelX + 110, sliderY});
    node->addChild(m_thicknessLabel);

    // Color palette
    float colorY = sliderY - 35;

    auto colorLbl = CCLabelBMFont::create("Border Color", "bigFont.fnt");
    colorLbl->setScale(0.3f);
    colorLbl->setPosition({panelX - 60, colorY + 12});
    node->addChild(colorLbl);

    // Paleta de colores: blanco, negro, rojo, verde, azul, amarillo, naranja, rosa, cyan, morado
    struct ColorOption { ccColor3B color; const char* name; };
    std::vector<ColorOption> colors = {
        {{255, 255, 255}, "White"},
        {{0, 0, 0}, "Black"},
        {{255, 50, 50}, "Red"},
        {{50, 255, 50}, "Green"},
        {{50, 100, 255}, "Blue"},
        {{255, 255, 50}, "Yellow"},
        {{255, 150, 0}, "Orange"},
        {{255, 100, 200}, "Pink"},
        {{0, 220, 220}, "Cyan"},
        {{150, 50, 255}, "Purple"},
        {{255, 215, 0}, "Gold"},
        {{192, 192, 192}, "Silver"},
    };

    float cellSize = 22;
    float gap = 3;
    int maxCols = 6;
    int cCol = 0;
    int cRow = 0;
    float gridX = panelX - 80;

    for (size_t i = 0; i < colors.size(); i++) {
        auto& co = colors[i];

        // Cuadrito de color
        auto colorNode = CCLayerColor::create(ccc4(co.color.r, co.color.g, co.color.b, 255));
        colorNode->setContentSize({cellSize - 2, cellSize - 2});

        // Borde fino para que se vean los colores claros
        auto borderBg = CCLayerColor::create(ccc4(80, 80, 80, 255));
        borderBg->setContentSize({cellSize, cellSize});
        colorNode->setPosition({1, 1});

        auto btnContainer = CCNode::create();
        btnContainer->setContentSize({cellSize, cellSize});
        btnContainer->setAnchorPoint({0.5f, 0.5f});
        btnContainer->addChild(borderBg);
        btnContainer->addChild(colorNode);

        // Highlight si es el color seleccionado
        if (m_editConfig.frame.color.r == co.color.r &&
            m_editConfig.frame.color.g == co.color.g &&
            m_editConfig.frame.color.b == co.color.b) {
            auto highlight = CCLayerColor::create(ccc4(255, 255, 255, 180));
            highlight->setContentSize({cellSize + 2, cellSize + 2});
            highlight->setPosition({-1, -1});
            btnContainer->addChild(highlight, -1);
        }

        auto btn = CCMenuItemSpriteExtra::create(btnContainer, this, menu_selector(ProfilePicEditorPopup::onBorderColorSelect));
        btn->setTag(static_cast<int>(i));
        float bx = gridX + cCol * (cellSize + gap);
        float by = colorY - cRow * (cellSize + gap);
        btn->setPosition({bx, by});
        menu->addChild(btn);

        cCol++;
        if (cCol >= maxCols) { cCol = 0; cRow++; }
    }

    return node;
}

void ProfilePicEditorPopup::onFrameToggle(CCObject* sender) {
    auto toggle = typeinfo_cast<CCMenuItemToggler*>(sender);
    if (!toggle) return;
    m_editConfig.frameEnabled = !toggle->isToggled();
    rebuildPreview();
}

void ProfilePicEditorPopup::onFrameSelect(CCObject* sender) {
    // ya no se usa frame sprites, pero mantener por compatibilidad
}

void ProfilePicEditorPopup::onFrameColorR(CCObject*) {
    m_editConfig.frame.color = {255, 50, 50};
    rebuildPreview();
}

void ProfilePicEditorPopup::onFrameColorG(CCObject*) {
    m_editConfig.frame.color = {50, 255, 50};
    rebuildPreview();
}

void ProfilePicEditorPopup::onFrameColorB(CCObject*) {
    m_editConfig.frame.color = {50, 100, 255};
    rebuildPreview();
}

void ProfilePicEditorPopup::onBorderColorSelect(CCObject* sender) {
    int idx = static_cast<CCMenuItemSpriteExtra*>(sender)->getTag();
    struct ColorOption { ccColor3B color; };
    std::vector<ColorOption> colors = {
        {{255, 255, 255}}, {{0, 0, 0}}, {{255, 50, 50}}, {{50, 255, 50}},
        {{50, 100, 255}}, {{255, 255, 50}}, {{255, 150, 0}}, {{255, 100, 200}},
        {{0, 220, 220}}, {{150, 50, 255}}, {{255, 215, 0}}, {{192, 192, 192}},
    };
    if (idx >= 0 && idx < static_cast<int>(colors.size())) {
        m_editConfig.frame.color = colors[idx].color;
        m_editConfig.frameEnabled = true;
        rebuildPreview();

        // Reconstruir el tab para actualizar el highlight
        if (m_frameTab) {
            m_frameTab->removeAllChildren();
            m_frameTab->addChild(createFrameTab());
        }
    }
}

void ProfilePicEditorPopup::onThicknessChanged(CCObject*) {
    if (!m_thicknessSlider) return;
    float val = m_thicknessSlider->getValue();
    m_editConfig.frame.thickness = val * 20.f;
    if (m_thicknessLabel) {
        m_thicknessLabel->setString(fmt::format("{:.0f}", m_editConfig.frame.thickness).c_str());
    }
    rebuildPreview();
}

// === SHAPE TAB ===

CCNode* ProfilePicEditorPopup::createShapeTab() {
    auto node = CCNode::create();
    float panelX = 290;
    float startY = m_mainLayer->getContentSize().height - 70;

    auto menu = CCMenu::create();
    menu->setPosition({0, 0});
    node->addChild(menu);

    // Scale X slider
    auto sxLbl = CCLabelBMFont::create("Width", "bigFont.fnt");
    sxLbl->setScale(0.3f);
    sxLbl->setPosition({panelX - 110, startY});
    node->addChild(sxLbl);

    m_scaleXSlider = Slider::create(this, menu_selector(ProfilePicEditorPopup::onScaleXChanged), 0.5f);
    m_scaleXSlider->setPosition({panelX + 10, startY});
    m_scaleXSlider->setValue((m_editConfig.scaleX - 0.3f) / 1.7f); // rango 0.3 a 2.0
    node->addChild(m_scaleXSlider);

    m_scaleXLabel = CCLabelBMFont::create(fmt::format("{:.2f}", m_editConfig.scaleX).c_str(), "bigFont.fnt");
    m_scaleXLabel->setScale(0.3f);
    m_scaleXLabel->setPosition({panelX + 115, startY});
    node->addChild(m_scaleXLabel);

    // Scale Y slider
    float yPos = startY - 35;
    auto syLbl = CCLabelBMFont::create("Height", "bigFont.fnt");
    syLbl->setScale(0.3f);
    syLbl->setPosition({panelX - 110, yPos});
    node->addChild(syLbl);

    m_scaleYSlider = Slider::create(this, menu_selector(ProfilePicEditorPopup::onScaleYChanged), 0.5f);
    m_scaleYSlider->setPosition({panelX + 10, yPos});
    m_scaleYSlider->setValue((m_editConfig.scaleY - 0.3f) / 1.7f);
    node->addChild(m_scaleYSlider);

    m_scaleYLabel = CCLabelBMFont::create(fmt::format("{:.2f}", m_editConfig.scaleY).c_str(), "bigFont.fnt");
    m_scaleYLabel->setScale(0.3f);
    m_scaleYLabel->setPosition({panelX + 115, yPos});
    node->addChild(m_scaleYLabel);

    // Size slider
    float sizeY = yPos - 35;
    auto sizeLbl = CCLabelBMFont::create("Size", "bigFont.fnt");
    sizeLbl->setScale(0.3f);
    sizeLbl->setPosition({panelX - 110, sizeY});
    node->addChild(sizeLbl);

    m_sizeSlider = Slider::create(this, menu_selector(ProfilePicEditorPopup::onSizeChanged), 0.5f);
    m_sizeSlider->setPosition({panelX + 10, sizeY});
    m_sizeSlider->setValue((m_editConfig.size - 40.f) / 200.f); // rango 40 a 240
    node->addChild(m_sizeSlider);

    m_sizeLabel = CCLabelBMFont::create(fmt::format("{:.0f}", m_editConfig.size).c_str(), "bigFont.fnt");
    m_sizeLabel->setScale(0.3f);
    m_sizeLabel->setPosition({panelX + 115, sizeY});
    node->addChild(m_sizeLabel);

    // Stencil shape selection (formas geométricas + sprites)
    float shapeY = sizeY - 35;
    auto shapeLbl = CCLabelBMFont::create("Shape", "bigFont.fnt");
    shapeLbl->setScale(0.35f);
    shapeLbl->setPosition({panelX - 110, shapeY - 10});
    node->addChild(shapeLbl);

    auto stencils = ProfilePicCustomizer::getAvailableStencils();
    float cellSz = 28;
    float gapSz = 4;
    int colCount = 0;
    int rowCount = 0;
    int maxCols = 6;
    float gridBaseX = panelX - 70;

    for (size_t i = 0; i < stencils.size(); i++) {
        auto& [shapeName, label] = stencils[i];
        // Crear miniatura de la forma
        auto shapeNode = createShapeStencil(shapeName, cellSz - 4);
        if (!shapeNode) continue;

        // Colorear verde si está seleccionado
        if (m_editConfig.stencilSprite == shapeName) {
            // Iterar hijos para pintar de verde
            for (auto* child : CCArrayExt<CCNode*>(shapeNode->getChildren())) {
                if (auto* drawNode = dynamic_cast<CCDrawNode*>(child)) {
                    // CCDrawNode no tiene setColor fácil, usamos un tinte
                }
                if (auto* s9 = dynamic_cast<CCScale9Sprite*>(child)) {
                    s9->setColor({0, 255, 0});
                }
            }
        }

        // Envolver en un sprite para el botón (CCMenuItemSpriteExtra necesita CCNode derivado de CCSprite-like)
        // Truco: poner un CCLayerColor de fondo y la forma encima
        auto btnContainer = CCNode::create();
        btnContainer->setContentSize({cellSz, cellSz});
        btnContainer->setAnchorPoint({0.5f, 0.5f});

        // Fondo semi-transparente
        auto bgRect = CCLayerColor::create(
            m_editConfig.stencilSprite == shapeName ? ccc4(0, 100, 0, 120) : ccc4(50, 50, 50, 80)
        );
        bgRect->setContentSize({cellSz, cellSz});
        btnContainer->addChild(bgRect);

        shapeNode->setPosition({cellSz / 2, cellSz / 2});
        btnContainer->addChild(shapeNode);

        auto btn = CCMenuItemSpriteExtra::create(btnContainer, this, menu_selector(ProfilePicEditorPopup::onStencilSelect));
        btn->setTag(static_cast<int>(i));
        float bx = gridBaseX + colCount * (cellSz + gapSz);
        float by = shapeY - rowCount * (cellSz + gapSz);
        btn->setPosition({bx, by});
        menu->addChild(btn);

        colCount++;
        if (colCount >= maxCols) { colCount = 0; rowCount++; }
    }

    // Reset button
    float resetY = shapeY - (rowCount + 1) * (cellSz + gapSz) - 10;
    auto resetSpr = ButtonSprite::create("Reset Shape", "bigFont.fnt", "GJ_button_04.png", 0.6f);
    resetSpr->setScale(0.5f);
    auto resetBtn = CCMenuItemSpriteExtra::create(resetSpr, this, menu_selector(ProfilePicEditorPopup::onResetShape));
    resetBtn->setPosition({panelX, resetY});
    menu->addChild(resetBtn);

    return node;
}

void ProfilePicEditorPopup::onScaleXChanged(CCObject*) {
    if (!m_scaleXSlider) return;
    float val = m_scaleXSlider->getValue();
    m_editConfig.scaleX = 0.3f + val * 1.7f;
    if (m_scaleXLabel) m_scaleXLabel->setString(fmt::format("{:.2f}", m_editConfig.scaleX).c_str());
    rebuildPreview();
}

void ProfilePicEditorPopup::onScaleYChanged(CCObject*) {
    if (!m_scaleYSlider) return;
    float val = m_scaleYSlider->getValue();
    m_editConfig.scaleY = 0.3f + val * 1.7f;
    if (m_scaleYLabel) m_scaleYLabel->setString(fmt::format("{:.2f}", m_editConfig.scaleY).c_str());
    rebuildPreview();
}

void ProfilePicEditorPopup::onSizeChanged(CCObject*) {
    if (!m_sizeSlider) return;
    float val = m_sizeSlider->getValue();
    m_editConfig.size = 40.f + val * 200.f;
    if (m_sizeLabel) m_sizeLabel->setString(fmt::format("{:.0f}", m_editConfig.size).c_str());
    rebuildPreview();
}

void ProfilePicEditorPopup::onStencilSelect(CCObject* sender) {
    int idx = static_cast<CCMenuItemSpriteExtra*>(sender)->getTag();
    auto stencils = ProfilePicCustomizer::getAvailableStencils();
    if (idx >= 0 && idx < static_cast<int>(stencils.size())) {
        m_editConfig.stencilSprite = stencils[idx].first;
        rebuildPreview();
    }
}

void ProfilePicEditorPopup::onResetShape(CCObject*) {
    m_editConfig.scaleX = 1.f;
    m_editConfig.scaleY = 1.f;
    m_editConfig.size = 120.f;
    m_editConfig.stencilSprite = "circle";
    if (m_scaleXSlider) m_scaleXSlider->setValue(0.41f); // (1.0 - 0.3) / 1.7
    if (m_scaleYSlider) m_scaleYSlider->setValue(0.41f);
    if (m_sizeSlider) m_sizeSlider->setValue(0.4f); // (120 - 40) / 200
    if (m_scaleXLabel) m_scaleXLabel->setString("1.00");
    if (m_scaleYLabel) m_scaleYLabel->setString("1.00");
    if (m_sizeLabel) m_sizeLabel->setString("120");
    rebuildPreview();
}

// === DECO TAB ===

CCNode* ProfilePicEditorPopup::createDecoTab() {
    auto node = CCNode::create();
    float panelX = 290;
    float startY = m_mainLayer->getContentSize().height - 65;

    auto menu = CCMenu::create();
    menu->setPosition({0, 0});
    node->addChild(menu);

    auto titleLbl = CCLabelBMFont::create("Add Game Assets", "bigFont.fnt");
    titleLbl->setScale(0.35f);
    titleLbl->setPosition({panelX, startY + 5});
    node->addChild(titleLbl);

    // Grid de assets disponibles
    auto decos = ProfilePicCustomizer::getAvailableDecorations();
    float gridStartY = startY - 20;
    float gridX = panelX - 120;
    int col = 0;
    int row = 0;
    float cellSize = 28;
    float gap = 3;
    int maxPerPage = 35; // 7 cols x 5 rows
    int startIdx = m_decoPage * maxPerPage;

    for (int i = startIdx; i < static_cast<int>(decos.size()) && i < startIdx + maxPerPage; i++) {
        auto& [spriteName, label] = decos[i];

        CCNode* sprNode = CCSprite::create(spriteName.c_str());
        if (!sprNode) {
            sprNode = CCSprite::createWithSpriteFrameName(spriteName.c_str());
        }
        if (!sprNode) continue;

        // Escalar para que quepa en la celda
        float maxDim = std::max(sprNode->getContentWidth(), sprNode->getContentHeight());
        if (maxDim > 0) sprNode->setScale((cellSize - 6) / maxDim);

        auto btn = CCMenuItemSpriteExtra::create(
            static_cast<CCSprite*>(sprNode), this, menu_selector(ProfilePicEditorPopup::onAddDeco));
        btn->setTag(i);
        float bx = gridX + col * (cellSize + gap) + cellSize / 2;
        float by = gridStartY - row * (cellSize + gap) - cellSize / 2;
        btn->setPosition({bx, by});
        menu->addChild(btn);

        col++;
        if (col >= 7) { col = 0; row++; }
    }

    // Paging
    float pageY = gridStartY - 5 * (cellSize + gap) - 15;
    if (m_decoPage > 0) {
        auto prevSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_02_001.png");
        if (prevSpr) prevSpr->setScale(0.4f);
        auto prevBtn = CCMenuItemSpriteExtra::create(
            prevSpr ? prevSpr : ButtonSprite::create("<"), this, menu_selector(ProfilePicEditorPopup::onDecoPage));
        prevBtn->setTag(-1);
        prevBtn->setPosition({panelX - 60, pageY});
        menu->addChild(prevBtn);
    }
    if (startIdx + maxPerPage < static_cast<int>(decos.size())) {
        auto nextSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png");
        if (nextSpr) nextSpr->setScale(0.4f);
        auto nextBtn = CCMenuItemSpriteExtra::create(
            nextSpr ? nextSpr : ButtonSprite::create(">"), this, menu_selector(ProfilePicEditorPopup::onDecoPage));
        nextBtn->setTag(1);
        nextBtn->setPosition({panelX + 60, pageY});
        menu->addChild(nextBtn);
    }

    // Info about current decorations
    auto countLbl = CCLabelBMFont::create(
        fmt::format("{} decorations placed", m_editConfig.decorations.size()).c_str(), "chatFont.fnt");
    countLbl->setScale(0.55f);
    countLbl->setColor({200, 200, 200});
    countLbl->setPosition({panelX, pageY - 18});
    node->addChild(countLbl);

    // Remove last button
    if (!m_editConfig.decorations.empty()) {
        auto rmSpr = ButtonSprite::create("Remove Last", "bigFont.fnt", "GJ_button_05.png", 0.6f);
        rmSpr->setScale(0.45f);
        auto rmBtn = CCMenuItemSpriteExtra::create(rmSpr, this, menu_selector(ProfilePicEditorPopup::onRemoveDeco));
        rmBtn->setPosition({panelX, pageY - 38});
        menu->addChild(rmBtn);
    }

    return node;
}

void ProfilePicEditorPopup::onAddDeco(CCObject* sender) {
    int idx = static_cast<CCMenuItemSpriteExtra*>(sender)->getTag();
    auto decos = ProfilePicCustomizer::getAvailableDecorations();
    if (idx < 0 || idx >= static_cast<int>(decos.size())) return;

    if (m_editConfig.decorations.size() >= 20) {
        PaimonNotify::create("Max 20 decorations!", NotificationIcon::Warning)->show();
        return;
    }

    PicDecoration deco;
    deco.spriteName = decos[idx].first;
    // Posición aleatoria alrededor del borde
    float angle = static_cast<float>(rand() % 360) * 3.14159f / 180.f;
    deco.posX = cosf(angle) * 0.7f;
    deco.posY = sinf(angle) * 0.7f;
    deco.scale = 0.6f;
    deco.zOrder = static_cast<int>(m_editConfig.decorations.size());

    m_editConfig.decorations.push_back(deco);
    rebuildPreview();
    refreshDecoList();
}

void ProfilePicEditorPopup::onRemoveDeco(CCObject*) {
    if (m_editConfig.decorations.empty()) return;
    m_editConfig.decorations.pop_back();
    rebuildPreview();
    refreshDecoList();
}

void ProfilePicEditorPopup::onDecoPage(CCObject* sender) {
    int dir = static_cast<CCMenuItemSpriteExtra*>(sender)->getTag();
    m_decoPage = std::max(0, m_decoPage + dir);
    refreshDecoList();
}

void ProfilePicEditorPopup::refreshDecoList() {
    // Reconstruir el tab de decoraciones
    if (m_decoTab) {
        m_decoTab->removeAllChildren();
        m_decoTab->addChild(createDecoTab());
    }
}

// === PREVIEW ===

void ProfilePicEditorPopup::rebuildPreview() {
    if (!m_previewContainer) return;
    m_previewContainer->removeAllChildren();

    float previewSize = 120.f;
    float centerX = m_previewContainer->getContentWidth() / 2;
    float centerY = m_previewContainer->getContentHeight() / 2;
    float thumbSize = m_editConfig.size;

    // Escalar el preview para que siempre quepa
    float previewScale = previewSize / std::max(thumbSize, 1.f);
    previewScale = std::min(previewScale, 1.f);

    // Contenedor con escala/forma aplicada
    auto picNode = CCNode::create();
    picNode->setContentSize({thumbSize, thumbSize});
    picNode->setAnchorPoint({0.5f, 0.5f});
    picNode->setPosition({centerX, centerY});
    picNode->setScaleX(m_editConfig.scaleX * previewScale);
    picNode->setScaleY(m_editConfig.scaleY * previewScale);

    // Stencil con forma geométrica o sprite
    auto stencil = createShapeStencil(m_editConfig.stencilSprite, thumbSize);
    if (!stencil) stencil = createShapeStencil("circle", thumbSize);
    if (!stencil) return;

    auto clip = CCClippingNode::create();
    clip->setStencil(stencil);
    clip->setAlphaThreshold(-1.0f);
    clip->setContentSize({thumbSize, thumbSize});

    // === Intentar cargar la imagen/GIF real del perfil del usuario ===
    int myAccountID = GJAccountManager::get()->m_accountID;
    bool hasContent = false;

    // 1) GIF en cache
    auto* cached = ProfileThumbs::get().getCachedProfile(myAccountID);
    if (cached && !cached->gifKey.empty() && AnimatedGIFSprite::isCached(cached->gifKey)) {
        auto gifSpr = AnimatedGIFSprite::createFromCache(cached->gifKey);
        if (gifSpr) {
            float gw = gifSpr->getContentSize().width;
            float gh = gifSpr->getContentSize().height;
            if (gw > 0 && gh > 0) {
                float scale = std::max(thumbSize / gw, thumbSize / gh);
                gifSpr->setScale(scale);
            }
            gifSpr->setAnchorPoint({0.5f, 0.5f});
            gifSpr->setPosition({thumbSize / 2, thumbSize / 2});
            clip->addChild(gifSpr);
            hasContent = true;
        }
    }

    // 2) Textura en cache de ProfileThumbs
    if (!hasContent && cached && cached->texture) {
        auto imgSpr = CCSprite::createWithTexture(cached->texture);
        if (imgSpr) {
            float iw = imgSpr->getContentWidth();
            float ih = imgSpr->getContentHeight();
            if (iw > 0 && ih > 0) {
                float scale = std::max(thumbSize / iw, thumbSize / ih);
                imgSpr->setScale(scale);
            }
            imgSpr->setAnchorPoint({0.5f, 0.5f});
            imgSpr->setPosition({thumbSize / 2, thumbSize / 2});
            clip->addChild(imgSpr);
            hasContent = true;
        }
    }

    // 3) Cargar desde disco vía ProfileThumbs::loadTexture (RGB guardado)
    if (!hasContent) {
        auto tex = ProfileThumbs::get().loadTexture(myAccountID);
        if (tex) {
            auto imgSpr = CCSprite::createWithTexture(tex);
            if (imgSpr) {
                float iw = imgSpr->getContentWidth();
                float ih = imgSpr->getContentHeight();
                if (iw > 0 && ih > 0) {
                    float scale = std::max(thumbSize / iw, thumbSize / ih);
                    imgSpr->setScale(scale);
                }
                imgSpr->setAnchorPoint({0.5f, 0.5f});
                imgSpr->setPosition({thumbSize / 2, thumbSize / 2});
                clip->addChild(imgSpr);
                hasContent = true;
            }
        }
    }

    // 4) Cargar desde profileimg_cache en disco (datos descargados del servidor)
    if (!hasContent) {
        auto cacheDir = Mod::get()->getSaveDir() / "profileimg_cache";
        auto cachePath = cacheDir / fmt::format("{}.dat", myAccountID);
        if (std::filesystem::exists(cachePath)) {
            std::ifstream file(cachePath, std::ios::binary);
            if (file) {
                std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
                file.close();
                if (!data.empty()) {
                    CCImage img;
                    if (img.initWithImageData(const_cast<uint8_t*>(data.data()), data.size())) {
                        auto* tex = new CCTexture2D();
                        if (tex->initWithImage(&img)) {
                            tex->autorelease();
                            auto imgSpr = CCSprite::createWithTexture(tex);
                            if (imgSpr) {
                                float iw = imgSpr->getContentWidth();
                                float ih = imgSpr->getContentHeight();
                                if (iw > 0 && ih > 0) {
                                    float scale = std::max(thumbSize / iw, thumbSize / ih);
                                    imgSpr->setScale(scale);
                                }
                                imgSpr->setAnchorPoint({0.5f, 0.5f});
                                imgSpr->setPosition({thumbSize / 2, thumbSize / 2});
                                clip->addChild(imgSpr);
                                hasContent = true;
                            }
                        } else {
                            tex->release();
                        }
                    }
                }
            }
        }
    }

    // 5) Placeholder si no hay imagen
    if (!hasContent) {
        auto placeholder = CCLayerColor::create({80, 40, 40, 255});
        placeholder->setContentSize({thumbSize, thumbSize});
        clip->addChild(placeholder);

        auto camIcon = CCLabelBMFont::create("No Image", "bigFont.fnt");
        camIcon->setScale(0.35f);
        camIcon->setPosition({thumbSize / 2, thumbSize / 2});
        camIcon->setColor({255, 255, 255});
        camIcon->setOpacity(120);
        clip->addChild(camIcon);
    }

    picNode->addChild(clip);

    // Marco/borde con la MISMA FORMA que el stencil
    if (m_editConfig.frameEnabled) {
        float frameSize = thumbSize + m_editConfig.frame.thickness * 2;
        auto border = createShapeBorder(
            m_editConfig.stencilSprite, frameSize,
            m_editConfig.frame.thickness,
            m_editConfig.frame.color,
            static_cast<GLubyte>(m_editConfig.frame.opacity)
        );
        if (border) {
            border->setAnchorPoint({0.5f, 0.5f});
            border->setPosition({thumbSize / 2, thumbSize / 2});
            border->setZOrder(-1);
            picNode->addChild(border);
        }
    }

    // Decoraciones
    for (const auto& deco : m_editConfig.decorations) {
        CCSprite* decoSpr = CCSprite::create(deco.spriteName.c_str());
        if (!decoSpr) decoSpr = CCSprite::createWithSpriteFrameName(deco.spriteName.c_str());
        if (!decoSpr) continue;

        decoSpr->setScale(deco.scale);
        decoSpr->setRotation(deco.rotation);
        decoSpr->setColor(deco.color);
        decoSpr->setOpacity(static_cast<GLubyte>(deco.opacity));
        decoSpr->setFlipX(deco.flipX);
        decoSpr->setFlipY(deco.flipY);
        decoSpr->setZOrder(deco.zOrder + 10);

        float dx = thumbSize / 2 + deco.posX * (thumbSize / 2);
        float dy = thumbSize / 2 + deco.posY * (thumbSize / 2);
        decoSpr->setPosition({dx, dy});
        picNode->addChild(decoSpr);
    }

    m_previewContainer->addChild(picNode);
}

// === ACTIONS ===

void ProfilePicEditorPopup::onSave(CCObject*) {
    ProfilePicCustomizer::get().setConfig(m_editConfig);
    ProfilePicCustomizer::get().save();
    ProfilePicCustomizer::get().setDirty(true);
    PaimonNotify::create("Profile photo config saved!", NotificationIcon::Success)->show();
    this->onClose(nullptr);
}

void ProfilePicEditorPopup::onReset(CCObject*) {
    m_editConfig = ProfilePicConfig();
    ProfilePicCustomizer::get().setConfig(m_editConfig);
    ProfilePicCustomizer::get().save();

    // Reset sliders
    if (m_scaleXSlider) m_scaleXSlider->setValue(0.41f);
    if (m_scaleYSlider) m_scaleYSlider->setValue(0.41f);
    if (m_sizeSlider) m_sizeSlider->setValue(0.4f);
    if (m_thicknessSlider) m_thicknessSlider->setValue(0.2f);
    if (m_scaleXLabel) m_scaleXLabel->setString("1.00");
    if (m_scaleYLabel) m_scaleYLabel->setString("1.00");
    if (m_sizeLabel) m_sizeLabel->setString("120");
    if (m_thicknessLabel) m_thicknessLabel->setString("4");

    rebuildPreview();
    PaimonNotify::create("Photo config reset!", NotificationIcon::Success)->show();
}

CCMenuItemSpriteExtra* ProfilePicEditorPopup::makeBtn(const char* text, SEL_MenuHandler sel, CCNode* parent, float scale) {
    auto spr = ButtonSprite::create(text);
    spr->setScale(scale);
    auto btn = CCMenuItemSpriteExtra::create(spr, this, sel);
    parent->addChild(btn);
    return btn;
}

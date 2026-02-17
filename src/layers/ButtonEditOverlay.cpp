#include "ButtonEditOverlay.hpp"
#include "../utils/PaimonButtonHighlighter.hpp"
#include "../managers/ButtonLayoutManager.hpp"
#include <Geode/binding/ButtonSprite.hpp>
#include "../utils/Localization.hpp"
#include <Geode/loader/Log.hpp>
#include <cocos-ext.h>

using namespace cocos2d;
using namespace geode::prelude;
using namespace cocos2d::extension;

ButtonEditOverlay* ButtonEditOverlay::create(const std::string& sceneKey, CCMenu* menu) {
    auto ret = new ButtonEditOverlay();
    if (ret && ret->init(sceneKey, menu)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool ButtonEditOverlay::init(const std::string& sceneKey, CCMenu* menu) {
    if (!CCLayer::init()) return false;

    m_sceneKey = sceneKey;
    m_targetMenu = menu;
    if (m_targetMenu) m_targetMenu->retain();
    m_selectedButton = nullptr;
    m_draggedButton = nullptr;

    // cachear winsize para evitar multiples llamadas
    const auto winSize = CCDirector::sharedDirector()->getWinSize();
    
    m_darkBG = CCLayerColor::create(ccc4(0, 0, 0, 120));
    m_darkBG->setContentSize(winSize);
    m_darkBG->setZOrder(-1);
    this->addChild(m_darkBG);
    
    collectEditableButtons();
    
    for (auto& btn : m_editableButtons) {
        if (btn.item && btn.item->getParent()) {
            btn.item->setZOrder(1000);
        }
    }
    
    createControls();
    showControls(false);
    
    m_selectionHighlight = CCScale9Sprite::create("square02b_001.png");
    // ccscale9sprite siempre implementa ccrgbaprotocol, no necesita cast
    m_selectionHighlight->setColor(ccColor3B{100, 255, 100});
    m_selectionHighlight->setOpacity(150);
    m_selectionHighlight->setVisible(false);
    m_selectionHighlight->setZOrder(999);
    
    if (m_targetMenu && m_targetMenu->getParent()) {
        m_targetMenu->getParent()->addChild(m_selectionHighlight, 999);
    }

    createAllHighlights();
    createSnapGuides();

    this->setTouchEnabled(true);
    this->setTouchMode(kCCTouchesOneByOne);
    this->setTouchPriority(-500);
    this->scheduleUpdate();
    
    return true;
}

ButtonEditOverlay::~ButtonEditOverlay() {
    // seguridad: aseguro que el resaltado este separado y el menu retenido libre
    if (m_selectionHighlight && m_selectionHighlight->getParent()) {
        m_selectionHighlight->removeFromParent();
    }
    clearAllHighlights();
    if (m_targetMenu) {
        m_targetMenu->release();
        m_targetMenu = nullptr;
    }
}

void ButtonEditOverlay::collectEditableButtons() {
    m_editableButtons.clear();

    if (!m_targetMenu) return;

    auto children = m_targetMenu->getChildren();
    if (!children) return;

    // reservar memoria anticipadamente para evitar realocaciones
    m_editableButtons.reserve(children->count());

    for (auto* child : CCArrayExt<CCObject*>(children)) {
        auto item = typeinfo_cast<CCMenuItem*>(child);
        if (!item) continue;

        // getid() devuelve string_view en geode, mas eficiente
        auto buttonID = item->getID();
        if (buttonID.empty()) continue;

        EditableButton editable;
        editable.item = item;
        editable.buttonID = std::string(buttonID);
        editable.originalPos = item->getPosition();
        editable.originalScale = item->getScale();
        editable.originalOpacity = item->getOpacity() / 255.0f;

        m_editableButtons.push_back(std::move(editable));
    }

    log::info("[ButtonEditOverlay] Collected {} editable buttons", m_editableButtons.size());
}

void ButtonEditOverlay::createControls() {
    const auto winSize = CCDirector::sharedDirector()->getWinSize();

    m_controlsMenu = CCMenu::create();
    m_controlsMenu->setPosition(CCPointZero);  // usar constante de cocos2d
    m_controlsMenu->setZOrder(1001);
    this->addChild(m_controlsMenu);

    // panel de controles en la parte inferior
    const float panelHeight = 100.f;
    const float panelY = panelHeight / 2.f + 10.f;
    const float centerX = winSize.width / 2.f;

    // fondo panel
    auto panelBg = CCScale9Sprite::create("square02b_001.png");
    panelBg->setContentSize({winSize.width - 20.f, panelHeight});
    panelBg->setPosition({centerX, panelY});
    panelBg->setColor({0, 0, 0});
    panelBg->setOpacity(200);
    this->addChild(panelBg, -1);

    // titulo panel
    const float titleY = panelY + panelHeight / 2.f - 15.f;
    auto titleLabel = CCLabelBMFont::create(Localization::get().getString("edit.title").c_str(), "bigFont.fnt");
    titleLabel->setScale(0.55f);
    titleLabel->setPosition({centerX, titleY});
    this->addChild(titleLabel);

    // sliders + botones
    const float contentStartY = panelY + 10.f;
    const float row1Y = contentStartY;          // fila de scale
    const float row2Y = contentStartY - 35.f;   // fila de opacity

    // posiciones para los elementos de sliders
    const float labelX = 30.f;
    const float sliderX = centerX - 60.f;
    const float valueX = sliderX + 130.f;

    // --- escala ---
    auto scaleText = CCLabelBMFont::create(Localization::get().getString("edit.scale").c_str(), "goldFont.fnt");
    scaleText->setScale(0.5f);
    scaleText->setAnchorPoint({0.f, 0.5f});
    scaleText->setPosition({labelX, row1Y});
    this->addChild(scaleText);
    
    m_scaleSlider = Slider::create(this, menu_selector(ButtonEditOverlay::onScaleChanged));
    m_scaleSlider->setPosition({sliderX, row1Y});
    m_scaleSlider->setScale(0.8f);
    m_scaleSlider->setValue(0.5f);
    this->addChild(m_scaleSlider);

    m_scaleLabel = CCLabelBMFont::create("1.00x", "bigFont.fnt");
    m_scaleLabel->setScale(0.4f);
    m_scaleLabel->setAnchorPoint({0.f, 0.5f});
    m_scaleLabel->setPosition({valueX, row1Y});
    this->addChild(m_scaleLabel);

    // --- opacidad ---
    auto opacityText = CCLabelBMFont::create(Localization::get().getString("edit.opacity").c_str(), "goldFont.fnt");
    opacityText->setScale(0.5f);
    opacityText->setAnchorPoint({0.f, 0.5f});
    opacityText->setPosition({labelX, row2Y});
    this->addChild(opacityText);

    m_opacitySlider = Slider::create(this, menu_selector(ButtonEditOverlay::onOpacityChanged));
    m_opacitySlider->setPosition({sliderX, row2Y});
    m_opacitySlider->setScale(0.8f);
    m_opacitySlider->setValue(1.0f);
    this->addChild(m_opacitySlider);

    m_opacityLabel = CCLabelBMFont::create("100%", "bigFont.fnt");
    m_opacityLabel->setScale(0.4f);
    m_opacityLabel->setAnchorPoint({0.f, 0.5f});
    m_opacityLabel->setPosition({valueX, row2Y});
    this->addChild(m_opacityLabel);

    // botones accion
    const float btnX = winSize.width - 70.f;
    const float btnCenterY = panelY - 5.f;

    auto acceptSpr = ButtonSprite::create(Localization::get().getString("edit.accept").c_str(), 80, true, "bigFont.fnt", "GJ_button_01.png", 28.f, 0.6f);
    auto acceptBtn = CCMenuItemSpriteExtra::create(acceptSpr, this, menu_selector(ButtonEditOverlay::onAccept));
    acceptBtn->setPosition({btnX, btnCenterY + 20.f});
    m_controlsMenu->addChild(acceptBtn);

    auto resetSpr = ButtonSprite::create(Localization::get().getString("edit.reset").c_str(), 80, true, "bigFont.fnt", "GJ_button_06.png", 28.f, 0.6f);
    auto resetBtn = CCMenuItemSpriteExtra::create(resetSpr, this, menu_selector(ButtonEditOverlay::onReset));
    resetBtn->setPosition({btnX, btnCenterY - 20.f});
    m_controlsMenu->addChild(resetBtn);

    // instruccion arriba
    auto instrLabel = CCLabelBMFont::create("Drag buttons to move them", "chatFont.fnt");
    instrLabel->setScale(0.6f);
    instrLabel->setPosition({centerX, winSize.height - 20.f});
    instrLabel->setColor({220, 220, 220});
    this->addChild(instrLabel);
}

void ButtonEditOverlay::showControls(bool show) {
    if (m_scaleSlider) m_scaleSlider->setVisible(show);
    if (m_opacitySlider) m_opacitySlider->setVisible(show);
    if (m_scaleLabel) m_scaleLabel->setVisible(show);
    if (m_opacityLabel) m_opacityLabel->setVisible(show);
}

void ButtonEditOverlay::update(float) {
    // menu invalido, cierro
    if (!m_targetMenu || !m_targetMenu->getParent()) {
        log::warn("[ButtonEditOverlay] Target menu no longer valid; closing editor to avoid crash");
        m_isClosing = true;
        // desactivo controles
        if (m_controlsMenu) m_controlsMenu->setTouchEnabled(false);
        this->setTouchEnabled(false);
        // elimino el resaltado de seleccion si todavia esta adjunto
        if (m_selectionHighlight && m_selectionHighlight->getParent()) {
            m_selectionHighlight->removeFromParent();
        }
        clearAllHighlights();
        // elimino superposicion
        this->unscheduleUpdate();
        this->removeFromParent();
        return;
    }

    // mantengo todos los resaltados por boton sincronizados con sus elementos
    updateAllHighlights();
}

void ButtonEditOverlay::selectButton(EditableButton* btn) {
    m_selectedButton = btn;
    
    if (!btn) {
        showControls(false);
        m_selectionHighlight->setVisible(false);
        return;
    }

    showControls(true);
    
    // establezco valores del deslizador desde el estado actual del boton
    float currentScale = btn->item->getScale();
    float currentOpacity = btn->item->getOpacity() / 255.0f;
    
    // mapeo escala [0.3, 2.0] a deslizador [0, 1]
    float scaleNorm = (currentScale - 0.3f) / 1.7f;
    m_scaleSlider->setValue(std::max(0.f, std::min(1.f, scaleNorm)));
    
    m_opacitySlider->setValue(currentOpacity);
    
    updateSliderLabels();
    updateSelectionHighlight();
    updateAllHighlights();
}

void ButtonEditOverlay::updateSelectionHighlight() {
    if (!m_selectedButton || !m_selectionHighlight) return;
    
    auto item = m_selectedButton->item;
    auto contentSize = item->getContentSize();
    float scale = item->getScale();
    
    m_selectionHighlight->setContentSize({
        contentSize.width * scale + 10.f,
        contentSize.height * scale + 10.f
    });
    
    auto worldPos = item->getParent()->convertToWorldSpace(item->getPosition());
    m_selectionHighlight->setPosition(worldPos);
    m_selectionHighlight->setVisible(true);
}

void ButtonEditOverlay::updateSliderLabels() {
    if (!m_selectedButton) return;
    
    float scale = m_selectedButton->item->getScale();
    float opacity = m_selectedButton->item->getOpacity() / 255.0f * 100.0f;
    
    m_scaleLabel->setString(fmt::format("{:.2f}x", scale).c_str());
    m_opacityLabel->setString(fmt::format("{:.0f}%", opacity).c_str());
}

EditableButton* ButtonEditOverlay::findButtonAtPoint(CCPoint worldPos) {
    for (auto& btn : m_editableButtons) {
        if (!btn.item) continue;
        
        auto parent = btn.item->getParent();
        if (!parent) continue;
        
        auto localPos = parent->convertToNodeSpace(worldPos);
        auto bbox = btn.item->boundingBox();
        
        if (bbox.containsPoint(localPos)) {
            return &btn;
        }
    }
    return nullptr;
}

bool ButtonEditOverlay::ccTouchBegan(CCTouch* touch, CCEvent* event) {
    auto touchPos = touch->getLocation();
    auto foundBtn = findButtonAtPoint(touchPos);
    
    if (foundBtn) {
        m_draggedButton = foundBtn;
        m_dragStartPos = touchPos;
        m_originalButtonPos = foundBtn->item->getPosition();
        
        // selecciono
        selectButton(foundBtn);
        
        return true;
    }
    
    // no toco btn, deselecciono
    selectButton(nullptr);
    return true;
}

void ButtonEditOverlay::ccTouchMoved(CCTouch* touch, CCEvent* event) {
    if (!m_draggedButton || !m_draggedButton->item) return;

    const auto touchPos = touch->getLocation();
    const auto delta = ccpSub(touchPos, m_dragStartPos);
    auto newPos = ccpAdd(m_originalButtonPos, delta);
    
    // snap
    newPos = applySnap(newPos);

    m_draggedButton->item->setPosition(newPos);
    updateSelectionHighlight();
    updateAllHighlights();
}

void ButtonEditOverlay::ccTouchEnded(CCTouch* touch, CCEvent* event) {
    m_draggedButton = nullptr;
    hideSnapGuides();
}

void ButtonEditOverlay::ccTouchCancelled(CCTouch* touch, CCEvent* event) {
    m_draggedButton = nullptr;
    hideSnapGuides();
}

void ButtonEditOverlay::onScaleChanged(CCObject*) {
    if (!m_selectedButton || !m_selectedButton->item) return;

    const float sliderValue = m_scaleSlider->getValue();
    const float scale = 0.3f + sliderValue * 1.7f; // map [0,1] a [0.3, 2.0]

    m_selectedButton->item->setScale(scale);

    // importante: actualizar m_basescale para que el hover no resetee la escala
    // cachear el cast si es ccmenuitemspriteextra
    if (auto menuItem = typeinfo_cast<CCMenuItemSpriteExtra*>(m_selectedButton->item)) {
        menuItem->m_baseScale = scale;
    }

    updateSliderLabels();
    updateSelectionHighlight();
}

void ButtonEditOverlay::onOpacityChanged(CCObject*) {
    if (!m_selectedButton || !m_selectedButton->item) return;
    
    float opacity = m_opacitySlider->getValue();
    m_selectedButton->item->setOpacity(static_cast<GLubyte>(opacity * 255));
    updateSliderLabels();
}

void ButtonEditOverlay::onAccept(CCObject*) {
    if (m_isClosing) {
        // ya se esta cerrando; ignorar accion
        return;
    }
    // si el contexto se ha ido (navegacion a otra parte), cierro de forma segura
    if (!m_targetMenu || !m_targetMenu->getParent()) {
        log::warn("[ButtonEditOverlay] Accept pressed after leaving page; closing without saving");
        if (m_selectionHighlight && m_selectionHighlight->getParent()) {
            m_selectionHighlight->removeFromParent();
        }
        m_isClosing = true;
        this->removeFromParent();
        return;
    }

    // guardo todos los disenos de botones del menu actual por id
    if (auto children = m_targetMenu->getChildren()) {
        for (auto obj : CCArrayExt<CCObject*>(children)) {
            auto item = typeinfo_cast<CCMenuItemSpriteExtra*>(obj);
            if (!item) continue;
            std::string id = item->getID();
            if (id.empty()) continue;

            ButtonLayout layout;
            layout.position = item->getPosition();
            layout.scale = item->getScale();
            layout.opacity = item->getOpacity() / 255.0f;

            // guardar el layout
            ButtonLayoutManager::get().setLayout(m_sceneKey, id, layout);

            // importante: actualizar m_basescale para que el hover no resetee la escala
            item->m_baseScale = layout.scale;

            log::info("[ButtonEditOverlay] Saved button '{}': pos({}, {}), scale={}, opacity={}",
                id, layout.position.x, layout.position.y, layout.scale, layout.opacity);
        }
    }
    
    // elimino resaltado de seleccion
    if (m_selectionHighlight && m_selectionHighlight->getParent()) {
        m_selectionHighlight->removeFromParent();
    }
    clearAllHighlights();

    // elimino superposicion
    m_isClosing = true;
    this->removeFromParent();
}

void ButtonEditOverlay::onReset(CCObject*) {
    if (m_isClosing) {
        return;
    }
    // si el contexto se ha ido, solo limpio disenos guardados y cierro
    if (!m_targetMenu || !m_targetMenu->getParent()) {
        ButtonLayoutManager::get().resetScene(m_sceneKey);
        ButtonLayoutManager::get().save();
        if (m_selectionHighlight && m_selectionHighlight->getParent()) {
            m_selectionHighlight->removeFromParent();
        }
        m_isClosing = true;
        this->removeFromParent();
        return;
    }

    // restauro botones por defecto persistentes cuando esten disponibles, sino a originales capturados
    for (auto& btn : m_editableButtons) {
        if (btn.buttonID.empty()) continue;
        auto node = m_targetMenu->getChildByID(btn.buttonID);
        auto item = typeinfo_cast<CCMenuItemSpriteExtra*>(node);
        if (!item) continue;

        auto def = ButtonLayoutManager::get().getDefaultLayout(m_sceneKey, btn.buttonID);
        float newScale;
        if (def) {
            item->setPosition(def->position);
            item->setScale(def->scale);
            item->setOpacity(static_cast<GLubyte>(def->opacity * 255));
            newScale = def->scale;
        } else {
            item->setPosition(btn.originalPos);
            item->setScale(btn.originalScale);
            item->setOpacity(static_cast<GLubyte>(btn.originalOpacity * 255));
            newScale = btn.originalScale;
        }

        // importante: actualizar m_basescale para que el hover no resetee la escala
        item->m_baseScale = newScale;
    }

    // limpio disenos guardados para esta escena y persisto
    ButtonLayoutManager::get().resetScene(m_sceneKey);
    ButtonLayoutManager::get().save();

    // deselecciono
    selectButton(nullptr);
    updateAllHighlights();
}

void ButtonEditOverlay::createAllHighlights() {
    clearAllHighlights();
    if (!m_targetMenu || !m_targetMenu->getParent()) return;

    auto parent = m_targetMenu->getParent();
    // reservar espacio en el mapa para evitar rehashing
    m_buttonHighlights.reserve(m_editableButtons.size());

    for (auto& btn : m_editableButtons) {
        if (!btn.item || btn.buttonID.empty()) continue;
        auto spr = CCScale9Sprite::create("square02b_001.png");
        if (!spr) continue;

        // ccscale9sprite siempre implementa ccrgbaprotocol
        spr->setColor(ccColor3B{80, 180, 255});
        spr->setOpacity(120);
        spr->setZOrder(998);
        parent->addChild(spr, 998);
        m_buttonHighlights[btn.buttonID] = spr;
    }
    updateAllHighlights();
}

void ButtonEditOverlay::updateAllHighlights() {
    if (!m_targetMenu) return;
    for (auto& btn : m_editableButtons) {
        if (!btn.item || btn.buttonID.empty()) continue;
        auto it = m_buttonHighlights.find(btn.buttonID);
        if (it == m_buttonHighlights.end()) continue;
        auto node = it->second;
        if (!node) continue;

        auto contentSize = btn.item->getContentSize();
        float scale = btn.item->getScale();
        node->setContentSize({ contentSize.width * scale + 10.f, contentSize.height * scale + 10.f });

        if (auto parent = btn.item->getParent()) {
            auto worldPos = parent->convertToWorldSpace(btn.item->getPosition());
            node->setPosition(worldPos);
            node->setVisible(true);
        } else {
            node->setVisible(false);
        }
    }
}

void ButtonEditOverlay::clearAllHighlights() {
    for (auto it = m_buttonHighlights.begin(); it != m_buttonHighlights.end(); ++it) {
        auto node = it->second;
        if (node && node->getParent()) {
            node->removeFromParent();
        }
    }
    m_buttonHighlights.clear();
}

// snap guides
void ButtonEditOverlay::createSnapGuides() {
    auto winSize = CCDirector::sharedDirector()->getWinSize();

    // guia x
    m_snapGuideX = CCDrawNode::create();
    m_snapGuideX->setZOrder(2000);
    m_snapGuideX->setVisible(false);
    this->addChild(m_snapGuideX);

    // guia y
    m_snapGuideY = CCDrawNode::create();
    m_snapGuideY->setZOrder(2000);
    m_snapGuideY->setVisible(false);
    this->addChild(m_snapGuideY);
}

void ButtonEditOverlay::updateSnapGuides(bool showX, bool showY, float snapX, float snapY) {
    auto winSize = CCDirector::sharedDirector()->getWinSize();

    // linea x
    if (m_snapGuideX) {
        m_snapGuideX->clear();
        if (showX) {
            m_snapGuideX->drawSegment(
                ccp(snapX, 0),
                ccp(snapX, winSize.height),
                1.0f,
                ccc4f(0.0f, 1.0f, 0.5f, 0.8f) // verde brillante
            );
            m_snapGuideX->setVisible(true);
        } else {
            m_snapGuideX->setVisible(false);
        }
    }

    // linea y
    if (m_snapGuideY) {
        m_snapGuideY->clear();
        if (showY) {
            m_snapGuideY->drawSegment(
                ccp(0, snapY),
                ccp(winSize.width, snapY),
                1.0f,
                ccc4f(0.0f, 1.0f, 0.5f, 0.8f) // verde brillante
            );
            m_snapGuideY->setVisible(true);
        } else {
            m_snapGuideY->setVisible(false);
        }
    }
}

void ButtonEditOverlay::hideSnapGuides() {
    if (m_snapGuideX) m_snapGuideX->setVisible(false);
    if (m_snapGuideY) m_snapGuideY->setVisible(false);
    m_snappedX = false;
    m_snappedY = false;
}

CCPoint ButtonEditOverlay::applySnap(CCPoint pos) {
    if (!m_draggedButton || !m_targetMenu) return pos;

    float bestSnapX = pos.x;
    float bestSnapY = pos.y;
    float minDistX = m_snapThreshold + 1.0f;
    float minDistY = m_snapThreshold + 1.0f;
    bool foundSnapX = false;
    bool foundSnapY = false;

    // buscar otros para alinear
    for (auto& btn : m_editableButtons) {
        // no comparar con el que arrastro
        if (&btn == m_draggedButton || !btn.item) continue;

        CCPoint otherPos = btn.item->getPosition();

        // alineacion x
        float distX = std::abs(pos.x - otherPos.x);
        if (distX < m_snapThreshold && distX < minDistX) {
            minDistX = distX;
            bestSnapX = otherPos.x;
            foundSnapX = true;
        }

        // alineacion y
        float distY = std::abs(pos.y - otherPos.y);
        if (distY < m_snapThreshold && distY < minDistY) {
            minDistY = distY;
            bestSnapY = otherPos.y;
            foundSnapY = true;
        }
    }

    // aplicar snap
    CCPoint result = pos;
    if (foundSnapX) {
        result.x = bestSnapX;
    }
    if (foundSnapY) {
        result.y = bestSnapY;
    }

    // pos mundo para guias
    float guideX = bestSnapX;
    float guideY = bestSnapY;
    if (m_targetMenu && m_targetMenu->getParent()) {
        auto worldPos = m_targetMenu->getParent()->convertToWorldSpace(
            m_targetMenu->convertToWorldSpace(ccp(bestSnapX, bestSnapY))
        );
        // pos del menu
        auto menuWorldPos = m_targetMenu->convertToWorldSpace(ccp(bestSnapX, bestSnapY));
        guideX = menuWorldPos.x;
        guideY = menuWorldPos.y;
    }

    // actualizo guias
    updateSnapGuides(foundSnapX, foundSnapY, guideX, guideY);

    m_snappedX = foundSnapX;
    m_snappedY = foundSnapY;

    return result;
}

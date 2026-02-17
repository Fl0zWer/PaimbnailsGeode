#include "CaptureLayerEditorPopup.hpp"
#include "CapturePreviewPopup.hpp"
#include "../utils/Localization.hpp"
#include "../utils/PaimonButtonHighlighter.hpp"
#include "../utils/FramebufferCapture.hpp"
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/binding/CCMenuItemToggler.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/PlayLayer.hpp>
#include <Geode/binding/PlayerObject.hpp>
#include <Geode/binding/ShaderLayer.hpp>
#include <Geode/binding/FLAlertLayer.hpp>
#include <Geode/cocos/platform/CCGL.h>
#include <Geode/cocos/kazmath/include/kazmath/GL/matrix.h>
#include <set>

using namespace geode::prelude;
using namespace cocos2d;

// guarda visibilidad original de capas
// se limpia solo al llamar restoreAllLayers()
static std::vector<std::pair<CCNode*, bool>> s_originalVisibilities;

// ─── auxiliares ───────────────────────────────────────────────────

static std::string simplifyClassName(const std::string& cls) {
    std::string name = cls;

    for (const char* prefix : {"class ", "struct "}) {
        if (name.find(prefix) == 0) {
            name = name.substr(std::strlen(prefix));
        }
    }

    auto pos = name.find('<');
    if (pos != std::string::npos) name = name.substr(0, pos);

    if (name.length() > 22) name = name.substr(0, 19) + "...";
    return name;
}

// ─── api estatica ────────────────────────────────────────────────

CaptureLayerEditorPopup* CaptureLayerEditorPopup::create(CapturePreviewPopup* previewPopup) {
    auto ret = new CaptureLayerEditorPopup();
    ret->m_previewPopup = previewPopup;
    if (ret->init()) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

void CaptureLayerEditorPopup::restoreAllLayers() {
    for (auto& [node, vis] : s_originalVisibilities) {
        if (node) {
            try { node->setVisible(vis); } catch (...) {}
        }
    }
    s_originalVisibilities.clear();
    log::info("[LayerEditor] All layers restored to original visibility");
}

// ─── inicio ──────────────────────────────────────────────────────

bool CaptureLayerEditorPopup::init() {
    if (!Popup::init(280.f, 280.f)) return false;

    this->setTitle(Localization::get().getString("layers.title").c_str());

    auto content = this->m_mainLayer->getContentSize();

    populateLayers();

    if (m_layers.empty()) {
        auto noLabel = CCLabelBMFont::create(
            Localization::get().getString("layers.no_playlayer").c_str(),
            "bigFont.fnt"
        );
        noLabel->setScale(0.4f);
        noLabel->setPosition({content.width * 0.5f, content.height * 0.5f});
        this->m_mainLayer->addChild(noLabel);
        return true;
    }

    // mini vista previa arriba
    const float previewW = 160.f;
    const float previewH = 90.f;
    const float previewY = content.height - 32.f - previewH * 0.5f - 2.f;

    auto previewBg = CCLayerColor::create({0, 0, 0, 120});
    previewBg->setContentSize({previewW + 4.f, previewH + 4.f});
    previewBg->ignoreAnchorPointForPosition(false);
    previewBg->setAnchorPoint({0.5f, 0.5f});
    previewBg->setPosition({content.width * 0.5f, previewY});
    this->m_mainLayer->addChild(previewBg, 0);

    m_miniPreview = CCSprite::create();
    m_miniPreview->setContentSize({previewW, previewH});
    m_miniPreview->setPosition({content.width * 0.5f, previewY});
    this->m_mainLayer->addChild(m_miniPreview, 1);

    updateMiniPreview();

    // lista de capas desplazable
    buildList();

    // botones inferiores: restaurar | listo
    auto btnMenu = CCMenu::create();
    btnMenu->setPosition({content.width * 0.5f, 22.f});

    auto restoreSpr = ButtonSprite::create(
        Localization::get().getString("layers.restore_all").c_str(),
        70, true, "bigFont.fnt", "GJ_button_01.png", 22.f, 0.4f
    );
    if (restoreSpr) {
        auto btn = CCMenuItemSpriteExtra::create(
            restoreSpr, this,
            menu_selector(CaptureLayerEditorPopup::onRestoreAllBtn)
        );
        PaimonButtonHighlighter::registerButton(btn);
        btnMenu->addChild(btn);
    }

    auto doneSpr = ButtonSprite::create(
        Localization::get().getString("layers.done").c_str(),
        70, true, "bigFont.fnt", "GJ_button_02.png", 22.f, 0.4f
    );
    if (doneSpr) {
        auto btn = CCMenuItemSpriteExtra::create(
            doneSpr, this,
            menu_selector(CaptureLayerEditorPopup::onDoneBtn)
        );
        PaimonButtonHighlighter::registerButton(btn);
        btnMenu->addChild(btn);
    }

    btnMenu->alignItemsHorizontallyWithPadding(8.f);
    this->m_mainLayer->addChild(btnMenu);

    return true;
}

// ─── enumeracion de capas ─────────────────────────────────────────

void CaptureLayerEditorPopup::populateLayers() {
    auto* pl = PlayLayer::get();
    if (!pl) return;

    auto* scene = CCDirector::sharedDirector()->getRunningScene();

    m_layers.clear();

    bool needRecordOriginals = s_originalVisibilities.empty();
    std::set<CCNode*> added;

    // registra nodo con visibilidad real (sin ocultar auto)
    // editor muestra lo que hay en pantalla; usuario decide
    auto addEntry = [&](CCNode* node, const std::string& name) {
        if (!node || added.count(node)) return;
        added.insert(node);
        if (needRecordOriginals) {
            s_originalVisibilities.push_back({node, node->isVisible()});
        }
        m_layers.push_back({node, name, node->isVisible()});
    };

    // 1. elementos de juego
    addEntry(pl->m_player1,     Localization::get().getString("layers.player1"));
    addEntry(pl->m_player2,     Localization::get().getString("layers.player2"));
    addEntry(pl->m_shaderLayer, Localization::get().getString("layers.effects"));

    // 2. miembros ui conocidos
    addEntry(pl->m_attemptLabel,    "attempt-label");
    addEntry(pl->m_percentageLabel, "percentage-label");

    // 3. uilayer (maestro) + hijos
    // uilayer contiene toda la ui
    // ocultarlo esconde todo dentro
    if (pl->m_uiLayer) {
        addEntry(pl->m_uiLayer, "UILayer (all UI)");

        auto* uiChildren = pl->m_uiLayer->getChildren();
        if (uiChildren) {
            for (auto* obj : CCArrayExt<CCNode*>(uiChildren)) {
                if (!obj || added.count(obj)) continue;

                std::string id = obj->getID();
                std::string name = "  UI: ";
                if (!id.empty()) {
                    name += id;
                } else {
                    name += simplifyClassName(typeid(*obj).name());
                }
                addEntry(obj, name);
            }
        }
    }

    // 4. otros hijos directos de playlayer
    auto* plChildren = pl->getChildren();
    if (plChildren) {
        for (auto* obj : CCArrayExt<CCNode*>(plChildren)) {
            if (!obj || added.count(obj)) continue;
            auto sz = obj->getContentSize();
            if (sz.width < 0.1f && sz.height < 0.1f) continue;

            std::string id = obj->getID();
            std::string name;

            if (!id.empty()) {
                name = id;
            } else {
                name = simplifyClassName(typeid(*obj).name());
                name += " [Z:" + std::to_string(obj->getZOrder()) + "]";
            }

            addEntry(obj, name);
        }
    }

    // 5. overlays de escena (uis externas)
    if (scene) {
        for (auto* obj : CCArrayExt<CCNode*>(scene->getChildren())) {
            if (!obj || added.count(obj) || obj == pl) continue;

            // salta capas del sistema (ocultas al renderizar)
            if (typeinfo_cast<FLAlertLayer*>(obj)) continue;
            std::string cls = typeid(*obj).name();
            if (cls.find("PauseLayer") != std::string::npos) continue;

            if (!obj->isVisible()) continue;

            auto sz = obj->getContentSize();
            if (sz.width < 0.1f && sz.height < 0.1f) continue;

            std::string id = obj->getID();
            std::string name = "Overlay: ";
            if (!id.empty()) {
                name += id;
            } else {
                name += simplifyClassName(cls);
            }
            addEntry(obj, name);
        }
    }

    log::info("[LayerEditor] Enumerated {} layers", m_layers.size());
}

// ─── ui de lista ───────────────────────────────────────────────────

void CaptureLayerEditorPopup::buildList() {
    auto content = this->m_mainLayer->getContentSize();

    const float listW    = content.width - 16.f;
    const float rowH     = 28.f;
    const int   numItems = static_cast<int>(m_layers.size());

    // area visible: entre preview y botones
    const float listTop  = content.height - 32.f - 90.f - 6.f;
    const float listBot  = 42.f;
    const float viewH    = listTop - listBot;
    const float viewX    = (content.width - listW) * 0.5f;

    // panel oscuro detras del area de scroll
    auto panel = CCScale9Sprite::create("square02_001.png");
    panel->setColor({0, 0, 0});
    panel->setOpacity(70);
    panel->setContentSize({listW, viewH});
    panel->ignoreAnchorPointForPosition(false);
    panel->setAnchorPoint({0.f, 0.f});
    panel->setPosition({viewX, listBot});
    this->m_mainLayer->addChild(panel, 0);

    // altura total contenido
    float totalH = numItems * rowH;
    if (totalH < viewH) totalH = viewH;

    // menu contenedor para filas
    m_listMenu = CCMenu::create();
    m_listMenu->setContentSize({listW, totalH});
    m_listMenu->setPosition({0, 0});

    for (int i = 0; i < numItems; ++i) {
        auto& entry = m_layers[i];
        // filas de arriba a abajo
        float y = totalH - rowH * 0.5f - i * rowH;

        // fondo alternado de fila
        if (i % 2 == 0) {
            auto bg = CCLayerColor::create({255, 255, 255, 15});
            bg->setContentSize({listW, rowH});
            bg->ignoreAnchorPointForPosition(false);
            bg->setAnchorPoint({0.f, 0.5f});
            bg->setPosition({0.f, y});
            m_listMenu->addChild(bg, -1);
        }

        // interruptor checkbox
        auto onSpr  = CCSprite::createWithSpriteFrameName("GJ_checkOn_001.png");
        auto offSpr = CCSprite::createWithSpriteFrameName("GJ_checkOff_001.png");
        if (!onSpr || !offSpr) continue;
        onSpr->setScale(0.45f);
        offSpr->setScale(0.45f);

        auto toggler = CCMenuItemToggler::create(
            onSpr, offSpr,
            this, menu_selector(CaptureLayerEditorPopup::onToggleLayer)
        );
        toggler->setTag(i);
        if (!entry.currentVisibility) {
            toggler->toggle(true);      // actualiza visual (muestra caja vacia)
            toggler->m_toggled = true;  // sincroniza estado interno
        }
        toggler->setPosition({18.f, y});
        m_listMenu->addChild(toggler);

        // etiqueta
        auto label = CCLabelBMFont::create(entry.name.c_str(), "bigFont.fnt");
        label->setScale(0.28f);
        label->setAnchorPoint({0.f, 0.5f});
        label->setPosition({38.f, y});
        if (!entry.currentVisibility) {
            label->setColor({150, 150, 150});
        }
        label->setTag(1000 + i);
        m_listMenu->addChild(label);
    }

    // scrollview envolviendo menu
    m_scrollView = cocos2d::extension::CCScrollView::create();
    m_scrollView->setViewSize({listW, viewH});
    m_scrollView->setPosition({viewX, listBot});
    m_scrollView->setDirection(kCCScrollViewDirectionVertical);
    m_scrollView->setContainer(m_listMenu);

    // scroll al inicio
    m_scrollView->setContentOffset({0, viewH - totalH}, false);

    this->m_mainLayer->addChild(m_scrollView, 2);
}

// ─── render mini-preview ──────────────────────────────────────

void CaptureLayerEditorPopup::updateMiniPreview() {
    if (!m_miniPreview) return;

    auto* scene = CCDirector::sharedDirector()->getRunningScene();
    if (!scene) return;

    auto winSize = CCDirector::sharedDirector()->getWinSize();

    const int rtW = 480;
    const int rtH = 270;

    // oculta solo overlays del sistema
    // popup hereda de flalertlayer
    // captura nuestros popups automaticamente
    // el resto lo controla el usuario
    std::vector<std::pair<CCNode*, bool>> hiddenSystem;

    for (auto* child : CCArrayExt<CCNode*>(scene->getChildren())) {
        if (!child || !child->isVisible()) continue;
        if (child == PlayLayer::get()) continue;

        bool isSystem = false;
        if (typeinfo_cast<FLAlertLayer*>(child)) isSystem = true;
        else {
            std::string cls = typeid(*child).name();
            if (cls.find("PauseLayer") != std::string::npos) isSystem = true;
        }

        if (isSystem) {
            hiddenSystem.push_back({child, true});
            child->setVisible(false);
        }
    }

    // renderiza escena completa
    auto* rt = CCRenderTexture::create(rtW, rtH, kCCTexture2DPixelFormat_RGBA8888);
    if (!rt) {
        for (auto& [node, _] : hiddenSystem) node->setVisible(true);
        return;
    }

    rt->begin();
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

    kmGLPushMatrix();
    float sx = static_cast<float>(rtW) / winSize.width;
    float sy = static_cast<float>(rtH) / winSize.height;
    kmGLScalef(sx, sy, 1.0f);
    scene->visit();
    kmGLPopMatrix();

    rt->end();

    // restaura overlays del sistema
    for (auto& [node, _] : hiddenSystem) node->setVisible(true);

    // actualiza sprite mini-preview
    auto* rtTex = rt->getSprite()->getTexture();
    if (!rtTex) return;

    m_miniPreview->setTexture(rtTex);
    m_miniPreview->setTextureRect(CCRect(0, 0, rtW, rtH));
    m_miniPreview->setFlipY(true);

    const float areaW = 160.f;
    const float areaH = 90.f;
    float scaleToFit = std::min(areaW / rtW, areaH / rtH);
    m_miniPreview->setScale(scaleToFit);
}

// ─── callbacks ─────────────────────────────────────────────────

void CaptureLayerEditorPopup::onToggleLayer(CCObject* sender) {
    auto* toggler = static_cast<CCMenuItemToggler*>(sender);
    if (!toggler) return;

    int idx = toggler->getTag();
    if (idx < 0 || idx >= static_cast<int>(m_layers.size())) return;

    auto& entry = m_layers[idx];

    // en gd, callback se ejecuta antes de actualizar m_toggled
    // asi que isToggled() devuelve estado anterior
    // visible (falso) -> ocultando -> nuevoVisible = falso
    // oculto (verdadero) -> mostrando -> nuevoVisible = verdadero
    bool newVisible = toggler->isToggled();
    entry.node->setVisible(newVisible);
    entry.currentVisibility = newVisible;

    // actualiza color etiqueta
    if (m_listMenu) {
        if (auto* label = dynamic_cast<CCLabelBMFont*>(
                m_listMenu->getChildByTag(1000 + idx))) {
            label->setColor(newVisible ? ccColor3B{255, 255, 255}
                                       : ccColor3B{150, 150, 150});
        }
    }

    log::info("[LayerEditor] '{}' -> {}", entry.name,
              newVisible ? "visible" : "hidden");

    // feedback visual instantaneo
    updateMiniPreview();
}

void CaptureLayerEditorPopup::onDoneBtn(CCObject* sender) {
    if (!sender) return;

    // recaptura completa para buffer actualizado
    if (m_previewPopup) {
        m_previewPopup->liveRecapture(true);
    }

    this->onClose(nullptr);
}

void CaptureLayerEditorPopup::onRestoreAllBtn(CCObject* sender) {
    if (!sender) return;

    for (auto& entry : m_layers) {
        if (!entry.node) continue;
        for (auto& [node, vis] : s_originalVisibilities) {
            if (node == entry.node) {
                entry.node->setVisible(vis);
                entry.currentVisibility = vis;
                break;
            }
        }
    }

    // muestra estado restaurado
    updateMiniPreview();

    Notification::create(
        Localization::get().getString("layers.restored").c_str(),
        NotificationIcon::Success
    )->show();
}

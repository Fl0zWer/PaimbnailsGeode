#include "CaptureLayerEditorPopup.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "CapturePreviewPopup.hpp"
#include "../../../utils/Localization.hpp"
#include "../../../utils/PaimonButtonHighlighter.hpp"
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
#include <Geode/cocos/kazmath/include/kazmath/mat4.h>
#include <set>
#include <algorithm>
#include <cstring>

using namespace geode::prelude;
using namespace cocos2d;

// guarda visibilidad original de capas
static std::vector<std::pair<CCNode*, bool>> s_originalVisibilities;

// ─── auxiliares ───────────────────────────────────────────────────

namespace {
    static std::string simplifyClassName(std::string const& cls) {
        std::string name = cls;
        for (char const* prefix : {"class ", "struct "}) {
            if (name.find(prefix) == 0) {
                name = name.substr(std::strlen(prefix));
            }
        }
        auto pos = name.find('<');
        if (pos != std::string::npos) name = name.substr(0, pos);
        if (name.length() > 24) name = name.substr(0, 21) + "...";
        return name;
    }

    static std::string tr(std::string const& es, std::string const& en) {
        return Localization::get().getLanguage() == Localization::Language::SPANISH ? es : en;
    }

    static std::string toLower(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return s;
    }

    static std::string describeNode(CCNode* node) {
        if (!node) return {};
        std::string id = node->getID();
        if (!id.empty()) return id;
        auto cls = simplifyClassName(typeid(*node).name());
        if (typeinfo_cast<CCParticleSystem*>(node)) {
            return tr("Partículas", "Particles") + " (" + cls + ")";
        }
        if (typeinfo_cast<CCMenu*>(node)) {
            return tr("Menú", "Menu") + " (" + cls + ")";
        }
        return cls;
    }

    static bool looksLikeEffectNode(CCNode* node) {
        if (!node) return false;
        if (typeinfo_cast<ShaderLayer*>(node)) return true;
        if (typeinfo_cast<CCParticleSystem*>(node)) return true;
        auto cls = toLower(typeid(*node).name());
        auto id = toLower(node->getID());
        static std::vector<std::string> patterns = {
            "shader", "effect", "particle", "trail", "glow", "bloom", "swing", "dash", "fire"
        };
        for (auto const& pattern : patterns) {
            if (cls.find(pattern) != std::string::npos || id.find(pattern) != std::string::npos) {
                return true;
            }
        }
        return false;
    }
}

// ─── api estatica ────────────────────────────────────────────────

CaptureLayerEditorPopup* CaptureLayerEditorPopup::create(CapturePreviewPopup* previewPopup) {
    auto ret = new CaptureLayerEditorPopup();
    ret->m_previewPopup = previewPopup;
    if (ret && ret->init()) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

void CaptureLayerEditorPopup::restoreAllLayers() {
    for (auto& [node, vis] : s_originalVisibilities) {
        if (node) node->setVisible(vis);
    }
    s_originalVisibilities.clear();
    log::info("[LayerEditor] All layers restored to original visibility");
}

// ─── init ──────────────────────────────────────────────────────

bool CaptureLayerEditorPopup::init() {
    if (!Popup::init(300.f, 300.f)) return false;
    this->setTitle(Localization::get().getString("layers.title").c_str());

    auto content = m_mainLayer->getContentSize();

    populateLayers();

    if (m_layers.empty()) {
        auto noLabel = CCLabelBMFont::create(
            Localization::get().getString("layers.no_playlayer").c_str(),
            "bigFont.fnt"
        );
        noLabel->setScale(0.4f);
        noLabel->setPosition({content.width * 0.5f, content.height * 0.5f});
        m_mainLayer->addChild(noLabel);
        return true;
    }

    // ── mini preview ─────────────────────────────────────────────
    const float previewW = 180.f;
    const float previewH = 101.f; // 16:9 aspect
    const float previewY = content.height - 32.f - previewH * 0.5f - 2.f;

    auto previewBorder = CCScale9Sprite::create("GJ_square07.png");
    if (previewBorder) {
        previewBorder->setContentSize({previewW + 6.f, previewH + 6.f});
        previewBorder->setPosition({content.width * 0.5f, previewY});
        m_mainLayer->addChild(previewBorder, 0);
    }

    auto previewBg = CCLayerColor::create({0, 0, 0, 200});
    previewBg->setContentSize({previewW, previewH});
    previewBg->ignoreAnchorPointForPosition(false);
    previewBg->setAnchorPoint({0.5f, 0.5f});
    previewBg->setPosition({content.width * 0.5f, previewY});
    m_mainLayer->addChild(previewBg, 1);

    m_miniPreview = CCSprite::create();
    m_miniPreview->setContentSize({previewW, previewH});
    m_miniPreview->setPosition({content.width * 0.5f, previewY});
    m_mainLayer->addChild(m_miniPreview, 2);

    updateMiniPreview();

    // ── lista de capas ───────────────────────────────────────────
    buildList();

    // ── botones inferiores ───────────────────────────────────────
    auto btnMenu = CCMenu::create();
    btnMenu->setPosition({content.width * 0.5f, 22.f});
    btnMenu->setID("bottom-buttons"_spr);

    auto restoreSpr = ButtonSprite::create(
        Localization::get().getString("layers.restore_all").c_str(),
        80, true, "bigFont.fnt", "GJ_button_01.png", 24.f, 0.4f
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
        80, true, "bigFont.fnt", "GJ_button_02.png", 24.f, 0.4f
    );
    if (doneSpr) {
        auto btn = CCMenuItemSpriteExtra::create(
            doneSpr, this,
            menu_selector(CaptureLayerEditorPopup::onDoneBtn)
        );
        PaimonButtonHighlighter::registerButton(btn);
        btnMenu->addChild(btn);
    }

    btnMenu->alignItemsHorizontallyWithPadding(10.f);
    m_mainLayer->addChild(btnMenu);

    return true;
}

// ─── enumeracion de capas ─────────────────────────────────────────

void CaptureLayerEditorPopup::populateLayers() {
    auto* pl = PlayLayer::get();
    if (!pl) return;

    auto* scene = CCDirector::sharedDirector()->getRunningScene();

    m_layers.clear();

    bool needRecordOriginals = s_originalVisibilities.empty();
    std::set<CCNode*> addedNodes;
    std::set<CCNode*> recordedNodes;

    auto addEntry = [&](CCNode* node, std::string const& name, bool isGroup, int depth, int parent) {
        LayerEntry entry;
        entry.node = node;
        entry.name = name;
        entry.currentVisibility = node ? node->isVisible() : true;
        entry.isGroup = isGroup;
        entry.depth = depth;
        entry.parentIndex = parent;

        int idx = static_cast<int>(m_layers.size());
        m_layers.push_back(std::move(entry));
        if (parent >= 0 && parent < static_cast<int>(m_layers.size())) {
            m_layers[parent].childIndices.push_back(idx);
        }

        if (!isGroup && node) {
            addedNodes.insert(node);
            if (needRecordOriginals && recordedNodes.insert(node).second) {
                s_originalVisibilities.push_back({node, node->isVisible()});
            }
        }
        return idx;
    };

    auto addGroup = [&](std::string const& name, int parent, int depth) {
        return addEntry(nullptr, name, true, depth, parent);
    };

    auto addLeaf = [&](CCNode* node, std::string const& name, int parent, int depth) {
        if (!node || addedNodes.count(node)) return -1;
        return addEntry(node, name, false, depth, parent);
    };

    auto addPlayerGroup = [&](PlayerObject* player, std::string const& playerName) {
        if (!player) return;

        int playerGroup = addGroup(playerName, -1, 0);
        addLeaf(player, tr("Cuerpo", "Body"), playerGroup, 1);

        int trailsGroup = -1;
        auto addTrail = [&](CCNode* node, std::string const& name) {
            if (!node) return;
            if (trailsGroup == -1) trailsGroup = addGroup(tr("Trazos", "Trails"), playerGroup, 1);
            addLeaf(node, name, trailsGroup, 2);
        };

        int particlesGroup = -1;
        auto addParticle = [&](CCNode* node, std::string const& name) {
            if (!node) return;
            if (particlesGroup == -1) particlesGroup = addGroup(tr("Partículas", "Particles"), playerGroup, 1);
            addLeaf(node, name, particlesGroup, 2);
        };

        addTrail(player->m_regularTrail, tr("Trazo normal", "Regular trail"));
        addTrail(player->m_waveTrail, tr("Trazo wave", "Wave trail"));
        addTrail(player->m_ghostTrail, tr("Trazo ghost", "Ghost trail"));

        addParticle(player->m_vehicleGroundParticles, tr("Polvo vehículo", "Vehicle dust"));
        addParticle(player->m_robotFire, tr("Fuego robot", "Robot fire"));
        addParticle(player->m_playerGroundParticles, tr("Polvo suelo", "Ground particles"));
        addParticle(player->m_trailingParticles, tr("Partículas trail", "Trailing particles"));
        addParticle(player->m_shipClickParticles, tr("Click ship", "Ship click particles"));
        addParticle(player->m_ufoClickParticles, tr("Click UFO", "UFO click particles"));
        addParticle(player->m_robotBurstParticles, tr("Explosión robot", "Robot burst"));
        addParticle(player->m_dashParticles, tr("Partículas dash", "Dash particles"));
        addParticle(player->m_swingBurstParticles1, tr("Swing burst 1", "Swing burst 1"));
        addParticle(player->m_swingBurstParticles2, tr("Swing burst 2", "Swing burst 2"));
        addParticle(player->m_landParticles0, tr("Aterrizaje 0", "Landing particles 0"));
        addParticle(player->m_landParticles1, tr("Aterrizaje 1", "Landing particles 1"));
        addParticle(player->m_dashFireSprite, tr("Fuego dash", "Dash fire"));

        int extrasGroup = -1;
        std::set<CCNode*> skipped = {
            player,
            player->m_regularTrail, player->m_waveTrail, player->m_ghostTrail,
            player->m_vehicleGroundParticles, player->m_robotFire,
            player->m_playerGroundParticles, player->m_trailingParticles,
            player->m_shipClickParticles, player->m_ufoClickParticles,
            player->m_robotBurstParticles, player->m_dashParticles,
            player->m_swingBurstParticles1, player->m_swingBurstParticles2,
            player->m_landParticles0, player->m_landParticles1,
            player->m_dashFireSprite
        };

        auto collectExtras = [&](auto&& self, CCNode* root) -> void {
            if (!root) return;
            auto* children = root->getChildren();
            if (!children) return;
            for (auto* obj : CCArrayExt<CCObject*>(children)) {
                auto* nd = typeinfo_cast<CCNode*>(obj);
                if (!nd) continue;
                if (!skipped.insert(nd).second) { self(self, nd); continue; }
                if (!addedNodes.count(nd)) {
                    if (extrasGroup == -1) extrasGroup = addGroup(tr("Extras", "Extras"), playerGroup, 1);
                    addLeaf(nd, describeNode(nd), extrasGroup, 2);
                }
                self(self, nd);
            }
        };
        collectExtras(collectExtras, player);
    };

    addPlayerGroup(pl->m_player1, Localization::get().getString("layers.player1"));
    addPlayerGroup(pl->m_player2, Localization::get().getString("layers.player2"));

    int uiGroup = -1;
    auto ensureUiGroup = [&]() {
        if (uiGroup == -1) uiGroup = addGroup(tr("UI / HUD", "UI / HUD"), -1, 0);
        return uiGroup;
    };

    if (pl->m_uiLayer) {
        addLeaf(pl->m_uiLayer, tr("Todo el UI", "All UI"), ensureUiGroup(), 1);
        if (auto* uiChildren = pl->m_uiLayer->getChildren()) {
            int uiChildrenGroup = -1;
            for (auto* obj : CCArrayExt<CCNode*>(uiChildren)) {
                if (!obj || addedNodes.count(obj)) continue;
                if (uiChildrenGroup == -1) uiChildrenGroup = addGroup(tr("Componentes UI", "UI components"), ensureUiGroup(), 1);
                addLeaf(obj, describeNode(obj), uiChildrenGroup, 2);
            }
        }
    }

    addLeaf(pl->m_attemptLabel, tr("Texto de intento", "Attempt label"), ensureUiGroup(), 1);
    addLeaf(pl->m_percentageLabel, tr("Porcentaje", "Percentage label"), ensureUiGroup(), 1);

    int effectsGroup = -1;
    auto ensureEffectsGroup = [&]() {
        if (effectsGroup == -1) effectsGroup = addGroup(Localization::get().getString("layers.effects"), -1, 0);
        return effectsGroup;
    };

    addLeaf(pl->m_shaderLayer, tr("Shader layer", "Shader layer"), ensureEffectsGroup(), 1);

    int gameplayGroup = -1;
    auto ensureGameplayGroup = [&]() {
        if (gameplayGroup == -1) gameplayGroup = addGroup(tr("Gameplay / Escena", "Gameplay / Scene"), -1, 0);
        return gameplayGroup;
    };

    if (auto* plChildren = pl->getChildren()) {
        for (auto* obj : CCArrayExt<CCNode*>(plChildren)) {
            if (!obj || addedNodes.count(obj)) continue;
            auto size = obj->getContentSize();
            bool hasVisualSize = size.width >= 0.1f || size.height >= 0.1f;
            if (!hasVisualSize && !(obj->getChildren() && obj->getChildren()->count() > 0)) continue;
            if (looksLikeEffectNode(obj)) {
                addLeaf(obj, describeNode(obj), ensureEffectsGroup(), 1);
            } else {
                addLeaf(obj, describeNode(obj), ensureGameplayGroup(), 1);
            }
        }
    }

    int overlayGroup = -1;
    auto ensureOverlayGroup = [&]() {
        if (overlayGroup == -1) overlayGroup = addGroup(tr("Overlays de escena", "Scene overlays"), -1, 0);
        return overlayGroup;
    };

    if (scene) {
        for (auto* obj : CCArrayExt<CCNode*>(scene->getChildren())) {
            if (!obj || obj == pl || addedNodes.count(obj)) continue;
            if (!obj->isVisible()) continue;
            if (typeinfo_cast<FLAlertLayer*>(obj)) continue;
            std::string cls = typeid(*obj).name();
            if (cls.find("PauseLayer") != std::string::npos) continue;
            auto size = obj->getContentSize();
            bool hasVisualSize = size.width >= 0.1f || size.height >= 0.1f;
            if (!hasVisualSize && !(obj->getChildren() && obj->getChildren()->count() > 0)) continue;
            addLeaf(obj, describeNode(obj), ensureOverlayGroup(), 1);
        }
    }

    log::info("[LayerEditor] Enumerated {} layer entries", m_layers.size());
}

// ─── visibility helpers ───────────────────────────────────────────

bool CaptureLayerEditorPopup::isEntryVisible(int idx) const {
    if (idx < 0 || idx >= static_cast<int>(m_layers.size())) return false;
    auto const& entry = m_layers[idx];

    // For groups: visible if ANY child is visible (more intuitive)
    if (!entry.childIndices.empty()) {
        for (int child : entry.childIndices) {
            if (isEntryVisible(child)) return true;
        }
        return false;
    }

    return entry.node ? entry.node->isVisible() : entry.currentVisibility;
}

void CaptureLayerEditorPopup::setEntryVisible(int idx, bool visible, bool cascadeChildren) {
    if (idx < 0 || idx >= static_cast<int>(m_layers.size())) return;
    auto& entry = m_layers[idx];
    entry.currentVisibility = visible;

    if (entry.node) {
        entry.node->setVisible(visible);
    }

    if (cascadeChildren) {
        for (int child : entry.childIndices) {
            setEntryVisible(child, visible, true);
        }
    }
}

// ─── in-place visual refresh (no rebuild) ─────────────────────────

void CaptureLayerEditorPopup::refreshRowVisuals(int idx) {
    if (idx < 0 || idx >= static_cast<int>(m_layers.size())) return;
    auto& entry = m_layers[idx];

    bool vis = isEntryVisible(idx);

    // Update toggler visual without triggering callback
    if (entry.toggler) {
        bool isCurrentlyToggled = entry.toggler->isToggled();
        bool shouldBeToggled = !vis; // toggled = hidden
        if (isCurrentlyToggled != shouldBeToggled) {
            entry.toggler->toggle(shouldBeToggled);
        }
    }

    // Update label color
    if (entry.label) {
        if (entry.isGroup) {
            entry.label->setColor(vis ? ccColor3B{255, 226, 120} : ccColor3B{120, 110, 80});
        } else {
            entry.label->setColor(vis ? ccColor3B{255, 255, 255} : ccColor3B{130, 130, 130});
        }
    }
}

// ─── build list UI ─────────────────────────────────────────────────

void CaptureLayerEditorPopup::buildList() {
    if (m_listRoot) {
        m_listRoot->removeFromParentAndCleanup(true);
        m_listRoot = nullptr;
        m_scrollView = nullptr;
    }

    auto content = m_mainLayer->getContentSize();

    const float listW    = content.width - 16.f;
    const float rowH     = 30.f;
    const int   numItems = static_cast<int>(m_layers.size());
    const float listTop  = content.height - 32.f - 101.f - 10.f; // below preview
    const float listBot  = 42.f;
    const float viewH    = listTop - listBot;
    const float viewX    = (content.width - listW) * 0.5f;

    m_listRoot = CCNode::create();
    m_listRoot->setID("list-root"_spr);
    m_mainLayer->addChild(m_listRoot, 2);

    // Dark panel background
    auto panel = CCScale9Sprite::create("square02_001.png");
    panel->setColor({0, 0, 0});
    panel->setOpacity(80);
    panel->setContentSize({listW, viewH});
    panel->ignoreAnchorPointForPosition(false);
    panel->setAnchorPoint({0.f, 0.f});
    panel->setPosition({viewX, listBot});
    m_listRoot->addChild(panel, 0);

    float totalH = std::max(viewH, numItems * rowH);

    // ScrollLayer
    m_scrollView = ScrollLayer::create({listW, viewH});
    m_scrollView->setPosition({viewX, listBot});
    m_scrollView->m_contentLayer->setContentSize({listW, totalH});

    for (int i = 0; i < numItems; ++i) {
        auto& entry = m_layers[i];
        float y = totalH - rowH - i * rowH;

        // Row container
        auto rowNode = CCNode::create();
        rowNode->setContentSize({listW, rowH});
        rowNode->setPosition({0.f, y});
        rowNode->setAnchorPoint({0.f, 0.f});

        // Row background
        if (entry.isGroup) {
            auto bg = CCLayerColor::create({255, 215, 90, 30});
            bg->setContentSize({listW, rowH});
            bg->setAnchorPoint({0.f, 0.f});
            bg->ignoreAnchorPointForPosition(false);
            bg->setPosition({0.f, 0.f});
            rowNode->addChild(bg, -2);

            // Accent bar
            auto accent = CCLayerColor::create({255, 215, 90, 140});
            accent->setContentSize({3.f, rowH - 4.f});
            accent->ignoreAnchorPointForPosition(false);
            accent->setAnchorPoint({0.f, 0.5f});
            accent->setPosition({4.f + entry.depth * 12.f, rowH * 0.5f});
            rowNode->addChild(accent, -1);
        } else if (i % 2 == 0) {
            auto bg = CCLayerColor::create({255, 255, 255, 10});
            bg->setContentSize({listW, rowH});
            bg->setAnchorPoint({0.f, 0.f});
            bg->ignoreAnchorPointForPosition(false);
            bg->setPosition({0.f, 0.f});
            rowNode->addChild(bg, -1);
        }

        // Toggle menu (one per row — avoids CCMenu swallowing scroll)
        auto rowMenu = CCMenu::create();
        rowMenu->setContentSize({listW, rowH});
        rowMenu->setPosition({0.f, 0.f});
        rowMenu->setAnchorPoint({0.f, 0.f});

        // Checkbox toggler
        float checkScale = entry.isGroup ? 0.52f : 0.45f;
        auto onSpr  = CCSprite::createWithSpriteFrameName("GJ_checkOn_001.png");
        auto offSpr = CCSprite::createWithSpriteFrameName("GJ_checkOff_001.png");
        if (onSpr && offSpr) {
            onSpr->setScale(checkScale);
            offSpr->setScale(checkScale);

            auto toggler = CCMenuItemToggler::create(
                offSpr, onSpr,
                this, menu_selector(CaptureLayerEditorPopup::onToggleLayer)
            );
            toggler->setTag(i);

            bool vis = isEntryVisible(i);
            toggler->toggle(vis);

            float checkX = 18.f + entry.depth * 12.f;
            toggler->setPosition({checkX, rowH * 0.5f});
            rowMenu->addChild(toggler);

            entry.toggler = toggler;
        }

        rowNode->addChild(rowMenu, 1);

        // Label
        std::string labelText = entry.name;
        if (!entry.isGroup) {
            labelText = std::string(entry.depth >= 2 ? "• " : "› ") + labelText;
        }

        auto label = CCLabelBMFont::create(labelText.c_str(), "bigFont.fnt");
        label->setScale(entry.isGroup ? 0.35f : (entry.depth >= 2 ? 0.26f : 0.29f));
        label->setAnchorPoint({0.f, 0.5f});
        label->setPosition({38.f + entry.depth * 12.f, rowH * 0.5f});

        bool vis = isEntryVisible(i);
        if (entry.isGroup) {
            label->setColor(vis ? ccColor3B{255, 226, 120} : ccColor3B{120, 110, 80});
        } else if (!vis) {
            label->setColor({130, 130, 130});
        }
        rowNode->addChild(label, 2);

        entry.label = label;

        m_scrollView->m_contentLayer->addChild(rowNode);
    }

    m_scrollView->scrollToTop();
    m_listRoot->addChild(m_scrollView, 2);
}

// ─── render mini-preview using CCRenderTexture ──────────────────

void CaptureLayerEditorPopup::updateMiniPreview() {
    if (!m_miniPreview) return;

    auto* pl = PlayLayer::get();
    if (!pl) return;

    auto* director = CCDirector::sharedDirector();
    auto winSize = director->getWinSize();

    // Use native CCRenderTexture with proper scale matrix
    // This avoids the custom RenderTexture's design-resolution changes
    // that cause the compressed preview
    const int rtW = 480;
    const int rtH = 270;

    auto* rt = CCRenderTexture::create(rtW, rtH, kCCTexture2DPixelFormat_RGBA8888);
    if (!rt) return;

    // Temporarily hide this popup and other system overlays
    bool selfWasVisible = this->isVisible();
    this->setVisible(false);

    auto* scene = director->getRunningScene();
    std::vector<std::pair<CCNode*, bool>> hiddenOverlays;
    if (scene) {
        for (auto* child : CCArrayExt<CCNode*>(scene->getChildren())) {
            if (!child || !child->isVisible() || child == pl) continue;
            if (typeinfo_cast<FLAlertLayer*>(child)) {
                hiddenOverlays.push_back({child, true});
                child->setVisible(false);
            } else {
                std::string cls = typeid(*child).name();
                if (cls.find("PauseLayer") != std::string::npos) {
                    hiddenOverlays.push_back({child, true});
                    child->setVisible(false);
                }
            }
        }
    }

    rt->begin();
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

    kmGLPushMatrix();
    float sx = static_cast<float>(rtW) / winSize.width;
    float sy = static_cast<float>(rtH) / winSize.height;
    kmMat4 scaleMat;
    std::memset(&scaleMat, 0, sizeof(scaleMat));
    scaleMat.mat[0]  = sx;
    scaleMat.mat[5]  = sy;
    scaleMat.mat[10] = 1.0f;
    scaleMat.mat[15] = 1.0f;
    kmGLMultMatrix(&scaleMat);
    pl->visit();
    kmGLPopMatrix();
    rt->end();

    // Restore overlays
    for (auto& [node, _] : hiddenOverlays) node->setVisible(true);
    this->setVisible(selfWasVisible);

    // Apply texture to mini preview
    auto* rtSprite = rt->getSprite();
    if (rtSprite) {
        auto* rtTex = rtSprite->getTexture();
        if (rtTex) {
            m_miniPreview->setTexture(rtTex);
            m_miniPreview->setTextureRect(CCRect(0, 0, rtW, rtH));
            m_miniPreview->setFlipY(true);

            const float areaW = 180.f;
            const float areaH = 101.f;
            float scaleToFit = std::min(areaW / rtW, areaH / rtH);
            m_miniPreview->setScale(scaleToFit);
        }
    }
}

// ─── callbacks ─────────────────────────────────────────────────

void CaptureLayerEditorPopup::onToggleLayer(CCObject* sender) {
    auto* toggler = typeinfo_cast<CCMenuItemToggler*>(sender);
    if (!toggler) return;

    int idx = toggler->getTag();
    if (idx < 0 || idx >= static_cast<int>(m_layers.size())) return;

    // With create(offSpr, onSpr, ...):
    //   - Normal (untoggled): shows offSpr (checkOff) = hidden
    //   - Toggled: shows onSpr (checkOn) = visible
    // After CCMenuItemToggler fires callback, it has already toggled.
    // isToggled() == true means it's now showing onSpr (checkOn) = visible
    bool newVisible = toggler->isToggled();
    setEntryVisible(idx, newVisible, true);

    log::info("[LayerEditor] '{}' -> {}", m_layers[idx].name,
        newVisible ? "visible" : "hidden");

    // Refresh visuals of this entry and related entries in-place
    // (parent groups and children) — NO rebuild!
    refreshRowVisuals(idx);

    // Refresh parent chain
    int parentIdx = m_layers[idx].parentIndex;
    while (parentIdx >= 0) {
        refreshRowVisuals(parentIdx);
        parentIdx = m_layers[parentIdx].parentIndex;
    }

    // Refresh children (for group toggles)
    for (int child : m_layers[idx].childIndices) {
        refreshRowVisuals(child);
        // Also refresh grandchildren
        for (int grandchild : m_layers[child].childIndices) {
            refreshRowVisuals(grandchild);
        }
    }

    updateMiniPreview();
}

void CaptureLayerEditorPopup::onDoneBtn(CCObject* sender) {
    if (!sender) return;

    // Close self first so the layer editor popup is not captured
    auto previewRef = m_previewPopup;
    this->onClose(nullptr);

    // Now recapture with updated layer visibility
    if (previewRef) {
        previewRef->liveRecapture(true);
    }
}

void CaptureLayerEditorPopup::onRestoreAllBtn(CCObject* sender) {
    if (!sender) return;

    for (auto& [node, vis] : s_originalVisibilities) {
        if (node) node->setVisible(vis);
    }

    // Refresh all entry currentVisibility
    for (auto& entry : m_layers) {
        entry.currentVisibility = entry.node ? entry.node->isVisible() : true;
    }

    // Refresh all row visuals in-place
    for (int i = 0; i < static_cast<int>(m_layers.size()); ++i) {
        refreshRowVisuals(i);
    }

    updateMiniPreview();

    PaimonNotify::create(
        Localization::get().getString("layers.restored").c_str(),
        NotificationIcon::Success
    )->show();
}

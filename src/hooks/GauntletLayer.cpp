#include <Geode/Geode.hpp>
#include <Geode/modify/GauntletLayer.hpp>
#include <Geode/binding/GauntletLayer.hpp>
#include <Geode/binding/GameLevelManager.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/binding/GJMapPack.hpp>
#include "../managers/ThumbnailLoader.hpp"
#include "../utils/Debug.hpp"

using namespace geode::prelude;

const char* g_vertexShader = R"(
    attribute vec4 a_position;
    attribute vec4 a_color;
    attribute vec2 a_texCoord;

    #ifdef GL_ES
    varying lowp vec4 v_fragmentColor;
    varying mediump vec2 v_texCoord;
    #else
    varying vec4 v_fragmentColor;
    varying vec2 v_texCoord;
    #endif

    void main()
    {
        gl_Position = CC_MVPMatrix * a_position;
        v_fragmentColor = a_color;
        v_texCoord = a_texCoord;
    }
)";

// Gaussian blur 13-tap, linear sampling
// pesos gaussianos sigma ~4
const char* g_fragmentShaderGaussianBlur = R"(
    #ifdef GL_ES
    precision highp float;
    #endif

    varying vec4 v_fragmentColor;
    varying vec2 v_texCoord;
    uniform sampler2D u_texture;
    uniform vec2 u_texSize;
    uniform vec2 u_direction; // (1,0) H (0,1) V
    uniform float u_blurAmount; // intensidad blur

    void main() {
        vec2 texOffset = u_blurAmount / u_texSize;

        // pesos 13-tap sigma ~4, linear -> 7 taps

        // pesos + linear sampling
        const float weight0 = 0.2270270270;
        const float weight1 = 0.3162162162;
        const float weight2 = 0.0702702703;

        const float offset1 = 1.3846153846;
        const float offset2 = 3.2307692308;

        vec2 dir = u_direction * texOffset;

        vec4 color = texture2D(u_texture, v_texCoord) * weight0;

        color += texture2D(u_texture, v_texCoord + dir * offset1) * weight1;
        color += texture2D(u_texture, v_texCoord - dir * offset1) * weight1;
        color += texture2D(u_texture, v_texCoord + dir * offset2) * weight2;
        color += texture2D(u_texture, v_texCoord - dir * offset2) * weight2;

        gl_FragColor = color * v_fragmentColor;
    }
)";

// Kawase blur 1 pasada, rapido
const char* g_fragmentShaderKawaseBlur = R"(
    #ifdef GL_ES
    precision highp float;
    #endif

    varying vec4 v_fragmentColor;
    varying vec2 v_texCoord;
    uniform sampler2D u_texture;
    uniform vec2 u_texSize;
    uniform float u_blurAmount;

    void main() {
        vec2 texOffset = u_blurAmount / u_texSize;

        // Kawase 4 samples + centro
        vec4 color = texture2D(u_texture, v_texCoord) * 0.2;

        color += texture2D(u_texture, v_texCoord + vec2(-1.0, -1.0) * texOffset) * 0.2;
        color += texture2D(u_texture, v_texCoord + vec2( 1.0, -1.0) * texOffset) * 0.2;
        color += texture2D(u_texture, v_texCoord + vec2(-1.0,  1.0) * texOffset) * 0.2;
        color += texture2D(u_texture, v_texCoord + vec2( 1.0,  1.0) * texOffset) * 0.2;

        gl_FragColor = color * v_fragmentColor;
    }
)";

// Dual Kawase
// una pasada, sampling
const char* g_fragmentShaderDualKawase = R"(
    #ifdef GL_ES
    precision highp float;
    #endif

    varying vec4 v_fragmentColor;
    varying vec2 v_texCoord;
    uniform sampler2D u_texture;
    uniform vec2 u_texSize;
    uniform float u_blurAmount;

    void main() {
        vec2 halfpixel = (u_blurAmount * 0.5) / u_texSize;
        vec2 offset = u_blurAmount / u_texSize;

        vec4 color = texture2D(u_texture, v_texCoord) * 4.0;

        color += texture2D(u_texture, v_texCoord - halfpixel);
        color += texture2D(u_texture, v_texCoord + halfpixel);
        color += texture2D(u_texture, v_texCoord + vec2(halfpixel.x, -halfpixel.y));
        color += texture2D(u_texture, v_texCoord - vec2(halfpixel.x, -halfpixel.y));

        color += texture2D(u_texture, v_texCoord + vec2(-offset.x, 0.0)) * 2.0;
        color += texture2D(u_texture, v_texCoord + vec2( offset.x, 0.0)) * 2.0;
        color += texture2D(u_texture, v_texCoord + vec2(0.0, -offset.y)) * 2.0;
        color += texture2D(u_texture, v_texCoord + vec2(0.0,  offset.y)) * 2.0;

        gl_FragColor = (color / 16.0) * v_fragmentColor;
    }
)";

class GauntletThumbnailNode : public CCNode {
    std::vector<int> m_levelIDs;
    std::vector<Ref<CCTexture2D>> m_loadedTextures;
    int m_currentIndex = 0;
    float m_timer = 0.f;
    
    // 2 sprites reusables (evita crash con mods)
    CCSprite* m_sprites[2] = {nullptr, nullptr};
    int m_activeSpriteIndex = 0; // sprite visible
    bool m_transitioning = false;
    float m_transitionTime = 0.f; // timer transicion
    bool m_loadingStarted = false;
    bool m_firstLoad = true;

public:
    static GauntletThumbnailNode* create(const std::vector<int>& levelIDs) {
        auto node = new GauntletThumbnailNode();
        if (node && node->init(levelIDs)) {
            node->autorelease();
            return node;
        }
        CC_SAFE_DELETE(node);
        return nullptr;
    }

    bool init(const std::vector<int>& levelIDs) {
        if (!CCNode::init()) return false;
        
        m_levelIDs = levelIDs;
        m_currentIndex = 0;
        
        // fondo negro init
        auto winSize = CCDirector::sharedDirector()->getWinSize();
        auto bg = CCLayerColor::create(ccc4(0, 0, 0, 200), winSize.width, winSize.height);
        this->addChild(bg, -1);
        
        this->setContentSize(winSize);
        this->setAnchorPoint({0.f, 0.f});
        this->setZOrder(-100); 

        // 2 sprites
        m_sprites[0] = CCSprite::create();
        m_sprites[1] = CCSprite::create();
        
        // add invisibles
        m_sprites[0]->setOpacity(0);
        m_sprites[1]->setOpacity(0);
        // ids pa mods
        m_sprites[0]->setID("paimon-bg-sprite-1"_spr);
        m_sprites[1]->setID("paimon-bg-sprite-2"_spr);

        this->addChild(m_sprites[0], 0);
        this->addChild(m_sprites[1], 0);

        this->schedule(schedule_selector(GauntletThumbnailNode::updateSlide), 1.0f / 60.f);
        
        // start load
        this->loadAllThumbnails();
        
        return true;
    }

    void loadAllThumbnails() {
        if (m_loadingStarted) return;
        m_loadingStarted = true;

        for (int id : m_levelIDs) {
            ThumbnailLoader::get().requestLoad(id, "", [this, id](CCTexture2D* tex, bool success) {
                // precache
            }, 10, false);
        }
    }

    void updateSlide(float dt) {
        if (m_levelIDs.empty()) return;

        m_timer += dt;
        
        // timer transicion
        if (m_transitioning) {
            m_transitionTime += dt;
            if (m_transitionTime >= 0.6f) { // buffer fade
                onTransitionFinished();
            }
        }

        // carga inicial
        if (m_firstLoad && m_timer > 0.1f) {
            showNextImage();
            m_timer = 0;
            m_firstLoad = false;
        }
        // ciclo
        else if (!m_firstLoad && m_timer > 3.0f && !m_transitioning) {
            m_currentIndex = (m_currentIndex + 1) % m_levelIDs.size();
            showNextImage();
            m_timer = 0;
        }
    }

    void showNextImage() {
        if (m_levelIDs.empty()) return;
        
        int id = m_levelIDs[m_currentIndex];
        
        // no cargada -> buscar otra
        if (!ThumbnailLoader::get().isLoaded(id)) {
            bool found = false;
            for (size_t i = 0; i < m_levelIDs.size(); i++) {
                int checkIdx = (m_currentIndex + i) % m_levelIDs.size();
                if (ThumbnailLoader::get().isLoaded(m_levelIDs[checkIdx])) {
                    m_currentIndex = checkIdx;
                    id = m_levelIDs[m_currentIndex];
                    found = true;
                    break;
                }
            }
            if (!found) return;
        }

        ThumbnailLoader::get().requestLoad(id, "", [this](CCTexture2D* tex, bool success) {
            if (success && tex) {
                transitionTo(tex);
            }
        }, 11, false);
    }

    void transitionTo(CCTexture2D* tex) {
        if (m_transitioning) return;
        m_transitioning = true;

        // siguiente sprite
        int nextIdx = 1 - m_activeSpriteIndex;
        CCSprite* nextSprite = m_sprites[nextIdx];
        CCSprite* currentSprite = m_sprites[m_activeSpriteIndex];

        auto winSize = CCDirector::sharedDirector()->getWinSize();

        // update textura + shader
        nextSprite->setTexture(tex);
        nextSprite->setTextureRect(CCRect(0, 0, tex->getContentSize().width, tex->getContentSize().height));

        // shader Dual Kawase
        auto shader = new CCGLProgram();
        shader->initWithVertexShaderByteArray(g_vertexShader, g_fragmentShaderDualKawase);

        shader->addAttribute(kCCAttributeNamePosition, kCCVertexAttrib_Position);
        shader->addAttribute(kCCAttributeNameColor, kCCVertexAttrib_Color);
        shader->addAttribute(kCCAttributeNameTexCoord, kCCVertexAttrib_TexCoords);
        
        shader->link();
        shader->updateUniforms();

        shader->use();
        auto size = tex->getContentSizeInPixels();

        // uniforms
        auto locTexSize = shader->getUniformLocationForName("u_texSize");
        shader->setUniformLocationWith2f(locTexSize, size.width, size.height);

        // blur 2-5
        auto locBlurAmount = shader->getUniformLocationForName("u_blurAmount");
        shader->setUniformLocationWith1f(locBlurAmount, 3.5f);

        nextSprite->setShaderProgram(shader);
        shader->release();

        // aspect fill
        float scaleX = winSize.width / nextSprite->getContentSize().width;
        float scaleY = winSize.height / nextSprite->getContentSize().height;
        float scale = std::max(scaleX, scaleY);
        
        nextSprite->setScale(scale);
        nextSprite->setPosition(winSize / 2);
        nextSprite->setColor({150, 150, 150});
        nextSprite->setOpacity(0);
        nextSprite->setZOrder(1); // al frente

        if (currentSprite) {
            currentSprite->setZOrder(0); // lo mando atras
        }

        // acciones
        nextSprite->stopAllActions();
        nextSprite->runAction(CCFadeIn::create(0.5f));
        nextSprite->runAction(CCEaseSineOut::create(CCScaleTo::create(3.5f, scale * 1.05f)));

        if (currentSprite) {
            currentSprite->stopAllActions();
            currentSprite->runAction(CCFadeOut::create(0.5f));
        } else {
            // primera vez listo
            onTransitionFinished();
        }
        
        // reset timer
        m_transitionTime = 0.f;
        
        // index activo
        m_activeSpriteIndex = nextIdx;
    }

    void onTransitionFinished() {
        m_transitioning = false;
        // reset sprite viejo
    }
};


class $modify(PaimonGauntletLayer, GauntletLayer) {
    bool init(GauntletType type) {
        if (!GauntletLayer::init(type)) return false;
        
        // hide bg default
        if (auto bg = this->getChildByID("background")) {
            bg->setVisible(false);
        } else {
            // fallback primer hijo = fondo
            if (this->getChildrenCount() > 0) {
                 if (auto node = typeinfo_cast<CCNode*>(this->getChildren()->objectAtIndex(0))) {
                     node->setVisible(false);
                 }
            }
        }

        auto levelManager = GameLevelManager::sharedState();
        auto mapPack = levelManager->getSavedGauntlet(static_cast<int>(type));
        
        std::vector<int> ids;
        if (mapPack && mapPack->m_levels) {
            // m_levels = ccarray strings (ids)
            for (int i = 0; i < mapPack->m_levels->count(); ++i) {
                if (auto str = typeinfo_cast<CCString*>(mapPack->m_levels->objectAtIndex(i))) {
                    try {
                        ids.push_back(str->intValue());
                    } catch(...) {}
                }
            }
        }

        if (!ids.empty()) {
            auto bgNode = GauntletThumbnailNode::create(ids);
            if (bgNode) {
                bgNode->setID("paimon-gauntlet-background"_spr);
                this->addChild(bgNode, -100);
            }
        }
        
        return true;
    }
};

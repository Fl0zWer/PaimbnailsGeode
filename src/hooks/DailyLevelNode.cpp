#include <Geode/Geode.hpp>
#include <Geode/modify/DailyLevelNode.hpp>
#include "../managers/ThumbnailLoader.hpp"

using namespace geode::prelude;

// shaders
const char* kVertexShaderDaily = R"(
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

const char* kFragmentShaderBlurDaily = R"(
#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform vec2 u_texSize;
uniform float u_intensity;

void main() {
    vec4 color = vec4(0.0);
    // Increased multiplier from 2.0 to 10.0 to make blur visible
    float blurSize = u_intensity * 10.0; 
    vec2 onePixel = vec2(1.0, 1.0) / u_texSize;
    
    float totalWeight = 0.0;
    
    for (float x = -2.0; x <= 2.0; x+=1.0) {
        for (float y = -2.0; y <= 2.0; y+=1.0) {
            float weight = 1.0 / (1.0 + x*x + y*y);
            vec4 sample = texture2D(u_texture, v_texCoord + vec2(x, y) * onePixel * blurSize);
            color += sample * weight;
            totalWeight += weight;
        }
    }
    vec4 finalColor = (color / totalWeight) * v_fragmentColor;
    gl_FragColor = finalColor;
}
)";

class PaimonBlurSprite : public CCSprite {
public:
    float m_intensity = 1.0f;
    CCSize m_texSize;
    float m_timer = 0.0f;
    int m_state = 0; // 0: max, 1: baja a 0, 2: hold 0, 3: sube a max

    static PaimonBlurSprite* createWithTexture(CCTexture2D* texture) {
        auto sprite = new PaimonBlurSprite();
        if (sprite && sprite->initWithTexture(texture)) {
            sprite->autorelease();
            return sprite;
        }
        CC_SAFE_DELETE(sprite);
        return nullptr;
    }

    void startLoop() {
        m_intensity = 0.4f;
        m_timer = 0.0f;
        m_state = 0;
        this->scheduleUpdate();
    }

    void update(float dt) override {
        m_timer += dt;
        float maxBlur = 0.4f;
        
        switch(m_state) {
            case 0: // aguanto el blur al max
                m_intensity = maxBlur;
                if (m_timer > 0.5f) { 
                    m_state = 1; 
                    m_timer = 0; 
                }
                break;
            case 1: // bajo a 0 suave
                {
                    float dur = 1.5f;
                    float p = std::min(m_timer / dur, 1.0f);
                    
                    // Smootherstep: t * t * t * (t * (t * 6 - 15) + 10)
                    float t = p;
                    float ease = t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
                    
                    m_intensity = maxBlur * (1.0f - ease);

                    if (p >= 1.0f) { 
                        m_intensity = 0.0f; 
                        m_state = 2; 
                        m_timer = 0; 
                    }
                }
                break;
            case 2: // aguanto sin blur
                m_intensity = 0.0f;
                if (m_timer > 2.0f) { 
                    m_state = 3; 
                    m_timer = 0; 
                }
                break;
            case 3: // subo al max
                {
                    float dur = 1.5f;
                    float p = std::min(m_timer / dur, 1.0f);
                    
                    // smootherstep
                    float t = p;
                    float ease = t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
                    
                    m_intensity = maxBlur * ease;

                    if (p >= 1.0f) { 
                        m_intensity = maxBlur; 
                        m_state = 0; 
                        m_timer = 0; 
                    }
                }
                break;
        }
        CCSprite::update(dt);
    }

    void draw() override {
        if (getShaderProgram()) {
            getShaderProgram()->use();
            getShaderProgram()->setUniformsForBuiltins();
            
            GLint intensityLoc = getShaderProgram()->getUniformLocationForName("u_intensity");
            getShaderProgram()->setUniformLocationWith1f(intensityLoc, m_intensity);
            
            GLint sizeLoc = getShaderProgram()->getUniformLocationForName("u_texSize");
            getShaderProgram()->setUniformLocationWith2f(sizeLoc, m_texSize.width, m_texSize.height);
        }
        CCSprite::draw();
    }
};

class $modify(PaimonDailyLevelNode, DailyLevelNode) {
    struct Fields {
        CCSprite* m_paimonThumb = nullptr;
        CCClippingNode* m_paimonClipper = nullptr;
        CCSprite* m_loadingSpinner = nullptr;
        int m_levelID = 0;
    };

    bool init(GJGameLevel* level, DailyLevelPage* page, bool isTime) {
        if (!DailyLevelNode::init(level, page, isTime)) return false;

        if (!level) return true;
        m_fields->m_levelID = level->m_levelID;

        // saco el size/pos pa la miniatura
        CCSize nodeSize = this->getContentSize();
        CCPoint nodePos = {0.f, 0.f};
        CCPoint nodeAnchor = {0.f, 0.f};
        
        CCNode* bg = this->getChildByID("background");
        if (!bg) {
             // intento pillar un scale9sprite si no esta el id
             if (this->getChildren() && this->getChildrenCount() > 0) {
                 for(int i=0; i<this->getChildrenCount(); ++i) {
                     if (auto sprite = typeinfo_cast<CCScale9Sprite*>(this->getChildren()->objectAtIndex(i))) {
                         bg = sprite;
                         break;
                     }
                 }
             }
        }

        if (bg) {
            nodeSize = bg->getContentSize();
            nodePos = bg->getPosition();
            nodeAnchor = bg->getAnchorPoint();
        } else if (nodeSize.width < 10.f) {
            // fallback de size
            nodeSize = CCSize(340.f, 230.f);
        }

        // creo el clipping node
        // valores sacados de la imagen del user
        m_fields->m_paimonClipper = CCClippingNode::create();
        m_fields->m_paimonClipper->setContentSize({382.f, 116.f});
        m_fields->m_paimonClipper->setAnchorPoint({0.5f, 0.5f});
        m_fields->m_paimonClipper->setPosition({0.f, 0.f});
        m_fields->m_paimonClipper->setScale(0.985f);
        m_fields->m_paimonClipper->setID("paimon-thumbnail-clipper"_spr);
        m_fields->m_paimonClipper->setAlphaThreshold(0.05f); // pa que el alpha del stencil funcione

        // creo stencil (mascara)
        // uso square02_001.png como en previewpopup
        auto stencil = CCScale9Sprite::create("square02_001.png");
        stencil->setContentSize({382.f, 116.f});
        stencil->setAnchorPoint({0.5f, 0.5f});
        stencil->setPosition({382.f / 2.f, 116.f / 2.f});
        m_fields->m_paimonClipper->setStencil(stencil);

        // lo meto con z=1
        this->addChild(m_fields->m_paimonClipper, 1);

        // creo el spinner de carga
        auto spinner = CCSprite::create("loadingCircle.png");
        if (!spinner) spinner = CCSprite::createWithSpriteFrameName("loadingCircle.png");
        if (spinner) {
            spinner->setScale(0.6f);
            spinner->setOpacity(200);
            spinner->setPosition({382.f / 2.f, 116.f / 2.f});
            spinner->runAction(CCRepeatForever::create(CCRotateBy::create(1.0f, 360.0f)));
            m_fields->m_paimonClipper->addChild(spinner, 10);
            m_fields->m_loadingSpinner = spinner;
        }

        // pido la miniatura
        int levelID = level->m_levelID;
        std::string fileName = fmt::format("{}.png", levelID);
        
        this->retain(); // me mantengo vivo pa el callback
        ThumbnailLoader::get().requestLoad(levelID, fileName, [this](CCTexture2D* tex, bool success) {
            // chequeo rapido por si ya no existo
            if (!this->getParent() && !this->m_fields->m_paimonClipper) {
                this->release();
                return;
            }

            // quito el spinner
            if (this->m_fields->m_loadingSpinner) {
                this->m_fields->m_loadingSpinner->removeFromParent();
                this->m_fields->m_loadingSpinner = nullptr;
            }

            if (success && tex && this->m_fields->m_paimonClipper) {
                if (this->m_fields->m_paimonThumb) {
                    this->m_fields->m_paimonThumb->removeFromParent();
                }
                
                auto sprite = PaimonBlurSprite::createWithTexture(tex);
                sprite->m_texSize = tex->getContentSizeInPixels();
                this->m_fields->m_paimonThumb = sprite;
                
                // seteo shader
                auto shader = new CCGLProgram();
                shader->initWithVertexShaderByteArray(kVertexShaderDaily, kFragmentShaderBlurDaily);
                shader->addAttribute(kCCAttributeNamePosition, kCCVertexAttrib_Position);
                shader->addAttribute(kCCAttributeNameColor, kCCVertexAttrib_Color);
                shader->addAttribute(kCCAttributeNameTexCoord, kCCVertexAttrib_TexCoords);
                shader->link();
                shader->updateUniforms();
                sprite->setShaderProgram(shader);
                shader->release();
                
                // hago aspect fill
                CCSize containerSize = this->m_fields->m_paimonClipper->getContentSize();
                float sx = containerSize.width / sprite->getContentWidth();
                float sy = containerSize.height / sprite->getContentHeight();
                float scale = std::max(sx, sy); // aspect fill: cubro todo el area

                sprite->setScale(scale);
                sprite->setPosition(containerSize / 2);
                
                // anim: fade in
                sprite->setOpacity(0);
                sprite->runAction(CCFadeIn::create(0.5f));
                
                // arranco el loop del blur
                sprite->startLoop();

                this->m_fields->m_paimonClipper->addChild(sprite);
            }
            this->release();
        });

        return true;
    }
};

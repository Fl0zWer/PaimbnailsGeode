#include <Geode/Geode.hpp>
#include <Geode/modify/DailyLevelNode.hpp>
#include <Geode/ui/LoadingSpinner.hpp>
#include "../features/thumbnails/services/ThumbnailLoader.hpp"
#include "../utils/Shaders.hpp"
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

using namespace geode::prelude;

// shaders (static pa evitar colisiones de linkage con otros TU)
static char const* kVertexShaderDaily = R"(
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

static char const* kFragmentShaderBlurDaily = R"(
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
    int m_state = 0; // 0: max blur, 1: baja a 0, 2: hold 0, 3: sube a max

    static PaimonBlurSprite* createWithTexture(CCTexture2D* texture) {
        auto sprite = new PaimonBlurSprite();
        if (sprite && sprite->initWithTexture(texture)) {
            sprite->autorelease();
            return sprite;
        }
        CC_SAFE_DELETE(sprite);
        return nullptr;
    }

    static float smootherstep(float t) {
        return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
    }

    void startLoop() {
        m_intensity = 0.4f;
        m_timer = 0.0f;
        m_state = 0;
        this->scheduleUpdate();
    }

    void update(float dt) override {
        m_timer += dt;
        constexpr float maxBlur = 0.4f;
        constexpr float rampDur = 1.5f;

        switch (m_state) {
            case 0: // hold blur max
                m_intensity = maxBlur;
                if (m_timer > 0.5f) { m_state = 1; m_timer = 0.0f; }
                break;
            case 1: { // ramp down
                float p = std::min(m_timer / rampDur, 1.0f);
                m_intensity = maxBlur * (1.0f - smootherstep(p));
                if (p >= 1.0f) { m_intensity = 0.0f; m_state = 2; m_timer = 0.0f; }
            } break;
            case 2: // hold no blur
                m_intensity = 0.0f;
                if (m_timer > 2.0f) { m_state = 3; m_timer = 0.0f; }
                break;
            case 3: { // ramp up
                float p = std::min(m_timer / rampDur, 1.0f);
                m_intensity = maxBlur * smootherstep(p);
                if (p >= 1.0f) { m_intensity = maxBlur; m_state = 0; m_timer = 0.0f; }
            } break;
            default: break;
        }
        CCSprite::update(dt);
    }

    void onExit() override {
        this->unscheduleUpdate();
        CCSprite::onExit();
    }

    void draw() override {
        if (auto* prog = getShaderProgram()) {
            prog->use();
            prog->setUniformsForBuiltins();
            prog->setUniformLocationWith1f(prog->getUniformLocationForName("u_intensity"), m_intensity);
            prog->setUniformLocationWith2f(prog->getUniformLocationForName("u_texSize"), m_texSize.width, m_texSize.height);
        }
        CCSprite::draw();
    }
};

class $modify(PaimonDailyLevelNode, DailyLevelNode) {
    static void onModify(auto& self) {
        // Usa IDs asignados por geode.node-ids cuando estan disponibles.
        (void)self.setHookPriorityAfterPost("DailyLevelNode::init", "geode.node-ids");
    }

    struct Fields {
        Ref<CCSprite> m_paimonThumb = nullptr;
        Ref<CCClippingNode> m_paimonClipper = nullptr;
        Ref<geode::LoadingSpinner> m_loadingSpinner = nullptr;
        int m_levelID = 0;
    };

    $override
    bool init(GJGameLevel* level, DailyLevelPage* page, bool isTime) {
        if (!DailyLevelNode::init(level, page, isTime)) return false;

        if (!level) return true;
        m_fields->m_levelID = level->m_levelID;
        log::info("[DailyLevelNode] init: levelID={}", level->m_levelID.value());

        // saco el size pa la miniatura
        CCSize nodeSize = this->getContentSize();

        CCNode* bg = this->getChildByID("background");
        if (!bg) {
             // intento pillar un scale9sprite si no esta el id
             if (auto scale9 = this->getChildByType<CCScale9Sprite>(0)) {
                 bg = scale9;
             }
        }

        // Determinar tamaño y posicion del clipping a partir del background real
        CCSize clipSize;
        CCPoint clipPos;
        CCPoint clipAnchor = ccp(0.5f, 0.5f);
        float padding = 3.f;

        if (bg) {
            clipSize = bg->getScaledContentSize();
            clipPos  = bg->getPosition();
            clipAnchor = bg->getAnchorPoint();
        } else if (nodeSize.width >= 10.f) {
            clipSize = nodeSize;
            clipPos  = ccp(0.f, 0.f);
        } else {
            clipSize = CCSize(340.f, 230.f);
            clipPos  = ccp(0.f, 0.f);
        }

        // Restar padding para que la miniatura no toque los bordes
        CCSize imgArea = CCSize(clipSize.width - padding * 2.f,
                                clipSize.height - padding * 2.f);

        // creo el clipping node con tamaño dinamico del background
        m_fields->m_paimonClipper = CCClippingNode::create();
        m_fields->m_paimonClipper->setContentSize(imgArea);
        m_fields->m_paimonClipper->setAnchorPoint(clipAnchor);
        m_fields->m_paimonClipper->setPosition(clipPos);
        m_fields->m_paimonClipper->setID("paimon-thumbnail-clipper"_spr);

        // stencil con esquinas redondeadas — usa CCDrawNode para evitar
        // conflictos con mods de texturas (HappyTextures, TextureLdr)
        float clipW = imgArea.width;
        float clipH = imgArea.height;
        float r = 6.f;
        int segs = 8;
        auto stencil = CCDrawNode::create();
        std::vector<CCPoint> verts;
        // esquina inferior-izquierda: (0,r) -> (r,0)
        for (int i = 0; i <= segs; i++) {
            float a = static_cast<float>(M_PI + (M_PI / 2.0) * i / segs);
            verts.push_back(ccp(r + r * cosf(a), r + r * sinf(a)));
        }
        // esquina inferior-derecha: (W-r,0) -> (W,r)
        for (int i = 0; i <= segs; i++) {
            float a = static_cast<float>(3.0 * M_PI / 2.0 + (M_PI / 2.0) * i / segs);
            verts.push_back(ccp(clipW - r + r * cosf(a), r + r * sinf(a)));
        }
        // esquina superior-derecha: (W,H-r) -> (W-r,H)
        for (int i = 0; i <= segs; i++) {
            float a = static_cast<float>((M_PI / 2.0) * i / segs);
            verts.push_back(ccp(clipW - r + r * cosf(a), clipH - r + r * sinf(a)));
        }
        // esquina superior-izquierda: (r,H) -> (0,H-r)
        for (int i = 0; i <= segs; i++) {
            float a = static_cast<float>(M_PI / 2.0 + (M_PI / 2.0) * i / segs);
            verts.push_back(ccp(r + r * cosf(a), clipH - r + r * sinf(a)));
        }
        ccColor4F white = {1,1,1,1};
        stencil->drawPolygon(verts.data(), static_cast<int>(verts.size()), white, 0, white);
        m_fields->m_paimonClipper->setStencil(stencil);

        // lo meto con z=1
        this->addChild(m_fields->m_paimonClipper, 1);

        // creo el spinner de carga (geode::LoadingSpinner gira solo y respeta content size)
        auto spinner = geode::LoadingSpinner::create(25.f);
        spinner->setPosition(imgArea / 2);
        m_fields->m_paimonClipper->addChild(spinner, 10);
        m_fields->m_loadingSpinner = spinner;

        // pido la miniatura
        int levelID = level->m_levelID;
        std::string fileName = fmt::format("{}.png", levelID);
        
        // Ref<> mantiene vivo este nodo hasta que el callback termine
        log::info("[DailyLevelNode] requesting thumbnail: levelID={}", levelID);
        Ref<DailyLevelNode> self = this;
        ThumbnailLoader::get().requestLoad(levelID, fileName, [self, levelID](CCTexture2D* tex, bool success) {
            auto* fields = static_cast<PaimonDailyLevelNode*>(self.data())->m_fields.self();
            // chequeo rapido por si ya no existo o el clipper se perdio
            if (!self->getParent() || !fields->m_paimonClipper) {
                return;
            }

            // quito el spinner
            if (fields->m_loadingSpinner) {
                fields->m_loadingSpinner->removeFromParent();
                fields->m_loadingSpinner = nullptr;
            }

            if (success && tex && fields->m_paimonClipper) {
                log::info("[DailyLevelNode] thumbnail loaded OK: levelID={}", levelID);
                if (fields->m_paimonThumb) {
                    fields->m_paimonThumb->removeFromParent();
                }
                
                auto sprite = PaimonBlurSprite::createWithTexture(tex);
                sprite->m_texSize = tex->getContentSizeInPixels();
                fields->m_paimonThumb = sprite;

                // reuso el shader cacheado; los uniforms se actualizan en draw()
                if (auto* shader = Shaders::getOrCreateShader(
                    "paimon-daily-level-blur",
                    kVertexShaderDaily,
                    kFragmentShaderBlurDaily
                )) {
                    sprite->setShaderProgram(shader);
                }

                // hago aspect fill
                CCSize containerSize = fields->m_paimonClipper->getContentSize();
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

                fields->m_paimonClipper->addChild(sprite);
            }
        });

        return true;
    }
};

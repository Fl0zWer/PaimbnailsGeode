#include <Geode/modify/LevelCell.hpp>
#include <Geode/binding/DailyLevelNode.hpp>
#include <Geode/utils/cocos.hpp>
#include <Geode/loader/Mod.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/ui/LoadingSpinner.hpp>
#include <cmath>
#include <algorithm>
#include <string_view>
#include <random>
#include <unordered_map>
#include <unordered_set>
#include "../features/thumbnails/services/LocalThumbs.hpp"
#include "../features/thumbnails/services/LevelColors.hpp"
#include "../features/thumbnails/services/ThumbnailLoader.hpp"
#include "../managers/ThumbnailAPI.hpp"
#include "../utils/Constants.hpp"
#include "../utils/AnimatedGIFSprite.hpp"
#include "../utils/Shaders.hpp"
#include "../utils/PaimonShaderSprite.hpp"
#include "../utils/RetainedLazyTextureLoad.hpp"
#include "../features/thumbnails/ui/LevelCellSettingsPopup.hpp"
#include "../framework/compat/ModCompat.hpp"
#include "../utils/SpriteHelper.hpp"

using namespace geode::prelude;
using namespace Shaders;

// Enums for cached settings — avoid per-frame string comparisons
enum class PaimonAnimType : uint8_t {
    None, ZoomSlide, Zoom, Slide, Bounce, Rotate, RotateContent, Shake, Pulse, Swing
};

enum class PaimonAnimEffect : uint8_t {
    None, Brightness, Darken, Sepia, Sharpen, EdgeDetection, Vignette, Pixelate,
    Posterize, Chromatic, Scanlines, Solarize, Rainbow, Red, Blue, Gold, Fade,
    Grayscale, Invert, Blur, Glitch
};

enum class PaimonBgType : uint8_t { Gradient, Thumbnail };

enum class PaimonGalleryTransition : uint8_t {
    Crossfade, SlideLeft, SlideRight, SlideUp, SlideDown,
    ZoomIn, ZoomOut, FlipHorizontal, FlipVertical,
    RotateCW, RotateCCW, Cube, Dissolve, Swipe, Bounce,
    Random
};

static float safeCoverScale(float targetWidth, float targetHeight, float contentWidth, float contentHeight, float fallback = 1.0f) {
    if (targetWidth <= 0.0f || targetHeight <= 0.0f || contentWidth <= 0.0f || contentHeight <= 0.0f) {
        return fallback;
    }
    float scale = std::max(targetWidth / contentWidth, targetHeight / contentHeight);
    if (scale <= 0.0f) return fallback;
    return std::clamp(scale, 0.01f, 64.0f);
}

static float getLevelCellThumbWidthFactor() {
    float widthFactor = static_cast<float>(Mod::get()->getSettingValue<double>("level-thumb-width"));
    return std::clamp(widthFactor, PaimonConstants::MIN_THUMB_WIDTH_FACTOR, PaimonConstants::MAX_THUMB_WIDTH_FACTOR);
}

static float calculateLevelCellThumbCoverScale(CCSprite* sprite, float bgWidth, float bgHeight, float widthFactor, float fallback = 1.0f) {
    if (!sprite) {
        return fallback;
    }

    return safeCoverScale(
        bgWidth * widthFactor,
        bgHeight,
        sprite->getContentSize().width,
        sprite->getContentSize().height,
        fallback
    );
}

static std::vector<ThumbnailAPI::ThumbnailInfo> normalizeLevelCellGalleryThumbnails(
    int32_t levelID,
    std::vector<ThumbnailAPI::ThumbnailInfo> thumbnails
) {
    std::erase_if(thumbnails, [](ThumbnailAPI::ThumbnailInfo const& thumb) {
        return thumb.url.empty();
    });

    std::stable_sort(thumbnails.begin(), thumbnails.end(), [](ThumbnailAPI::ThumbnailInfo const& a, ThumbnailAPI::ThumbnailInfo const& b) {
        if (a.position != b.position) return a.position < b.position;
        if (a.id != b.id) return a.id < b.id;
        return a.url < b.url;
    });

    std::unordered_set<std::string> seenUrls;
    std::vector<ThumbnailAPI::ThumbnailInfo> normalized;
    normalized.reserve(thumbnails.size() + 1);

    for (auto& thumb : thumbnails) {
        if (!seenUrls.insert(thumb.url).second) {
            continue;
        }
        normalized.push_back(std::move(thumb));
    }

    if (normalized.empty() && levelID > 0) {
        ThumbnailAPI::ThumbnailInfo mainThumb;
        mainThumb.id = "0";
        mainThumb.url = ThumbnailAPI::get().getThumbnailURL(levelID);
        mainThumb.type = "static";
        mainThumb.position = 0;
        normalized.push_back(std::move(mainThumb));
    }

    return normalized;
}

static PaimonAnimType parseAnimType(std::string const& s) {
    if (s == "zoom-slide") return PaimonAnimType::ZoomSlide;
    if (s == "zoom") return PaimonAnimType::Zoom;
    if (s == "slide") return PaimonAnimType::Slide;
    if (s == "bounce") return PaimonAnimType::Bounce;
    if (s == "rotate") return PaimonAnimType::Rotate;
    if (s == "rotate-content") return PaimonAnimType::RotateContent;
    if (s == "shake") return PaimonAnimType::Shake;
    if (s == "pulse") return PaimonAnimType::Pulse;
    if (s == "swing") return PaimonAnimType::Swing;
    return PaimonAnimType::None;
}

static PaimonAnimEffect parseAnimEffect(std::string const& s) {
    if (s == "brightness") return PaimonAnimEffect::Brightness;
    if (s == "darken") return PaimonAnimEffect::Darken;
    if (s == "sepia") return PaimonAnimEffect::Sepia;
    if (s == "sharpen") return PaimonAnimEffect::Sharpen;
    if (s == "edge-detection") return PaimonAnimEffect::EdgeDetection;
    if (s == "vignette") return PaimonAnimEffect::Vignette;
    if (s == "pixelate") return PaimonAnimEffect::Pixelate;
    if (s == "posterize") return PaimonAnimEffect::Posterize;
    if (s == "chromatic") return PaimonAnimEffect::Chromatic;
    if (s == "scanlines") return PaimonAnimEffect::Scanlines;
    if (s == "solarize") return PaimonAnimEffect::Solarize;
    if (s == "rainbow") return PaimonAnimEffect::Rainbow;
    if (s == "red") return PaimonAnimEffect::Red;
    if (s == "blue") return PaimonAnimEffect::Blue;
    if (s == "gold") return PaimonAnimEffect::Gold;
    if (s == "fade") return PaimonAnimEffect::Fade;
    if (s == "grayscale") return PaimonAnimEffect::Grayscale;
    if (s == "invert") return PaimonAnimEffect::Invert;
    if (s == "blur") return PaimonAnimEffect::Blur;
    if (s == "glitch") return PaimonAnimEffect::Glitch;
    return PaimonAnimEffect::None;
}

static PaimonBgType parseBgType(std::string const& s) {
    return s == "thumbnail" ? PaimonBgType::Thumbnail : PaimonBgType::Gradient;
}

static PaimonGalleryTransition parseGalleryTransition(std::string const& s) {
    if (s == "crossfade") return PaimonGalleryTransition::Crossfade;
    if (s == "slide-left") return PaimonGalleryTransition::SlideLeft;
    if (s == "slide-right") return PaimonGalleryTransition::SlideRight;
    if (s == "slide-up") return PaimonGalleryTransition::SlideUp;
    if (s == "slide-down") return PaimonGalleryTransition::SlideDown;
    if (s == "zoom-in") return PaimonGalleryTransition::ZoomIn;
    if (s == "zoom-out") return PaimonGalleryTransition::ZoomOut;
    if (s == "flip-horizontal") return PaimonGalleryTransition::FlipHorizontal;
    if (s == "flip-vertical") return PaimonGalleryTransition::FlipVertical;
    if (s == "rotate-cw") return PaimonGalleryTransition::RotateCW;
    if (s == "rotate-ccw") return PaimonGalleryTransition::RotateCCW;
    if (s == "cube") return PaimonGalleryTransition::Cube;
    if (s == "dissolve") return PaimonGalleryTransition::Dissolve;
    if (s == "swipe") return PaimonGalleryTransition::Swipe;
    if (s == "bounce") return PaimonGalleryTransition::Bounce;
    if (s == "random") return PaimonGalleryTransition::Random;
    return PaimonGalleryTransition::Crossfade;
}

static PaimonGalleryTransition resolveRandomTransition() {
    static std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<int> dist(0, 14); // 0..14 = all types except Random
    return static_cast<PaimonGalleryTransition>(dist(rng));
}

class $modify(PaimonLevelCell, LevelCell) {
    static void onModify(auto& self) {
        // Dependemos de los node IDs para localizar/reordenar partes de la celda.
        (void)self.setHookPriorityAfterPost("LevelCell::loadFromLevel", "geode.node-ids");
        (void)self.setHookPriorityAfterPost("LevelCell::loadCustomLevelCell", "geode.node-ids");
    }

    struct Fields {
        Ref<CCClippingNode> m_clippingNode = nullptr;
        Ref<CCLayerColor> m_separator = nullptr;
        Ref<CCNode> m_gradient = nullptr;
        Ref<CCParticleSystemQuad> m_mythicParticles = nullptr;
        Ref<CCDrawNode> m_darkOverlay = nullptr;
        float m_gradientTime = 0.0f;
        ccColor3B m_gradientColorA = {0, 0, 0};
        ccColor3B m_gradientColorB = {0, 0, 0};
        Ref<CCSprite> m_gradientLayer = nullptr;
        Ref<geode::LoadingSpinner> m_loadingSpinner = nullptr;
        bool m_isBeingDestroyed = false;
        Ref<CCSprite> m_thumbSprite = nullptr;
        CCPoint m_thumbBasePos = {0.f, 0.f};
        CCPoint m_clipBasePos = {0.f, 0.f}; // posicion base del nodo de clipping
        CCPoint m_separatorBasePos = {0.f, 0.f}; // posicion base del separador
        float m_thumbBaseScaleX = 1.0f;
        float m_thumbBaseScaleY = 1.0f;
        bool m_thumbnailRequested = false; // pa evitar cargas duplicadas
        int m_requestId = 0; // id unico de request pa invalidar callbacks tardios
        int m_lastRequestedLevelID = 0; // ultimo levelID pedido pa detectar cambios
        bool m_thumbnailApplied = false; // pa no aplicar miniatura varias veces
        bool m_wasInCenter = false; // pa detectar cambios de estado
        float m_centerLerp = 0.0f; // interpolacion suave 0-1
        Ref<CCMenuItemSpriteExtra> m_viewOverlay = nullptr; // overlay invisible pa el boton
        
        float m_animTime = 0.0f;
        bool m_hasGif = false;
        Ref<CCTexture2D> m_staticTexture = nullptr;
        paimon::image::RetainedLazyTextureLoad m_staticThumbLoad;
        bool m_isHovering = false;
        float m_hoverCheckAccumulator = 0.0f;

        // cache de settings pa no leerlas cada frame (60fps)
        bool m_settingsCached = false;
        PaimonAnimType m_cachedAnimType = PaimonAnimType::ZoomSlide;
        float m_cachedAnimSpeed = 1.0f;
        PaimonAnimEffect m_cachedAnimEffect = PaimonAnimEffect::None;
        bool m_cachedHoverEnabled = true;
        bool m_cachedCompactMode = false;
        bool m_cachedEffectOnGradient = false;
        PaimonBgType m_cachedBgType = PaimonBgType::Gradient;
        PaimonGalleryTransition m_cachedGalleryTransition = PaimonGalleryTransition::Crossfade;
        float m_cachedTransitionDuration = 0.6f;
        bool m_isGalleryTransitioning = false; // guard: true while a gallery transition is animating

        // version de invalidacion: si cambia, recargar miniatura
        int m_loadedInvalidationVersion = 0;

        // version de settings del popup: si cambia, invalidar cache y re-aplicar
        int m_loadedSettingsVersion = 0;

        int m_cellLevelID = 0;
        bool m_isDailyCell = false;
        bool m_isDailyCellCached = false;
        std::vector<ThumbnailAPI::ThumbnailInfo> m_galleryThumbnails;
        std::unordered_map<std::string, Ref<CCTexture2D>> m_galleryTextureCache;
        std::unordered_set<std::string> m_galleryPendingUrls;
        int m_galleryIndex = -1;
        float m_galleryTimer = 0.f;
        bool m_galleryRequested = false;
        int m_galleryToken = 0;
        int m_invalidationListenerId = 0;
    };
    
    // destructor pa marcar celda como destruyendose
    // NOTA: no hacer unschedule aqui â€” Geode desaconseja logica pesada en
    // destructores de $modify. La limpieza se hace en onExit().
    ~PaimonLevelCell() {
        auto fields = m_fields.self();
        if (fields) {
            fields->m_isBeingDestroyed = true;
        }
    }
    
    // calcula escala de miniatura en LevelCell (respeta factor ancho, cubre altura)
    static void calculateLevelCellThumbScale(CCSprite* sprite, float bgWidth, float bgHeight, float widthFactor, float& outScaleX, float& outScaleY) {
        if (!sprite) return;
        
        const float contentWidth = sprite->getContentSize().width;
        const float contentHeight = sprite->getContentSize().height;
        if (contentWidth <= 0.f || contentHeight <= 0.f || bgWidth <= 0.f || bgHeight <= 0.f) {
            outScaleX = 1.f;
            outScaleY = 1.f;
            return;
        }
        const float desiredWidth = bgWidth * widthFactor;
        
        // escalar pa cubrir altura exacta
        outScaleY = bgHeight / contentHeight;
        
        // ancho: usar el deseado pero no bajar de lo necesario pa aspect ratio
        float minScaleX = outScaleY; // mantener aspect ratio
        float desiredScaleX = desiredWidth / contentWidth;
        outScaleX = std::max(minScaleX, desiredScaleX);
    }
    
    // calcula escala de miniatura pa popups (cobertura total con margen)
    static void calculateFullCoverageThumbScale(CCSprite* sprite, float targetWidth, float targetHeight, float& outScale) {
        if (!sprite) return;
        
        const float contentWidth = sprite->getContentSize().width;
        const float contentHeight = sprite->getContentSize().height;
        if (contentWidth <= 0.f || contentHeight <= 0.f || targetWidth <= 0.f || targetHeight <= 0.f) {
            outScale = 1.f;
            return;
        }

        // usar la escala mayor pa cubrir todo, con margen de seguridad
        outScale = safeCoverScale(targetWidth, targetHeight, contentWidth, contentHeight, 1.f) * 1.15f;
    }
    
    void showLoadingSpinner() {
        auto fields = m_fields.self();
        
        // quitar spinner existente si hay
        if (fields->m_loadingSpinner) {
            fields->m_loadingSpinner->removeFromParent();
            fields->m_loadingSpinner = nullptr;
        }
        
        // crear spinner usando geode::LoadingSpinner (10px diametro Ã¢â€°Ë† loadingCircle.png * 0.25)
        auto spinner = geode::LoadingSpinner::create(10.f);
        
        // posicionar en el centro del area de miniatura
        auto bg = m_backgroundLayer;
        if (bg) {
            auto cs = bg->getContentSize();
            spinner->setPosition({cs.width - 75.f, cs.height / 2.f});
        } else {
            spinner->setPosition({PaimonConstants::LEVELCELL_SPINNER_FALLBACK_X, PaimonConstants::LEVELCELL_SPINNER_FALLBACK_Y});
        }
        
        spinner->setZOrder(999);
        
        spinner->setID("paimon-loading-spinner"_spr);
        
        this->addChild(spinner);
        fields->m_loadingSpinner = spinner;
        
        // fade-in suave
        spinner->setOpacity(0);
        spinner->runAction(CCFadeTo::create(0.3f, 180));
    }
    
    void hideLoadingSpinner() {
        auto fields = m_fields.self();
        if (fields->m_loadingSpinner) {
            // animacion fade out y remover
            auto* spinnerNode = static_cast<geode::LoadingSpinner*>(fields->m_loadingSpinner);
            spinnerNode->runAction(CCSequence::create(
                CCFadeOut::create(0.2f),
                CCCallFunc::create(spinnerNode, callfunc_selector(CCNode::removeFromParent)),
                nullptr
            ));
            
            fields->m_loadingSpinner = nullptr;
        }
    }

    void configureThumbnailLoader() {
        static bool s_loaderConfigured = false;
        if (!s_loaderConfigured) {
            int maxDownloads = static_cast<int>(Mod::get()->getSettingValue<int64_t>("thumbnail-concurrent-downloads"));
            ThumbnailLoader::get().setMaxConcurrentTasks(maxDownloads);
            s_loaderConfigured = true;
        }
    }

    static bool supportsLazyStaticThumbnailPath(std::string const& path) {
        auto lowerPath = geode::utils::string::toLower(path);
        return lowerPath.ends_with(".png") ||
            lowerPath.ends_with(".jpg") ||
            lowerPath.ends_with(".jpeg") ||
            lowerPath.ends_with(".webp");
    }

    static std::optional<std::string> resolveLazyStaticThumbnailPath(int32_t levelID) {
        auto path = LocalThumbs::get().findAnyThumbnail(levelID);
        if (!path || !supportsLazyStaticThumbnailPath(*path)) {
            return std::nullopt;
        }
        return path;
    }

    bool shouldHandleThumbnailCallback(int32_t levelID, int currentRequestId) {
        auto fields = m_fields.self();
        return this->getParent() &&
            fields &&
            !fields->m_isBeingDestroyed &&
            fields->m_requestId == currentRequestId &&
            m_level &&
            m_level->m_levelID == levelID;
    }

    void flashThumbnailSprite() {
        auto fields = m_fields.self();
        if (!fields || !fields->m_thumbSprite) {
            return;
        }

        auto flash = CCLayerColor::create({255, 255, 255, 255});
        flash->setContentSize(fields->m_thumbSprite->getContentSize());
        flash->setBlendFunc({GL_SRC_ALPHA, GL_ONE});
        fields->m_thumbSprite->addChild(flash, 100);
        flash->runAction(CCSequence::create(CCFadeOut::create(0.5f), CCRemoveSelf::create(), nullptr));
    }

    void applyMainLevelFallbackThumbnail(int32_t levelID) {
        if (levelID <= 0 || levelID > 100) {
            return;
        }

        auto* blackTex = new CCTexture2D();
        uint8_t blackPixel[4] = {0, 0, 0, 255};
        if (blackTex->initWithData(blackPixel, kCCTexture2DPixelFormat_RGBA8888, 1, 1, CCSize(1, 1))) {
            blackTex->autorelease();
            this->addOrUpdateThumb(blackTex);
        } else {
            blackTex->release();
        }
    }

    void applyStaticThumbnailTexture(int32_t levelID, int currentRequestId, CCTexture2D* texture, bool enableSpinners) {
        if (!this->shouldHandleThumbnailCallback(levelID, currentRequestId)) {
            return;
        }

        auto fields = m_fields.self();
        fields->m_staticThumbLoad.reset();

        if (enableSpinners) {
            this->hideLoadingSpinner();
        }

        if (fields->m_thumbnailApplied) {
            return;
        }

        if (!texture) {
            this->applyMainLevelFallbackThumbnail(levelID);
            return;
        }

        fields->m_thumbnailApplied = true;
        fields->m_staticTexture = texture;
        this->addOrUpdateThumb(texture);
        this->flashThumbnailSprite();
    }

    void startLazyStaticThumbnailLoad(int32_t levelID, int currentRequestId, bool enableSpinners, CCTexture2D* fallbackTexture) {
        auto fields = m_fields.self();
        if (!fields) {
            return;
        }

        auto path = resolveLazyStaticThumbnailPath(levelID);
        if (!path) {
            this->applyStaticThumbnailTexture(levelID, currentRequestId, fallbackTexture, enableSpinners);
            return;
        }

        WeakRef<PaimonLevelCell> safeRef = this;
        Ref<CCTexture2D> fallbackRef = fallbackTexture;
        fields->m_staticThumbLoad.loadFromFile(std::filesystem::path(*path), [
            safeRef,
            levelID,
            currentRequestId,
            enableSpinners,
            fallbackRef
        ](CCTexture2D* texture, bool success) {
            auto cellRef = safeRef.lock();
            auto* cell = static_cast<PaimonLevelCell*>(cellRef.data());
            if (!cell) {
                return;
            }

            cell->applyStaticThumbnailTexture(
                levelID,
                currentRequestId,
                success ? texture : fallbackRef.data(),
                enableSpinners
            );
        });
    }

    void cleanPaimonNodes(CCNode* bg) {
        auto fields = m_fields.self();
        
        // eliminacion segura de nodos rastreados
        auto removeNodeSafe = [](auto& node) {
            if (node) {
                if (node->getParent()) node->removeFromParent();
                node = nullptr; 
            }
        };

        removeNodeSafe(fields->m_clippingNode);
        removeNodeSafe(fields->m_separator);
        removeNodeSafe(fields->m_gradient);
        
        // m_mythicParticles is a CCParticleSystemQuad*, manual handling or cast
        if (fields->m_mythicParticles) {
            if (fields->m_mythicParticles->getParent()) fields->m_mythicParticles->removeFromParent();
            fields->m_mythicParticles = nullptr;
        }

        removeNodeSafe(fields->m_darkOverlay);
        
        // anular otras referencias
        fields->m_gradientLayer = nullptr;
        fields->m_thumbSprite = nullptr;
        fields->m_staticThumbLoad.reset();
        fields->m_loadingSpinner = nullptr; // spinner suele gestionarse con show/hide, limpiar aqui por seguridad

        // limpiar restos con id "paimon" en bg y this
        auto cleanByID = [](CCNode* parent) {
            if (!parent) return;
            auto children = parent->getChildren();
            if (!children) return;
            std::vector<CCNode*> toRemove;
            for (auto* child : CCArrayExt<CCNode*>(children)) {
                if (child && std::string_view(child->getID()).find("paimon") != std::string_view::npos) {
                    toRemove.push_back(child);
                }
            }
            for (auto* node : toRemove) node->removeFromParent();
        };
        cleanByID(bg);
        cleanByID(this);
    }

    // setupDarkMode removed Ã¢â‚¬â€ was empty (dead code)

    CCSprite* createThumbnailSprite(CCTexture2D* texture, bool allowLevelGIF = true) {
        CCSprite* sprite = PaimonShaderSprite::createWithTexture(texture);
        if (!sprite) return nullptr;

        int32_t levelIDForGIF = m_level ? m_level->m_levelID.value() : 0;
        
        if (allowLevelGIF && levelIDForGIF > 0 && ThumbnailLoader::get().hasGIFData(levelIDForGIF)) {
            auto path = ThumbnailLoader::get().getCachePath(levelIDForGIF, true);
            
            WeakRef<PaimonLevelCell> safeRef = this;
            AnimatedGIFSprite::createAsync(geode::utils::string::pathToString(path), [safeRef, levelIDForGIF](AnimatedGIFSprite* anim) {
                auto cellRef = safeRef.lock();
                auto* cell = static_cast<PaimonLevelCell*>(cellRef.data());
                if (!cell) return;
                if (!cell->getParent()) return; // cell removed from scene
                if (cell->m_level && cell->m_level->m_levelID == levelIDForGIF) {
                    auto fields = cell->m_fields.self();
                    if (!fields || fields->m_isBeingDestroyed) return;
                    if (anim && fields->m_thumbSprite) {
                        auto old = fields->m_thumbSprite;
                        auto parent = old->getParent();
                        if (parent) {
                            anim->setScaleX(old->getScaleX());
                            anim->setScaleY(old->getScaleY());
                            anim->setPosition(old->getPosition());
                            anim->setAnchorPoint(old->getAnchorPoint());
                            anim->setSkewX(old->getSkewX());
                            anim->setSkewY(old->getSkewY());
                            anim->setZOrder(old->getZOrder());
                            anim->setColor(old->getColor());
                            anim->setOpacity(old->getOpacity());
                            anim->setID("paimon-thumbnail"_spr);
                            
                            old->removeFromParent();
                            parent->addChild(anim);
                            fields->m_thumbSprite = anim;
                        }
                    }
                }
            });
        }
        
        if (sprite) {
            sprite->setID("paimon-thumbnail"_spr);
            // asegurar id pa deteccion de shader si es nuestro sprite custom
            if (auto pss = typeinfo_cast<PaimonShaderSprite*>(sprite)) {
                 pss->setID("paimon-shader-sprite"_spr);
            }
            
            std::string bgType = Mod::get()->getSettingValue<std::string>("levelcell-background-type");

            if (bgType == "thumbnail") {
                // Si ImagePlus esta activo, verificar que la textura no tenga
                // animacion adjunta antes de aplicar shaders (ImagePlus hookea
                // CCSprite::initWithTexture y puede añadir scheduler de animacion
                // que conflictua con nuestros shaders custom)
                bool skipShader = false;
                if (paimon::compat::ModCompat::isImagePlusLoaded()) {
                    // ImagePlus usa schedule_selector(ImagePlusSprite::animationUpdate)
                    // Verificamos si el sprite tiene acciones/schedulers no nuestros
                    if (sprite->numberOfRunningActions() > 0) {
                        skipShader = true;
                    }
                }
                
                if (!skipShader) {
                    auto shader = getOrCreateShader("paimon_cell_saturation", vertexShaderCell, fragmentShaderSaturationCell);
                    if (shader) {
                        // aplicar boost saturacion/brillo solo a imagenes estaticas (PaimonShaderSprite)
                        if (auto pss = typeinfo_cast<PaimonShaderSprite*>(sprite)) {
                            sprite->setShaderProgram(shader);
                            float saturation = 2.5f;
                            float brightness = 3.0f;
                            pss->m_intensity = saturation;
                            pss->m_brightness = brightness;
                        }
                        // AnimatedGIFSprite (GIFs) se renderiza con shader por defecto (sin efecto)
                    }
                }
            }
        }
        return sprite;
    }

    CCClippingNode* createThumbnailClippingNode(CCNode* bg, CCSprite* sprite, float& outCoverScale) {
        if (!bg || !sprite) {
            log::warn("[LevelCell] createThumbnailClippingNode: null bg or sprite");
            return nullptr;
        }

        float kThumbWidthFactor = getLevelCellThumbWidthFactor();
        const float bgWidth = bg->getContentWidth();
        const float bgHeight = bg->getContentHeight();
        const float desiredWidth = bgWidth * kThumbWidthFactor;

        float scaleX, scaleY;
        calculateLevelCellThumbScale(sprite, bgWidth, bgHeight, kThumbWidthFactor, scaleX, scaleY);
        outCoverScale = calculateLevelCellThumbCoverScale(sprite, bgWidth, bgHeight, kThumbWidthFactor, scaleX);
        sprite->setScale(outCoverScale);
        log::debug("[LevelCell] createThumbnailClippingNode: bgSize=({:.1f},{:.1f}) widthFactor={:.2f} coverScale={:.4f} scaleX={:.4f} scaleY={:.4f}",
            bgWidth, bgHeight, kThumbWidthFactor, outCoverScale, scaleX, scaleY);

        CCSize scaledSize{ desiredWidth, bgHeight };
        const float kDiagonalSkew = 35.f; // desplazamiento diagonal del borde izquierdo
        auto drawMask = paimon::SpriteHelper::createDiagonalStencil(scaledSize.width, scaledSize.height, kDiagonalSkew);
        drawMask->setAnchorPoint({1,0});
        drawMask->ignoreAnchorPointForPosition(true);

        auto clippingNode = CCClippingNode::create();
        if (!clippingNode) return nullptr;

        clippingNode->setStencil(drawMask);
        clippingNode->setContentSize(scaledSize);
        clippingNode->setAnchorPoint({1,0});
        clippingNode->setPosition({ bgWidth, 0.f });
        clippingNode->setID("paimon-clipping-node"_spr);
        clippingNode->setZOrder(-1);

        sprite->setPosition(clippingNode->getContentSize() * 0.5f);
        clippingNode->addChild(sprite);
        return clippingNode;
    }

    void setupClippingAndSeparator(CCNode* bg, CCSprite* sprite) {
        auto fields = m_fields.self();
        if (!fields) return;
        log::info("[LevelCell] setupClippingAndSeparator: entering");

        // forzar ancho completo pa celdas Daily
        bool isDaily = false;
        if (m_level && m_level->m_dailyID > 0) isDaily = true;

        float coverScale = 1.0f;
        auto clippingNode = createThumbnailClippingNode(bg, sprite, coverScale);
        if (!clippingNode) return;

        // Clipper maestro que limita todo al area visible de la celda.
        // Evita que el hover-zoom y la miniatura desborden los bordes de la celda.
        auto boundsStencil = paimon::SpriteHelper::createRectStencil(bg->getContentWidth(), bg->getContentHeight());
        auto boundsClipper = CCClippingNode::create(boundsStencil);
        boundsClipper->setContentSize(bg->getContentSize());
        boundsClipper->setPosition(bg->getPosition());
        boundsClipper->setAnchorPoint({0, 0});
        boundsClipper->setZOrder(-1);
        boundsClipper->setID("paimon-bounds-clipper"_spr);
        this->addChild(boundsClipper);

        // La miniatura va dentro del bounds clipper
        boundsClipper->addChild(clippingNode);

        fields->m_thumbSprite = sprite;
        fields->m_thumbBasePos = sprite->getPosition();
        fields->m_clipBasePos = clippingNode->getPosition();
        fields->m_thumbBaseScaleX = coverScale;
        fields->m_thumbBaseScaleY = coverScale;
        log::info("[LevelCell] setupClippingAndSeparator: coverScale={:.4f} clipPos=({:.1f},{:.1f}) thumbPos=({:.1f},{:.1f})",
            coverScale, clippingNode->getPosition().x, clippingNode->getPosition().y, sprite->getPosition().x, sprite->getPosition().y);
        
        bool hoverEnabled = Mod::get()->getSettingValue<bool>("levelcell-hover-effects");

        if (hoverEnabled) {
            this->unschedule(schedule_selector(PaimonLevelCell::checkCenterPosition));
            this->unschedule(schedule_selector(PaimonLevelCell::updateCenterAnimation));
            this->schedule(schedule_selector(PaimonLevelCell::checkCenterPosition), 0.05f);
            this->schedule(schedule_selector(PaimonLevelCell::updateCenterAnimation));
        }

        fields->m_clippingNode = clippingNode;

        bool showSeparator = Mod::get()->getSettingValue<bool>("levelcell-show-separator");
        const float bgWidth = bg->getContentWidth();
        CCSize scaledSize = clippingNode->getContentSize();

        if (showSeparator && !isDaily) { // No separator for Daily
            float separatorXMul = m_compactView ? 0.75f : 1.0f;
            auto separator = CCLayerColor::create({0,0,0});
            separator->setZOrder(-2);
            separator->setOpacity(50);
            separator->setScaleX(0.45f);
            separator->ignoreAnchorPointForPosition(false);
            separator->setContentSize(scaledSize);
            separator->setAnchorPoint({1,0});
            separator->setPosition({bgWidth - separator->getContentWidth()/2 - (20.f * separatorXMul), 0.f});
            separator->setID("paimon-separator"_spr);

            fields->m_separator = separator;
            fields->m_separatorBasePos = separator->getPosition();
            boundsClipper->addChild(separator);
        }
    }

    void setupGradient(CCNode* bg, int levelID, CCTexture2D* texture) {
        auto fields = m_fields.self();
        log::info("[LevelCell] setupGradient: levelID={} hasTexture={}", levelID, texture != nullptr);

        // Clean up previous background nodes
        if (auto children = bg->getChildren()) {
            std::vector<CCNode*> toRemove;
            for (auto* child : CCArrayExt<CCNode*>(children)) {
                if (!child) continue;
                std::string_view childID = child->getID();
                if (childID.find("paimon-level-gradient") != std::string_view::npos ||
                    childID.find("paimon-bg-clipper") != std::string_view::npos ||
                    childID == "paimon-level-background") {
                    toRemove.push_back(child);
                }
            }
            for (auto node : toRemove) node->removeFromParent();
        }
        fields->m_gradientLayer = nullptr;

        cacheSettings();
        PaimonBgType bgType = fields->m_cachedBgType;

        if (bgType == PaimonBgType::Thumbnail && texture) {
             CCSprite* bgSprite = nullptr;
             
             float blurIntensity = static_cast<float>(Mod::get()->getSettingValue<double>("levelcell-background-blur"));

             if (ThumbnailLoader::get().hasGIFData(levelID)) {
                 auto path = ThumbnailLoader::get().getCachePath(levelID, true);
                 WeakRef<PaimonLevelCell> gradSafeRef = this;
                 AnimatedGIFSprite::createAsync(geode::utils::string::pathToString(path), [gradSafeRef, levelID, blurIntensity](AnimatedGIFSprite* anim) {
                     auto selfRef = gradSafeRef.lock();
                     auto* self = static_cast<PaimonLevelCell*>(selfRef.data());
                     if (!self) return;
                     if (!self->getParent()) return;
                     if (self->m_level && self->m_level->m_levelID == levelID) {
                         if (anim) {
                             if (auto bg = self->m_mainLayer) {
                                 if (auto clipper = bg->getChildByID("paimon-bg-clipper"_spr)) {
                                     clipper->removeAllChildren();
                                     
                                    float targetW = clipper->getContentWidth();
                                    float targetH = clipper->getContentHeight();
                                    float scale = safeCoverScale(
                                        targetW, targetH,
                                        anim->getContentWidth(), anim->getContentHeight(),
                                        1.0f
                                    );
                                     anim->setScale(scale);
                                     anim->setPosition(clipper->getContentSize() / 2);
                                     
                                     // Apply blur shader to GIF
                                     auto shader = Shaders::getBlurCellShader();
                                     if (shader) {
                                         anim->setShaderProgram(shader);
                                         anim->m_intensity = blurIntensity;
                                         if (auto* animTex = anim->getTexture()) {
                                             anim->m_texSize = animTex->getContentSizeInPixels();
                                         } else {
                                             anim->m_texSize = anim->getContentSize();
                                         }
                                     }
                                     
                                     clipper->addChild(anim);
                                 }
                             }
                         }
                     }
                 });
             }

             if (!bgSprite) {
                 // Static image - blur centralizado en Shaders.hpp
                 CCSize targetSize = bg->getContentSize();
                 targetSize.width = std::max(targetSize.width, 512.f);
                 targetSize.height = std::max(targetSize.height, 256.f);

                 bgSprite = Shaders::createBlurredSprite(texture, targetSize, blurIntensity);
                 if (!bgSprite) {
                     bgSprite = PaimonShaderSprite::createWithTexture(texture);
                 }
             }
             
             if (bgSprite) {
                auto stencil = paimon::SpriteHelper::createRectStencil(bg->getContentWidth(), bg->getContentHeight());

                 auto clipper = CCClippingNode::create(stencil);
                 clipper->setContentSize(bg->getContentSize());
                 // No alpha threshold for geometric stencil
                 clipper->setPosition({0,0});
                 clipper->setZOrder(10); // Same Z as gradient
                 clipper->setID("paimon-bg-clipper"_spr);

                 float targetW = bg->getContentWidth();
                 float targetH = bg->getContentHeight();

                float scale = safeCoverScale(
                    targetW, targetH,
                    bgSprite->getContentSize().width, bgSprite->getContentSize().height,
                    1.0f
                );
                 bgSprite->setScale(scale);
                 
                 bgSprite->setPosition(bg->getContentSize() / 2);
                 clipper->addChild(bgSprite);

                 float darkness = static_cast<float>(Mod::get()->getSettingValue<double>("levelcell-background-darkness"));
                 GLubyte opacity = static_cast<GLubyte>(std::clamp(darkness, 0.0f, 1.0f) * 255.0f);

                 auto overlay = paimon::SpriteHelper::createDarkPanel(bg->getContentWidth(), bg->getContentHeight(), opacity, 0.f);
                 overlay->setPosition({0, 0});
                 clipper->addChild(overlay);
                 fields->m_darkOverlay = overlay;

                 bg->addChild(clipper);
                 bg->reorderChild(clipper, 10);
                 fields->m_gradientLayer = bgSprite;

                 // Degradado suave: gradiente en el borde derecho del blur
                 // que va desapareciendo hacia la miniatura. Se extiende un
                 // poco mas alla del blur para crear una transicion natural.
                 if (fields->m_clippingNode) {
                     float thumbLeft = bg->getContentWidth() - fields->m_clippingNode->getContentSize().width;
                     // gradiente que cubre desde ~40px antes del borde de la miniatura
                     // hasta ~25px dentro de la miniatura (overlap)
                     float fadeW = 65.f;
                     float fadeH = bg->getContentHeight();
                     float fadeX = thumbLeft - 40.f; // empieza antes del borde

                     auto blurFade = CCLayerGradient::create(
                         ccc4(0, 0, 0, 0),        // izquierda: transparente (blur visible)
                         ccc4(0, 0, 0, opacity),   // derecha: oscuro (match dark overlay)
                         ccp(1, 0)                  // direccion izq→der
                     );
                     blurFade->ignoreAnchorPointForPosition(false);
                     blurFade->setAnchorPoint({0, 0});
                     blurFade->setContentSize({fadeW, fadeH});
                     blurFade->setPosition({std::max(0.f, fadeX), 0});
                     blurFade->setID("paimon-blur-fade"_spr);
                     clipper->addChild(blurFade, 2);
                 }

                 return;
             }
        }

        ccColor3B colorA = {0, 0, 0};
        ccColor3B colorB = {255, 0, 0};

        if (auto pair = LevelColors::get().getPair(levelID)) {
            colorA = pair->a;
            colorB = pair->b;
        }

        bool animatedGradient = Mod::get()->getSettingValue<bool>("levelcell-animated-gradient");

        auto grad = PaimonShaderGradient::create(
            ccc4(colorA.r, colorA.g, colorA.b, 255),
            ccc4(colorB.r, colorB.g, colorB.b, 255)
        );
        grad->setContentSize({ bg->getContentWidth() + 2.f, bg->getContentHeight() + 1.f });
        grad->setAnchorPoint({0,0});
        grad->setPosition({0.0f, 0.0f}); // Reset to 0,0 relative to this if bg is at 0,0? No, bg checks its pos
        
        // Fix: Add gradient to 'this' instead of 'bg' to avoid potential BatchNode issues (Happy Textures)
        // Ensure z-order is below content but similar to bg
        int bgZ = bg->getZOrder();
        grad->setZOrder(bgZ - 1); // Behind bg
        grad->setID("paimon-level-gradient"_spr);
        
        // We need to position 'grad' same as 'bg'.
        grad->setPosition(bg->getPosition());
        // Bg anchor is usually 0,0?
        if (bg->isIgnoreAnchorPointForPosition()) {
             // If bg ignores anchor, its position is bottom-left.
             // grad should mimic.
        }
        
        // Simply hide the original background and place ours
        bg->setVisible(false);
        this->addChild(grad);
        
        // fields->m_gradient = grad; // m_gradient is CCNode* in struct
        fields->m_gradient = grad;
        // bg->addChild(grad); // REMOVED
        // bg->reorderChild(grad, 10); // REMOVED

        fields->m_gradientLayer = grad;
        fields->m_gradientColorA = colorA;
        fields->m_gradientColorB = colorB;

        if (animatedGradient) {
            this->schedule(schedule_selector(PaimonLevelCell::updateGradientAnim), 0.0f);
        }
    }

    void setupMythicParticles(CCNode* bg, int levelID) {
        auto fields = m_fields.self();
        bool enableMythic = Mod::get()->getSettingValue<bool>("levelcell-mythic-particles");

        if (enableMythic && m_level && m_level->m_isEpic >= 3) {
                auto brighten = [](ccColor3B c) {
                    auto clamp = [](int v){ return std::max(0, std::min(255, v)); };
                    int add = 35;
                    return ccColor3B{ (GLubyte)clamp(c.r + add), (GLubyte)clamp(c.g + add), (GLubyte)clamp(c.b + add) };
                };
                ccColor3B ca{220,220,220}, cb{255,255,255};
                if (auto pair2 = LevelColors::get().getPair(levelID)) {
                    ca = brighten(pair2->a);
                    cb = brighten(pair2->b);
                }
                
                auto ps = CCParticleSystemQuad::create();
                if (!ps) return;
                
                ps->setBlendAdditive(false);
                ps->setID("paimon-mythic-particles"_spr);
                ps->setEmitterMode(kCCParticleModeGravity);
                ps->setGravity({0.f, 0.f});
                ps->setAngle(0.f);
                ps->setAngleVar(6.f);
                
                float width = bg->getContentWidth();
                float speed = 160.f;
                float life = (0.70f * width) / speed;
                if (life < 0.4f) life = 0.4f;
                
                ps->setSpeed(speed);
                ps->setSpeedVar(20.f);
                ps->setLife(life);
                ps->setLifeVar(life * 0.15f);
                
                float height = bg->getContentHeight();
                ps->setPosition({0.f, height * 0.5f});
                ps->setPosVar({0.f, height * 0.5f});
                
                ps->setStartSize(3.0f);
                ps->setStartSizeVar(1.2f);
                ps->setEndSize(2.0f);
                ps->setEndSizeVar(1.0f);
                
                ccColor4F startColorA{ ca.r / 255.f, ca.g / 255.f, ca.b / 255.f, 0.80f };
                ccColor4F startColorB{ cb.r / 255.f, cb.g / 255.f, cb.b / 255.f, 0.80f };
                ccColor4F base{
                    (startColorA.r + startColorB.r) * 0.5f,
                    (startColorA.g + startColorB.g) * 0.5f,
                    (startColorA.b + startColorB.b) * 0.5f,
                    0.80f
                };
                ccColor4F var{
                    fabsf(startColorA.r - startColorB.r) * 0.5f,
                    fabsf(startColorA.g - startColorB.g) * 0.5f,
                    fabsf(startColorA.b - startColorB.b) * 0.5f,
                    0.05f
                };
                ps->setStartColor(base);
                ps->setStartColorVar(var);
                ccColor4F end = base; end.a = 0.f;
                ccColor4F endVar = var; endVar.a = 0.05f;
                ps->setEndColor(end);
                ps->setEndColorVar(endVar);
                
                ps->setTotalParticles(120);
                ps->setEmissionRate(120.f / life);
                ps->setDuration(-1.f);
                ps->setPositionType(kCCPositionTypeRelative);
                ps->setAutoRemoveOnFinish(false);
                
                fields->m_mythicParticles = ps;
                
                // Fix: Add particles to 'this' instead of 'bg'
                ps->setPosition(ps->getPosition() + bg->getPosition());
                this->addChild(ps, bg->getZOrder() + 1); // Above bg
                
                ps->resetSystem();
        }
    }

    // Helper: check if a menu item is the "view" button by ID or label text
    static bool isViewButtonItem(CCMenuItemSpriteExtra* menuItem, bool checkDailyPos, bool isDaily, CCSize const& cellSize) {
        std::string_view id = menuItem->getID();
        if (id == "view-button" || id == "main-button" || id == "paimon-view-button") return true;
        if (id == "paimon-view-button"_spr) return true;

        if (auto ni = menuItem->getNormalImage()) {
            if (auto ch = ni->getChildren()) {
                for (auto* child : CCArrayExt<CCNode*>(ch)) {
                    if (auto lbl = typeinfo_cast<CCLabelBMFont*>(child)) {
                        auto tl = geode::utils::string::toLower(std::string(lbl->getString()));
                        if (tl.find("view") != std::string::npos || tl.find("ver") != std::string::npos ||
                            tl.find("get it") != std::string::npos || tl.find("play") != std::string::npos ||
                            tl.find("safe") != std::string::npos) {
                            return true;
                        }
                    }
                }
            }
        }

        if (checkDailyPos && isDaily) {
            if (menuItem->getPosition().x > cellSize.width * 0.4f) return true;
        }
        return false;
    }

    // Unified view button finder — single DFS replaces two duplicated search passes
    void findAndSetupViewButton() {
        auto fields = m_fields.self();
        bool isDaily = isDailyCell();

        bool showButton = Mod::get()->getSettingValue<bool>("levelcell-show-view-button");
        if (showButton) return;

        auto cellSize = this->getContentSize();
        constexpr float kAreaWidth = 90.f;
        float areaHeight = cellSize.height;

        // Single DFS traversal over all children
        std::vector<CCNode*> stack;
        stack.reserve(32);
        stack.push_back(this);

        while (!stack.empty()) {
            CCNode* cur = stack.back();
            stack.pop_back();
            if (!cur) continue;

            if (auto menuItem = typeinfo_cast<CCMenuItemSpriteExtra*>(cur)) {
                if (isViewButtonItem(menuItem, true, isDaily, cellSize)) {
                    if (std::string_view(menuItem->getID()) == "paimon-view-button" ||
                        std::string_view(menuItem->getID()) == "paimon-view-button"_spr) {
                        menuItem->setID("view-button");
                    }
                    fields->m_viewOverlay = menuItem;
                    menuItem->m_baseScale = menuItem->getScale();
                    menuItem->setVisible(true);
                    menuItem->setEnabled(true);

                    if (!isDaily) {
                        // Create invisible overlay sprites (no CCLayerColor needed — alpha is always 0)
                        auto makeInvisible = [kAreaWidth, areaHeight]() {
                            auto s = CCSprite::create();
                            s->setContentSize({kAreaWidth, areaHeight});
                            s->setAnchorPoint({0.5f, 0.5f});
                            return s;
                        };

                        if (auto img = menuItem->getNormalImage()) img->setVisible(false);
                        if (auto img = menuItem->getSelectedImage()) img->setVisible(false);
                        if (auto img = menuItem->getDisabledImage()) img->setVisible(false);

                        menuItem->setNormalImage(makeInvisible());
                        menuItem->setSelectedImage(makeInvisible());
                        menuItem->setDisabledImage(makeInvisible());

                        // Position overlay at thumbnail edge
                        CCPoint centerLocal;
                        if (fields->m_clippingNode) {
                            CCPoint clipPos = fields->m_clippingNode->getPosition();
                            centerLocal = CCPoint(clipPos.x - kAreaWidth / 2.f - 15.f, cellSize.height / 2.f - 1.f);
                        } else {
                            centerLocal = CCPoint(cellSize.width - kAreaWidth / 2.f - 15.f, cellSize.height / 2.f - 1.f);
                        }
                        CCPoint centerWorld = this->convertToWorldSpace(centerLocal);
                        if (auto parentNode = menuItem->getParent()) {
                            parentNode->setVisible(true);
                            menuItem->setPosition(parentNode->convertToNodeSpace(centerWorld));
                        } else {
                            menuItem->setPosition(centerLocal);
                        }
                    }
                    return; // Found and configured
                }
            }

            if (auto arr = cur->getChildren()) {
                for (auto* child : CCArrayExt<CCNode*>(arr)) {
                    if (child) stack.push_back(child);
                }
            }
        }
    }

    // Aplica la transicion configurada manteniendo el cover-scale dentro del clip.
    void applyGalleryTransition(CCNode* newNode, CCSprite* newSprite,
                                CCNode* oldNode, CCSprite* oldSprite,
                                PaimonGalleryTransition type, float dur, CCSize clipSize) {
        if (!newNode || !newSprite) {
            log::warn("[LevelCell] applyGalleryTransition: null node/sprite");
            return;
        }
        if (type == PaimonGalleryTransition::Random)
            type = resolveRandomTransition();

        CCPoint targetPos = newNode->getPosition();
        float sx = newNode->getScaleX();
        float sy = newNode->getScaleY();
        log::info("[LevelCell] applyGalleryTransition: type={} dur={:.2f} targetPos=({:.1f},{:.1f}) sx={:.3f} sy={:.3f} hasOld={}",
            static_cast<int>(type), dur, targetPos.x, targetPos.y, sx, sy, oldNode != nullptr);
        float halfDur = dur * 0.5f;
        float removeDelay = dur + 0.05f;

        switch (type) {

        case PaimonGalleryTransition::Crossfade: {
            newSprite->setOpacity(0);
            newSprite->runAction(CCFadeTo::create(dur, 255));
            if (oldNode) oldNode->runAction(CCSequence::create(
                CCDelayTime::create(removeDelay),
                CCCallFunc::create(oldNode, callfunc_selector(CCNode::removeFromParent)), nullptr));
        } break;

        case PaimonGalleryTransition::SlideLeft: {
            newNode->setPosition({targetPos.x + clipSize.width, targetPos.y});
            newNode->runAction(CCEaseOut::create(CCMoveTo::create(dur, targetPos), 2.5f));
            if (oldNode) oldNode->runAction(CCSequence::create(
                CCSpawn::create(
                    CCEaseIn::create(CCMoveTo::create(dur, {targetPos.x - clipSize.width, targetPos.y}), 2.0f),
                    oldSprite ? static_cast<CCFiniteTimeAction*>(CCFadeTo::create(dur * 0.8f, 0)) : static_cast<CCFiniteTimeAction*>(CCDelayTime::create(dur)),
                    nullptr),
                CCCallFunc::create(oldNode, callfunc_selector(CCNode::removeFromParent)), nullptr));
        } break;

        case PaimonGalleryTransition::SlideRight: {
            newNode->setPosition({targetPos.x - clipSize.width, targetPos.y});
            newNode->runAction(CCEaseOut::create(CCMoveTo::create(dur, targetPos), 2.5f));
            if (oldNode) oldNode->runAction(CCSequence::create(
                CCSpawn::create(
                    CCEaseIn::create(CCMoveTo::create(dur, {targetPos.x + clipSize.width, targetPos.y}), 2.0f),
                    oldSprite ? static_cast<CCFiniteTimeAction*>(CCFadeTo::create(dur * 0.8f, 0)) : static_cast<CCFiniteTimeAction*>(CCDelayTime::create(dur)),
                    nullptr),
                CCCallFunc::create(oldNode, callfunc_selector(CCNode::removeFromParent)), nullptr));
        } break;

        case PaimonGalleryTransition::SlideUp: {
            newNode->setPosition({targetPos.x, targetPos.y - clipSize.height});
            newNode->runAction(CCEaseOut::create(CCMoveTo::create(dur, targetPos), 2.5f));
            if (oldNode) oldNode->runAction(CCSequence::create(
                CCSpawn::create(
                    CCEaseIn::create(CCMoveTo::create(dur, {targetPos.x, targetPos.y + clipSize.height}), 2.0f),
                    oldSprite ? static_cast<CCFiniteTimeAction*>(CCFadeTo::create(dur * 0.8f, 0)) : static_cast<CCFiniteTimeAction*>(CCDelayTime::create(dur)),
                    nullptr),
                CCCallFunc::create(oldNode, callfunc_selector(CCNode::removeFromParent)), nullptr));
        } break;

        case PaimonGalleryTransition::SlideDown: {
            newNode->setPosition({targetPos.x, targetPos.y + clipSize.height});
            newNode->runAction(CCEaseOut::create(CCMoveTo::create(dur, targetPos), 2.5f));
            if (oldNode) oldNode->runAction(CCSequence::create(
                CCSpawn::create(
                    CCEaseIn::create(CCMoveTo::create(dur, {targetPos.x, targetPos.y - clipSize.height}), 2.0f),
                    oldSprite ? static_cast<CCFiniteTimeAction*>(CCFadeTo::create(dur * 0.8f, 0)) : static_cast<CCFiniteTimeAction*>(CCDelayTime::create(dur)),
                    nullptr),
                CCCallFunc::create(oldNode, callfunc_selector(CCNode::removeFromParent)), nullptr));
        } break;

        case PaimonGalleryTransition::ZoomIn: {
            newNode->setScaleX(sx * 1.12f);
            newNode->setScaleY(sy * 1.12f);
            newSprite->setOpacity(0);
            newNode->runAction(CCEaseOut::create(CCScaleTo::create(dur, sx, sy), 2.5f));
            newSprite->runAction(CCFadeTo::create(dur * 0.7f, 255));
            if (oldNode) oldNode->runAction(CCSequence::create(
                CCDelayTime::create(removeDelay),
                CCCallFunc::create(oldNode, callfunc_selector(CCNode::removeFromParent)), nullptr));
        } break;

        case PaimonGalleryTransition::ZoomOut: {
            newNode->setScaleX(sx * 1.6f);
            newNode->setScaleY(sy * 1.6f);
            newSprite->setOpacity(0);
            newNode->runAction(CCEaseOut::create(CCScaleTo::create(dur, sx, sy), 2.5f));
            newSprite->runAction(CCFadeTo::create(dur * 0.7f, 255));
            if (oldNode) oldNode->runAction(CCSequence::create(
                CCDelayTime::create(removeDelay),
                CCCallFunc::create(oldNode, callfunc_selector(CCNode::removeFromParent)), nullptr));
        } break;

        case PaimonGalleryTransition::FlipHorizontal: {
            newNode->setScaleX(0.01f);
            newSprite->setOpacity(0);
            newNode->runAction(CCSequence::create(
                CCDelayTime::create(halfDur),
                CCEaseOut::create(CCScaleTo::create(halfDur, sx, sy), 2.0f), nullptr));
            newSprite->runAction(CCSequence::create(
                CCDelayTime::create(halfDur),
                CCFadeTo::create(halfDur * 0.5f, 255), nullptr));
            if (oldNode) oldNode->runAction(CCSequence::create(
                CCSpawn::create(
                    CCEaseIn::create(CCScaleTo::create(halfDur, 0.01f, sy), 2.0f),
                    oldSprite ? static_cast<CCFiniteTimeAction*>(CCFadeTo::create(halfDur, 0)) : static_cast<CCFiniteTimeAction*>(CCDelayTime::create(halfDur)),
                    nullptr),
                CCCallFunc::create(oldNode, callfunc_selector(CCNode::removeFromParent)), nullptr));
        } break;

        case PaimonGalleryTransition::FlipVertical: {
            newNode->setScaleY(0.01f);
            newSprite->setOpacity(0);
            newNode->runAction(CCSequence::create(
                CCDelayTime::create(halfDur),
                CCEaseOut::create(CCScaleTo::create(halfDur, sx, sy), 2.0f), nullptr));
            newSprite->runAction(CCSequence::create(
                CCDelayTime::create(halfDur),
                CCFadeTo::create(halfDur * 0.5f, 255), nullptr));
            if (oldNode) oldNode->runAction(CCSequence::create(
                CCSpawn::create(
                    CCEaseIn::create(CCScaleTo::create(halfDur, sx, 0.01f), 2.0f),
                    oldSprite ? static_cast<CCFiniteTimeAction*>(CCFadeTo::create(halfDur, 0)) : static_cast<CCFiniteTimeAction*>(CCDelayTime::create(halfDur)),
                    nullptr),
                CCCallFunc::create(oldNode, callfunc_selector(CCNode::removeFromParent)), nullptr));
        } break;

        case PaimonGalleryTransition::RotateCW: {
            newNode->setRotation(90.0f);
            newSprite->setOpacity(0);
            newNode->runAction(CCEaseOut::create(CCRotateTo::create(dur, 0.0f), 2.5f));
            newSprite->runAction(CCFadeTo::create(dur * 0.6f, 255));
            if (oldNode) oldNode->runAction(CCSequence::create(
                CCSpawn::create(
                    CCEaseIn::create(CCRotateTo::create(dur, -90.0f), 2.0f),
                    oldSprite ? static_cast<CCFiniteTimeAction*>(CCFadeTo::create(dur * 0.8f, 0)) : static_cast<CCFiniteTimeAction*>(CCDelayTime::create(dur)),
                    nullptr),
                CCCallFunc::create(oldNode, callfunc_selector(CCNode::removeFromParent)), nullptr));
        } break;

        case PaimonGalleryTransition::RotateCCW: {
            newNode->setRotation(-90.0f);
            newSprite->setOpacity(0);
            newNode->runAction(CCEaseOut::create(CCRotateTo::create(dur, 0.0f), 2.5f));
            newSprite->runAction(CCFadeTo::create(dur * 0.6f, 255));
            if (oldNode) oldNode->runAction(CCSequence::create(
                CCSpawn::create(
                    CCEaseIn::create(CCRotateTo::create(dur, 90.0f), 2.0f),
                    oldSprite ? static_cast<CCFiniteTimeAction*>(CCFadeTo::create(dur * 0.8f, 0)) : static_cast<CCFiniteTimeAction*>(CCDelayTime::create(dur)),
                    nullptr),
                CCCallFunc::create(oldNode, callfunc_selector(CCNode::removeFromParent)), nullptr));
        } break;

        case PaimonGalleryTransition::Cube: {
            // 3D cube illusion: slide + scaleX perspective
            float offset = clipSize.width * 0.5f;
            newNode->setPosition({targetPos.x + offset, targetPos.y});
            newNode->setScaleX(0.01f);
            newNode->runAction(CCSpawn::create(
                CCEaseOut::create(CCMoveTo::create(dur, targetPos), 2.0f),
                CCEaseOut::create(CCScaleTo::create(dur, sx, sy), 2.0f), nullptr));
            if (oldNode) oldNode->runAction(CCSequence::create(
                CCSpawn::create(
                    CCEaseIn::create(CCMoveTo::create(dur, {targetPos.x - offset, targetPos.y}), 2.0f),
                    CCEaseIn::create(CCScaleTo::create(dur, 0.01f, sy), 2.0f), nullptr),
                CCCallFunc::create(oldNode, callfunc_selector(CCNode::removeFromParent)), nullptr));
        } break;

        case PaimonGalleryTransition::Dissolve: {
            // stepped fade: opacity ramps in discrete steps for a dissolve look
            newSprite->setOpacity(0);
            float step = dur / 5.0f;
            newSprite->runAction(CCSequence::create(
                CCFadeTo::create(step, 50),
                CCFadeTo::create(step, 120),
                CCFadeTo::create(step, 180),
                CCFadeTo::create(step, 220),
                CCFadeTo::create(step, 255), nullptr));
            if (oldNode) oldNode->runAction(CCSequence::create(
                CCDelayTime::create(removeDelay),
                CCCallFunc::create(oldNode, callfunc_selector(CCNode::removeFromParent)), nullptr));
        } break;

        case PaimonGalleryTransition::Swipe: {
            // new covers old from right — old stays still
            newNode->setPosition({targetPos.x + clipSize.width, targetPos.y});
            newNode->runAction(CCEaseOut::create(CCMoveTo::create(dur, targetPos), 3.0f));
            if (oldNode) oldNode->runAction(CCSequence::create(
                CCDelayTime::create(removeDelay),
                CCCallFunc::create(oldNode, callfunc_selector(CCNode::removeFromParent)), nullptr));
        } break;

        case PaimonGalleryTransition::Bounce: {
            newNode->setPosition({targetPos.x + clipSize.width, targetPos.y});
            newNode->runAction(CCEaseBounceOut::create(CCMoveTo::create(dur, targetPos)));
            if (oldNode) oldNode->runAction(CCSequence::create(
                CCSpawn::create(
                    CCEaseIn::create(CCMoveTo::create(dur * 0.5f, {targetPos.x - clipSize.width * 0.3f, targetPos.y}), 2.0f),
                    oldSprite ? static_cast<CCFiniteTimeAction*>(CCFadeTo::create(dur * 0.5f, 0)) : static_cast<CCFiniteTimeAction*>(CCDelayTime::create(dur * 0.5f)),
                    nullptr),
                CCCallFunc::create(oldNode, callfunc_selector(CCNode::removeFromParent)), nullptr));
        } break;

        default: { // fallback = crossfade
            newSprite->setOpacity(0);
            newSprite->runAction(CCFadeTo::create(dur, 255));
            if (oldNode) oldNode->runAction(CCSequence::create(
                CCDelayTime::create(removeDelay),
                CCCallFunc::create(oldNode, callfunc_selector(CCNode::removeFromParent)), nullptr));
        } break;
        }
    }

    // Aplica transicion de entrada (primera aparicion del thumbnail)
    void applyEntryTransition(CCNode* clipNode, CCSprite* sprite, PaimonGalleryTransition type,
                              float dur, CCSize clipSize) {
        log::info("[LevelCell] applyEntryTransition: type={} dur={:.2f} clipSize=({:.1f},{:.1f})", static_cast<int>(type), dur, clipSize.width, clipSize.height);
        applyGalleryTransition(clipNode, sprite, nullptr, nullptr, type, dur, clipSize);
    }

    // Marca fin de transicion de galeria (permite que hover animation retome control)
    void endGalleryTransition(float /*dt*/) {
        auto fields = m_fields.self();
        if (fields) {
            fields->m_isGalleryTransitioning = false;
            // Reset hover lerp so the effect smoothly animates in from zero
            // instead of snapping to full intensity after the transition
            fields->m_centerLerp = 0.0f;
        }
    }

    // Activa el guard de transicion y programa su fin tras la duracion dada
    void beginGalleryTransitionGuard(float dur) {
        auto fields = m_fields.self();
        if (!fields) return;
        fields->m_isGalleryTransitioning = true;
        log::info("[LevelCell] beginGalleryTransitionGuard: dur={:.2f}", dur);
        // cancelar callback anterior si habia una transicion previa aun pendiente
        this->unschedule(schedule_selector(PaimonLevelCell::endGalleryTransition));
        this->scheduleOnce(schedule_selector(PaimonLevelCell::endGalleryTransition), dur + 0.1f);
    }

    void crossfadeToThumb(CCTexture2D* texture) {
        if (!texture) {
            log::warn("[LevelCell] crossfadeToThumb: null texture");
            return;
        }
        auto fields = m_fields.self();
        if (!fields || fields->m_isBeingDestroyed) return;

        // fallback to full rebuild if clipping node or sprite missing
        if (!fields->m_clippingNode || !fields->m_clippingNode->getParent() ||
            !fields->m_thumbSprite || !fields->m_thumbSprite->getParent()) {
            log::info("[LevelCell] crossfadeToThumb: fallback to full rebuild (missing clip/sprite)");
            addOrUpdateThumb(texture);
            return;
        }
        log::info("[LevelCell] crossfadeToThumb: starting, oldBaseScale={:.4f}", fields->m_thumbBaseScaleX);

        auto oldClip = fields->m_clippingNode;
        auto oldSprite = fields->m_thumbSprite;

        CCSprite* newSprite = createThumbnailSprite(texture, false);
        if (!newSprite) {
            addOrUpdateThumb(texture);
            return;
        }

        float oldBaseScale = fields->m_thumbBaseScaleX;
        float newBaseScale = oldBaseScale;
        auto bg = m_backgroundLayer;
        auto newClip = createThumbnailClippingNode(bg, newSprite, newBaseScale);
        if (!newClip) {
            log::warn("[LevelCell] crossfadeToThumb: createThumbnailClippingNode failed, fallback");
            addOrUpdateThumb(texture);
            return;
        }
        log::debug("[LevelCell] crossfadeToThumb: newBaseScale={:.4f} clipSize=({:.1f},{:.1f})", newBaseScale, newClip->getContentSize().width, newClip->getContentSize().height);

        newSprite->setAnchorPoint(oldSprite->getAnchorPoint());
        newSprite->setZOrder(oldSprite->getZOrder());
        newSprite->setColor(oldSprite->getColor());
        newSprite->setPosition(newClip->getContentSize() * 0.5f);
        newClip->setPosition(fields->m_clipBasePos);
        newClip->setScale(1.0f);
        newClip->setRotation(0.0f);

        oldClip->setPosition(fields->m_clipBasePos);
        oldClip->setScale(1.0f);
        oldClip->setRotation(0.0f);
        oldSprite->setPosition(fields->m_thumbBasePos);
        oldSprite->setScale(oldBaseScale);
        oldSprite->setRotation(0.0f);
        oldSprite->setOpacity(255);

        // apply same shader if thumbnail bg type
        std::string bgType = Mod::get()->getSettingValue<std::string>("levelcell-background-type");
        if (bgType == "thumbnail") {
            if (auto pss = typeinfo_cast<PaimonShaderSprite*>(newSprite)) {
                auto shader = getOrCreateShader("paimon_cell_saturation", vertexShaderCell, fragmentShaderSaturationCell);
                if (shader) {
                    newSprite->setShaderProgram(shader);
                    pss->m_intensity = 2.5f;
                    pss->m_brightness = 3.0f;
                }
            }
        }

        // read cached transition settings
        cacheSettings();
        auto transType = fields->m_cachedGalleryTransition;
        float dur = fields->m_cachedTransitionDuration;
        CCSize clipSize = newClip->getContentSize();

        log::info("[LevelCell] crossfadeToThumb: transType={} dur={:.2f}", static_cast<int>(transType), dur);

        // Update fields BEFORE applying the transition so that:
        // 1) m_clipBasePos stores the correct target position (not the offset
        //    position that applyGalleryTransition may set for slide animations)
        // 2) updateCenterAnimation (which is guarded by m_isGalleryTransitioning)
        //    will have the right base values once the transition ends
        fields->m_clippingNode = newClip;
        fields->m_thumbSprite = newSprite;
        fields->m_thumbBasePos = newSprite->getPosition();
        fields->m_clipBasePos = newClip->getPosition();
        fields->m_thumbBaseScaleX = newBaseScale;
        fields->m_thumbBaseScaleY = newBaseScale;

        this->addChild(newClip);
        beginGalleryTransitionGuard(dur);
        applyGalleryTransition(newClip, newSprite, oldClip, oldSprite, transType, dur, clipSize);

        // crossfade gradient background if bgType is thumbnail
        if (bgType == "thumbnail" && m_level && fields->m_gradientLayer &&
            fields->m_gradientLayer->getParent()) {
            auto bg = m_backgroundLayer;
            if (bg) {
                float blurIntensity = static_cast<float>(Mod::get()->getSettingValue<double>("levelcell-background-blur"));
                CCSize targetSize = bg->getContentSize();
                targetSize.width = std::max(targetSize.width, 512.f);
                targetSize.height = std::max(targetSize.height, 256.f);

                auto newBgSprite = Shaders::createBlurredSprite(texture, targetSize, blurIntensity);
                if (newBgSprite) {
                    auto clipper = fields->m_gradientLayer->getParent();
                    float scale = safeCoverScale(
                        bg->getContentWidth(), bg->getContentHeight(),
                        newBgSprite->getContentSize().width, newBgSprite->getContentSize().height,
                        1.0f
                    );
                    newBgSprite->setScale(scale);
                    newBgSprite->setPosition(bg->getContentSize() / 2);
                    newBgSprite->setOpacity(0);
                    clipper->addChild(newBgSprite);
                    newBgSprite->runAction(CCFadeTo::create(dur, 255));

                    // viejo se queda a opacidad completa debajo; se elimina al terminar
                    auto oldGrad = fields->m_gradientLayer;
                    oldGrad->runAction(CCSequence::create(
                        CCDelayTime::create(dur + 0.05f),
                        CCCallFunc::create(oldGrad, callfunc_selector(CCNode::removeFromParent)),
                        nullptr
                    ));
                    fields->m_gradientLayer = newBgSprite;
                }
            }
        }
    }

    void requestGalleryThumbnail(int index) {
        auto fields = m_fields.self();
        if (!fields || fields->m_isBeingDestroyed || !m_level) return;
        if (index < 0 || index >= static_cast<int>(fields->m_galleryThumbnails.size())) return;

        auto const& thumb = fields->m_galleryThumbnails[index];
        if (thumb.url.empty()) return;

        if (auto it = fields->m_galleryTextureCache.find(thumb.url); it != fields->m_galleryTextureCache.end() && it->second.data()) {
            if (fields->m_galleryIndex == index) {
                log::info("[LevelCell] requestGalleryThumbnail: cache hit index={} url={}", index, thumb.url);
                this->crossfadeToThumb(it->second.data());
            }
            return;
        }

        if (!fields->m_galleryPendingUrls.insert(thumb.url).second) {
            log::debug("[LevelCell] requestGalleryThumbnail: already pending index={}", index);
            return;
        }
        log::info("[LevelCell] requestGalleryThumbnail: downloading index={} url={}", index, thumb.url);

        const int levelID = m_level->m_levelID.value();
        const int galleryToken = fields->m_galleryToken;
        WeakRef<PaimonLevelCell> safeRef = this;
        ThumbnailAPI::get().downloadFromUrl(thumb.url, [safeRef, levelID, galleryToken, index, url = thumb.url](bool success, CCTexture2D* tex) {
            auto cellRef = safeRef.lock();
            auto* cell = static_cast<PaimonLevelCell*>(cellRef.data());
            if (!cell || !cell->getParent() || !cell->m_level || cell->m_level->m_levelID != levelID) return;

            auto fields = cell->m_fields.self();
            if (!fields) return;
            fields->m_galleryPendingUrls.erase(url);
            if (fields->m_galleryToken != galleryToken) return;
            if (!success || !tex) {
                log::warn("[LevelCell] requestGalleryThumbnail callback: download failed index={} url={}", index, url);
                return;
            }
            log::info("[LevelCell] requestGalleryThumbnail callback: downloaded index={} success={}", index, success);

            fields->m_galleryTextureCache[url] = tex;
            if (fields->m_galleryIndex == index) {
                cell->crossfadeToThumb(tex);
            }
        });
    }

    void updateGalleryCycle(float dt) {
        auto fields = m_fields.self();
        if (!fields || fields->m_isBeingDestroyed || !m_level) return;
        if (fields->m_galleryThumbnails.size() < 2) {
            this->unschedule(schedule_selector(PaimonLevelCell::updateGalleryCycle));
            return;
        }
        fields->m_galleryIndex = (fields->m_galleryIndex + 1) % static_cast<int>(fields->m_galleryThumbnails.size());
        this->requestGalleryThumbnail(fields->m_galleryIndex);

        if (fields->m_galleryThumbnails.size() > 2) {
            int prefetchIndex = (fields->m_galleryIndex + 1) % static_cast<int>(fields->m_galleryThumbnails.size());
            if (prefetchIndex != fields->m_galleryIndex) {
                this->requestGalleryThumbnail(prefetchIndex);
            }
        }
    }

    void addOrUpdateThumb(CCTexture2D* texture) {
        if (!texture) {
            log::warn("[LevelCell] addOrUpdateThumb called with null texture");
            return;
        }
        
        // Re-added parent check because adding children to 'this' when 'this' is detached might be unsafe
        auto fields = m_fields.self();
            if (!fields || fields->m_isBeingDestroyed) {
                log::warn("[LevelCell] Fields null or destroyed in addOrUpdateThumb");
                return;
            }

            auto bg = m_backgroundLayer;
            if (!bg) {
                log::warn("[LevelCell] Background layer null in addOrUpdateThumb");
                return;
            }
            
            // If bg has no parent, it might be detached. We can still try to add children to it.
            // But cleanPaimonNodes checks parent? No, it checks fields.
            
            cleanPaimonNodes(bg);

            bg->setZOrder(-2);

            CCSprite* sprite = createThumbnailSprite(texture);
            if (!sprite) {
                log::warn("[LevelCell] Failed to create sprite from texture");
                return;
            }

            setupClippingAndSeparator(bg, sprite);
            log::info("[LevelCell] addOrUpdateThumb: setup complete, baseScaleX={:.4f}", fields->m_thumbBaseScaleX);

            // Entry animation for first thumbnail appearance
            if (fields->m_clippingNode && fields->m_thumbSprite) {
                cacheSettings();
                auto transType = fields->m_cachedGalleryTransition;
                float dur = fields->m_cachedTransitionDuration;
                CCSize clipSize = fields->m_clippingNode->getContentSize();
                beginGalleryTransitionGuard(dur);
                applyEntryTransition(fields->m_clippingNode, fields->m_thumbSprite, transType, dur, clipSize);
            }

            if (m_level) {
                int32_t levelID = m_level->m_levelID.value();
                setupGradient(bg, levelID, texture);
                setupMythicParticles(bg, levelID);
            }

            findAndSetupViewButton();

            // schedule update() so gallery cycling, invalidation checks and GIF hover run
            this->scheduleUpdate();

            // Update gradient colors
            if (m_level && fields && !fields->m_isBeingDestroyed && fields->m_gradientLayer) {
                if (!fields->m_gradientLayer->getParent()) {
                    fields->m_gradientLayer = nullptr;
                } else {
                    int32_t levelID = m_level->m_levelID.value();
                    if (auto pair = LevelColors::get().getPair(levelID)) {
                        fields->m_gradientColorA = pair->a;
                        fields->m_gradientColorB = pair->b;
                        if (auto grad = typeinfo_cast<PaimonShaderGradient*>(static_cast<CCSprite*>(fields->m_gradientLayer))) {
                            grad->setStartColor(pair->a);
                            grad->setEndColor(pair->b);
                        }
                    }
                }
            }
    }

    bool checkMenuCollision(CCNode* node, CCPoint worldPoint, CCNode* ignoreNode) {
        if (!node || !node->isVisible()) return false;
        
        // If it's a CCMenu, check its items
        if (auto menu = typeinfo_cast<CCMenu*>(node)) {
            if (!menu->isEnabled()) return false;
            
            auto children = menu->getChildren();
            if (!children) return false;
            
            for (auto* child : CCArrayExt<CCNode*>(children)) {
                if (!child || !child->isVisible()) continue;

                // Skip the ignored node (m_viewOverlay)
                if (child == ignoreNode) continue;
                
                auto item = typeinfo_cast<CCMenuItem*>(child);
                if (item && item->isEnabled()) {
                    // Check collision
                    CCPoint local = item->getParent()->convertToNodeSpace(worldPoint);
                    CCRect r = item->boundingBox();
                    if (r.containsPoint(local)) {
                        return true;
                    }
                }
            }
        }
        
        // Recurse
        auto children = node->getChildren();
        if (children) {
            for (auto* child : CCArrayExt<CCNode*>(children)) {
                if (child && checkMenuCollision(child, worldPoint, ignoreNode)) return true;
            }
        }
        return false;
    }

    bool isTouchOnMenu(CCTouch* touch) {
        auto fields = m_fields.self();
        CCPoint worldPoint = touch->getLocation();
        // Pass nullptr as ignoreNode so m_viewOverlay is INCLUDED in collision check
        // This ensures ccTouchBegan returns false if m_viewOverlay is hit, letting CCMenu handle it
        return checkMenuCollision(this, worldPoint, nullptr);
    }

    $override void onExit() {
        log::info("[LevelCell] onExit: levelID={}", m_level ? m_level->m_levelID.value() : 0);
        // parar animaciones (movido desde destructor — Geode desaconseja
        // logica pesada en destructores de $modify)
        this->unscheduleUpdate();
        this->unschedule(schedule_selector(PaimonLevelCell::updateGalleryCycle));
        this->unschedule(schedule_selector(PaimonLevelCell::updateGradientAnim));
        this->unschedule(schedule_selector(PaimonLevelCell::checkCenterPosition));
        this->unschedule(schedule_selector(PaimonLevelCell::updateCenterAnimation));

        if (auto fields = m_fields.self()) {
            fields->m_isBeingDestroyed = true;
            fields->m_requestId++;
            fields->m_thumbnailRequested = false;
            fields->m_staticThumbLoad.reset();
            fields->m_galleryThumbnails.clear();
            fields->m_galleryTextureCache.clear();
            fields->m_galleryPendingUrls.clear();
            fields->m_galleryRequested = false;
            fields->m_galleryToken++;
            if (fields->m_invalidationListenerId != 0) {
                ThumbnailLoader::get().removeInvalidationListener(fields->m_invalidationListenerId);
                fields->m_invalidationListenerId = 0;
            }
        }

        if (m_level) {
            ThumbnailLoader::get().cancelLoad(m_level->m_levelID.value());
            ThumbnailLoader::get().cancelLoad(m_level->m_levelID.value(), true);
        }
        LevelCell::onExit();
    }

    void playTapFlashAnimation() {
        auto fields = m_fields.self();
        if (!fields) return;

        CCSize cellSize = this->getContentSize();
        if (cellSize.width < 4.f || cellSize.height < 4.f) {
            if (auto p = this->getParent()) cellSize = p->getContentSize();
        }
        // Prefer m_mainMenu/m_button parent if available for higher stacking
        CCNode* flashParent = this;
        if (m_mainMenu) flashParent = m_mainMenu;
        else if (m_button && m_button->getParent()) flashParent = m_button->getParent();

        auto flash = CCLayerColor::create(ccc4(255,255,255,0));
        if (!flash) return;
        flash->setContentSize(cellSize);
        flash->ignoreAnchorPointForPosition(false);
        flash->setAnchorPoint({0.5f,0.5f});
        
        // Fix: Calculate position in flashParent's coordinate space to ensure centering
        // Using world space conversion handles cases where flashParent (e.g. m_mainMenu) 
        // has a different position/anchor than the cell itself.
        CCPoint centerLocal = CCPoint(cellSize.width / 2.0f, cellSize.height / 2.0f);
        CCPoint centerWorld = this->convertToWorldSpace(centerLocal);
        CCPoint centerInParent = flashParent->convertToNodeSpace(centerWorld);
        flash->setPosition(centerInParent);

        flash->setZOrder(99999);
        flash->setID("paimon-tap-flash"_spr);
        flashParent->addChild(flash);
        flashParent->reorderChild(flash, 99999);

        ccBlendFunc blend { GL_SRC_ALPHA, GL_ONE };
        flash->setBlendFunc(blend);

        auto fadeIn  = CCFadeTo::create(0.05f, 230);
        auto hold    = CCDelayTime::create(0.02f);
        auto fadeOut = CCFadeTo::create(0.30f, 0);
        auto remove  = CCCallFunc::create(flash, callfunc_selector(CCNode::removeFromParent));
        auto scaleUp = CCScaleTo::create(0.07f, 1.05f);
        auto scaleDown = CCScaleTo::create(0.25f, 1.0f);
        auto pulse = CCSequence::create(scaleUp, scaleDown, nullptr);

        auto easeIn = CCEaseOut::create(static_cast<CCActionInterval*>(fadeIn->copy()->autorelease()), 2.6f);
        auto easeOut = CCEaseIn::create(static_cast<CCActionInterval*>(fadeOut->copy()->autorelease()), 1.4f);
        auto flashSeq = CCSequence::create(easeIn, hold, easeOut, remove, nullptr);
        flash->runAction(flashSeq);
        flash->runAction(pulse);

        if (m_backgroundLayer) {
            auto originalColor = m_backgroundLayer->getColor();
            m_backgroundLayer->setColor({255,255,255});
            auto delayBG = CCDelayTime::create(0.03f);
            auto tintBack = CCTintTo::create(0.22f, originalColor.r, originalColor.g, originalColor.b);
            m_backgroundLayer->runAction(CCSequence::create(delayBG, tintBack, nullptr));
        }

        if (fields->m_thumbSprite) {
            auto ts = fields->m_thumbSprite;
            ts->setOpacity(255);
            auto thumbPulseUp = CCScaleTo::create(0.07f, ts->getScale() * 1.02f);
            auto thumbPulseDown = CCScaleTo::create(0.22f, ts->getScale());
            ts->runAction(CCSequence::create(thumbPulseUp, thumbPulseDown, nullptr));
        }
    }

    // Fallback: flash on button click (original click behavior preserved)
    $override void onClick(CCObject* sender) {
        playTapFlashAnimation();
        LevelCell::onClick(sender);
    }

    // Helper: brighten color by amount [0..255]
    static inline ccColor3B brightenColor(ccColor3B const& c, int add) {
        auto clamp = [](int v){ return std::max(0, std::min(255, v)); };
        return ccColor3B{
            (GLubyte)clamp(c.r + add),
            (GLubyte)clamp(c.g + add),
            (GLubyte)clamp(c.b + add)
        };
    }

    void updateGradientAnim(float dt) {
        {
            auto fields = m_fields.self();
            if (!fields || fields->m_isBeingDestroyed || !fields->m_gradientLayer) return;
            
            auto grad = typeinfo_cast<PaimonShaderGradient*>(static_cast<CCSprite*>(fields->m_gradientLayer));
            if (!grad) return;

            // Dynamic GIF gradient support
            if (fields->m_thumbSprite) {
                // GifSprite does not support getCurrentFrameColors yet
            }

            fields->m_gradientTime += dt;
            float t = (sinf(fields->m_gradientTime * 1.2f) + 1.0f) / 2.0f;
            
            // Base gradient colors (animated wave)
            ccColor3B left = {
                (GLubyte)((1-t)*fields->m_gradientColorA.r + t*fields->m_gradientColorB.r),
                (GLubyte)((1-t)*fields->m_gradientColorA.g + t*fields->m_gradientColorB.g),
                (GLubyte)((1-t)*fields->m_gradientColorA.b + t*fields->m_gradientColorB.b)
            };
            ccColor3B right = {
                (GLubyte)((1-t)*fields->m_gradientColorB.r + t*fields->m_gradientColorA.r),
                (GLubyte)((1-t)*fields->m_gradientColorB.g + t*fields->m_gradientColorA.g),
                (GLubyte)((1-t)*fields->m_gradientColorB.b + t*fields->m_gradientColorA.b)
            };
            
            // Apply brightness based on centerLerp
            auto clamp = [](int v) { return std::max(0, std::min(255, v)); };
            int brightAmount = static_cast<int>(60.0f * fields->m_centerLerp);
            
            left.r = (GLubyte)clamp(left.r + brightAmount);
            left.g = (GLubyte)clamp(left.g + brightAmount);
            left.b = (GLubyte)clamp(left.b + brightAmount);
            
            right.r = (GLubyte)clamp(right.r + brightAmount);
            right.g = (GLubyte)clamp(right.g + brightAmount);
            right.b = (GLubyte)clamp(right.b + brightAmount);
            
            grad->setStartColor(left);
            grad->setEndColor(right);
        }
    }

    void cacheSettings() {
        auto fields = m_fields.self();
        if (fields->m_settingsCached) return;
        fields->m_settingsCached = true;
        fields->m_cachedAnimType = parseAnimType(Mod::get()->getSettingValue<std::string>("levelcell-anim-type"));
        fields->m_cachedAnimSpeed = static_cast<float>(Mod::get()->getSettingValue<double>("levelcell-anim-speed"));
        fields->m_cachedAnimEffect = parseAnimEffect(Mod::get()->getSettingValue<std::string>("levelcell-anim-effect"));
        fields->m_cachedHoverEnabled = Mod::get()->getSettingValue<bool>("levelcell-hover-effects");
        fields->m_cachedCompactMode = Mod::get()->getSettingValue<bool>("compact-list-mode");
        fields->m_cachedEffectOnGradient = Mod::get()->getSettingValue<bool>("levelcell-effect-on-gradient");
        fields->m_cachedBgType = parseBgType(Mod::get()->getSettingValue<std::string>("levelcell-background-type"));
        fields->m_cachedGalleryTransition = parseGalleryTransition(Mod::get()->getSettingValue<std::string>("levelcell-gallery-transition"));
        fields->m_cachedTransitionDuration = std::clamp(static_cast<float>(Mod::get()->getSettingValue<double>("levelcell-gallery-transition-duration")), 0.2f, 2.0f);
    }

    void checkCenterPosition(float dt) {
        auto fields = m_fields.self();
        if (!fields || fields->m_isBeingDestroyed) return;

        cacheSettings();

        if (!fields->m_cachedHoverEnabled) {
            if (fields->m_centerLerp > 0.0f) fields->m_centerLerp = 0.0f;
            return;
        }

        if (!this->getParent()) return;

        auto winSize = CCDirector::sharedDirector()->getWinSize();
        CCPoint worldPos = this->convertToWorldSpace(CCPointZero);

        float cellCenterY = worldPos.y + this->getContentSize().height / 2.0f;
        float screenCenterY = winSize.height / 2.0f;
        float centerZone = fields->m_cachedCompactMode ? 24.75f : 45.0f;

        float distanceFromCenter = std::abs(cellCenterY - screenCenterY);
        bool isVisible = cellCenterY > 0 && cellCenterY < winSize.height;

        if (!isVisible) {
            fields->m_wasInCenter = false;
            return;
        }

        fields->m_wasInCenter = distanceFromCenter < centerZone;
    }


    void updateCenterAnimation(float dt) {
        auto fields = m_fields.self();
        if (!fields || fields->m_isBeingDestroyed) return;

        fields->m_animTime += dt;
        cacheSettings();

        PaimonAnimType animType = fields->m_cachedAnimType;
        float speedMult = fields->m_cachedAnimSpeed;
        PaimonAnimEffect animEffect = fields->m_cachedAnimEffect;

        if (animType == PaimonAnimType::None) {
            fields->m_centerLerp = 0.0f;
        } else {
            // During gallery transitions the hover transforms are not applied,
            // so drive lerp toward 0 to avoid a snap when the guard lifts.
            float target = (fields->m_wasInCenter && !fields->m_isGalleryTransitioning) ? 1.0f : 0.0f;
            float speed = 6.0f * speedMult;
            fields->m_centerLerp += (target - fields->m_centerLerp) * std::min(1.0f, dt * speed);
        }

        float lerp = fields->m_centerLerp;
        float offsetX = 0.0f, zoomFactor = 1.0f, rotation = 0.0f;
        float spriteRotation = 0.0f, spriteOffsetX = 0.0f;
        float animT = fields->m_animTime;

        switch (animType) {
        case PaimonAnimType::ZoomSlide:
            offsetX = -10.f * lerp;
            zoomFactor = 1.0f + (0.12f * lerp);
            break;
        case PaimonAnimType::Zoom:
            zoomFactor = 1.0f + (0.15f * lerp);
            break;
        case PaimonAnimType::Slide:
            zoomFactor = 1.0f + (0.1f * lerp);
            spriteOffsetX = -15.f * lerp;
            break;
        case PaimonAnimType::Bounce:
            zoomFactor = 1.0f + (0.20f * lerp);
            break;
        case PaimonAnimType::Rotate:
            zoomFactor = 1.0f + (0.05f * lerp);
            rotation = sinf(animT * 3.0f) * 1.5f * lerp;
            break;
        case PaimonAnimType::RotateContent:
            zoomFactor = 1.0f + (0.15f * lerp);
            spriteRotation = sinf(animT * 4.0f) * 3.0f * lerp;
            break;
        case PaimonAnimType::Shake:
            zoomFactor = 1.0f + (0.05f * lerp);
            offsetX = sinf(animT * 20.0f) * 3.0f * lerp;
            break;
        case PaimonAnimType::Pulse: {
            float pulse = (sinf(animT * 10.0f) + 1.0f) * 0.5f;
            zoomFactor = 1.0f + (0.05f * lerp) + (pulse * 0.05f * lerp);
            break;
        }
        case PaimonAnimType::Swing:
            zoomFactor = 1.0f + (0.05f * lerp);
            rotation = sinf(animT * 4.0f) * 3.0f * lerp;
            break;
        default: break;
        }

        // Compact mode reduction
        if (fields->m_cachedCompactMode) {
            zoomFactor = 1.0f + ((zoomFactor - 1.0f) * 0.55f);
            offsetX *= 0.55f;
            spriteOffsetX *= 0.55f;
            rotation *= 0.55f;
            spriteRotation *= 0.55f;
        }

        // Apply transforms to clipping node (skip during gallery transition to avoid
        // fighting with the transition animation actions on the same node)
        if (fields->m_clippingNode && !fields->m_isGalleryTransitioning) {
            float posAdjustment = 0.0f;
            if (animType != PaimonAnimType::None && animType != PaimonAnimType::Slide) {
                posAdjustment = (zoomFactor - 1.0f) * fields->m_clippingNode->getContentSize().width;
            }
            fields->m_clippingNode->setPosition({fields->m_clipBasePos.x + offsetX + posAdjustment, fields->m_clipBasePos.y});
            fields->m_clippingNode->setScaleX(zoomFactor);
            fields->m_clippingNode->setRotation(rotation);
        }

        if (fields->m_separator && !fields->m_isGalleryTransitioning) {
            fields->m_separator->setPosition({fields->m_separatorBasePos.x + offsetX, fields->m_separatorBasePos.y});
            fields->m_separator->setRotation(rotation);
        }

        if (fields->m_thumbSprite && !fields->m_isGalleryTransitioning) {
            fields->m_thumbSprite->setScale(fields->m_thumbBaseScaleX * zoomFactor);
            fields->m_thumbSprite->setRotation(spriteRotation);
            fields->m_thumbSprite->setPosition(fields->m_thumbBasePos + CCPoint(spriteOffsetX, 0.0f));
            fields->m_thumbSprite->setOpacity(255);
        }

        // Build targets on stack (no heap allocation)
        PaimonBgType bgType = fields->m_cachedBgType;
        CCSprite* targets[2];
        int targetCount = 0;
        if (fields->m_thumbSprite) targets[targetCount++] = fields->m_thumbSprite;
        if (fields->m_cachedEffectOnGradient && fields->m_gradientLayer && bgType != PaimonBgType::Thumbnail) {
            targets[targetCount++] = fields->m_gradientLayer;
        }

        for (int ti = 0; ti < targetCount; ++ti) {
            CCSprite* target = targets[ti];
            bool usingShader = false;
            PaimonShaderSprite* pss = typeinfo_cast<PaimonShaderSprite*>(target);
            PaimonShaderGradient* psg = typeinfo_cast<PaimonShaderGradient*>(target);

            auto setIntensity = [&](float i) {
                if (pss) pss->m_intensity = i;
                if (psg) psg->m_intensity = i;
            };
            auto setTime = [&](float t) {
                if (pss) pss->m_time = t;
                if (psg) psg->m_time = t;
            };
            auto setTexSize = [&]() {
                if (pss) {
                    if (auto* targetTex = target->getTexture()) {
                        pss->m_texSize = targetTex->getContentSizeInPixels();
                    } else {
                        pss->m_texSize = target->getContentSize();
                    }
                }
                if (psg) psg->m_texSize = target->getContentSize();
            };

            switch (animEffect) {
            case PaimonAnimEffect::Brightness: {
                float b = 180.0f + (75.0f * lerp);
                target->setColor({(GLubyte)b, (GLubyte)b, (GLubyte)b});
                break;
            }
            case PaimonAnimEffect::Darken: {
                float b = 255.0f - (100.0f * lerp);
                target->setColor({(GLubyte)b, (GLubyte)b, (GLubyte)b});
                break;
            }
            case PaimonAnimEffect::Sepia:
                usingShader = true;
                if (auto sh = getOrCreateShader("paimon_cell_sepia", vertexShaderCell, fragmentShaderSaturationCell)) {
                    target->setShaderProgram(sh); setIntensity(lerp);
                }
                target->setColor({255, 255, 255});
                break;
            case PaimonAnimEffect::Sharpen:
                usingShader = true;
                if (auto sh = getOrCreateShader("paimon_cell_sharpen", vertexShaderCell, fragmentShaderSharpenCell)) {
                    target->setShaderProgram(sh); setIntensity(lerp); setTexSize();
                }
                target->setColor({255, 255, 255});
                break;
            case PaimonAnimEffect::EdgeDetection:
                usingShader = true;
                if (auto sh = getOrCreateShader("paimon_cell_edge", vertexShaderCell, fragmentShaderEdgeCell)) {
                    target->setShaderProgram(sh); setIntensity(lerp); setTexSize();
                }
                target->setColor({255, 255, 255});
                break;
            case PaimonAnimEffect::Vignette:
                usingShader = true;
                if (auto sh = getOrCreateShader("paimon_cell_vignette", vertexShaderCell, fragmentShaderVignetteCell)) {
                    target->setShaderProgram(sh); setIntensity(lerp);
                }
                target->setColor({255, 255, 255});
                break;
            case PaimonAnimEffect::Pixelate:
                usingShader = true;
                if (auto sh = getOrCreateShader("paimon_cell_pixelate", vertexShaderCell, fragmentShaderPixelateCell)) {
                    target->setShaderProgram(sh); setIntensity(lerp); setTexSize();
                }
                target->setColor({255, 255, 255});
                break;
            case PaimonAnimEffect::Posterize:
                usingShader = true;
                if (auto sh = getOrCreateShader("paimon_cell_posterize", vertexShaderCell, fragmentShaderPosterizeCell)) {
                    target->setShaderProgram(sh); setIntensity(lerp);
                }
                target->setColor({255, 255, 255});
                break;
            case PaimonAnimEffect::Chromatic:
                usingShader = true;
                if (auto sh = getOrCreateShader("paimon_cell_chromatic", vertexShaderCell, fragmentShaderChromaticCell)) {
                    target->setShaderProgram(sh); setIntensity(lerp);
                }
                target->setColor({255, 255, 255});
                break;
            case PaimonAnimEffect::Scanlines:
                usingShader = true;
                if (auto sh = getOrCreateShader("paimon_cell_scanlines", vertexShaderCell, fragmentShaderScanlinesCell)) {
                    target->setShaderProgram(sh); setIntensity(lerp); setTexSize();
                }
                target->setColor({255, 255, 255});
                break;
            case PaimonAnimEffect::Solarize:
                usingShader = true;
                if (auto sh = getOrCreateShader("paimon_cell_solarize", vertexShaderCell, fragmentShaderSolarizeCell)) {
                    target->setShaderProgram(sh); setIntensity(lerp);
                }
                target->setColor({255, 255, 255});
                break;
            case PaimonAnimEffect::Rainbow:
                usingShader = true;
                if (auto sh = getOrCreateShader("paimon_cell_rainbow", vertexShaderCell, fragmentShaderRainbowCell)) {
                    target->setShaderProgram(sh); setIntensity(lerp); setTime(animT);
                }
                target->setColor({255, 255, 255});
                break;
            case PaimonAnimEffect::Red: {
                GLubyte v = (GLubyte)(255.0f - (100.0f * lerp));
                target->setColor({255, v, v});
                break;
            }
            case PaimonAnimEffect::Blue: {
                GLubyte v = (GLubyte)(255.0f - (100.0f * lerp));
                target->setColor({v, v, 255});
                break;
            }
            case PaimonAnimEffect::Gold: {
                GLubyte g = (GLubyte)(255.0f - (40.0f * lerp));
                GLubyte bv = (GLubyte)(255.0f - (255.0f * lerp));
                target->setColor({255, g, bv});
                break;
            }
            case PaimonAnimEffect::Fade:
                target->setColor({255, 255, 255});
                target->setOpacity((GLubyte)(255.0f - (100.0f * lerp)));
                break;
            case PaimonAnimEffect::Grayscale:
                usingShader = true;
                if (auto sh = getOrCreateShader("paimon_cell_grayscale", vertexShaderCell, fragmentShaderGrayscaleCell)) {
                    target->setShaderProgram(sh); setIntensity(lerp);
                }
                target->setColor({255, 255, 255});
                break;
            case PaimonAnimEffect::Invert:
                usingShader = true;
                if (auto sh = getOrCreateShader("paimon_cell_invert", vertexShaderCell, fragmentShaderInvertCell)) {
                    target->setShaderProgram(sh); setIntensity(lerp);
                }
                target->setColor({255, 255, 255});
                break;
            case PaimonAnimEffect::Blur:
                usingShader = true;
                if (auto sh = Shaders::getBlurCellShader()) {
                    target->setShaderProgram(sh); setIntensity(lerp); setTexSize();
                }
                target->setColor({255, 255, 255});
                break;
            case PaimonAnimEffect::Glitch:
                usingShader = true;
                if (auto sh = getOrCreateShader("paimon_cell_glitch", vertexShaderCell, fragmentShaderGlitchCell)) {
                    target->setShaderProgram(sh); setIntensity(lerp); setTime(animT);
                }
                target->setColor({255, 255, 255});
                break;
            default:
                if (!psg) target->setColor({255, 255, 255});
                break;
            }

            if (!usingShader) {
                target->setShaderProgram(CCShaderCache::sharedShaderCache()->programForKey(kCCShader_PositionTextureColor));
            }
        }

        // Update view overlay position
        if (fields->m_viewOverlay) {
            auto cs = this->getContentSize();
            float overlayW = 90.f;
            float overlayH = cs.height;
            CCPoint centerLocal;
            if (fields->m_clippingNode) {
                centerLocal = CCPoint(fields->m_clippingNode->getPosition().x - overlayW / 2.f - 15.f, cs.height / 2.f - 1.f);
            } else {
                centerLocal = CCPoint(cs.width - overlayW / 2.f - 15.f, cs.height / 2.f - 1.f);
            }
            CCPoint centerWorld = this->convertToWorldSpace(centerLocal);
            if (auto p = fields->m_viewOverlay->getParent()) {
                fields->m_viewOverlay->setPosition(p->convertToNodeSpace(centerWorld));
            } else {
                fields->m_viewOverlay->setPosition(centerLocal);
            }

            auto adjustState = [overlayW, overlayH](CCNode* n) {
                if (auto sp = typeinfo_cast<CCSprite*>(n)) {
                    sp->setContentSize({overlayW, overlayH});
                }
            };
            adjustState(fields->m_viewOverlay->getNormalImage());
            adjustState(fields->m_viewOverlay->getSelectedImage());
            adjustState(fields->m_viewOverlay->getDisabledImage());
        }
    }

    // animateToCenter/animateFromCenter removed Ã¢â‚¬â€ deprecated (now uses lerp system)

    // Detect whether this cell is inside a DailyLevelNode/DailyLevelPage
    bool isDailyCell() {
        auto fields = m_fields.self();
        if (fields->m_isDailyCellCached) return fields->m_isDailyCell;

        bool result = false;
        CCNode* parent = this->getParent();
        for (int depth = 0; parent && depth < 10; ++depth, parent = parent->getParent()) {
            if (typeinfo_cast<DailyLevelNode*>(parent)) {
                result = true;
                break;
            }
            std::string_view className(typeid(*parent).name());
            if (className.find("DailyLevelNode") != std::string_view::npos ||
                className.find("DailyLevelPage") != std::string_view::npos) {
                result = true;
                break;
            }
        }
        fields->m_isDailyCell = result;
        fields->m_isDailyCellCached = true;
        return result;
    }
    
    // fixDailyCell removed Ã¢â‚¬â€ was empty (logic moved to DailyLevelNode)

    // Removed onPaimonDailyPlay as per user request to remove animation
    
    // Removed onLevelInfo hook as it's not available in binding
    
    void tryLoadThumbnail() {
            configureThumbnailLoader();

            if (!m_level) {
                log::warn("[LevelCell] tryLoadThumbnail: m_level is null, aborting");
                return;
            }
            
            int dailyID = m_level->m_dailyID.value();
            bool isDaily = dailyID > 0;
            if (isDaily) {
                log::debug("[LevelCell] tryLoadThumbnail: skipping daily cell dailyID={}", dailyID);
                return;
            }
            
            int32_t levelID = m_level->m_levelID.value();
            if (levelID <= 0) {
                log::debug("[LevelCell] tryLoadThumbnail: invalid levelID={}", levelID);
                return;
            }
            log::info("[LevelCell] tryLoadThumbnail: levelID={}", levelID);
            
            auto fields = m_fields.self();
            if (fields->m_invalidationListenerId == 0) {
                WeakRef<PaimonLevelCell> safeRef = this;
                fields->m_invalidationListenerId = ThumbnailLoader::get().addInvalidationListener([safeRef](int invalidLevelID) {
                    auto selfRef = safeRef.lock();
                    auto* self = static_cast<PaimonLevelCell*>(selfRef.data());
                    if (!self || !self->getParent() || !self->m_level) return;
                    if (self->m_level->m_levelID.value() != invalidLevelID) return;
                    auto fields = self->m_fields.self();
                    if (!fields) return;
                    fields->m_thumbnailRequested = false;
                    fields->m_thumbnailApplied = false;
                    fields->m_galleryRequested = false;
                    fields->m_galleryThumbnails.clear();
                    fields->m_galleryTextureCache.clear();
                    fields->m_galleryPendingUrls.clear();
                    fields->m_galleryIndex = -1;
                    fields->m_galleryTimer = 0.f;
                    fields->m_galleryToken++;
                    fields->m_loadedInvalidationVersion = ThumbnailLoader::get().getInvalidationVersion(invalidLevelID);
                    self->tryLoadThumbnail();
                });
            }
            
            // comprobar si el level cambio
            if (fields->m_lastRequestedLevelID != levelID) {
                log::info("[LevelCell] tryLoadThumbnail: level changed {} -> {}, resetting state", fields->m_lastRequestedLevelID, levelID);
                fields->m_thumbnailRequested = false;
                fields->m_thumbnailApplied = false;
                fields->m_lastRequestedLevelID = levelID;
                fields->m_hasGif = false;
                fields->m_staticTexture = nullptr;
                fields->m_staticThumbLoad.reset();
                fields->m_loadedInvalidationVersion = 0;
                fields->m_isDailyCellCached = false;
                fields->m_galleryThumbnails.clear();
                fields->m_galleryTextureCache.clear();
                fields->m_galleryPendingUrls.clear();
                fields->m_galleryIndex = -1;
                fields->m_galleryTimer = 0.f;
                fields->m_galleryRequested = false;
                fields->m_galleryToken++;
            }

            // comprobar si la miniatura fue invalidada (usuario subiÃƒÂ³ una nueva)
            int currentVersion = ThumbnailLoader::get().getInvalidationVersion(levelID);
            if (fields->m_thumbnailApplied && currentVersion != fields->m_loadedInvalidationVersion) {
                log::info("[LevelCell] tryLoadThumbnail: thumbnail invalidated for levelID={}, ver {} -> {}", levelID, fields->m_loadedInvalidationVersion, currentVersion);
                fields->m_thumbnailRequested = false;
                fields->m_thumbnailApplied = false;
                fields->m_hasGif = false;
                fields->m_staticTexture = nullptr;
                fields->m_staticThumbLoad.reset();
            }
            fields->m_loadedInvalidationVersion = currentVersion;

            if (!fields->m_galleryRequested) {
                fields->m_galleryRequested = true;
                int galleryToken = ++fields->m_galleryToken;
                log::info("[LevelCell] tryLoadThumbnail: requesting gallery for levelID={} token={}", levelID, galleryToken);
                WeakRef<PaimonLevelCell> safeGalleryRef = this;
                ThumbnailAPI::get().getThumbnails(levelID, [safeGalleryRef, levelID, galleryToken](bool success, std::vector<ThumbnailAPI::ThumbnailInfo> const& thumbs) {
                    auto cellRef = safeGalleryRef.lock();
                    auto* cell = static_cast<PaimonLevelCell*>(cellRef.data());
                    if (!cell || !cell->getParent() || !cell->m_level || cell->m_level->m_levelID != levelID) return;
                    auto fields = cell->m_fields.self();
                    if (!fields || fields->m_galleryToken != galleryToken) return;
                    fields->m_galleryTextureCache.clear();
                    fields->m_galleryPendingUrls.clear();
                    fields->m_galleryThumbnails = normalizeLevelCellGalleryThumbnails(
                        levelID,
                        success ? thumbs : std::vector<ThumbnailAPI::ThumbnailInfo>{}
                    );
                    log::info("[LevelCell] gallery callback: levelID={} success={} thumbCount={}", levelID, success, fields->m_galleryThumbnails.size());
                    fields->m_galleryIndex = -1;
                    fields->m_galleryTimer = 0.f;
                    bool autoCycleEnabled = Mod::get()->getSettingValue<bool>("levelcell-gallery-autocycle");
                    cell->unschedule(schedule_selector(PaimonLevelCell::updateGalleryCycle));
                    if (autoCycleEnabled && fields->m_galleryThumbnails.size() > 1) {
                        log::info("[LevelCell] gallery: auto-cycle enabled for levelID={} with {} thumbs", levelID, fields->m_galleryThumbnails.size());
                        cell->schedule(schedule_selector(PaimonLevelCell::updateGalleryCycle), 3.0f);
                    }
                    for (int i = 0; i < static_cast<int>(fields->m_galleryThumbnails.size()); ++i) {
                        cell->requestGalleryThumbnail(i);
                    }
                });
            }

            if (fields->m_thumbnailRequested) {
                log::debug("[LevelCell] tryLoadThumbnail: already requested for levelID={}", levelID);
                return;
            }
            
            fields->m_requestId++;
            int currentRequestId = fields->m_requestId;
            fields->m_thumbnailRequested = true;
            fields->m_lastRequestedLevelID = levelID;
            fields->m_hasGif = ThumbnailLoader::get().hasGIFData(levelID);
            
            std::string fileName = fmt::format("{}.png", levelID);
            
            bool enableSpinners = true;
            // try { enableSpinners = Mod::get()->getSettingValue<bool>("enable-loading-spinners"); } catch (...) {}
            
            if (enableSpinners) showLoadingSpinner();
            
            log::info("[LevelCell] tryLoadThumbnail: requesting load levelID={} requestId={} hasGif={}", levelID, currentRequestId, fields->m_hasGif);
            WeakRef<PaimonLevelCell> safeRef = this;
            int capturedVersion = fields->m_loadedInvalidationVersion;

            // 1) intentar GIF primero para evitar quedar pegado a cache viejo PNG/WEBP
            ThumbnailLoader::get().requestLoad(levelID, fileName, [safeRef, levelID, enableSpinners, currentRequestId, fileName, capturedVersion](CCTexture2D* gifTex, bool gifSuccess) {
                auto cellRef = safeRef.lock();
                auto* cell = static_cast<PaimonLevelCell*>(cellRef.data());
                if (!cell || !cell->shouldHandleThumbnailCallback(levelID, currentRequestId)) {
                    return;
                }

                // version cambio mientras cargaba: abortar y dejar que el listener recargue
                {
                    auto f = cell->m_fields.self();
                    if (f && f->m_loadedInvalidationVersion != capturedVersion) return;
                }

                if (gifSuccess && gifTex) {
                    log::info("[LevelCell] tryLoadThumbnail: GIF loaded OK levelID={}", levelID);
                    auto fields = cell->m_fields.self();
                    if (fields) fields->m_hasGif = true;
                    cell->applyStaticThumbnailTexture(levelID, currentRequestId, gifTex, enableSpinners);
                    return;
                }
                log::debug("[LevelCell] tryLoadThumbnail: GIF not found for levelID={}, trying static", levelID);

                // 2) fallback a estatico si no hay GIF remoto/local
                ThumbnailLoader::get().requestLoad(levelID, fileName, [safeRef, levelID, enableSpinners, currentRequestId, capturedVersion](CCTexture2D* tex, bool success) {
                    auto cellRef2 = safeRef.lock();
                    auto* cell2 = static_cast<PaimonLevelCell*>(cellRef2.data());
                    if (!cell2 || !cell2->shouldHandleThumbnailCallback(levelID, currentRequestId)) {
                        return;
                    }

                    // version cambio mientras cargaba: abortar
                    {
                        auto f = cell2->m_fields.self();
                        if (f && f->m_loadedInvalidationVersion != capturedVersion) return;
                    }

                    if (!success || !tex) {
                        log::warn("[LevelCell] tryLoadThumbnail: static load FAILED levelID={}", levelID);
                        cell2->applyStaticThumbnailTexture(levelID, currentRequestId, nullptr, enableSpinners);
                        return;
                    }
                    log::info("[LevelCell] tryLoadThumbnail: static texture loaded OK levelID={}", levelID);

                    auto fields2 = cell2->m_fields.self();
                    if (fields2) {
                        fields2->m_hasGif = ThumbnailLoader::get().hasGIFData(levelID);
                    }

                    if (ThumbnailLoader::get().hasGIFData(levelID)) {
                        cell2->applyStaticThumbnailTexture(levelID, currentRequestId, tex, enableSpinners);
                        return;
                    }

                    cell2->startLazyStaticThumbnailLoad(levelID, currentRequestId, enableSpinners, tex);
                });
            }, 0, true);
    }

    $override void update(float dt) {
        LevelCell::update(dt);
        
        auto fields = m_fields.self();
        if (!fields) return;

        // comprobar si los settings del popup cambiaron (live reload)
        int globalSettingsVer = LevelCellSettingsPopup::s_settingsVersion;
        if (globalSettingsVer != fields->m_loadedSettingsVersion) {
            fields->m_loadedSettingsVersion = globalSettingsVer;
            // invalidar cache de settings pa que se re-lean
            fields->m_settingsCached = false;
            // forzar re-aplicar la miniatura con los nuevos settings
            if (fields->m_thumbnailApplied && m_level) {
                fields->m_thumbnailRequested = false;
                fields->m_thumbnailApplied = false;
                tryLoadThumbnail();
            }
        }

        // comprobar si la miniatura fue invalidada mientras la celda estÃƒÂ¡ visible
        if (fields->m_thumbnailApplied && m_level) {
            int32_t levelID = m_level->m_levelID.value();
            if (levelID > 0) {
                int currentVersion = ThumbnailLoader::get().getInvalidationVersion(levelID);
                if (currentVersion != fields->m_loadedInvalidationVersion) {
                    // miniatura actualizada, recargar
                    tryLoadThumbnail();
                }
            }
        }

        // gallery cycling is handled by updateGalleryCycle scheduled method

        if (fields->m_hasGif && fields->m_thumbSprite) {
            auto* animatedThumb = typeinfo_cast<AnimatedGIFSprite*>(fields->m_thumbSprite.data());
            if (!animatedThumb) {
                return;
            }
            fields->m_hoverCheckAccumulator += dt;
#if defined(GEODE_IS_WINDOWS)
            if (fields->m_hoverCheckAccumulator < 0.04f) {
                return;
            }
            fields->m_hoverCheckAccumulator = 0.0f;
#endif
#if defined(GEODE_IS_WINDOWS)
            // Check hover (solo Windows - getMousePosition solo existe en Windows)
            auto winSize = CCDirector::sharedDirector()->getWinSize();
            auto mousePos = cocos2d::CCDirector::sharedDirector()->getOpenGLView()->getMousePosition();
            // In cocos2d-x 2.2.3 (GD), getMousePosition returns y from top.
            mousePos.y = winSize.height - mousePos.y;
            
            auto* thumbParent = fields->m_thumbSprite->getParent();
            if (!thumbParent) return;
            auto nodePos = thumbParent->convertToNodeSpace(mousePos);
            auto box = fields->m_thumbSprite->boundingBox();
            
            bool hovering = box.containsPoint(nodePos);
            
            if (hovering && !fields->m_isHovering) {
                fields->m_isHovering = true;
                animatedThumb->play();
            } else if (!hovering) {
                if (fields->m_isHovering) {
                    fields->m_isHovering = false;
                }
                animatedThumb->pause();
            }
#else
            // En movil: mostrar siempre el GIF animado
            if (!fields->m_isHovering) {
                fields->m_isHovering = true;
                animatedThumb->play();
            }
#endif
        }
    }

    $override void loadCustomLevelCell() {
        LevelCell::loadCustomLevelCell();
        log::info("[LevelCell] loadCustomLevelCell levelID={}", m_level ? m_level->m_levelID.value() : 0);
        tryLoadThumbnail();
    }

    $override void loadFromLevel(GJGameLevel* level) {
        LevelCell::loadFromLevel(level);
        log::info("[LevelCell] loadFromLevel levelID={}", level ? level->m_levelID.value() : 0);
        tryLoadThumbnail();
    }

};

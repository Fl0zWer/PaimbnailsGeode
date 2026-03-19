#include "PetManager.hpp"
#include "../../../utils/ImageLoadHelper.hpp"
#include "../../../utils/AnimatedGIFSprite.hpp"
#include <Geode/loader/Mod.hpp>
#include <Geode/utils/file.hpp>
#include <fstream>
#include <algorithm>
#include <cmath>
#include <typeinfo>

using namespace geode::prelude;
using namespace cocos2d;

// singleton

PetManager& PetManager::get() {
    static PetManager inst;
    return inst;
}

// rutas

std::filesystem::path PetManager::configPath() const {
    return Mod::get()->getSaveDir() / "pet_config.json";
}

std::filesystem::path PetManager::galleryDir() const {
    auto dir = Mod::get()->getSaveDir() / "pet_gallery";
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec)) {
        std::filesystem::create_directories(dir, ec);
    }
    return dir;
}

// persistencia

void PetManager::loadConfig() {
    auto path = configPath();
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) return;

    std::ifstream f(path);
    if (!f) {
        log::error("[PetManager] Failed to open config file");
        return;
    }
    std::string raw((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    auto res = matjson::parse(raw);
    if (res.isErr()) return;
    auto j = res.unwrap();

        auto getB = [&](char const* k, bool def) -> bool {
            if (j.contains(k)) return j[k].asBool().unwrapOr(def);
            return def;
        };
        auto getF = [&](char const* k, double def) -> float {
            if (j.contains(k)) return static_cast<float>(j[k].asDouble().unwrapOr(def));
            return static_cast<float>(def);
        };
        auto getI = [&](char const* k, int def) -> int {
            if (j.contains(k)) return static_cast<int>(j[k].asInt().unwrapOr(def));
            return def;
        };
        auto getS = [&](char const* k, std::string const& def) -> std::string {
            if (j.contains(k)) return j[k].asString().unwrapOr(def);
            return def;
        };

        m_config.enabled         = getB("enabled", false);
        m_config.scale           = getF("scale", 0.5);
        m_config.sensitivity     = getF("sensitivity", 0.12);
        m_config.opacity         = getI("opacity", 220);
        m_config.bounceHeight    = getF("bounceHeight", 4.0);
        m_config.bounceSpeed     = getF("bounceSpeed", 3.0);
        m_config.rotationDamping = getF("rotationDamping", 0.3);
        m_config.maxTilt         = getF("maxTilt", 15.0);
        m_config.flipOnDirection = getB("flipOnDirection", true);
        m_config.showTrail       = getB("showTrail", false);
        m_config.trailLength     = getF("trailLength", 30.0);
        m_config.trailWidth      = getF("trailWidth", 6.0);
        m_config.idleAnimation   = getB("idleAnimation", true);
        m_config.bounce          = getB("bounce", true);
        m_config.idleBreathScale = getF("idleBreathScale", 0.04);
        m_config.idleBreathSpeed = getF("idleBreathSpeed", 1.5);
        m_config.selectedImage   = getS("selectedImage", "");
        m_config.squishOnLand    = getB("squishOnLand", true);
        m_config.squishAmount    = getF("squishAmount", 0.15);
        m_config.offsetX         = getF("offsetX", 0.0);
        m_config.offsetY         = getF("offsetY", 25.0);

        // layers visibles
        if (j.contains("visibleLayers")) {
            m_config.visibleLayers.clear();
            auto arr = j["visibleLayers"];
            if (arr.isArray()) {
                auto arrRes = arr.asArray();
                if (arrRes.isOk()) {
                    for (auto& v : arrRes.unwrap()) {
                        auto s = v.asString().unwrapOr("");
                        if (!s.empty()) m_config.visibleLayers.insert(s);
                    }
                }
            }
        }
        // si no existe la key, me quedo con el default
}

void PetManager::saveConfig() {
    matjson::Value j = matjson::Value();
    j["enabled"]         = m_config.enabled;
        j["scale"]           = static_cast<double>(m_config.scale);
        j["sensitivity"]     = static_cast<double>(m_config.sensitivity);
        j["opacity"]         = m_config.opacity;
        j["bounceHeight"]    = static_cast<double>(m_config.bounceHeight);
        j["bounceSpeed"]     = static_cast<double>(m_config.bounceSpeed);
        j["rotationDamping"] = static_cast<double>(m_config.rotationDamping);
        j["maxTilt"]         = static_cast<double>(m_config.maxTilt);
        j["flipOnDirection"] = m_config.flipOnDirection;
        j["showTrail"]       = m_config.showTrail;
        j["trailLength"]     = static_cast<double>(m_config.trailLength);
        j["trailWidth"]      = static_cast<double>(m_config.trailWidth);
        j["idleAnimation"]   = m_config.idleAnimation;
        j["bounce"]          = m_config.bounce;
        j["idleBreathScale"] = static_cast<double>(m_config.idleBreathScale);
        j["idleBreathSpeed"] = static_cast<double>(m_config.idleBreathSpeed);
        j["selectedImage"]   = m_config.selectedImage;
        j["squishOnLand"]    = m_config.squishOnLand;
        j["squishAmount"]    = static_cast<double>(m_config.squishAmount);
        j["offsetX"]         = static_cast<double>(m_config.offsetX);
        j["offsetY"]         = static_cast<double>(m_config.offsetY);

        // layers visibles
        matjson::Value layers = matjson::Value::array();
        for (auto& l : m_config.visibleLayers) {
            layers.push(l);
        }
        j["visibleLayers"] = layers;

        auto str = j.dump();
        std::ofstream f(configPath());
        if (!f) {
            log::error("[PetManager] Failed to open config file for writing");
            return;
        }
        f << str;
}

// visibilidad por escena

bool PetManager::shouldShowOnCurrentScene() const {
    // vacio = visible en todos lados
    if (m_config.visibleLayers.empty()) return true;

    // si estan todos marcados, es lo mismo que mostrar siempre
    bool allSelected = true;
    for (auto& opt : PET_LAYER_OPTIONS) {
        if (m_config.visibleLayers.count(opt) == 0) {
            allSelected = false;
            break;
        }
    }
    if (allSelected) return true;

    auto scene = CCDirector::sharedDirector()->getRunningScene();
    if (!scene) return false;

    // reviso los hijos de la escena
    auto children = scene->getChildren();
    if (!children) return false;

    for (auto* child : CCArrayExt<CCNode*>(children)) {
        if (!child) continue;

        std::string className = typeid(*child).name();

        // en MSVC viene con "class " adelante
        auto pos = className.find("class ");
        if (pos == 0) className = className.substr(6);

        // tambien pruebo por node ID
        std::string nodeID = child->getID();

        for (auto& layer : m_config.visibleLayers) {
            if (className.find(layer) != std::string::npos) return true;
            if (!nodeID.empty() && nodeID.find(layer) != std::string::npos) return true;
        }
    }
    return false;
}

// galeria

std::vector<std::string> PetManager::getGalleryImages() const {
    std::vector<std::string> result;
    auto dir = galleryDir();
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec) || ec) return result;

    for (auto& entry : std::filesystem::directory_iterator(dir)) {
        if (!entry.is_regular_file()) continue;
        auto ext = geode::utils::string::pathToString(entry.path().extension());
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".gif" || ext == ".bmp" || ext == ".webp") {
            result.push_back(geode::utils::string::pathToString(entry.path().filename()));
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

std::string PetManager::addToGallery(std::filesystem::path const& srcPath) {
    auto dir = galleryDir();
    auto filename = geode::utils::string::pathToString(srcPath.filename());

    // evito pisar archivos con el mismo nombre
    auto dest = dir / filename;
    int counter = 1;
    std::error_code existsEc;
    while (std::filesystem::exists(dest, existsEc) && !existsEc) {
        auto stem = geode::utils::string::pathToString(srcPath.stem());
        auto ext = geode::utils::string::pathToString(srcPath.extension());
        filename = fmt::format("{}_{}{}", stem, counter++, ext);
        dest = dir / filename;
    }

    std::error_code copyEc;
    std::filesystem::copy_file(srcPath, dest, std::filesystem::copy_options::overwrite_existing, copyEc);
    if (copyEc) {
        log::error("[PetManager] Failed to copy to gallery: {}", copyEc.message());
        return "";
    }
    return filename;
}

void PetManager::removeFromGallery(std::string const& filename) {
    auto path = galleryDir() / filename;
    std::error_code rmEc;
    if (std::filesystem::exists(path, rmEc)) {
        std::filesystem::remove(path, rmEc);
    }

    // si era la actual, la saco
    if (m_config.selectedImage == filename) {
        m_config.selectedImage = "";
        saveConfig();
        reloadSprite();
    }
}

void PetManager::removeAllFromGallery() {
    auto images = getGalleryImages();
    for (auto& img : images) {
        auto path = galleryDir() / img;
        std::error_code rmAllEc;
        if (std::filesystem::exists(path, rmAllEc)) {
            std::filesystem::remove(path, rmAllEc);
        }
    }

    // saco la imagen actual si hacia falta
    if (!m_config.selectedImage.empty()) {
        m_config.selectedImage = "";
        saveConfig();
        reloadSprite();
    }
}

int PetManager::cleanupInvalidImages() {
    auto images = getGalleryImages();
    int removed = 0;

    for (auto& img : images) {
        auto path = galleryDir() / img;
        std::error_code ec;
        if (!std::filesystem::exists(path, ec)) continue;

        // chequeo la cabecera real del archivo
        std::ifstream f(path, std::ios::binary);
        if (!f.is_open()) {
                removeFromGallery(img);
                removed++;
                continue;
            }

            unsigned char header[12] = {};
            f.read(reinterpret_cast<char*>(header), 12);
            auto bytesRead = f.gcount();
            f.close();

            if (bytesRead < 4) {
                log::warn("[PetManager] File too small, removing: {}", img);
                removeFromGallery(img);
                removed++;
                continue;
            }

            bool valid = false;
            // PNG: 89 50 4E 47
            if (header[0] == 0x89 && header[1] == 0x50 && header[2] == 0x4E && header[3] == 0x47) valid = true;
            // JPEG: FF D8 FF
            else if (header[0] == 0xFF && header[1] == 0xD8 && header[2] == 0xFF) valid = true;
            // GIF: GIF8
            else if (header[0] == 'G' && header[1] == 'I' && header[2] == 'F' && header[3] == '8') valid = true;
            // WEBP: RIFF....WEBP
            else if (bytesRead >= 12 && header[0] == 'R' && header[1] == 'I' && header[2] == 'F' && header[3] == 'F'
                && header[8] == 'W' && header[9] == 'E' && header[10] == 'B' && header[11] == 'P') valid = true;
            // BMP: BM
            else if (header[0] == 'B' && header[1] == 'M') valid = true;

            if (!valid) {
                log::warn("[PetManager] Invalid image file, removing: {} (bytes: {:02x} {:02x} {:02x} {:02x})",
                    img, header[0], header[1], header[2], header[3]);
                removeFromGallery(img);
                removed++;
            }
    }

    return removed;
}

CCTexture2D* PetManager::loadGalleryThumb(std::string const& filename) const {
    auto path = galleryDir() / filename;
    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || ec) return nullptr;

    auto img = ImageLoadHelper::loadStaticImage(path);
    if (img.success && img.texture) {
        return img.texture; // caller manages lifetime
    }
    return nullptr;
}

// ciclo de vida

void PetManager::init() {
    loadConfig();
}

void PetManager::setImage(std::string const& galleryFilename) {
    m_config.selectedImage = galleryFilename;
    saveConfig();
    reloadSprite();
}

void PetManager::createPetSprite() {
    if (m_config.selectedImage.empty()) return;

    auto path = galleryDir() / m_config.selectedImage;
    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || ec) return;

    auto pathStr = geode::utils::string::pathToString(path);

    // si es gif uso el sprite animado
    bool isGif = false;
    auto ext = geode::utils::string::pathToString(path.extension());
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    if (ext == ".gif") isGif = true;

    if (isGif) {
        auto anim = AnimatedGIFSprite::create(pathStr);
        if (anim) {
            m_petSprite = anim;
            return;
        }
    }

    // imagen normal
    auto img = ImageLoadHelper::loadStaticImage(path);
    if (img.success && img.texture) {
        auto spr = CCSprite::createWithTexture(img.texture);
        if (spr) {
            m_petSprite = spr;
            img.texture->release(); // sprite retains it
        }
    }
}

void PetManager::reloadSprite() {
    // limpio lo viejo antes de volver a crear
    if (m_petSprite && m_petNode) {
        m_petSprite->removeFromParent();
        m_petSprite = nullptr;
    }
    if (m_trail && m_petNode) {
        m_trail->removeFromParent();
        m_trail = nullptr;
    }

    if (!m_config.enabled || m_config.selectedImage.empty()) return;
    if (!m_petNode) return;

    createPetSprite();
    if (m_petSprite) {
        m_petSprite->setScale(m_config.scale);
        m_petSprite->setOpacity(static_cast<GLubyte>(m_config.opacity));
        m_petNode->addChild(m_petSprite, 10);

        updateTrail();
    }
}

void PetManager::attachToScene(CCScene* scene) {
    if (!scene) return;

    // me salgo de la escena anterior
    detachFromScene();

    if (!m_config.enabled) return;

    m_petNode = CCNode::create();
    m_petNode->setID("paimon-pet-host"_spr);
    m_petNode->setZOrder(99999); // always on top
    scene->addChild(m_petNode);

    // arranco centrado
    auto winSize = CCDirector::sharedDirector()->getWinSize();
    m_currentPos = ccp(winSize.width / 2.f, winSize.height / 2.f);
    m_targetPos = m_currentPos;
    m_velocity = ccp(0, 0);

    reloadSprite();
}

void PetManager::detachFromScene() {
    if (m_petNode) {
        m_petNode->removeFromParent();
        m_petNode = nullptr;
        m_petSprite = nullptr;
        m_trail = nullptr;
    }
}

// update

void PetManager::update(float dt) {
    if (!m_config.enabled || !m_petNode || !m_petSprite) return;

    // si perdio el parent en un cambio de escena, limpio referencias
    if (!m_petNode->getParent()) {
        m_petNode = nullptr;
        m_petSprite = nullptr;
        m_trail = nullptr;
        return;
    }

    auto glView = CCEGLView::sharedOpenGLView();
    if (!glView) return;
    auto winSize = CCDirector::sharedDirector()->getWinSize();

#if defined(GEODE_IS_WINDOWS)
    // en desktop sigo el mouse
    auto frameSize = glView->getFrameSize();
    float scaleX = winSize.width / frameSize.width;
    float scaleY = winSize.height / frameSize.height;
    auto mousePos = glView->getMousePosition();
    m_targetPos.x = mousePos.x * scaleX + m_config.offsetX;
    m_targetPos.y = (frameSize.height - mousePos.y) * scaleY + m_config.offsetY;
#else
    // en movil se queda en una base fija con idle
    if (m_targetPos.x == 0.f && m_targetPos.y == 0.f) {
        m_targetPos.x = winSize.width * 0.85f + m_config.offsetX;
        m_targetPos.y = winSize.height * 0.15f + m_config.offsetY;
    }
#endif

    float lerpFactor = 1.f - std::pow(1.f - m_config.sensitivity, dt * 60.f);
    lerpFactor = std::max(0.001f, std::min(1.f, lerpFactor));

    CCPoint prevPos = m_currentPos;
    m_currentPos.x += (m_targetPos.x - m_currentPos.x) * lerpFactor;
    m_currentPos.y += (m_targetPos.y - m_currentPos.y) * lerpFactor;

    m_velocity.x = (m_currentPos.x - prevPos.x) / std::max(dt, 0.001f);
    m_velocity.y = (m_currentPos.y - prevPos.y) / std::max(dt, 0.001f);

    float speed = std::sqrt(m_velocity.x * m_velocity.x + m_velocity.y * m_velocity.y);

    m_wasWalking = m_walking;
    m_walking = speed > 8.f;

    if (m_wasWalking && !m_walking && m_config.squishOnLand) {
        m_landSquishTimer = 0.2f; // 200ms squish
    }

    if (m_config.flipOnDirection && std::abs(m_velocity.x) > 5.f) {
        m_facingRight = m_velocity.x > 0;
    }
    m_petSprite->setFlipX(!m_facingRight);

    if (m_config.rotationDamping > 0.f) {
        float targetTilt = 0.f;
        if (m_walking) {
            targetTilt = std::max(-m_config.maxTilt, std::min(m_config.maxTilt,
                -m_velocity.x * 0.02f * m_config.maxTilt));
        }
        float tiltLerp = 1.f - std::pow(1.f - m_config.rotationDamping, dt * 60.f);
        m_currentTilt += (targetTilt - m_currentTilt) * tiltLerp;
        m_petSprite->setRotation(m_currentTilt);
    } else {
        m_petSprite->setRotation(0.f);
    }

    CCPoint finalPos = m_currentPos;

    if (m_config.bounce && m_config.bounceHeight > 0.f) {
        m_walkTimer += dt * m_config.bounceSpeed;
        float bounceOffset = std::abs(std::sin(m_walkTimer * 3.14159f)) * m_config.bounceHeight;
        if (m_walking) {
            finalPos.y += bounceOffset;
        } else {
            // mini bob en idle
            if (m_config.idleAnimation) {
                m_idleTimer += dt * m_config.idleBreathSpeed;
                finalPos.y += std::sin(m_idleTimer * 3.14159f) * m_config.bounceHeight * 0.3f;
            }
        }
    } else if (m_config.idleAnimation && !m_walking) {
        m_idleTimer += dt * m_config.idleBreathSpeed;
        finalPos.y += std::sin(m_idleTimer * 3.14159f) * 2.f;
    }

    if (m_landSquishTimer > 0.f) {
        m_landSquishTimer -= dt;
        float t = m_landSquishTimer / 0.2f;
        float squishX = 1.f + m_config.squishAmount * t;
        float squishY = 1.f - m_config.squishAmount * t;
        m_petSprite->setScaleX(m_config.scale * squishX);
        m_petSprite->setScaleY(m_config.scale * squishY);
    } else {
        // respiracion en idle
        if (m_config.idleAnimation && !m_walking) {
            float breath = 1.f + std::sin(m_idleTimer * 3.14159f * 2.f) * m_config.idleBreathScale;
            m_petSprite->setScaleX(m_config.scale * breath);
            m_petSprite->setScaleY(m_config.scale * (2.f - breath)); // inverse on Y for breathing
        } else {
            m_petSprite->setScale(m_config.scale);
        }
    }

    m_petSprite->setPosition(finalPos);

    if (m_trail) {
        m_trail->setPosition(finalPos);
    }
}

void PetManager::applyConfigLive() {
    if (!m_petSprite) return;

    m_petSprite->setScale(m_config.scale);
    m_petSprite->setOpacity(static_cast<GLubyte>(m_config.opacity));

    updateTrail();
    saveConfig();
}

void PetManager::updateTrail() {
    // primero saco el trail anterior
    if (m_trail && m_petNode) {
        m_trail->removeFromParent();
        m_trail = nullptr;
    }

    if (!m_config.showTrail || !m_petNode || !m_petSprite) return;

    // textura blanca minima para no depender de assets externos
    static CCTexture2D* s_whiteTrailTex = nullptr;
    if (!s_whiteTrailTex) {
        const int sz = 2;
        uint8_t pixels[sz * sz * 4];
        memset(pixels, 255, sizeof(pixels));
        s_whiteTrailTex = new CCTexture2D();
        if (!s_whiteTrailTex->initWithData(pixels, kCCTexture2DPixelFormat_RGBA8888, sz, sz, CCSizeMake(sz, sz))) {
            s_whiteTrailTex->release();
            s_whiteTrailTex = nullptr;
        }
    }

    if (!s_whiteTrailTex) return;

    m_trail = CCMotionStreak::create(
        m_config.trailLength / 60.f,  // fade time
        1.f,                           // min seg
        m_config.trailWidth,           // stroke width
        ccc3(255, 255, 255),           // color
        s_whiteTrailTex                // safe texture ptr
    );

    if (m_trail && m_trail->getTexture()) {
        m_trail->setOpacity(static_cast<GLubyte>(m_config.opacity * 0.4f));
        ccBlendFunc blend = {GL_SRC_ALPHA, GL_ONE};
        m_trail->setBlendFunc(blend);
        m_petNode->addChild(m_trail, 5);
    } else {
        m_trail = nullptr;
        log::warn("[PetManager] Failed to create trail with valid texture");
    }
}

// stubs

void PetManager::updateIdleAnimation(float dt) {}
void PetManager::updateWalkAnimation(float dt) {}


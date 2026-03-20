#pragma once
#include <Geode/Geode.hpp>
#include <filesystem>
#include <vector>
#include <string>
#include <set>
#include <functional>

// ────────────────────────────────────────────────────────────
// PetConfig: all pet settings, serializable to JSON
// ────────────────────────────────────────────────────────────

// Supported layer names for pet visibility
inline std::vector<std::string> PET_LAYER_OPTIONS = {
    "MenuLayer", "LevelBrowserLayer", "LevelInfoLayer",
    "CreatorLayer", "LevelSearchLayer", "GauntletSelectLayer",
    "ProfilePage", "LevelListLayer", "LevelEditorLayer"
};

struct PetConfig {
    bool enabled = false;
    float scale = 0.5f;          // 0.1 – 3.0
    float sensitivity = 0.12f;   // 0.01 – 1.0  (lerp factor per frame)
    int   opacity = 220;         // 0 – 255
    float bounceHeight = 4.f;    // 0 – 20  pixels
    float bounceSpeed = 3.f;     // 0.5 – 10  cycles/sec
    float rotationDamping = 0.3f;// 0 – 1
    float maxTilt = 15.f;        // 0 – 45 degrees
    bool  flipOnDirection = true;
    bool  showTrail = false;
    float trailLength = 30.f;    // 5 – 100
    float trailWidth = 6.f;      // 1 – 20
    bool  idleAnimation = true;
    bool  bounce = true;
    float idleBreathScale = 0.04f; // 0 – 0.15
    float idleBreathSpeed = 1.5f;  // 0.5 – 5
    std::string selectedImage;     // filename in gallery dir (empty = none)

    // squish on land
    bool squishOnLand = true;
    float squishAmount = 0.15f;  // 0 – 0.5

    // offset from cursor
    float offsetX = 0.f;        // -50 – 50
    float offsetY = 25.f;       // -50 – 100  (default: above cursor)

    // layer visibility (if empty, visible everywhere)
    std::set<std::string> visibleLayers = {
        "MenuLayer", "LevelBrowserLayer", "LevelInfoLayer",
        "CreatorLayer", "LevelSearchLayer", "GauntletSelectLayer",
        "ProfilePage", "LevelListLayer", "LevelEditorLayer"
    };
};

// ────────────────────────────────────────────────────────────
// PetManager: singleton
// ────────────────────────────────────────────────────────────

class PetManager {
public:
    static PetManager& get();

    // lifecycle
    void init();
    void update(float dt);
    void attachToScene(cocos2d::CCScene* scene);
    void detachFromScene();
    void releaseSharedResources();

    // config
    PetConfig& config() { return m_config; }
    void loadConfig();
    void saveConfig();
    void applyConfigLive();   // push current config to sprite

    // image
    void setImage(std::string const& galleryFilename);
    void reloadSprite();

    // gallery
    std::vector<std::string> getGalleryImages() const;
    std::string addToGallery(std::filesystem::path const& srcPath);   // returns filename
    void removeFromGallery(std::string const& filename);
    void removeAllFromGallery();
    int cleanupInvalidImages();  // removes non-image files, returns count removed
    std::filesystem::path galleryDir() const;
    cocos2d::CCTexture2D* loadGalleryThumb(std::string const& filename) const;

    // state (read-only)
    bool isAttached() const { return m_petNode != nullptr && m_petNode->getParent() != nullptr; }
    bool isWalking() const { return m_walking; }
    bool shouldShowOnCurrentScene() const;

private:
    PetManager() = default;

    PetConfig m_config;

    // scene node tree: hostNode -> petSprite / trailNode
    geode::Ref<cocos2d::CCNode> m_petNode = nullptr;        // host node added to scene
    cocos2d::CCSprite* m_petSprite = nullptr;     // the actual image (child of m_petNode)
    cocos2d::CCMotionStreak* m_trail = nullptr;   // child of m_petNode

    // physics state
    cocos2d::CCPoint m_currentPos;
    cocos2d::CCPoint m_targetPos;
    cocos2d::CCPoint m_velocity;
    float m_idleTimer = 0.f;
    float m_walkTimer = 0.f;
    bool  m_walking = false;
    bool  m_facingRight = true;
    float m_currentTilt = 0.f;
    float m_landSquishTimer = 0.f;  // >0 while squishing
    bool  m_wasWalking = false;

    // helpers
    std::filesystem::path configPath() const;
    void createPetSprite();
    void updateIdleAnimation(float dt);
    void updateWalkAnimation(float dt);
    void updateTrail();
};





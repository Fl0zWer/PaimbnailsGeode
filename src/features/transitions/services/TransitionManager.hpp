#pragma once
#include <Geode/Geode.hpp>
#include <cocos2d.h>
#include <string>
#include <vector>
#include <optional>

// TransitionManager — sistema de transiciones personalizables
//
// Diseno simple:
//   - UNA transicion global para TODA navegacion (push/replace/pop)
//   - UNA transicion opcional separada para entrada a nivel (PlayLayer)
//   - 20+ tipos de transicion nativos + custom DSL

enum class TransitionType {
    // Fades
    Fade,               // Fade to black (or configured color)
    FadeWhite,          // Fade to white
    FadeColor,          // Fade to user-selected color
    CrossFade,          // Both scenes overlap with alpha blend

    // Slides
    SlideLeft,          // New scene slides in from left
    SlideRight,         // New scene slides in from right
    SlideUp,            // New scene slides in from top
    SlideDown,          // New scene slides in from bottom
    MoveInLeft,         // New scene moves in, old stays
    MoveInRight,        // New scene moves in from right, old stays

    // Zooms & Flips
    ZoomIn,             // Zoom flip (left-over)
    ZoomOut,            // Zoom flip (right-over)
    FlipX,              // Horizontal flip
    FlipY,              // Vertical flip
    FlipAngular,        // Angular flip (diagonal)
    ShrinkGrow,         // Old shrinks, new grows
    RotoZoom,           // Rotate + zoom combo
    JumpZoom,           // Jump + zoom combo

    // Tiles & Progress
    FadeTR,             // Tiles fade from top-right
    FadeBL,             // Tiles fade from bottom-left
    TurnOffTiles,       // Tiles turn off randomly
    SplitCols,          // Split into columns
    SplitRows,          // Split into rows
    ProgressRadialCW,   // Radial wipe clockwise (circle iris)
    ProgressRadialCCW,  // Radial wipe counter-clockwise
    ProgressInOut,      // Iris in/out (circle open/close)

    // Pages
    PageForward,        // Page curl forward
    PageBackward,       // Page curl backward

    // Special
    Custom,             // User-defined DSL commands
    None                // Instant, no animation
};

enum class CommandAction {
    FadeOut,
    FadeIn,
    Move,
    Scale,
    Rotate,
    Wait,
    Color,
    EaseIn,
    EaseOut,
    Spawn,      // Execute next N commands in parallel
    Image,      // Display an image overlay
    Shake,      // Camera shake effect
    Bounce      // Bounce easing
};

struct TransitionCommand {
    CommandAction action = CommandAction::Wait;
    std::string target = "from";  // "from", "to", or "overlay"
    float duration = 0.3f;
    float fromX = 0.f, fromY = 0.f;
    float toX = 0.f, toY = 0.f;
    float fromVal = 1.f;
    float toVal = 1.f;
    int r = 0, g = 0, b = 0;
    std::string imagePath;     // for Image action
    int spawnCount = 0;        // for Spawn: how many next commands run in parallel
    float delay = 0.f;         // delay before this command starts
    float intensity = 5.f;     // for Shake: amplitude
};

struct TransitionConfig {
    TransitionType type = TransitionType::Fade;
    float duration = 0.5f;
    int colorR = 0, colorG = 0, colorB = 0;
    std::string imagePath;   // for image-based transitions
    std::vector<std::string> imageList; // multiple images for Custom
    std::vector<TransitionCommand> commands;
    std::string scriptPath;
};

class TransitionManager {
public:
    static TransitionManager& get();

    void loadConfig();
    void saveConfig();

    TransitionConfig getGlobalConfig() const { return m_globalConfig; }
    void setGlobalConfig(TransitionConfig const& cfg) { m_globalConfig = cfg; }

    TransitionConfig getLevelEntryConfig() const;
    void setLevelEntryConfig(TransitionConfig const& cfg);
    bool hasLevelEntryConfig() const { return m_hasLevelEntryConfig; }
    void clearLevelEntryConfig();

    cocos2d::CCScene* createTransition(TransitionConfig const& cfg, cocos2d::CCScene* dest);

    void replaceScene(cocos2d::CCScene* dest);
    void pushScene(cocos2d::CCScene* dest);

    bool isEnabled() const { return m_enabled; }
    void setEnabled(bool v) { m_enabled = v; }

    static TransitionType typeFromString(std::string const& s);
    static std::string typeToString(TransitionType t);
    static std::string typeDisplayName(TransitionType t);
    static std::string typeDescription(TransitionType t);
    static CommandAction actionFromString(std::string const& s);
    static std::string actionToString(CommandAction a);

    static std::vector<TransitionType> const& allTypes();

    cocos2d::CCTransitionScene* createNativeTransition(TransitionConfig const& cfg, cocos2d::CCScene* dest) const;

private:
    TransitionManager();

    std::vector<TransitionCommand> parseScriptFile(std::string const& path) const;
    std::filesystem::path getConfigPath() const;

    TransitionConfig m_globalConfig;
    TransitionConfig m_levelEntryConfig;
    bool m_hasLevelEntryConfig = false;
    bool m_enabled = true;
    bool m_loaded = false;
};

#pragma once
#include <Geode/Geode.hpp>
#include <Geode/utils/function.hpp>
#include <Geode/binding/Slider.hpp>

class ThumbnailSettingsPopup : public geode::Popup {
protected:
    Slider* m_intensitySlider = nullptr;
    cocos2d::CCLabelBMFont* m_intensityLabel = nullptr;
    Slider* m_darknessSlider = nullptr;
    cocos2d::CCLabelBMFont* m_darknessLabel = nullptr;
    cocos2d::CCLabelBMFont* m_styleValueLabel = nullptr;
    CCMenuItemToggler* m_dynamicSongToggle = nullptr;

    std::string m_currentStyle;
    int m_currentIntensity = 5;
    int m_currentDarkness = 27;
    bool m_dynamicSong = false;

    std::vector<std::string> m_styles;
    int m_styleIndex = 0;

    geode::CopyableFunction<void()> m_onSettingsChanged;

    // peek mode: oculta popups para ver el fondo
    bool m_peekMode = false;
    cocos2d::CCMenu* m_peekMenu = nullptr;

    bool init() override;

    void onStylePrev(cocos2d::CCObject*);
    void onStyleNext(cocos2d::CCObject*);
    void onIntensityChanged(cocos2d::CCObject*);
    void onDarknessChanged(cocos2d::CCObject*);
    void onDynamicSongToggled(cocos2d::CCObject*);
    void onOpenExtraEffects(cocos2d::CCObject*);
    void onTogglePeek(cocos2d::CCObject*);
    void onClose(cocos2d::CCObject*) override;

    void updateStyleLabel();
    void saveSettings();
    std::string getStyleDisplayName(std::string const& style);

public:
    static ThumbnailSettingsPopup* create();
    void setOnSettingsChanged(geode::CopyableFunction<void()> cb) { m_onSettingsChanged = std::move(cb); }
    void togglePeek();
};

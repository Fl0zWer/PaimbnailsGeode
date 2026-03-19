#pragma once

#include <Geode/Geode.hpp>

class PaimonSupportLayer : public cocos2d::CCLayer {
protected:
    bool init() override;
    void keyBackClicked() override;

    void onBack(cocos2d::CCObject*);
    void onDonate(cocos2d::CCObject*);

    // armado de UI
    void createBackground();
    void createTitle();
    void createBadgePanel();
    void createBenefitsPanel();
    void createThankYouSection();
    void createButtons();
    void createParticles();
    void spawnParticles(float dt);

    // fondo dinamico con thumbs
    void loadShowcaseThumbnails();
    void cycleThumbnail(float dt);
    void applyThumbnailBackground(cocos2d::CCTexture2D* texture);

    cocos2d::CCSprite* m_bgThumb = nullptr;
    cocos2d::CCNode* m_bgDiagonalGlow = nullptr;
    std::vector<std::string> m_cachedThumbPaths;
    int m_currentThumbIndex = 0;
    bool m_loadingThumb = false;

public:
    static PaimonSupportLayer* create();
    static cocos2d::CCScene* scene();
};

#pragma once
#include <Geode/DefaultInclude.hpp>
#include <Geode/ui/LoadingSpinner.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <Geode/binding/GJUserScore.hpp>
#include <Geode/binding/CustomListView.hpp>
#include <Geode/binding/GJListLayer.hpp>
#include <Geode/utils/async.hpp>
#include <Geode/utils/web.hpp>
#include <fmod.hpp>
#include <memory>
#include <unordered_set>

class CommunityHubLayer : public cocos2d::CCLayer {
public:
    static CommunityHubLayer* create();
    static cocos2d::CCScene* scene();

    enum class Tab {
        Moderators,
        TopCreators,
        TopThumbnails
    };

protected:
    bool init() override;
    void keyBackClicked() override;
    void onEnterTransitionDidFinish() override;
    void update(float dt) override;
    bool ccMouseScroll(float x, float y);

    void onBack(cocos2d::CCObject* sender);
    void onTab(cocos2d::CCObject* sender);
    void onModProfile(cocos2d::CCObject* sender);
    void onDeferredModeratorsRebuild(float);

    // data loading
    void loadTab(Tab tab);
    void loadModerators();
    void loadTopCreators();
    void loadTopThumbnails();

    // list building
    void buildModeratorsList();
    void buildCreatorsList();
    void buildThumbnailsList();
    void requestModeratorsListRebuild();

    // GDBrowser profile fetch (for moderators)
    void fetchGDBrowserProfile(std::string const& username, std::string const& role);
    void onProfileFetched(std::string const& username, std::string const& jsonData, std::string const& role);
    void onAllProfilesFetched();

    void clearList();
    void ensureBgSilenced();

    // tab state
    Tab m_currentTab = Tab::Moderators;
    cocos2d::CCMenu* m_tabsMenu = nullptr;
    std::vector<CCMenuItemToggler*> m_tabs;
    bool m_isLoadingTab = false;
    bool m_moderatorsRebuildQueued = false;

    // loading
    geode::LoadingSpinner* m_loadingSpinner = nullptr;

    // list container
    cocos2d::CCNode* m_listContainer = nullptr;
    geode::ScrollLayer* m_scrollView = nullptr;

    // moderators data
    struct ModEntry {
        std::string username;
        std::string role; // "admin" or "mod"
    };
    std::vector<ModEntry> m_modEntries;
    geode::Ref<cocos2d::CCArray> m_modScores;
    int m_pendingProfiles = 0;
    std::unordered_set<int> m_requestedModeratorProfiles;

    // creators data
    struct CreatorEntry {
        std::string username;
        int accountID = 0;
        int uploadCount = 0;
        float avgRating = 0.f;
    };
    std::vector<CreatorEntry> m_creatorEntries;

    // thumbnails data
    struct ThumbnailEntry {
        int levelId = 0;
        float rating = 0.f;
        int count = 0;
        std::string uploadedBy;
        int accountID = 0;
    };
    std::vector<ThumbnailEntry> m_thumbnailEntries;

    // FMOD efecto cueva sobre musica de menu
    FMOD::DSP* m_lowpassDSP = nullptr;
    FMOD::DSP* m_reverbDSP = nullptr;
    float m_savedBgVolume = 1.0f;
    bool m_caveApplied = false;
    bool m_isExiting = false;
    std::vector<std::unique_ptr<geode::async::TaskHolder<geode::utils::web::WebResponse>>> m_ownedRequests;

    void applyCaveEffect();
    void removeCaveEffect();

public:
    ~CommunityHubLayer();
};

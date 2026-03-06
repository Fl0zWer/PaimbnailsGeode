#pragma once
#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/ui/LoadingSpinner.hpp>
#include <Geode/binding/UserInfoDelegate.hpp>
#include <Geode/binding/GameLevelManager.hpp>
#include <Geode/binding/GJUserScore.hpp>

class ModeratorsLayer : public geode::Popup, public UserInfoDelegate {
protected:
    geode::Ref<cocos2d::CCArray> m_scores;
    CustomListView* m_listView;
    GJListLayer* m_listLayer;
    geode::LoadingSpinner* m_loadingSpinner = nullptr;
    int m_pendingRequests = 0;
    std::vector<std::string> m_moderatorNames;

    bool init() override;
    void setup();
    void createList();
    void fetchModerators();
    void fetchGDBrowserProfile(std::string const& username);
    void onProfileFetched(std::string const& username, std::string const& response);
    void onAllProfilesFetched();
    
    void getUserInfoFinished(GJUserScore* score) override;
    void getUserInfoFailed(int type) override;
    void userInfoChanged(GJUserScore* score) override;

    void onAPIKeyParams(cocos2d::CCObject*);


    virtual ~ModeratorsLayer();

public:
    static ModeratorsLayer* s_instance;
    bool isScoreInList(GJUserScore* score);
    static ModeratorsLayer* create();
};

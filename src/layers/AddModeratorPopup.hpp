#pragma once

#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/TextInput.hpp>
#include <Geode/cocos/extensions/GUI/CCScrollView/CCScrollView.h>

using namespace geode::prelude;

class AddModeratorPopup : public Popup {
protected:
    geode::TextInput* m_usernameInput = nullptr;
    LoadingCircle* m_loadingCircle = nullptr;
    std::function<void(bool, const std::string&)> m_callback;

    cocos2d::CCNode* m_listContainer = nullptr;
    cocos2d::extension::CCScrollView* m_scroll = nullptr;
    std::vector<std::string> m_moderatorNames;
    
    bool init(std::function<void(bool, const std::string&)> callback);
    void onAdd(CCObject*);
    void onRemove(CCObject* sender);
    void fetchAndShowModerators();
    void rebuildList();
    
public:
    static AddModeratorPopup* create(std::function<void(bool, const std::string&)> callback);
};

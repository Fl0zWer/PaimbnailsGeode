#pragma once
#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/TextInput.hpp>

using namespace geode::prelude;

class ReportInputPopup : public Popup {
protected:
    int m_levelID = 0;
    geode::TextInput* m_textInput = nullptr;
    std::function<void(std::string)> m_callback;
    
    bool init(int levelID, std::function<void(std::string)> callback);
    void onSend(CCObject*);
    
public:
    static ReportInputPopup* create(int levelID, std::function<void(std::string)> callback);
};

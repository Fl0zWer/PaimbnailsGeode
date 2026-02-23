#pragma once
#include <Geode/Geode.hpp>

using namespace geode::prelude;

class ReportInputPopup : public FLAlertLayer, public TextInputDelegate, public FLAlertLayerProtocol {
protected:
    int m_levelID = 0;
    CCTextInputNode* m_textInput = nullptr;
    std::function<void(std::string)> m_callback;
    
    bool init(int levelID, std::function<void(std::string)> callback);
    void FLAlert_Clicked(FLAlertLayer* layer, bool btn2) override;
    
public:
    static ReportInputPopup* create(int levelID, std::function<void(std::string)> callback);
};

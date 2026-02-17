#include "ReportInputPopup.hpp"
#include "../utils/Localization.hpp"

bool ReportInputPopup::init(int levelID, std::function<void(std::string)> callback) {
    m_levelID = levelID;
    m_callback = callback;
    
    if (!FLAlertLayer::init(this, 
        Localization::get().getString("report.title").c_str(),
        "",
        Localization::get().getString("report.cancel").c_str(), Localization::get().getString("report.send").c_str(), 420.f, false, 280.f, 1.0f)) return false;
    
    // tamaño popup
    if (m_mainLayer) {
        auto currentSize = m_mainLayer->getContentSize();
        m_mainLayer->setContentSize({currentSize.width, currentSize.height + 60.f});
        
        // fondo
        auto bgSprite = typeinfo_cast<CCScale9Sprite*>(m_mainLayer->getChildren()->objectAtIndex(0));
        if (bgSprite) {
            bgSprite->setContentSize({bgSprite->getContentSize().width, bgSprite->getContentSize().height + 60.f});
        }
    }
    
    // pos menú
    if (m_buttonMenu) {
        m_buttonMenu->setPositionY(m_buttonMenu->getPositionY() - 5.f);
    }
    
    // pos labels
    if (m_mainLayer && m_mainLayer->getChildrenCount() > 0) {
        auto children = m_mainLayer->getChildren();
        for (unsigned int i = 0; i < children->count(); i++) {
            auto child = typeinfo_cast<CCLabelBMFont*>(children->objectAtIndex(i));
            if (child) {
                child->setPositionY(child->getPositionY() + 5.f);
            }
        }
    }
    
    // campo de texto
    auto inputBG = CCScale9Sprite::create("square02b_small.png");
    inputBG->setColor({40, 40, 40});
    inputBG->setOpacity(255);
    inputBG->setContentSize({360.f, 50.f});
    inputBG->setPosition({m_mainLayer->getContentWidth() / 2, m_mainLayer->getContentHeight() / 2 - 20.f});
    m_mainLayer->addChild(inputBG, 10);
    
    m_textInput = CCTextInputNode::create(340.f, 40.f, Localization::get().getString("report.placeholder").c_str(), "chatFont.fnt");
    m_textInput->setLabelPlaceholderColor({150, 150, 150});
    m_textInput->setLabelPlaceholderScale(0.8f);
    m_textInput->setAllowedChars("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 .,;:!?()-_");
    m_textInput->setMaxLabelLength(120);
    m_textInput->setPosition(inputBG->getPosition());
    m_textInput->setDelegate(this);
    m_textInput->setString("");
    m_mainLayer->addChild(m_textInput, 11);
    
    return true;
}

void ReportInputPopup::FLAlert_Clicked(FLAlertLayer* layer, bool btn2) {
    if (btn2) {
        std::string reason = m_textInput->getString();
        if (reason.empty() || reason == Localization::get().getString("report.placeholder")) {
            Notification::create(Localization::get().getString("report.empty_reason").c_str(), NotificationIcon::Warning)->show();
            return;
        }
        
        if (m_callback) {
            m_callback(reason);
        }
    }
}

ReportInputPopup* ReportInputPopup::create(int levelID, std::function<void(std::string)> callback) {
    auto ret = new ReportInputPopup();
    if (ret && ret->init(levelID, callback)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

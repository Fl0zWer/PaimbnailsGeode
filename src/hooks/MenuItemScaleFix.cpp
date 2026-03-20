#include <Geode/modify/CCMenuItemSpriteExtra.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/loader/Log.hpp>
#include <Geode/loader/Mod.hpp>

#include "../utils/PaimonButtonHighlighter.hpp"

using namespace geode::prelude;

// conserva la escala original de nuestros botones despues del efecto de GD
class $modify(PaimonMenuItemScaleFix, CCMenuItemSpriteExtra) {
    static void onModify(auto& self) {
        // VeryLate porque toca todos los botones, menos chance de pisar otros mods
        (void)self.setHookPriorityPost("CCMenuItemSpriteExtra::selected", geode::Priority::VeryLate);
        (void)self.setHookPriorityPost("CCMenuItemSpriteExtra::unselected", geode::Priority::VeryLate);
        (void)self.setHookPriorityPost("CCMenuItemSpriteExtra::activate", geode::Priority::VeryLate);
    }

    struct Fields {
        float m_originalScale = 1.0f;
        bool m_scaleCaptured = false;
    };

    $override
    void selected() {
        if (PaimonButtonHighlighter::isRegisteredButton(this)) {
            if (!m_fields->m_scaleCaptured) {
                m_fields->m_originalScale = this->getScale();
                m_fields->m_scaleCaptured = true;
            }
        }
        
        CCMenuItemSpriteExtra::selected();
    }

    $override
    void unselected() {
        CCMenuItemSpriteExtra::unselected();
        
        if (PaimonButtonHighlighter::isRegisteredButton(this)) {
            if (m_fields->m_scaleCaptured) {
                this->stopAllActions();
                auto scaleTo = CCScaleTo::create(0.2f, m_fields->m_originalScale);
                this->runAction(CCEaseSineOut::create(scaleTo));
            }
        }
    }
    
    $override
    void activate() {
        CCMenuItemSpriteExtra::activate();
        
        if (PaimonButtonHighlighter::isRegisteredButton(this)) {
            if (m_fields->m_scaleCaptured) {
                this->stopAllActions();
                this->setScale(m_fields->m_originalScale);
            }
        }
    }
};

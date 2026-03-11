#include <Geode/modify/CCMenuItemSpriteExtra.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/loader/Log.hpp>
#include <Geode/loader/Mod.hpp>

#include "../utils/PaimonButtonHighlighter.hpp"

using namespace geode::prelude;

// Simplificado: mantiene el comportamiento de GD pero conserva la escala original.
class $modify(PaimonMenuItemScaleFix, CCMenuItemSpriteExtra) {
    static void onModify(auto& self) {
        // VeryLate = correr despues de casi todos los otros mods
        // este hook intercepta TODOS los botones del juego, asi que minimizamos conflictos
        (void)self.setHookPriorityPost("CCMenuItemSpriteExtra::selected", geode::Priority::VeryLate);
        (void)self.setHookPriorityPost("CCMenuItemSpriteExtra::unselected", geode::Priority::VeryLate);
    }

    struct Fields {
        float m_originalScale = 1.0f;
        bool m_scaleCaptured = false;
    };

    void selected() {
        // solo guardamos escala para los botones que registramos nosotros
        if (PaimonButtonHighlighter::isRegisteredButton(this)) {
            if (!m_fields->m_scaleCaptured) {
                m_fields->m_originalScale = this->getScale();
                m_fields->m_scaleCaptured = true;
            }
        }
        
        CCMenuItemSpriteExtra::selected();
    }

    void unselected() {
        CCMenuItemSpriteExtra::unselected();
        
        if (PaimonButtonHighlighter::isRegisteredButton(this)) {
            if (m_fields->m_scaleCaptured) {
                // cancela la animacion de escala por defecto de GD
                this->stopAllActions();
                
                // vuelve suavemente a la escala original
                auto scaleTo = CCScaleTo::create(0.2f, m_fields->m_originalScale);
                this->runAction(CCEaseSineOut::create(scaleTo));
            }
        }
    }
    
    void activate() {
        CCMenuItemSpriteExtra::activate();
        
        if (PaimonButtonHighlighter::isRegisteredButton(this)) {
            if (m_fields->m_scaleCaptured) {
                // restaura la escala original de golpe al activar
                this->stopAllActions();
                this->setScale(m_fields->m_originalScale);
            }
        }
    }
};

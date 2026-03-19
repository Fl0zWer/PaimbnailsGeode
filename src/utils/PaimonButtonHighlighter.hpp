#pragma once
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <cocos2d.h>

// marca botones del mod
class PaimonButtonHighlighter {
public:
    // registra un boton del mod
    static void registerButton(CCMenuItemSpriteExtra* btn) {
        if (!btn) return;
        
        // conserva el ID original y solo le mete prefijo
        std::string currentID = btn->getID();
        // evita prefijarlo dos veces
        if (currentID.find("paimon-mod-btn") != 0) {
            std::string newID = currentID.empty() ? "paimon-mod-btn" : ("paimon-mod-btn-" + currentID);
            btn->setID(newID);
        }
    }
    
    // mira si ya esta marcado
    static bool isRegisteredButton(CCMenuItemSpriteExtra* btn) {
        if (!btn) return false;
        // lo hago por ID para que sobreviva al remove/re-add
        std::string id = btn->getID();
        return id.find("paimon-mod-btn") == 0;
    }
};

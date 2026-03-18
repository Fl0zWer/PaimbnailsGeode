#pragma once

#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>


/*
 * ProfileImgPopup, Popup overlay que muestra la imagen de perfil
 * como fondo del popup, clipeada para que nunca sobresalga.
 * Se posiciona exactamente encima del ProfilePage popup
 */
class ProfileImgPopup : public geode::Popup {
protected:
    int m_accountID;
    geode::Ref<cocos2d::CCTexture2D> m_texture;
    cocos2d::CCClippingNode* m_imgClip = nullptr;

    bool init(int accountID, cocos2d::CCTexture2D* texture);

public:
    static ProfileImgPopup* create(int accountID, cocos2d::CCTexture2D* texture);
};


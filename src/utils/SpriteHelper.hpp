#pragma once

#include <Geode/cocos/include/cocos2d.h>
#include <Geode/cocos/extensions/GUI/CCControlExtension/CCScale9Sprite.h>

namespace paimon {

// Utilidad para validar sprites y mantener fallbacks deterministas.
// Con Geode 5.4, createWithSpriteFrameName / create(file) pueden devolver
// un fallback sprite en vez de nullptr cuando el asset falta.
struct SpriteHelper {

    // Crea un CCDrawNode rectangular para usar como stencil en CCClippingNode.
    // Evita usar CCScale9Sprite (hookeado por HappyTextures) o CCLayerColor
    // (problemas con anchorPoint en CCLayer). CCDrawNode es geometria pura.
    static cocos2d::CCDrawNode* createRectStencil(float width, float height) {
        auto stencil = cocos2d::CCDrawNode::create();
        cocos2d::CCPoint rect[4] = {
            ccp(0, 0),
            ccp(width, 0),
            ccp(width, height),
            ccp(0, height)
        };
        cocos2d::ccColor4F white = {1, 1, 1, 1};
        stencil->drawPolygon(rect, 4, white, 0, white);
        return stencil;
    }

    // Verifica si un sprite es utilizable para el mod.
    static bool isValidSprite(cocos2d::CCSprite* spr) {
        if (!spr) return false;
        if (spr->isUsingFallback()) return false;
        auto tex = spr->getTexture();
        if (!tex) return false;
        auto size = tex->getContentSizeInPixels();
        // la textura placeholder de cocos2d es 2x2 magenta
        if (size.width <= 2.f && size.height <= 2.f) return false;
        return true;
    }

    // Wrapper seguro de createWithSpriteFrameName que retorna null si el frame
    // no existe o si Geode devolvio un fallback sprite/frame.
    static cocos2d::CCSprite* safeCreateWithFrameName(const char* frameName) {
        auto frame = cocos2d::CCSpriteFrameCache::sharedSpriteFrameCache()->spriteFrameByName(frameName);
        if (!frame || frame->isUsingFallback()) return nullptr;
        auto spr = cocos2d::CCSprite::createWithSpriteFrame(frame);
        if (!isValidSprite(spr)) return nullptr;
        return spr;
    }

    // Wrapper seguro de create(file) que trata el fallback integrado de Geode
    // como un fallo real para poder encadenar fallbacks propios.
    static cocos2d::CCSprite* safeCreate(const char* file) {
        auto spr = cocos2d::CCSprite::create(file);
        if (!isValidSprite(spr)) return nullptr;
        return spr;
    }

    // Wrapper seguro de CCScale9Sprite::create que retorna null si la textura
    // no existe. CCScale9Sprite::create crashea internamente en vez de
    // retornar nullptr cuando el sprite no se encuentra.
    static cocos2d::extension::CCScale9Sprite* safeCreateScale9(const char* file) {
        auto* tex = cocos2d::CCTextureCache::sharedTextureCache()->addImage(file, false);
        if (!tex) return nullptr;
        return cocos2d::extension::CCScale9Sprite::create(file);
    }

    // Variante segura para sprite frames, evitando que el fallback de Geode 5.4
    // corte cadenas de fallback del mod.
    static cocos2d::extension::CCScale9Sprite* safeCreateScale9WithFrameName(const char* frameName) {
        auto frame = cocos2d::CCSpriteFrameCache::sharedSpriteFrameCache()->spriteFrameByName(frameName);
        if (!frame || frame->isUsingFallback()) return nullptr;
        return cocos2d::extension::CCScale9Sprite::createWithSpriteFrame(frame);
    }
};

} // namespace paimon

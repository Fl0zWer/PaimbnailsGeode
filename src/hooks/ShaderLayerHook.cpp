// ─────────────────────────────────────────────────────────────
// ShaderLayerHook.cpp
//
// durante el juego normal esto es un pass-through.
//
// estrategia de captura:
//   cuando los shaders estan activos, framebuffercapture usa captura
//   directa (lectura de back-buffer) que ya contiene el frame final
//   compuesto con todos los efectos. por tanto este
//   hook NO necesita modificar shaderlayer durante la captura.
//
//   cuando los shaders NO estan activos, framebuffercapture usa
//   modo rerender, y shaderlayer no tiene m_rendertexture,
//   asi que tampoco se necesita manejo especial.
//
//   el hook se mantiene para logs de diagnostico durante captura y
//   como punto de extension para mejoras futuras.
// ─────────────────────────────────────────────────────────────

#include <Geode/Geode.hpp>
#include <Geode/modify/ShaderLayer.hpp>
#include "../utils/FramebufferCapture.hpp"

using namespace geode::prelude;
using namespace cocos2d;

class $modify(PaimonShaderLayer, ShaderLayer) {
    void visit() {
        if (FramebufferCapture::isCapturing() && m_renderTexture) {
            log::debug("[ShaderLayerHook] ShaderLayer::visit() during capture "
                       "(screen={}x{}, target={}x{})",
                       m_screenSize.width, m_screenSize.height,
                       m_targetTextureSize.width, m_targetTextureSize.height);
        }
        ShaderLayer::visit();
    }
};

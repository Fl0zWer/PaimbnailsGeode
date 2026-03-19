#pragma once

#include <Geode/DefaultInclude.hpp>
#include <Geode/utils/function.hpp>
#include <string>

namespace Assets {

// carga un sprite de boton desde un txt override en el save del mod
// - key: nombre logico del boton; si falta, crea assets/buttons/<key>.txt
// - defaultContent: contenido base para el archivo nuevo
// - fallback: lo que se usa si el override no sirve
//
// formato del override: se usa la primera linea no vacia
//   frame:FrameName.png         -> carga desde el atlas de sprite frames
//   file:C:\path\img.png     -> carga el PNG desde ruta absoluta o relativa
//   C:\path\img.png           -> igual que file:, ruta a un PNG
//   (vacio)                    -> usa el fallback
// devuelve un CCSprite* autorelease; si todo falla, al menos devuelve uno vacio valido
cocos2d::CCSprite* loadButtonSprite(
    std::string const& key,
    std::string const& defaultContent,
    geode::CopyableFunction<cocos2d::CCSprite*()> fallback
);

} // namespace Assets


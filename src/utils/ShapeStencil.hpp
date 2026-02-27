#pragma once
#include <cocos2d.h>
#include <string>

// Crea un nodo stencil con la forma indicada.
// Soporta formas geométricas (circle, triangle, hexagon, diamond, star, heart, pentagon, octagon)
// y también sprites Scale9 (cualquier nombre que termine en .png).
// El nodo resultante tiene el contentSize indicado y está centrado en su propio centro.
cocos2d::CCNode* createShapeStencil(const std::string& shapeName, float size);

// Crea un nodo con el BORDE/CONTORNO de la forma indicada (no relleno).
// Útil para dibujar marcos que siguen la misma forma del stencil.
cocos2d::CCNode* createShapeBorder(const std::string& shapeName, float size, float thickness, cocos2d::ccColor3B color, GLubyte opacity = 255);

// Lista de formas geométricas disponibles (no incluye sprites Scale9)
std::vector<std::pair<std::string, std::string>> getGeometricShapes();

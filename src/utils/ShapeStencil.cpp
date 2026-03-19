#include "ShapeStencil.hpp"
#include <cmath>

using namespace cocos2d;

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// poligono regular de N lados
static CCDrawNode* drawRegularPolygon(int sides, float radius, CCPoint center) {
    auto draw = CCDrawNode::create();
    std::vector<CCPoint> verts;
    verts.reserve(sides);
    for (int i = 0; i < sides; i++) {
        float angle = static_cast<float>(2.0 * M_PI * i / sides - M_PI / 2.0);
        verts.push_back(ccp(
            center.x + radius * cosf(angle),
            center.y + radius * sinf(angle)
        ));
    }
    draw->drawPolygon(verts.data(), sides, ccc4f(1, 1, 1, 1), 0, ccc4f(0, 0, 0, 0));
    return draw;
}

// estrella de N puntas
static CCDrawNode* drawStar(int points, float outerR, float innerR, CCPoint center) {
    auto draw = CCDrawNode::create();
    int totalVerts = points * 2;
    std::vector<CCPoint> verts;
    verts.reserve(totalVerts);
    for (int i = 0; i < totalVerts; i++) {
        float angle = static_cast<float>(2.0 * M_PI * i / totalVerts - M_PI / 2.0);
        float r = (i % 2 == 0) ? outerR : innerR;
        verts.push_back(ccp(
            center.x + r * cosf(angle),
            center.y + r * sinf(angle)
        ));
    }
    draw->drawPolygon(verts.data(), totalVerts, ccc4f(1, 1, 1, 1), 0, ccc4f(0, 0, 0, 0));
    return draw;
}

// corazon aproximado con poligono
static CCDrawNode* drawHeart(float size, CCPoint center) {
    auto draw = CCDrawNode::create();
    // corazon parametrico
    const int segments = 60;
    std::vector<CCPoint> verts;
    verts.reserve(segments);
    for (int i = 0; i < segments; i++) {
        float t = static_cast<float>(2.0 * M_PI * i / segments);
        // formula parametrica del corazon
        float x = 16.0f * powf(sinf(t), 3.0f);
        float y = 13.0f * cosf(t) - 5.0f * cosf(2*t) - 2.0f * cosf(3*t) - cosf(4*t);
        // normalizo y escalo
        float scale = size / 36.0f;
        verts.push_back(ccp(
            center.x + x * scale,
            center.y + y * scale
        ));
    }
    draw->drawPolygon(verts.data(), segments, ccc4f(1, 1, 1, 1), 0, ccc4f(0, 0, 0, 0));
    return draw;
}

// circulo con muchos segmentos
static CCDrawNode* drawCircle(float radius, CCPoint center) {
    return drawRegularPolygon(64, radius, center);
}

CCNode* createShapeStencil(std::string const& shapeName, float size) {
    float half = size / 2.f;
    CCPoint center = ccp(half, half);
    
    CCDrawNode* draw = nullptr;

    if (shapeName == "circle") {
        draw = drawCircle(half, center);
    }
    else if (shapeName == "square") {
        // cuadrado redondeado puro (sin depender de sprites GD)
        draw = CCDrawNode::create();
        CCPoint rect[4] = { ccp(0, 0), ccp(size, 0), ccp(size, size), ccp(0, size) };
        draw->drawPolygon(rect, 4, ccc4f(1, 1, 1, 1), 0, ccc4f(0, 0, 0, 0));
    }
    else if (shapeName == "rectangle") {
        draw = CCDrawNode::create();
        CCPoint rect[4] = { ccp(0, 0), ccp(size, 0), ccp(size, size), ccp(0, size) };
        draw->drawPolygon(rect, 4, ccc4f(1, 1, 1, 1), 0, ccc4f(0, 0, 0, 0));
    }
    else if (shapeName == "triangle") {
        draw = drawRegularPolygon(3, half, center);
    }
    else if (shapeName == "diamond") {
        draw = drawRegularPolygon(4, half, center);
    }
    else if (shapeName == "pentagon") {
        draw = drawRegularPolygon(5, half, center);
    }
    else if (shapeName == "hexagon") {
        draw = drawRegularPolygon(6, half, center);
    }
    else if (shapeName == "octagon") {
        draw = drawRegularPolygon(8, half, center);
    }
    else if (shapeName == "star") {
        draw = drawStar(5, half, half * 0.4f, center);
    }
    else if (shapeName == "star6") {
        draw = drawStar(6, half, half * 0.5f, center);
    }
    else if (shapeName == "heart") {
        draw = drawHeart(half, center);
    }
    
    if (draw) {
        draw->setContentSize({size, size});
        // no hace falta moverlo: los vertices ya salen centrados
        
        auto container = CCNode::create();
        container->setContentSize({size, size});
        container->addChild(draw);
        return container;
    }

    // fallback: intento con Scale9Sprite
    auto* tex = CCTextureCache::sharedTextureCache()->addImage(shapeName.c_str(), false);
    if (tex) {
        auto s9 = cocos2d::extension::CCScale9Sprite::create(shapeName.c_str());
        if (s9) {
            s9->setContentSize({size, size});
            s9->setAnchorPoint({0.5f, 0.5f});
            s9->setPosition(center);
            
            auto container = CCNode::create();
            container->setContentSize({size, size});
            container->addChild(s9);
            return container;
        }
    }

    return nullptr;
}

// borde

// vertices de poligono regular
static std::vector<CCPoint> getRegularPolygonVerts(int sides, float radius, CCPoint center) {
    std::vector<CCPoint> verts;
    verts.reserve(sides);
    for (int i = 0; i < sides; i++) {
        float angle = static_cast<float>(2.0 * M_PI * i / sides - M_PI / 2.0);
        verts.push_back(ccp(
            center.x + radius * cosf(angle),
            center.y + radius * sinf(angle)
        ));
    }
    return verts;
}

// vertices de estrella
static std::vector<CCPoint> getStarVerts(int points, float outerR, float innerR, CCPoint center) {
    int totalVerts = points * 2;
    std::vector<CCPoint> verts;
    verts.reserve(totalVerts);
    for (int i = 0; i < totalVerts; i++) {
        float angle = static_cast<float>(2.0 * M_PI * i / totalVerts - M_PI / 2.0);
        float r = (i % 2 == 0) ? outerR : innerR;
        verts.push_back(ccp(
            center.x + r * cosf(angle),
            center.y + r * sinf(angle)
        ));
    }
    return verts;
}

// vertices de corazon
static std::vector<CCPoint> getHeartVerts(float size, CCPoint center) {
    const int segments = 60;
    std::vector<CCPoint> verts;
    verts.reserve(segments);
    for (int i = 0; i < segments; i++) {
        float t = static_cast<float>(2.0 * M_PI * i / segments);
        float x = 16.0f * powf(sinf(t), 3.0f);
        float y = 13.0f * cosf(t) - 5.0f * cosf(2*t) - 2.0f * cosf(3*t) - cosf(4*t);
        float scale = size / 36.0f;
        verts.push_back(ccp(center.x + x * scale, center.y + y * scale));
    }
    return verts;
}

// vertices segun nombre
static std::vector<CCPoint> getShapeVerts(std::string const& shapeName, float half, CCPoint center) {
    if (shapeName == "circle") return getRegularPolygonVerts(64, half, center);
    if (shapeName == "square" || shapeName == "rectangle") {
        return { ccp(center.x - half, center.y - half), ccp(center.x + half, center.y - half),
                 ccp(center.x + half, center.y + half), ccp(center.x - half, center.y + half) };
    }
    if (shapeName == "triangle") return getRegularPolygonVerts(3, half, center);
    if (shapeName == "diamond") return getRegularPolygonVerts(4, half, center);
    if (shapeName == "pentagon") return getRegularPolygonVerts(5, half, center);
    if (shapeName == "hexagon") return getRegularPolygonVerts(6, half, center);
    if (shapeName == "octagon") return getRegularPolygonVerts(8, half, center);
    if (shapeName == "star") return getStarVerts(5, half, half * 0.4f, center);
    if (shapeName == "star6") return getStarVerts(6, half, half * 0.5f, center);
    if (shapeName == "heart") return getHeartVerts(half, center);
    return {};
}

CCNode* createShapeBorder(std::string const& shapeName, float size, float thickness, ccColor3B color, GLubyte opacity) {
    float half = size / 2.f;
    CCPoint center = ccp(half, half);
    
    auto verts = getShapeVerts(shapeName, half, center);
    
    if (!verts.empty()) {
        auto draw = CCDrawNode::create();
        ccColor4F borderColor = ccc4FFromccc4B(ccc4(color.r, color.g, color.b, opacity));
        
        // Dibujar segmentos del borde
        for (size_t i = 0; i < verts.size(); i++) {
            size_t next = (i + 1) % verts.size();
            draw->drawSegment(verts[i], verts[next], thickness / 2.f, borderColor);
        }
        
        draw->setContentSize({size, size});
        // NO setPosition: los vertices ya estan en coordenadas centradas en (half, half)
        
        auto container = CCNode::create();
        container->setContentSize({size, size});
        container->addChild(draw);
        return container;
    }
    
    // Fallback: Scale9Sprite para formas basadas en sprites
    auto* tex = CCTextureCache::sharedTextureCache()->addImage(shapeName.c_str(), false);
    if (tex) {
        auto s9 = cocos2d::extension::CCScale9Sprite::create(shapeName.c_str());
        if (s9) {
            s9->setContentSize({size, size});
            s9->setColor(color);
            s9->setOpacity(opacity);
            s9->setAnchorPoint({0.5f, 0.5f});
            s9->setPosition(ccp(size / 2, size / 2));
            
            auto container = CCNode::create();
            container->setContentSize({size, size});
            container->addChild(s9);
            return container;
        }
    }
    
    return nullptr;
}

std::vector<std::pair<std::string, std::string>> getGeometricShapes() {
    return {
        {"circle", "Circle"},
        {"square", "Rounded Square"},
        {"rectangle", "Square"},
        {"triangle", "Triangle"},
        {"diamond", "Diamond"},
        {"pentagon", "Pentagon"},
        {"hexagon", "Hexagon"},
        {"octagon", "Octagon"},
        {"star", "Star 5"},
        {"star6", "Star 6"},
        {"heart", "Heart"},
    };
}

#pragma once
#include <Geode/DefaultInclude.hpp>
#include <cocos2d.h>
#include <string>
#include <vector>
#include <unordered_map>

// Configuración de un frame/marco para la foto de perfil
struct PicFrameConfig {
    std::string spriteFrame = "";       // ya no se usa, el borde sigue la forma del stencil
    cocos2d::ccColor3B color = {255, 255, 255}; // blanco por defecto
    float opacity = 255.f;
    float thickness = 4.f;              // grosor del borde
    float offsetX = 0.f;
    float offsetY = 0.f;
};

// Configuración de una decoración (asset del juego colocado alrededor de la foto)
struct PicDecoration {
    std::string spriteName = "";        // nombre del sprite (ej: "star_small01_001.png")
    float posX = 0.f;                   // posición relativa al centro de la foto (-1 a 1)
    float posY = 0.f;
    float scale = 1.f;
    float rotation = 0.f;
    cocos2d::ccColor3B color = {255, 255, 255};
    float opacity = 255.f;
    bool flipX = false;
    bool flipY = false;
    int zOrder = 0;
};

// Configuración completa de personalización de la foto circular
struct ProfilePicConfig {
    // Tamaño y forma
    float scaleX = 1.f;                // escala horizontal (1 = normal)
    float scaleY = 1.f;                // escala vertical (1 = normal)
    float size = 120.f;                 // tamaño base en px

    // Marco
    bool frameEnabled = false;
    PicFrameConfig frame;

    // Forma del stencil
    std::string stencilSprite = "circle"; // forma geométrica por defecto
    
    // Decoraciones
    std::vector<PicDecoration> decorations;

    // Offset de posición
    float offsetX = 0.f;
    float offsetY = 0.f;
};

// Manager singleton que guarda/carga la configuración de la foto de perfil del usuario
class ProfilePicCustomizer {
public:
    static ProfilePicCustomizer& get();

    // Obtener/establecer configuración
    ProfilePicConfig getConfig() const;
    void setConfig(const ProfilePicConfig& config);

    // Guardar a disco
    void save();
    // Cargar de disco
    void load();
    
    // Flag para indicar que la config cambió y el ProfilePage debe re-renderizar
    bool isDirty() const { return m_dirty; }
    void setDirty(bool dirty) { m_dirty = dirty; }

    // Lista de frames disponibles
    static std::vector<std::pair<std::string, std::string>> getAvailableFrames();
    // Lista de stencils disponibles (formas)
    static std::vector<std::pair<std::string, std::string>> getAvailableStencils();
    // Lista de sprites decorativos disponibles (assets del juego)
    static std::vector<std::pair<std::string, std::string>> getAvailableDecorations();

private:
    ProfilePicCustomizer();
    ProfilePicConfig m_config;
    bool m_loaded = false;
    bool m_dirty = false;
};

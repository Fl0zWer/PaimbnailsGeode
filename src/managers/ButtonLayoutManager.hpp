#pragma once

#include <Geode/DefaultInclude.hpp>
#include <string>
#include <unordered_map>
#include <cocos2d.h>

// gestiona posiciones, escala, y opacidad personalizadas para botones del mod por escena e id.
// posiciones se guardan y cargan desde archivo texto en directorio guardado mod.

struct ButtonLayout {
    cocos2d::CCPoint position;
    float scale = 1.0f;
    float opacity = 1.0f; // 0.0 a 1.0
};

class ButtonLayoutManager {
public:
    static ButtonLayoutManager& get();

    // cargar disenos guardados desde archivo
    void load();
    // guardar disenos actuales a archivo
    void save();
    // cargar/guardar disenos default desde/a archivo
    void loadDefaults();
    void saveDefaults();

    // obtener diseno guardado para boton; retorna nullopt si no personalizado
    std::optional<ButtonLayout> getLayout(const std::string& sceneKey, const std::string& buttonID) const;

    // establecer diseno personalizado para boton
    void setLayout(const std::string& sceneKey, const std::string& buttonID, const ButtonLayout& layout);

    // eliminar diseno personalizado para boton (revertir a default)
    void removeLayout(const std::string& sceneKey, const std::string& buttonID);

    // verificar si escena+boton tiene diseno personalizado
    bool hasCustomLayout(const std::string& sceneKey, const std::string& buttonID) const;

    // resetear todos disenos para escena especifica
    void resetScene(const std::string& sceneKey);

    // api defaults: posiciones base persistentes independientes de ediciones usuario
    std::optional<ButtonLayout> getDefaultLayout(const std::string& sceneKey, const std::string& buttonID) const;
    // establecer default solo si ausente; evita sobreescribir una vez capturado
    void setDefaultLayoutIfAbsent(const std::string& sceneKey, const std::string& buttonID, const ButtonLayout& layout);
    // sobreescribir default para boton (usado para migraciones/ajustes)
    void setDefaultLayout(const std::string& sceneKey, const std::string& buttonID, const ButtonLayout& layout);

private:
    ButtonLayoutManager() = default;
    // mapa: sceneKey -> (buttonID -> ButtonLayout)
    std::unordered_map<std::string, std::unordered_map<std::string, ButtonLayout>> m_layouts;
    std::unordered_map<std::string, std::unordered_map<std::string, ButtonLayout>> m_defaults;
};


#pragma once
#include <Geode/Geode.hpp>
#include <cocos2d.h>
#include <vector>
#include "../managers/TransitionManager.hpp"

// ════════════════════════════════════════════════════════════
// CustomTransitionScene — escena que ejecuta una transicion
// personalizada definida por una secuencia de comandos.
// Mantiene ambas capas (origen y destino) simultaneamente
// y ejecuta los comandos secuencialmente.
// ════════════════════════════════════════════════════════════

class CustomTransitionScene : public cocos2d::CCScene {
public:
    static CustomTransitionScene* create(
        cocos2d::CCScene* fromScene,
        cocos2d::CCScene* destScene,
        std::vector<TransitionCommand> const& commands,
        bool isPush);

    bool initWithScenes(
        cocos2d::CCScene* fromScene,
        cocos2d::CCScene* destScene,
        std::vector<TransitionCommand> const& commands,
        bool isPush);

    void update(float dt) override;
    void onEnter() override;
    void onExit() override;
    ~CustomTransitionScene();

private:
    // Aplica el estado inicial de un comando
    void beginCommand(TransitionCommand const& cmd);

    // Actualiza el comando actual con progreso [0..1]
    void updateCommand(TransitionCommand const& cmd, float progress);

    // Finaliza un comando y avanza al siguiente
    void finishCurrentCommand();

    // Finaliza toda la transicion
    void finishTransition();

    // Callback de schedule para finalizar la transicion
    void onTransitionFinished(float dt);

    // Obtiene el nodo target de un comando ("from" o "to")
    cocos2d::CCLayerColor* getTarget(std::string const& targetName);

    // Nodos contenedores para las capas (CCLayerColor para soportar opacidad)
    cocos2d::CCLayerColor* m_fromContainer = nullptr;
    cocos2d::CCLayerColor* m_toContainer = nullptr;

    // Escena destino final (retenida)
    cocos2d::CCScene* m_destScene = nullptr;

    // Cola de comandos
    std::vector<TransitionCommand> m_commands;
    int m_currentCommandIdx = 0;
    float m_commandElapsed = 0.f;
    bool m_isPush = false;
    bool m_finished = false;
};

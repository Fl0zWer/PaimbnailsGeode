#include "CustomTransitionScene.hpp"

using namespace cocos2d;
using namespace geode::prelude;

// ════════════════════════════════════════════════════════════
// CustomTransitionScene — ejecuta transiciones DSL
// ════════════════════════════════════════════════════════════

CustomTransitionScene* CustomTransitionScene::create(
    CCScene* fromScene,
    CCScene* destScene,
    std::vector<TransitionCommand> const& commands,
    bool isPush)
{
    auto ret = new CustomTransitionScene();
    if (ret && ret->initWithScenes(fromScene, destScene, commands, isPush)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool CustomTransitionScene::initWithScenes(
    CCScene* fromScene,
    CCScene* destScene,
    std::vector<TransitionCommand> const& commands,
    bool isPush)
{
    if (!CCScene::init()) return false;

    m_commands = commands;
    m_isPush = isPush;
    m_destScene = destScene;
    m_destScene->retain();

    auto winSize = CCDirector::sharedDirector()->getWinSize();

    // ── Contenedor para la escena origen (CCLayerColor para soportar opacidad) ──
    m_fromContainer = CCLayerColor::create({255, 255, 255, 255}, winSize.width, winSize.height);
    m_fromContainer->setAnchorPoint({0.f, 0.f});
    m_fromContainer->setPosition({0, 0});
    this->addChild(m_fromContainer, 0);

    // Mover hijos de fromScene a nuestro contenedor
    if (fromScene) {
        CCArray* children = fromScene->getChildren();
        if (children) {
            auto copy = CCArray::createWithArray(children);
            for (auto* node : CCArrayExt<CCNode*>(copy)) {
                node->retain();
                node->removeFromParentAndCleanup(false);
                m_fromContainer->addChild(node, node->getZOrder());
                node->release();
            }
        }
    }

    // ── Contenedor para la escena destino (CCLayerColor para soportar opacidad) ──
    m_toContainer = CCLayerColor::create({255, 255, 255, 0}, winSize.width, winSize.height);
    m_toContainer->setAnchorPoint({0.f, 0.f});
    m_toContainer->setPosition({0, 0});
    this->addChild(m_toContainer, 1);

    // Mover hijos de destScene a nuestro contenedor
    if (destScene) {
        CCArray* children = destScene->getChildren();
        if (children) {
            auto copy = CCArray::createWithArray(children);
            for (auto* node : CCArrayExt<CCNode*>(copy)) {
                node->retain();
                node->removeFromParentAndCleanup(false);
                m_toContainer->addChild(node, node->getZOrder());
                node->release();
            }
        }
    }

    // Estado inicial: destino invisible
    m_toContainer->setVisible(true);
    m_toContainer->setOpacity(0);

    // Si no hay comandos, hacer fade simple de 0.3s
    if (m_commands.empty()) {
        m_commands.push_back({CommandAction::FadeOut, "from", 0.15f, 0,0,0,0, 255.f, 0.f});
        m_commands.push_back({CommandAction::FadeIn, "to", 0.15f, 0,0,0,0, 0.f, 255.f});
    }

    return true;
}

CustomTransitionScene::~CustomTransitionScene() {
    CC_SAFE_RELEASE_NULL(m_destScene);
}

void CustomTransitionScene::onEnter() {
    CCScene::onEnter();
    this->scheduleUpdate();

    // Comenzar primer comando
    if (m_currentCommandIdx < static_cast<int>(m_commands.size())) {
        beginCommand(m_commands[m_currentCommandIdx]);
    }
}

void CustomTransitionScene::onExit() {
    this->unscheduleUpdate();
    CCScene::onExit();
}

void CustomTransitionScene::update(float dt) {
    if (m_finished) return;
    if (m_currentCommandIdx >= static_cast<int>(m_commands.size())) {
        finishTransition();
        return;
    }

    auto& cmd = m_commands[m_currentCommandIdx];
    m_commandElapsed += dt;

    float duration = std::max(cmd.duration, 0.001f);
    float progress = std::min(m_commandElapsed / duration, 1.0f);

    updateCommand(cmd, progress);

    if (progress >= 1.0f) {
        finishCurrentCommand();
    }
}

void CustomTransitionScene::beginCommand(TransitionCommand const& cmd) {
    m_commandElapsed = 0.f;
    auto* target = getTarget(cmd.target);
    if (!target) return;

    switch (cmd.action) {
        case CommandAction::FadeOut:
            target->setOpacity(static_cast<GLubyte>(cmd.fromVal));
            break;
        case CommandAction::FadeIn:
            target->setVisible(true);
            target->setOpacity(static_cast<GLubyte>(cmd.fromVal));
            break;
        case CommandAction::Move:
            target->setPosition({cmd.fromX, cmd.fromY});
            break;
        case CommandAction::Scale:
            target->setScale(cmd.fromVal);
            break;
        case CommandAction::Rotate:
            target->setRotation(cmd.fromVal);
            break;
        case CommandAction::Color:
            target->setColor({
                static_cast<GLubyte>(cmd.r),
                static_cast<GLubyte>(cmd.g),
                static_cast<GLubyte>(cmd.b)
            });
            break;
        case CommandAction::Wait:
            break;
        case CommandAction::EaseIn:
        case CommandAction::EaseOut:
            target->setOpacity(static_cast<GLubyte>(cmd.fromVal));
            break;
    }
}

void CustomTransitionScene::updateCommand(TransitionCommand const& cmd, float progress) {
    auto* target = getTarget(cmd.target);
    if (!target && cmd.action != CommandAction::Wait) return;

    // easing
    float t = progress;
    if (cmd.action == CommandAction::EaseIn) {
        t = progress * progress;
    } else if (cmd.action == CommandAction::EaseOut) {
        t = 1.f - (1.f - progress) * (1.f - progress);
    }

    switch (cmd.action) {
        case CommandAction::FadeOut:
        case CommandAction::FadeIn:
        case CommandAction::EaseIn:
        case CommandAction::EaseOut: {
            float val = cmd.fromVal + (cmd.toVal - cmd.fromVal) * t;
            target->setOpacity(static_cast<GLubyte>(std::clamp(val, 0.f, 255.f)));
            break;
        }
        case CommandAction::Move: {
            float x = cmd.fromX + (cmd.toX - cmd.fromX) * t;
            float y = cmd.fromY + (cmd.toY - cmd.fromY) * t;
            target->setPosition({x, y});
            break;
        }
        case CommandAction::Scale: {
            float val = cmd.fromVal + (cmd.toVal - cmd.fromVal) * t;
            target->setScale(val);
            break;
        }
        case CommandAction::Rotate: {
            float val = cmd.fromVal + (cmd.toVal - cmd.fromVal) * t;
            target->setRotation(val);
            break;
        }
        case CommandAction::Color:
        case CommandAction::Wait:
            break;
    }
}

void CustomTransitionScene::finishCurrentCommand() {
    auto& cmd = m_commands[m_currentCommandIdx];
    updateCommand(cmd, 1.0f);

    m_currentCommandIdx++;
    m_commandElapsed = 0.f;

    if (m_currentCommandIdx < static_cast<int>(m_commands.size())) {
        beginCommand(m_commands[m_currentCommandIdx]);
    }
}

void CustomTransitionScene::finishTransition() {
    if (m_finished) return;
    m_finished = true;
    this->unscheduleUpdate();

    // Mover hijos del toContainer de vuelta a la destScene
    // Restaurar TODAS las propiedades que pudieron ser modificadas por los comandos
    // para que los nodos lleguen a la destScene en estado limpio.
    if (m_destScene && m_toContainer) {
        CCArray* children = m_toContainer->getChildren();
        if (children) {
            auto copy = CCArray::createWithArray(children);
            for (auto* node : CCArrayExt<CCNode*>(copy)) {
                node->retain();
                node->removeFromParentAndCleanup(false);
                // Restaurar transformaciones a estado neutro
                node->setScale(1.0f);
                node->setRotation(0.f);
                node->setPosition(node->getPosition()); // mantener posicion original
                m_destScene->addChild(node, node->getZOrder());
                node->release();
            }
        }
    }

    // Limpiar contenedores
    if (m_fromContainer) {
        m_fromContainer->removeAllChildrenWithCleanup(true);
    }
    if (m_toContainer) {
        m_toContainer->removeAllChildrenWithCleanup(true);
    }

    // Reemplazar esta escena por la destScene final en el siguiente frame.
    // Nota: usamos scheduleOnce para diferir al siguiente frame, evitando
    // que replaceScene se ejecute durante el update() del director.
    if (this->isRunning()) {
        this->scheduleOnce(schedule_selector(CustomTransitionScene::onTransitionFinished), 0.f);
    } else {
        // Fallback si la escena ya no esta en el arbol activo
        onTransitionFinished(0.f);
    }
}

void CustomTransitionScene::onTransitionFinished(float) {
    if (!m_destScene) return;

    auto director = CCDirector::sharedDirector();
    // replaceScene directamente — el hook de TransitionHook.cpp
    // detecta CustomTransitionScene y la deja pasar, pero m_destScene
    // NO es CustomTransitionScene, asi que se interceptaria de nuevo.
    // Sin embargo, m_destScene no esta envuelta en CCTransitionScene,
    // y el hook solo intercepta escenas envueltas → no hay reentrada.
    director->replaceScene(m_destScene);
}

CCLayerColor* CustomTransitionScene::getTarget(std::string const& targetName) {
    if (targetName == "to") return m_toContainer;
    return m_fromContainer;
}

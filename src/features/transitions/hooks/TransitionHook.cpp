// â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
// TransitionHook: intercepta las transiciones del juego
//
// Hookea CCDirector::replaceScene, pushScene y popSceneWithTransition
// para reemplazar las transiciones nativas con la que configure el usuario.
//
// ARQUITECTURA:
//   - Solo intercepta escenas que YA vienen envueltas en CCTransitionScene
//     (no afecta replaceScene directos sin transicion).
//   - Detecta PlayLayer para aplicar levelEntryConfig si esta configurada.
//   - Guard de reentrada robusto con RAII para evitar doble intercepcion
//     (especialmente desde CustomTransitionScene::onTransitionFinished).
// â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•

#include <Geode/Geode.hpp>
#include <Geode/modify/CCDirector.hpp>
#include "../services/TransitionManager.hpp"
#include "../ui/CustomTransitionScene.hpp"

using namespace geode::prelude;

// flag para evitar recursion infinita (protegido con RAII)
static bool s_applying = false;

// el juego no esta listo hasta que MenuLayer aparezca al menos una vez
static bool s_gameReady = false;

// RAII guard para s_applying
// garantiza que se restaure aunque haya un return temprano o una excepcion
struct ApplyingGuard {
    ApplyingGuard()  { s_applying = true; }
    ~ApplyingGuard() { s_applying = false; }
    ApplyingGuard(ApplyingGuard const&) = delete;
    ApplyingGuard& operator=(ApplyingGuard const&) = delete;
};

// extrae la escena destino real de una CCTransitionScene
static CCScene* unwrapTransition(CCTransitionScene* trans) {
    if (trans && trans->m_pInScene) return trans->m_pInScene;
    return nullptr;
}

static bool shouldIntercept() {
    if (s_applying) return false;
    if (!s_gameReady) return false;
    if (!TransitionManager::get().isEnabled()) return false;
    return true;
}

// â”€â”€ Detecta si la escena destino contiene un PlayLayer â”€â”€â”€â”€â”€â”€
// Esto permite aplicar levelEntryConfig cuando el usuario navega hacia un nivel.
static bool destContainsPlayLayer(CCScene* scene) {
    return scene && scene->getChildByType<PlayLayer>(0);
}

// â”€â”€ Selecciona la configuracion de transicion apropiada â”€â”€â”€â”€â”€
// Si la escena destino contiene PlayLayer y hay una config de nivel configurada,
// usa esa; de lo contrario usa la global.
static TransitionConfig selectConfig(CCScene* destScene) {
    auto& tm = TransitionManager::get();
    if (tm.hasLevelEntryConfig() && destContainsPlayLayer(destScene)) {
        return tm.getLevelEntryConfig();
    }
    return tm.getGlobalConfig();
}

class $modify(PaimonDirector, CCDirector) {

    // â”€â”€ replaceScene â”€â”€
    bool replaceScene(CCScene* scene) {
        if (!scene) return CCDirector::replaceScene(scene);

        // Detectar cuando MenuLayer aparece â†’ juego listo
        if (!s_gameReady) {
            bool foundMenu = false;
            if (scene->getChildByType<MenuLayer>(0)) foundMenu = true;
            if (!foundMenu) {
                if (auto* trans = typeinfo_cast<CCTransitionScene*>(scene)) {
                    if (trans->m_pInScene && trans->m_pInScene->getChildByType<MenuLayer>(0))
                        foundMenu = true;
                }
            }
            if (foundMenu) {
                s_gameReady = true;
            }
            // Dejar pasar la primera transicion a MenuLayer sin interceptar
            return CCDirector::replaceScene(scene);
        }

        if (!shouldIntercept()) return CCDirector::replaceScene(scene);

        // No re-interceptar nuestras propias CustomTransitionScene
        if (typeinfo_cast<CustomTransitionScene*>(scene)) return CCDirector::replaceScene(scene);

        // Solo interceptar si viene envuelta en CCTransitionScene
        auto* nativeTrans = typeinfo_cast<CCTransitionScene*>(scene);
        if (!nativeTrans) return CCDirector::replaceScene(scene);

        CCScene* realDest = unwrapTransition(nativeTrans);
        if (!realDest) return CCDirector::replaceScene(scene);

        // Ref<> gestiona el refcount automaticamente â€” prohibido retain/release directo
        Ref<CCScene> safeDest = realDest;
        auto cfg = selectConfig(realDest);
        ApplyingGuard guard;
        auto* ourTrans = TransitionManager::get().createTransition(cfg, realDest);
        return CCDirector::replaceScene(ourTrans ? ourTrans : realDest);
    }

    // â”€â”€ pushScene â”€â”€
    bool pushScene(CCScene* scene) {
        if (!scene || !shouldIntercept()) return CCDirector::pushScene(scene);

        // No re-interceptar nuestras propias CustomTransitionScene
        if (typeinfo_cast<CustomTransitionScene*>(scene)) return CCDirector::pushScene(scene);

        auto* nativeTrans = typeinfo_cast<CCTransitionScene*>(scene);
        if (!nativeTrans) return CCDirector::pushScene(scene);

        CCScene* realDest = unwrapTransition(nativeTrans);
        if (!realDest) return CCDirector::pushScene(scene);

        // Ref<> gestiona el refcount automaticamente â€” prohibido retain/release directo
        Ref<CCScene> safeDest = realDest;
        auto cfg = selectConfig(realDest);
        ApplyingGuard guard;
        auto* ourTrans = TransitionManager::get().createTransition(cfg, realDest);
        return CCDirector::pushScene(ourTrans ? ourTrans : realDest);
    }

    // â”€â”€ popSceneWithTransition (cubre la mayoria de "back" en GD) â”€â”€
    bool popSceneWithTransition(float duration, PopTransition type) {
        if (!shouldIntercept()) {
            return CCDirector::popSceneWithTransition(duration, type);
        }

        // El popSceneWithTransition internamente hace pop y crea una transicion.
        // Necesitamos interceptar: hacer el pop, pero aplicar NUESTRA transicion.
        auto& stack = m_pobScenesStack;
        if (!stack || stack->count() < 2) {
            return CCDirector::popSceneWithTransition(duration, type);
        }

        // La escena destino es la penultima del stack
        auto* destScene = typeinfo_cast<CCScene*>(stack->objectAtIndex(stack->count() - 2));
        if (!destScene) {
            return CCDirector::popSceneWithTransition(duration, type);
        }

        // Ref<> en vez de retain/release manual pa seguridad de memoria
        Ref<CCScene> safeDest = destScene;

        auto cfg = selectConfig(destScene);

        ApplyingGuard guard;
        // Hacer pop normal (sin transicion visual nuestra)
        // con s_applying activo, replaceScene dejara pasar sin interceptar
        CCDirector::popScene();

        // Ahora reemplazar con nuestra transicion
        auto* ourTrans = TransitionManager::get().createTransition(cfg, destScene);
        CCDirector::replaceScene(ourTrans ? ourTrans : destScene);

        return true;
    }

    // â”€â”€ popScene (sin transicion nativa) â”€â”€
    void popScene() {
        if (!shouldIntercept()) {
            CCDirector::popScene();
            return;
        }

        auto& stack = m_pobScenesStack;
        if (!stack || stack->count() < 2) {
            CCDirector::popScene();
            return;
        }

        auto* destScene = typeinfo_cast<CCScene*>(stack->objectAtIndex(stack->count() - 2));
        if (!destScene) {
            CCDirector::popScene();
            return;
        }

        // Ref<> en vez de retain/release manual pa seguridad de memoria
        Ref<CCScene> safeDest = destScene;

        auto cfg = selectConfig(destScene);

        ApplyingGuard guard;
        CCDirector::popScene();
        auto* ourTrans = TransitionManager::get().createTransition(cfg, destScene);
        CCDirector::replaceScene(ourTrans ? ourTrans : destScene);
    }
};

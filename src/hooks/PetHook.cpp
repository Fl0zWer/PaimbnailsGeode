#include <Geode/Geode.hpp>
#include <Geode/modify/CCScheduler.hpp>
#include "../managers/PetManager.hpp"

using namespace geode::prelude;
using namespace cocos2d;

class $modify(PetSchedulerHook, CCScheduler) {
    static void onModify(auto& self) {
        // VeryLate = corre despues de otros mods que hookeen el scheduler
        // la logica del pet es puramente visual y no debe interferir con otros hooks
        (void)self.setHookPriorityPost("cocos2d::CCScheduler::update", geode::Priority::VeryLate);
    }

    void update(float dt) {
        CCScheduler::update(dt);

        auto& pet = PetManager::get();

        // la animacion del pet necesita correr cada frame si esta attached
        if (pet.isAttached()) {
            pet.update(dt);
        }

        // los chequeos de config/scene/visibility son costosos a 60fps
        // throttle: solo cada ~6 frames (unos 100ms a 60fps)
        static int s_petFrameCounter = 0;
        if (++s_petFrameCounter % 6 != 0) return;

        if (!pet.config().enabled) {
            if (pet.isAttached()) pet.detachFromScene();
            return;
        }

        auto scene = CCDirector::sharedDirector()->getRunningScene();
        if (!scene) return;

        if (!pet.shouldShowOnCurrentScene()) {
            if (pet.isAttached()) pet.detachFromScene();
            return;
        }

        if (!pet.isAttached()) {
            pet.attachToScene(scene);
        }
    }
};

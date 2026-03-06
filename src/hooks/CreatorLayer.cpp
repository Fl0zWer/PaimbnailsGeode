#include <Geode/modify/CreatorLayer.hpp>
#include "../managers/LayerBackgroundManager.hpp"

using namespace geode::prelude;

class $modify(PaimonCreatorLayer, CreatorLayer) {
    static void onModify(auto& self) {
        (void)self.setHookPriorityPost("CreatorLayer::init", geode::Priority::Late);
    }

    bool init() {
        if (!CreatorLayer::init()) return false;
        LayerBackgroundManager::get().applyBackground(this, "creator");
        return true;
    }
};

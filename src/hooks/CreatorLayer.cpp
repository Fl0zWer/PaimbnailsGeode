#include <Geode/modify/CreatorLayer.hpp>
#include "../features/backgrounds/services/LayerBackgroundManager.hpp"

using namespace geode::prelude;

class $modify(PaimonCreatorLayer, CreatorLayer) {
    static void onModify(auto& self) {
        (void)self.setHookPriorityPost("CreatorLayer::init", geode::Priority::Late);
    }

    $override
    bool init() {
        if (!CreatorLayer::init()) return false;
        LayerBackgroundManager::get().applyBackground(this, "creator");
        return true;
    }
};

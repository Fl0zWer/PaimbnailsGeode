#include <Geode/Geode.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include "../layers/PaimonSupportLayer.hpp"
#include "../features/transitions/services/TransitionManager.hpp"

using namespace geode::prelude;

// nodo auxiliar pa manejar el click del boton support
class SupportButtonHandler : public CCNode {
public:
    static SupportButtonHandler* create() {
        auto ret = new SupportButtonHandler();
        if (ret && ret->init()) {
            ret->autorelease();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }

    void onSupportClicked(CCObject*) {
        TransitionManager::get().pushScene(
            PaimonSupportLayer::scene()
        );
    }
};

$execute {
    // cuando se abre el popup del mod, cambiamos el boton support por el nuestro
    static auto handle = ModPopupUIEvent().listen(
        +[](FLAlertLayer* popup, std::string_view modID, std::optional<Mod*>) -> ListenerResult {
            if (modID != Mod::get()->getID()) {
                return ListenerResult::Propagate;
            }

            // buscar el boton en links-container
            auto linksMenu = popup->querySelector("links-container");
            if (!linksMenu) return ListenerResult::Propagate;

            auto supportBtn = linksMenu->getChildByID("support");
            if (!supportBtn) return ListenerResult::Propagate;

            auto menuItem = typeinfo_cast<CCMenuItemSpriteExtra*>(supportBtn);
            if (!menuItem) return ListenerResult::Propagate;

            // si ya lo cambiamos antes, no duplicar
            if (menuItem->getChildByID("support-handler"_spr)) {
                return ListenerResult::Propagate;
            }

            // el handler vive como hijo del boton
            auto handler = SupportButtonHandler::create();
            handler->setID("support-handler"_spr);
            menuItem->addChild(handler);

            // redirigir a nuestro handler
            menuItem->setTarget(handler, menu_selector(SupportButtonHandler::onSupportClicked));
            menuItem->setEnabled(true);

            return ListenerResult::Propagate;
        }
    );
}

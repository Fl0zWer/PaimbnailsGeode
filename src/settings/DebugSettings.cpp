#include <Geode/Geode.hpp>
#include "../utils/Debug.hpp"

using namespace geode::prelude;

$execute {
    // inicializar estado predeterminado
    bool initial = Mod::get()->getSettingValue<bool>("enable-debug-logs");
    PaimonDebug::setEnabled(initial);

    geode::listenForSettingChanges<bool>("enable-debug-logs", +[](bool value) {
        if (value) {
            auto password = Mod::get()->getSettingValue<std::string>("debug-password");
            if (password == "Paimon285") {
                PaimonDebug::setEnabled(true);
                log::info("Paimbnails Debug Logs Enabled");
                Notification::create("Debug Logs Enabled", NotificationIcon::Success)->show();
            } else {
                Notification::create("Incorrect Password", NotificationIcon::Error)->show();
                PaimonDebug::setEnabled(false);
                
                // Revert the setting to false
                Loader::get()->queueInMainThread([]{
                    Mod::get()->setSettingValue("enable-debug-logs", false);
                });
            }
        } else {
            PaimonDebug::setEnabled(false);
        }
    });
}

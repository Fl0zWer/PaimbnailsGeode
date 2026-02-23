#include "../MSVCFix.hpp"
#include <Geode/Geode.hpp>
#include "../utils/Debug.hpp"

using namespace geode::prelude;

$execute {
    // aplicar optimizer lo antes posible (desactiva todos los logs de Geode)
    bool optimizerEnabled = true;
    try {
        optimizerEnabled = Mod::get()->getSettingValue<bool>("optimizer");
    } catch (...) {}

    if (optimizerEnabled) {
        Mod::get()->setLoggingEnabled(false);
    }

    geode::listenForSettingChanges<bool>("optimizer", +[](bool value) {
        if (value) {
            Mod::get()->setLoggingEnabled(false);
        } else {
            Mod::get()->setLoggingEnabled(true);
        }
    });

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

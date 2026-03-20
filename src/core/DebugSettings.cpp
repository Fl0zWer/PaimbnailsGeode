#include <Geode/Geode.hpp>
#include "../utils/PaimonNotification.hpp"
#include "../utils/Debug.hpp"

using namespace geode::prelude;

namespace {
bool canEnableDebugLogs() {
    // Evita secretos hardcodeados en binario: el token se inyecta por launch arg.
    // Ejemplo: --geode:flozwer.paimbnails.debug-token=mi_token
    auto expectedToken = Mod::get()->getLaunchArgument("debug-token");
    if (!expectedToken.has_value() || expectedToken->empty()) {
        return false;
    }

    auto enteredToken = Mod::get()->getSettingValue<std::string>("debug-password");
    return enteredToken == *expectedToken;
}
} // namespace

$execute {
    // aplicar optimizer lo antes posible (desactiva todos los logs de Geode)
    bool optimizerEnabled = Mod::get()->getSettingValue<bool>("optimizer");

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
            if (canEnableDebugLogs()) {
                PaimonDebug::setEnabled(true);
                log::info("Paimbnails Debug Logs Enabled");
                PaimonNotify::create("Debug Logs Enabled", NotificationIcon::Success)->show();
            } else {
                PaimonNotify::create("Debug token missing/invalid", NotificationIcon::Error)->show();
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

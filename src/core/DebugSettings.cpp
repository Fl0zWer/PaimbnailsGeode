#include <Geode/Geode.hpp>
#include "../utils/Debug.hpp"

using namespace geode::prelude;

$execute {
    bool initial = Mod::get()->getSettingValue<bool>("enable-debug-logs");
    Mod::get()->setLoggingEnabled(initial);
    PaimonDebug::setEnabled(initial);

    geode::listenForSettingChanges<bool>("enable-debug-logs", +[](bool value) {
        Mod::get()->setLoggingEnabled(value);
        PaimonDebug::setEnabled(value);
    });
}

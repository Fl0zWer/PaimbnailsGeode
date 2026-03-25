#include <Geode/modify/CCKeyboardDispatcher.hpp>
#include <Geode/Geode.hpp>
#include "../features/settings-panel/services/SettingsPanelManager.hpp"

using namespace geode::prelude;
using namespace cocos2d;

namespace {

enumKeyCodes keyFromString(std::string const& key) {
    if (key == "A") return enumKeyCodes::KEY_A;
    if (key == "B") return enumKeyCodes::KEY_B;
    if (key == "C") return enumKeyCodes::KEY_C;
    if (key == "D") return enumKeyCodes::KEY_D;
    if (key == "E") return enumKeyCodes::KEY_E;
    if (key == "F") return enumKeyCodes::KEY_F;
    if (key == "G") return enumKeyCodes::KEY_G;
    if (key == "H") return enumKeyCodes::KEY_H;
    if (key == "I") return enumKeyCodes::KEY_I;
    if (key == "J") return enumKeyCodes::KEY_J;
    if (key == "K") return enumKeyCodes::KEY_K;
    if (key == "L") return enumKeyCodes::KEY_L;
    if (key == "M") return enumKeyCodes::KEY_M;
    if (key == "N") return enumKeyCodes::KEY_N;
    if (key == "O") return enumKeyCodes::KEY_O;
    if (key == "P") return enumKeyCodes::KEY_P;
    if (key == "Q") return enumKeyCodes::KEY_Q;
    if (key == "R") return enumKeyCodes::KEY_R;
    if (key == "S") return enumKeyCodes::KEY_S;
    if (key == "T") return enumKeyCodes::KEY_T;
    if (key == "U") return enumKeyCodes::KEY_U;
    if (key == "V") return enumKeyCodes::KEY_V;
    if (key == "W") return enumKeyCodes::KEY_W;
    if (key == "X") return enumKeyCodes::KEY_X;
    if (key == "Y") return enumKeyCodes::KEY_Y;
    if (key == "Z") return enumKeyCodes::KEY_Z;
    if (key == "F1") return enumKeyCodes::KEY_F1;
    if (key == "F2") return enumKeyCodes::KEY_F2;
    if (key == "F3") return enumKeyCodes::KEY_F3;
    if (key == "F4") return enumKeyCodes::KEY_F4;
    if (key == "F5") return enumKeyCodes::KEY_F5;
    if (key == "F6") return enumKeyCodes::KEY_F6;
    if (key == "F7") return enumKeyCodes::KEY_F7;
    if (key == "F8") return enumKeyCodes::KEY_F8;
    if (key == "F9") return enumKeyCodes::KEY_F9;
    if (key == "F10") return enumKeyCodes::KEY_F10;
    if (key == "F11") return enumKeyCodes::KEY_F11;
    if (key == "F12") return enumKeyCodes::KEY_F12;
    return enumKeyCodes::KEY_P; // default
}

bool checkModifier(CCKeyboardDispatcher* disp, std::string const& mod) {
    if (mod == "ctrl")  return disp->getControlKeyPressed();
    if (mod == "shift") return disp->getShiftKeyPressed();
    if (mod == "alt")   return disp->getAltKeyPressed();
    if (mod == "none")  return !disp->getControlKeyPressed() &&
                                !disp->getShiftKeyPressed() &&
                                !disp->getAltKeyPressed();
    return disp->getControlKeyPressed(); // default: ctrl
}

} // namespace

class $modify(PaimonKeyboardHook, CCKeyboardDispatcher) {
    bool dispatchKeyboardMSG(enumKeyCodes key, bool down, bool repeat, double timestamp) {
        if (down && !repeat) {
            // configurable keybind from saved values
            auto keyStr = Mod::get()->getSavedValue<std::string>("settings-panel-key", "P");
            auto modStr = Mod::get()->getSavedValue<std::string>("settings-panel-modifier", "ctrl");

            if (key == keyFromString(keyStr) && checkModifier(this, modStr)) {
                SettingsPanelManager::get().toggle();
                return true;
            }

            // Escape — close panel if open
            if (key == enumKeyCodes::KEY_Escape && SettingsPanelManager::get().isOpen()) {
                SettingsPanelManager::get().close();
                return true;
            }
        }
        return CCKeyboardDispatcher::dispatchKeyboardMSG(key, down, repeat, timestamp);
    }
};

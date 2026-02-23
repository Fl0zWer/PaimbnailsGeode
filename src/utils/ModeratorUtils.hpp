#pragma once

#include <Geode/loader/Mod.hpp>
#include <filesystem>
#include <fstream>
#include <chrono>

namespace PaimonUtils {

    // verifica si el usuario es moderador verificado
    // lee moderator_verification.dat y comprueba que no hayan pasado mas de 30 dias
    inline bool isUserModerator() {
        try {
            auto modDataPath = geode::Mod::get()->getSaveDir() / "moderator_verification.dat";
            if (std::filesystem::exists(modDataPath)) {
                std::ifstream modFile(modDataPath, std::ios::binary);
                if (modFile) {
                    time_t timestamp{};
                    modFile.read(reinterpret_cast<char*>(&timestamp), sizeof(timestamp));
                    modFile.close();
                    auto now = std::chrono::system_clock::now();
                    auto fileTime = std::chrono::system_clock::from_time_t(timestamp);
                    auto daysDiff = std::chrono::duration_cast<std::chrono::hours>(now - fileTime).count() / 24;
                    if (daysDiff < 30) {
                        return true;
                    }
                }
            }
            return geode::Mod::get()->getSavedValue<bool>("is-verified-moderator", false);
        } catch (...) {
            return false;
        }
    }

} // namespace PaimonUtils


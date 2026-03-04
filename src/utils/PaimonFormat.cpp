#include "PaimonFormat.hpp"
#include <fstream>
#include <Geode/loader/Log.hpp>
#include <Geode/utils/string.hpp>

using namespace geode::prelude;

namespace PaimonFormat {
    bool save(std::filesystem::path const& path, std::vector<uint8_t> const& data) {
        std::error_code ec;
        if (!std::filesystem::exists(path.parent_path(), ec)) {
            std::filesystem::create_directories(path.parent_path(), ec);
            if (ec) {
                log::error("[PaimonFormat] Failed to create directories: {}", ec.message());
                return false;
            }
        }

        auto encrypted = encrypt(data);

        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        if (!file) {
            log::error("[PaimonFormat] Failed to open file for writing: {}", geode::utils::string::pathToString(path));
            return false;
        }

        char const magic[] = "PAIMON";
        file.write(magic, 6);
        uint8_t version = 2;
        file.write(reinterpret_cast<char const*>(&version), 1);

        uint32_t size = static_cast<uint32_t>(encrypted.size());
        file.write(reinterpret_cast<char const*>(&size), 4);

        file.write(reinterpret_cast<char const*>(encrypted.data()), encrypted.size());

        uint64_t hash = calculateHash(encrypted);
        file.write(reinterpret_cast<char const*>(&hash), 8);

        file.close();
        if (!file) {
            log::error("[PaimonFormat] Failed to write file data");
            return false;
        }
        return true;
    }
    
    std::vector<uint8_t> load(std::filesystem::path const& path) {
        std::error_code ec;
        if (!std::filesystem::exists(path, ec)) {
            return {};
        }

        std::ifstream file(path, std::ios::binary);
        if (!file) {
            log::error("[PaimonFormat] Failed to open file for reading: {}", geode::utils::string::pathToString(path));
            return {};
        }

        char magic[6];
        file.read(magic, 6);
        if (!file || std::string(magic, 6) != "PAIMON") {
            log::error("[PaimonFormat] Invalid file format (bad magic header)");
            return {};
        }

        uint8_t version;
        file.read(reinterpret_cast<char*>(&version), 1);
        if (version > 2) {
            log::warn("[PaimonFormat] Unsupported future file version: {}", version);
            return {};
        }

        uint32_t size;
        file.read(reinterpret_cast<char*>(&size), 4);

        if (size == 0 || size > 10 * 1024 * 1024) {
            log::error("[PaimonFormat] Invalid data size: {}", size);
            return {};
        }

        std::vector<uint8_t> encrypted(size);
        file.read(reinterpret_cast<char*>(encrypted.data()), size);

        if (!file) {
            log::error("[PaimonFormat] Failed to read encrypted data");
            return {};
        }

        if (version >= 2) {
            uint64_t storedHash;
            file.read(reinterpret_cast<char*>(&storedHash), 8);

            if (file.gcount() == 8) {
                uint64_t calculatedHash = calculateHash(encrypted);
                if (storedHash != calculatedHash) {
                    log::error("[PaimonFormat] Integrity check failed: file was modified or corrupted.");
                    return {};
                }
            } else {
                log::warn("[PaimonFormat] Incomplete v2 file (missing hash)");
                return {};
            }
        }

        file.close();

        return decrypt(encrypted);
    }
}


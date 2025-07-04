//
// Created by Jean-Michel Frouin on 04/07/2025.
//

#include "ConfigManager.h"
#include <fstream>
#include <iostream>

namespace Utils {

    CConfigManager::CConfigManager() {
        mConfig = nlohmann::json::object();
    }

    CConfigManager& CConfigManager::GetInstance() {
        static CConfigManager instance;
        return instance;
    }

    bool CConfigManager::LoadFromFile(const std::string& filename) {
        try {
            std::ifstream file(filename);
            if (!file.is_open()) {
                std::cerr << "Failed to open mConfig file: " << filename << std::endl;
                return false;
            }

            nlohmann::json loadedConfig;
            file >> loadedConfig;

            std::lock_guard<std::mutex> guard(mConfigMutex);
            mConfig = loadedConfig;

            return true;
        } catch (const std::exception& e) {
            std::cerr << "Error loading mConfig file: " << e.what() << std::endl;
            return false;
        }
    }

    bool CConfigManager::SaveToFile(const std::string& filename) const {
        try {
            std::ofstream file(filename);
            if (!file.is_open()) {
                std::cerr << "Failed to open mConfig file for writing: " << filename << std::endl;
                return false;
            }

            std::lock_guard<std::mutex> guard(mConfigMutex);
            file << mConfig.dump(4) << std::endl;

            return true;
        } catch (const std::exception& e) {
            std::cerr << "Error saving mConfig file: " << e.what() << std::endl;
            return false;
        }
    }

} // namespace Utils
//
// Created by Jean-Michel Frouin on 04/07/2025.
//

#ifndef CONFIGMANAGER_H
#define CONFIGMANAGER_H

#include <string>
#include <nlohmann/json.hpp>
#include <mutex>

namespace Utils {

    class CConfigManager {
    public:
        static CConfigManager& GetInstance();

        // Configuration loading/saving
        bool LoadFromFile(const std::string& filename);
        bool SaveToFile(const std::string& filename) const;

        // Get mConfiguration values
        template<typename T>
        T GetValue(const std::string& key, const T& defaultValue = T()) const {
            std::lock_guard<std::mutex> guard(mConfigMutex);
            try {
                return mConfig.at(key).get<T>();
            } catch (...) {
                return defaultValue;
            }
        }

        // Get nested mConfiguration values
        template<typename T>
        T GetValue(const std::string& section, const std::string& key, const T& defaultValue = T()) const {
            std::lock_guard<std::mutex> guard(mConfigMutex);
            try {
                return mConfig.at(section).at(key).get<T>();
            } catch (...) {
                return defaultValue;
            }
        }

        // Set mConfiguration values
        template<typename T>
        void SetValue(const std::string& key, const T& value) {
            std::lock_guard<std::mutex> guard(mConfigMutex);
            mConfig[key] = value;
        }

        template<typename T>
        void SetValue(const std::string& section, const std::string& key, const T& value) {
            std::lock_guard<std::mutex> guard(mConfigMutex);
            mConfig[section][key] = value;
        }

        nlohmann::json GetConfig() const {
            std::lock_guard<std::mutex> guard(mConfigMutex);
            return mConfig;
        }

        void SetConfig(const nlohmann::json& newConfig) {
            std::lock_guard<std::mutex> guard(mConfigMutex);
            mConfig = newConfig;
        }

    private:
        CConfigManager();
        CConfigManager(const CConfigManager&) = delete;
        CConfigManager& operator=(const CConfigManager&) = delete;

        nlohmann::json mConfig;
        mutable std::mutex mConfigMutex;
    };

} // namespace Utils

#endif //CONFIGMANAGER_H

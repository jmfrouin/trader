//
// Created by Jean-Michel Frouin on 02/07/2025.
//

#ifndef LOGGER_H
#define LOGGER_H

#include <string>
#include <fstream>
#include <mutex>
#include <chrono>
#include <map>
#include <vector>
#include <memory>

namespace Utils {

    enum class ELogLevel {
        DEBUG,
        INFO,
        WARNING,
        ERROR,
        TRADE
    };

    struct SLogEntry {
        std::chrono::system_clock::time_point mTimestamp;
        ELogLevel mLevel;
        std::string mMessage;
        std::map<std::string, std::string> mMetadata;
    };

    class ILogHandler {
        public:
            virtual ~ILogHandler() = default;
            virtual void Write(const SLogEntry& entry) = 0;
    };

    class CFileLogHandler : public ILogHandler {
        public:
            CFileLogHandler(const std::string& filename);
            ~CFileLogHandler() override;
            void Write(const SLogEntry& entry) override;

        private:
            std::ofstream mFile;
            std::mutex mFileMutex;
    };

    class CConsoleLogHandler : public ILogHandler {
        public:
            CConsoleLogHandler() = default;
            ~CConsoleLogHandler() override = default;
            void Write(const SLogEntry& entry) override;

        private:
            std::mutex mConsoleMutex;
    };

    class CLogger {
    public:
        static CLogger& getInstance();

        // Configuration
        void SetLogLevel(ELogLevel level);
        void AddHandler(std::shared_ptr<ILogHandler> handler);
        void RemoveAllHandlers();

        // Logging methods
        void Debug(const std::string& message, const std::map<std::string, std::string>& metadata = {});
        void Info(const std::string& message, const std::map<std::string, std::string>& metadata = {});
        void Warning(const std::string& message, const std::map<std::string, std::string>& metadata = {});
        void Error(const std::string& message, const std::map<std::string, std::string>& metadata = {});
        void Trade(const std::string& message, const std::map<std::string, std::string>& metadata = {});

        // Utility
        std::string FormatTimestamp(const std::chrono::system_clock::time_point& timestamp) const;
        std::string LevelToString(ELogLevel level) const;

    private:
        CLogger();
        CLogger(const CLogger&) = delete;
        CLogger& operator=(const CLogger&) = delete;

        void Log(ELogLevel level, const std::string& message, const std::map<std::string, std::string>& metadata);

        ELogLevel mMinLevel;
        std::vector<std::shared_ptr<ILogHandler>> mHandlers;
        std::mutex mHandlersMutex;
    };

} // Utils

#endif //LOGGER_H

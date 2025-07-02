//
// Created by Jean-Michel Frouin on 02/07/2025.
//

#include "logger.h"

#include "utils/Logger.h"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <ctime>

namespace Utils {

// FileLogHandler implementation
CFileLogHandler::CFileLogHandler(const std::string& filename) {
    mFile.open(filename, std::ios::app);
    if (!mFile.is_open()) {
        throw std::runtime_error("Failed to open log file: " + filename);
    }
}

CFileLogHandler::~CFileLogHandler() {
    if (mFile.is_open()) {
        mFile.close();
    }
}

void CFileLogHandler::write(const SLogEntry& entry) {
    if (!mFile.is_open()) {
        return;
    }

    std::lock_guard<std::mutex> guard(mFileMutex);

    // Get logger instance for formatting
    auto& logger = CLogger::getInstance();

    // Format the entry
    mFile << logger.FormatTimestamp(entry.mTimestamp) << " "
         << "[" << logger.LevelToString(entry.mLevel) << "] "
         << entry.mMessage;

    // Add metadata if present
    if (!entry.mMetadata.empty()) {
        mFile << " {";
        bool first = true;
        for (const auto& pair : entry.mMetadata) {
            if (!first) {
                mFile << ", ";
            }
            mFile << pair.first << ": " << pair.second;
            first = false;
        }
        mFile << "}";
    }

    mFile << std::endl;
    mFile.flush();
}

// ConsoleLogHandler implementation
void CConsoleLogHandler::Write(const SLogEntry& entry) {
    std::lock_guard<std::mutex> guard(mConsoleMutex);

    // Get logger instance for formatting
    auto& logger = CLogger::getInstance();

    // Set color based on log level
    std::string colorCode;
    switch (entry.mLevel) {
        case ELogLevel::DEBUG:
            colorCode = "\033[37m"; // White
            break;
        case ELogLevel::INFO:
            colorCode = "\033[32m"; // Green
            break;
        case ELogLevel::WARNING:
            colorCode = "\033[33m"; // Yellow
            break;
        case ELogLevel::ERROR:
            colorCode = "\033[31m"; // Red
            break;
        case ELogLevel::TRADE:
            colorCode = "\033[36m"; // Cyan
            break;
    }

    // Format the entry
    std::cout << colorCode
              << logger.FormatTimestamp(entry.mTimestamp) << " "
              << "[" << logger.LevelToString(entry.mLevel) << "] "
              << entry.mMessage;

    // Add metadata if present
    if (!entry.mMetadata.empty()) {
        std::cout << " {";
        bool first = true;
        for (const auto& pair : entry.mMetadata) {
            if (!first) {
                std::cout << ", ";
            }
            std::cout << pair.first << ": " << pair.second;
            first = false;
        }
        std::cout << "}";
    }

    // Reset color
    std::cout << "\033[0m" << std::endl;
}

// Logger implementation
CLogger::CLogger() : mMinLevel(ELogLevel::INFO) {
    // Add a default console handler
    AddHandler(std::make_shared<CConsoleLogHandler>());
}

CLogger& CLogger::getInstance() {
    static CLogger instance;
    return instance;
}

void CLogger::SetLogLevel(CLogger level) {
    mMinLevel = level;
}

void CLogger::AddHandler(std::shared_ptr<ILogHandler> handler) {
    if (!handler) {
        return;
    }

    std::lock_guard<std::mutex> guard(mHandlersMutex);
    mHandlers.push_back(handler);
}

void CLogger::RemoveAllHandlers() {
    std::lock_guard<std::mutex> guard(mHandlersMutex);
    mHandlers.clear();
}

void CLogger::Debug(const std::string& message, const std::map<std::string, std::string>& metadata) {
    Log(ELogLevel::DEBUG, message, metadata);
}

void CLogger::Info(const std::string& message, const std::map<std::string, std::string>& metadata) {
    Log(ELogLevel::INFO, message, metadata);
}

void CLogger::Warning(const std::string& message, const std::map<std::string, std::string>& metadata) {
    Log(ELogLevel::WARNING, message, metadata);
}

void CLogger::Error(const std::string& message, const std::map<std::string, std::string>& metadata) {
    Log(ELogLevel::ERROR, message, metadata);
}

void CLogger::Trade(const std::string& message, const std::map<std::string, std::string>& metadata) {
    Log(ELogLevel::TRADE, message, metadata);
}

void CLogger::Log(ELogLevel level, const std::string& message, const std::map<std::string, std::string>& metadata) {
    if (level < mMinLevel) {
        return;
    }

    SLogEntry entry;
    entry.mTimestamp = std::chrono::system_clock::now();
    entry.mLevel = level;
    entry.mMessage = message;
    entry.mMetadata = metadata;

    std::lock_guard<std::mutex> guard(mHandlersMutex);
    for (const auto& handler : mHandlers) {
        handler->Write(entry);
    }
}

std::string CLogger::FormatTimestamp(const std::chrono::system_clock::time_point& timestamp) const {
    auto time = std::chrono::system_clock::to_time_t(timestamp);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(timestamp.time_since_epoch()) % 1000;

    std::stringstream ss;
    ss << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S")
       << '.' << std::setfill('0') << std::setw(3) << ms.count();

    return ss.str();
}

std::string CLogger::LevelToString(ELogLevel level) const {
    switch (level) {
        case ELogLevel::DEBUG: return "DEBUG";
        case ELogLevel::INFO: return "INFO";
        case ELogLevel::WARNING: return "WARNING";
        case ELogLevel::ERROR: return "ERROR";
        case ELogLevel::TRADE: return "TRADE";
        default: return "UNKNOWN";
    }
}

} // namespace Utils
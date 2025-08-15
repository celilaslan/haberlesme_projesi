#include "Logger.h"
#include <iostream>
#include <chrono>
#include <iomanip>

std::unique_ptr<std::ofstream> Logger::logFile = nullptr;
std::mutex Logger::mtx;

void Logger::init(const std::string& logFilePath) {
    std::lock_guard<std::mutex> lock(mtx);
    if (!logFile) {
        logFile = std::make_unique<std::ofstream>(logFilePath, std::ios::trunc);
        if (!logFile->is_open()) {
            std::cerr << "[" << getTimestamp() << "] ERROR: Could not open log file: " << logFilePath << std::endl;
        }
    }
}

std::string Logger::getTimestamp() {
    const auto now = std::chrono::system_clock::now();
    const auto time_t_now = std::chrono::system_clock::to_time_t(now);
    const auto duration = now.time_since_epoch();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(duration) % 1000;

    std::ostringstream oss;
    struct tm time_info;
#if defined(_WIN32)
    localtime_s(&time_info, &time_t_now);
#else
    localtime_r(&time_t_now, &time_info);
#endif

    oss << std::put_time(&time_info, "%Y-%m-%d %H:%M:%S");
    oss << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return oss.str();
}

void Logger::info(const std::string& msg) {
    std::lock_guard<std::mutex> lock(mtx);
    std::string logMsg = "[" + getTimestamp() + "] " + msg;
    std::cout << logMsg << std::endl;
    if (logFile && logFile->is_open()) {
        *logFile << logMsg << std::endl;
    }
}

void Logger::error(const std::string& msg) {
    std::lock_guard<std::mutex> lock(mtx);
    std::string logMsg = "[" + getTimestamp() + "] ERROR: " + msg;
    std::cerr << logMsg << std::endl;
    if (logFile && logFile->is_open()) {
        *logFile << logMsg << std::endl;
    }
}

std::ofstream& Logger::getLogStream() {
    return *logFile;
}
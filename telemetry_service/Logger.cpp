/**
 * @file Logger.cpp
 * @brief Implementation of the thread-safe logging system
 * 
 * This file contains the implementation of the Logger class, providing
 * thread-safe logging functionality with timestamps and dual output
 * (console and file).
 */

#include "Logger.h"
#include <iostream>
#include <chrono>
#include <iomanip>

// Static member definitions
std::unique_ptr<std::ofstream> Logger::logFile = nullptr;
std::mutex Logger::mtx;

/**
 * @brief Initialize the logging system
 * @param logFilePath Path to the log file
 * 
 * Creates or opens the specified log file for writing. If the file cannot
 * be opened, an error message is printed to stderr. This method is thread-safe
 * and can be called multiple times (subsequent calls are ignored).
 */
void Logger::init(const std::string& logFilePath) {
    std::lock_guard<std::mutex> lock(mtx);
    if (!logFile) {
        // Open log file in truncate mode (overwrite existing content)
        logFile = std::make_unique<std::ofstream>(logFilePath, std::ios::trunc);
        if (!logFile->is_open()) {
            std::cerr << "[" << getTimestamp() << "] ERROR: Could not open log file: " << logFilePath << std::endl;
        }
    }
}

/**
 * @brief Generate a formatted timestamp string
 * @return Timestamp string with millisecond precision
 * 
 * Creates a timestamp in the format "YYYY-MM-DD HH:MM:SS.mmm".
 * Uses platform-specific time conversion functions for thread safety.
 */
std::string Logger::getTimestamp() {
    // Get current time with high precision
    const auto now = std::chrono::system_clock::now();
    const auto time_t_now = std::chrono::system_clock::to_time_t(now);
    const auto duration = now.time_since_epoch();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(duration) % 1000;

    std::ostringstream oss;
    struct tm time_info;
    
    // Use platform-specific thread-safe time conversion
#if defined(_WIN32)
    localtime_s(&time_info, &time_t_now);  // Windows thread-safe version
#else
    localtime_r(&time_t_now, &time_info);  // POSIX thread-safe version
#endif

    // Format the timestamp with milliseconds
    oss << std::put_time(&time_info, "%Y-%m-%d %H:%M:%S");
    oss << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return oss.str();
}

/**
 * @brief Log an informational message
 * @param msg The message to log
 * 
 * Writes the message with timestamp to both stdout and the log file.
 * Thread-safe through mutex protection.
 */
void Logger::info(const std::string& msg) {
    std::lock_guard<std::mutex> lock(mtx);
    std::string logMsg = "[" + getTimestamp() + "] " + msg;
    
    // Write to console
    std::cout << logMsg << std::endl;
    
    // Write to log file if available
    if (logFile && logFile->is_open()) {
        *logFile << logMsg << std::endl;
    }
}

/**
 * @brief Log an error message
 * @param msg The error message to log
 * 
 * Writes the message with timestamp and "ERROR:" prefix to both stderr
 * and the log file. Thread-safe through mutex protection.
 */
void Logger::error(const std::string& msg) {
    std::lock_guard<std::mutex> lock(mtx);
    std::string logMsg = "[" + getTimestamp() + "] ERROR: " + msg;
    
    // Write to stderr for errors
    std::cerr << logMsg << std::endl;
    
    // Write to log file if available
    if (logFile && logFile->is_open()) {
        *logFile << logMsg << std::endl;
    }
}

/**
 * @brief Get direct access to the log file stream
 * @return Reference to the log file output stream
 * 
 * Provides direct access to the log file for advanced scenarios.
 * Note: This method does not provide thread safety - caller must
 * handle synchronization if needed.
 */
std::ofstream& Logger::getLogStream() {
    return *logFile;
}
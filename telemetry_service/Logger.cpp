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
LogLevel Logger::currentLevel = LogLevel::INFO;

/**
 * @brief Initialize the logging system
 * @param logFilePath Path to the log file
 * @param level Minimum log level to output
 * 
 * Creates or opens the specified log file for writing. If the file cannot
 * be opened, an error message is printed to stderr. This method is thread-safe
 * and can be called multiple times (subsequent calls are ignored).
 */
void Logger::init(const std::string& logFilePath, LogLevel level) {
    std::lock_guard<std::mutex> lock(mtx);
    currentLevel = level;
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
    log(LogLevel::INFO, msg, false);
}

/**
 * @brief Log a debug message
 * @param msg The debug message to log
 */
void Logger::debug(const std::string& msg) {
    log(LogLevel::DEBUG, msg, false);
}

/**
 * @brief Log a warning message
 * @param msg The warning message to log
 */
void Logger::warn(const std::string& msg) {
    log(LogLevel::WARN, msg, true);  // Use stderr for warnings
}

/**
 * @brief Log an error message
 * @param msg The error message to log
 * 
 * Writes the message with timestamp and "ERROR:" prefix to both stderr
 * and the log file. Thread-safe through mutex protection.
 */
void Logger::error(const std::string& msg) {
    log(LogLevel::ERROR, msg, true);
}

/**
 * @brief Set the minimum log level
 * @param level New minimum log level
 */
void Logger::setLevel(LogLevel level) {
    std::lock_guard<std::mutex> lock(mtx);
    currentLevel = level;
}

/**
 * @brief Internal logging method with level support
 * @param level Log level
 * @param msg Message to log
 * @param useStderr Whether to use stderr instead of stdout
 */
void Logger::log(LogLevel level, const std::string& msg, bool useStderr) {
    std::lock_guard<std::mutex> lock(mtx);
    
    // Check if this message should be logged based on current level
    if (level < currentLevel) {
        return;
    }
    
    std::string levelStr = levelToString(level);
    std::string logMsg = "[" + getTimestamp() + "] " + levelStr + ": " + msg;
    
    // Write to appropriate output stream
    if (useStderr) {
        std::cerr << logMsg << std::endl;
    } else {
        std::cout << logMsg << std::endl;
    }
    
    // Write to log file if available and properly initialized
    if (logFile && logFile->is_open()) {
        *logFile << logMsg << std::endl;
        logFile->flush(); // Ensure immediate write for important messages
    }
}

/**
 * @brief Convert log level to string
 * @param level Log level to convert
 * @return String representation of the log level
 */
std::string Logger::levelToString(LogLevel level) {
    switch (level) {
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO:  return "INFO";
        case LogLevel::WARN:  return "WARN";
        case LogLevel::ERROR: return "ERROR";
        default:              return "UNKNOWN";
    }
}

/**
 * @brief Log a structured service status message
 * @param component Component name
 * @param status Status message
 * @param details Optional additional details
 */
void Logger::status(const std::string& component, const std::string& status, const std::string& details) {
    std::string msg = "[" + component + "] " + status;
    if (!details.empty()) {
        msg += " (" + details + ")";
    }
    info(msg);
}

/**
 * @brief Log a performance metric
 * @param metric Metric name
 * @param value Metric value
 * @param unit Optional unit
 */
void Logger::metric(const std::string& metric, double value, const std::string& unit) {
    std::string msg = "METRIC: " + metric + " = " + std::to_string(value);
    if (!unit.empty()) {
        msg += " " + unit;
    }
    info(msg);
}

/**
 * @brief Log service startup completion with summary
 * @param uavCount Number of UAVs configured
 * @param tcpPorts List of TCP ports in use
 * @param udpPorts List of UDP ports in use
 */
void Logger::serviceStarted(int uavCount, const std::vector<int>& tcpPorts, const std::vector<int>& udpPorts) {
    info("=== SERVICE STARTUP COMPLETE ===");
    info("Configuration Summary:");
    info("  UAVs configured: " + std::to_string(uavCount));
    
    std::string tcpPortsStr = "  TCP ports: ";
    for (size_t i = 0; i < tcpPorts.size(); ++i) {
        if (i > 0) tcpPortsStr += ", ";
        tcpPortsStr += std::to_string(tcpPorts[i]);
    }
    info(tcpPortsStr);
    
    std::string udpPortsStr = "  UDP ports: ";
    for (size_t i = 0; i < udpPorts.size(); ++i) {
        if (i > 0) udpPortsStr += ", ";
        udpPortsStr += std::to_string(udpPorts[i]);
    }
    info(udpPortsStr);
    info("Service ready for connections.");
}

/**
 * @brief Check if the logger has been properly initialized
 * @return true if logger is initialized and ready to use
 */
bool Logger::isInitialized() {
    std::lock_guard<std::mutex> lock(mtx);
    return logFile != nullptr && logFile->is_open();
}

/**
 * @brief Flush and close the log file (cleanup)
 * 
 * Ensures all pending log data is written to file and properly closed.
 * Thread-safe and can be called multiple times safely.
 */
void Logger::shutdown() {
    std::lock_guard<std::mutex> lock(mtx);
    if (logFile && logFile->is_open()) {
        logFile->flush();
        logFile->close();
        logFile.reset();
    }
}
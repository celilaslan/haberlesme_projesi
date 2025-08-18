/**
 * @file Logger.h
 * @brief Thread-safe logging system for the telemetry service
 * 
 * This file defines a simple singleton-style logging system that provides
 * thread-safe logging with automatic timestamps. The logger writes to both
 * console and file outputs.
 */

#ifndef LOGGER_H
#define LOGGER_H

#include <string>
#include <fstream>
#include <mutex>
#include <memory>
#include <vector>

/**
 * @enum LogLevel
 * @brief Log level enumeration for message categorization
 */
enum class LogLevel {
    DEBUG = 0,  ///< Detailed debug information
    INFO = 1,   ///< General informational messages
    WARN = 2,   ///< Warning messages
    ERROR = 3   ///< Error messages
};

/**
 * @class Logger
 * @brief Static logging utility class with enhanced structured logging
 * 
 * Provides thread-safe logging functionality with automatic timestamping,
 * log levels, and structured output for both console and systemd journald.
 * All methods are static, making it easy to use from anywhere in the application.
 */
class Logger {
public:
    /**
     * @brief Initialize the logging system with a log file
     * @param logFilePath Path to the log file where messages will be written
     * @param level Minimum log level to output (default: INFO)
     * 
     * This must be called before using any other logging methods.
     * Creates or opens the specified log file for writing.
     */
    static void init(const std::string& logFilePath, LogLevel level = LogLevel::INFO);
    
    /**
     * @brief Set the minimum log level
     * @param level New minimum log level
     */
    static void setLevel(LogLevel level);
    
    /**
     * @brief Log a debug message
     * @param msg The debug message to log
     */
    static void debug(const std::string& msg);
    
    /**
     * @brief Log an informational message
     * @param msg The message to log
     * 
     * Writes an INFO level message with timestamp to both console and log file.
     * Thread-safe and can be called from multiple threads simultaneously.
     */
    static void info(const std::string& msg);
    
    /**
     * @brief Log a warning message
     * @param msg The warning message to log
     */
    static void warn(const std::string& msg);
    
    /**
     * @brief Log an error message
     * @param msg The error message to log
     * 
     * Writes an ERROR level message with timestamp to both console (stderr) and log file.
     * Thread-safe and can be called from multiple threads simultaneously.
     */
    static void error(const std::string& msg);
    
    /**
     * @brief Log a structured service status message
     * @param component Component name (e.g., "ZMQ", "UDP", "Service")
     * @param status Status message
     * @param details Optional additional details
     */
    static void status(const std::string& component, const std::string& status, const std::string& details = "");
    
    /**
     * @brief Log a performance metric
     * @param metric Metric name
     * @param value Metric value
     * @param unit Optional unit (e.g., "msg/s", "bytes")
     */
    static void metric(const std::string& metric, double value, const std::string& unit = "");
    
    /**
     * @brief Log service startup completion with summary
     * @param uavCount Number of UAVs configured
     * @param zmqPorts List of ZMQ ports in use
     * @param udpPorts List of UDP ports in use
     */
    static void serviceStarted(int uavCount, const std::vector<int>& zmqPorts, const std::vector<int>& udpPorts);
    
    /**
     * @brief Get direct access to the log file stream
     * @return Reference to the log file output stream
     * 
     * Provides direct access to the log file for advanced logging scenarios.
     * Should be used with proper synchronization.
     */
    static std::ofstream& getLogStream();

private:
    static std::unique_ptr<std::ofstream> logFile;  ///< Log file output stream
    static std::mutex mtx;                          ///< Mutex for thread-safe access
    static LogLevel currentLevel;                   ///< Current minimum log level
    
    /**
     * @brief Generate a timestamp string for log entries
     * @return Formatted timestamp string with millisecond precision
     * 
     * Creates timestamps in format: "YYYY-MM-DD HH:MM:SS.mmm"
     */
    static std::string getTimestamp();
    
    /**
     * @brief Convert log level to string
     * @param level Log level to convert
     * @return String representation of the log level
     */
    static std::string levelToString(LogLevel level);
    
    /**
     * @brief Internal logging method with level support
     * @param level Log level
     * @param msg Message to log
     * @param useStderr Whether to use stderr instead of stdout
     */
    static void log(LogLevel level, const std::string& msg, bool useStderr = false);
};

#endif // LOGGER_H
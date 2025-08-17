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

/**
 * @class Logger
 * @brief Static logging utility class
 * 
 * Provides thread-safe logging functionality with automatic timestamping.
 * All methods are static, making it easy to use from anywhere in the application.
 * The logger writes to both console (stdout/stderr) and a log file.
 */
class Logger {
public:
    /**
     * @brief Initialize the logging system with a log file
     * @param logFilePath Path to the log file where messages will be written
     * 
     * This must be called before using any other logging methods.
     * Creates or opens the specified log file for writing.
     */
    static void init(const std::string& logFilePath);
    
    /**
     * @brief Log an informational message
     * @param msg The message to log
     * 
     * Writes an INFO level message with timestamp to both console and log file.
     * Thread-safe and can be called from multiple threads simultaneously.
     */
    static void info(const std::string& msg);
    
    /**
     * @brief Log an error message
     * @param msg The error message to log
     * 
     * Writes an ERROR level message with timestamp to both console (stderr) and log file.
     * Thread-safe and can be called from multiple threads simultaneously.
     */
    static void error(const std::string& msg);
    
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
    
    /**
     * @brief Generate a timestamp string for log entries
     * @return Formatted timestamp string with millisecond precision
     * 
     * Creates timestamps in format: "YYYY-MM-DD HH:MM:SS.mmm"
     */
    static std::string getTimestamp();
};

#endif // LOGGER_H
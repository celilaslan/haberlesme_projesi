/**
 * @file main.cpp
 * @brief Main entry point for the telemetry service application
 *
 * This file contains the main function that initializes and runs the telemetry service.
 * The service acts as a central hub for UAV telemetry data, supporting both ZeroMQ and UDP protocols.
 */

#include <unistd.h>

#include <atomic>
#include <csignal>
#include <cstring>
#include <iostream>
#include <mutex>

#include "Logger.h"
#include "TelemetryService.h"

// Global flag to control the main application loop
// This is set to false when a shutdown signal is received
std::atomic<bool> g_running(true);
std::atomic<int> g_signal_received(0);
std::mutex g_signal_mutex;

/**
 * @brief Signal handler for graceful shutdown
 * @param signum The signal number received (SIGINT, SIGTERM, etc.)
 *
 * This function is called when the application receives a shutdown signal.
 * It safely sets the global flags using async-signal-safe operations only.
 */
void signalHandler(int signum) {
    // Only use async-signal-safe functions in signal handlers
    g_signal_received.store(signum);
    g_running.store(false);

    // Use write() instead of std::cout (async-signal-safe)
    const char* msg = "Signal received. Shutting down...\n";
    ssize_t result = write(STDERR_FILENO, msg, strlen(msg));
    (void)result;  // Suppress unused variable warning
}

/**
 * @brief Main entry point of the telemetry service application
 * @return Exit code (0 for success, 1 for error)
 *
 * This function:
 * 1. Initializes the logger system
 * 2. Sets up comprehensive signal handlers for graceful shutdown
 * 3. Creates and initializes the TelemetryService instance
 * 4. Runs the service until a shutdown signal is received
 * 5. Handles all types of exceptions that occur during service execution
 */
int main() {
    // Initialize logger first for consistent logging throughout
    Logger::init("telemetry_log.txt");

    try {
        // Register signal handlers for graceful shutdown
        // SIGINT: Ctrl+C interrupt signal
        // SIGTERM: Termination request signal (used by systemd)
        // SIGHUP: Hangup signal (terminal closed)
        // SIGUSR1: User-defined signal for custom shutdown
        if (std::signal(SIGINT, signalHandler) == SIG_ERR) {
            Logger::error("Failed to register SIGINT handler");
            return 1;
        }
        if (std::signal(SIGTERM, signalHandler) == SIG_ERR) {
            Logger::error("Failed to register SIGTERM handler");
            return 1;
        }
        if (std::signal(SIGHUP, signalHandler) == SIG_ERR) {
            Logger::error("Failed to register SIGHUP handler");
            return 1;
        }
        if (std::signal(SIGUSR1, signalHandler) == SIG_ERR) {
            Logger::error("Failed to register SIGUSR1 handler");
            return 1;
        }

        Logger::info("=== TELEMETRY SERVICE STARTING ===");

        // Create and start the telemetry service
        TelemetryService service;
        service.run(g_running);

        // Log which signal caused shutdown
        int signal_num = g_signal_received.load();
        if (signal_num > 0) {
            Logger::info("Service stopped by signal: " + std::to_string(signal_num));
        }

    } catch (const std::exception& e) {
        // Log any fatal errors that cause the service to crash
        Logger::error("A fatal error occurred: " + std::string(e.what()));
        std::cerr << "FATAL ERROR: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        // Catch any other types of exceptions (not derived from std::exception)
        Logger::error("An unknown fatal error occurred");
        std::cerr << "FATAL ERROR: Unknown exception occurred" << std::endl;
        return 1;
    }

    Logger::info("=== APPLICATION TERMINATED GRACEFULLY ===");
    std::cout << "Application terminated gracefully." << std::endl;

    // Ensure logger cleanup
    Logger::shutdown();
    return 0;
}
/**
 * @file main.cpp
 * @brief Main entry point for the telemetry service application
 * 
 * This file contains the main function that initializes and runs the telemetry service.
 * The service acts as a central hub for UAV telemetry data, supporting both ZeroMQ and UDP protocols.
 */

#include "TelemetryService.h"
#include <iostream>
#include <csignal>
#include <atomic>

// Global flag to control the main application loop
// This is set to false when a shutdown signal is received
std::atomic<bool> g_running(true);

/**
 * @brief Signal handler for graceful shutdown
 * @param signum The signal number received (SIGINT, SIGTERM, etc.)
 * 
 * This function is called when the application receives a shutdown signal.
 * It sets the global running flag to false, which causes the main loop to exit.
 */
void signalHandler(int signum) {
    std::cout << "Signal " << signum << " received. Shutting down..." << std::endl;
    g_running = false;
}

/**
 * @brief Main entry point of the telemetry service application
 * @return Exit code (0 for success, 1 for error)
 * 
 * This function:
 * 1. Sets up signal handlers for graceful shutdown
 * 2. Creates and initializes the TelemetryService instance
 * 3. Runs the service until a shutdown signal is received
 * 4. Handles any exceptions that occur during service execution
 */
int main() {
    // Register signal handlers for graceful shutdown
    // SIGINT: Ctrl+C interrupt signal
    // SIGTERM: Termination request signal (used by systemd)
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    try {
        // Create and start the telemetry service
        TelemetryService service;
        service.run(g_running);
    } catch (const std::exception& e) {
        // Log any fatal errors that cause the service to crash
        std::cerr << "A fatal error occurred: " << e.what() << std::endl;
        return 1;
    }

    std::cout << "Application terminated gracefully." << std::endl;
    return 0;
}
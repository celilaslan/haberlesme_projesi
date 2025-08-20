/**
 * @file mapping_ui.cpp
 * @brief Mapping UI application for receiving and displaying mapping telemetry data
 * 
 * This application uses the TelemetryClient library to connect to the telemetry service
 * and subscribe to mapping-related telemetry data from UAVs. It supports both TCP and UDP
 * protocols via the simplified library API and can send commands back to UAVs.
 */

#include "TelemetryClient.h"
#include <iostream>
#include <chrono>
#include <ctime>
#include <sstream>
#include <memory>
#include <thread>
#include <string>
#include <atomic>
#include <csignal>
#include <iomanip>
#include <sys/select.h>
#include <unistd.h>

using namespace TelemetryAPI;

// Global flag for graceful shutdown
std::atomic<bool> g_running(true);
std::atomic<int> g_signal_received(0);

/**
 * @brief Signal handler for graceful shutdown
 * @param signal Signal number received
 */
void signalHandler(int signal) {
    g_signal_received.store(signal);
    g_running.store(false);
}

/**
 * @brief Generate a formatted timestamp string with millisecond precision
 * @return Timestamp string in format "YYYY-MM-DD HH:MM:SS.mmm"
 * 
 * Uses platform-specific time conversion functions for thread safety.
 * Used for logging telemetry data reception times.
 */
std::string GetTimestamp() {
    const auto now = std::chrono::system_clock::now();
    const auto time_t_now = std::chrono::system_clock::to_time_t(now);
    const auto duration = now.time_since_epoch();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(duration) % 1000;

    std::ostringstream oss;
    struct tm time_info;
    
    // Use platform-specific thread-safe time conversion
#if defined(_WIN32)
    localtime_s(&time_info, &time_t_now);
#else
    localtime_r(&time_t_now, &time_info);
#endif

    oss << std::put_time(&time_info, "%Y-%m-%d %H:%M:%S");
    oss << '.' << std::setfill('0') << std::setw(3) << ms.count();

    return oss.str();
}

/**
 * @brief Telemetry data callback function
 * @param data Received telemetry data from the service
 * 
 * This function is called by the TelemetryClient whenever mapping telemetry
 * data is received. It filters for mapping data and displays it with timestamps.
 */
void onTelemetryReceived(const TelemetryData& data) {
    // Only display mapping data (filtering is also done at subscription level)
    if (data.data_type == DataType::MAPPING) {
        std::string protocol_str = (data.received_via == Protocol::TCP_ONLY) ? "TCP" : 
                                  (data.received_via == Protocol::UDP_ONLY) ? "UDP" : "MIXED";
        
        std::cout << "[" << GetTimestamp() << "] "
                  << "UAV: " << data.uav_name << " | "
                  << "Type: MAPPING | "
                  << "Protocol: " << protocol_str << " | "
                  << "Data: " << data.raw_data << std::endl;
    }
}

/**
 * @brief Error callback function for telemetry client
 * @param error_message Description of the error that occurred
 */
void onTelemetryError(const std::string& error_message) {
    std::cerr << "[Mapping UI Error] " << error_message << std::endl;
}

/**
 * @brief Main function - Mapping UI application entry point
 * @param argc Number of command line arguments
 * @param argv Array of command line argument strings
 * @return Exit code (0 for success, non-zero for error)
 * 
 * Usage: ./mapping_ui [--protocol tcp|udp|both] [--send UAV_NAME] [--uav UAV_NAME]
 */
int main(int argc, char* argv[]) {
    // Set up signal handlers for graceful shutdown
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    // Parse command line arguments
    std::string protocol = "both";  // Default to both protocols
    bool enableSender = false;
    std::string target;
    std::string filter_uav;

    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "--protocol" && i + 1 < argc) {
            protocol = argv[++i];
        } else if (std::string(argv[i]) == "--send" && i + 1 < argc) {
            enableSender = true;
            target = argv[++i];
        } else if (std::string(argv[i]) == "--uav" && i + 1 < argc) {
            filter_uav = argv[++i];
        } else if (std::string(argv[i]) == "--help") {
            std::cout << "Mapping UI - Telemetry Client Library Demo\n";
            std::cout << "Usage: " << argv[0] << " [options]\n";
            std::cout << "Options:\n";
            std::cout << "  --protocol tcp|udp|both : Communication protocol (default: both)\n";
            std::cout << "  --send UAV_NAME         : Enable command sending to specified UAV\n";
            std::cout << "  --uav UAV_NAME          : Filter telemetry to specific UAV only\n";
            std::cout << "  --help                  : Show this help message\n";
            return 0;
        }
    }

    // Validate protocol argument
    Protocol client_protocol;
    if (protocol == "tcp") {
        client_protocol = Protocol::TCP_ONLY;
    } else if (protocol == "udp") {
        client_protocol = Protocol::UDP_ONLY;
    } else if (protocol == "both") {
        client_protocol = Protocol::BOTH;
    } else {
        std::cerr << "Error: Protocol must be 'tcp', 'udp', or 'both'\n";
        return 1;
    }

    std::cout << "=== Mapping UI - Using TelemetryClient Library ===\n";
    std::cout << "Protocol: " << protocol << "\n";
    if (!filter_uav.empty()) {
        std::cout << "Filtering UAV: " << filter_uav << "\n";
    }
    if (enableSender) {
        std::cout << "Command target: " << target << "\n";
    }
    std::cout << "\n";

    // Create and initialize telemetry client
    TelemetryClient client;
    
    if (!client.initialize("localhost")) {
        std::cerr << "Failed to initialize telemetry client\n";
        return 1;
    }

    std::cout << "✓ Telemetry client initialized\n";
    std::cout << "Available UAVs: ";
    auto uavs = client.getAvailableUAVs();
    if (uavs.empty()) {
        std::cout << "None found (using defaults)\n";
    } else {
        for (size_t i = 0; i < uavs.size(); ++i) {
            std::cout << uavs[i];
            if (i < uavs.size() - 1) std::cout << ", ";
        }
        std::cout << "\n";
    }

    // Start receiving telemetry data
    if (!client.startReceiving(client_protocol, onTelemetryReceived, onTelemetryError)) {
        std::cerr << "Failed to start receiving telemetry data\n";
        return 1;
    }

    std::cout << "✓ Started receiving telemetry data\n";

    // Subscribe to mapping data only
    if (!client.subscribeToDataType(DataType::MAPPING)) {
        std::cerr << "Warning: Failed to subscribe to mapping data type\n";
    }

    // If filtering by specific UAV, subscribe to it
    if (!filter_uav.empty()) {
        if (!client.subscribeToUAV(filter_uav, DataType::MAPPING)) {
            std::cerr << "Warning: Failed to subscribe to UAV " << filter_uav << "\n";
        } else {
            std::cout << "✓ Filtering mapping data from " << filter_uav << "\n";
        }
    }

    std::cout << client.getConnectionStatus() << "\n\n";

    // Start command sender thread if enabled
    std::thread senderThread;
    if (enableSender) {
        senderThread = std::thread([&client, target]() {
            std::string line;
            std::cout << "[Mapping UI] Type commands for " << target << " (press Enter to send, Ctrl+C to exit):\n";
            
            while (g_running) {
                // Check if input is available without blocking
                fd_set readfds;
                FD_ZERO(&readfds);
                FD_SET(STDIN_FILENO, &readfds);
                
                struct timeval timeout;
                timeout.tv_sec = 0;
                timeout.tv_usec = 100000; // 100ms timeout
                
                int result = select(STDIN_FILENO + 1, &readfds, nullptr, nullptr, &timeout);
                
                if (result > 0 && FD_ISSET(STDIN_FILENO, &readfds)) {
                    if (std::getline(std::cin, line)) {
                        if (!g_running) break;
                        
                        if (client.sendCommand(target, line, "mapping-ui")) {
                            std::cout << "[Mapping UI] Sent command: " << line << std::endl;
                        } else {
                            std::cerr << "[Mapping UI] Failed to send command: " << line << std::endl;
                        }
                    }
                } else if (result < 0) {
                    break; // Error occurred
                }
            }
        });
    }

    std::cout << "Listening for mapping telemetry data... (Press Ctrl+C to stop)\n";
    std::cout << "============================================\n";

    // Main loop - just wait for shutdown signal
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::cout << "\nShutting down...\n";

    // Stop the telemetry client
    client.stopReceiving();

    // Wait for sender thread to finish
    if (senderThread.joinable()) {
        senderThread.join();
    }

    // Log shutdown reason if caused by signal
    int signal_num = g_signal_received.load();
    if (signal_num > 0) {
        std::cout << "Mapping UI shutdown initiated by signal: " << signal_num << std::endl;
    }

    std::cout << "Mapping UI stopped.\n";
    return 0;
}

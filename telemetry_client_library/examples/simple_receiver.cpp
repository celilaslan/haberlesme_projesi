/**
 * @file simple_receiver.cpp
 * @brief Simple example showing how to receive telemetry data using the client library
 *
 * This example demonstrates the basic usage of the TelemetryClient library
 * to receive telemetry data from UAVs via the telemetry service.
 */

#include "TelemetryClient.h"
#include <iostream>
#include <csignal>
#include <atomic>
#include <thread>
#include <chrono>

using namespace TelemetryAPI;

// Global flag for graceful shutdown
std::atomic<bool> g_running(true);

/**
 * @brief Signal handler for graceful shutdown
 */
void signalHandler(int signal) {
    std::cout << "\nShutdown signal received..." << std::endl;
    g_running = false;
}

/**
 * @brief Callback function to handle received telemetry data
 * @param data The received telemetry data
 */
void onTelemetryReceived(const TelemetryData& data) {
    std::cout << "[" << data.timestamp_ms << "] "
              << "UAV: " << data.uav_name << " | "
              << "Type: " << (data.data_type == DataType::MAPPING ? "MAPPING" :
                            data.data_type == DataType::CAMERA ? "CAMERA" : "UNKNOWN") << " | "
              << "Protocol: " << (data.received_via == Protocol::TCP_ONLY ? "TCP" :
                                 data.received_via == Protocol::UDP_ONLY ? "UDP" : "BOTH") << " | "
              << "Data: " << data.raw_data << std::endl;
}

/**
 * @brief Callback function to handle errors
 * @param error_message Description of the error
 */
void onError(const std::string& error_message) {
    std::cerr << "ERROR: " << error_message << std::endl;
}

int main(int argc, char* argv[]) {
    // Set up signal handler
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    std::cout << "=== Simple Telemetry Receiver ===" << std::endl;
    std::cout << "TelemetryClient Library Version: " << getLibraryVersion() << std::endl;
    std::cout << std::endl;

    // Parse command line arguments
    std::string protocol_str = "tcp";
    std::string service_host = "localhost";

    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "--protocol" && i + 1 < argc) {
            protocol_str = argv[++i];
        } else if (std::string(argv[i]) == "--host" && i + 1 < argc) {
            service_host = argv[++i];
        } else if (std::string(argv[i]) == "--help") {
            std::cout << "Usage: " << argv[0] << " [--protocol tcp|udp|both] [--host hostname]" << std::endl;
            std::cout << "  --protocol: Communication protocol (default: tcp)" << std::endl;
            std::cout << "  --host:     Telemetry service hostname (default: localhost)" << std::endl;
            return 0;
        }
    }

    // Convert protocol string to enum
    Protocol protocol = Protocol::TCP_ONLY;
    if (protocol_str == "tcp") {
        protocol = Protocol::TCP_ONLY;
    } else if (protocol_str == "udp") {
        protocol = Protocol::UDP_ONLY;
    } else if (protocol_str == "both") {
        protocol = Protocol::BOTH;
    } else {
        std::cerr << "Invalid protocol: " << protocol_str << std::endl;
        return 1;
    }

    // Test service connection first
    std::cout << "Testing connection to telemetry service..." << std::endl;
    if (!testServiceConnection(service_host, 5557, 3000)) {
        std::cout << "Warning: Could not connect to telemetry service. Will try anyway." << std::endl;
    } else {
        std::cout << "✓ Service connection test passed." << std::endl;
    }

    // Create and initialize the client
    TelemetryClient client;
    client.setDebugMode(true);

    if (!client.initialize(service_host)) {
        std::cerr << "Failed to initialize client: " << client.getLastError() << std::endl;
        return 1;
    }

    std::cout << "✓ Client initialized successfully." << std::endl;
    std::cout << "Available UAVs: ";
    auto uavs = client.getAvailableUAVs();
    if (uavs.empty()) {
        std::cout << "None found (using defaults)" << std::endl;
    } else {
        for (const auto& uav : uavs) {
            std::cout << uav << " ";
        }
        std::cout << std::endl;
    }

    // Start receiving telemetry data
    if (!client.startReceiving(protocol, onTelemetryReceived, onError)) {
        std::cerr << "Failed to start receiving: " << client.getLastError() << std::endl;
        return 1;
    }

    std::cout << "✓ Started receiving telemetry data." << std::endl;
    std::cout << "Connection Status: " << client.getConnectionStatus() << std::endl;
    std::cout << std::endl;
    std::cout << "Listening for telemetry data... (Press Ctrl+C to stop)" << std::endl;
    std::cout << "============================================" << std::endl;

    // Main loop - just wait for data
    while (g_running && client.isReceiving()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // Clean shutdown
    std::cout << std::endl << "Stopping client..." << std::endl;
    client.stopReceiving();

    std::cout << "Simple receiver stopped." << std::endl;
    return 0;
}

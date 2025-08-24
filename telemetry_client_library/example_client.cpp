/**
 * @file example_client.cpp
 * @brief Example usage of the TelemetryClient library
 *
 * This example demonstrates how to use the TelemetryClient library to:
 * 1. Connect to the telemetry service
 * 2. Subscribe to telemetry topics with wildcards
 * 3. Receive and parse telemetry data
 * 4. Send commands to UAVs (TCP only)
 * 5. Handle connection status changes
 */

#include "TelemetryClient.h"
#include <iostream>
#include <iomanip>
#include <thread>
#include <chrono>
#include <csignal>
#include <atomic>

// Global flag for clean shutdown
std::atomic<bool> g_running{true};

void signalHandler(int signal) {
    (void)signal;  // Suppress unused parameter warning
    std::cout << "\nShutdown signal received..." << std::endl;
    g_running = false;
}

int main() {
    // Set up signal handlers for clean shutdown
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    std::cout << "===========================================" << std::endl;
    std::cout << "Telemetry Client Library Example" << std::endl;
    std::cout << "===========================================" << std::endl;

    // Create a telemetry client with unique ID
    TelemetryAPI::TelemetryClient client("example_ui");

    // Set up connection status callback
    client.setConnectionCallback([](bool connected, const std::string& error_message) {
        if (connected) {
            std::cout << "✅ Connected to telemetry service" << std::endl;
        } else {
            std::cout << "❌ Disconnected from telemetry service";
            if (!error_message.empty()) {
                std::cout << " - Error: " << error_message;
            }
            std::cout << std::endl;
        }
    });

    // Set up telemetry data callback
    client.setTelemetryCallback([](const std::string& topic, const std::vector<uint8_t>& data) {
        std::cout << std::endl;
        std::cout << "📡 Received telemetry on topic: " << topic << std::endl;
        std::cout << "   Data size: " << data.size() << " bytes" << std::endl;

        // Try to parse the packet header
        const auto* header = TelemetryAPI::TelemetryClient::parseHeader(data);
        if (header) {
            std::cout << "   Target: " << TelemetryAPI::TelemetryClient::getTargetName(header->targetID) << std::endl;
            std::cout << "   Type: " << TelemetryAPI::TelemetryClient::getPacketTypeName(header->packetType) << std::endl;

            // Show first few bytes of payload
            if (data.size() > sizeof(TelemetryAPI::PacketHeader)) {
                std::cout << "   Payload (hex): ";
                const uint8_t* payload = data.data() + sizeof(TelemetryAPI::PacketHeader);
                size_t payload_size = data.size() - sizeof(TelemetryAPI::PacketHeader);
                size_t show_bytes = std::min(payload_size, size_t(16));  // Show first 16 bytes

                for (size_t i = 0; i < show_bytes; ++i) {
                    std::cout << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(payload[i]) << " ";
                }
                if (payload_size > show_bytes) {
                    std::cout << "... (" << (payload_size - show_bytes) << " more bytes)";
                }
                std::cout << std::dec << std::endl;  // Reset to decimal
            }
        } else {
            std::cout << "   Invalid packet header" << std::endl;
        }
    });

    // Try to connect using configuration file first
    std::cout << "Attempting to connect via configuration file..." << std::endl;
    bool connected = client.connectFromConfig("service_config.json", TelemetryAPI::Protocol::TCP);

    if (!connected) {
        // Fallback to manual connection
        std::cout << "Config connection failed, trying manual connection..." << std::endl;
        connected = client.connect("localhost", 5556, TelemetryAPI::Protocol::TCP);  // Default TCP subscriber port
    }

    if (!connected) {
        std::cerr << "❌ Failed to connect to telemetry service!" << std::endl;
        std::cerr << "Make sure the telemetry service is running." << std::endl;
        return 1;
    }

    // Wait a moment for connection to establish
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // Subscribe to various telemetry topics
    std::cout << std::endl;
    std::cout << "Subscribing to telemetry topics..." << std::endl;

    // Subscribe to all telemetry data
    if (client.subscribe("telemetry.*")) {
        std::cout << "✅ Subscribed to all telemetry: telemetry.*" << std::endl;
    } else {
        std::cout << "❌ Failed to subscribe to telemetry.*" << std::endl;
    }

    // Subscribe to specific camera data (this will be filtered by the above subscription)
    if (client.subscribe("telemetry.*.camera.*")) {
        std::cout << "✅ Subscribed to camera data: telemetry.*.camera.*" << std::endl;
    } else {
        std::cout << "❌ Failed to subscribe to camera data" << std::endl;
    }

    // Subscribe to location data from all UAVs
    if (client.subscribe("telemetry.*.*.location")) {
        std::cout << "✅ Subscribed to location data: telemetry.*.*.location" << std::endl;
    } else {
        std::cout << "❌ Failed to subscribe to location data" << std::endl;
    }

    std::cout << std::endl;
    std::cout << "Listening for telemetry data..." << std::endl;
    std::cout << "Press Ctrl+C to stop" << std::endl;
    std::cout << "===========================================" << std::endl;

    // Demonstration of sending commands (TCP only)
    std::thread command_thread([&client]() {
        std::this_thread::sleep_for(std::chrono::seconds(5));  // Wait 5 seconds

        if (client.getProtocol() == TelemetryAPI::Protocol::TCP) {
            std::cout << std::endl;
            std::cout << "🚁 Sending test command to UAV_1..." << std::endl;
            if (client.sendCommand("UAV_1", "GET_STATUS")) {
                std::cout << "✅ Command sent successfully" << std::endl;
            } else {
                std::cout << "❌ Failed to send command" << std::endl;
            }
        }
    });

    // Main loop - just wait for data and handle shutdown
    while (g_running && client.isConnected()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // Clean shutdown
    std::cout << std::endl;
    std::cout << "Shutting down..." << std::endl;

    // Wait for command thread to finish
    if (command_thread.joinable()) {
        command_thread.join();
    }

    // Disconnect from service
    client.disconnect();

    std::cout << "✅ Client stopped successfully" << std::endl;
    return 0;
}

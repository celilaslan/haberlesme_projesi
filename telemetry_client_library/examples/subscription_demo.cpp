/**
 * @file subscription_demo.cpp
 * @brief Demonstration of subscription and unsubscription functionality
 *
 * This example shows how to dynamically subscribe and unsubscribe from
 * UAV data streams to optimize network traffic and processing.
 */

#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <thread>

#include "TelemetryClient.h"

using namespace TelemetryAPI;

std::atomic<bool> g_running(true);

void signalHandler(int signal) {
    std::cout << "\nReceived signal " << signal << ". Shutting down gracefully..." << std::endl;
    g_running = false;
}

void onTelemetryReceived(const TelemetryData& data) {
    std::cout << "[RECEIVED] " << data.uav_name << " (" << (data.data_type == DataType::MAPPING ? "MAP" : "CAM")
              << "): " << data.raw_data << std::endl;
}

void onError(const std::string& error_message) { std::cout << "ERROR: " << error_message << std::endl; }

int main() {
    std::cout << "=== SUBSCRIPTION/UNSUBSCRIPTION DEMO ===" << std::endl;
    std::cout << "This demo shows dynamic subscription management." << std::endl;
    std::cout << std::endl;

    // Setup signal handling
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    try {
        TelemetryClient client;

        // Initialize client
        if (!client.initialize("localhost")) {
            std::cerr << "Failed to initialize client: " << client.getLastError() << std::endl;
            return 1;
        }
        std::cout << "✓ Client initialized" << std::endl;

        // Enable debug mode to see subscription details
        client.setDebugMode(true);

        // Start receiving with both protocols
        if (!client.startReceiving(Protocol::BOTH, onTelemetryReceived, onError)) {
            std::cerr << "Failed to start receiving: " << client.getLastError() << std::endl;
            return 1;
        }
        std::cout << "✓ Started receiving telemetry data" << std::endl;

        // Phase 1: Subscribe to all mapping data
        std::cout << "\n=== PHASE 1: Subscribe to ALL mapping data ===" << std::endl;
        client.subscribeToDataType(DataType::MAPPING);
        std::this_thread::sleep_for(std::chrono::seconds(5));

        // Phase 2: Subscribe to specific UAV only
        std::cout << "\n=== PHASE 2: Subscribe to UAV_1 only ===" << std::endl;
        client.clearAllSubscriptions();  // Clear previous subscriptions
        client.subscribeToUAV("UAV_1");  // Both mapping and camera for UAV_1
        std::this_thread::sleep_for(std::chrono::seconds(5));

        // Phase 3: Subscribe to UAV_1 camera only
        std::cout << "\n=== PHASE 3: Subscribe to UAV_1 camera only ===" << std::endl;
        client.unsubscribeFromUAV("UAV_1", DataType::MAPPING);  // Remove mapping data
        std::this_thread::sleep_for(std::chrono::seconds(5));

        // Phase 4: Add UAV_2 mapping data
        std::cout << "\n=== PHASE 4: Add UAV_2 mapping data ===" << std::endl;
        client.subscribeToUAV("UAV_2", DataType::MAPPING);
        std::this_thread::sleep_for(std::chrono::seconds(5));

        // Phase 5: Remove all UAV_1 data
        std::cout << "\n=== PHASE 5: Remove all UAV_1 data ===" << std::endl;
        client.unsubscribeFromUAV("UAV_1");  // Remove all UAV_1 data
        std::this_thread::sleep_for(std::chrono::seconds(5));

        // Phase 6: Clear all and listen to everything
        std::cout << "\n=== PHASE 6: Clear all subscriptions (listen to everything) ===" << std::endl;
        client.clearAllSubscriptions();
        std::this_thread::sleep_for(std::chrono::seconds(5));

        std::cout << "\n=== DEMO COMPLETED ===" << std::endl;
        client.stopReceiving();

    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}

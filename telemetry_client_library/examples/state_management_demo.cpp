/**
 * @file state_management_demo.cpp
 * @brief Demonstration of enhanced state management and fail-fast design
 *
 * This example shows the new robust state management system that prevents
 * incorrect API usage and provides clear error messages.
 */

#include <chrono>
#include <iostream>
#include <thread>

#include "TelemetryClient.h"

using namespace TelemetryAPI;

void onTelemetryReceived(const TelemetryData& data) {
    std::cout << "[DATA] " << data.uav_name << ": " << data.raw_data << std::endl;
}

void onError(const std::string& error_message) { std::cout << "[ERROR] " << error_message << std::endl; }

void printClientState(const TelemetryClient& client) {
    std::cout << "Client State: " << client.getStateDescription()
              << " (Receiving: " << (client.isReceiving() ? "Yes" : "No") << ")" << std::endl;
}

int main() {
    std::cout << "=== STATE MANAGEMENT DEMONSTRATION ===" << std::endl;
    std::cout << "This demo shows the enhanced fail-fast state management system." << std::endl;
    std::cout << std::endl;

    TelemetryClient client;

    // Phase 1: Show initial state
    std::cout << "=== PHASE 1: Initial State ===" << std::endl;
    printClientState(client);
    std::cout << std::endl;

    // Try to start receiving before initialization (should fail)
    std::cout << "=== PHASE 2: Try to start receiving before initialization ===" << std::endl;
    if (!client.startReceiving(Protocol::BOTH, onTelemetryReceived, onError)) {
        std::cout << "Expected failure: " << client.getLastError() << std::endl;
    }
    printClientState(client);
    std::cout << std::endl;

    // Initialize client
    std::cout << "=== PHASE 3: Initialize client ===" << std::endl;
    if (client.initialize("localhost")) {
        std::cout << "✓ Client initialized successfully" << std::endl;
    } else {
        std::cout << "✗ Failed to initialize: " << client.getLastError() << std::endl;
    }
    printClientState(client);
    std::cout << std::endl;

    // Try to initialize again (should fail)
    std::cout << "=== PHASE 4: Try to initialize again ===" << std::endl;
    if (!client.initialize("localhost")) {
        std::cout << "Expected failure: " << client.getLastError() << std::endl;
    }
    printClientState(client);
    std::cout << std::endl;

    // Start receiving
    std::cout << "=== PHASE 5: Start receiving telemetry ===" << std::endl;
    if (client.startReceiving(Protocol::BOTH, onTelemetryReceived, onError)) {
        std::cout << "✓ Started receiving successfully" << std::endl;
    } else {
        std::cout << "✗ Failed to start receiving: " << client.getLastError() << std::endl;
    }
    printClientState(client);
    std::cout << std::endl;

    // Try to start receiving again (should fail)
    std::cout << "=== PHASE 6: Try to start receiving again ===" << std::endl;
    if (!client.startReceiving(Protocol::BOTH, onTelemetryReceived, onError)) {
        std::cout << "Expected failure: " << client.getLastError() << std::endl;
    }
    printClientState(client);
    std::cout << std::endl;

    // Let it run for a bit
    std::cout << "=== PHASE 7: Running for 3 seconds ===" << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(3));
    printClientState(client);
    std::cout << std::endl;

    // Stop receiving
    std::cout << "=== PHASE 8: Stop receiving ===" << std::endl;
    client.stopReceiving();
    std::cout << "✓ Stopped receiving" << std::endl;
    printClientState(client);
    std::cout << std::endl;

    // Restart receiving (should work from STOPPED state)
    std::cout << "=== PHASE 9: Restart receiving ===" << std::endl;
    if (client.startReceiving(Protocol::BOTH, onTelemetryReceived, onError)) {
        std::cout << "✓ Restarted receiving successfully" << std::endl;
    } else {
        std::cout << "✗ Failed to restart receiving: " << client.getLastError() << std::endl;
    }
    printClientState(client);
    std::cout << std::endl;

    // Test client reset
    std::cout << "=== PHASE 10: Reset client ===" << std::endl;
    if (client.resetClient()) {
        std::cout << "✓ Client reset successfully" << std::endl;
    } else {
        std::cout << "✗ Failed to reset client" << std::endl;
    }
    printClientState(client);
    std::cout << std::endl;

    std::cout << "=== STATE MANAGEMENT DEMO COMPLETED ===" << std::endl;
    std::cout << "The fail-fast design prevents incorrect API usage and provides clear error messages." << std::endl;

    return 0;
}

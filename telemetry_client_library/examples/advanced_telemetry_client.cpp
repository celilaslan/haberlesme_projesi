/**
 * @file advanced_telemetry_client.cpp
 * @brief Comprehensive example demonstrating all advanced telemetry client features
 *
 * This example showcases the enhanced TelemetryClientAdvanced class with:
 * - Asynchronous command responses
 * - Fleet management
 * - Data recording and analysis
 * - Event handling
 * - Performance monitoring
 * - Mock UAV simulation
 */

#include <atomic>
#include <chrono>
#include <csignal>
#include <iomanip>
#include <iostream>
#include <thread>

#include "TelemetryClient.h"

using namespace TelemetryAPI;

// Global flag for graceful shutdown
std::atomic<bool> g_running(true);

/**
 * @brief Signal handler for graceful shutdown
 */
void signalHandler(int signal) {
    std::cout << "\nReceived signal " << signal << ". Shutting down gracefully..." << std::endl;
    g_running = false;
}

/**
 * @brief Display performance metrics in a formatted way
 */
void displayPerformanceMetrics(const PerformanceMetrics& metrics) {
    std::cout << "\n=== PERFORMANCE METRICS ===" << std::endl;
    std::cout << "CPU Usage: " << std::fixed << std::setprecision(1) << metrics.cpu_usage_percent << "%" << std::endl;
    std::cout << "Memory Usage: " << metrics.memory_usage_mb << " MB" << std::endl;
    std::cout << "Messages/sec: " << metrics.messages_per_second << std::endl;
    std::cout << "Avg Processing Time: " << std::fixed << std::setprecision(2) << metrics.average_processing_time_ms
              << " ms" << std::endl;
    std::cout << "Uptime: " << metrics.uptime_seconds << " seconds" << std::endl;
}

/**
 * @brief Display fleet status in a formatted way
 */
void displayFleetStatus(const FleetStatus& status) {
    std::cout << "\n=== FLEET STATUS ===" << std::endl;
    std::cout << "Active UAVs: " << status.active_uavs << "/" << status.total_uavs << std::endl;
    std::cout << "Overall Health: " << std::fixed << std::setprecision(1) << status.overall_health_score * 100 << "%"
              << std::endl;

    for (const auto& [uav_name, uav_status] : status.uav_statuses) {
        std::cout << "  " << uav_name << ": " << (uav_status.connected ? "ONLINE" : "OFFLINE")
                  << " (Health: " << std::fixed << std::setprecision(1) << uav_status.health_score * 100 << "%)"
                  << std::endl;
    }
}

/**
 * @brief Display data quality metrics
 */
void displayDataQuality(const std::string& uav_name, const DataQuality& quality) {
    std::cout << "\n=== DATA QUALITY: " << uav_name << " ===" << std::endl;
    std::cout << "Packet Loss: " << std::fixed << std::setprecision(2) << quality.packet_loss_rate * 100 << "%"
              << std::endl;
    std::cout << "Avg Latency: " << quality.average_latency_ms << " ms" << std::endl;
    std::cout << "Missing Sequences: " << quality.missing_sequences << std::endl;
    std::cout << "Freshness Score: " << std::fixed << std::setprecision(2) << quality.data_freshness_score * 100 << "%"
              << std::endl;
}

/**
 * @brief Main telemetry callback
 */
void onTelemetryReceived(const TelemetryData& data) {
    static int message_count = 0;
    message_count++;

    if (message_count % 10 == 0) {  // Print every 10th message to avoid spam
        std::cout << "[" << message_count << "] " << data.uav_name << " ("
                  << (data.data_type == DataType::MAPPING ? "MAP" : "CAM") << "): " << data.raw_data << std::endl;
    }
}

/**
 * @brief Error callback
 */
void onError(const std::string& error_message) { std::cout << "ERROR: " << error_message << std::endl; }

/**
 * @brief Event callback for telemetry events
 */
void onTelemetryEvent(TelemetryEvent event, const std::string& details) {
    std::string event_name;
    switch (event) {
        case TelemetryEvent::UAV_CONNECTED:
            event_name = "UAV_CONNECTED";
            break;
        case TelemetryEvent::UAV_DISCONNECTED:
            event_name = "UAV_DISCONNECTED";
            break;
        case TelemetryEvent::DATA_QUALITY_DEGRADED:
            event_name = "DATA_QUALITY_DEGRADED";
            break;
        case TelemetryEvent::COMMAND_FAILED:
            event_name = "COMMAND_FAILED";
            break;
        case TelemetryEvent::NETWORK_ISSUES:
            event_name = "NETWORK_ISSUES";
            break;
        case TelemetryEvent::EMERGENCY_STATUS:
            event_name = "EMERGENCY_STATUS";
            break;
    }

    std::cout << "EVENT: " << event_name << " - " << details << std::endl;
}

/**
 * @brief Command response callback
 */
void onCommandResponse(const CommandResponse& response) {
    std::cout << "Command " << response.command_id << " status: " << static_cast<int>(response.status);

    if (!response.response_data.empty()) {
        std::cout << " - Response: " << response.response_data;
    }

    if (!response.error_message.empty()) {
        std::cout << " - Error: " << response.error_message;
    }

    std::cout << " (Time: " << response.response_time_ms << "ms)" << std::endl;
}

/**
 * @brief Demonstrate mock UAV functionality
 */
void demonstrateMockUAV(TelemetryClientAdvanced& client) {
    std::cout << "\n=== MOCK UAV DEMONSTRATION ===" << std::endl;

    auto mock_uav = client.getMockUAV();

    // Configure mock UAV
    std::map<std::string, std::string> config;
    config["data_rate_ms"] = "500";  // Send data every 500ms
    config["base_code"] = "9000";    // Use 9000-series codes

    if (auto mock_ptr = mock_uav.lock()) {
        if (mock_ptr->createMockUAV("MOCK_UAV_1", config)) {
            std::cout << "✓ Mock UAV created successfully" << std::endl;

            // Simulate some network conditions
            mock_ptr->simulateDataLoss(0.05);  // 5% packet loss
            mock_ptr->simulateLatency(50);     // 50ms additional latency

            std::cout << "✓ Network simulation enabled (5% loss, +50ms latency)" << std::endl;

            // Start mock UAV
            if (mock_ptr->start()) {
                std::cout << "✓ Mock UAV started" << std::endl;

                // Let it run for a few seconds
                std::this_thread::sleep_for(std::chrono::seconds(3));

                // Inject some test data
                mock_ptr->injectTestData("MOCK_UAV_1  TEST_EMERGENCY_9999");
                std::cout << "✓ Emergency test data injected" << std::endl;

                std::this_thread::sleep_for(std::chrono::seconds(2));

                // Stop mock UAV
                mock_ptr->stop();
                std::cout << "✓ Mock UAV stopped" << std::endl;
            }
        }
    } else {
        std::cout << "✗ Failed to get Mock UAV instance" << std::endl;
    }
}

/**
 * @brief Demonstrate data recording and analysis
 */
void demonstrateDataRecording(TelemetryClientAdvanced& advanced_client) {
    std::cout << "\n=== Data Recording Demo ===" << std::endl;

    auto data_buffer = advanced_client.getDataBuffer();
    if (auto buffer_ptr = data_buffer.lock()) {
        buffer_ptr->enableBuffering(10);  // 10MB buffer

        // Create a filename with timestamp
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        std::stringstream ss;
        ss << "telemetry_recording_" << time_t << ".json";
        std::string filename = ss.str();

        if (buffer_ptr->startRecording(filename)) {
            std::cout << "✓ Started recording to: " << filename << std::endl;

            // Record for a few seconds
            std::this_thread::sleep_for(std::chrono::seconds(5));

            buffer_ptr->stopRecording();
            std::cout << "✓ Recording stopped" << std::endl;
            std::cout << "  Buffer usage: " << buffer_ptr->getBufferUsage() * 100 << "%" << std::endl;
        }
    } else {
        std::cout << "✗ Failed to get DataBuffer instance" << std::endl;
    }
}

/**
 * @brief Demonstrate fleet management
 */
void demonstrateFleetManagement(TelemetryClientAdvanced& client) {
    std::cout << "\n=== FLEET MANAGEMENT DEMONSTRATION ===" << std::endl;

    auto fleet_manager = client.getFleetManager();
    if (auto fleet_ptr = fleet_manager.lock()) {
        // Add UAVs to fleet monitoring
        fleet_ptr->addUAV("UAV_1");
        fleet_ptr->addUAV("UAV_2");
        fleet_ptr->addUAV("UAV_3");
        std::cout << "✓ UAVs added to fleet monitoring" << std::endl;

        // Broadcast command to all UAVs
        std::vector<std::string> all_uavs = {"UAV_1", "UAV_2", "UAV_3"};
        if (fleet_ptr->broadcastCommand(all_uavs, "status_report")) {
            std::cout << "✓ Status report command broadcasted to all UAVs" << std::endl;
        }

        // Execute coordinated commands
        std::map<std::string, std::string> coordinated_commands;
        coordinated_commands["UAV_1"] = "takeoff altitude=100";
        coordinated_commands["UAV_2"] = "takeoff altitude=150";
        coordinated_commands["UAV_3"] = "takeoff altitude=200";

        if (fleet_ptr->executeCoordinatedCommand(coordinated_commands)) {
            std::cout << "✓ Coordinated takeoff commands executed" << std::endl;
        }

        // Display fleet status
        auto fleet_status = fleet_ptr->getFleetStatus();
        displayFleetStatus(fleet_status);
    } else {
        std::cout << "✗ Failed to get FleetManager instance" << std::endl;
    }
}

int main(int argc, char* argv[]) {
    std::cout << "=== ADVANCED TELEMETRY CLIENT DEMONSTRATION ===" << std::endl;
    std::cout << "Library Version: " << getLibraryVersion() << std::endl;
    std::cout << "This example demonstrates all advanced features of the telemetry client." << std::endl;
    std::cout << std::endl;

    // Setup signal handling
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    // Parse command line arguments
    std::string service_host = "localhost";
    if (argc > 1) {
        service_host = argv[1];
    }

    try {
        // Create advanced telemetry client
        TelemetryClientAdvanced client;

        // Configure client for development mode
        client.setOperationMode(OperationMode::DEVELOPMENT);
        client.setDebugMode(true);
        client.enablePerformanceMonitoring(true);

        std::cout << "✓ Advanced client created and configured" << std::endl;

        // Initialize client
        if (!client.initialize(service_host)) {
            std::cerr << "Failed to initialize client: " << client.getLastError() << std::endl;
            return 1;
        }
        std::cout << "✓ Client initialized with service at " << service_host << std::endl;

        // Subscribe to events
        client.subscribeToEvents(TelemetryEvent::UAV_CONNECTED, onTelemetryEvent);
        client.subscribeToEvents(TelemetryEvent::UAV_DISCONNECTED, onTelemetryEvent);
        client.subscribeToEvents(TelemetryEvent::COMMAND_FAILED, onTelemetryEvent);
        std::cout << "✓ Event subscriptions configured" << std::endl;

        // Set up data thresholds
        client.setDataThreshold(
            "UAV_1", "altitude", 500.0, [](const std::string& uav, const std::string& param, double value) {
                std::cout << "ALERT: " << uav << " " << param << " exceeded threshold: " << value << std::endl;
            });

        // Configure streaming mode
        client.setStreamMode(StreamMode::RELIABLE);
        std::cout << "✓ Streaming mode set to RELIABLE" << std::endl;

        // Start receiving telemetry data
        if (!client.startReceiving(Protocol::BOTH, onTelemetryReceived, onError)) {
            std::cerr << "Failed to start receiving: " << client.getLastError() << std::endl;
            return 1;
        }
        std::cout << "✓ Started receiving telemetry data" << std::endl;

        // Demonstrate mock UAV
        demonstrateMockUAV(client);

        // Demonstrate fleet management
        demonstrateFleetManagement(client);

        // Demonstrate data recording
        demonstrateDataRecording(client);

        // Send some async commands
        std::cout << "\n=== ASYNCHRONOUS COMMANDS ===" << std::endl;

        std::string cmd_id1 = client.sendCommandAsync("UAV_1", "get_battery_status", onCommandResponse);
        std::string cmd_id2 = client.sendCommandAsync("UAV_2", "get_position", onCommandResponse);

        std::cout << "✓ Async commands sent (IDs: " << cmd_id1 << ", " << cmd_id2 << ")" << std::endl;

        // Send a synchronous command
        auto sync_response = client.sendCommandSync("UAV_3", "get_system_info", 3000);
        std::cout << "✓ Sync command completed - Status: " << static_cast<int>(sync_response.status)
                  << " Time: " << sync_response.response_time_ms << "ms" << std::endl;

        // Main monitoring loop
        std::cout << "\n=== MONITORING LOOP ===" << std::endl;
        std::cout << "Press Ctrl+C to stop..." << std::endl;

        int loop_counter = 0;
        while (g_running) {
            std::this_thread::sleep_for(std::chrono::seconds(5));
            loop_counter++;

            // Display performance metrics every 3 loops (15 seconds)
            if (loop_counter % 3 == 0) {
                auto metrics = client.getPerformanceMetrics();
                displayPerformanceMetrics(metrics);

                // Display network stats
                auto net_stats = client.getNetworkStats();
                std::cout << "\n=== NETWORK STATS ===" << std::endl;
                std::cout << "Latency: " << net_stats.latency_ms << "ms" << std::endl;
                std::cout << "Packet Loss: " << net_stats.packet_loss_percent << "%" << std::endl;
                std::cout << "Reconnections: " << net_stats.reconnection_count << std::endl;
            }

            // Display data quality for UAV_1 every 2 loops (10 seconds)
            if (loop_counter % 2 == 0) {
                auto data_analyzer = client.getDataAnalyzer();
                if (auto analyzer_ptr = data_analyzer.lock()) {
                    auto quality = analyzer_ptr->getDataQuality("UAV_1");
                    displayDataQuality("UAV_1", quality);

                    // Display bandwidth usage
                    auto bandwidth = analyzer_ptr->getBandwidthUsage();
                    std::cout << "\n=== BANDWIDTH USAGE ===" << std::endl;
                    std::cout << "In: " << std::fixed << std::setprecision(2) << bandwidth.bytes_per_second_in << " B/s"
                              << std::endl;
                    std::cout << "Out: " << bandwidth.bytes_per_second_out << " B/s" << std::endl;
                    std::cout << "Total Received: " << bandwidth.total_bytes_received << " bytes" << std::endl;
                }
            }
        }

        // Cleanup
        std::cout << "\n=== CLEANUP ===" << std::endl;
        client.stopReceiving();
        std::cout << "✓ Client stopped" << std::endl;

        // Final statistics
        auto final_metrics = client.getPerformanceMetrics();
        displayPerformanceMetrics(final_metrics);

        std::cout << "\n=== DEMONSTRATION COMPLETED ===" << std::endl;
        return 0;

    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return 1;
    }
}

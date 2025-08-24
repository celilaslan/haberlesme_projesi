/**
 * @file mapping_ui.cpp
 * @brief Mapping UI application for receiving and displaying mapping telemetry data
 *
 * This application uses the TelemetryClient library to connect to the telemetry service
 * and subscribe to mapping-targeted telemetry data from UAVs. It focuses on location
 * and navigation data for real-time mapping visualization.
 */

#include <sys/select.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>

#include "TelemetryClient.h"

using namespace TelemetryAPI;

// Global flag for graceful shutdown
std::atomic<bool> g_running(true);
std::atomic<int> g_signal_received(0);
std::atomic<int> g_packet_count(0);

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
 * @brief Parse and display location data with mapping focus
 * @param data Raw telemetry data
 * @param uav_name UAV name extracted from topic
 */
void displayMappingLocationData(const std::vector<uint8_t>& data, const std::string& uav_name) {
    if (data.size() < sizeof(PacketHeader) + sizeof(double) * 2 + sizeof(float) * 3) {
        std::cout << "   🗺️ Location data (insufficient data)" << std::endl;
        return;
    }

    const uint8_t* payload = data.data() + sizeof(PacketHeader);

    // Assuming UAV location packet structure: lat, lon, alt, heading, speed
    double latitude, longitude;
    float altitude, heading, speed;

    memcpy(&latitude, payload, sizeof(double));
    memcpy(&longitude, payload + sizeof(double), sizeof(double));
    memcpy(&altitude, payload + sizeof(double) * 2, sizeof(float));
    memcpy(&heading, payload + sizeof(double) * 2 + sizeof(float), sizeof(float));
    memcpy(&speed, payload + sizeof(double) * 2 + sizeof(float) * 2, sizeof(float));

    std::cout << "   🗺️ [" << uav_name << "] GPS: " << std::fixed << std::setprecision(7)
              << latitude << ", " << longitude << std::endl;
    std::cout << "       Alt: " << std::setprecision(1) << altitude << "m"
              << " | Course: " << std::setprecision(0) << heading << "°"
              << " | Speed: " << std::setprecision(1) << speed << "m/s" << std::endl;

    // Show distance from reference point (example: Bosphorus Bridge in Istanbul)
    double ref_lat = 41.0392;
    double ref_lon = 29.0352;
    double lat_diff = latitude - ref_lat;
    double lon_diff = longitude - ref_lon;
    double distance_km = sqrt(lat_diff * lat_diff + lon_diff * lon_diff) * 111.0; // Rough approximation

    std::cout << "       Distance from reference: " << std::setprecision(2) << distance_km << " km" << std::endl;
}

/**
 * @brief Parse and display mapping-specific status data
 * @param data Raw telemetry data
 */
void displayMappingStatusData(const std::vector<uint8_t>& data) {
    if (data.size() < sizeof(PacketHeader) + sizeof(uint8_t) * 2 + sizeof(uint16_t) + sizeof(float) * 2) {
        std::cout << "   📊 Status data (insufficient data)" << std::endl;
        return;
    }

    const uint8_t* payload = data.data() + sizeof(PacketHeader);

    uint8_t health = payload[0];
    uint8_t mission = payload[1];
    uint16_t flight_time;
    float cpu_usage, memory_usage;

    memcpy(&flight_time, payload + 2, sizeof(uint16_t));
    memcpy(&cpu_usage, payload + 4, sizeof(float));
    memcpy(&memory_usage, payload + 8, sizeof(float));

    std::string health_indicator;
    switch (health) {
        case 0: health_indicator = "🔴"; break;
        case 1: health_indicator = "🟡"; break;
        case 2: health_indicator = "🟢"; break;
        case 3: health_indicator = "💚"; break;
        default: health_indicator = "⚪"; break;
    }

    std::string mission_indicator;
    switch (mission) {
        case 0: mission_indicator = "⏸️ Idle"; break;
        case 1: mission_indicator = "🚁 Takeoff"; break;
        case 2: mission_indicator = "🗺️ Mapping"; break;
        case 3: mission_indicator = "🛬 Landing"; break;
        case 4: mission_indicator = "🚨 Emergency"; break;
        default: mission_indicator = "❓ Unknown"; break;
    }

    std::cout << "   📊 System: " << health_indicator << " | Mission: " << mission_indicator << std::endl;
    std::cout << "       Flight time: " << (flight_time / 60) << "m " << (flight_time % 60) << "s"
              << " | Resources: CPU " << std::setprecision(1) << cpu_usage << "%, RAM " << memory_usage << "%" << std::endl;
}

/**
 * @brief Extract UAV name from topic string
 * @param topic Topic string like "telemetry.UAV_1.mapping.location"
 * @return UAV name or "Unknown"
 */
std::string extractUAVName(const std::string& topic) {
    size_t first_dot = topic.find('.');
    if (first_dot == std::string::npos) return "Unknown";

    size_t second_dot = topic.find('.', first_dot + 1);
    if (second_dot == std::string::npos) return "Unknown";

    return topic.substr(first_dot + 1, second_dot - first_dot - 1);
}

/**
 * @brief Main function - Mapping UI application entry point
 */
int main(int argc, char* argv[]) {
    // Set up signal handlers for graceful shutdown
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    // Parse command line arguments
    std::string protocol = "tcp";  // Default to TCP
    bool enableSender = false;
    bool enableAllTargets = false;  // New: Enable receiving all target types
    bool enableCrossTarget = false; // New: Enable specific cross-target subscriptions
    std::string target_uav;

    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "--protocol" && i + 1 < argc) {
            protocol = argv[++i];
        } else if (std::string(argv[i]) == "--send" && i + 1 < argc) {
            enableSender = true;
            target_uav = argv[++i];
        } else if (std::string(argv[i]) == "--all-targets") {
            enableAllTargets = true;
        } else if (std::string(argv[i]) == "--cross-target") {
            enableCrossTarget = true;
        } else if (std::string(argv[i]) == "--help") {
            std::cout << "Mapping UI - UAV Location Tracking\n";
            std::cout << "Usage: " << argv[0] << " [options]\n";
            std::cout << "Options:\n";
            std::cout << "  --protocol tcp|udp : Communication protocol (default: tcp)\n";
            std::cout << "  --send UAV_NAME    : Enable command sending to specified UAV\n";
            std::cout << "  --all-targets      : Subscribe to ALL target types (camera, mapping, general)\n";
            std::cout << "  --cross-target     : Subscribe to camera and general data in addition to mapping\n";
            std::cout << "  --help             : Show this help message\n";
            std::cout << "\nSubscription modes:\n";
            std::cout << "  Default: Mapping data only (telemetry.*.mapping.*)\n";
            std::cout << "  --cross-target: Mapping + camera + general data\n";
            std::cout << "  --all-targets: All telemetry data from all targets\n";
            return 0;
        }
    }

    // Validate protocol argument
    Protocol client_protocol;
    if (protocol == "tcp") {
        client_protocol = Protocol::TCP;
    } else if (protocol == "udp") {
        client_protocol = Protocol::UDP;
    } else {
        std::cerr << "Error: Protocol must be 'tcp' or 'udp'\n";
        return 1;
    }

    std::cout << "🗺️ =========================================== 🗺️" << std::endl;
    std::cout << "    Mapping UI - UAV Location Tracking" << std::endl;
    std::cout << "🗺️ =========================================== 🗺️" << std::endl;
    std::cout << "Protocol: " << protocol << std::endl;
    if (enableSender) {
        std::cout << "Command target: " << target_uav << std::endl;
    }
    if (enableAllTargets) {
        std::cout << "Mode: Monitoring ALL target types (camera, mapping, general)" << std::endl;
    } else if (enableCrossTarget) {
        std::cout << "Mode: Cross-target monitoring (mapping + camera + general)" << std::endl;
    } else {
        std::cout << "Mode: Mapping-only monitoring" << std::endl;
    }
    std::cout << std::endl;

    // Create telemetry client
    TelemetryClient client("mapping_ui");

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
    client.setTelemetryCallback([enableAllTargets, enableCrossTarget](const std::string& topic, const std::vector<uint8_t>& data) {
        int count = g_packet_count.fetch_add(1) + 1;
        std::string uav_name = extractUAVName(topic);

        std::cout << std::endl;
        std::cout << "📍 [" << count << "] " << GetTimestamp() << " - " << topic << std::endl;
        std::cout << "   📦 Size: " << data.size() << " bytes" << std::endl;

        // Parse packet header
        const auto* header = TelemetryClient::parseHeader(data);
        if (header) {
            std::string target_name = TelemetryClient::getTargetName(header->targetID);
            std::cout << "   🎯 Target: " << target_name;

            // Show indicator if this is cross-target data
            if (enableAllTargets) {
                std::cout << " [ALL MODE]";
            } else if (enableCrossTarget && target_name != "mapping") {
                std::cout << " [CROSS-TARGET]";
            }
            std::cout << std::endl;

            std::cout << "   📋 Type: " << TelemetryClient::getPacketTypeName(header->packetType) << std::endl;

            // Display mapping-focused data
            switch (header->packetType) {
                case PacketTypes::LOCATION_PACKET:
                    displayMappingLocationData(data, uav_name);
                    break;
                case PacketTypes::STATUS_PACKET:
                    displayMappingStatusData(data);
                    break;
                default:
                    std::cout << "   📡 Other telemetry: " << data.size() << " bytes" << std::endl;
                    break;
            }
        } else {
            std::cout << "   ⚠️ Invalid packet header" << std::endl;
        }
    });

    // Connect to telemetry service
    std::cout << "Connecting to telemetry service..." << std::endl;
    bool connected = client.connectFromConfig("service_config.json", client_protocol);

    if (!connected) {
        std::cout << "Config connection failed, trying manual connection..." << std::endl;
        int port = (client_protocol == Protocol::TCP) ? 5556 : 5558;
        connected = client.connect("localhost", port, client_protocol);
    }

    if (!connected) {
        std::cerr << "❌ Failed to connect to telemetry service!" << std::endl;
        std::cerr << "Make sure the telemetry service is running." << std::endl;
        return 1;
    }

    // Wait a moment for connection to establish
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // Subscribe to mapping-focused telemetry data
    std::cout << std::endl;
    std::cout << "Subscribing to telemetry..." << std::endl;

    if (enableAllTargets) {
        // Subscribe to ALL telemetry data
        if (client.subscribe("telemetry.*")) {
            std::cout << "✅ Subscribed to ALL telemetry: telemetry.*" << std::endl;
        } else {
            std::cout << "❌ Failed to subscribe to all telemetry" << std::endl;
        }
    } else if (enableCrossTarget) {
        // Subscribe to mapping, camera, and general data
        if (client.subscribe("telemetry.*.mapping.*")) {
            std::cout << "✅ Subscribed to mapping telemetry: telemetry.*.mapping.*" << std::endl;
        } else {
            std::cout << "❌ Failed to subscribe to mapping telemetry" << std::endl;
        }

        if (client.subscribe("telemetry.*.camera.*")) {
            std::cout << "✅ Subscribed to camera telemetry: telemetry.*.camera.*" << std::endl;
        } else {
            std::cout << "❌ Failed to subscribe to camera telemetry" << std::endl;
        }

        if (client.subscribe("telemetry.*.general.*")) {
            std::cout << "✅ Subscribed to general telemetry: telemetry.*.general.*" << std::endl;
        } else {
            std::cout << "❌ Failed to subscribe to general telemetry" << std::endl;
        }

        // Subscribe to all location data (important for mapping)
        if (client.subscribe("telemetry.*.*.location")) {
            std::cout << "✅ Subscribed to all location data: telemetry.*.*.location" << std::endl;
        } else {
            std::cout << "❌ Failed to subscribe to location data" << std::endl;
        }

        // Subscribe to general status for mission tracking
        if (client.subscribe("telemetry.*.*.status")) {
            std::cout << "✅ Subscribed to status data: telemetry.*.*.status" << std::endl;
        } else {
            std::cout << "❌ Failed to subscribe to status data" << std::endl;
        }
    } else {
        // Default: Mapping data only
        if (client.subscribe("telemetry.*.mapping.*")) {
            std::cout << "✅ Subscribed to mapping telemetry: telemetry.*.mapping.*" << std::endl;
        } else {
            std::cout << "❌ Failed to subscribe to mapping telemetry" << std::endl;
        }

        // Subscribe to all location data (important for mapping)
        if (client.subscribe("telemetry.*.*.location")) {
            std::cout << "✅ Subscribed to all location data: telemetry.*.*.location" << std::endl;
        } else {
            std::cout << "❌ Failed to subscribe to location data" << std::endl;
        }

        // Subscribe to general status for mission tracking
        if (client.subscribe("telemetry.*.*.status")) {
            std::cout << "✅ Subscribed to status data: telemetry.*.*.status" << std::endl;
        } else {
            std::cout << "❌ Failed to subscribe to status data" << std::endl;
        }
    }

    std::cout << std::endl;
    if (enableAllTargets) {
        std::cout << "🗺️ Mapping UI ready - tracking ALL UAV telemetry systems..." << std::endl;
    } else if (enableCrossTarget) {
        std::cout << "🗺️ Mapping UI ready - tracking mapping, camera, and general systems..." << std::endl;
    } else {
        std::cout << "🗺️ Mapping UI ready - tracking UAV locations and missions..." << std::endl;
    }
    std::cout << "Press Ctrl+C to stop" << std::endl;
    std::cout << "===============================================" << std::endl;

    // Start command sender thread if enabled
    std::thread senderThread;
    if (enableSender && client_protocol == Protocol::TCP) {
        senderThread = std::thread([&client, target_uav]() {
            std::string line;
            std::cout << std::endl;
            std::cout << "📡 Command interface enabled for " << target_uav << std::endl;
            std::cout << "Sample commands: 'SET_WAYPOINT 41.0392 29.0352', 'GET_MISSION_STATUS', 'RETURN_HOME'" << std::endl;
            std::cout << "Type commands and press Enter (or Ctrl+C to exit):" << std::endl;

            while (g_running) {
                // Check if input is available without blocking
                fd_set readfds;
                FD_ZERO(&readfds);
                FD_SET(STDIN_FILENO, &readfds);

                struct timeval timeout;
                timeout.tv_sec = 0;
                timeout.tv_usec = 100000;  // 100ms timeout

                int result = select(STDIN_FILENO + 1, &readfds, nullptr, nullptr, &timeout);

                if (result > 0 && FD_ISSET(STDIN_FILENO, &readfds)) {
                    if (std::getline(std::cin, line)) {
                        if (!g_running) break;

                        if (!line.empty()) {
                            if (client.sendCommand(target_uav, line)) {
                                std::cout << "✅ [" << GetTimestamp() << "] Sent navigation command to " << target_uav << ": " << line << std::endl;
                            } else {
                                std::cout << "❌ [" << GetTimestamp() << "] Failed to send command: " << line << std::endl;
                            }
                        }
                    }
                } else if (result < 0) {
                    break;  // Error occurred
                }
            }
        });
    } else if (enableSender && client_protocol == Protocol::UDP) {
        std::cout << "⚠️ Command sending not supported with UDP protocol" << std::endl;
    }

    // Main loop - just wait for shutdown signal
    while (g_running && client.isConnected()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::cout << std::endl;
    std::cout << "🔄 Shutting down Mapping UI..." << std::endl;

    // Wait for sender thread to finish
    if (senderThread.joinable()) {
        senderThread.join();
    }

    // Disconnect from service
    client.disconnect();

    // Log shutdown reason if caused by signal
    int signal_num = g_signal_received.load();
    if (signal_num > 0) {
        std::cout << "Mapping UI shutdown initiated by signal: " << signal_num << std::endl;
    }

    std::cout << "🗺️ Mapping UI stopped. Total packets received: " << g_packet_count.load() << std::endl;
    return 0;
}

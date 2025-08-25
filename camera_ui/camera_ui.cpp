/**
 * @file camera_ui.cpp
 * @brief Camera UI application for receiving and displaying camera telemetry data
 *
 * This application uses the TelemetryClient library to connect to the telemetry service
 * and subscribe to camera-targeted telemetry data from UAVs. It demonstrates real-time
 * telemetry visualization and command sending capabilities.
 */

#include <sys/select.h>
#include <unistd.h>
#include <cstdlib>  // for setenv

#include <atomic>
#include <chrono>
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
 * @brief Parse and display location data from telemetry packet
 * @param data Raw telemetry data
 */
void displayLocationData(const std::vector<uint8_t>& data) {
    if (data.size() < sizeof(PacketHeader) + sizeof(double) * 2 + sizeof(float) * 3) {
        std::cout << "   📍 Location data (insufficient data)" << std::endl;
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

    std::cout << "   📍 Location: " << std::fixed << std::setprecision(6)
              << latitude << ", " << longitude
              << " | Alt: " << std::setprecision(1) << altitude << "m"
              << " | Heading: " << std::setprecision(0) << heading << "°"
              << " | Speed: " << std::setprecision(1) << speed << "m/s" << std::endl;
}

/**
 * @brief Parse and display status data from telemetry packet
 * @param data Raw telemetry data
 */
void displayStatusData(const std::vector<uint8_t>& data) {
    if (data.size() < sizeof(PacketHeader) + sizeof(uint8_t) * 2 + sizeof(uint16_t) + sizeof(float) * 2) {
        std::cout << "   🔋 Status data (insufficient data)" << std::endl;
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

    std::string health_str;
    switch (health) {
        case 0: health_str = "❌ Critical"; break;
        case 1: health_str = "⚠️ Warning"; break;
        case 2: health_str = "✅ Good"; break;
        case 3: health_str = "🟢 Excellent"; break;
        default: health_str = "❓ Unknown"; break;
    }

    std::string mission_str;
    switch (mission) {
        case 0: mission_str = "🏠 Idle"; break;
        case 1: mission_str = "🚁 Takeoff"; break;
        case 2: mission_str = "🎯 Mission"; break;
        case 3: mission_str = "🛬 Landing"; break;
        case 4: mission_str = "🚨 Emergency"; break;
        default: mission_str = "❓ Unknown"; break;
    }

    std::cout << "   🔋 Status: " << health_str << " | " << mission_str
              << " | Flight: " << flight_time << "s"
              << " | CPU: " << std::setprecision(1) << cpu_usage << "%"
              << " | Mem: " << memory_usage << "%" << std::endl;
}

/**
 * @brief Main function - Camera UI application entry point
 */
int main(int argc, char* argv[]) {
    // Set up signal handlers for graceful shutdown
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    // Parse command line arguments
    std::string protocol = "tcp";  // Default to TCP
    bool enableSender = false;
    bool enableAllTargets = false;  // Enable receiving all target types
    bool locationOnly = false;      // Subscribe only to location data
    bool statusOnly = false;        // Subscribe only to status data
    bool debugMode = false;         // Enable debug output
    bool interactiveMode = false;   // Enable interactive subscription management
    std::string target_uav;

    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "--protocol" && i + 1 < argc) {
            protocol = argv[++i];
        } else if (std::string(argv[i]) == "--send" && i + 1 < argc) {
            enableSender = true;
            target_uav = argv[++i];
        } else if (std::string(argv[i]) == "--all-targets") {
            enableAllTargets = true;
        } else if (std::string(argv[i]) == "--location-only") {
            locationOnly = true;
        } else if (std::string(argv[i]) == "--status-only") {
            statusOnly = true;
        } else if (std::string(argv[i]) == "--debug") {
            debugMode = true;
        } else if (std::string(argv[i]) == "--interactive") {
            interactiveMode = true;
        } else if (std::string(argv[i]) == "--help") {
            std::cout << "Camera UI - Telemetry Visualization\n";
            std::cout << "Usage: " << argv[0] << " [options]\n";
            std::cout << "Options:\n";
            std::cout << "  --protocol tcp|udp : Communication protocol (default: tcp)\n";
            std::cout << "  --send UAV_NAME    : Enable command sending to specified UAV\n";
            std::cout << "  --all-targets      : Subscribe to ALL target types (camera, mapping)\n";
            std::cout << "  --location-only    : Subscribe only to location data (telemetry.*.*.location)\n";
            std::cout << "  --status-only      : Subscribe only to status data (telemetry.*.*.status)\n";
            std::cout << "  --debug            : Enable debug output to see internal filtering\n";
            std::cout << "  --interactive      : Enable interactive subscription management\n";
            std::cout << "  --help             : Show this help message\n";
            std::cout << "\nSubscription modes:\n";
            std::cout << "  Default: Camera data only (telemetry.*.camera.*)\n";
            std::cout << "  --all-targets: All telemetry data from all targets\n";
            std::cout << "  --location-only: Location data from all UAVs (telemetry.*.*.location)\n";
            std::cout << "  --status-only: Status data from all UAVs (telemetry.*.*.status)\n";
            return 0;
        }
    }

    // Validate mutually exclusive options
    int modeCount = 0;
    if (enableAllTargets) modeCount++;
    if (locationOnly) modeCount++;
    if (statusOnly) modeCount++;

    if (modeCount > 1) {
        std::cerr << "Error: --all-targets, --location-only, and --status-only are mutually exclusive\n";
        return 1;
    }    // Validate protocol argument
    Protocol client_protocol;
    if (protocol == "tcp") {
        client_protocol = Protocol::TCP;
    } else if (protocol == "udp") {
        client_protocol = Protocol::UDP;
    } else {
        std::cerr << "Error: Protocol must be 'tcp' or 'udp'\n";
        return 1;
    }

    std::cout << "📸 =========================================== 📸" << std::endl;
    std::cout << "    Camera UI - UAV Telemetry Visualization" << std::endl;
    std::cout << "📸 =========================================== 📸" << std::endl;
    std::cout << "Protocol: " << protocol << std::endl;
    if (enableSender) {
        std::cout << "Command target: " << target_uav << std::endl;
    }
    if (enableAllTargets) {
        std::cout << "Mode: Monitoring ALL target types (camera, mapping)" << std::endl;
    } else if (locationOnly) {
        std::cout << "Mode: Location data only from all UAVs and targets" << std::endl;
    } else if (statusOnly) {
        std::cout << "Mode: Status data only from all UAVs and targets" << std::endl;
    } else {
        std::cout << "Mode: Camera-only monitoring" << std::endl;
    }
    if (debugMode) {
        std::cout << "Debug: Enabled (will show internal filtering messages)" << std::endl;
    }
    std::cout << std::endl;

    // Enable debug mode if requested
    if (debugMode) {
        setenv("TELEMETRY_DEBUG", "1", 1);  // Set environment variable for library
    }

    // Create telemetry client
    TelemetryClient client("camera_ui");

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
    client.setTelemetryCallback([enableAllTargets, locationOnly, statusOnly](const std::string& topic, const std::vector<uint8_t>& data) {
        int count = g_packet_count.fetch_add(1) + 1;

        std::cout << std::endl;
        std::cout << "📡 [" << count << "] " << GetTimestamp() << " - " << topic << std::endl;
        std::cout << "   📦 Size: " << data.size() << " bytes" << std::endl;

        // Parse packet header
        const auto* header = TelemetryClient::parseHeader(data);
        if (header) {
            std::string target_name = TelemetryClient::getTargetName(header->targetID);
            std::cout << "   🎯 Target: " << target_name;

            // Show indicator based on monitoring mode
            if (enableAllTargets) {
                std::cout << " [ALL MODE]";
            } else if (locationOnly) {
                std::cout << " [LOCATION-ONLY]";
            } else if (statusOnly) {
                std::cout << " [STATUS-ONLY]";
            }
            std::cout << std::endl;

            std::cout << "   📋 Type: " << TelemetryClient::getPacketTypeName(header->packetType) << std::endl;

            // Display specific data based on packet type
            switch (header->packetType) {
                case PacketTypes::LOCATION:
                    displayLocationData(data);
                    break;
                case PacketTypes::STATUS:
                    displayStatusData(data);
                    break;
                default:
                    std::cout << "   📊 Packet data: " << data.size() << " bytes" << std::endl;
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

    // Subscribe to telemetry data based on selected mode
    std::cout << std::endl;
    std::cout << "Subscribing to telemetry..." << std::endl;

    if (locationOnly) {
        // Subscribe only to location data from all UAVs and targets
        if (client.subscribe("telemetry.*.*.location")) {
            std::cout << "✅ Subscribed to location data: telemetry.*.*.location" << std::endl;
        } else {
            std::cout << "❌ Failed to subscribe to location data" << std::endl;
        }
    } else if (statusOnly) {
        // Subscribe only to status data from all UAVs and targets
        if (client.subscribe("telemetry.*.*.status")) {
            std::cout << "✅ Subscribed to status data: telemetry.*.*.status" << std::endl;
        } else {
            std::cout << "❌ Failed to subscribe to status data" << std::endl;
        }
    } else if (enableAllTargets) {
        // Subscribe to ALL telemetry data
        if (client.subscribe("telemetry.*")) {
            std::cout << "✅ Subscribed to ALL telemetry: telemetry.*" << std::endl;
        } else {
            std::cout << "❌ Failed to subscribe to all telemetry" << std::endl;
        }
    } else {
        // Default: Camera data only (all data types from camera target)
        if (client.subscribe("telemetry.*.camera.*")) {
            std::cout << "✅ Subscribed to camera telemetry: telemetry.*.camera.*" << std::endl;
        } else {
            std::cout << "❌ Failed to subscribe to camera telemetry" << std::endl;
        }
    }

    std::cout << std::endl;
    if (locationOnly) {
        std::cout << "🎥 Camera UI ready - monitoring location data from all UAVs..." << std::endl;
    } else if (statusOnly) {
        std::cout << "🎥 Camera UI ready - monitoring status data from all UAVs..." << std::endl;
    } else if (enableAllTargets) {
        std::cout << "🎥 Camera UI ready - monitoring ALL UAV telemetry systems..." << std::endl;
    } else {
        std::cout << "🎥 Camera UI ready - monitoring UAV camera systems..." << std::endl;
    }
    std::cout << "Press Ctrl+C to stop" << std::endl;
    std::cout << "===============================================" << std::endl;

    // Start command sender thread if enabled
    std::thread senderThread;
    std::thread subscriptionThread;
    if (enableSender && client_protocol == Protocol::TCP) {
        senderThread = std::thread([&client, target_uav]() {
            std::string line;
            std::cout << std::endl;
            std::cout << "📡 Command interface enabled for " << target_uav << std::endl;
            std::cout << "Type commands and press Enter (or Ctrl+C to exit):" << std::endl;
            std::cout << "Special commands:" << std::endl;
            std::cout << "  sub <topic>   - Subscribe to topic (e.g., 'sub telemetry.*.mapping.*')" << std::endl;
            std::cout << "  unsub <topic> - Unsubscribe from topic" << std::endl;
            std::cout << "  list          - List current subscriptions" << std::endl;
            std::cout << "UAV commands: CAPTURE_PHOTO, START_RECORDING, etc." << std::endl;

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
                            // Check for subscription management commands
                            std::istringstream iss(line);
                            std::string command, topic;
                            iss >> command >> topic;

                            if (command == "sub" && !topic.empty()) {
                                if (client.subscribe(topic)) {
                                    std::cout << "✅ [" << GetTimestamp() << "] Subscribed to: " << topic << std::endl;
                                } else {
                                    std::cout << "❌ [" << GetTimestamp() << "] Failed to subscribe to: " << topic << std::endl;
                                }
                            } else if (command == "unsub" && !topic.empty()) {
                                if (client.unsubscribe(topic)) {
                                    std::cout << "✅ [" << GetTimestamp() << "] Unsubscribed from: " << topic << std::endl;
                                } else {
                                    std::cout << "❌ [" << GetTimestamp() << "] Failed to unsubscribe from: " << topic << std::endl;
                                }
                            } else if (command == "list") {
                                std::cout << "📋 [" << GetTimestamp() << "] Current subscriptions:" << std::endl;
                                std::cout << "   (Note: Use --debug to see internal subscription details)" << std::endl;
                            } else {
                                // Regular UAV command
                                if (client.sendCommand(target_uav, line)) {
                                    std::cout << "✅ [" << GetTimestamp() << "] Sent command to " << target_uav << ": " << line << std::endl;
                                } else {
                                    std::cout << "❌ [" << GetTimestamp() << "] Failed to send command: " << line << std::endl;
                                }
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

    // Start interactive subscription management thread if enabled (works with any protocol)
    if (interactiveMode && !enableSender) {
        subscriptionThread = std::thread([&client]() {
            std::string line;
            std::cout << std::endl;
            std::cout << "📋 Interactive subscription management enabled" << std::endl;
            std::cout << "Commands:" << std::endl;
            std::cout << "  sub <topic>   - Subscribe to topic (e.g., 'sub telemetry.*.mapping.*')" << std::endl;
            std::cout << "  unsub <topic> - Unsubscribe from topic" << std::endl;
            std::cout << "  list          - List current subscriptions" << std::endl;
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
                            std::istringstream iss(line);
                            std::string command, topic;
                            iss >> command >> topic;

                            if (command == "sub" && !topic.empty()) {
                                if (client.subscribe(topic)) {
                                    std::cout << "✅ [" << GetTimestamp() << "] Subscribed to: " << topic << std::endl;
                                } else {
                                    std::cout << "❌ [" << GetTimestamp() << "] Failed to subscribe to: " << topic << std::endl;
                                }
                            } else if (command == "unsub" && !topic.empty()) {
                                if (client.unsubscribe(topic)) {
                                    std::cout << "✅ [" << GetTimestamp() << "] Unsubscribed from: " << topic << std::endl;
                                } else {
                                    std::cout << "❌ [" << GetTimestamp() << "] Failed to unsubscribe from: " << topic << std::endl;
                                }
                            } else if (command == "list") {
                                std::cout << "📋 [" << GetTimestamp() << "] Current subscriptions:" << std::endl;
                                std::cout << "   (Note: Use --debug to see internal subscription details)" << std::endl;
                            } else {
                                std::cout << "❓ Unknown command. Use: sub <topic>, unsub <topic>, or list" << std::endl;
                            }
                        }
                    }
                } else if (result < 0) {
                    break;  // Error occurred
                }
            }
        });
    }

    // Main loop - just wait for shutdown signal
    while (g_running && client.isConnected()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::cout << std::endl;
    std::cout << "🔄 Shutting down Camera UI..." << std::endl;

    // Wait for threads to finish
    if (senderThread.joinable()) {
        senderThread.join();
    }
    if (subscriptionThread.joinable()) {
        subscriptionThread.join();
    }

    // Disconnect from service
    client.disconnect();

    // Log shutdown reason if caused by signal
    int signal_num = g_signal_received.load();
    if (signal_num > 0) {
        std::cout << "Camera UI shutdown initiated by signal: " << signal_num << std::endl;
    }

    std::cout << "📸 Camera UI stopped. Total packets received: " << g_packet_count.load() << std::endl;
    return 0;
}

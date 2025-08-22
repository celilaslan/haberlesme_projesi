/**
 * @file camera_ui.cpp
 * @brief Camera UI application for receiving and displaying camera telemetry data
 *
 * This application uses the TelemetryClient library to connect to the telemetry service
 * and subscribe to camera-related telemetry data from UAVs. It supports both TCP and UDP
 * protocols via the simplified library API and can send commands back to UAVs.
 */

#include <sys/select.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>

#include "TelemetryClient.h"

// Binary packet parsing structures
#pragma pack(push, 1)
struct PacketHeader {
    uint8_t targetID;        ///< Primary target (1: Camera, 2: Mapping, 3: General)
    uint8_t packetType;      ///< Packet type (4: Location, 5: Status, 6: IMU, 7: Battery)
};

struct StatusPayload {
    uint8_t systemHealth;    ///< System health (0: Critical, 1: Warning, 2: Good, 3: Excellent)
    uint8_t missionState;    ///< Mission state (0: Idle, 1: Takeoff, 2: Mission, 3: Landing, 4: Emergency)
    uint16_t flightTime;     ///< Flight time in seconds
    float cpuUsage;          ///< CPU usage percentage (0.0-100.0)
    float memoryUsage;       ///< Memory usage percentage (0.0-100.0)
};
#pragma pack(pop)

// Packet type constants
constexpr uint8_t PACKET_TYPE_STATUS = 5;

using namespace TelemetryAPI;

/**
 * @brief Convert binary data to hexadecimal string representation
 * @param data The binary data to convert
 * @return Hexadecimal string representation
 */
std::string toHexString(const std::string& data) {
    std::stringstream ss;
    for (size_t i = 0; i < data.size(); ++i) {
        if (i > 0) ss << " ";
        ss << std::hex << std::setw(2) << std::setfill('0') << std::uppercase
           << static_cast<unsigned int>(static_cast<unsigned char>(data[i]));
    }
    return ss.str();
}

/**
 * @brief Parse and format PacketHeader information
 * @param raw_data The raw binary data
 * @return Formatted string with header information
 */
std::string parsePacketHeaderInfo(const std::string& raw_data) {
    if (raw_data.size() < sizeof(PacketHeader)) {
        return "Invalid header size";
    }

    const PacketHeader* header = reinterpret_cast<const PacketHeader*>(raw_data.data());

    std::stringstream ss;
    ss << "Target:" << static_cast<int>(header->targetID)
       << " Type:" << static_cast<int>(header->packetType);

    return ss.str();
}/**
 * @brief Parse binary status packet from raw data
 * @param raw_data The raw binary data received
 * @param health Reference to store parsed system health
 * @param mission Reference to store parsed mission state
 * @param flight_time Reference to store parsed flight time
 * @param cpu_usage Reference to store parsed CPU usage
 * @param memory_usage Reference to store parsed memory usage
 * @return True if parsing was successful, false otherwise
 */
bool parseBinaryStatusPacket(const std::string& raw_data, uint8_t& health, uint8_t& mission,
                            uint16_t& flight_time, float& cpu_usage, float& memory_usage) {
    // Check if we have enough data for header + status payload
    if (raw_data.size() < sizeof(PacketHeader) + sizeof(StatusPayload)) {
        return false;
    }

    // Parse packet header
    const PacketHeader* header = reinterpret_cast<const PacketHeader*>(raw_data.data());

    // Verify this is a status packet
    if (header->packetType != PACKET_TYPE_STATUS) {
        return false;
    }

    // Parse status payload
    const StatusPayload* status = reinterpret_cast<const StatusPayload*>(
        raw_data.data() + sizeof(PacketHeader));

    health = status->systemHealth;
    mission = status->missionState;
    flight_time = status->flightTime;
    cpu_usage = status->cpuUsage;
    memory_usage = status->memoryUsage;

    return true;
}

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
 * This function is called by the TelemetryClient whenever telemetry data
 * is received. It handles multiple data types and displays them appropriately.
 */
void onTelemetryReceived(const TelemetryData& data) {
    // Display all types of data now (camera focus but flexible)
    std::string protocol_str = (data.received_via == Protocol::TCP_ONLY)   ? "TCP"
                               : (data.received_via == Protocol::UDP_ONLY) ? "UDP"
                                                                           : "MIXED";

    std::string data_type_str;
    switch (data.data_type) {
        case DataType::CAMERA: data_type_str = "CAMERA"; break;
        case DataType::MAPPING: data_type_str = "MAPPING"; break;
        case DataType::LOCATION: data_type_str = "LOCATION"; break;
        case DataType::STATUS: data_type_str = "STATUS"; break;
        case DataType::IMU: data_type_str = "IMU"; break;
        case DataType::BATTERY: data_type_str = "BATTERY"; break;
        case DataType::GENERAL: data_type_str = "GENERAL"; break;
        default: data_type_str = "UNKNOWN"; break;
    }

    std::cout << "[" << GetTimestamp() << "] " << "UAV: " << data.uav_name << " | " 
              << "Type: " << data_type_str << " | " << "Protocol: " << protocol_str;

    // Try to parse binary status data for STATUS packets
    uint8_t health, mission;
    uint16_t flight_time;
    float cpu_usage, memory_usage;

    if (data.data_type == DataType::STATUS || data.data_type == DataType::CAMERA) {
        if (parseBinaryStatusPacket(data.raw_data, health, mission, flight_time, cpu_usage, memory_usage)) {
            // Display parsed status data
            std::string health_str;
            switch (health) {
                case 0: health_str = "CRITICAL"; break;
                case 1: health_str = "WARNING"; break;
                case 2: health_str = "GOOD"; break;
                case 3: health_str = "EXCELLENT"; break;
                default: health_str = "UNKNOWN"; break;
            }

            std::string mission_str;
            switch (mission) {
                case 0: mission_str = "IDLE"; break;
                case 1: mission_str = "TAKEOFF"; break;
                case 2: mission_str = "MISSION"; break;
                case 3: mission_str = "LANDING"; break;
                case 4: mission_str = "EMERGENCY"; break;
                default: mission_str = "UNKNOWN"; break;
            }

            std::cout << " | Health: " << health_str
                      << " | Mission: " << mission_str
                      << " | Flight Time: " << flight_time << "s"
                      << " | CPU: " << std::fixed << std::setprecision(1) << cpu_usage << "%"
                      << " | Memory: " << std::fixed << std::setprecision(1) << memory_usage << "%"
                      << " | " << parsePacketHeaderInfo(data.raw_data)
                      << " | Raw size: " << data.raw_data.size() << " bytes"
                      << " | Hex: " << toHexString(data.raw_data);
        } else {
            // Fallback for non-status packets or parsing errors
            std::cout << " | " << parsePacketHeaderInfo(data.raw_data)
                      << " | Raw size: " << data.raw_data.size() << " bytes"
                      << " | Hex: " << toHexString(data.raw_data);
        }
    } else {
        // For other data types, just show the raw packet info
        std::cout << " | " << parsePacketHeaderInfo(data.raw_data)
                  << " | Raw size: " << data.raw_data.size() << " bytes"
                  << " | Hex: " << toHexString(data.raw_data);
    }

    std::cout << std::endl;
}

/**
 * @brief Error callback function for telemetry client
 * @param error_message Description of the error that occurred
 */
void onTelemetryError(const std::string& error_message) {
    std::cerr << "[Camera UI Error] " << error_message << std::endl;
}

/**
 * @brief Main function - Camera UI application entry point
 * @param argc Number of command line arguments
 * @param argv Array of command line argument strings
 * @return Exit code (0 for success, non-zero for error)
 *
 * Usage: ./camera_ui [--protocol tcp|udp|both] [--send UAV_NAME] [--uav UAV_NAME]
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
    bool monitor_all = false;
    bool include_status = false;
    bool include_battery = false;

    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "--protocol" && i + 1 < argc) {
            protocol = argv[++i];
        } else if (std::string(argv[i]) == "--send" && i + 1 < argc) {
            enableSender = true;
            target = argv[++i];
        } else if (std::string(argv[i]) == "--uav" && i + 1 < argc) {
            filter_uav = argv[++i];
        } else if (std::string(argv[i]) == "--monitor-all") {
            monitor_all = true;
        } else if (std::string(argv[i]) == "--include-status") {
            include_status = true;
        } else if (std::string(argv[i]) == "--include-battery") {
            include_battery = true;
        } else if (std::string(argv[i]) == "--help") {
            std::cout << "Camera UI - Telemetry Client Library Demo\n";
            std::cout << "Usage: " << argv[0] << " [options]\n";
            std::cout << "Options:\n";
            std::cout << "  --protocol tcp|udp|both : Communication protocol (default: both)\n";
            std::cout << "  --send UAV_NAME         : Enable command sending to specified UAV\n";
            std::cout << "  --uav UAV_NAME          : Filter telemetry to specific UAV only\n";
            std::cout << "  --monitor-all           : Monitor all UAVs and data types\n";
            std::cout << "  --include-status        : Also receive STATUS data from all UAVs\n";
            std::cout << "  --include-battery       : Also receive BATTERY data from all UAVs\n";
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

    std::cout << "=== Camera UI - Using TelemetryClient Library ===\n";
    std::cout << "Protocol: " << protocol << "\n";
    if (monitor_all) {
        std::cout << "Mode: Monitoring all UAVs and data types\n";
    } else {
        std::cout << "Mode: Camera data focus";
        if (include_status) std::cout << " + STATUS data";
        if (include_battery) std::cout << " + BATTERY data";
        std::cout << "\n";
    }
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

    // Configure subscriptions based on user preferences
    if (monitor_all) {
        // Monitor all UAVs and all data types
        if (!client.subscribeToAllUAVs(DataType::ALL)) {
            std::cerr << "Warning: Failed to subscribe to all UAVs\n";
        } else {
            std::cout << "✓ Monitoring all UAVs and data types\n";
        }
    } else {
        // Primary subscription to camera data
        if (!client.subscribeToDataType(DataType::CAMERA)) {
            std::cerr << "Warning: Failed to subscribe to camera data type\n";
        } else {
            std::cout << "✓ Subscribed to CAMERA data\n";
        }

        // Additional cross-subscriptions for richer monitoring
        std::vector<DataType> additional_types;
        if (include_status) {
            additional_types.push_back(DataType::STATUS);
        }
        if (include_battery) {
            additional_types.push_back(DataType::BATTERY);
        }

        if (!additional_types.empty()) {
            if (!client.crossSubscribe(DataType::CAMERA, additional_types)) {
                std::cerr << "Warning: Failed to set up cross-subscriptions\n";
            } else {
                std::cout << "✓ Cross-subscriptions configured for: ";
                for (size_t i = 0; i < additional_types.size(); ++i) {
                    if (additional_types[i] == DataType::STATUS) std::cout << "STATUS";
                    else if (additional_types[i] == DataType::BATTERY) std::cout << "BATTERY";
                    if (i < additional_types.size() - 1) std::cout << ", ";
                }
                std::cout << "\n";
            }
        }
    }

    // If filtering by specific UAV, subscribe to it
    if (!filter_uav.empty()) {
        if (monitor_all) {
            // For monitor mode, get all data from this UAV
            if (!client.subscribeToAllDataFromUAV(filter_uav)) {
                std::cerr << "Warning: Failed to subscribe to all data from UAV " << filter_uav << "\n";
            } else {
                std::cout << "✓ Monitoring all data from " << filter_uav << "\n";
            }
        } else {
            // For normal mode, apply UAV filter to camera data
            if (!client.subscribeToUAV(filter_uav, DataType::CAMERA)) {
                std::cerr << "Warning: Failed to subscribe to UAV " << filter_uav << "\n";
            } else {
                std::cout << "✓ Filtering camera data from " << filter_uav << "\n";
            }
        }
    }

    std::cout << client.getConnectionStatus() << "\n\n";

    // Start command sender thread if enabled
    std::thread senderThread;
    if (enableSender) {
        senderThread = std::thread([&client, target]() {
            std::string line;
            std::cout << "[Camera UI] Type commands for " << target << " (press Enter to send, Ctrl+C to exit):\n";

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

                        if (client.sendCommand(target, line, "camera-ui")) {
                            std::cout << "[Camera UI] Sent command: " << line << std::endl;
                        } else {
                            std::cerr << "[Camera UI] Failed to send command: " << line << std::endl;
                        }
                    }
                } else if (result < 0) {
                    break;  // Error occurred
                }
            }
        });
    }

    std::cout << "Listening for camera telemetry data... (Press Ctrl+C to stop)\n";
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
        std::cout << "Camera UI shutdown initiated by signal: " << signal_num << std::endl;
    }

    std::cout << "Camera UI stopped.\n";
    return 0;
}

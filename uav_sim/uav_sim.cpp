/**
 * @file uav_sim.cpp
 * @brief UAV simulator for testing telemetry communication
 *
 * This file contains a UAV simulator that generates and sends telemetry data
 * to the telemetry service. It supports both TCP and UDP protocols
 * and can also receive commands from UI components via TCP.
 */

#include <atomic>
#include <boost/asio.hpp>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>
#include <system_error>
#include <thread>
#include <vector>
#include <zmq.hpp>

// Simple raw telemetry structures for UAV to send
#pragma pack(push, 1)

// Target IDs - must match what service expects
enum TargetIDs : uint8_t { CAMERA = 1, MAPPING = 2 };

// Packet Types - must match what service expects
enum PacketTypes : uint8_t { LOCATION = 4, STATUS = 5, IMU_PACKET = 6, BATTERY_PACKET = 7 };

// Packet header structure (must match service)
struct UAVPacketHeader {
    uint8_t targetID;        ///< Primary target (1: Camera, 2: Mapping)
    uint8_t packetType;      ///< Packet type (4: Location, 5: Status, 6: IMU, 7: Battery)
};

// Simple payload structures
struct UAVLocationPayload {
    double latitude;   ///< Latitude in decimal degrees
    double longitude;  ///< Longitude in decimal degrees
    float altitude;    ///< Altitude in meters above sea level
    float heading;     ///< Heading in degrees (0-359)
    float speed;       ///< Ground speed in m/s
};

struct UAVStatusPayload {
    uint8_t systemHealth;  ///< System health (0: Critical, 1: Warning, 2: Good, 3: Excellent)
    uint8_t missionState;  ///< Mission state (0: Idle, 1: Takeoff, 2: Mission, 3: Landing, 4: Emergency)
    uint16_t flightTime;   ///< Flight time in seconds
    float cpuUsage;        ///< CPU usage percentage (0.0-100.0)
    float memoryUsage;     ///< Memory usage percentage (0.0-100.0)
};

// Complete packet structures
struct UAVLocationPacket {
    UAVPacketHeader header;
    UAVLocationPayload payload;
};

struct UAVStatusPacket {
    UAVPacketHeader header;
    UAVStatusPayload payload;
};

#pragma pack(pop)

// Helper functions to create packets
UAVLocationPacket createLocationPacket(uint8_t targetID, double lat, double lon, float alt, float heading,
                                       float speed) {
    UAVLocationPacket packet = {};
    packet.header.targetID = targetID;
    packet.header.packetType = LOCATION;

    packet.payload.latitude = lat;
    packet.payload.longitude = lon;
    packet.payload.altitude = alt;
    packet.payload.heading = heading;
    packet.payload.speed = speed;

    return packet;
}

UAVStatusPacket createStatusPacket(uint8_t targetID, uint8_t health, uint8_t mission, uint16_t flightTime, float cpu,
                                   float memory) {
    UAVStatusPacket packet = {};
    packet.header.targetID = targetID;
    packet.header.packetType = STATUS;

    packet.payload.systemHealth = health;
    packet.payload.missionState = mission;
    packet.payload.flightTime = flightTime;
    packet.payload.cpuUsage = cpu;
    packet.payload.memoryUsage = memory;

    return packet;
}

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

using json = nlohmann::json;
using boost::asio::ip::udp;

// Global flag to control all threads
// NOLINT: Global variables needed for signal handler communication
std::atomic<bool> g_running(true);      // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
std::atomic<int> g_signal_received(0);  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

// Configuration constants
namespace {
    constexpr int default_telemetry_iterations = 50;
    constexpr int base_sleep_interval_ms = 500;
    constexpr int data_send_interval_ms = 100;
    constexpr int command_poll_interval_ms = 10;
}  // namespace

/**
 * @struct UAVConfig
 * @brief Configuration data for a single UAV simulator
 *
 * Contains all the network configuration needed for the UAV simulator to
 * connect to the telemetry service using either TCP or UDP protocols.
 */
struct UAVConfig {
    std::string name;           ///< UAV identifier (e.g., "UAV_1")
    std::string ip;             ///< IP address of the telemetry service
    int tcp_telemetry_port{0};  ///< TCP port for sending telemetry data
    int tcp_command_port{0};    ///< TCP port for receiving commands
    int udp_telemetry_port{0};  ///< UDP port for sending telemetry data (service-side port)
};

/**
 * @brief Generate a formatted timestamp string with millisecond precision
 * @return Timestamp string in format "YYYY-MM-DD HH:MM:SS.mmm"
 *
 * Uses platform-specific time conversion functions for thread safety.
 * Used for logging and debugging output.
 */
std::string getTimestamp() {
    const auto now = std::chrono::system_clock::now();
    const auto time_t_now = std::chrono::system_clock::to_time_t(now);
    const auto duration = now.time_since_epoch();
    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(duration) % 1000;

    std::ostringstream oss;
    struct tm time_info {};

    // Use platform-specific thread-safe time conversion
#if defined(_WIN32)
    localtime_s(&time_info, &time_t_now);
#else
    localtime_r(&time_t_now, &time_info);
#endif

    oss << std::put_time(&time_info, "%Y-%m-%d %H:%M:%S");
    oss << '.' << std::setfill('0') << std::setw(3) << milliseconds.count();

    return oss.str();
}

/**
 * @brief Signal handler for graceful shutdown
 * @param signum The signal number received
 *
 * Sets the global running flag to false, which causes all threads
 * to exit their main loops and terminate gracefully.
 * Uses only async-signal-safe operations.
 */
void signalHandler(int signum) {
    g_signal_received.store(signum);
    g_running.store(false);

    // Use async-signal-safe write instead of std::cout
    const char* msg = "Signal received. Shutting down...\n";
    ssize_t result = write(STDERR_FILENO, msg, strlen(msg));
    (void)result;  // Suppress unused variable warning
}

/**
 * @brief Load UAV configuration from JSON file
 * @param config_file Path to the JSON configuration file
 * @param uav_name Name of the UAV to load configuration for
 * @return UAVConfig structure with the loaded configuration
 *
 * Parses the JSON configuration file and extracts settings for the
 * specified UAV. Throws exceptions for file access or parsing errors.
 */
UAVConfig loadUavConfig(const std::string& config_file, const std::string& uav_name) {
    std::ifstream file(config_file);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open config file: " + config_file +
                                 "\nMake sure the file exists in the project root directory!");
    }

    json json_data;
    try {
        file >> json_data;
    } catch (const json::exception& e) {
        file.close();
        throw std::runtime_error("Invalid JSON in config file: " + std::string(e.what()));
    }
    file.close();

    UAVConfig config;
    bool uav_found = false;

    // Search for the specified UAV in the configuration
    if (!json_data.contains("uavs") || !json_data["uavs"].is_array()) {
        throw std::runtime_error("Config file missing 'uavs' array");
    }

    for (const auto& uav_json : json_data["uavs"]) {
        if (!uav_json.contains("name") || !uav_json["name"].is_string()) {
            continue;  // Skip invalid entries
        }

        if (uav_json["name"] == uav_name) {
            try {
                config.name = uav_json["name"];
                config.ip = uav_json.value("ip", "localhost");

                // Extract TCP port configuration with validation
                config.tcp_telemetry_port = uav_json.at("tcp_telemetry_port");
                config.tcp_command_port = uav_json.at("tcp_command_port");
                config.udp_telemetry_port = uav_json.at("udp_telemetry_port");

                uav_found = true;
                break;
            } catch (const json::exception& e) {
                throw std::runtime_error("Invalid configuration for UAV '" + uav_name + "': " + std::string(e.what()));
            }
        }
    }

    if (!uav_found) {
        throw std::runtime_error("UAV '" + uav_name + "' not found in config file!");
    }

    return config;
}

/**
 * @brief Print a list of available UAVs from the configuration file
 * @param config_file Path to the JSON configuration file
 *
 * Used to help users identify valid UAV names when they don't provide
 * command line arguments or specify an invalid UAV name.
 */
void printAvailableUaVs(const std::string& config_file) {
    try {
        std::ifstream file(config_file);
        if (!file.is_open()) {
            std::cerr << "Cannot open config file: " << config_file << '\n';
            return;
        }

        json json_data;
        file >> json_data;
        file.close();

        if (!json_data.contains("uavs") || !json_data["uavs"].is_array()) {
            std::cerr << "Invalid config file format\n";
            return;
        }

        std::cout << "Available UAVs in " << config_file << ":\n";
        for (const auto& uav_json : json_data["uavs"]) {
            if (!uav_json.contains("name")) continue;

            try {
                int tcp_telemetry_port = uav_json.at("tcp_telemetry_port");
                int tcp_command_port = uav_json.at("tcp_command_port");
                int udp_telemetry_port = uav_json.at("udp_telemetry_port");

                std::cout << "  - " << uav_json["name"] << " (TCP Telemetry: " << tcp_telemetry_port
                          << ", TCP Commands: " << tcp_command_port << ", UDP Telemetry: " << udp_telemetry_port << ")"
                          << '\n';
            } catch (const json::exception&) {
                std::cout << "  - " << uav_json["name"] << " (invalid configuration)\n";
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Error reading config file: " << e.what() << '\n';
    }
}

/**
 * @brief Main entry point for the UAV simulator
 * @param argc Number of command line arguments
 * @param argv Array of command line argument strings
 * @return Exit code (0 for success, 1 for error)
 *
 * Usage: ./uav_sim <UAV_NAME> --protocol <tcp|udp>
 *
 * This function:
 * 1. Parses command line arguments (UAV name and protocol - both required)
 * 2. Loads configuration for the specified UAV
 * 3. Starts telemetry generation thread
 * 4. Starts command receiver thread (TCP only)
 * 5. Waits for threads to complete
 */
int main(int argc, char* argv[]) {
    // Set up signal handlers for graceful shutdown
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    // Convert argv to vector for safer access
    std::vector<std::string> args;
    args.reserve(static_cast<size_t>(argc));
    for (int i = 0; i < argc; ++i) {
        args.emplace_back(argv[i]);  // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    }

    // Check command line arguments
    if (argc < 2) {
        std::cout << "Usage: " << args[0] << " <UAV_NAME> [--protocol <tcp|udp|both>]" << '\n';
        std::cout << "Examples:" << '\n';
        std::cout << "  " << args[0] << " UAV_1                    # Use both protocols (default)" << '\n';
        std::cout << "  " << args[0] << " UAV_1 --protocol tcp     # TCP only (for debugging)" << '\n';
        std::cout << "  " << args[0] << " UAV_1 --protocol udp     # UDP only (for debugging)" << '\n';
        std::cout << "  " << args[0] << " UAV_1 --protocol both    # Both protocols (explicit)" << '\n';
        std::cout << '\n';

        // Display available UAVs from configuration using proper path resolution
        auto get_executable_dir_for_usage = []() -> std::string {
            try {
#if defined(_WIN32)
                char path[MAX_PATH];
                DWORD len = GetModuleFileNameA(nullptr, path, MAX_PATH);
                if (len == 0 || len == MAX_PATH) return std::filesystem::current_path().string();
                return std::filesystem::path(path).parent_path().string();
#else
                std::array<char, 4096> buf{};
                ssize_t len = readlink("/proc/self/exe", buf.data(), buf.size() - 1);
                if (len == -1) return std::filesystem::current_path().string();
                buf.at(static_cast<size_t>(len)) = '\0';
                return std::filesystem::path(buf.data()).parent_path().string();
#endif
            } catch (const std::exception&) {
                return std::filesystem::current_path().string();
            }
        };

        auto resolve_config_path_for_usage = [&]() -> std::string {
            try {
                // Check environment variable first
                if (const char* env = std::getenv("SERVICE_CONFIG")) {
                    std::error_code error_code;
                    if (std::filesystem::exists(env, error_code) && !error_code) return {env};
                }

                // Try multiple candidate locations
                std::vector<std::filesystem::path> candidates;
                candidates.emplace_back("service_config.json");

                std::filesystem::path exe_dir = get_executable_dir_for_usage();
                candidates.push_back(exe_dir / "service_config.json");
                candidates.push_back(exe_dir.parent_path() / "service_config.json");

                for (auto& path : candidates) {
                    std::error_code error_code;
                    if (std::filesystem::exists(path, error_code) && !error_code) return path.string();
                }
            } catch (const std::exception&) {
                // Fall through to default
            }
            return {"service_config.json"};
        };

        std::string config_path = resolve_config_path_for_usage();
        printAvailableUaVs(config_path);
        return 1;
    }

    // Parse command line arguments
    std::string uav_name = args[1];
    std::string protocol = "both";  // Default to both protocols

    if (argc >= 4 && args[2] == "--protocol") {
        protocol = args[3];
    }

    if (protocol != "tcp" && protocol != "udp" && protocol != "both") {
        std::cerr << "Error: Protocol must be 'tcp', 'udp', or 'both'" << '\n';
        return 1;
    }

    /**
     * @brief Lambda function to get the directory containing the executable
     * @return Path to the executable directory
     *
     * Platform-specific implementation for finding the executable path.
     */
    auto get_executable_dir = []() -> std::string {
        try {
#if defined(_WIN32)
            char path[MAX_PATH];
            DWORD len = GetModuleFileNameA(nullptr, path, MAX_PATH);
            if (len == 0 || len == MAX_PATH) return std::filesystem::current_path().string();
            return std::filesystem::path(path).parent_path().string();
#else
            std::array<char, 4096> buf{};
            ssize_t len = readlink("/proc/self/exe", buf.data(), buf.size() - 1);
            if (len == -1) return std::filesystem::current_path().string();
            buf.at(static_cast<size_t>(len)) = '\0';
            return std::filesystem::path(buf.data()).parent_path().string();
#endif
        } catch (const std::exception&) {
            // Intentionally ignore filesystem errors and return current directory as fallback
            return std::filesystem::current_path().string();
        }
    };

    /**
     * @brief Lambda function to resolve the configuration file path
     * @return Full path to the configuration file
     *
     * Checks multiple locations for the config file:
     * 1. SERVICE_CONFIG environment variable
     * 2. Current directory
     * 3. Executable directory
     * 4. Parent of executable directory
     */
    auto resolve_config_path = [&]() -> std::string {
        try {
            // Check environment variable first
            if (const char* env = std::getenv("SERVICE_CONFIG")) {
                std::error_code error_code;
                if (std::filesystem::exists(env, error_code) && !error_code) return {env};
            }

            // Try multiple candidate locations
            std::vector<std::filesystem::path> candidates;
            candidates.emplace_back("service_config.json");

            std::filesystem::path exe_dir = get_executable_dir();
            candidates.push_back(exe_dir / "service_config.json");
            candidates.push_back(exe_dir.parent_path() / "service_config.json");

            for (auto& path : candidates) {
                std::error_code error_code;
                if (std::filesystem::exists(path, error_code) && !error_code) return path.string();
            }
        } catch (const std::exception&) {
            // Fall through to default
        }
        return {"service_config.json"};
    };
    std::string config_path = resolve_config_path();

    // Load UAV configuration from file
    UAVConfig config;
    try {
        config = loadUavConfig(config_path, uav_name);
    } catch (const std::exception& e) {
        std::cerr << "Failed to load config: " << e.what() << '\n';
        return 1;
    }

    // Display startup information
    std::cout << "===========================================" << '\n';
    std::cout << "Starting UAV Simulator: " << config.name << '\n';
    if (protocol == "udp") {
        std::cout << "Protocol: UDP" << '\n';
        std::cout << "Service UDP Port: " << config.udp_telemetry_port << '\n';
    } else if (protocol == "tcp") {
        std::cout << "Protocol: TCP" << '\n';
        std::cout << "Telemetry Port: " << config.tcp_telemetry_port << '\n';
        std::cout << "Command Port: " << config.tcp_command_port << '\n';
    } else if (protocol == "both") {
        std::cout << "Protocol: Both TCP and UDP (Default)" << '\n';
        std::cout << "TCP Telemetry Port: " << config.tcp_telemetry_port << '\n';
        std::cout << "TCP Command Port: " << config.tcp_command_port << '\n';
        std::cout << "UDP Service Port: " << config.udp_telemetry_port << '\n';
    }
    std::cout << "===========================================" << '\n';

    /**
     * @brief Telemetry generation and transmission thread
     *
     * This thread:
     * 1. Determines base telemetry codes for this UAV
     * 2. Sets up communication sockets (UDP or TCP)
     * 3. Generates and sends telemetry data at regular intervals
     * 4. Alternates between mapping and camera data types
     */
    std::thread telemetry_sender([&]() {
        try {
            // Generate realistic telemetry data for this UAV
            // Each UAV will have slightly different base coordinates and characteristics
            double base_latitude = 41.01384;
            double base_longitude = 28.94966;
            float base_altitude = 100.0f;

            // Offset each UAV's position slightly for realistic simulation
            if (config.name == "UAV_2") {
                base_latitude += 0.001;   // ~111m north
                base_longitude += 0.001;  // ~78m east (at Istanbul latitude)
                base_altitude += 20.0f;
            } else if (config.name == "UAV_3") {
                base_latitude -= 0.001;   // ~111m south
                base_longitude += 0.002;  // ~156m east
                base_altitude += 40.0f;
            }

            int sleep_interval = base_sleep_interval_ms;

            // Different UAVs send data at different rates to simulate varying workloads
            if (config.name == "UAV_2")
                sleep_interval = 750;
            else if (config.name == "UAV_3")
                sleep_interval = 1000;

            // Set up communication sockets based on selected protocol
            if (protocol == "udp") {
                // UDP only implementation with proper error handling
                boost::asio::io_context io_context;
                udp::socket socket(io_context, udp::endpoint(udp::v4(), 0));

                // Resolve the service hostname to get an IP address
                udp::resolver resolver(io_context);
                udp::resolver::results_type endpoints =
                    resolver.resolve(udp::v4(), config.ip, std::to_string(config.udp_telemetry_port));
                udp::endpoint remote_endpoint = *endpoints.begin();

                // Send telemetry data for configured iterations
                for (int i = 0; i < default_telemetry_iterations && g_running; ++i) {
                    // Create and send raw location data
                    UAVLocationPacket locationPacket =
                        createLocationPacket(TargetIDs::MAPPING,
                                             base_latitude + (rand() % 2000 - 1000) / 100000.0,   // ±10m variation
                                             base_longitude + (rand() % 2000 - 1000) / 100000.0,  // ±7.8m variation
                                             base_altitude + (rand() % 20 - 10),  // ±10m altitude variation
                                             rand() % 360,                        // Random heading
                                             10.0f + (rand() % 50) / 10.0f        // 10-15 m/s speed
                        );

                    socket.send_to(boost::asio::buffer(&locationPacket, sizeof(UAVLocationPacket)), remote_endpoint);
                    std::cout << "[" << getTimestamp() << "] [" << config.name
                              << "] Sent Location Data (UDP): Lat=" << std::fixed << std::setprecision(6)
                              << locationPacket.payload.latitude << ", Lon=" << locationPacket.payload.longitude
                              << ", Alt=" << locationPacket.payload.altitude << "m" << '\n';
                    std::this_thread::sleep_for(std::chrono::milliseconds(data_send_interval_ms));

                    if (!g_running) break;

                    // Create and send raw status data
                    UAVStatusPacket statusPacket = createStatusPacket(TargetIDs::CAMERA,
                                                                      2 + (rand() % 2),  // Health: Good to Excellent
                                                                      2,                 // Mission state: In mission
                                                                      i * 2,             // Flight time increases
                                                                      20.0f + (rand() % 30),  // CPU usage 20-50%
                                                                      30.0f + (rand() % 40)   // Memory usage 30-70%
                    );

                    socket.send_to(boost::asio::buffer(&statusPacket, sizeof(UAVStatusPacket)), remote_endpoint);
                    std::cout << "[" << getTimestamp() << "] [" << config.name
                              << "] Sent Status Data (UDP): Health=" << (int)statusPacket.payload.systemHealth
                              << ", CPU=" << statusPacket.payload.cpuUsage << "%" << '\n';
                    std::this_thread::sleep_for(std::chrono::milliseconds(sleep_interval));
                }
            } else if (protocol == "tcp") {
                // TCP only implementation with proper resource management
                zmq::context_t context(1);
                zmq::socket_t push_to_service(context, zmq::socket_type::push);

                // Set socket options for better responsiveness
                int linger = 0;  // Don't wait on close
                push_to_service.set(zmq::sockopt::linger, linger);

                std::string telemetry_addr = "tcp://" + config.ip + ":" + std::to_string(config.tcp_telemetry_port);
                push_to_service.connect(telemetry_addr);

                // Small delay to allow connection establishment
                std::this_thread::sleep_for(std::chrono::milliseconds(100));

                // Send telemetry data for configured iterations
                for (int i = 0; i < default_telemetry_iterations && g_running; ++i) {
                    if (!g_running) break;

                    // Create and send raw location data
                    UAVLocationPacket locationPacket =
                        createLocationPacket(TargetIDs::MAPPING,
                                             base_latitude + (rand() % 2000 - 1000) / 100000.0,   // ±10m variation
                                             base_longitude + (rand() % 2000 - 1000) / 100000.0,  // ±7.8m variation
                                             base_altitude + (rand() % 20 - 10),  // ±10m altitude variation
                                             rand() % 360,                        // Random heading
                                             10.0f + (rand() % 50) / 10.0f        // 10-15 m/s speed
                        );

                    try {
                        push_to_service.send(zmq::buffer(&locationPacket, sizeof(UAVLocationPacket)),
                                             zmq::send_flags::dontwait);
                        std::cout << "[" << getTimestamp() << "] [" << config.name
                                  << "] Sent Location Data (TCP): Lat=" << std::fixed << std::setprecision(6)
                                  << locationPacket.payload.latitude << ", Lon=" << locationPacket.payload.longitude
                                  << ", Alt=" << locationPacket.payload.altitude << "m" << '\n';
                    } catch (const zmq::error_t& e) {
                        if (e.num() != EAGAIN) {  // EAGAIN is normal for non-blocking
                            std::cerr << "[" << getTimestamp() << "] [" << config.name
                                      << "] TCP send error: " << e.what() << '\n';
                        }
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(data_send_interval_ms));

                    if (!g_running) break;

                    // Create and send raw status data
                    UAVStatusPacket statusPacket = createStatusPacket(TargetIDs::CAMERA,
                                                                      2 + (rand() % 2),  // Health: Good to Excellent
                                                                      2,                 // Mission state: In mission
                                                                      i * 2,             // Flight time increases
                                                                      20.0f + (rand() % 30),  // CPU usage 20-50%
                                                                      30.0f + (rand() % 40)   // Memory usage 30-70%
                    );

                    try {
                        push_to_service.send(zmq::buffer(&statusPacket, sizeof(UAVStatusPacket)),
                                             zmq::send_flags::dontwait);
                        std::cout << "[" << getTimestamp() << "] [" << config.name
                                  << "] Sent Status Data (TCP): Health=" << (int)statusPacket.payload.systemHealth
                                  << ", CPU=" << statusPacket.payload.cpuUsage << "%" << '\n';
                    } catch (const zmq::error_t& e) {
                        if (e.num() != EAGAIN) {
                            std::cerr << "[" << getTimestamp() << "] [" << config.name
                                      << "] TCP send error: " << e.what() << '\n';
                        }
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(sleep_interval));
                }

                // Explicit cleanup
                push_to_service.close();
            } else if (protocol == "both") {
                // Both protocols implementation - send to both TCP and UDP simultaneously
                boost::asio::io_context io_context;
                udp::socket udp_socket(io_context, udp::endpoint(udp::v4(), 0));

                // Set up UDP endpoint
                udp::resolver resolver(io_context);
                udp::resolver::results_type endpoints =
                    resolver.resolve(udp::v4(), config.ip, std::to_string(config.udp_telemetry_port));
                udp::endpoint remote_endpoint = *endpoints.begin();

                // Set up TCP connection
                zmq::context_t context(1);
                zmq::socket_t push_to_service(context, zmq::socket_type::push);

                // Set socket options for better responsiveness
                int linger = 0;
                push_to_service.set(zmq::sockopt::linger, linger);

                std::string telemetry_addr = "tcp://" + config.ip + ":" + std::to_string(config.tcp_telemetry_port);
                push_to_service.connect(telemetry_addr);

                // Small delay to allow connection establishment
                std::this_thread::sleep_for(std::chrono::milliseconds(100));

                // Send telemetry data for configured iterations to both protocols
                for (int i = 0; i < default_telemetry_iterations && g_running; ++i) {
                    if (!g_running) break;

                    // Create raw location data
                    UAVLocationPacket locationPacket =
                        createLocationPacket(TargetIDs::MAPPING,
                                             base_latitude + (rand() % 2000 - 1000) / 100000.0,   // ±10m variation
                                             base_longitude + (rand() % 2000 - 1000) / 100000.0,  // ±7.8m variation
                                             base_altitude + (rand() % 20 - 10),  // ±10m altitude variation
                                             rand() % 360,                        // Random heading
                                             10.0f + (rand() % 50) / 10.0f        // 10-15 m/s speed
                        );

                    // UDP send (this should be fast)
                    try {
                        udp_socket.send_to(boost::asio::buffer(&locationPacket, sizeof(UAVLocationPacket)),
                                           remote_endpoint);
                    } catch (const std::exception& e) {
                        std::cerr << "[" << getTimestamp() << "] [" << config.name << "] UDP send error: " << e.what()
                                  << '\n';
                    }

                    // TCP send (non-blocking)
                    try {
                        push_to_service.send(zmq::buffer(&locationPacket, sizeof(UAVLocationPacket)),
                                             zmq::send_flags::dontwait);
                    } catch (const zmq::error_t& e) {
                        if (e.num() != EAGAIN) {
                            std::cerr << "[" << getTimestamp() << "] [" << config.name
                                      << "] TCP send error: " << e.what() << '\n';
                        }
                    }

                    std::cout << "[" << getTimestamp() << "] [" << config.name
                              << "] Sent Location Data (TCP+UDP): Lat=" << std::fixed << std::setprecision(6)
                              << locationPacket.payload.latitude << ", Lon=" << locationPacket.payload.longitude
                              << ", Alt=" << locationPacket.payload.altitude << "m" << '\n';
                    std::this_thread::sleep_for(std::chrono::milliseconds(data_send_interval_ms));

                    if (!g_running) break;

                    // Create raw status data
                    UAVStatusPacket statusPacket = createStatusPacket(TargetIDs::CAMERA,
                                                                      2 + (rand() % 2),  // Health: Good to Excellent
                                                                      2,                 // Mission state: In mission
                                                                      i * 2,             // Flight time increases
                                                                      20.0f + (rand() % 30),  // CPU usage 20-50%
                                                                      30.0f + (rand() % 40)   // Memory usage 30-70%
                    );

                    // UDP send
                    try {
                        udp_socket.send_to(boost::asio::buffer(&statusPacket, sizeof(UAVStatusPacket)),
                                           remote_endpoint);
                    } catch (const std::exception& e) {
                        std::cerr << "[" << getTimestamp() << "] [" << config.name << "] UDP send error: " << e.what()
                                  << '\n';
                    }

                    // TCP send (non-blocking)
                    try {
                        push_to_service.send(zmq::buffer(&statusPacket, sizeof(UAVStatusPacket)),
                                             zmq::send_flags::dontwait);
                    } catch (const zmq::error_t& e) {
                        if (e.num() != EAGAIN) {
                            std::cerr << "[" << getTimestamp() << "] [" << config.name
                                      << "] TCP send error: " << e.what() << '\n';
                        }
                    }

                    std::cout << "[" << getTimestamp() << "] [" << config.name
                              << "] Sent Status Data (TCP+UDP): Health=" << (int)statusPacket.payload.systemHealth
                              << ", CPU=" << statusPacket.payload.cpuUsage << "%" << '\n';
                    std::this_thread::sleep_for(std::chrono::milliseconds(sleep_interval));
                }

                // Explicit cleanup
                push_to_service.close();
            }
        } catch (const std::exception& e) {
            std::cerr << "[" << getTimestamp() << "] [" << config.name << "] Telemetry sender error: " << e.what()
                      << '\n';
        }
        std::cout << "[" << getTimestamp() << "] [" << config.name << "] Telemetry sending completed." << '\n';
    });

    /**
     * @brief Command receiver thread (TCP only)
     *
     * This thread listens for commands from UI components via the telemetry service.
     * Commands are only supported when using TCP protocol (tcp or both modes).
     * UDP is unidirectional in this implementation.
     */
    std::thread command_receiver;
    if (protocol == "tcp" || protocol == "both") {
        command_receiver = std::thread([&]() {
            try {
                zmq::context_t context(1);
                zmq::socket_t pull_commands(context, zmq::socket_type::pull);
                std::string command_addr = "tcp://" + config.ip + ":" + std::to_string(config.tcp_command_port);
                pull_commands.connect(command_addr);

                while (g_running) {
                    zmq::message_t command;
                    // Use non-blocking receive to allow periodic checks of g_running
                    if (pull_commands.recv(command, zmq::recv_flags::dontwait)) {
                        std::string cmd(static_cast<char*>(command.data()), command.size());

                        // Display received command with visual emphasis
                        std::cout << '\n';
                        std::cout << "============================================" << '\n';
                        std::cout << "[" << getTimestamp() << "] [" << config.name << "] 🚁 UI MESSAGE: " << cmd
                                  << " 🚁" << '\n';
                        std::cout << "============================================" << '\n';
                        std::cout << '\n';

                        // Note: A complete implementation would process the command
                        // and potentially send responses back to the service
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(command_poll_interval_ms));
                }

                // Explicit cleanup
                pull_commands.close();
            } catch (const std::exception& e) {
                std::cerr << "[" << getTimestamp() << "] [" << config.name << "] Command receiver error: " << e.what()
                          << '\n';
            }
            std::cout << "[" << getTimestamp() << "] [" << config.name << "] Command receiver stopped." << '\n';
        });
    }

    // Wait for all threads to complete with responsive shutdown
    std::cout << "Press Ctrl+C to stop the simulator..." << '\n';

    // Wait for threads to complete, but check for shutdown signals
    while ((telemetry_sender.joinable() || command_receiver.joinable()) && g_running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // Ensure threads are properly joined
    if (telemetry_sender.joinable()) telemetry_sender.join();
    if (command_receiver.joinable()) command_receiver.join();

    // Log shutdown reason if caused by signal
    int signal_num = g_signal_received.load();
    if (signal_num > 0) {
        std::cout << "[" << getTimestamp() << "] [" << config.name << "] Shutdown initiated by signal: " << signal_num
                  << '\n';
    }

    std::cout << "[" << getTimestamp() << "] [" << config.name << "] Simulator stopped." << '\n';
    return 0;
}

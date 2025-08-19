/**
 * @file uav_sim.cpp
 * @brief UAV simulator for testing telemetry communication
 * 
 * This file contains a UAV simulator that generates and sends telemetry data
 * to the telemetry service. It supports both TCP and UDP protocols
 * and can also receive commands from UI components via TCP.
 */

#include <zmq.hpp>
#include <iostream>
#include <thread>
#include <chrono>
#include <string>
#include <iomanip>
#include <ctime>
#include <sstream>
#include <atomic>
#include <csignal>
#include <fstream>
#include <nlohmann/json.hpp>
#include <cstdlib>
#include <boost/asio.hpp>
#include <filesystem>
#include <vector>
#include <system_error>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

using json = nlohmann::json;
using boost::asio::ip::udp;

// Global flag to control all threads
std::atomic<bool> g_running(true);
std::atomic<int> g_signal_received(0);

// Configuration constants
namespace {
    constexpr int DEFAULT_TELEMETRY_ITERATIONS = 50;
    constexpr int BASE_SLEEP_INTERVAL_MS = 500;
    constexpr int DATA_SEND_INTERVAL_MS = 100;
    constexpr int COMMAND_POLL_INTERVAL_MS = 10;
}

/**
 * @struct UAVConfig
 * @brief Configuration data for a single UAV simulator
 * 
 * Contains all the network configuration needed for the UAV simulator to
 * connect to the telemetry service using either TCP or UDP protocols.
 */
struct UAVConfig {
    std::string name;               ///< UAV identifier (e.g., "UAV_1")
    std::string ip;                 ///< IP address of the telemetry service
    int tcp_telemetry_port;         ///< TCP port for sending telemetry data
    int tcp_command_port;           ///< TCP port for receiving commands
    int udp_telemetry_port;         ///< UDP port for sending telemetry data (service-side port)
};

/**
 * @brief Generate a formatted timestamp string with millisecond precision
 * @return Timestamp string in format "YYYY-MM-DD HH:MM:SS.mmm"
 * 
 * Uses platform-specific time conversion functions for thread safety.
 * Used for logging and debugging output.
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
    (void)result; // Suppress unused variable warning
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
UAVConfig LoadUAVConfig(const std::string& config_file, const std::string& uav_name) {
    std::ifstream file(config_file);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open config file: " + config_file + 
                                 "\nMake sure the file exists in the project root directory!");
    }

    json j;
    try {
        file >> j;
    } catch (const json::exception& e) {
        file.close();
        throw std::runtime_error("Invalid JSON in config file: " + std::string(e.what()));
    }
    file.close();

    UAVConfig config;
    bool uav_found = false;

    // Search for the specified UAV in the configuration
    if (!j.contains("uavs") || !j["uavs"].is_array()) {
        throw std::runtime_error("Config file missing 'uavs' array");
    }

    for (const auto& uav_json : j["uavs"]) {
        if (!uav_json.contains("name") || !uav_json["name"].is_string()) {
            continue; // Skip invalid entries
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
                throw std::runtime_error("Invalid configuration for UAV '" + uav_name + 
                                       "': " + std::string(e.what()));
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
void PrintAvailableUAVs(const std::string& config_file) {
    try {
        std::ifstream file(config_file);
        if (!file.is_open()) {
            std::cerr << "Cannot open config file: " << config_file << std::endl;
            return;
        }

        json j;
        file >> j;
        file.close();

        if (!j.contains("uavs") || !j["uavs"].is_array()) {
            std::cerr << "Invalid config file format" << std::endl;
            return;
        }

        std::cout << "Available UAVs in " << config_file << ":" << std::endl;
        for (const auto& uav_json : j["uavs"]) {
            if (!uav_json.contains("name")) continue;
            
            try {
                int tcp_telemetry_port = uav_json.at("tcp_telemetry_port");
                int tcp_command_port = uav_json.at("tcp_command_port");
                int udp_telemetry_port = uav_json.at("udp_telemetry_port");
                
                std::cout << "  - " << uav_json["name"]
                    << " (TCP Telemetry: " << tcp_telemetry_port
                    << ", TCP Commands: " << tcp_command_port
                    << ", UDP Telemetry: " << udp_telemetry_port << ")" << std::endl;
            } catch (const json::exception&) {
                std::cout << "  - " << uav_json["name"] << " (invalid configuration)" << std::endl;
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Error reading config file: " << e.what() << std::endl;
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

    // Check command line arguments
    if (argc < 2) {
        std::cout << "Usage: " << argv[0] << " <UAV_NAME> [--protocol <tcp|udp|both>]" << std::endl;
        std::cout << "Examples:" << std::endl;
        std::cout << "  " << argv[0] << " UAV_1                    # Use both protocols (default)" << std::endl;
        std::cout << "  " << argv[0] << " UAV_1 --protocol tcp     # TCP only (for debugging)" << std::endl;
        std::cout << "  " << argv[0] << " UAV_1 --protocol udp     # UDP only (for debugging)" << std::endl;
        std::cout << "  " << argv[0] << " UAV_1 --protocol both    # Both protocols (explicit)" << std::endl;
        std::cout << std::endl;

        // Display available UAVs from configuration
        const char* cfgEnv = std::getenv("SERVICE_CONFIG");
        std::string config_path = cfgEnv ? std::string(cfgEnv) : std::string("service_config.json");
        PrintAvailableUAVs(config_path);
        return 1;
    }

    // Parse command line arguments
    std::string uav_name = argv[1];
    std::string protocol = "both";  // Default to both protocols
    
    if (argc >= 4 && std::string(argv[2]) == "--protocol") {
        protocol = argv[3];
    }
    
    if (protocol != "tcp" && protocol != "udp" && protocol != "both") {
        std::cerr << "Error: Protocol must be 'tcp', 'udp', or 'both'" << std::endl;
        return 1;
    }

    /**
     * @brief Lambda function to get the directory containing the executable
     * @return Path to the executable directory
     * 
     * Platform-specific implementation for finding the executable path.
     */
    auto getExecutableDir = []() -> std::string {
        try {
#if defined(_WIN32)
            char path[MAX_PATH];
            DWORD len = GetModuleFileNameA(nullptr, path, MAX_PATH);
            if (len == 0 || len == MAX_PATH) return std::filesystem::current_path().string();
            return std::filesystem::path(path).parent_path().string();
#else
            char buf[4096];
            ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
            if (len == -1) return std::filesystem::current_path().string();
            buf[len] = '\0';
            return std::filesystem::path(buf).parent_path().string();
#endif
        } catch (const std::exception&) {
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
    auto resolveConfigPath = [&]() -> std::string {
        try {
            // Check environment variable first
            if (const char* env = std::getenv("SERVICE_CONFIG")) {
                std::error_code ec;
                if (std::filesystem::exists(env, ec) && !ec) return std::string(env);
            }
            
            // Try multiple candidate locations
            std::vector<std::filesystem::path> candidates;
            candidates.emplace_back("service_config.json");
            
            std::filesystem::path exeDir = getExecutableDir();
            candidates.push_back(exeDir / "service_config.json");
            candidates.push_back(exeDir.parent_path() / "service_config.json");
            
            for (auto &p : candidates) {
                std::error_code ec; 
                if (std::filesystem::exists(p, ec) && !ec) return p.string();
            }
        } catch (const std::exception&) {
            // Fall through to default
        }
        return std::string("service_config.json");
    };
    std::string config_path = resolveConfigPath();

    // Load UAV configuration from file
    UAVConfig config;
    try {
        config = LoadUAVConfig(config_path, uav_name);
    }
    catch (const std::exception& e) {
        std::cerr << "Failed to load config: " << e.what() << std::endl;
        return 1;
    }

    // Display startup information
    std::cout << "===========================================" << std::endl;
    std::cout << "Starting UAV Simulator: " << config.name << std::endl;
    if (protocol == "udp") {
        std::cout << "Protocol: UDP" << std::endl;
        std::cout << "Service UDP Port: " << config.udp_telemetry_port << std::endl;
    } else if (protocol == "tcp") {
        std::cout << "Protocol: TCP" << std::endl;
        std::cout << "Telemetry Port: " << config.tcp_telemetry_port << std::endl;
        std::cout << "Command Port: " << config.tcp_command_port << std::endl;
    } else if (protocol == "both") {
        std::cout << "Protocol: Both TCP and UDP (Default)" << std::endl;
        std::cout << "TCP Telemetry Port: " << config.tcp_telemetry_port << std::endl;
        std::cout << "TCP Command Port: " << config.tcp_command_port << std::endl;
        std::cout << "UDP Service Port: " << config.udp_telemetry_port << std::endl;
    }
    std::cout << "===========================================" << std::endl;

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
            // Determine base values for telemetry data based on UAV name
            // Different UAVs use different code ranges to simulate different data types
            int base_mapping = 1000;  // Mapping data codes: 1000-1999
            int base_camera = 2000;   // Camera data codes: 2000-2999
            if (config.name == "UAV_2") {
                base_mapping = 3000;  // UAV_2 mapping: 3000-3999
                base_camera = 4000;   // UAV_2 camera: 4000-4999
            } else if (config.name == "UAV_3") {
                base_mapping = 5000;  // UAV_3 mapping: 5000-5999
                base_camera = 6000;   // UAV_3 camera: 6000-6999
            }

            int mapping = base_mapping + 1;
            int camera = base_camera + 1;
            int sleep_interval = BASE_SLEEP_INTERVAL_MS;

            // Different UAVs send data at different rates to simulate varying workloads
            if (config.name == "UAV_2") sleep_interval = 750;
            else if (config.name == "UAV_3") sleep_interval = 1000;

            // Set up communication sockets based on selected protocol
            if (protocol == "udp") {
                // UDP only implementation with proper error handling
                boost::asio::io_context io_context;
                udp::socket socket(io_context, udp::endpoint(udp::v4(), 0));

                // Resolve the service hostname to get an IP address
                udp::resolver resolver(io_context);
                udp::resolver::results_type endpoints = resolver.resolve(udp::v4(), config.ip, std::to_string(config.udp_telemetry_port));
                udp::endpoint remote_endpoint = *endpoints.begin();

                // Send telemetry data for configured iterations
                for (int i = 0; i < DEFAULT_TELEMETRY_ITERATIONS && g_running; ++i) {
                    // Send mapping data
                    std::string msg1 = config.name + "  " + std::to_string(mapping + i);
                    socket.send_to(boost::asio::buffer(msg1), remote_endpoint);
                    std::cout << "[" << GetTimestamp() << "] [" << config.name << "] Sent (UDP): " << msg1 << std::endl;
                    std::this_thread::sleep_for(std::chrono::milliseconds(DATA_SEND_INTERVAL_MS));
                    
                    if (!g_running) break;
                    
                    // Send camera data
                    std::string msg2 = config.name + "  " + std::to_string(camera + i);
                    socket.send_to(boost::asio::buffer(msg2), remote_endpoint);
                    std::cout << "[" << GetTimestamp() << "] [" << config.name << "] Sent (UDP): " << msg2 << std::endl;
                    std::this_thread::sleep_for(std::chrono::milliseconds(sleep_interval));
                }
            } else if (protocol == "tcp") {
                // TCP only implementation with proper resource management
                zmq::context_t context(1);
                zmq::socket_t push_to_service(context, zmq::socket_type::push);
                
                // Set socket options for better responsiveness
                int linger = 0; // Don't wait on close
                push_to_service.set(zmq::sockopt::linger, linger);
                
                std::string telemetry_addr = "tcp://" + config.ip + ":" + std::to_string(config.tcp_telemetry_port);
                push_to_service.connect(telemetry_addr);
                
                // Small delay to allow connection establishment
                std::this_thread::sleep_for(std::chrono::milliseconds(100));

                // Send telemetry data for configured iterations
                for (int i = 0; i < DEFAULT_TELEMETRY_ITERATIONS && g_running; ++i) {
                    if (!g_running) break;
                    
                    // Send mapping data
                    std::string msg1 = config.name + "  " + std::to_string(mapping + i);
                    try {
                        push_to_service.send(zmq::buffer(msg1), zmq::send_flags::dontwait);
                        std::cout << "[" << GetTimestamp() << "] [" << config.name << "] Sent (TCP): " << msg1 << std::endl;
                    } catch (const zmq::error_t& e) {
                        if (e.num() != EAGAIN) { // EAGAIN is normal for non-blocking
                            std::cerr << "[" << GetTimestamp() << "] [" << config.name << "] TCP send error: " << e.what() << std::endl;
                        }
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(DATA_SEND_INTERVAL_MS));
                    
                    if (!g_running) break;
                    
                    // Send camera data
                    std::string msg2 = config.name + "  " + std::to_string(camera + i);
                    try {
                        push_to_service.send(zmq::buffer(msg2), zmq::send_flags::dontwait);
                        std::cout << "[" << GetTimestamp() << "] [" << config.name << "] Sent (TCP): " << msg2 << std::endl;
                    } catch (const zmq::error_t& e) {
                        if (e.num() != EAGAIN) {
                            std::cerr << "[" << GetTimestamp() << "] [" << config.name << "] TCP send error: " << e.what() << std::endl;
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
                udp::resolver::results_type endpoints = resolver.resolve(udp::v4(), config.ip, std::to_string(config.udp_telemetry_port));
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
                for (int i = 0; i < DEFAULT_TELEMETRY_ITERATIONS && g_running; ++i) {
                    if (!g_running) break;
                    
                    // Send mapping data via both protocols
                    std::string msg1 = config.name + "  " + std::to_string(mapping + i);
                    
                    // UDP send (this should be fast)
                    try {
                        udp_socket.send_to(boost::asio::buffer(msg1), remote_endpoint);
                    } catch (const std::exception& e) {
                        std::cerr << "[" << GetTimestamp() << "] [" << config.name << "] UDP send error: " << e.what() << std::endl;
                    }
                    
                    // TCP send (non-blocking)
                    try {
                        push_to_service.send(zmq::buffer(msg1), zmq::send_flags::dontwait);
                    } catch (const zmq::error_t& e) {
                        if (e.num() != EAGAIN) {
                            std::cerr << "[" << GetTimestamp() << "] [" << config.name << "] TCP send error: " << e.what() << std::endl;
                        }
                    }
                    
                    std::cout << "[" << GetTimestamp() << "] [" << config.name << "] Sent (TCP+UDP): " << msg1 << std::endl;
                    std::this_thread::sleep_for(std::chrono::milliseconds(DATA_SEND_INTERVAL_MS));
                    
                    if (!g_running) break;
                    
                    // Send camera data via both protocols
                    std::string msg2 = config.name + "  " + std::to_string(camera + i);
                    
                    // UDP send
                    try {
                        udp_socket.send_to(boost::asio::buffer(msg2), remote_endpoint);
                    } catch (const std::exception& e) {
                        std::cerr << "[" << GetTimestamp() << "] [" << config.name << "] UDP send error: " << e.what() << std::endl;
                    }
                    
                    // TCP send (non-blocking)
                    try {
                        push_to_service.send(zmq::buffer(msg2), zmq::send_flags::dontwait);
                    } catch (const zmq::error_t& e) {
                        if (e.num() != EAGAIN) {
                            std::cerr << "[" << GetTimestamp() << "] [" << config.name << "] TCP send error: " << e.what() << std::endl;
                        }
                    }
                    
                    std::cout << "[" << GetTimestamp() << "] [" << config.name << "] Sent (TCP+UDP): " << msg2 << std::endl;
                    std::this_thread::sleep_for(std::chrono::milliseconds(sleep_interval));
                }
                
                // Explicit cleanup
                push_to_service.close();
            }
        } catch (const std::exception& e) {
            std::cerr << "[" << GetTimestamp() << "] [" << config.name << "] Telemetry sender error: " << e.what() << std::endl;
        }
        std::cout << "[" << GetTimestamp() << "] [" << config.name << "] Telemetry sending completed." << std::endl;
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
                        std::cout << std::endl;
                        std::cout << "============================================" << std::endl;
                        std::cout << "[" << GetTimestamp() << "] [" << config.name << "] 🚁 UI MESSAGE: " << cmd << " 🚁" << std::endl;
                        std::cout << "============================================" << std::endl;
                        std::cout << std::endl;

                        // Note: A complete implementation would process the command
                        // and potentially send responses back to the service
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(COMMAND_POLL_INTERVAL_MS));
                }
                
                // Explicit cleanup
                pull_commands.close();
            } catch (const std::exception& e) {
                std::cerr << "[" << GetTimestamp() << "] [" << config.name << "] Command receiver error: " << e.what() << std::endl;
            }
            std::cout << "[" << GetTimestamp() << "] [" << config.name << "] Command receiver stopped." << std::endl;
        });
    }

    // Wait for all threads to complete with responsive shutdown
    std::cout << "Press Ctrl+C to stop the simulator..." << std::endl;
    
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
        std::cout << "[" << GetTimestamp() << "] [" << config.name << "] Shutdown initiated by signal: " << signal_num << std::endl;
    }

    std::cout << "[" << GetTimestamp() << "] [" << config.name << "] Simulator stopped." << std::endl;
    return 0;
}

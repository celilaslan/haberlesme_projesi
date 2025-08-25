/**
 * @file TelemetryService.cpp
 * @brief Implementation of the TelemetryService class
 *
 * This file contains the implementation of the main telemetry service functionality,
 * including service initialization, message processing, and shutdown coordination.
 */

#include "TelemetryService.h"

#include <array>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "Logger.h"
#include "TelemetryPackets.h"

// Platform-specific includes for executable path detection
#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif

/**
 * @brief Constructor - initializes ZeroMQ context for TCP communication
 *
 * Creates a ZeroMQ context with 1 I/O thread for TCP telemetry and command handling.
 * UDP communication uses Boost.Asio and doesn't require ZeroMQ initialization.
 */
TelemetryService::TelemetryService() try : zmqContext_(1) {
    // ZMQ context initialized successfully for TCP communication
} catch (const zmq::error_t& e) {
    throw std::runtime_error("Failed to initialize ZMQ context for TCP: " + std::string(e.what()));
} catch (const std::exception& e) {
    throw std::runtime_error("Failed to initialize TelemetryService: " + std::string(e.what()));
}

/**
 * @brief Main service execution method
 * @param app_running Reference to atomic boolean controlling service lifecycle
 *
 * This is the main service loop that:
 * 1. Loads configuration and initializes logging
 * 2. Creates and starts communication managers
 * 3. Runs until shutdown is requested
 * 4. Performs graceful cleanup
 */
void TelemetryService::run(std::atomic<bool>& app_running) {
    try {
        // Load configuration from file (checks environment variable first)
        std::string cfg_path = resolveConfigPath();
        if (!config_.loadFromFile(cfg_path)) {
            throw std::runtime_error("Could not load config file: " + cfg_path);
        }

        // Set up logging system - resolve log file path relative to executable if needed
        std::filesystem::path log_path(config_.getLogFile());
        if (!log_path.is_absolute()) {
            log_path = std::filesystem::path(getExecutableDir()) / log_path;
        }
        // Create log directory if it doesn't exist
        if (log_path.has_parent_path()) {
            std::error_code error_code;
            std::filesystem::create_directories(log_path.parent_path(), error_code);
        }

        // Initialize logging system and log startup information
        Logger::init(log_path.string());
        Logger::statusWithDetails("SERVICE", StatusMessage("STARTING"), DetailMessage("Multi-UAV Telemetry Service"));
        Logger::info("Config loaded successfully. Found " + std::to_string(config_.getUAVs().size()) + " UAVs");

        // Create managers with proper error handling
        bool zmq_started = false;
        bool udp_started = false;

        try {
            // Create TCP manager with callback for incoming messages
            tcpManager_ = std::make_unique<TcpManager>(
                zmqContext_, config_, [this](const std::string& source, const std::vector<uint8_t>& data) {
                    this->onZmqMessage(source, data);
                });

            // Create UDP manager with callback for incoming messages
            udpManager_ = std::make_unique<UdpManager>(
                config_, [this](const std::string& source, const std::vector<uint8_t>& data) {
                    this->onUdpMessage(source, data);
                });

            // Start both communication managers with error handling
            tcpManager_->start();
            zmq_started = true;

            udpManager_->start();
            udp_started = true;

        } catch (const std::exception& e) {
            Logger::error("Failed to start communication managers: " + std::string(e.what()));

            // Cleanup any partially started managers
            if (udp_started && udpManager_) {
                udpManager_->stop();
                udpManager_->join();
            }
            if (zmq_started && tcpManager_) {
                tcpManager_->stop();
                tcpManager_->join();
            }
            throw;
        }

        // Collect port information for startup summary
        std::vector<int> tcp_ports;
        std::vector<int> udp_ports;
        for (const auto& uav : config_.getUAVs()) {
            tcp_ports.push_back(uav.tcp_telemetry_port);
            tcp_ports.push_back(uav.tcp_command_port);
            udp_ports.push_back(uav.udp_telemetry_port);
        }
        tcp_ports.push_back(config_.getUiPorts().tcp_publish_port);
        tcp_ports.push_back(config_.getUiPorts().tcp_command_port);
        udp_ports.push_back(config_.getUiPorts().udp_publish_port);

        Logger::serviceStarted(static_cast<int>(config_.getUAVs().size()), tcp_ports, udp_ports);

        // Main service loop - just sleep and wait for shutdown signal
        while (app_running) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        // Graceful shutdown sequence
        Logger::statusWithDetails("SERVICE", StatusMessage("SHUTTING DOWN"), DetailMessage("Signal received"));

        // Stop managers (this stops their internal threads)
        Logger::statusWithDetails("UDP", StatusMessage("STOPPING"), DetailMessage("Shutting down UDP services"));
        if (udpManager_) {
            udpManager_->stop();
        }
        Logger::statusWithDetails("TCP", StatusMessage("STOPPING"), DetailMessage("Shutting down TCP services"));
        if (tcpManager_) {
            tcpManager_->stop();
        }

        // Wait for threads to complete
        if (udpManager_) {
            udpManager_->join();
        }
        if (tcpManager_) {
            tcpManager_->join();
        }

        Logger::statusWithDetails(
            "SERVICE", StatusMessage("SHUTDOWN COMPLETE"), DetailMessage("All services stopped gracefully"));

    } catch (const std::exception& e) {
        Logger::error("Service error: " + std::string(e.what()));
        throw;
    }
}

/**
 * @brief Handler for UDP messages - forwards to common processing
 * @param uav_name Name of the UAV that sent the message
 * @param data The received binary message data
 */
void TelemetryService::onUdpMessage(const std::string& uav_name, const std::vector<uint8_t>& data) {
    try {
        std::lock_guard<std::mutex> lock(processingMutex_);
        processAndPublishTelemetry(data, uav_name, "UDP");
    } catch (const std::exception& e) {
        Logger::error("UDP message processing error: " + std::string(e.what()));
    }
}

/**
 * @brief Handler for TCP messages - forwards to common processing
 * @param uav_name Name of the UAV that sent the message
 * @param data The received binary message data
 */
void TelemetryService::onZmqMessage(const std::string& uav_name, const std::vector<uint8_t>& data) {
    try {
        std::lock_guard<std::mutex> lock(processingMutex_);
        processAndPublishTelemetry(data, uav_name, "TCP");
    } catch (const std::exception& e) {
        Logger::error("TCP message processing error: " + std::string(e.what()));
    }
}

/**
 * @brief Common processing pipeline for all telemetry data
 * @param data The binary telemetry data to process
 * @param uav_name Name of the UAV that sent the data
 * @param protocol The protocol used (TCP or UDP)
 *
 * This method implements hierarchical topic routing with wildcard support:
 * 1. Parses the binary packet header to determine target and type
 * 2. Uses the UAV name directly from service_config.json
 * 3. Creates a single hierarchical topic for efficient routing
 * 4. Routes the complete binary packet to matching wildcard subscriptions
 */
void TelemetryService::processAndPublishTelemetry(const std::vector<uint8_t>& data,
                                                  const std::string& uav_name,
                                                  const std::string& protocol) {
    try {
        // Ensure we have at least enough data for a packet header
        if (data.size() < sizeof(PacketHeader)) {
            Logger::warn("Received packet too small for header from " + uav_name
                         + " (size: " + std::to_string(data.size()) + " bytes)");
            return;
        }

        // Parse the packet header
        const PacketHeader* header = reinterpret_cast<const PacketHeader*>(data.data());

        // Determine target and type strings for topic creation
        std::string target_name;
        std::string type_name;

        switch (header->targetID) {
            case TargetIDs::CAMERA:
                target_name = "camera";
                break;
            case TargetIDs::MAPPING:
                target_name = "mapping";
                break;
            default:
                target_name = "unknown";
                break;
        }

        switch (header->packetType) {
            case PacketTypes::LOCATION:
                type_name = "location";
                break;
            case PacketTypes::STATUS:
                type_name = "status";
                break;
            default:
                type_name = "unknown";
                break;
        }

        // Log packet information
        Logger::info("Received " + type_name + " packet for " + target_name + " from " + uav_name + " ("
                     + std::to_string(data.size()) + " bytes)");

        // Create hierarchical topic for efficient wildcard subscriptions
        // Format: telemetry.{UAV_name}.{target}.{type}
        std::string topic = "telemetry." + uav_name + "." + target_name + "." + type_name;
        // Example: "telemetry.UAV_1.camera.location"

        // Route to UIs using the same protocol as the source
        if (protocol == "TCP" && tcpManager_) {
            // Single topic publish - ZeroMQ handles wildcard subscriptions natively
            tcpManager_->publishTelemetry(topic, data);
        } else if (protocol == "UDP" && udpManager_) {
            // Single topic publish - UDP manager handles wildcard pattern matching
            udpManager_->publishTelemetry(topic, data);
        } else {
            Logger::error("Cannot publish telemetry - manager not available for protocol: " + protocol);
        }

    } catch (const std::exception& e) {
        Logger::error("Error processing telemetry packet (" + std::to_string(data.size())
                      + " bytes): " + std::string(e.what()));
    }
}

/**
 * @brief Resolves the configuration file path from environment or defaults
 * @return Full path to the configuration file to use
 *
 * This method checks multiple locations for the config file:
 * 1. SERVICE_CONFIG environment variable (if set and file exists)
 * 2. "service_config.json" in current directory
 * 3. "service_config.json" in executable directory
 * 4. "service_config.json" in parent of executable directory
 * 5. Falls back to "service_config.json" in current directory
 */
std::string TelemetryService::resolveConfigPath() {
    // Check environment variable first
    if (const char* env = std::getenv("SERVICE_CONFIG")) {
        if (std::filesystem::exists(env))
            return {env};
    }

    // Try multiple candidate locations
    std::vector<std::filesystem::path> candidates;
    candidates.emplace_back("service_config.json");  // Current directory

    std::filesystem::path exe_dir = getExecutableDir();
    candidates.push_back(exe_dir / "service_config.json");                // Executable directory
    candidates.push_back(exe_dir.parent_path() / "service_config.json");  // Parent of executable

    // Return the first existing file
    for (auto& path : candidates) {
        std::error_code error_code;
        if (std::filesystem::exists(path, error_code))
            return path.string();
    }

    // Fallback to current directory
    return "service_config.json";
}

/**
 * @brief Gets the directory where the current executable is located
 * @return Full path to the directory containing the executable
 *
 * This is used to resolve relative paths in configuration files.
 * Implementation is platform-specific.
 */
std::string TelemetryService::getExecutableDir() {
#if defined(_WIN32)
    // Windows implementation using GetModuleFileName
    char path[MAX_PATH];
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    return std::filesystem::path(path).parent_path().string();
#else
    // Linux implementation using /proc/self/exe
    std::array<char, 4096> buffer{};
    ssize_t len = readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
    if (len == -1)
        return "";
    // Use string constructor to avoid array subscript warning
    return std::filesystem::path(std::string(buffer.data(), static_cast<size_t>(len))).parent_path().string();
#endif
}

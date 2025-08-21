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
#include <stdexcept>
#include <string>
#include <thread>

#include "../telemetry_packets.h"
#include <vector>

#include "Logger.h"

// Platform-specific includes for executable path detection
#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif

/**
 * @brief Constructor - initializes ZeroMQ context
 *
 * Creates a ZeroMQ context with 1 I/O thread, which will be shared
 * by all ZeroMQ sockets in the application.
 */
TelemetryService::TelemetryService() try : zmqContext_(1) {
    // ZMQ context initialized successfully
} catch (const zmq::error_t& e) {
    throw std::runtime_error("Failed to initialize ZMQ context: " + std::string(e.what()));
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
        Logger::statusWithDetails("SERVICE", StatusMessage("STARTING"), DetailMessage("Multi-UAV Telemetry Service v1.0"));
        Logger::info("Config loaded successfully. Found " + std::to_string(config_.getUAVs().size()) + " UAVs");

        // Create managers with proper error handling
        bool zmq_started = false;
        bool udp_started = false;

        try {
            // Create ZeroMQ manager with callback for incoming messages
            zmqManager_ = std::make_unique<ZmqManager>(
                zmqContext_, config_,
                [this](const std::string& source, const std::string& data) { this->onZmqMessage(source, data); });

            // Create UDP manager with callback for incoming messages
            udpManager_ = std::make_unique<UdpManager>(
                config_,
                [this](const std::string& source, const std::string& data) { this->onUdpMessage(source, data); });

            // Start both communication managers with error handling
            zmqManager_->start();
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
            if (zmq_started && zmqManager_) {
                zmqManager_->stop();
                zmqManager_->join();
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
        if (zmqManager_) {
            zmqManager_->stop();
        }

        // Wait for threads to complete
        if (udpManager_) {
            udpManager_->join();
        }
        if (zmqManager_) {
            zmqManager_->join();
        }

        Logger::statusWithDetails("SERVICE", StatusMessage("SHUTDOWN COMPLETE"), DetailMessage("All services stopped gracefully"));

    } catch (const std::exception& e) {
        Logger::error("Service error: " + std::string(e.what()));
        throw;
    }
}

/**
 * @brief Handler for UDP messages - forwards to common processing
 * @param sourceDescription Description of the UDP source
 * @param data The received message data
 */
void TelemetryService::onUdpMessage(const std::string& sourceDescription, const std::string& data) {
    try {
        std::lock_guard<std::mutex> lock(processingMutex_);
        processAndPublishTelemetry(data, sourceDescription, "UDP");
    } catch (const std::exception& e) {
        Logger::error("UDP message processing error: " + std::string(e.what()));
    }
}

/**
 * @brief Handler for TCP messages - forwards to common processing
 * @param sourceDescription Description of the TCP source
 * @param data The received message data
 */
void TelemetryService::onZmqMessage(const std::string& sourceDescription, const std::string& data) {
    try {
        std::lock_guard<std::mutex> lock(processingMutex_);
        processAndPublishTelemetry(data, sourceDescription, "TCP");
    } catch (const std::exception& e) {
        Logger::error("TCP message processing error: " + std::string(e.what()));
    }
}

/**
 * @brief Common processing pipeline for all telemetry data
 * @param data The telemetry data to process
 * @param source_description Description of the data source
 * @param protocol The protocol used (TCP or UDP)
 *
 * This method:
 * 1. Logs the incoming message
 * 2. Extracts the UAV name from the source description
 * 3. Determines the topic based on the numeric code in the data
 * 4. Routes the data to UI components using the same protocol as the source
 */
void TelemetryService::processAndPublishTelemetry(const std::string& data, const std::string& source_description,
                                                  const std::string& protocol) {
    try {
        // Ensure we have at least enough data for a packet header
        if (data.size() < sizeof(PacketHeader)) {
            Logger::warn("Received packet too small for header from " + source_description + 
                        " (size: " + std::to_string(data.size()) + " bytes)");
            return;
        }

        // Parse the packet header
        const PacketHeader* header = reinterpret_cast<const PacketHeader*>(data.data());
        
        // Extract UAV name by removing protocol prefix ("TCP:", "UDP:")
        std::string uav_name = "unknown_uav";
        size_t colon_pos = source_description.find(':');
        if (colon_pos != std::string::npos) {
            uav_name = source_description.substr(colon_pos + 1);
        } else {
            uav_name = source_description;
        }

        // Determine target and type strings for topic creation
        std::string target_name;
        std::string type_name;
        
        switch (header->targetID) {
            case TargetIDs::CAMERA:  target_name = "camera"; break;
            case TargetIDs::MAPPING: target_name = "mapping"; break;
            case TargetIDs::GENERAL: target_name = "general"; break;
            default: target_name = "unknown"; break;
        }
        
        switch (header->packetType) {
            case PacketTypes::LOCATION: type_name = "location"; break;
            case PacketTypes::STATUS:   type_name = "status"; break;
            case PacketTypes::IMU:      type_name = "imu"; break;
            case PacketTypes::BATTERY:  type_name = "battery"; break;
            default: type_name = "unknown"; break;
        }

        // Log packet information
        Logger::info("Received " + type_name + " packet for " + target_name + 
                    " from " + uav_name + " (" + std::to_string(data.size()) + " bytes)");

        // Create topic names for flexible routing (both target-based and type-based)
        std::string target_topic = target_name + "_" + uav_name;
        std::string type_topic = type_name + "_" + uav_name;

        // Route to UIs using the same protocol as the source, with validation
        if (protocol == "TCP" && zmqManager_) {
            // Publish to both target-based and type-based topics for maximum flexibility
            zmqManager_->publishTelemetry(target_topic, data);
            if (target_topic != type_topic) {  // Avoid duplicate sends
                zmqManager_->publishTelemetry(type_topic, data);
            }
        } else if (protocol == "UDP" && udpManager_) {
            // For UDP, use target-based routing (maintains compatibility)
            udpManager_->publishTelemetry(target_topic, data);
        } else {
            Logger::error("Cannot publish telemetry - manager not available for protocol: " + protocol);
        }

    } catch (const std::exception& e) {
        Logger::error("Error processing telemetry data '" + data + "': " + std::string(e.what()));
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
        if (std::filesystem::exists(env)) return {env};
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
        if (std::filesystem::exists(path, error_code)) return path.string();
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
    if (len == -1) return "";
    // Use string constructor to avoid array subscript warning
    return std::filesystem::path(std::string(buffer.data(), static_cast<size_t>(len))).parent_path().string();
#endif
}

/**
 * @file TelemetryService.cpp
 * @brief Implementation of the TelemetryService class
 * 
 * This file contains the implementation of the main telemetry service functionality,
 * including service initialization, message processing, and shutdown coordination.
 */

#include "TelemetryService.h"
#include "Logger.h"
#include <stdexcept>
#include <vector>
#include <string>
#include <cstdlib>

// Platform-specific includes for executable path detection
#if defined(_WIN32)
#  include <windows.h>
#else
#  include <unistd.h>
#endif

/**
 * @brief Constructor - initializes ZeroMQ context
 * 
 * Creates a ZeroMQ context with 1 I/O thread, which will be shared
 * by all ZeroMQ sockets in the application.
 */
TelemetryService::TelemetryService() : zmqContext_(1) {}

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
    // Load configuration from file (checks environment variable first)
    std::string cfgPath = resolveConfigPath();
    if (!config_.loadFromFile(cfgPath)) {
        throw std::runtime_error("Could not load config file: " + cfgPath);
    }
    
    // Set up logging system - resolve log file path relative to executable if needed
    std::filesystem::path logPath(config_.getLogFile());
    if (!logPath.is_absolute()) {
        logPath = std::filesystem::path(getExecutableDir()) / logPath;
    }
    // Create log directory if it doesn't exist
    if (logPath.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(logPath.parent_path(), ec);
    }

    // Initialize logging system and log startup information
    Logger::init(logPath.string());
    Logger::status("SERVICE", "STARTING", "Multi-UAV Telemetry Service v1.0");
    Logger::info("Config loaded successfully. Found " + std::to_string(config_.getUAVs().size()) + " UAVs");

    // Create ZeroMQ manager with callback for incoming messages
    // The lambda captures 'this' to call the instance method
    zmqManager_ = std::make_unique<ZmqManager>(zmqContext_, config_, 
        [this](const std::string& source, const std::string& data) {
            this->onZmqMessage(source, data);
        }
    );
    
    // Create UDP manager with callback for incoming messages
    udpManager_ = std::make_unique<UdpManager>(config_, 
        [this](const std::string& source, const std::string& data) {
            this->onUdpMessage(source, data);
        }
    );

    // Start both communication managers
    zmqManager_->start();
    udpManager_->start();

    // Collect port information for startup summary
    std::vector<int> tcpPorts, udpPorts;
    for (const auto& uav : config_.getUAVs()) {
        tcpPorts.push_back(uav.tcp_telemetry_port);
        tcpPorts.push_back(uav.tcp_command_port);
        udpPorts.push_back(uav.udp_telemetry_port);
    }
    tcpPorts.push_back(config_.getUiPorts().tcp_publish_port);
    tcpPorts.push_back(config_.getUiPorts().tcp_command_port);

    Logger::serviceStarted(config_.getUAVs().size(), tcpPorts, udpPorts);

    // Main service loop - just sleep and wait for shutdown signal
    while (app_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // Graceful shutdown sequence
    Logger::status("SERVICE", "SHUTTING DOWN", "Signal received");
    
    // Stop managers (this stops their internal threads)
    Logger::status("UDP", "STOPPING", "Shutting down UDP services");
    udpManager_->stop();
    Logger::status("TCP", "STOPPING", "Shutting down TCP services");
    zmqManager_->stop();
    
    // Wait for threads to complete
    udpManager_->join();
    zmqManager_->join();

    Logger::status("SERVICE", "SHUTDOWN COMPLETE", "All services stopped gracefully");
}

/**
 * @brief Handler for UDP messages - forwards to common processing
 * @param sourceDescription Description of the UDP source
 * @param data The received message data
 */
void TelemetryService::onUdpMessage(const std::string& sourceDescription, const std::string& data) {
    processAndPublishTelemetry(data, sourceDescription, "UDP");
}

/**
 * @brief Handler for TCP messages - forwards to common processing
 * @param sourceDescription Description of the TCP source
 * @param data The received message data
 */
void TelemetryService::onZmqMessage(const std::string& sourceDescription, const std::string& data) {
    processAndPublishTelemetry(data, sourceDescription, "TCP");
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
void TelemetryService::processAndPublishTelemetry(const std::string& data, const std::string& source_description, const std::string& protocol) {
    Logger::info("Received from " + source_description + ": " + data);
    
    std::string topic = "unknown";
    std::string uav_name = "unknown_uav";

    // Extract UAV name by removing protocol prefix ("TCP:", "UDP:")
    size_t colon_pos = source_description.find(':');
    if (colon_pos != std::string::npos) {
        uav_name = source_description.substr(colon_pos + 1);
    } else {
        uav_name = source_description;
    }
    
    // Use the data as-is (format: "UAV_1  1001")
    const std::string& actual_data = data;
    
    try {
        // Extract the numeric code from the end of the message
        size_t last_space = actual_data.find_last_of(" \t");
        std::string numeric_part = (last_space != std::string::npos) ? actual_data.substr(last_space + 1) : actual_data;
        int code = std::stoi(numeric_part);
        
        // Determine topic based on code ranges
        // 1000-1999: mapping data
        // 2000-2999: camera data
        // 3000-3999: mapping data
        // 4000-4999: camera data
        // 5000-5999: mapping data
        // 6000-6999: camera data
        if (code >= 1000 && code < 2000) topic = "mapping";
        else if (code >= 2000 && code < 3000) topic = "camera";
        else if (code >= 3000 && code < 4000) topic = "mapping";
        else if (code >= 4000 && code < 5000) topic = "camera";
        else if (code >= 5000 && code < 6000) topic = "mapping";
        else if (code >= 6000 && code < 7000) topic = "camera";

    } catch (...) {
        // Log parsing errors but continue processing
        Logger::error("Could not parse telemetry data: " + actual_data);
    }
    
    // Create the full topic name: "topic_UAVname" (e.g., "camera_UAV_1")
    std::string full_topic = topic + "_" + uav_name;
    
    // Route to UIs using the same protocol as the source
    if (protocol == "TCP") {
        zmqManager_->publishTelemetry(full_topic, actual_data);
    } else if (protocol == "UDP") {
        udpManager_->publishTelemetry(full_topic, actual_data);
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
        if (std::filesystem::exists(env)) return std::string(env);
    }
    
    // Try multiple candidate locations
    std::vector<std::filesystem::path> candidates;
    candidates.emplace_back("service_config.json");  // Current directory
    
    std::filesystem::path exeDir = getExecutableDir();
    candidates.push_back(exeDir / "service_config.json");          // Executable directory
    candidates.push_back(exeDir.parent_path() / "service_config.json");  // Parent of executable
    
    // Return the first existing file
    for (auto &p : candidates) {
        std::error_code ec; 
        if (std::filesystem::exists(p, ec)) return p.string();
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
    char buf[4096];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len == -1) return "";
    buf[len] = '\0';
    return std::filesystem::path(buf).parent_path().string();
#endif
}
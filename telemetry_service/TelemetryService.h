/**
 * @file TelemetryService.h
 * @brief Header file for the main TelemetryService class
 * 
 * This file defines the TelemetryService class which coordinates the entire telemetry system.
 * It manages both ZeroMQ and UDP communication channels and handles message routing between
 * UAVs and UI components.
 */

#ifndef TELEMETRYSERVICE_H
#define TELEMETRYSERVICE_H

#include "Config.h"
#include "ZmqManager.h"
#include "UdpManager.h"
#include <atomic>
#include <memory>
#include <filesystem>
#include <unordered_map>

/**
 * @class TelemetryService
 * @brief Main service class that orchestrates the telemetry communication system
 * 
 * The TelemetryService class is responsible for:
 * - Loading configuration from JSON files
 * - Managing ZeroMQ and UDP communication channels
 * - Processing and routing telemetry messages between UAVs and UI components
 * - Logging service activities
 * - Coordinating graceful shutdown
 */
class TelemetryService {
public:
    /**
     * @brief Constructor - initializes the ZeroMQ context
     */
    TelemetryService();
    
    /**
     * @brief Main service execution loop
     * @param app_running Reference to atomic boolean that controls the service lifecycle
     * 
     * This method:
     * 1. Loads configuration from file
     * 2. Initializes logging system
     * 3. Creates and starts TCP and UDP managers
     * 4. Runs the main service loop until shutdown is requested
     * 5. Performs graceful cleanup
     */
    void run(std::atomic<bool>& app_running);

private:
    /**
     * @brief Callback handler for incoming UDP messages
     * @param sourceDescription Description of the message source (e.g., "UDP:UAV_1")
     * @param data The raw message data received
     * 
     * This method is called by the UdpManager when a UDP message is received.
     * It forwards the message to the common processing pipeline.
     */
    void onUdpMessage(const std::string& sourceDescription, const std::string& data);
    
    /**
     * @brief Callback handler for incoming TCP messages
     * @param sourceDescription Description of the message source (e.g., "TCP:UAV_1")
     * @param data The raw message data received
     * 
     * This method is called by the ZmqManager when a TCP message is received.
     * It forwards the message to the common processing pipeline.
     */
    void onZmqMessage(const std::string& sourceDescription, const std::string& data);
    
    /**
     * @brief Common message processing and publishing pipeline
     * @param data The telemetry data to process
     * @param source_description Description of where the data came from
     * @param protocol The protocol used (TCP or UDP)
     * 
     * This method:
     * 1. Logs the incoming message
     * 2. Determines the appropriate topic based on message content
     * 3. Publishes the processed data to UI subscribers using the same protocol
     */
    void processAndPublishTelemetry(const std::string& data, const std::string& source_description, const std::string& protocol);
    
    /**
     * @brief Resolves the configuration file path
     * @return Full path to the configuration file
     * 
     * Checks the SERVICE_CONFIG environment variable first, then falls back
     * to the default "service_config.json" in the executable directory.
     */
    static std::string resolveConfigPath();
    
    /**
     * @brief Gets the directory where the executable is located
     * @return Path to the executable directory
     * 
     * Used for resolving relative paths in configuration files.
     */
    static std::string getExecutableDir();

    // Core service components
    Config config_;                                    ///< Configuration data loaded from JSON
    zmq::context_t zmqContext_;                       ///< ZeroMQ context for all ZMQ operations
    std::unique_ptr<ZmqManager> zmqManager_;          ///< Manages ZeroMQ communications
    std::unique_ptr<UdpManager> udpManager_;          ///< Manages UDP communications
    
    // Protocol tracking for routing decisions
    std::unordered_map<std::string, std::string> uavProtocols_; ///< Maps UAV name to protocol ("TCP" or "UDP")
};

#endif // TELEMETRYSERVICE_H
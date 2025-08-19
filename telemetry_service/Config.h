/**
 * @file Config.h
 * @brief Configuration management for the telemetry service
 * 
 * This file defines structures and classes for loading and accessing
 * configuration data from JSON files. The configuration includes UAV
 * definitions, UI port settings, and logging preferences.
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <string>
#include <vector>
#include <nlohmann/json.hpp>

/**
 * @struct UAVConfig
 * @brief Configuration data for a single UAV
 * 
 * Contains all the network configuration needed to communicate with one UAV,
 * including ports for different communication protocols.
 */
struct UAVConfig {
    std::string name;               ///< Unique identifier for the UAV (e.g., "UAV_1")
    std::string ip;                 ///< IP address or hostname of the UAV
    int tcp_telemetry_port;         ///< TCP/ZeroMQ port for receiving telemetry data
    int tcp_command_port;           ///< TCP/ZeroMQ port for sending commands to UAV
    int udp_telemetry_port;         ///< UDP port for receiving telemetry data
};

/**
 * @struct UIConfig
 * @brief Configuration for UI communication ports
 * 
 * Defines the ports used for communication with user interface components.
 * All ports are required and must be specified in the JSON configuration.
 */
struct UIConfig {
    int tcp_command_port;    ///< Port for receiving commands from UI components (TCP)
    int tcp_publish_port;    ///< Port for publishing telemetry data to UI components (TCP)
    int udp_camera_port;     ///< Port for publishing camera telemetry data via UDP
    int udp_mapping_port;    ///< Port for publishing mapping telemetry data via UDP
    int udp_command_port;    ///< Port for receiving commands from UI components (UDP)
};

/**
 * @class Config
 * @brief Main configuration management class
 * 
 * Handles loading configuration from JSON files and provides access to
 * all configuration parameters needed by the telemetry service.
 */
class Config {
public:
    /**
     * @brief Load configuration from a JSON file
     * @param path Path to the JSON configuration file
     * @return true if configuration was loaded successfully, false otherwise
     * 
     * Parses the JSON file and populates the internal configuration structures.
     * Expected JSON format:
     * {
     *   "uavs": [...],
     *   "ui_ports": {...},
     *   "log_file": "..."
     * }
     */
    bool loadFromFile(const std::string& path);

    /**
     * @brief Get the list of configured UAVs
     * @return Reference to vector of UAV configurations
     */
    const std::vector<UAVConfig>& getUAVs() const { return uavs; }
    
    /**
     * @brief Get the UI port configuration
     * @return Reference to UI configuration structure
     */
    const UIConfig& getUiPorts() const { return uiPorts; }
    
    /**
     * @brief Get the log file path
     * @return Reference to log file path string
     */
    const std::string& getLogFile() const { return logFile; }

private:
    std::vector<UAVConfig> uavs;                    ///< List of configured UAVs
    UIConfig uiPorts;                               ///< UI communication ports
    std::string logFile;                            ///< Path to log file (required in JSON)
};

#endif // CONFIG_H
/**
 * @file Config.cpp
 * @brief Implementation of configuration loading functionality
 * 
 * This file contains the implementation for parsing JSON configuration files
 * and populating the configuration data structures.
 */

#include "Config.h"
#include <fstream>
#include <stdexcept>

/**
 * @brief Load configuration from a JSON file
 * @param path Path to the JSON configuration file
 * @return true if configuration was loaded successfully, false if file couldn't be opened
 * 
 * This method:
 * 1. Opens and parses the JSON file
 * 2. Extracts UAV configurations from the "uavs" array
 * 3. Loads UI port settings from "ui_ports" object
 * 4. Sets the log file path from "log_file" field
 * 
 * @throws nlohmann::json::exception if JSON parsing fails
 */
bool Config::loadFromFile(const std::string& path) {
    // Try to open the configuration file
    std::ifstream file(path);
    if (!file.is_open()) {
        return false;  // File not found or not accessible
    }

    // Parse JSON content
    nlohmann::json j;
    file >> j;
    file.close();

    // Process each UAV in the configuration
    for (const auto& uav_json : j["uavs"]) {
        UAVConfig uav;
        
        // Extract required UAV fields
        uav.name = uav_json["name"];
        uav.ip = uav_json["ip"];
        
        // Support both old and new field names for backward compatibility
        if (uav_json.contains("tcp_telemetry_port")) {
            uav.tcp_telemetry_port = uav_json["tcp_telemetry_port"];
        } else if (uav_json.contains("telemetry_port")) {
            uav.tcp_telemetry_port = uav_json["telemetry_port"];  // Backward compatibility
        }
        
        if (uav_json.contains("tcp_command_port")) {
            uav.tcp_command_port = uav_json["tcp_command_port"];
        } else if (uav_json.contains("command_port")) {
            uav.tcp_command_port = uav_json["command_port"];  // Backward compatibility
        }
        
        // Extract optional UDP telemetry port
        if (uav_json.contains("udp_telemetry_port")) {
            uav.udp_telemetry_port = uav_json["udp_telemetry_port"];
        } else {
            uav.udp_telemetry_port = -1; // Disabled if not specified
        }
        
        uavs.push_back(uav);
    }

    // Load UI port configuration if present
    if (j.contains("ui_ports")) {
        // Support both old and new field names for backward compatibility
        if (j["ui_ports"].contains("tcp_command_port")) {
            uiPorts.tcp_command_port = j["ui_ports"]["tcp_command_port"];
        } else if (j["ui_ports"].contains("command_port")) {
            uiPorts.tcp_command_port = j["ui_ports"]["command_port"];  // Backward compatibility
        }
        
        if (j["ui_ports"].contains("tcp_publish_port")) {
            uiPorts.tcp_publish_port = j["ui_ports"]["tcp_publish_port"];
        } else if (j["ui_ports"].contains("publish_port")) {
            uiPorts.tcp_publish_port = j["ui_ports"]["publish_port"];  // Backward compatibility
        }
        
        // Load optional UDP ports
        if (j["ui_ports"].contains("udp_camera_port")) {
            uiPorts.udp_camera_port = j["ui_ports"]["udp_camera_port"];
        }
        if (j["ui_ports"].contains("udp_mapping_port")) {
            uiPorts.udp_mapping_port = j["ui_ports"]["udp_mapping_port"];
        }
        if (j["ui_ports"].contains("udp_command_port")) {
            uiPorts.udp_command_port = j["ui_ports"]["udp_command_port"];
        }
    }

    // Load log file path if specified
    if (j.contains("log_file")) {
        logFile = j["log_file"];
    }
    
    return true;
}
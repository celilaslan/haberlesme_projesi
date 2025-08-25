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
 * 2. Extracts UAV configurations from the "uavs" array (TCP and UDP ports required)
 * 3. Loads UI port settings from "ui_ports" object (TCP and UDP ports required)
 * 4. Sets the log file path from "log_file" field
 *
 * @throws nlohmann::json::exception if JSON parsing fails
 */
bool Config::loadFromFile(const std::string& path) {
    // Try to open the configuration file
    std::ifstream file(path);
    if (!file.is_open()) {
        return false;
    }

    // Parse JSON content
    nlohmann::json json_data;
    file >> json_data;
    file.close();

    // Process each UAV in the configuration
    for (const auto& uav_json : json_data["uavs"]) {
        UAVConfig uav;

        // Extract required UAV fields with validation
        if (!uav_json.contains("name") || !uav_json.contains("ip")) {
            throw std::runtime_error("UAV configuration missing required 'name' or 'ip' field");
        }
        uav.name = uav_json["name"];
        uav.ip = uav_json["ip"];

        // Extract TCP ports configuration with validation
        if (!uav_json.contains("tcp_telemetry_port") || !uav_json.contains("tcp_command_port")) {
            throw std::runtime_error("UAV '" + uav.name + "' missing required TCP ports configuration");
        }
        uav.tcp_telemetry_port = uav_json["tcp_telemetry_port"];
        uav.tcp_command_port = uav_json["tcp_command_port"];

        // Extract UDP telemetry port with validation
        if (!uav_json.contains("udp_telemetry_port")) {
            throw std::runtime_error("UAV '" + uav.name + "' missing required UDP telemetry port configuration");
        }
        uav.udp_telemetry_port = uav_json["udp_telemetry_port"];

        // Validate port ranges
        auto validatePort = [&](int port, const std::string& portName) {
            if (port < 1 || port > 65535) {
                throw std::runtime_error("UAV '" + uav.name + "' has invalid " + portName + ": " + std::to_string(port)
                                         + " (must be 1-65535)");
            }
        };
        validatePort(uav.tcp_telemetry_port, "tcp_telemetry_port");
        validatePort(uav.tcp_command_port, "tcp_command_port");
        validatePort(uav.udp_telemetry_port, "udp_telemetry_port");

        uavs.push_back(uav);
    }

    // Load UI port configuration with validation
    if (!json_data.contains("ui_ports")) {
        throw std::runtime_error("Configuration missing required 'ui_ports' section");
    }
    const auto& ui_ports_json = json_data["ui_ports"];

    if (!ui_ports_json.contains("tcp_command_port") || !ui_ports_json.contains("tcp_publish_port")
        || !ui_ports_json.contains("udp_publish_port")) {
        throw std::runtime_error("UI ports configuration missing required fields");
    }

    uiPorts.tcp_command_port = ui_ports_json["tcp_command_port"];
    uiPorts.tcp_publish_port = ui_ports_json["tcp_publish_port"];
    uiPorts.udp_publish_port = ui_ports_json["udp_publish_port"];

    // Validate UI port ranges
    auto validateUIPort = [](int port, const std::string& portName) {
        if (port < 1 || port > 65535) {
            throw std::runtime_error("UI port '" + portName + "' has invalid value: " + std::to_string(port)
                                     + " (must be 1-65535)");
        }
    };
    validateUIPort(uiPorts.tcp_command_port, "tcp_command_port");
    validateUIPort(uiPorts.tcp_publish_port, "tcp_publish_port");
    validateUIPort(uiPorts.udp_publish_port, "udp_publish_port");

    // Load log file path if specified
    if (json_data.contains("log_file")) {
        logFile = json_data["log_file"];
    }

    return true;
}

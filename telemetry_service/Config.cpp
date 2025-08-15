#include "Config.h"
#include <fstream>
#include <stdexcept>

bool Config::loadFromFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return false;
    }

    nlohmann::json j;
    file >> j;
    file.close();

    for (const auto& uav_json : j["uavs"]) {
        UAVConfig uav;
        uav.name = uav_json["name"];
        uav.ip = uav_json["ip"];
        uav.telemetry_port = uav_json["telemetry_port"];
        uav.command_port = uav_json["command_port"];
        
        if (uav_json.contains("udp_telemetry_port")) {
            uav.udp_telemetry_port = uav_json["udp_telemetry_port"];
        } else {
            uav.udp_telemetry_port = -1; // Devre dışı
        }
        uavs.push_back(uav);
    }

    if (j.contains("ui_ports")) {
        uiPorts.command_port = j["ui_ports"]["command_port"];
        uiPorts.publish_port = j["ui_ports"]["publish_port"];
    }

    if (j.contains("log_file")) {
        logFile = j["log_file"];
    }
    
    return true;
}
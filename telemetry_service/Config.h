#ifndef CONFIG_H
#define CONFIG_H

#include <string>
#include <vector>
#include <nlohmann/json.hpp>

struct UAVConfig {
    std::string name;
    std::string ip;
    int telemetry_port;
    int command_port;
    int udp_telemetry_port;
};

struct UIConfig {
    int command_port = 5558;
    int publish_port = 5557;
};

class Config {
public:
    bool loadFromFile(const std::string& path);

    const std::vector<UAVConfig>& getUAVs() const { return uavs; }
    const UIConfig& getUiPorts() const { return uiPorts; }
    const std::string& getLogFile() const { return logFile; }

private:
    std::vector<UAVConfig> uavs;
    UIConfig uiPorts;
    std::string logFile = "telemetry_log.txt";
};

#endif // CONFIG_H
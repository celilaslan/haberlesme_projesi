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

using json = nlohmann::json;

std::atomic<bool> g_running(true);

struct UAVConfig {
    std::string name;
    std::string ip;
    int telemetry_port;
    int command_port;
};

std::string GetTimestamp() {
    const auto now = std::chrono::system_clock::now();
    const auto time_t_now = std::chrono::system_clock::to_time_t(now);
    const auto duration = now.time_since_epoch();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(duration) % 1000;

    std::ostringstream oss;
    struct tm time_info;
#if defined(_WIN32)
    localtime_s(&time_info, &time_t_now);
#else
    localtime_r(&time_t_now, &time_info);
#endif

    oss << std::put_time(&time_info, "%Y-%m-%d %H:%M:%S");
    oss << '.' << std::setfill('0') << std::setw(3) << ms.count();

    return oss.str();
}

void signalHandler(int signum) {
    std::cout << "[" << GetTimestamp() << "] Signal received. Shutting down..." << std::endl;
    g_running = false;
}

// JSON'dan UAV konfigürasyonunu yükle
UAVConfig LoadUAVConfig(const std::string& config_file, const std::string& uav_name) {
    std::ifstream file(config_file);
    if (!file.is_open()) {
        std::cerr << "ERROR: Cannot open config file: " << config_file << std::endl;
        std::cerr << "Make sure the file exists in the project root directory!" << std::endl;
        exit(1);
    }

    json j;
    file >> j;
    file.close();

    // UAV'ı bul
    for (const auto& uav_json : j["uavs"]) {
        if (uav_json["name"] == uav_name) {
            UAVConfig config;
            config.name = uav_json["name"];
            config.ip = uav_json["ip"];
            config.telemetry_port = uav_json["telemetry_port"];
            config.command_port = uav_json["command_port"];
            return config;
        }
    }

    std::cerr << "ERROR: UAV '" << uav_name << "' not found in config file!" << std::endl;
    std::cerr << "Available UAVs in config:" << std::endl;
    for (const auto& uav_json : j["uavs"]) {
        std::cerr << "  - " << uav_json["name"] << std::endl;
    }
    exit(1);
}

void PrintAvailableUAVs(const std::string& config_file) {
    std::ifstream file(config_file);
    if (!file.is_open()) {
        std::cerr << "Cannot open config file: " << config_file << std::endl;
        return;
    }

    json j;
    file >> j;
    file.close();

    std::cout << "Available UAVs in " << config_file << ":" << std::endl;
    for (const auto& uav_json : j["uavs"]) {
        std::cout << "  - " << uav_json["name"]
            << " (Telemetry: " << uav_json["telemetry_port"]
            << ", Commands: " << uav_json["command_port"] << ")" << std::endl;
    }
}

int main(int argc, char* argv[]) {
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    // Komut satırı argümanları kontrolü
    if (argc < 2) {
        std::cout << "Usage: " << argv[0] << " <UAV_NAME>" << std::endl;
        std::cout << "Example: " << argv[0] << " UAV_1" << std::endl;
        std::cout << std::endl;

        // Mevcut UAV'ları listele
        const char* cfgEnv = std::getenv("SERVICE_CONFIG");
        std::string config_path = cfgEnv ? std::string(cfgEnv) : std::string("service_config.json");
        PrintAvailableUAVs(config_path);
        return 1;
    }

    std::string uav_name = argv[1];
    const char* cfgEnv = std::getenv("SERVICE_CONFIG");
    std::string config_path = cfgEnv ? std::string(cfgEnv) : std::string("service_config.json");

    // UAV konfigürasyonunu yükle
    UAVConfig config;
    try {
        config = LoadUAVConfig(config_path, uav_name);
    }
    catch (const std::exception& e) {
        std::cerr << "Failed to load config: " << e.what() << std::endl;
        return 1;
    }

    std::cout << "===========================================" << std::endl;
    std::cout << "Starting UAV Simulator: " << config.name << std::endl;
    std::cout << "Telemetry Port: " << config.telemetry_port << std::endl;
    std::cout << "Command Port: " << config.command_port << std::endl;
    std::cout << "===========================================" << std::endl;

    zmq::context_t context(1);

    // PUSH socket for sending telemetry to service
    zmq::socket_t push_to_service(context, zmq::socket_type::push);
    std::string telemetry_addr = "tcp://" + config.ip + ":" + std::to_string(config.telemetry_port);
    push_to_service.connect(telemetry_addr);

    // PULL socket for receiving commands from service
    zmq::socket_t pull_commands(context, zmq::socket_type::pull);
    std::string command_addr = "tcp://" + config.ip + ":" + std::to_string(config.command_port);
    pull_commands.connect(command_addr);

    std::cout << "[" << GetTimestamp() << "] " << config.name << " Connected!" << std::endl;
    std::cout << "  Telemetry -> " << telemetry_addr << std::endl;
    std::cout << "  Commands <- " << command_addr << std::endl;

    // Send debug session start message
    std::string debug = config.name + " Debug session started at: " + GetTimestamp();
    zmq::message_t zmq_msg_debug(debug.begin(), debug.end());
    push_to_service.send(zmq_msg_debug, zmq::send_flags::none);
    std::cout << "[" << GetTimestamp() << "] [" << config.name << "] Sent: " << debug << std::endl;

    // Her UAV için farklı data aralıkları (UAV adına göre)
    int base_mapping = 1000;
    int base_camera = 2000;

    // UAV_1 -> 1001-1030, 2001-2030
    // UAV_2 -> 1501-1530, 2501-2530
    // UAV_3 -> 1801-1830, 2801-2830
    if (config.name == "UAV_2") {
        base_mapping = 1500;
        base_camera = 2500;
    }
    else if (config.name == "UAV_3") { 
        base_mapping = 1800;
        base_camera = 2800;
    }

    // Telemetry sender thread
    std::thread telemetry_sender([&]() {
        int mapping = base_mapping + 1;
        int camera = base_camera + 1;
        int sleep_interval = 500;  // Default interval

        // Her UAV için farklı interval
        if (config.name == "UAV_2") sleep_interval = 750;
        else if (config.name == "UAV_3") sleep_interval = 1000;

        for (int i = 0; i < 50 && g_running; ++i) {  // Daha uzun test için 50 mesaj
            // Send mapping data
            std::string msg1 = config.name + "  " + std::to_string(mapping + i);
            zmq::message_t zmq_msg1(msg1.begin(), msg1.end());
            push_to_service.send(zmq_msg1, zmq::send_flags::none);
            std::cout << "[" << GetTimestamp() << "] [" << config.name << "] Sent: " << msg1 << std::endl;

            std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Kısa ara
            
            // Send camera data
            std::string msg2 = config.name + "  " + std::to_string(camera + i);
            zmq::message_t zmq_msg2(msg2.begin(), msg2.end());
            push_to_service.send(zmq_msg2, zmq::send_flags::none);
            std::cout << "[" << GetTimestamp() << "] [" << config.name << "] Sent: " << msg2 << std::endl;

            std::this_thread::sleep_for(std::chrono::milliseconds(sleep_interval));
        }
        std::cout << "[" << GetTimestamp() << "] [" << config.name << "] Telemetry sending completed." << std::endl;
        });

    // Command receiver thread
    std::thread command_receiver([&]() {
        while (g_running) {
            zmq::message_t command;
            if (pull_commands.recv(command, zmq::recv_flags::dontwait)) {
                std::string cmd(static_cast<char*>(command.data()), command.size());

                std::cout << std::endl;
                std::cout << "============================================" << std::endl;
                std::cout << "[" << GetTimestamp() << "] [" << config.name << "] 🚁 UI MESSAGE: " << cmd << " 🚁" << std::endl;
                std::cout << "============================================" << std::endl;
                std::cout << std::endl;

                // UAV specific response
                std::string response = config.name + "_ACK: Command '" + cmd + "' received and processed";
                zmq::message_t ack_msg(response.begin(), response.end());
                push_to_service.send(ack_msg, zmq::send_flags::none);
                std::cout << "[" << GetTimestamp() << "] [" << config.name << "] Sent ACK: " << response << std::endl;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        std::cout << "[" << GetTimestamp() << "] [" << config.name << "] Command receiver stopped." << std::endl;
        });

    if (telemetry_sender.joinable()) telemetry_sender.join();
    if (command_receiver.joinable()) command_receiver.join();
    g_running = false;

    std::cout << "[" << GetTimestamp() << "] [" << config.name << "] Simulator stopped." << std::endl;
    return 0;
}

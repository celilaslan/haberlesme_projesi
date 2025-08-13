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
#include <boost/asio.hpp>

using json = nlohmann::json;
using boost::asio::ip::udp;

std::atomic<bool> g_running(true);

struct UAVConfig {
    std::string name;
    std::string ip;
    int telemetry_port;
    int command_port;
    int udp_telemetry_port; // Service's UDP port
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

    UAVConfig config;
    bool uav_found = false;

    // UAV'ı bul
    for (const auto& uav_json : j["uavs"]) {
        if (uav_json["name"] == uav_name) {
            config.name = uav_json["name"];
            config.ip = uav_json["ip"];
            config.telemetry_port = uav_json["telemetry_port"];
            config.command_port = uav_json["command_port"];
            uav_found = true;
            break;
        }
    }

    if (!uav_found) {
        std::cerr << "ERROR: UAV '" << uav_name << "' not found in config file!" << std::endl;
        exit(1);
    }

    // Global UDP portunu yükle
    if (j.contains("udp_telemetry_port")) {
        config.udp_telemetry_port = j["udp_telemetry_port"];
    } else {
        std::cerr << "ERROR: 'udp_telemetry_port' not found in config file!" << std::endl;
        exit(1);
    }

    return config;
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
        std::cout << "Usage: " << argv[0] << " <UAV_NAME> [--protocol udp|zmq]" << std::endl;
        std::cout << "Example: " << argv[0] << " UAV_1 --protocol udp" << std::endl;
        std::cout << std::endl;

        // Mevcut UAV'ları listele
        const char* cfgEnv = std::getenv("SERVICE_CONFIG");
        std::string config_path = cfgEnv ? std::string(cfgEnv) : std::string("service_config.json");
        PrintAvailableUAVs(config_path);
        return 1;
    }

    std::string uav_name = argv[1];
    std::string protocol = "zmq"; // default
    if (argc > 3 && std::string(argv[2]) == "--protocol") {
        protocol = argv[3];
    }

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
    if (protocol == "udp") {
        std::cout << "Protocol: UDP" << std::endl;
        std::cout << "Service UDP Port: " << config.udp_telemetry_port << std::endl;
    } else {
        std::cout << "Protocol: ZeroMQ (TCP)" << std::endl;
        std::cout << "Telemetry Port: " << config.telemetry_port << std::endl;
        std::cout << "Command Port: " << config.command_port << std::endl;
    }
    std::cout << "===========================================" << std::endl;

    // Telemetry sender thread
    std::thread telemetry_sender([&]() {
        // Base values for telemetry data
        int base_mapping = 1000;
        int base_camera = 2000;
        if (config.name == "UAV_2") {
            base_mapping = 3000;
            base_camera = 4000;
        } else if (config.name == "UAV_3") {
            base_mapping = 5000;
            base_camera = 6000;
        }

        int mapping = base_mapping + 1;
        int camera = base_camera + 1;
        int sleep_interval = 500;  // Default interval

        // Her UAV için farklı interval
        if (config.name == "UAV_2") sleep_interval = 750;
        else if (config.name == "UAV_3") sleep_interval = 1000;

        // Setup sockets based on protocol
        if (protocol == "udp") {
            boost::asio::io_context io_context;
            udp::socket socket(io_context, udp::endpoint(udp::v4(), 0));

            // Resolve the hostname to get an IP address
            udp::resolver resolver(io_context);
            udp::resolver::results_type endpoints = resolver.resolve(udp::v4(), config.ip, std::to_string(config.udp_telemetry_port));
            udp::endpoint remote_endpoint = *endpoints.begin();

            for (int i = 0; i < 50 && g_running; ++i) {
                // UDP messages must be prefixed with UAV name
                std::string msg1 = config.name + ":" + config.name + "  " + std::to_string(mapping + i);
                socket.send_to(boost::asio::buffer(msg1), remote_endpoint);
                std::cout << "[" << GetTimestamp() << "] [" << config.name << "] Sent (UDP): " << msg1 << std::endl;
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                
                std::string msg2 = config.name + ":" + config.name + "  " + std::to_string(camera + i);
                socket.send_to(boost::asio::buffer(msg2), remote_endpoint);
                std::cout << "[" << GetTimestamp() << "] [" << config.name << "] Sent (UDP): " << msg2 << std::endl;
                std::this_thread::sleep_for(std::chrono::milliseconds(sleep_interval));
            }
        } else { // ZMQ
            zmq::context_t context(1);
            zmq::socket_t push_to_service(context, zmq::socket_type::push);
            std::string telemetry_addr = "tcp://" + config.ip + ":" + std::to_string(config.telemetry_port);
            push_to_service.connect(telemetry_addr);

            for (int i = 0; i < 50 && g_running; ++i) {
                std::string msg1 = config.name + "  " + std::to_string(mapping + i);
                push_to_service.send(zmq::buffer(msg1), zmq::send_flags::none);
                std::cout << "[" << GetTimestamp() << "] [" << config.name << "] Sent (ZMQ): " << msg1 << std::endl;
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                
                std::string msg2 = config.name + "  " + std::to_string(camera + i);
                push_to_service.send(zmq::buffer(msg2), zmq::send_flags::none);
                std::cout << "[" << GetTimestamp() << "] [" << config.name << "] Sent (ZMQ): " << msg2 << std::endl;
                std::this_thread::sleep_for(std::chrono::milliseconds(sleep_interval));
            }
        }
        std::cout << "[" << GetTimestamp() << "] [" << config.name << "] Telemetry sending completed." << std::endl;
    });


    // Command receiver thread (only for ZMQ)
    std::thread command_receiver;
    if (protocol == "zmq") {
        command_receiver = std::thread([&]() {
            zmq::context_t context(1);
            zmq::socket_t pull_commands(context, zmq::socket_type::pull);
            std::string command_addr = "tcp://" + config.ip + ":" + std::to_string(config.command_port);
            pull_commands.connect(command_addr);

            while (g_running) {
                zmq::message_t command;
                if (pull_commands.recv(command, zmq::recv_flags::dontwait)) {
                    std::string cmd(static_cast<char*>(command.data()), command.size());

                    std::cout << std::endl;
                    std::cout << "============================================" << std::endl;
                    std::cout << "[" << GetTimestamp() << "] [" << config.name << "] 🚁 UI MESSAGE: " << cmd << " 🚁" << std::endl;
                    std::cout << "============================================" << std::endl;
                    std::cout << std::endl;

                    // This part needs a push socket to send back the ACK
                    // For simplicity, we'll just log it. A full implementation
                    // would require sharing the push_to_service socket.
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            std::cout << "[" << GetTimestamp() << "] [" << config.name << "] Command receiver stopped." << std::endl;
        });
    }


    if (telemetry_sender.joinable()) telemetry_sender.join();
    if (command_receiver.joinable()) command_receiver.join();
    g_running = false;

    std::cout << "[" << GetTimestamp() << "] [" << config.name << "] Simulator stopped." << std::endl;
    return 0;
}

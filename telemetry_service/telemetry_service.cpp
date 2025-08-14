#include <zmq.hpp>
#include <nlohmann/json.hpp>
#include <iostream>
#include <fstream>
#include <thread>
#include <chrono>
#include <string>
#include <iomanip>
#include <ctime>
#include <atomic>
#include <csignal>
#include <sstream>
#include <stdexcept>
#include <vector>
#include <map>
#include <cstdlib>
#include <filesystem>
#include <cstring>
#include <boost/asio.hpp>
#if defined(_WIN32)
#  include <windows.h>
#else
#  include <unistd.h>
#endif

using json = nlohmann::json;
using boost::asio::ip::udp;

std::atomic<bool> g_running(true);

struct UAVConfig {
    std::string name;
    std::string ip;
    int telemetry_port;  // UAV'den telemetri alma portu
    int command_port;    // UAV'ye komut gönderme portu
};

struct ServiceConfig {
    std::vector<UAVConfig> uavs;
    struct {
        int ui_command_port = 5558;    // UI'lardan komut alma
        int ui_publish_port = 5557;    // UI'lara telemetri yayınlama
    } ui_ports;
    int udp_telemetry_port = 5556; // UDP telemetri portu

    std::string log_file = "telemetry_log.txt";
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

void Log(const std::string& msg) {
    std::cout << "[" << GetTimestamp() << "] " << msg << std::endl;
}

void LogToFile(std::ofstream& logfile, const std::string& msg) {
    if (logfile.is_open()) {
        logfile << "[" << GetTimestamp() << "] " << msg << std::endl;
        logfile.flush();
    }
}

void signalHandler(int signum) {
    Log("Signal received. Shutting down...");
    g_running = false;
}

// Default konfigürasyon oluştur
ServiceConfig CreateDefaultConfig() {
    ServiceConfig config;

    // UAV_1 - localhost:5555 (telemetry), localhost:5559 (commands)
    config.uavs.push_back({ "UAV_1", "localhost", 5555, 5559 });

    // UAV_2 - localhost:5565 (telemetry), localhost:5569 (commands)
    config.uavs.push_back({ "UAV_2", "localhost", 5565, 5569 });

    return config;
}

//void SaveConfig(const std::string& filename, const ServiceConfig& config);
// Konfigürasyonu JSON'a kaydet
void SaveConfig(const std::string& filename, const ServiceConfig& config) {
    json j;

    // UAV'ları kaydet
    for (const auto& uav : config.uavs) {
        json uav_json;
        uav_json["name"] = uav.name;
        uav_json["ip"] = uav.ip;
        uav_json["telemetry_port"] = uav.telemetry_port;
        uav_json["command_port"] = uav.command_port;
        j["uavs"].push_back(uav_json);
    }

    // UI portları kaydet
    j["ui_ports"]["command_port"] = config.ui_ports.ui_command_port;
    j["ui_ports"]["publish_port"] = config.ui_ports.ui_publish_port;

    j["udp_telemetry_port"] = config.udp_telemetry_port;
    j["log_file"] = config.log_file;

    std::ofstream file(filename);
    file << j.dump(4);  // 4 space indentation
    file.close();

    Log("Config saved to: " + filename);
}

// JSON'dan konfigürasyon oku
ServiceConfig LoadConfig(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        Log("Config file not found. Creating default config: " + filename);
        ServiceConfig defaultConfig = CreateDefaultConfig();
        SaveConfig(filename, defaultConfig);
        return defaultConfig;
    }

    json j;
    file >> j;
    file.close();

    ServiceConfig config;

    // UAV'ları yükle
    for (const auto& uav_json : j["uavs"]) {
        UAVConfig uav;
        uav.name = uav_json["name"];
        uav.ip = uav_json["ip"];
        uav.telemetry_port = uav_json["telemetry_port"];
        uav.command_port = uav_json["command_port"];
        config.uavs.push_back(uav);
    }

    // UI portları yükle
    if (j.contains("ui_ports")) {
        config.ui_ports.ui_command_port = j["ui_ports"]["command_port"];
        config.ui_ports.ui_publish_port = j["ui_ports"]["publish_port"];
    }

    if (j.contains("udp_telemetry_port")) {
        config.udp_telemetry_port = j["udp_telemetry_port"];
    }

    if (j.contains("log_file")) {
        config.log_file = j["log_file"];
    }

    Log("Config loaded successfully. Found " + std::to_string(config.uavs.size()) + " UAVs");
    return config;
}


// Executable directory (for resolving relative log paths to land under telemetry_service)
static std::string GetExecutableDir() {
#if defined(_WIN32)
    char path[MAX_PATH];
    DWORD len = GetModuleFileNameA(nullptr, path, MAX_PATH);
    if (len == 0 || len == MAX_PATH) return std::filesystem::current_path().string();
    return std::filesystem::path(path).parent_path().string();
#else
    char buf[4096];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len == -1) return std::filesystem::current_path().string();
    buf[len] = '\0';
    return std::filesystem::path(buf).parent_path().string();
#endif
}


std::string ExtractUISource(const std::string& message) {
    if (message.find("[camera-ui]") != std::string::npos) {
        return "camera";
    }
    else if (message.find("[mapping-ui]") != std::string::npos) {
        return "mapping";
    }
    return "unknown";
}

// Function to process any telemetry message (from ZMQ or UDP)
void ProcessAndPublishTelemetry(const std::string& data, const std::string& source_description, zmq::socket_t& pub_socket, std::ofstream& logfile) {
    Log("Received from " + source_description + ": " + data);
    LogToFile(logfile, "RECEIVED from " + source_description + ": " + data);

    // Topic belirleme
    std::string topic = "unknown";
    std::string uav_name = "unknown_uav";

    // UDP messages are expected to have "UAV_NAME:data" format
    size_t colon_pos = data.find(':');
    std::string actual_data = data;
    if (colon_pos != std::string::npos) {
        uav_name = data.substr(0, colon_pos);
        actual_data = data.substr(colon_pos + 1);
    } else {
        // Fallback for ZMQ messages which don't have the prefix
        // We can infer UAV name from the source description for ZMQ
        size_t uav_pos = source_description.find("UAV_");
        if (uav_pos != std::string::npos) {
            uav_name = source_description;
        }
    }
    
    try {
        size_t last_space = actual_data.find_last_of(" \t");
        std::string numeric_part = (last_space != std::string::npos) ? actual_data.substr(last_space + 1) : actual_data;
        int code = std::stoi(numeric_part);
        if (code >= 1000 && code < 2000) topic = "mapping";
        else if (code >= 2000 && code < 3000) topic = "camera";
        else if (actual_data.find("Debug") != std::string::npos || actual_data.find("_ACK") != std::string::npos) topic = "system";
    } catch (const std::invalid_argument&) {
        if (actual_data.find("Debug") != std::string::npos || actual_data.find("_ACK") != std::string::npos) topic = "system";
        else Log("Warning: Could not parse message to determine topic: " + actual_data);
    }

    // UAV adını da topic'e ekle
    std::string full_topic = topic + "_" + uav_name;

    // UI'lara yayınla
    pub_socket.send(zmq::buffer(full_topic), zmq::send_flags::sndmore);
    pub_socket.send(zmq::buffer(actual_data), zmq::send_flags::none);

    Log("Published to [" + full_topic + "]: " + actual_data);
    LogToFile(logfile, "PUBLISHED: " + full_topic + " -> " + actual_data);
}


class UdpServer {
public:
    UdpServer(boost::asio::io_context& io_context, short port, zmq::socket_t& pub_socket, std::ofstream& logfile)
        : socket_(io_context, udp::endpoint(udp::v4(), port)),
          pub_socket_(pub_socket),
          logfile_(logfile) {
        do_receive();
    }

private:
    void do_receive() {
        socket_.async_receive_from(
            boost::asio::buffer(data_, max_length), remote_endpoint_,
            [this](boost::system::error_code ec, std::size_t bytes_recvd) {
                if (!ec && bytes_recvd > 0) {
                    std::string received_data(data_, bytes_recvd);
                    std::string source_desc = "UDP:" + remote_endpoint_.address().to_string();
                    ProcessAndPublishTelemetry(received_data, source_desc, pub_socket_, logfile_);
                }
                if (g_running) {
                    do_receive();
                }
            });
    }

    udp::socket socket_;
    udp::endpoint remote_endpoint_;
    enum { max_length = 1024 };
    char data_[max_length];
    zmq::socket_t& pub_socket_;
    std::ofstream& logfile_;
};


void TelemetryWorker() {
    // Konfigürasyon dosyasını çöz (ortam değişkeni, çalışma dizini veya executable dizini)
    auto resolveConfigPath = []() -> std::string {
        if (const char* env = std::getenv("SERVICE_CONFIG")) {
            if (std::filesystem::exists(env)) return std::string(env);
        }
        // Candidates
        std::vector<std::filesystem::path> candidates;
        candidates.emplace_back("service_config.json"); // current working dir
        std::filesystem::path exeDir = GetExecutableDir();
        candidates.push_back(exeDir / "service_config.json");
        candidates.push_back(exeDir.parent_path() / "service_config.json");
        for (auto &p : candidates) {
            std::error_code ec; if (std::filesystem::exists(p, ec)) return p.string();
        }
        // fallback to CWD name; it will be created later if missing
        return std::string("service_config.json");
    };
    std::string cfgPath = resolveConfigPath();
    ServiceConfig config = LoadConfig(cfgPath);

    // Resolve log path to executable directory when relative
    std::filesystem::path logPath(config.log_file);
    if (!logPath.is_absolute()) {
        logPath = std::filesystem::path(GetExecutableDir()) / logPath;
    }
    if (logPath.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(logPath.parent_path(), ec);
    }

    std::ofstream logfile(logPath, std::ios::trunc);
    if (!logfile.is_open()) {
        Log("Warning: Could not open log file " + logPath.string());
    }
    else {
        LogToFile(logfile, "=== SERVICE STARTED - Multi-UAV Telemetry Service ===");
        LogToFile(logfile, "Configured UAVs: " + std::to_string(config.uavs.size()));
        LogToFile(logfile, "UDP Telemetry Port: " + std::to_string(config.udp_telemetry_port));
    }
   
    zmq::context_t context(1);
    boost::asio::io_context io_context;

    // UI ile iletişim socketleri
    zmq::socket_t pub_to_ui(context, zmq::socket_type::pub);
    std::string ui_pub_addr = "tcp://*:" + std::to_string(config.ui_ports.ui_publish_port);
    pub_to_ui.bind(ui_pub_addr);
    Log("UI Publisher bound to: " + ui_pub_addr);

    zmq::socket_t pull_from_ui(context, zmq::socket_type::pull);
    std::string ui_cmd_addr = "tcp://*:" + std::to_string(config.ui_ports.ui_command_port);
    pull_from_ui.bind(ui_cmd_addr);
    Log("UI Command receiver bound to: " + ui_cmd_addr);

    // UDP Telemetry Server
    try {
        UdpServer udp_server(io_context, config.udp_telemetry_port, pub_to_ui, logfile);
        std::thread udp_thread([&io_context]() { 
            while(g_running) {
                try {
                    io_context.run();
                    break; // run() exited normally
                } catch (const std::exception& e) {
                    Log("UDP server error: " + std::string(e.what()));
                }
            }
        });
        Log("UDP Telemetry listener running on port: " + std::to_string(config.udp_telemetry_port));

        // Her UAV için telemetri alma socketleri
        std::vector<std::unique_ptr<zmq::socket_t>> uav_telemetry_sockets;
        std::vector<std::unique_ptr<zmq::socket_t>> uav_command_sockets;

        for (const auto& uav : config.uavs) {
            // Telemetri alma socketi
            auto pull_socket = std::make_unique<zmq::socket_t>(context, zmq::socket_type::pull);
            std::string telemetry_addr = "tcp://*:" + std::to_string(uav.telemetry_port);
            pull_socket->bind(telemetry_addr);
            uav_telemetry_sockets.push_back(std::move(pull_socket));

            // Komut gönderme socketi
            auto push_socket = std::make_unique<zmq::socket_t>(context, zmq::socket_type::push);
            std::string command_addr = "tcp://*:" + std::to_string(uav.command_port);
            push_socket->bind(command_addr);
            uav_command_sockets.push_back(std::move(push_socket));

            Log("UAV " + uav.name + " (ZMQ) - Telemetry: " + telemetry_addr + ", Commands: " + command_addr);
            LogToFile(logfile, "UAV " + uav.name + " (ZMQ) - Telemetry: " + telemetry_addr + ", Commands: " + command_addr);
        }

        Log("All sockets bound. Service running with " + std::to_string(config.uavs.size()) + " UAVs");

        // Telemetri alıcı thread - tüm UAV'lardan dinle (poll-based)
        std::thread receiver([&]() {
            // Build pollitems once
            std::vector<zmq::pollitem_t> pollitems;
            pollitems.reserve(uav_telemetry_sockets.size());
            for (auto &sockPtr : uav_telemetry_sockets) {
                pollitems.push_back(zmq::pollitem_t{ static_cast<void*>(*sockPtr), 0, ZMQ_POLLIN, 0 });
            }
            while (g_running) {
                if (!pollitems.empty()) {
                    zmq::poll(pollitems.data(), pollitems.size(), std::chrono::milliseconds(10));
                } else {
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                }
                for (size_t i = 0; i < pollitems.size(); ++i) {
                    if (pollitems[i].revents & ZMQ_POLLIN) {
                        zmq::message_t message;
                        if (!uav_telemetry_sockets[i]->recv(message, zmq::recv_flags::none)) continue;
                        std::string data(static_cast<char*>(message.data()), message.size());
                        std::string uav_name = config.uavs[i].name;
                        ProcessAndPublishTelemetry(data, uav_name, pub_to_ui, logfile);
                    }
                }
            }
            Log("Receiver thread stopped.");
            LogToFile(logfile, "=== RECEIVER THREAD STOPPED ===");
        });

        // UI komut yönlendirici thread
        std::thread forwarder([&]() {
            zmq::pollitem_t ui_poll{ static_cast<void*>(pull_from_ui), 0, ZMQ_POLLIN, 0 };
            while (g_running) {
                zmq::poll(&ui_poll, 1, std::chrono::milliseconds(10));
                if (ui_poll.revents & ZMQ_POLLIN) {
                    zmq::message_t ui_msg;
                    if (!pull_from_ui.recv(ui_msg, zmq::recv_flags::none)) continue;
                    std::string msg(static_cast<char*>(ui_msg.data()), ui_msg.size());

                    std::string ui_source = ExtractUISource(msg);
                    Log("RECEIVED FROM UI [" + ui_source + "]: " + msg);
                    LogToFile(logfile, "RECEIVED FROM UI [" + ui_source + "]: " + msg);

                    // Hedef UAV'ı ve komutu parse et
                    std::string target_uav = "UAV_1";  // default
                    std::string actual_cmd = msg;

                    size_t colon_pos = msg.find(':');
                    if (colon_pos != std::string::npos) {
                        target_uav = msg.substr(0, colon_pos);
                        actual_cmd = msg.substr(colon_pos + 1);
                    }

                    // Hedef UAV'ı bul ve komut gönder
                    for (size_t i = 0; i < config.uavs.size(); ++i) {
                        if (config.uavs[i].name == target_uav) {
                            Log("SENDING TO " + target_uav + ": " + actual_cmd);
                            LogToFile(logfile, "FORWARDING TO " + target_uav + ": " + actual_cmd);

                            uav_command_sockets[i]->send(zmq::buffer(actual_cmd), zmq::send_flags::none);

                            Log("MESSAGE SENT TO " + target_uav + " VIA PUSH!");
                            LogToFile(logfile, "MESSAGE SENT TO " + target_uav + " VIA PUSH: " + actual_cmd);
                            break;
                        }
                    }
                }
            }
            Log("Forwarder thread stopped.");
            LogToFile(logfile, "=== FORWARDER THREAD STOPPED ===");
            });

        while (g_running) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        io_context.stop();
        udp_thread.join();
        receiver.join();
        forwarder.join();

        LogToFile(logfile, "=== SERVICE SHUTDOWN COMPLETED ===");
        logfile.close();
        Log("Multi-UAV Telemetry service fully stopped.");

    } catch (const std::exception& e) {
        Log("Fatal error in TelemetryWorker: " + std::string(e.what()));
        LogToFile(logfile, "Fatal error: " + std::string(e.what()));
    }
}

int main() {
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    Log("Starting Multi-UAV Telemetry Service...");
    TelemetryWorker();

    return 0;
}
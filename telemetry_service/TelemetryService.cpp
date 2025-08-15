#include "TelemetryService.h"
#include "Logger.h"
#include <stdexcept>
#include <vector>
#include <cstdlib>

#if defined(_WIN32)
#  include <windows.h>
#else
#  include <unistd.h>
#endif

TelemetryService::TelemetryService() : zmqContext_(1) {}

void TelemetryService::run(std::atomic<bool>& app_running) {
    std::string cfgPath = resolveConfigPath();
    if (!config_.loadFromFile(cfgPath)) {
        throw std::runtime_error("Could not load config file: " + cfgPath);
    }
    
    std::filesystem::path logPath(config_.getLogFile());
    if (!logPath.is_absolute()) {
        logPath = std::filesystem::path(getExecutableDir()) / logPath;
    }
    if (logPath.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(logPath.parent_path(), ec);
    }

    Logger::init(logPath.string());
    Logger::info("=== SERVICE STARTED - Multi-UAV Telemetry Service ===");
    Logger::info("Config loaded successfully. Found " + std::to_string(config_.getUAVs().size()) + " UAVs");

    // Manager'ları başlat
    zmqManager_ = std::make_unique<ZmqManager>(zmqContext_, config_);
    udpManager_ = std::make_unique<UdpManager>(config_, 
        [this](const std::string& source, const std::string& data) {
            this->onUdpMessage(source, data);
        }
    );

    zmqManager_->start();
    udpManager_->start();

    Logger::info("All services running.");

    while (app_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    Logger::info("Shutdown signal received. Stopping services...");
    
    udpManager_->stop();
    zmqManager_->stop();
    
    udpManager_->join();
    zmqManager_->join();

    Logger::info("=== SERVICE SHUTDOWN COMPLETED ===");
}

void TelemetryService::onUdpMessage(const std::string& sourceDescription, const std::string& data) {
    processAndPublishTelemetry(data, sourceDescription);
}

void TelemetryService::processAndPublishTelemetry(const std::string& data, const std::string& source_description) {
    Logger::info("Received from " + source_description + ": " + data);
    
    std::string topic = "unknown";
    std::string uav_name = "unknown_uav";

    size_t colon_pos = data.find(':');
    std::string actual_data = data;
    if (colon_pos != std::string::npos) {
        uav_name = data.substr(0, colon_pos);
        actual_data = data.substr(colon_pos + 1);
    } else {
        uav_name = source_description;
    }
    
    try {
        size_t last_space = actual_data.find_last_of(" \t");
        std::string numeric_part = (last_space != std::string::npos) ? actual_data.substr(last_space + 1) : actual_data;
        int code = std::stoi(numeric_part);
        if (code >= 1000 && code < 3000) topic = "mapping";
        else if (code >= 3000 && code < 5000) topic = "camera"; // Örnek aralıklar
    } catch (...) {
        // Hata durumunda topic "unknown" kalır
    }

    std::string full_topic = topic + "_" + uav_name;
    zmqManager_->publishTelemetry(full_topic, actual_data);
}

std::string TelemetryService::resolveConfigPath() {
    if (const char* env = std::getenv("SERVICE_CONFIG")) {
        if (std::filesystem::exists(env)) return std::string(env);
    }
    std::vector<std::filesystem::path> candidates;
    candidates.emplace_back("service_config.json");
    std::filesystem::path exeDir = getExecutableDir();
    candidates.push_back(exeDir / "service_config.json");
    candidates.push_back(exeDir.parent_path() / "service_config.json");
    for (auto &p : candidates) {
        std::error_code ec; if (std::filesystem::exists(p, ec)) return p.string();
    }
    return "service_config.json";
}

std::string TelemetryService::getExecutableDir() {
#if defined(_WIN32)
    char path[MAX_PATH];
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    return std::filesystem::path(path).parent_path().string();
#else
    char buf[4096];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    buf[len] = '\0';
    return std::filesystem::path(buf).parent_path().string();
#endif
}
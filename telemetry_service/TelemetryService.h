#ifndef TELEMETRYSERVICE_H
#define TELEMETRYSERVICE_H

#include "Config.h"
#include "ZmqManager.h"
#include "UdpManager.h"
#include <atomic>
#include <memory>
#include <filesystem>

class TelemetryService {
public:
    TelemetryService();
    void run(std::atomic<bool>& app_running);

private:
    void onUdpMessage(const std::string& sourceDescription, const std::string& data);
    // ZMQ için yeni handler metot
    void onZmqMessage(const std::string& sourceDescription, const std::string& data);
    
    void processAndPublishTelemetry(const std::string& data, const std::string& source_description);
    
    static std::string resolveConfigPath();
    static std::string getExecutableDir();

    Config config_;
    zmq::context_t zmqContext_;
    std::unique_ptr<ZmqManager> zmqManager_;
    std::unique_ptr<UdpManager> udpManager_;
};

#endif // TELEMETRYSERVICE_H
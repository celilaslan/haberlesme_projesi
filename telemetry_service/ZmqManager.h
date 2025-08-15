#ifndef ZMQMANAGER_H
#define ZMQMANAGER_H

#include <zmq.hpp>
#include <thread>
#include <atomic>
#include <memory>
#include <functional> // functional başlık dosyasını ekleyin
#include "Config.h"

// Gelen ZMQ mesajlarını işlemek için callback fonksiyonu tanımı
using ZmqMessageCallback = std::function<void(const std::string&, const std::string&)>;

class ZmqManager {
public:
    // Constructor'ı callback alacak şekilde güncelleyin
    ZmqManager(zmq::context_t& ctx, const Config& config, ZmqMessageCallback callback);
    ~ZmqManager();

    void start();
    void stop();
    void join();
    
    // Telemetri verilerini yayınlamak için public metot
    void publishTelemetry(const std::string& topic, const std::string& data);

private:
    void receiverLoop();
    void forwarderLoop();
    std::string extractUISource(const std::string& message);

    zmq::context_t& context;
    const Config& config;
    std::atomic<bool> running{false};

    // Callback fonksiyonunu üye olarak ekleyin
    ZmqMessageCallback messageCallback_;

    // Socket'ler
    std::unique_ptr<zmq::socket_t> pubToUi;
    std::unique_ptr<zmq::socket_t> pullFromUi;
    std::vector<std::unique_ptr<zmq::socket_t>> uavTelemetrySockets;
    std::vector<std::unique_ptr<zmq::socket_t>> uavCommandSockets;
    
    // Thread'ler
    std::thread receiverThread;
    std::thread forwarderThread;
};

#endif // ZMQMANAGER_H
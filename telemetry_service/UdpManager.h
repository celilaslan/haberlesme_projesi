#ifndef UDPMANAGER_H
#define UDPMANAGER_H

#include <boost/asio.hpp>
#include <thread>
#include <atomic>
#include <memory>
#include <functional>
#include "Config.h"

using boost::asio::ip::udp;

// Gelen UDP mesajlarını işlemek için callback fonksiyonu tanımı
using UdpMessageCallback = std::function<void(const std::string&, const std::string&)>;

// Tek bir UDP sunucusunu yöneten yardımcı sınıf
class UdpServer {
public:
    UdpServer(boost::asio::io_context& io_context, const std::string& ip, short port, 
              const std::string& uav_name, UdpMessageCallback callback);

private:
    void do_receive();

    udp::socket socket_;
    udp::endpoint remote_endpoint_;
    enum { max_length = 1024 };
    char data_[max_length];
    std::string uav_name_;
    UdpMessageCallback messageCallback_;
};


class UdpManager {
public:
    UdpManager(const Config& config, UdpMessageCallback callback);
    ~UdpManager();

    void start();
    void stop();
    void join();

private:
    boost::asio::io_context io_context_;
    const Config& config_;
    UdpMessageCallback messageCallback_;
    std::atomic<bool> running_{false};
    
    std::vector<std::unique_ptr<UdpServer>> servers_;
    std::thread serviceThread_;
};

#endif // UDPMANAGER_H
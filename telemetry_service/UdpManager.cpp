#include "UdpManager.h"
#include "Logger.h"

// --- UdpServer Implementation ---
UdpServer::UdpServer(boost::asio::io_context& io_context, const std::string& ip, short port, 
                     const std::string& uav_name, UdpMessageCallback callback)
    : socket_(io_context), uav_name_(uav_name), messageCallback_(std::move(callback)) {
    
    udp::resolver resolver(io_context);
    udp::resolver::results_type endpoints = resolver.resolve(udp::v4(), ip, std::to_string(port));
    udp::endpoint bind_endpoint = *endpoints.begin();
    
    socket_.open(udp::v4());
    socket_.bind(bind_endpoint);
    
    Logger::info("UDP Server for " + uav_name + " bound to " + ip + ":" + std::to_string(port));
    do_receive();
}

void UdpServer::do_receive() {
    socket_.async_receive_from(
        boost::asio::buffer(data_, max_length), remote_endpoint_,
        [this](boost::system::error_code ec, std::size_t bytes_recvd) {
            if (!ec && bytes_recvd > 0) {
                std::string received_data(data_, bytes_recvd);
                std::string source_desc = "UDP:" + uav_name_;
                if (messageCallback_) {
                    messageCallback_(source_desc, received_data);
                }
            }
            // Döngüye devam et
            do_receive();
        });
}

// --- UdpManager Implementation ---
UdpManager::UdpManager(const Config& config, UdpMessageCallback callback)
    : config_(config), messageCallback_(std::move(callback)) {}

UdpManager::~UdpManager() {
    stop();
    join();
}

void UdpManager::start() {
    running_ = true;
    for (const auto& uav : config_.getUAVs()) {
        if (uav.udp_telemetry_port > 0) {
            servers_.push_back(std::make_unique<UdpServer>(
                io_context_, uav.ip, uav.udp_telemetry_port, uav.name, messageCallback_
            ));
        }
    }

    if (!servers_.empty()) {
        serviceThread_ = std::thread([this]() {
            while (running_) {
                try {
                    io_context_.run();
                    if (!running_) break; 
                    io_context_.restart(); // run() durduysa yeniden başlat
                } catch (const std::exception& e) {
                    Logger::error("UDP service thread error: " + std::string(e.what()));
                }
            }
        });
    }
}

void UdpManager::stop() {
    running_ = false;
    io_context_.stop();
}

void UdpManager::join() {
    if (serviceThread_.joinable()) {
        serviceThread_.join();
    }
}
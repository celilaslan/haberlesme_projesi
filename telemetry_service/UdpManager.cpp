/**
 * @file UdpManager.cpp
 * @brief Implementation of UDP communication management
 * 
 * This file contains the implementation of UDP-based telemetry communication,
 * providing an alternative to ZeroMQ for UAVs that require lower latency
 * or have simpler networking requirements.
 */

#include "UdpManager.h"
#include "Logger.h"

// --- UdpServer Implementation ---

/**
 * @brief Constructor - initializes UDP server for one UAV
 * @param io_context Boost.Asio I/O context for async operations
 * @param ip IP address to bind to (e.g., "localhost" or "0.0.0.0")
 * @param port UDP port number to listen on
 * @param uav_name Identifier for the UAV this server handles
 * @param callback Function to call when messages are received
 * 
 * Sets up a UDP socket bound to the specified address and port,
 * then starts the asynchronous receive loop.
 */
UdpServer::UdpServer(boost::asio::io_context& io_context, const std::string& ip, short port, 
                     const std::string& uav_name, UdpMessageCallback callback)
    : socket_(io_context), uav_name_(uav_name), messageCallback_(std::move(callback)) {
    
    // Resolve the IP address and port to a UDP endpoint
    udp::resolver resolver(io_context);
    udp::resolver::results_type endpoints = resolver.resolve(udp::v4(), ip, std::to_string(port));
    udp::endpoint bind_endpoint = *endpoints.begin();
    
    // Create and bind the UDP socket
    socket_.open(udp::v4());
    socket_.bind(bind_endpoint);
    
    Logger::status("UDP", "Server bound for " + uav_name, ip + ":" + std::to_string(port));
    
    // Start the asynchronous receive loop
    do_receive();
}

/**
 * @brief Start asynchronous receive operation
 * 
 * Sets up an async_receive_from operation that continuously listens for
 * incoming UDP packets. When a packet is received, it processes the data
 * and automatically sets up the next receive operation.
 */
void UdpServer::do_receive() {
    socket_.async_receive_from(
        boost::asio::buffer(data_, max_length), remote_endpoint_,
        [this](boost::system::error_code ec, std::size_t bytes_recvd) {
            // Process received data if no error occurred
            if (!ec && bytes_recvd > 0) {
                std::string received_data(data_, bytes_recvd);
                std::string source_desc = "UDP:" + uav_name_;
                
                // Call the registered callback with the received data
                if (messageCallback_) {
                    messageCallback_(source_desc, received_data);
                }
            }
            
            // Continue the receive loop (recursive call)
            do_receive();
        });
}

// --- UdpManager Implementation ---

/**
 * @brief Constructor - initializes UDP manager with configuration
 * @param config Configuration containing UAV settings
 * @param callback Function to call when UDP messages are received
 */
UdpManager::UdpManager(const Config& config, UdpMessageCallback callback)
    : config_(config), messageCallback_(std::move(callback)) {}

/**
 * @brief Destructor - ensures clean shutdown
 */
UdpManager::~UdpManager() {
    stop();
    join();
}

/**
 * @brief Start UDP communication system
 * 
 * This method:
 * 1. Creates UDP servers for all UAVs with valid UDP ports
 * 2. Starts a background thread to run the I/O context
 * 3. Handles any errors that occur during UDP operations
 */
void UdpManager::start() {
    running_ = true;
    
    // Create UDP servers for each UAV with UDP telemetry enabled
    for (const auto& uav : config_.getUAVs()) {
        if (uav.udp_telemetry_port > 0) {
            servers_.push_back(std::make_unique<UdpServer>(
                io_context_, uav.ip, uav.udp_telemetry_port, uav.name, messageCallback_
            ));
        }
    }

    // Start the I/O service thread if we have any UDP servers
    if (!servers_.empty()) {
        serviceThread_ = std::thread([this]() {
            while (running_) {
                try {
                    // Run the I/O context to process async operations
                    io_context_.run();
                    
                    // If we're still supposed to be running, restart the context
                    if (!running_) break; 
                    io_context_.restart(); // Restart if run() returned but we're still running
                } catch (const std::exception& e) {
                    Logger::error("UDP service thread error: " + std::string(e.what()));
                }
            }
        });
    }
}

/**
 * @brief Stop UDP communication system
 * 
 * Sets the running flag to false and stops the I/O context,
 * which will cause all async operations to complete and the
 * background thread to exit.
 */
void UdpManager::stop() {
    running_ = false;
    io_context_.stop();
}

/**
 * @brief Wait for background thread to complete
 * 
 * Blocks until the I/O service thread has finished execution.
 * Should be called after stop().
 */
void UdpManager::join() {
    if (serviceThread_.joinable()) {
        serviceThread_.join();
    }
}
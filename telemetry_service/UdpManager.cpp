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
 * @param address IP address to bind to (e.g., "localhost" or "0.0.0.0")
 * @param port UDP port number to listen on
 * @param uav_name Identifier for the UAV this server handles
 * @param callback Function to call when messages are received
 *
 * Sets up a UDP socket bound to the specified address and port,
 * then starts the asynchronous receive loop.
 */
UdpServer::UdpServer(boost::asio::io_context& io_context, const std::string& address, short port,
                     const std::string& uav_name, UdpMessageCallback callback)
    : socket_(io_context), uav_name_(uav_name), messageCallback_(std::move(callback)) {
    try {
        // Resolve the IP address and port to a UDP endpoint
        udp::resolver resolver(io_context);
        udp::resolver::results_type endpoints = resolver.resolve(udp::v4(), address, std::to_string(port));
        udp::endpoint bind_endpoint = *endpoints.begin();

        // Create and bind the UDP socket
        socket_.open(udp::v4());
        socket_.bind(bind_endpoint);

        Logger::statusWithDetails("UDP", StatusMessage("Server bound for " + uav_name), DetailMessage(address + ":" + std::to_string(port)));

        // Start the asynchronous receive loop
        doReceive();
    } catch (const std::exception& e) {
        Logger::error("UDP Server setup failed for " + uav_name + ": " + std::string(e.what()));
        throw;
    }
}

/**
 * @brief Start asynchronous receive operation
 *
 * Sets up an async_receive_from operation that continuously listens for
 * incoming UDP packets. When a packet is received, it processes the data
 * and automatically sets up the next receive operation.
 */
void UdpServer::doReceive() {
    socket_.async_receive_from(
        boost::asio::buffer(data_.data(), data_.size()), remote_endpoint_,
        [this](boost::system::error_code error_code, std::size_t bytes_recvd) {
            if (!error_code && bytes_recvd > 0) {
                try {
                    // Process received data if no error occurred
                    std::string received_data(data_.data(), bytes_recvd);
                    std::string source_desc = "UDP:" + uav_name_;

                    // Call the registered callback with the received data
                    if (messageCallback_) {
                        messageCallback_(source_desc, received_data);
                    }
                } catch (const std::exception& e) {
                    Logger::error("UDP receive processing error for " + uav_name_ + ": " + std::string(e.what()));
                }
            } else if (error_code && error_code != boost::asio::error::operation_aborted) {
                Logger::error("UDP receive error for " + uav_name_ + ": " + error_code.message());
            }

            // Continue the receive loop only if no critical error
            if (!error_code || error_code == boost::asio::error::message_size) {
                doReceive();
            }
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

    // Explicit socket cleanup
    std::lock_guard<std::mutex> lock(socketMutex_);
    try {
        if (cameraPublishSocket_ && cameraPublishSocket_->is_open()) {
            cameraPublishSocket_->close();
        }
        if (mappingPublishSocket_ && mappingPublishSocket_->is_open()) {
            mappingPublishSocket_->close();
        }
        cameraPublishSocket_.reset();
        mappingPublishSocket_.reset();
        servers_.clear();
    } catch (const std::exception& e) {
        Logger::error("UDP cleanup error: " + std::string(e.what()));
    }
}

/**
 * @brief Start UDP communication system
 *
 * This method:
 * 1. Creates UDP servers for all UAVs with valid UDP ports
 * 2. Sets up UDP publishing socket for UI communication
 * 3. Starts a background thread to run the I/O context
 * 4. Handles any errors that occur during UDP operations
 */
void UdpManager::start() {
    running_ = true;

    try {
        std::lock_guard<std::mutex> lock(socketMutex_);

        // Create UDP servers for each UAV with UDP telemetry enabled
        for (const auto& uav : config_.getUAVs()) {
            if (uav.udp_telemetry_port > 0 && uav.udp_telemetry_port <= 65535) {
                servers_.push_back(std::make_unique<UdpServer>(io_context_, uav.ip, uav.udp_telemetry_port, uav.name,
                                                               messageCallback_));
            } else if (uav.udp_telemetry_port > 0) {
                Logger::warn("Invalid UDP port for " + uav.name + ": " + std::to_string(uav.udp_telemetry_port));
            }
        }

        // Set up UDP multicast publishing sockets for UI communication
        cameraPublishSocket_ = std::make_unique<udp::socket>(io_context_, udp::endpoint(udp::v4(), 0));
        mappingPublishSocket_ = std::make_unique<udp::socket>(io_context_, udp::endpoint(udp::v4(), 0));

        // Use multicast addresses instead of unicast to allow multiple subscribers
        cameraEndpoint_ =
            udp::endpoint(boost::asio::ip::address::from_string("239.0.0.1"), config_.getUiPorts().udp_camera_port);
        mappingEndpoint_ =
            udp::endpoint(boost::asio::ip::address::from_string("239.0.0.2"), config_.getUiPorts().udp_mapping_port);
        Logger::statusWithDetails("UDP", StatusMessage("UI Camera Publisher socket created"),
                       DetailMessage("Port: " + std::to_string(config_.getUiPorts().udp_camera_port)));
        Logger::statusWithDetails("UDP", StatusMessage("UI Mapping Publisher socket created"),
                       DetailMessage("Port: " + std::to_string(config_.getUiPorts().udp_mapping_port)));
    } catch (const std::exception& e) {
        Logger::error("UDP setup failed: " + std::string(e.what()));
        running_ = false;
        throw;
    }

    // Start the I/O service thread if we have any UDP servers or publishing is enabled
    if (!servers_.empty() || cameraPublishSocket_ || mappingPublishSocket_) {
        serviceThread_ = std::thread([this]() {
            while (running_) {
                try {
                    // Run the I/O context to process async operations
                    io_context_.run();

                    // If run() returned and we're still supposed to be running,
                    // it means all handlers completed, so restart the context
                    if (running_) {
                        io_context_.restart();
                        std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    }
                } catch (const std::exception& e) {
                    Logger::error("UDP service thread error: " + std::string(e.what()));
                    if (running_) {
                        // Wait longer on errors to prevent busy waiting
                        std::this_thread::sleep_for(std::chrono::milliseconds(500));
                        io_context_.restart();
                    }
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

/**
 * @brief Publish telemetry data to UI components via UDP
 * @param topic The topic to publish on (e.g., "camera_UAV_1")
 * @param data The telemetry data to send
 *
 * Sends telemetry data to UI components using UDP. Routes camera topics
 * to camera port and mapping topics to mapping port. Thread-safe.
 */
void UdpManager::publishTelemetry(const std::string& topic, const std::string& data) {
    try {
        std::lock_guard<std::mutex> lock(socketMutex_);

        // Determine which socket to use based on topic
        udp::socket* socket_ptr = nullptr;
        udp::endpoint* endpoint_ptr = nullptr;

        if (topic.find("camera") == 0) {
            socket_ptr = cameraPublishSocket_.get();
            endpoint_ptr = &cameraEndpoint_;
        } else if (topic.find("mapping") == 0) {
            socket_ptr = mappingPublishSocket_.get();
            endpoint_ptr = &mappingEndpoint_;
        } else {
            Logger::error("Unknown topic type for UDP publish: " + topic);
            return;
        }

        if (socket_ptr == nullptr || !socket_ptr->is_open()) {
            Logger::error("UDP publish socket not available for topic: " + topic);
            return;
        }

        // Format message as "topic|data" for easy parsing by UI components
        // Use more efficient string construction
        std::string message;
        message.reserve(topic.size() + data.size() + 1);
        message = topic + "|" + data;

        socket_ptr->send_to(boost::asio::buffer(message), *endpoint_ptr);
        Logger::info("UDP Published to [" + topic + "]: " + data);

    } catch (const std::exception& e) {
        Logger::error("UDP publish error: " + std::string(e.what()));
    }
}

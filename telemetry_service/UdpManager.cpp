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
#include "TelemetryPackets.h"

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

        Logger::statusWithDetails("UDP", StatusMessage("Server bound for " + uav_name),
                                  DetailMessage(address + ":" + std::to_string(port)));

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
                    // Process received binary data if no error occurred
                    std::vector<uint8_t> received_data(data_.data(), data_.data() + bytes_recvd);
                    // Call callback with UAV name directly
                    if (messageCallback_) {
                        messageCallback_(uav_name_, received_data);
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
 * @param topic The topic to publish on (e.g., "target.camera.UAV_1" or "type.location.UAV_1")
 * @param data The binary telemetry data to send
 *
 * Routing behavior:
 * - Target topics (target.camera.*, target.mapping.*) go only to their designated UI
 * - Type topics are routed based on their specific type:
 *   - type.location.*: primarily to mapping UI, also to camera UI for cross-subscription
 *   - type.status.*: primarily to camera UI, also to mapping UI for cross-subscription
 */
void UdpManager::publishTelemetry(const std::string& topic, const std::vector<uint8_t>& data) {
    try {
        std::lock_guard<std::mutex> lock(socketMutex_);

        // Format message as "topic|data" for easy parsing by UI components
        std::vector<uint8_t> message;
        message.reserve(topic.size() + 1 + data.size());

        // Add topic as text
        message.insert(message.end(), topic.begin(), topic.end());
        // Add separator
        message.push_back('|');
        // Add binary data
        message.insert(message.end(), data.begin(), data.end());

        // Helper function to decode packet info from binary data
        auto getPacketInfo = [](const std::vector<uint8_t>& data) -> std::string {
            if (data.size() >= sizeof(PacketHeader)) {
                const PacketHeader* header = reinterpret_cast<const PacketHeader*>(data.data());

                std::string targetName;
                switch (header->targetID) {
                    case TargetIDs::CAMERA: targetName = "Camera"; break;
                    case TargetIDs::MAPPING: targetName = "Mapping"; break;
                    default: targetName = "Unknown(" + std::to_string(header->targetID) + ")"; break;
                }

                std::string typeName;
                switch (header->packetType) {
                    case PacketTypes::LOCATION: typeName = "Location"; break;
                    case PacketTypes::STATUS: typeName = "Status"; break;
                    default: typeName = "Unknown(" + std::to_string(header->packetType) + ")"; break;
                }

                return " - Target: " + targetName + ", Type: " + typeName;
            }
            return "";
        };

        // Helper function to create hex dump
        auto getHexDump = [](const std::vector<uint8_t>& data) -> std::string {
            std::string hexData = "";
            size_t hexLimit = std::min(data.size(), static_cast<size_t>(32));
            for (size_t i = 0; i < hexLimit; ++i) {
                char hex[4];
                snprintf(hex, sizeof(hex), "%02X ", data[i]);
                hexData += hex;
            }
            if (data.size() > 32) hexData += "...";
            return " | Hex: " + hexData;
        };

        // Route based on topic type
        if (topic.find("target.camera.") == 0) {
            // Target-based routing: camera UI gets its targeted data
            if (cameraPublishSocket_ && cameraPublishSocket_->is_open()) {
                cameraPublishSocket_->send_to(boost::asio::buffer(message.data(), message.size()), cameraEndpoint_);
                Logger::info("UDP Published to [" + topic + "]: " + std::to_string(data.size()) + " bytes" + getPacketInfo(data) + getHexDump(data));
            }
        } else if (topic.find("target.mapping.") == 0) {
            // Target-based routing: mapping UI gets its targeted data
            if (mappingPublishSocket_ && mappingPublishSocket_->is_open()) {
                mappingPublishSocket_->send_to(boost::asio::buffer(message.data(), message.size()), mappingEndpoint_);
                Logger::info("UDP Published to [" + topic + "]: " + std::to_string(data.size()) + " bytes" + getPacketInfo(data) + getHexDump(data));
            }
        } else if (topic.find("type.location.") == 0) {
            // Location data: primarily for mapping UI, but camera UI can subscribe if needed
            std::string packetInfo = getPacketInfo(data);
            std::string hexInfo = getHexDump(data);
            if (mappingPublishSocket_ && mappingPublishSocket_->is_open()) {
                mappingPublishSocket_->send_to(boost::asio::buffer(message.data(), message.size()), mappingEndpoint_);
                Logger::info("UDP Published to [" + topic + "]: " + std::to_string(data.size()) + " bytes" + packetInfo + hexInfo);
            }
            if (cameraPublishSocket_ && cameraPublishSocket_->is_open()) {
                cameraPublishSocket_->send_to(boost::asio::buffer(message.data(), message.size()), cameraEndpoint_);
                Logger::info("UDP Published to [" + topic + "]: " + std::to_string(data.size()) + " bytes" + packetInfo + hexInfo);
            }
        } else if (topic.find("type.status.") == 0) {
            // Status data: primarily for camera UI, but mapping UI can subscribe if needed
            std::string packetInfo = getPacketInfo(data);
            std::string hexInfo = getHexDump(data);
            if (cameraPublishSocket_ && cameraPublishSocket_->is_open()) {
                cameraPublishSocket_->send_to(boost::asio::buffer(message.data(), message.size()), cameraEndpoint_);
                Logger::info("UDP Published to [" + topic + "]: " + std::to_string(data.size()) + " bytes" + packetInfo + hexInfo);
            }
            if (mappingPublishSocket_ && mappingPublishSocket_->is_open()) {
                mappingPublishSocket_->send_to(boost::asio::buffer(message.data(), message.size()), mappingEndpoint_);
                Logger::info("UDP Published to [" + topic + "]: " + std::to_string(data.size()) + " bytes" + packetInfo + hexInfo);
            }
        } else {
            Logger::error("Unknown topic format for UDP publish: " + topic);
            return;
        }

    } catch (const std::exception& e) {
        Logger::error("UDP publish error: " + std::string(e.what()));
    }
}

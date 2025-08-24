/**
 * @file UdpManager.cpp
 * @brief Implementation of UDP communication management
 *
 * This file contains the implementation of UDP-based telemetry communication,
 * providing an alternative to TCP for UAVs that prefer UDP for
 * lower latency or simpler networking requirements.
 */

#include "UdpManager.h"

#include <sstream>
#include "Logger.h"
#include "TelemetryPackets.h"

// --- UdpServer Implementation ---

/**
 * @brief Constructor - initializes UDP server for one UAV
 * @param io_context Boost.Asio I/O context for async operations
 * @param address IP address to bind to
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
 * @param config Configuration containing UAV and UI port settings
 * @param callback Function to call when UDP messages are received
 *
 * Initializes the UDP manager with the necessary configuration and sets up
 * the callback for handling incoming telemetry messages.
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
        if (publishSocket_ && publishSocket_->is_open()) {
            publishSocket_->close();
        }
        if (subscriptionSocket_ && subscriptionSocket_->is_open()) {
            subscriptionSocket_->close();
        }
        publishSocket_.reset();
        subscriptionSocket_.reset();
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

        // Set up UDP publishing socket for UI communication
        publishSocket_ = std::make_unique<udp::socket>(io_context_, udp::endpoint(udp::v4(), 0));

        // Set up subscription management socket for receiving subscription requests from UI
        subscriptionSocket_ = std::make_unique<udp::socket>(io_context_, udp::endpoint(udp::v4(), config_.getUiPorts().udp_publish_port));

        Logger::statusWithDetails("UDP", StatusMessage("UI Publisher bound"),
                                  DetailMessage("Port: " + std::to_string(config_.getUiPorts().udp_publish_port)));

        // Start receiving subscription requests
        startSubscriptionReceive();    } catch (const std::exception& e) {
        Logger::error("UDP setup failed: " + std::string(e.what()));
        running_ = false;
        throw;
    }

    // Start the I/O service thread if we have any UDP servers or publishing socket
    if (!servers_.empty() || publishSocket_) {
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
 * @brief Publish telemetry data to subscribed UI components via UDP
 * @param topic The topic being published (e.g., "target.camera.UAV_1" or "type.location.UAV_1")
 * @param data The binary telemetry data to send
 *
 * Sends telemetry data only to UI clients that have subscribed to this topic.
 * This provides the same subscription functionality as TCP but over UDP.
 * Thread-safe through mutex protection.
 */
void UdpManager::publishTelemetry(const std::string& topic, const std::vector<uint8_t>& data) {
    try {
        std::lock_guard<std::mutex> lock(socketMutex_);
        if (publishSocket_ && running_) {
            // Get subscribers for this topic
            std::vector<udp::endpoint> subscribers = getSubscribers(topic);

            if (subscribers.empty()) {
                return; // No subscribers, don't send anything
            }

            // Format message as "topic|data" for parsing by UI components (same as TCP)
            std::vector<uint8_t> message;
            message.reserve(topic.size() + 1 + data.size());
            message.insert(message.end(), topic.begin(), topic.end());
            message.push_back('|');
            message.insert(message.end(), data.begin(), data.end());

            // Send to each subscribed client
            for (const auto& subscriber : subscribers) {
                publishSocket_->send_to(boost::asio::buffer(message.data(), message.size()), subscriber);
            }

            // Decode packet info from binary data (same format as TCP)
            std::string packetInfo = "";
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

                packetInfo = " - Target: " + targetName + ", Type: " + typeName;
            }

            Logger::info("UDP Published [" + topic + "] to " + std::to_string(subscribers.size()) + " subscribers: " +
                        std::to_string(data.size()) + " bytes" + packetInfo);
        }
    } catch (const std::exception& e) {
        Logger::error("UDP publish error: " + std::string(e.what()));
    }
}

// === Simple Subscription Management Implementation ===

void UdpManager::startSubscriptionReceive() {
    if (!subscriptionSocket_) return;

    // Create buffers for receiving subscription requests (each call gets its own buffer)
    auto subscriptionBuffer = std::make_shared<std::array<uint8_t, 1024>>();
    auto senderEndpoint = std::make_shared<udp::endpoint>();

    subscriptionSocket_->async_receive_from(
        boost::asio::buffer(*subscriptionBuffer), *senderEndpoint,
        [this, subscriptionBuffer, senderEndpoint](boost::system::error_code error, std::size_t bytes_received) {
            if (!error && bytes_received > 0) {
                try {
                    std::vector<uint8_t> received_data(subscriptionBuffer->begin(),
                                                     subscriptionBuffer->begin() + bytes_received);
                    handleSubscriptionRequest(received_data, *senderEndpoint);
                } catch (const std::exception& e) {
                    Logger::error("Subscription request processing error: " + std::string(e.what()));
                }
            }

            // Continue receiving if still running
            if (running_) {
                startSubscriptionReceive();
            }
        });
}

void UdpManager::handleSubscriptionRequest(const std::vector<uint8_t>& data, const udp::endpoint& sender) {
    try {
        // Simple protocol: "SUBSCRIBE|topic|client_id" or "UNSUBSCRIBE|topic|client_id"
        std::string message(data.begin(), data.end());

        size_t firstPipe = message.find('|');
        if (firstPipe == std::string::npos) return;

        size_t secondPipe = message.find('|', firstPipe + 1);
        if (secondPipe == std::string::npos) return;

        std::string command = message.substr(0, firstPipe);
        std::string topic = message.substr(firstPipe + 1, secondPipe - firstPipe - 1);
        std::string client_id = message.substr(secondPipe + 1);

        std::lock_guard<std::mutex> lock(subscriptionMutex_);

        if (command == "SUBSCRIBE") {
            clients_[client_id] = sender;
            subscriptions_[topic].insert(client_id);
            Logger::info("UDP Client " + client_id + " subscribed to: " + topic);
        } else if (command == "UNSUBSCRIBE") {
            subscriptions_[topic].erase(client_id);
            if (subscriptions_[topic].empty()) {
                subscriptions_.erase(topic);
            }
            Logger::info("UDP Client " + client_id + " unsubscribed from: " + topic);
        }
    } catch (const std::exception& e) {
        Logger::error("Failed to parse subscription request: " + std::string(e.what()));
    }
}

std::vector<udp::endpoint> UdpManager::getSubscribers(const std::string& topic) const {
    std::lock_guard<std::mutex> lock(subscriptionMutex_);
    std::vector<udp::endpoint> result;
    std::unordered_set<std::string> matched_clients;

    // Check all subscription patterns for wildcard matches
    for (const auto& subscription : subscriptions_) {
        const std::string& pattern = subscription.first;
        const std::unordered_set<std::string>& client_ids = subscription.second;

        // Check if topic matches this pattern (exact match or wildcard)
        if (matchesWildcardPattern(pattern, topic)) {
            for (const auto& client_id : client_ids) {
                // Avoid duplicate clients
                if (matched_clients.find(client_id) == matched_clients.end()) {
                    auto client_it = clients_.find(client_id);
                    if (client_it != clients_.end()) {
                        result.push_back(client_it->second);
                        matched_clients.insert(client_id);
                    }
                }
            }
        }
    }

    return result;
}

std::string UdpManager::endpointToString(const udp::endpoint& endpoint) const {
    return endpoint.address().to_string() + ":" + std::to_string(endpoint.port());
}

bool UdpManager::matchesWildcardPattern(const std::string& pattern, const std::string& topic) const {
    // Handle exact match
    if (pattern == topic) {
        return true;
    }

    // Handle wildcard patterns (* means match any segment)
    // Pattern: "telemetry.*" matches "telemetry.UAV_1.camera.location"
    // Pattern: "telemetry.*.camera.*" matches "telemetry.UAV_1.camera.location"
    // Pattern: "telemetry.UAV_1.*" matches "telemetry.UAV_1.camera.location"

    // Split pattern and topic by dots
    std::vector<std::string> pattern_parts;
    std::vector<std::string> topic_parts;

    // Split pattern
    std::stringstream pattern_stream(pattern);
    std::string pattern_part;
    while (std::getline(pattern_stream, pattern_part, '.')) {
        pattern_parts.push_back(pattern_part);
    }

    // Split topic
    std::stringstream topic_stream(topic);
    std::string topic_part;
    while (std::getline(topic_stream, topic_part, '.')) {
        topic_parts.push_back(topic_part);
    }

    // Match parts
    size_t pattern_idx = 0;
    size_t topic_idx = 0;

    while (pattern_idx < pattern_parts.size() && topic_idx < topic_parts.size()) {
        const std::string& pattern_segment = pattern_parts[pattern_idx];

        if (pattern_segment == "*") {
            // Wildcard matches any single segment
            pattern_idx++;
            topic_idx++;
        } else {
            // Exact segment match required
            if (pattern_segment == topic_parts[topic_idx]) {
                pattern_idx++;
                topic_idx++;
            } else {
                return false; // Segment mismatch
            }
        }
    }

    // Handle trailing wildcards in pattern
    while (pattern_idx < pattern_parts.size() && pattern_parts[pattern_idx] == "*") {
        pattern_idx++;
        if (topic_idx < topic_parts.size()) {
            topic_idx++;
        }
    }

    // Match successful if both pattern and topic are fully consumed
    return (pattern_idx == pattern_parts.size() && topic_idx == topic_parts.size());
}



/**
 * @file TelemetryClient.cpp
 * @brief Implementation of the TelemetryClient class
 *
 * This file contains the implementation of the simplified telemetry API,
 * wrapping the complex ZeroMQ and Boost.Asio networking code.
 */

#include "TelemetryClient.h"

#include <algorithm>
#include <atomic>
#include <boost/asio.hpp>
#include <chrono>
#include <cstddef>
#include <fstream>
#include <iostream>
#include <mutex>
#include <nlohmann/json.hpp>
#include <sstream>
#include <thread>
#include <unordered_set>
#include <zmq.hpp>

using boost::asio::ip::udp;
using json = nlohmann::json;

namespace TelemetryAPI {

/**
 * @brief Implementation class using PIMPL pattern to hide dependencies
 */
class TelemetryClientImpl {
public:
    explicit TelemetryClientImpl(const std::string& client_id)
        : client_id_(client_id), protocol_(Protocol::TCP), connected_(false), running_(false) {}

    ~TelemetryClientImpl() {
        disconnect();
    }

    bool connect(const std::string& host, int port, Protocol protocol) {
        if (connected_) {
            return false;  // Already connected
        }

        host_ = host;
        port_ = port;
        protocol_ = protocol;

        try {
            if (protocol == Protocol::TCP) {
                return connectTCP();
            } else {
                return connectUDP();
            }
        } catch (const std::exception& e) {
            if (connection_callback_) {
                connection_callback_(false, "Connection failed: " + std::string(e.what()));
            }
            return false;
        }
    }

    bool connectFromConfig(const std::string& config_file, Protocol protocol) {
        try {
            std::ifstream file(config_file);
            if (!file.is_open()) {
                if (connection_callback_) {
                    connection_callback_(false, "Cannot open config file: " + config_file);
                }
                return false;
            }

            json config_json;
            file >> config_json;
            file.close();

            if (!config_json.contains("ui_ports")) {
                if (connection_callback_) {
                    connection_callback_(false, "Config file missing 'ui_ports' section");
                }
                return false;
            }

            const auto& ui_ports = config_json["ui_ports"];
            int port;

            if (protocol == Protocol::TCP) {
                port = ui_ports.at("tcp_publish_port");
            } else {
                port = ui_ports.at("udp_publish_port");
            }

            std::string host = "localhost";  // Default for local service
            if (config_json.contains("service") && config_json["service"].contains("ip")) {
                host = config_json["service"]["ip"];
            }

            return connect(host, port, protocol);

        } catch (const std::exception& e) {
            if (connection_callback_) {
                connection_callback_(false, "Config parsing error: " + std::string(e.what()));
            }
            return false;
        }
    }

    void disconnect() {
        if (!connected_) {
            return;
        }

        running_ = false;
        connected_ = false;

        // Stop networking
        if (protocol_ == Protocol::TCP) {
            disconnectTCP();
        } else {
            disconnectUDP();
        }

        // Wait for background thread to finish
        if (receive_thread_.joinable()) {
            receive_thread_.join();
        }

        if (connection_callback_) {
            connection_callback_(false, "");  // Normal disconnect
        }
    }

    bool isConnected() const {
        return connected_;
    }

    bool subscribe(const std::string& topic) {
        if (!connected_) {
            return false;
        }

        std::lock_guard<std::mutex> lock(subscriptions_mutex_);

        if (subscriptions_.find(topic) != subscriptions_.end()) {
            return true;  // Already subscribed
        }

        if (protocol_ == Protocol::TCP) {
            return subscribeTCP(topic);
        } else {
            return subscribeUDP(topic);
        }
    }

    bool unsubscribe(const std::string& topic) {
        if (!connected_) {
            return false;
        }

        std::lock_guard<std::mutex> lock(subscriptions_mutex_);

        if (subscriptions_.find(topic) == subscriptions_.end()) {
            return true;  // Not subscribed
        }

        if (protocol_ == Protocol::TCP) {
            return unsubscribeTCP(topic);
        } else {
            return unsubscribeUDP(topic);
        }
    }

    bool sendCommand(const std::string& uav_name, const std::string& command) {
        if (!connected_ || protocol_ != Protocol::TCP) {
            return false;  // Commands only work with TCP
        }

        try {
            if (!command_socket_) {
                // Create command socket on demand
                command_socket_ = std::make_unique<zmq::socket_t>(*zmq_context_, zmq::socket_type::push);
                std::string command_addr = "tcp://" + host_ + ":" + std::to_string(port_ + 1);  // Command port is usually +1
                command_socket_->connect(command_addr);
            }

            // Format: "uav_name:command"
            std::string message = uav_name + ":" + command;
            command_socket_->send(zmq::buffer(message.data(), message.size()), zmq::send_flags::dontwait);
            return true;

        } catch (const std::exception&) {
            return false;
        }
    }

    void setTelemetryCallback(TelemetryCallback callback) {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        telemetry_callback_ = std::move(callback);
    }

    void setConnectionCallback(ConnectionCallback callback) {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        connection_callback_ = std::move(callback);
    }

    const std::string& getClientId() const {
        return client_id_;
    }

    Protocol getProtocol() const {
        return protocol_;
    }

private:
    // Member variables
    std::string client_id_;
    std::string host_;
    int port_;
    Protocol protocol_;
    std::atomic<bool> connected_;
    std::atomic<bool> running_;

    // Threading
    std::thread receive_thread_;
    mutable std::mutex callback_mutex_;
    mutable std::mutex subscriptions_mutex_;

    // Callbacks
    TelemetryCallback telemetry_callback_;
    ConnectionCallback connection_callback_;

    // Subscriptions
    std::unordered_set<std::string> subscriptions_;

    // TCP (ZeroMQ) members
    std::unique_ptr<zmq::context_t> zmq_context_;
    std::unique_ptr<zmq::socket_t> subscriber_socket_;
    std::unique_ptr<zmq::socket_t> command_socket_;

    // UDP (Boost.Asio) members
    std::unique_ptr<boost::asio::io_context> io_context_;
    std::unique_ptr<udp::socket> udp_socket_;
    std::unique_ptr<udp::socket> subscription_socket_;

    // TCP Implementation
    bool connectTCP() {
        try {
            zmq_context_ = std::make_unique<zmq::context_t>(1);
            subscriber_socket_ = std::make_unique<zmq::socket_t>(*zmq_context_, zmq::socket_type::sub);

            // Set socket options
            int linger = 1000;  // 1 second linger
            subscriber_socket_->set(zmq::sockopt::linger, linger);

            std::string subscribe_addr = "tcp://" + host_ + ":" + std::to_string(port_);
            subscriber_socket_->connect(subscribe_addr);

            connected_ = true;
            running_ = true;

            // Start receive thread
            receive_thread_ = std::thread(&TelemetryClientImpl::tcpReceiveLoop, this);

            if (connection_callback_) {
                connection_callback_(true, "");
            }

            return true;

        } catch (const std::exception& e) {
            connected_ = false;
            if (connection_callback_) {
                connection_callback_(false, "TCP connection failed: " + std::string(e.what()));
            }
            return false;
        }
    }

    void disconnectTCP() {
        try {
            if (subscriber_socket_) {
                subscriber_socket_->close();
                subscriber_socket_.reset();
            }
            if (command_socket_) {
                command_socket_->close();
                command_socket_.reset();
            }
            if (zmq_context_) {
                zmq_context_->close();
                zmq_context_.reset();
            }
        } catch (const std::exception&) {
            // Ignore cleanup errors
        }
    }

    bool subscribeTCP(const std::string& topic) {
        try {
            if (subscriber_socket_) {
                // ZeroMQ only supports prefix matching, not wildcard patterns
                // Convert common wildcard patterns to appropriate prefixes
                std::string zmq_topic = topic;

                if (topic == "telemetry.*") {
                    // Subscribe to all telemetry by using the prefix
                    zmq_topic = "telemetry.";
                } else if (topic.find("telemetry.*.") == 0) {
                    // For patterns like "telemetry.*.camera.*" or "telemetry.*.mapping.*"
                    // Subscribe to the broader prefix and filter on client side
                    zmq_topic = "telemetry.";
                }

                subscriber_socket_->set(zmq::sockopt::subscribe, zmq_topic);
                subscriptions_.insert(topic); // Keep original pattern for client-side filtering
                return true;
            }
        } catch (const std::exception& e) {
            // Log subscription errors for debugging
            std::cerr << "TCP subscription error for topic '" << topic << "': " << e.what() << std::endl;
        }
        return false;
    }

    bool unsubscribeTCP(const std::string& topic) {
        try {
            if (subscriber_socket_) {
                // Convert wildcard patterns to ZeroMQ prefixes for unsubscribe too
                std::string zmq_topic = topic;

                if (topic == "telemetry.*") {
                    zmq_topic = "telemetry.";
                } else if (topic.find("telemetry.*.") == 0) {
                    zmq_topic = "telemetry.";
                }

                subscriber_socket_->set(zmq::sockopt::unsubscribe, zmq_topic);
                subscriptions_.erase(topic);
                return true;
            }
        } catch (const std::exception&) {
            // Ignore unsubscription errors
        }
        return false;
    }

    void tcpReceiveLoop() {
        std::cerr << "DEBUG: TCP receive loop started" << std::endl;
        while (running_ && connected_) {
            try {
                zmq::message_t topic_msg;
                zmq::message_t data_msg;

                // Use polling to avoid blocking indefinitely
                zmq::pollitem_t items[] = { { *subscriber_socket_, 0, ZMQ_POLLIN, 0 } };
                int rc = zmq::poll(items, 1, std::chrono::milliseconds(100));

                if (rc > 0 && (items[0].revents & ZMQ_POLLIN)) {
                    std::cerr << "DEBUG: Message available on TCP socket" << std::endl;
                    // Receive topic
                    if (subscriber_socket_->recv(topic_msg, zmq::recv_flags::dontwait)) {
                        // Receive data (if available)
                        if (subscriber_socket_->recv(data_msg, zmq::recv_flags::dontwait)) {
                            std::string topic(static_cast<char*>(topic_msg.data()), topic_msg.size());
                            std::vector<uint8_t> data(static_cast<uint8_t*>(data_msg.data()),
                                                    static_cast<uint8_t*>(data_msg.data()) + data_msg.size());

                            std::cerr << "DEBUG: Received topic: " << topic << ", data size: " << data.size() << std::endl;

                            // Check if this topic matches any of our wildcard subscriptions
                            bool shouldDeliver = false;
                            {
                                std::lock_guard<std::mutex> lock(subscriptions_mutex_);
                                for (const auto& subscription : subscriptions_) {
                                    if (matchesWildcardPattern(subscription, topic)) {
                                        shouldDeliver = true;
                                        break;
                                    }
                                }
                            }

                            if (shouldDeliver) {
                                // Call user callback
                                std::lock_guard<std::mutex> lock(callback_mutex_);
                                if (telemetry_callback_) {
                                    telemetry_callback_(topic, data);
                                } else {
                                    std::cerr << "DEBUG: No telemetry callback set!" << std::endl;
                                }
                            }
                        }
                    }
                }

            } catch (const std::exception& e) {
                std::cerr << "DEBUG: TCP receive error: " << e.what() << std::endl;
                if (running_) {
                    // Only report error if we're supposed to be running
                    connected_ = false;
                    if (connection_callback_) {
                        connection_callback_(false, "TCP receive error");
                    }
                    break;
                }
            }
        }
        std::cerr << "DEBUG: TCP receive loop ended" << std::endl;
    }

    // Helper function to match wildcard patterns (same logic as UDP server)
    bool matchesWildcardPattern(const std::string& pattern, const std::string& topic) const {
        // Exact match
        if (pattern == topic) {
            return true;
        }

        // Handle "telemetry.*" as prefix match
        if (pattern == "telemetry.*") {
            return topic.rfind("telemetry.", 0) == 0;
        }

        // If no wildcard, must be exact match (already checked)
        if (pattern.find('*') == std::string::npos) {
            return false;
        }

        // Split by dots for segment matching
        std::vector<std::string> pattern_parts;
        std::stringstream pss(pattern);
        std::string part;
        while (std::getline(pss, part, '.')) {
            pattern_parts.push_back(part);
        }

        std::vector<std::string> topic_parts;
        std::stringstream tss(topic);
        while (std::getline(tss, part, '.')) {
            topic_parts.push_back(part);
        }

        // Must have same number of segments for exact wildcard matching
        if (pattern_parts.size() != topic_parts.size()) {
            return false;
        }

        // Compare each segment
        for (size_t i = 0; i < pattern_parts.size(); ++i) {
            if (pattern_parts[i] != "*" && pattern_parts[i] != topic_parts[i]) {
                return false;
            }
        }

        return true;
    }

    // UDP Implementation
    bool connectUDP() {
        try {
            io_context_ = std::make_unique<boost::asio::io_context>();

            // Socket for receiving published telemetry (random port, service will send to this endpoint)
            udp_socket_ = std::make_unique<udp::socket>(*io_context_, udp::endpoint(udp::v4(), 0));

            // Socket for sending subscription requests (also random port)
            subscription_socket_ = std::make_unique<udp::socket>(*io_context_, udp::endpoint(udp::v4(), 0));

            connected_ = true;
            running_ = true;

            // Start receive thread
            receive_thread_ = std::thread(&TelemetryClientImpl::udpReceiveLoop, this);

            if (connection_callback_) {
                connection_callback_(true, "");
            }

            return true;

        } catch (const std::exception& e) {
            connected_ = false;
            if (connection_callback_) {
                connection_callback_(false, "UDP connection failed: " + std::string(e.what()));
            }
            return false;
        }
    }

    void disconnectUDP() {
        try {
            if (io_context_) {
                io_context_->stop();
            }
            if (udp_socket_ && udp_socket_->is_open()) {
                udp_socket_->close();
            }
            if (subscription_socket_ && subscription_socket_->is_open()) {
                subscription_socket_->close();
            }
            udp_socket_.reset();
            subscription_socket_.reset();
            io_context_.reset();
        } catch (const std::exception&) {
            // Ignore cleanup errors
        }
    }

    bool subscribeUDP(const std::string& topic) {
        try {
            if (subscription_socket_ && udp_socket_) {
                // Get the local endpoint where we're listening for published data
                auto local_endpoint = udp_socket_->local_endpoint();

                // Send subscription request: "SUBSCRIBE|topic|client_id|client_port"
                std::string message = "SUBSCRIBE|" + topic + "|" + client_id_ + "|" + std::to_string(local_endpoint.port());

                // Handle hostname resolution for UDP endpoint
                boost::asio::ip::address addr;
                if (host_ == "localhost") {
                    addr = boost::asio::ip::address::from_string("127.0.0.1");
                } else {
                    try {
                        addr = boost::asio::ip::address::from_string(host_);
                    } catch (const std::exception&) {
                        // If not a valid IP, try resolving as hostname
                        boost::asio::ip::udp::resolver resolver(*io_context_);
                        auto results = resolver.resolve(host_, std::to_string(port_));
                        addr = results.begin()->endpoint().address();
                    }
                }

                udp::endpoint service_endpoint(addr, port_);
                subscription_socket_->send_to(boost::asio::buffer(message), service_endpoint);
                subscriptions_.insert(topic);
                return true;
            }
        } catch (const std::exception& e) {
            // Log subscription errors for debugging
            std::cerr << "UDP subscription error for topic '" << topic << "': " << e.what() << std::endl;
        }
        return false;
    }

    bool unsubscribeUDP(const std::string& topic) {
        try {
            if (subscription_socket_) {
                // Send unsubscription request: "UNSUBSCRIBE|topic|client_id"
                std::string message = "UNSUBSCRIBE|" + topic + "|" + client_id_;

                // Handle hostname resolution for UDP endpoint
                boost::asio::ip::address addr;
                if (host_ == "localhost") {
                    addr = boost::asio::ip::address::from_string("127.0.0.1");
                } else {
                    try {
                        addr = boost::asio::ip::address::from_string(host_);
                    } catch (const std::exception&) {
                        // If not a valid IP, try resolving as hostname
                        boost::asio::ip::udp::resolver resolver(*io_context_);
                        auto results = resolver.resolve(host_, std::to_string(port_));
                        addr = results.begin()->endpoint().address();
                    }
                }

                udp::endpoint service_endpoint(addr, port_);
                subscription_socket_->send_to(boost::asio::buffer(message), service_endpoint);
                subscriptions_.erase(topic);
                return true;
            }
        } catch (const std::exception&) {
            // Ignore unsubscription errors
        }
        return false;
    }

    void udpReceiveLoop() {
        std::array<uint8_t, 2048> buffer;
        udp::endpoint sender_endpoint;

        while (running_ && connected_) {
            try {
                // Use async_receive_from to receive from service
                udp_socket_->async_receive_from(
                    boost::asio::buffer(buffer), sender_endpoint,
                    [this, &buffer](boost::system::error_code ec, std::size_t bytes_received) {
                        if (!ec && bytes_received > 0) {
                            // Parse message: "topic|data"
                            std::vector<uint8_t> received_data(buffer.begin(), buffer.begin() + bytes_received);

                            // Find the separator
                            auto separator_it = std::find(received_data.begin(), received_data.end(), '|');
                            if (separator_it != received_data.end()) {
                                std::string topic(received_data.begin(), separator_it);
                                std::vector<uint8_t> data(separator_it + 1, received_data.end());

                                // Call user callback
                                std::lock_guard<std::mutex> lock(callback_mutex_);
                                if (telemetry_callback_) {
                                    telemetry_callback_(topic, data);
                                }
                            }
                        }
                    });

                // Run for a short time
                io_context_->run_for(std::chrono::milliseconds(100));
                io_context_->restart();

            } catch (const std::exception&) {
                if (running_) {
                    connected_ = false;
                    if (connection_callback_) {
                        connection_callback_(false, "UDP receive error");
                    }
                    break;
                }
            }
        }
    }
};

// TelemetryClient Implementation

TelemetryClient::TelemetryClient(const std::string& client_id)
    : impl_(std::make_unique<TelemetryClientImpl>(client_id)) {}

TelemetryClient::~TelemetryClient() = default;

bool TelemetryClient::connect(const std::string& host, int port, Protocol protocol) {
    return impl_->connect(host, port, protocol);
}

bool TelemetryClient::connectFromConfig(const std::string& config_file, Protocol protocol) {
    return impl_->connectFromConfig(config_file, protocol);
}

void TelemetryClient::disconnect() {
    impl_->disconnect();
}

bool TelemetryClient::isConnected() const {
    return impl_->isConnected();
}

bool TelemetryClient::subscribe(const std::string& topic) {
    return impl_->subscribe(topic);
}

bool TelemetryClient::unsubscribe(const std::string& topic) {
    return impl_->unsubscribe(topic);
}

bool TelemetryClient::sendCommand(const std::string& uav_name, const std::string& command) {
    return impl_->sendCommand(uav_name, command);
}

void TelemetryClient::setTelemetryCallback(TelemetryCallback callback) {
    impl_->setTelemetryCallback(std::move(callback));
}

void TelemetryClient::setConnectionCallback(ConnectionCallback callback) {
    impl_->setConnectionCallback(std::move(callback));
}

const std::string& TelemetryClient::getClientId() const {
    return impl_->getClientId();
}

Protocol TelemetryClient::getProtocol() const {
    return impl_->getProtocol();
}

// Utility functions
const PacketHeader* TelemetryClient::parseHeader(const std::vector<uint8_t>& data) {
    if (data.size() >= sizeof(PacketHeader)) {
        return reinterpret_cast<const PacketHeader*>(data.data());
    }
    return nullptr;
}

std::string TelemetryClient::getTargetName(uint8_t targetId) {
    switch (targetId) {
        case TargetIDs::CAMERA: return "Camera";
        case TargetIDs::MAPPING: return "Mapping";
        default: return "Unknown(" + std::to_string(targetId) + ")";
    }
}

std::string TelemetryClient::getPacketTypeName(uint8_t packetType) {
    switch (packetType) {
        case PacketTypes::LOCATION: return "Location";
        case PacketTypes::STATUS: return "Status";
        case PacketTypes::IMU_PACKET: return "IMU";
        case PacketTypes::BATTERY_PACKET: return "Battery";
        default: return "Unknown(" + std::to_string(packetType) + ")";
    }
}

}  // namespace TelemetryAPI

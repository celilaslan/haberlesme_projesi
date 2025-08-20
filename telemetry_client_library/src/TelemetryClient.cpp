/**
 * @file TelemetryClient.cpp
 * @brief Implementation of the TelemetryClient class
 *
 * This file contains the implementation of the simplified telemetry API,
 * wrapping the complex ZeroMQ and Boost.Asio networking code.
 */

#include "TelemetryClient.h"
#include <zmq.hpp>
#include <boost/asio.hpp>
#include <nlohmann/json.hpp>
#include <iostream>
#include <fstream>
#include <thread>
#include <atomic>
#include <mutex>
#include <chrono>
#include <sstream>
#include <regex>
#include <unordered_set>

using boost::asio::ip::udp;
using json = nlohmann::json;

namespace TelemetryAPI {

/**
 * @class TelemetryClient::Impl
 * @brief Private implementation class (PIMPL pattern)
 *
 * This class contains all the implementation details, keeping the public
 * header clean and hiding ZeroMQ/Boost dependencies from library users.
 */
class TelemetryClient::Impl {
public:
    Impl() : zmq_context_(1), running_(false), debug_mode_(false) {}

    ~Impl() {
        stopReceiving();
    }

    bool initialize(const std::string& service_host, const std::string& config_file) {
        try {
            service_host_ = service_host;

            // Load configuration
            if (!loadConfiguration(config_file)) {
                last_error_ = "Failed to load service configuration";
                return false;
            }

            last_error_.clear();
            return true;
        } catch (const std::exception& e) {
            last_error_ = "Initialization error: " + std::string(e.what());
            return false;
        }
    }

    bool startReceiving(Protocol protocol, TelemetryCallback callback, ErrorCallback error_callback) {
        if (running_) {
            last_error_ = "Client is already receiving data";
            return false;
        }

        if (!callback) {
            last_error_ = "Telemetry callback cannot be null";
            return false;
        }

        try {
            protocol_ = protocol;
            telemetry_callback_ = callback;
            error_callback_ = error_callback;
            running_ = true;

            // Start appropriate receiver threads based on protocol
            if (protocol == Protocol::TCP_ONLY || protocol == Protocol::BOTH) {
                tcp_thread_ = std::thread(&Impl::tcpReceiverLoop, this);
            }

            if (protocol == Protocol::UDP_ONLY || protocol == Protocol::BOTH) {
                udp_thread_ = std::thread(&Impl::udpReceiverLoop, this);
            }

            last_error_.clear();
            return true;
        } catch (const std::exception& e) {
            running_ = false;
            last_error_ = "Failed to start receiving: " + std::string(e.what());
            return false;
        }
    }

    bool subscribeToUAV(const std::string& uav_name, DataType data_type) {
        std::lock_guard<std::mutex> lock(filter_mutex_);

        if (data_type == DataType::UNKNOWN) {
            // Subscribe to both mapping and camera data for this UAV
            uav_filters_.insert("mapping_" + uav_name);
            uav_filters_.insert("camera_" + uav_name);
        } else {
            std::string topic_prefix = (data_type == DataType::MAPPING) ? "mapping_" : "camera_";
            uav_filters_.insert(topic_prefix + uav_name);
        }

        return true;
    }

    bool subscribeToDataType(DataType data_type) {
        if (data_type == DataType::UNKNOWN) {
            last_error_ = "Cannot subscribe to UNKNOWN data type";
            return false;
        }

        std::lock_guard<std::mutex> lock(filter_mutex_);
        std::string prefix = (data_type == DataType::MAPPING) ? "mapping" : "camera";
        data_type_filters_.insert(prefix);

        return true;
    }

    bool sendCommand(const std::string& uav_name, const std::string& command, const std::string& client_name) {
        try {
            // Lazy initialization of command sender
            if (!command_socket_) {
                command_socket_ = std::make_unique<zmq::socket_t>(zmq_context_, zmq::socket_type::push);
                std::string addr = "tcp://" + service_host_ + ":" + std::to_string(config_.tcp_command_port);
                command_socket_->connect(addr);

                if (debug_mode_) {
                    std::cout << "[TelemetryClient] Connected command socket to " << addr << std::endl;
                }
            }

            // Format command message: "UAV_NAME:[client_name] command"
            std::string formatted_command = uav_name + ":[" + client_name + "] " + command;

            command_socket_->send(zmq::buffer(formatted_command), zmq::send_flags::dontwait);

            if (debug_mode_) {
                std::cout << "[TelemetryClient] Sent command: " << formatted_command << std::endl;
            }

            last_error_.clear();
            return true;
        } catch (const std::exception& e) {
            last_error_ = "Failed to send command: " + std::string(e.what());
            return false;
        }
    }

    void stopReceiving() {
        running_ = false;

        // Wait for threads to complete
        if (tcp_thread_.joinable()) {
            tcp_thread_.join();
        }
        if (udp_thread_.joinable()) {
            udp_thread_.join();
        }

        // Clean up sockets
        try {
            if (tcp_socket_) {
                tcp_socket_->close();
                tcp_socket_.reset();
            }
            if (command_socket_) {
                command_socket_->close();
                command_socket_.reset();
            }
            if (udp_socket_) {
                udp_socket_->close();
                udp_socket_.reset();
            }
        } catch (const std::exception& e) {
            if (debug_mode_) {
                std::cerr << "[TelemetryClient] Cleanup error: " << e.what() << std::endl;
            }
        }
    }

    bool isReceiving() const {
        return running_;
    }

    std::vector<std::string> getAvailableUAVs() const {
        return available_uavs_;
    }

    std::string getConnectionStatus() const {
        std::ostringstream oss;
        oss << "Service: " << service_host_;
        oss << ", Running: " << (running_ ? "Yes" : "No");
        oss << ", Protocol: ";

        switch (protocol_) {
            case Protocol::TCP_ONLY: oss << "TCP"; break;
            case Protocol::UDP_ONLY: oss << "UDP"; break;
            case Protocol::BOTH: oss << "TCP+UDP"; break;
        }

        oss << ", UAVs: " << available_uavs_.size();
        return oss.str();
    }

    void setDebugMode(bool enable) {
        debug_mode_ = enable;
    }

    std::string getLastError() const {
        return last_error_;
    }

private:
    // Configuration structure
    struct Config {
        int tcp_publish_port = 5557;
        int tcp_command_port = 5558;
        int udp_camera_port = 5570;
        int udp_mapping_port = 5571;
    };

    // Member variables
    std::string service_host_;
    Config config_;
    std::vector<std::string> available_uavs_;
    std::string last_error_;

    // Protocol and callbacks
    Protocol protocol_;
    TelemetryCallback telemetry_callback_;
    ErrorCallback error_callback_;

    // Threading and synchronization
    std::atomic<bool> running_;
    std::atomic<bool> debug_mode_;
    std::thread tcp_thread_;
    std::thread udp_thread_;

    // Filtering
    std::mutex filter_mutex_;
    std::unordered_set<std::string> uav_filters_;      // Exact topic matches (e.g., "mapping_UAV_1")
    std::unordered_set<std::string> data_type_filters_; // Prefix matches (e.g., "mapping")

    // Networking
    zmq::context_t zmq_context_;
    std::unique_ptr<zmq::socket_t> tcp_socket_;
    std::unique_ptr<zmq::socket_t> command_socket_;
    std::unique_ptr<boost::asio::io_context> io_context_;
    std::unique_ptr<udp::socket> udp_socket_;

    bool loadConfiguration(const std::string& config_file) {
        std::string config_path = config_file;

        // If no config file specified, try to find one
        if (config_path.empty()) {
            const char* env_config = std::getenv("SERVICE_CONFIG");
            if (env_config) {
                config_path = env_config;
            } else {
                config_path = "service_config.json";
            }
        }

        std::ifstream file(config_path);
        if (!file.is_open()) {
            if (debug_mode_) {
                std::cout << "[TelemetryClient] Config file not found: " << config_path
                         << ", using defaults" << std::endl;
            }
            return true; // Use default values
        }

        try {
            json j;
            file >> j;
            file.close();

            // Load UI ports
            if (j.contains("ui_ports")) {
                auto ui_ports = j["ui_ports"];
                config_.tcp_publish_port = ui_ports.value("tcp_publish_port", 5557);
                config_.tcp_command_port = ui_ports.value("tcp_command_port", 5558);
                config_.udp_camera_port = ui_ports.value("udp_camera_port", 5570);
                config_.udp_mapping_port = ui_ports.value("udp_mapping_port", 5571);
            }

            // Load available UAVs
            if (j.contains("uavs") && j["uavs"].is_array()) {
                for (const auto& uav : j["uavs"]) {
                    if (uav.contains("name")) {
                        available_uavs_.push_back(uav["name"]);
                    }
                }
            }

            if (debug_mode_) {
                std::cout << "[TelemetryClient] Loaded config: TCP " << config_.tcp_publish_port
                         << "/" << config_.tcp_command_port << ", UDP " << config_.udp_camera_port
                         << "/" << config_.udp_mapping_port << ", UAVs: " << available_uavs_.size() << std::endl;
            }

            return true;
        } catch (const std::exception& e) {
            last_error_ = "Config parsing error: " + std::string(e.what());
            return false;
        }
    }

    void tcpReceiverLoop() {
        try {
            tcp_socket_ = std::make_unique<zmq::socket_t>(zmq_context_, zmq::socket_type::sub);
            std::string addr = "tcp://" + service_host_ + ":" + std::to_string(config_.tcp_publish_port);
            tcp_socket_->connect(addr);

            // Subscribe to all topics initially
            tcp_socket_->set(zmq::sockopt::subscribe, "");

            if (debug_mode_) {
                std::cout << "[TelemetryClient] TCP receiver connected to " << addr << std::endl;
            }

            while (running_) {
                zmq::message_t topic_msg, data_msg;

                // Non-blocking receive with timeout
                auto recv_result = tcp_socket_->recv(topic_msg, zmq::recv_flags::dontwait);
                if (!recv_result) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    continue;
                }

                recv_result = tcp_socket_->recv(data_msg, zmq::recv_flags::dontwait);
                if (!recv_result) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    continue;
                }

                std::string topic(static_cast<char*>(topic_msg.data()), topic_msg.size());
                std::string data(static_cast<char*>(data_msg.data()), data_msg.size());

                processTelemetryData(topic, data, Protocol::TCP_ONLY);
            }
        } catch (const std::exception& e) {
            if (error_callback_ && running_) {
                error_callback_("TCP receiver error: " + std::string(e.what()));
            }
        }

        if (debug_mode_) {
            std::cout << "[TelemetryClient] TCP receiver thread stopped" << std::endl;
        }
    }

    void udpReceiverLoop() {
        try {
            io_context_ = std::make_unique<boost::asio::io_context>();

            // Create sockets for both camera and mapping data
            auto camera_socket = std::make_unique<udp::socket>(*io_context_,
                udp::endpoint(udp::v4(), config_.udp_camera_port));
            auto mapping_socket = std::make_unique<udp::socket>(*io_context_,
                udp::endpoint(udp::v4(), config_.udp_mapping_port));

            if (debug_mode_) {
                std::cout << "[TelemetryClient] UDP receivers bound to ports "
                         << config_.udp_camera_port << " and " << config_.udp_mapping_port << std::endl;
            }

            char buffer[2048];

            while (running_) {
                // Check camera socket
                udp::endpoint sender_endpoint;
                boost::system::error_code ec;

                camera_socket->non_blocking(true);
                size_t length = camera_socket->receive_from(boost::asio::buffer(buffer), sender_endpoint, 0, ec);

                if (!ec && length > 0) {
                    std::string message(buffer, length);
                    parseUdpMessage(message, Protocol::UDP_ONLY);
                }

                // Check mapping socket
                mapping_socket->non_blocking(true);
                length = mapping_socket->receive_from(boost::asio::buffer(buffer), sender_endpoint, 0, ec);

                if (!ec && length > 0) {
                    std::string message(buffer, length);
                    parseUdpMessage(message, Protocol::UDP_ONLY);
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        } catch (const std::exception& e) {
            if (error_callback_ && running_) {
                error_callback_("UDP receiver error: " + std::string(e.what()));
            }
        }

        if (debug_mode_) {
            std::cout << "[TelemetryClient] UDP receiver thread stopped" << std::endl;
        }
    }

    void parseUdpMessage(const std::string& message, Protocol protocol) {
        // UDP messages have format: "topic|data"
        size_t separator = message.find('|');
        if (separator != std::string::npos) {
            std::string topic = message.substr(0, separator);
            std::string data = message.substr(separator + 1);
            processTelemetryData(topic, data, protocol);
        }
    }

    void processTelemetryData(const std::string& topic, const std::string& data, Protocol protocol) {
        // Apply filters
        if (!shouldProcessTopic(topic)) {
            return;
        }

        // Parse the topic to extract UAV name and data type
        TelemetryData telemetry_data;
        telemetry_data.topic = topic;
        telemetry_data.raw_data = data;
        telemetry_data.received_via = protocol;
        telemetry_data.timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        // Extract UAV name and data type from topic (e.g., "mapping_UAV_1")
        if (topic.find("mapping_") == 0) {
            telemetry_data.data_type = DataType::MAPPING;
            telemetry_data.uav_name = topic.substr(8); // Remove "mapping_" prefix
        } else if (topic.find("camera_") == 0) {
            telemetry_data.data_type = DataType::CAMERA;
            telemetry_data.uav_name = topic.substr(7); // Remove "camera_" prefix
        } else {
            telemetry_data.data_type = DataType::UNKNOWN;
            telemetry_data.uav_name = "unknown";
        }

        if (debug_mode_) {
            std::cout << "[TelemetryClient] Received " << topic << ": " << data << std::endl;
        }

        // Call user callback
        if (telemetry_callback_) {
            try {
                telemetry_callback_(telemetry_data);
            } catch (const std::exception& e) {
                if (error_callback_) {
                    error_callback_("Callback error: " + std::string(e.what()));
                }
            }
        }
    }

    bool shouldProcessTopic(const std::string& topic) {
        std::lock_guard<std::mutex> lock(filter_mutex_);

        // If no filters are set, process all topics
        if (uav_filters_.empty() && data_type_filters_.empty()) {
            return true;
        }

        // Check exact UAV filters
        if (!uav_filters_.empty() && uav_filters_.count(topic) > 0) {
            return true;
        }

        // Check data type filters
        for (const auto& prefix : data_type_filters_) {
            if (topic.find(prefix) == 0) {
                return true;
            }
        }

        return false;
    }
};

// TelemetryClient implementation (forwarding to Impl)

TelemetryClient::TelemetryClient() : pImpl(std::make_unique<Impl>()) {}

TelemetryClient::~TelemetryClient() = default;

bool TelemetryClient::initialize(const std::string& service_host, const std::string& config_file) {
    return pImpl->initialize(service_host, config_file);
}

bool TelemetryClient::startReceiving(Protocol protocol, TelemetryCallback callback, ErrorCallback error_callback) {
    return pImpl->startReceiving(protocol, callback, error_callback);
}

bool TelemetryClient::subscribeToUAV(const std::string& uav_name, DataType data_type) {
    return pImpl->subscribeToUAV(uav_name, data_type);
}

bool TelemetryClient::subscribeToDataType(DataType data_type) {
    return pImpl->subscribeToDataType(data_type);
}

bool TelemetryClient::sendCommand(const std::string& uav_name, const std::string& command, const std::string& client_name) {
    return pImpl->sendCommand(uav_name, command, client_name);
}

void TelemetryClient::stopReceiving() {
    pImpl->stopReceiving();
}

bool TelemetryClient::isReceiving() const {
    return pImpl->isReceiving();
}

std::vector<std::string> TelemetryClient::getAvailableUAVs() const {
    return pImpl->getAvailableUAVs();
}

std::string TelemetryClient::getConnectionStatus() const {
    return pImpl->getConnectionStatus();
}

void TelemetryClient::setDebugMode(bool enable) {
    pImpl->setDebugMode(enable);
}

std::string TelemetryClient::getLastError() const {
    return pImpl->getLastError();
}

// Utility functions

std::string getLibraryVersion() {
    return "1.0.0";
}

bool testServiceConnection(const std::string& service_host, int tcp_port, int timeout_ms) {
    try {
        zmq::context_t context(1);
        zmq::socket_t socket(context, zmq::socket_type::req);

        // Set timeout
        socket.set(zmq::sockopt::rcvtimeo, timeout_ms);
        socket.set(zmq::sockopt::sndtimeo, timeout_ms);

        std::string addr = "tcp://" + service_host + ":" + std::to_string(tcp_port);
        socket.connect(addr);

        // Send a test message
        socket.send(zmq::buffer("ping"), zmq::send_flags::none);

        // Try to receive (this will timeout if service is not responding)
        zmq::message_t reply;
        auto recv_result = socket.recv(reply, zmq::recv_flags::none);

        socket.close();
        return recv_result.has_value();
    } catch (const std::exception&) {
        return false;
    }
}

bool parseTelemetryMessage(const std::string& raw_message, std::string& uav_name, int& numeric_code) {
    // Expected format: "UAV_NAME  NUMERIC_CODE" (e.g., "UAV_1  1001")
    std::regex pattern(R"(^(\w+)\s+(\d+)$)");
    std::smatch matches;

    if (std::regex_match(raw_message, matches, pattern)) {
        uav_name = matches[1];
        numeric_code = std::stoi(matches[2]);
        return true;
    }

    return false;
}

} // namespace TelemetryAPI

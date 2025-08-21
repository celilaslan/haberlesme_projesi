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
#include <fstream>
#include <iostream>
#include <mutex>
#include <nlohmann/json.hpp>
#include <regex>
#include <sstream>
#include <thread>
#include <unordered_set>
#include <zmq.hpp>

using boost::asio::ip::udp;
using json = nlohmann::json;

namespace TelemetryAPI {

    // Strong types to prevent parameter swapping
    namespace detail {
        struct ServiceHostTag {};
        struct ConfigFileTag {};
        struct TcpPortTag {};
        struct TimeoutMsTag {};

        template<typename Tag>
        struct TypedString {
            std::string value;

            // Implicit conversion from string for API compatibility
            TypedString(std::string str) : value(std::move(str)) {}
            TypedString(const char* str) : value(str) {}

            // Explicit conversion to string
            [[nodiscard]] const std::string& str() const { return value; }
            operator const std::string&() const { return value; }
        };

        template<typename Tag>
        struct TypedInt {
            int value;

            // Implicit conversion from int for API compatibility
            TypedInt(int val) : value(val) {}

            // Explicit conversion to int
            [[nodiscard]] int get() const { return value; }
            operator int() const { return value; }
        };

        using ServiceHost = TypedString<ServiceHostTag>;
        using ConfigFile = TypedString<ConfigFileTag>;
        using TcpPort = TypedInt<TcpPortTag>;
        using TimeoutMs = TypedInt<TimeoutMsTag>;
    }

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

        ~Impl() { stopReceiving(); }

        // Disable copy and move operations for complex networking state
        Impl(const Impl&) = delete;
        Impl& operator=(const Impl&) = delete;
        Impl(Impl&&) = delete;
        Impl& operator=(Impl&&) = delete;

        bool initialize(const detail::ServiceHost& service_host, const detail::ConfigFile& config_file) {
            // State validation (fail-fast design)
            if (client_state_ != ClientState::IDLE && client_state_ != ClientState::ERROR) {
                last_error_ =
                    "Client must be in IDLE or ERROR state to initialize (current: " + getStateDescription() + ")";
                return false;
            }

            try {
                service_host_ = service_host.str();
                return loadConfiguration(config_file.str());
            } catch (const std::exception& e) {
                last_error_ = "Initialization failed: " + std::string(e.what());
                client_state_ = ClientState::ERROR;
                return false;
            }
        }

        bool startReceiving(Protocol protocol, TelemetryCallback callback, ErrorCallback error_callback) {
            // State validation
            if (client_state_ != ClientState::INITIALIZED && client_state_ != ClientState::STOPPED) {
                last_error_ =
                    "Client must be INITIALIZED or STOPPED to start receiving (current: " + getStateDescription() + ")";
                return false;
            }

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
                telemetry_callback_ = std::move(callback);
                error_callback_ = std::move(error_callback);
                running_ = true;

                // Start appropriate receiver threads based on protocol
                if (protocol == Protocol::TCP_ONLY || protocol == Protocol::BOTH) {
                    tcp_thread_ = std::thread(&Impl::tcpReceiverLoop, this);
                }

                if (protocol == Protocol::UDP_ONLY || protocol == Protocol::BOTH) {
                    udp_thread_ = std::thread(&Impl::udpReceiverLoop, this);
                }

                last_error_.clear();
                client_state_ = ClientState::RUNNING;
                return true;
            } catch (const std::exception& e) {
                running_ = false;
                client_state_ = ClientState::ERROR;
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

                // Use ZeroMQ subscription for both types
                if (tcp_socket_) {
                    tcp_socket_->set(zmq::sockopt::subscribe, "mapping_" + uav_name);
                    tcp_socket_->set(zmq::sockopt::subscribe, "camera_" + uav_name);
                }
            } else {
                std::string topic_prefix = (data_type == DataType::MAPPING) ? "mapping_" : "camera_";
                std::string full_topic = topic_prefix + uav_name;
                uav_filters_.insert(full_topic);

                // Use ZeroMQ subscription for specific topic
                if (tcp_socket_) {
                    tcp_socket_->set(zmq::sockopt::subscribe, full_topic);
                }
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

            // ACTUALLY use ZeroMQ subscription filtering!
            if (tcp_socket_) {
                tcp_socket_->set(zmq::sockopt::subscribe, prefix);
            }

            return true;
        }

        bool unsubscribeFromUAV(const std::string& uav_name, DataType data_type) {
            std::lock_guard<std::mutex> lock(filter_mutex_);

            if (data_type == DataType::UNKNOWN) {
                // Unsubscribe from both mapping and camera data for this UAV
                std::string mapping_topic = "mapping_" + uav_name;
                std::string camera_topic = "camera_" + uav_name;

                uav_filters_.erase(mapping_topic);
                uav_filters_.erase(camera_topic);

                // Use ZeroMQ unsubscription for both types
                if (tcp_socket_) {
                    tcp_socket_->set(zmq::sockopt::unsubscribe, mapping_topic);
                    tcp_socket_->set(zmq::sockopt::unsubscribe, camera_topic);
                }
            } else {
                std::string topic_prefix = (data_type == DataType::MAPPING) ? "mapping_" : "camera_";
                std::string full_topic = topic_prefix + uav_name;
                uav_filters_.erase(full_topic);

                // Use ZeroMQ unsubscription for specific topic
                if (tcp_socket_) {
                    tcp_socket_->set(zmq::sockopt::unsubscribe, full_topic);
                }
            }

            if (debug_mode_) {
                std::cout << "[TelemetryClient] Unsubscribed from UAV: " << uav_name
                          << " (type: " << static_cast<int>(data_type) << ")" << '\n';
            }

            return true;
        }

        bool unsubscribeFromDataType(DataType data_type) {
            if (data_type == DataType::UNKNOWN) {
                last_error_ = "Cannot unsubscribe from UNKNOWN data type";
                return false;
            }

            std::lock_guard<std::mutex> lock(filter_mutex_);
            std::string prefix = (data_type == DataType::MAPPING) ? "mapping" : "camera";
            data_type_filters_.erase(prefix);

            // Use ZeroMQ unsubscription
            if (tcp_socket_) {
                tcp_socket_->set(zmq::sockopt::unsubscribe, prefix);
            }

            if (debug_mode_) {
                std::cout << "[TelemetryClient] Unsubscribed from data type: " << prefix << '\n';
            }

            return true;
        }

        bool clearAllSubscriptions() {
            std::lock_guard<std::mutex> lock(filter_mutex_);

            // Clear all filters
            auto all_filters = uav_filters_;
            auto all_type_filters = data_type_filters_;

            uav_filters_.clear();
            data_type_filters_.clear();

            // Remove all ZeroMQ subscriptions
            if (tcp_socket_) {
                // Unsubscribe from all UAV-specific topics
                for (const auto& topic : all_filters) {
                    tcp_socket_->set(zmq::sockopt::unsubscribe, topic);
                }

                // Unsubscribe from all data type filters
                for (const auto& prefix : all_type_filters) {
                    tcp_socket_->set(zmq::sockopt::unsubscribe, prefix);
                }
            }

            if (debug_mode_) {
                std::cout << "[TelemetryClient] Cleared all subscriptions ("
                          << all_filters.size() + all_type_filters.size() << " total)" << '\n';
            }

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
                        std::cout << "[TelemetryClient] Connected command socket to " << addr << '\n';
                    }
                }

                // Format command message: "UAV_NAME:[client_name] command"
                std::string formatted_command = uav_name + ":[" + client_name + "] " + command;

                command_socket_->send(zmq::buffer(formatted_command), zmq::send_flags::dontwait);

                if (debug_mode_) {
                    std::cout << "[TelemetryClient] Sent command: " << formatted_command << '\n';
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

            // Wait for any active callbacks to complete (thread safety)
            {
                std::lock_guard<std::mutex> callback_lock(callback_mutex_);
                // This ensures no callbacks are executing when we proceed
            }

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
                    std::cerr << "[TelemetryClient] Cleanup error: " << e.what() << '\n';
                }
            }

            client_state_ = ClientState::STOPPED;
        }

        bool isReceiving() const { return running_; }

        ClientState getCurrentState() const { return client_state_; }

        std::string getStateDescription() const {
            switch (client_state_) {
                case ClientState::IDLE:
                    return "IDLE";
                case ClientState::INITIALIZED:
                    return "INITIALIZED";
                case ClientState::RUNNING:
                    return "RUNNING";
                case ClientState::STOPPED:
                    return "STOPPED";
                case ClientState::ERROR:
                    return "ERROR";
                default:
                    return "UNKNOWN";
            }
        }

        bool resetClient() {
            if (running_) {
                stopReceiving();
            }

            client_state_ = ClientState::IDLE;
            last_error_.clear();
            return true;
        }

        std::vector<std::string> getAvailableUAVs() const { return available_uavs_; }

        std::string getConnectionStatus() const {
            std::ostringstream oss;
            oss << "Service: " << service_host_;
            oss << ", Running: " << (running_ ? "Yes" : "No");
            oss << ", Protocol: ";

            switch (protocol_) {
                case Protocol::TCP_ONLY:
                    oss << "TCP";
                    break;
                case Protocol::UDP_ONLY:
                    oss << "UDP";
                    break;
                case Protocol::BOTH:
                    oss << "TCP+UDP";
                    break;
            }

            oss << ", UAVs: " << available_uavs_.size();
            return oss.str();
        }

        void setDebugMode(bool enable) { debug_mode_ = enable; }

        std::string getLastError() const { return last_error_; }

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
        Protocol protocol_ = Protocol::TCP_ONLY;
        TelemetryCallback telemetry_callback_;
        ErrorCallback error_callback_;

        // State management (fail-fast design)
        std::atomic<ClientState> client_state_{ClientState::IDLE};

        // Threading and synchronization
        std::atomic<bool> running_;
        std::atomic<bool> debug_mode_;
        std::thread tcp_thread_;
        std::thread udp_thread_;

        // Filtering
        std::mutex filter_mutex_;
        std::unordered_set<std::string> uav_filters_;        // Exact topic matches (e.g., "mapping_UAV_1")
        std::unordered_set<std::string> data_type_filters_;  // Prefix matches (e.g., "mapping")

        // Callback protection (prevent race conditions during shutdown)
        mutable std::mutex callback_mutex_;

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
                if (env_config != nullptr) {
                    config_path = env_config;
                } else {
                    config_path = "service_config.json";
                }
            }

            std::ifstream file(config_path);
            if (!file.is_open()) {
                if (debug_mode_) {
                    std::cout << "[TelemetryClient] Config file not found: " << config_path << ", using defaults"
                              << '\n';
                }
                return true;  // Use default values
            }

            try {
                json config_json;
                file >> config_json;
                file.close();

                // Load UI ports
                if (config_json.contains("ui_ports")) {
                    auto ui_ports = config_json["ui_ports"];
                    config_.tcp_publish_port = ui_ports.value("tcp_publish_port", 5557);
                    config_.tcp_command_port = ui_ports.value("tcp_command_port", 5558);
                    config_.udp_camera_port = ui_ports.value("udp_camera_port", 5570);
                    config_.udp_mapping_port = ui_ports.value("udp_mapping_port", 5571);
                }

                // Load available UAVs
                if (config_json.contains("uavs") && config_json["uavs"].is_array()) {
                    for (const auto& uav : config_json["uavs"]) {
                        if (uav.contains("name")) {
                            available_uavs_.push_back(uav["name"]);
                        }
                    }
                }

                if (debug_mode_) {
                    std::cout << "[TelemetryClient] Loaded config: TCP " << config_.tcp_publish_port << "/"
                              << config_.tcp_command_port << ", UDP " << config_.udp_camera_port << "/"
                              << config_.udp_mapping_port << ", UAVs: " << available_uavs_.size() << '\n';
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

                // Don't subscribe to all topics - let user choose what they want
                // tcp_socket_->set(zmq::sockopt::subscribe, "");  // OLD: receive everything

                // Only subscribe if user has already set preferences
                {
                    std::lock_guard<std::mutex> lock(filter_mutex_);
                    if (!data_type_filters_.empty() || !uav_filters_.empty()) {
                        // User has set specific subscriptions - use them
                        for (const auto& prefix : data_type_filters_) {
                            tcp_socket_->set(zmq::sockopt::subscribe, prefix);
                        }
                        for (const auto& topic : uav_filters_) {
                            tcp_socket_->set(zmq::sockopt::subscribe, topic);
                        }
                    } else {
                        // No specific subscriptions - subscribe to all (default behavior)
                        tcp_socket_->set(zmq::sockopt::subscribe, "");
                    }
                }

                if (debug_mode_) {
                    std::cout << "[TelemetryClient] TCP receiver connected to " << addr << '\n';
                }

                while (running_) {
                    zmq::message_t topic_msg;
                    zmq::message_t data_msg;

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
                std::cout << "[TelemetryClient] TCP receiver thread stopped" << '\n';
            }
        }

        void udpReceiverLoop() {
            try {
                io_context_ = std::make_unique<boost::asio::io_context>();

                // Create sockets for both camera and mapping data
                auto camera_socket =
                    std::make_unique<udp::socket>(*io_context_, udp::endpoint(udp::v4(), config_.udp_camera_port));
                auto mapping_socket =
                    std::make_unique<udp::socket>(*io_context_, udp::endpoint(udp::v4(), config_.udp_mapping_port));

                if (debug_mode_) {
                    std::cout << "[TelemetryClient] UDP receivers bound to ports " << config_.udp_camera_port << " and "
                              << config_.udp_mapping_port << '\n';
                }

                // Buffers for async operations
                auto camera_buffer = std::make_shared<std::array<char, 2048>>();
                auto mapping_buffer = std::make_shared<std::array<char, 2048>>();
                auto camera_endpoint = std::make_shared<udp::endpoint>();
                auto mapping_endpoint = std::make_shared<udp::endpoint>();

                // Lambda for camera data reception
                std::function<void()> start_camera_receive = [this, &camera_socket, camera_buffer, camera_endpoint,
                                                              &start_camera_receive]() {
                    camera_socket->async_receive_from(boost::asio::buffer(*camera_buffer), *camera_endpoint,
                                                      [this, camera_buffer, &start_camera_receive](
                                                          const boost::system::error_code& error_code, std::size_t length) {
                                                          if (!error_code && length > 0 && running_) {
                                                              std::string message(camera_buffer->data(), length);
                                                              parseUdpMessage(message, Protocol::UDP_ONLY);
                                                          }
                                                          if (running_) {
                                                              start_camera_receive();  // Continue receiving
                                                          }
                                                      });
                };

                // Lambda for mapping data reception
                std::function<void()> start_mapping_receive = [this, &mapping_socket, mapping_buffer, mapping_endpoint,
                                                               &start_mapping_receive]() {
                    mapping_socket->async_receive_from(boost::asio::buffer(*mapping_buffer), *mapping_endpoint,
                                                       [this, mapping_buffer, &start_mapping_receive](
                                                           const boost::system::error_code& error_code, std::size_t length) {
                                                           if (!error_code && length > 0 && running_) {
                                                               std::string message(mapping_buffer->data(), length);
                                                               parseUdpMessage(message, Protocol::UDP_ONLY);
                                                           }
                                                           if (running_) {
                                                               start_mapping_receive();  // Continue receiving
                                                           }
                                                       });
                };

                // Start async operations
                start_camera_receive();
                start_mapping_receive();

                // Run the I/O context
                while (running_) {
                    io_context_->run_for(std::chrono::milliseconds(100));
                    if (!running_) break;
                    io_context_->restart();
                }

            } catch (const std::exception& e) {
                if (error_callback_ && running_) {
                    error_callback_("UDP receiver error: " + std::string(e.what()));
                }
            }

            if (debug_mode_) {
                std::cout << "[TelemetryClient] UDP receiver thread stopped" << '\n';
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
            // Check if client is still running (thread safety improvement)
            if (!running_) {
                return;
            }

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
                                              std::chrono::system_clock::now().time_since_epoch())
                                              .count();

            // Extract UAV name and data type from topic (e.g., "mapping_UAV_1")
            if (topic.find("mapping_") == 0) {
                telemetry_data.data_type = DataType::MAPPING;
                telemetry_data.uav_name = topic.substr(8);  // Remove "mapping_" prefix
            } else if (topic.find("camera_") == 0) {
                telemetry_data.data_type = DataType::CAMERA;
                telemetry_data.uav_name = topic.substr(7);  // Remove "camera_" prefix
            } else {
                telemetry_data.data_type = DataType::UNKNOWN;
                telemetry_data.uav_name = "unknown";
            }

            if (debug_mode_) {
                std::cout << "[TelemetryClient] Received " << topic << ": " << data << '\n';
            }

            // Call user callback with thread safety protection
            if (telemetry_callback_) {
                std::lock_guard<std::mutex> callback_lock(callback_mutex_);

                // Double-check client is still running after acquiring lock
                if (!running_) {
                    return;
                }

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
            return std::any_of(data_type_filters_.begin(), data_type_filters_.end(),
                              [&topic](const std::string& prefix) { return topic.find(prefix) == 0; });
        }
    };

    // TelemetryClient implementation (forwarding to Impl)

    TelemetryClient::TelemetryClient() : pImpl(std::make_unique<Impl>()) {}

    TelemetryClient::~TelemetryClient() = default;

    bool TelemetryClient::initialize(const std::string& service_host, const std::string& config_file) {
        return pImpl->initialize(service_host, config_file);
    }

    bool TelemetryClient::startReceiving(Protocol protocol, TelemetryCallback callback, ErrorCallback error_callback) {
        return pImpl->startReceiving(protocol, std::move(callback), std::move(error_callback));
    }

    bool TelemetryClient::subscribeToUAV(const std::string& uav_name, DataType data_type) {
        return pImpl->subscribeToUAV(uav_name, data_type);
    }

    bool TelemetryClient::subscribeToDataType(DataType data_type) { return pImpl->subscribeToDataType(data_type); }

    bool TelemetryClient::unsubscribeFromUAV(const std::string& uav_name, DataType data_type) {
        return pImpl->unsubscribeFromUAV(uav_name, data_type);
    }

    bool TelemetryClient::unsubscribeFromDataType(DataType data_type) {
        return pImpl->unsubscribeFromDataType(data_type);
    }

    bool TelemetryClient::clearAllSubscriptions() { return pImpl->clearAllSubscriptions(); }

    bool TelemetryClient::sendCommand(const std::string& uav_name, const std::string& command,
                                      const std::string& client_name) {
        return pImpl->sendCommand(uav_name, command, client_name);
    }

    void TelemetryClient::stopReceiving() { pImpl->stopReceiving(); }

    bool TelemetryClient::isReceiving() const { return pImpl->isReceiving(); }

    ClientState TelemetryClient::getCurrentState() const { return pImpl->getCurrentState(); }

    std::string TelemetryClient::getStateDescription() const { return pImpl->getStateDescription(); }

    bool TelemetryClient::resetClient() { return pImpl->resetClient(); }

    std::vector<std::string> TelemetryClient::getAvailableUAVs() const { return pImpl->getAvailableUAVs(); }

    std::string TelemetryClient::getConnectionStatus() const { return pImpl->getConnectionStatus(); }

    void TelemetryClient::setDebugMode(bool enable) { pImpl->setDebugMode(enable); }

    std::string TelemetryClient::getLastError() const { return pImpl->getLastError(); }

    // Utility functions

    std::string getLibraryVersion() { return "1.0.0"; }

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

}  // namespace TelemetryAPI

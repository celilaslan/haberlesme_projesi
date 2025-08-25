/**
 * @file TcpManager.cpp
 * @brief Implementation of TCP communication management
 *
 * This file contains the implementation of the TcpManager class, which handles
 * all TCP-based communication between the telemetry service, UAVs, and UI components
 * using ZeroMQ as the underlying messaging library.
 */

#include "TcpManager.h"

#include <vector>

#include "Logger.h"
#include "TelemetryPackets.h"

/**
 * @brief Constructor - initializes TCP manager with configuration and callback
 * @param ctx ZeroMQ context for socket creation
 * @param cfg Configuration containing UAV and port settings
 * @param callback Function to call when telemetry messages are received
 */
TcpManager::TcpManager(zmq::context_t& ctx, const Config& cfg, TcpMessageCallback callback)
    : context(ctx), config(cfg), messageCallback_(std::move(callback)) {}

/**
 * @brief Destructor - ensures clean shutdown
 */
TcpManager::~TcpManager() {
    stop();
    join();

    // Explicit socket cleanup for ZMQ
    std::lock_guard<std::mutex> lock(socketMutex);
    pubToUi.reset();
    pullFromUi.reset();
    uavTelemetrySockets.clear();
    uavCommandSockets.clear();
}

/**
 * @brief Start TCP communication system
 *
 * This method:
 * 1. Creates and binds all necessary ZMQ sockets
 * 2. Sets up UI communication (PUB/PULL sockets)
 * 3. Sets up UAV communication (PULL/PUSH sockets for each UAV)
 * 4. Starts background threads for message processing
 */
void TcpManager::start() {
    running = true;

    try {
        std::lock_guard<std::mutex> lock(socketMutex);

        // Set up UI communication sockets
        // PUB socket for publishing telemetry data to UI subscribers
        pubToUi = std::make_unique<zmq::socket_t>(context, zmq::socket_type::pub);
        std::string ui_pub_addr = "tcp://*:" + std::to_string(config.getUiPorts().tcp_publish_port);
        pubToUi->bind(ui_pub_addr);
        Logger::statusWithDetails("TCP", StatusMessage("UI Publisher bound"), DetailMessage(ui_pub_addr));

        // PULL socket for receiving commands from UI components
        pullFromUi = std::make_unique<zmq::socket_t>(context, zmq::socket_type::pull);
        std::string ui_cmd_addr = "tcp://*:" + std::to_string(config.getUiPorts().tcp_command_port);
        pullFromUi->bind(ui_cmd_addr);
        Logger::statusWithDetails("TCP", StatusMessage("UI Command receiver bound"), DetailMessage(ui_cmd_addr));

        // Set up UAV communication sockets for each configured UAV
        for (const auto& uav : config.getUAVs()) {
            // PULL socket for receiving telemetry data from UAV
            auto pull_socket = std::make_unique<zmq::socket_t>(context, zmq::socket_type::pull);
            std::string telemetry_addr = "tcp://*:" + std::to_string(uav.tcp_telemetry_port);
            pull_socket->bind(telemetry_addr);
            uavTelemetrySockets.push_back(std::move(pull_socket));

            // PUSH socket for sending commands to UAV
            auto push_socket = std::make_unique<zmq::socket_t>(context, zmq::socket_type::push);
            std::string command_addr = "tcp://*:" + std::to_string(uav.tcp_command_port);
            push_socket->bind(command_addr);
            uavCommandSockets.push_back(std::move(push_socket));

            std::string config_msg;
            config_msg.reserve(50 + telemetry_addr.size() + command_addr.size());
            config_msg += "Telemetry: ";
            config_msg += telemetry_addr;
            config_msg += ", Commands: ";
            config_msg += command_addr;

            std::string uav_msg = "UAV " + uav.name + " configured";
            Logger::statusWithDetails("TCP", StatusMessage(uav_msg), DetailMessage(config_msg));
        }
    } catch (const zmq::error_t& e) {
        Logger::error("TCP socket setup failed: " + std::string(e.what()));
        running = false;
        throw;
    } catch (const std::exception& e) {
        Logger::error("Unexpected error during TCP setup: " + std::string(e.what()));
        running = false;
        throw;
    }

    // Start background processing threads
    receiverThread = std::thread(&TcpManager::receiverLoop, this);
    forwarderThread = std::thread(&TcpManager::forwarderLoop, this);
}

/**
 * @brief Stop TCP communication threads
 *
 * Sets the running flag to false, causing background threads to exit
 * their main loops. Call join() afterwards to wait for completion.
 */
void TcpManager::stop() {
    running = false;
}

/**
 * @brief Wait for background threads to complete
 *
 * Blocks until both receiver and forwarder threads have finished execution.
 */
void TcpManager::join() {
    if (receiverThread.joinable())
        receiverThread.join();
    if (forwarderThread.joinable())
        forwarderThread.join();
}

/**
 * @brief Publish telemetry data to UI subscribers
 * @param topic The topic to publish on (e.g., "target.camera.UAV_1" or "type.location.UAV_1")
 * @param data The telemetry data to send
 *
 * Uses ZMQ multipart messaging: first frame is topic, second frame is data.
 * UI components subscribe to specific topics to receive relevant data.
 * ZMQ handles subscription filtering automatically based on topic prefixes.
 * Thread-safe through mutex protection.
 */
void TcpManager::publishTelemetry(const std::string& topic, const std::vector<uint8_t>& data) {
    try {
        std::lock_guard<std::mutex> lock(socketMutex);
        if (pubToUi && running) {
            pubToUi->send(zmq::buffer(topic), zmq::send_flags::sndmore);
            pubToUi->send(zmq::buffer(data.data(), data.size()), zmq::send_flags::none);

            // Decode packet info from binary data
            std::string packetInfo = "";
            if (data.size() >= sizeof(PacketHeader)) {
                const PacketHeader* header = reinterpret_cast<const PacketHeader*>(data.data());

                std::string targetName;
                switch (header->targetID) {
                    case TargetIDs::CAMERA:
                        targetName = "Camera";
                        break;
                    case TargetIDs::MAPPING:
                        targetName = "Mapping";
                        break;
                    default:
                        targetName = "Unknown(" + std::to_string(header->targetID) + ")";
                        break;
                }

                std::string typeName;
                switch (header->packetType) {
                    case PacketTypes::LOCATION:
                        typeName = "Location";
                        break;
                    case PacketTypes::STATUS:
                        typeName = "Status";
                        break;
                    default:
                        typeName = "Unknown(" + std::to_string(header->packetType) + ")";
                        break;
                }

                packetInfo = " - Target: " + targetName + ", Type: " + typeName;
            }

            // Create hex dump of raw data (first 32 bytes for readability)
            std::string hexData = "";
            size_t hexLimit = std::min(data.size(), static_cast<size_t>(32));
            for (size_t i = 0; i < hexLimit; ++i) {
                char hex[4];
                snprintf(hex, sizeof(hex), "%02X ", data[i]);
                hexData += hex;
            }
            if (data.size() > 32)
                hexData += "...";

            Logger::info("Published to [" + topic + "]: " + std::to_string(data.size()) + " bytes" + packetInfo
                         + " | Hex: " + hexData);
        }
    } catch (const zmq::error_t& e) {
        Logger::error("Failed to publish telemetry: " + std::string(e.what()));
    }
}

/**
 * @brief Main loop for receiving telemetry data from UAVs
 *
 * This method:
 * 1. Sets up polling for all UAV telemetry sockets
 * 2. Continuously polls for incoming messages
 * 3. When data is received, calls the registered callback
 * 4. Runs until the running flag is set to false
 */
void TcpManager::receiverLoop() {
    try {
        std::vector<zmq::pollitem_t> poll_items = setupTelemetryPolling();

        while (running) {
            // Handle case where no UAVs are configured
            if (poll_items.empty()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                continue;
            }

            // Poll all UAV sockets with 100ms timeout
            zmq::poll(poll_items.data(), poll_items.size(), std::chrono::milliseconds(100));

            // Check each socket for incoming data
            for (size_t i = 0; i < poll_items.size() && running; ++i) {
                if ((poll_items[i].revents & ZMQ_POLLIN) != 0) {
                    processIncomingTelemetry(i);
                }
            }
        }
    } catch (const std::exception& e) {
        Logger::error("TCP receiver loop error: " + std::string(e.what()));
    }
    Logger::status("TCP", "Receiver thread stopped");
}

/**
 * @brief Helper to set up polling items for UAV telemetry sockets
 * @return Vector of polling items for zmq::poll
 */
std::vector<zmq::pollitem_t> TcpManager::setupTelemetryPolling() const {
    std::vector<zmq::pollitem_t> poll_items;
    std::lock_guard<std::mutex> lock(socketMutex);
    poll_items.reserve(uavTelemetrySockets.size());
    for (const auto& sock_ptr : uavTelemetrySockets) {
        poll_items.push_back({*sock_ptr, 0, ZMQ_POLLIN, 0});
    }
    return poll_items;
}

/**
 * @brief Helper to process incoming telemetry from a specific UAV socket
 * @param socket_index Index of the socket that received data
 */
void TcpManager::processIncomingTelemetry(size_t socket_index) {
    zmq::message_t message;
    zmq::recv_result_t received;

    {
        std::lock_guard<std::mutex> lock(socketMutex);
        if (socket_index < uavTelemetrySockets.size()) {
            received = uavTelemetrySockets[socket_index]->recv(message, zmq::recv_flags::none);
        }
    }

    if (received.has_value()) {
        // Extract binary message data and identify source UAV
        std::vector<uint8_t> data(static_cast<uint8_t*>(message.data()),
                                  static_cast<uint8_t*>(message.data()) + message.size());
        std::string uav_name =
            (socket_index < config.getUAVs().size()) ? config.getUAVs()[socket_index].name : "UNKNOWN";

        // Call the registered callback with UAV name directly
        if (messageCallback_) {
            messageCallback_(uav_name, data);
        }
    }
}

/**
 * @brief Main loop for forwarding commands from UI to UAVs
 *
 * This method:
 * 1. Polls the UI command socket for incoming messages
 * 2. Parses commands to determine target UAV
 * 3. Forwards commands to the appropriate UAV
 * 4. Runs until the running flag is set to false
 */
void TcpManager::forwarderLoop() {
    try {
        // Set up polling for UI command socket
        zmq::pollitem_t ui_poll{*pullFromUi, 0, ZMQ_POLLIN, 0};

        while (running) {
            // Poll UI socket with 100ms timeout
            zmq::poll(&ui_poll, 1, std::chrono::milliseconds(100));
            if ((ui_poll.revents & ZMQ_POLLIN) != 0) {
                zmq::message_t ui_msg;
                auto recv_result = pullFromUi->recv(ui_msg, zmq::recv_flags::none);
                if (recv_result.has_value()) {
                    std::string msg(static_cast<char*>(ui_msg.data()), ui_msg.size());
                    Logger::info("RECEIVED FROM UI [" + extractUISource(msg) + "]: " + msg);

                    auto [target_uav, actual_cmd] = parseUICommand(msg);
                    bool success = forwardCommandToUAV(target_uav, actual_cmd);

                    if (!success) {
                        Logger::warn("Command target UAV not found: " + target_uav);
                    }
                }
            }
        }
    } catch (const std::exception& e) {
        Logger::error("TCP forwarder loop error: " + std::string(e.what()));
    }
    Logger::status("TCP", "Forwarder thread stopped");
}

/**
 * @brief Extract the UI source type from a command message
 * @param message The command message from UI
 * @return String identifying the UI source ("camera", "mapping", or "unknown")
 *
 * Analyzes the message content to determine which UI component sent the command.
 * This is used for logging and debugging purposes.
 */
std::string TcpManager::extractUISource(const std::string& message) {
    if (message.find("[camera-ui]") != std::string::npos)
        return "camera";
    if (message.find("[mapping-ui]") != std::string::npos)
        return "mapping";
    return "unknown";
}

/**
 * @brief Helper to parse UI command and extract target UAV and command
 * @param message The raw UI command message
 * @return Pair of target_uav and actual_command
 */
std::pair<std::string, std::string> TcpManager::parseUICommand(const std::string& message) {
    // Parse command to extract target UAV and actual command
    // Expected format: "UAV_NAME:command_data"
    size_t colon_pos = message.find(':');
    std::string target_uav = (colon_pos != std::string::npos) ? message.substr(0, colon_pos) : "UAV_1";
    std::string actual_cmd = (colon_pos != std::string::npos) ? message.substr(colon_pos + 1) : message;
    return {target_uav, actual_cmd};
}

/**
 * @brief Helper to forward command to specific UAV
 * @param target_uav The UAV name to send command to
 * @param command The command to send
 * @return true if command was forwarded successfully
 */
bool TcpManager::forwardCommandToUAV(const std::string& target_uav, const std::string& command) {
    const auto& uavs = config.getUAVs();

    for (size_t i = 0; i < uavs.size(); ++i) {
        if (uavs[i].name == target_uav) {
            std::lock_guard<std::mutex> lock(socketMutex);
            if (i < uavCommandSockets.size() && running) {
                std::string forward_msg;
                forward_msg.reserve(25 + target_uav.size() + command.size());
                forward_msg += "FORWARDING TO ";
                forward_msg += target_uav;
                forward_msg += ": ";
                forward_msg += command;
                Logger::info(forward_msg);
                uavCommandSockets[i]->send(zmq::buffer(command), zmq::send_flags::none);
                return true;
            }
            break;
        }
    }
    return false;
}

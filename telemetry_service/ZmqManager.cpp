/**
 * @file ZmqManager.cpp
 * @brief Implementation of ZeroMQ communication management
 * 
 * This file contains the implementation of the ZmqManager class, which handles
 * all ZeroMQ-based communication between the telemetry service, UAVs, and UI components.
 */

#include "ZmqManager.h"
#include "Logger.h"
#include <vector>

/**
 * @brief Constructor - initializes ZMQ manager with configuration and callback
 * @param ctx ZeroMQ context for socket creation
 * @param cfg Configuration containing UAV and port settings
 * @param callback Function to call when telemetry messages are received
 */
ZmqManager::ZmqManager(zmq::context_t& ctx, const Config& cfg, ZmqMessageCallback callback)
    : context(ctx), config(cfg), messageCallback_(std::move(callback)) {}

/**
 * @brief Destructor - ensures clean shutdown
 */
ZmqManager::~ZmqManager() {
    stop();
    join();
}

/**
 * @brief Start ZMQ communication system
 * 
 * This method:
 * 1. Creates and binds all necessary ZMQ sockets
 * 2. Sets up UI communication (PUB/PULL sockets)
 * 3. Sets up UAV communication (PULL/PUSH sockets for each UAV)
 * 4. Starts background threads for message processing
 */
void ZmqManager::start() {
    running = true;

    // Set up UI communication sockets
    // PUB socket for publishing telemetry data to UI subscribers
    pubToUi = std::make_unique<zmq::socket_t>(context, zmq::socket_type::pub);
    std::string ui_pub_addr = "tcp://*:" + std::to_string(config.getUiPorts().tcp_publish_port);
    pubToUi->bind(ui_pub_addr);
    Logger::status("TCP", "UI Publisher bound", ui_pub_addr);

    // PULL socket for receiving commands from UI components
    pullFromUi = std::make_unique<zmq::socket_t>(context, zmq::socket_type::pull);
    std::string ui_cmd_addr = "tcp://*:" + std::to_string(config.getUiPorts().tcp_command_port);
    pullFromUi->bind(ui_cmd_addr);
    Logger::status("TCP", "UI Command receiver bound", ui_cmd_addr);
    
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
        
        Logger::status("ZMQ", "UAV " + uav.name + " configured", "Telemetry: " + telemetry_addr + ", Commands: " + command_addr);
    }

    // Start background processing threads
    receiverThread = std::thread(&ZmqManager::receiverLoop, this);
    forwarderThread = std::thread(&ZmqManager::forwarderLoop, this);
}

/**
 * @brief Stop ZMQ communication threads
 * 
 * Sets the running flag to false, causing background threads to exit
 * their main loops. Call join() afterwards to wait for completion.
 */
void ZmqManager::stop() {
    running = false;
}

/**
 * @brief Wait for background threads to complete
 * 
 * Blocks until both receiver and forwarder threads have finished execution.
 */
void ZmqManager::join() {
    if (receiverThread.joinable()) receiverThread.join();
    if (forwarderThread.joinable()) forwarderThread.join();
}

/**
 * @brief Publish telemetry data to UI subscribers
 * @param topic The topic to publish on (e.g., "camera_UAV_1")
 * @param data The telemetry data to send
 * 
 * Uses ZMQ multipart messaging: first frame is topic, second frame is data.
 * UI components subscribe to specific topics to receive relevant data.
 */
void ZmqManager::publishTelemetry(const std::string& topic, const std::string& data) {
    pubToUi->send(zmq::buffer(topic), zmq::send_flags::sndmore);
    pubToUi->send(zmq::buffer(data), zmq::send_flags::none);
    Logger::info("Published to [" + topic + "]: " + data);
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
void ZmqManager::receiverLoop() {
    // Set up polling items for all UAV telemetry sockets
    std::vector<zmq::pollitem_t> pollitems;
    for (auto& sockPtr : uavTelemetrySockets) {
        pollitems.push_back({*sockPtr, 0, ZMQ_POLLIN, 0});
    }

    while (running) {
        // Handle case where no UAVs are configured
        if (pollitems.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }
        
        // Poll all UAV sockets with 100ms timeout
        zmq::poll(pollitems.data(), pollitems.size(), std::chrono::milliseconds(100));
        
        // Check each socket for incoming data
        for (size_t i = 0; i < pollitems.size(); ++i) {
            if (pollitems[i].revents & ZMQ_POLLIN) {
                zmq::message_t message;
                if (uavTelemetrySockets[i]->recv(message, zmq::recv_flags::none)) {
                    // Extract message data and identify source UAV
                    std::string data(static_cast<char*>(message.data()), message.size());
                    std::string uav_name = config.getUAVs()[i].name;
                    
                    // Call the registered callback with source identification
                    if (messageCallback_) {
                        messageCallback_("TCP:" + uav_name, data);
                    }
                }
            }
        }
    }
    Logger::status("ZMQ", "Receiver thread stopped", "");
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
void ZmqManager::forwarderLoop() {
    // Set up polling for UI command socket
    zmq::pollitem_t ui_poll{*pullFromUi, 0, ZMQ_POLLIN, 0};
    
    while (running) {
        // Poll UI socket with 100ms timeout
        zmq::poll(&ui_poll, 1, std::chrono::milliseconds(100));
        if (ui_poll.revents & ZMQ_POLLIN) {
            zmq::message_t ui_msg;
            if (pullFromUi->recv(ui_msg, zmq::recv_flags::none)) {
                std::string msg(static_cast<char*>(ui_msg.data()), ui_msg.size());
                Logger::info("RECEIVED FROM UI [" + extractUISource(msg) + "]: " + msg);

                // Parse command to extract target UAV and actual command
                // Expected format: "UAV_NAME:command_data"
                size_t colon_pos = msg.find(':');
                std::string target_uav = (colon_pos != std::string::npos) ? msg.substr(0, colon_pos) : "UAV_1";
                std::string actual_cmd = (colon_pos != std::string::npos) ? msg.substr(colon_pos + 1) : msg;

                // Find the target UAV and forward the command
                const auto& uavs = config.getUAVs();
                for (size_t i = 0; i < uavs.size(); ++i) {
                    if (uavs[i].name == target_uav) {
                        Logger::info("FORWARDING TO " + target_uav + ": " + actual_cmd);
                        uavCommandSockets[i]->send(zmq::buffer(actual_cmd), zmq::send_flags::none);
                        break;
                    }
                }
            }
        }
    }
    Logger::status("ZMQ", "Forwarder thread stopped", "");
}

/**
 * @brief Extract the UI source type from a command message
 * @param message The command message from UI
 * @return String identifying the UI source ("camera", "mapping", or "unknown")
 * 
 * Analyzes the message content to determine which UI component sent the command.
 * This is used for logging and debugging purposes.
 */
std::string ZmqManager::extractUISource(const std::string& message) {
    if (message.find("[camera-ui]") != std::string::npos) return "camera";
    if (message.find("[mapping-ui]") != std::string::npos) return "mapping";
    return "unknown";
}
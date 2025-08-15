#include "ZmqManager.h"
#include "Logger.h"
#include <vector>

// Constructor tanımını callback alacak şekilde güncelleyin
ZmqManager::ZmqManager(zmq::context_t& ctx, const Config& cfg, ZmqMessageCallback callback)
    : context(ctx), config(cfg), messageCallback_(std::move(callback)) {}

ZmqManager::~ZmqManager() {
    stop();
    join();
}

void ZmqManager::start() {
    running = true;

    // UI soketlerini kur
    pubToUi = std::make_unique<zmq::socket_t>(context, zmq::socket_type::pub);
    std::string ui_pub_addr = "tcp://*:" + std::to_string(config.getUiPorts().publish_port);
    pubToUi->bind(ui_pub_addr);
    Logger::info("UI Publisher bound to: " + ui_pub_addr);

    pullFromUi = std::make_unique<zmq::socket_t>(context, zmq::socket_type::pull);
    std::string ui_cmd_addr = "tcp://*:" + std::to_string(config.getUiPorts().command_port);
    pullFromUi->bind(ui_cmd_addr);
    Logger::info("UI Command receiver bound to: " + ui_cmd_addr);
    
    // UAV soketlerini kur
    for (const auto& uav : config.getUAVs()) {
        auto pull_socket = std::make_unique<zmq::socket_t>(context, zmq::socket_type::pull);
        std::string telemetry_addr = "tcp://*:" + std::to_string(uav.telemetry_port);
        pull_socket->bind(telemetry_addr);
        uavTelemetrySockets.push_back(std::move(pull_socket));

        auto push_socket = std::make_unique<zmq::socket_t>(context, zmq::socket_type::push);
        std::string command_addr = "tcp://*:" + std::to_string(uav.command_port);
        push_socket->bind(command_addr);
        uavCommandSockets.push_back(std::move(push_socket));
        
        Logger::info("UAV " + uav.name + " (ZMQ) - Telemetry: " + telemetry_addr + ", Commands: " + command_addr);
    }

    receiverThread = std::thread(&ZmqManager::receiverLoop, this);
    forwarderThread = std::thread(&ZmqManager::forwarderLoop, this);
}

void ZmqManager::stop() {
    running = false;
}

void ZmqManager::join() {
    if (receiverThread.joinable()) receiverThread.join();
    if (forwarderThread.joinable()) forwarderThread.join();
}

void ZmqManager::publishTelemetry(const std::string& topic, const std::string& data) {
    pubToUi->send(zmq::buffer(topic), zmq::send_flags::sndmore);
    pubToUi->send(zmq::buffer(data), zmq::send_flags::none);
    Logger::info("Published to [" + topic + "]: " + data);
}

void ZmqManager::receiverLoop() {
    std::vector<zmq::pollitem_t> pollitems;
    for (auto& sockPtr : uavTelemetrySockets) {
        pollitems.push_back({*sockPtr, 0, ZMQ_POLLIN, 0});
    }

    while (running) {
        if (pollitems.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }
        
        zmq::poll(pollitems.data(), pollitems.size(), std::chrono::milliseconds(100));
        
        for (size_t i = 0; i < pollitems.size(); ++i) {
            if (pollitems[i].revents & ZMQ_POLLIN) {
                zmq::message_t message;
                if (uavTelemetrySockets[i]->recv(message, zmq::recv_flags::none)) {
                    std::string data(static_cast<char*>(message.data()), message.size());
                    std::string uav_name = config.getUAVs()[i].name;
                    
                    // DOĞRU YER: Veriyi burada etiketle ve callback ile gönder
                    if (messageCallback_) {
                        messageCallback_("ZMQ:" + uav_name, data);
                    }
                }
            }
        }
    }
    Logger::info("ZMQ Receiver thread stopped.");
}

void ZmqManager::forwarderLoop() {
    zmq::pollitem_t ui_poll{*pullFromUi, 0, ZMQ_POLLIN, 0};
    
    while (running) {
        zmq::poll(&ui_poll, 1, std::chrono::milliseconds(100));
        if (ui_poll.revents & ZMQ_POLLIN) {
            zmq::message_t ui_msg;
            if (pullFromUi->recv(ui_msg, zmq::recv_flags::none)) {
                std::string msg(static_cast<char*>(ui_msg.data()), ui_msg.size());
                Logger::info("RECEIVED FROM UI [" + extractUISource(msg) + "]: " + msg);

                size_t colon_pos = msg.find(':');
                std::string target_uav = (colon_pos != std::string::npos) ? msg.substr(0, colon_pos) : "UAV_1";
                std::string actual_cmd = (colon_pos != std::string::npos) ? msg.substr(colon_pos + 1) : msg;

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
    Logger::info("ZMQ Forwarder thread stopped.");
}

std::string ZmqManager::extractUISource(const std::string& message) {
    if (message.find("[camera-ui]") != std::string::npos) return "camera";
    if (message.find("[mapping-ui]") != std::string::npos) return "mapping";
    return "unknown";
}
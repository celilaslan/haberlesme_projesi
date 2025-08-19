/**
 * @file camera_ui.cpp
 * @brief Camera UI application for receiving and displaying camera telemetry data
 * 
 * This application connects to the telemetry service and subscribes to camera-related
 * telemetry data from UAVs. It supports both TCP and UDP protocols for receiving data.
 * It can also optionally send commands back to UAVs via the telemetry service.
 */

#include <zmq.hpp>
#include <boost/asio.hpp>
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <chrono>
#include <ctime>
#include <sstream>
#include <cstdlib>
#include <memory>
#include <thread>
#include <string>
#include <atomic>
#include <csignal>
#include <iomanip>
#include <sys/select.h>
#include <unistd.h>

using boost::asio::ip::udp;
using json = nlohmann::json;

// Global flag for graceful shutdown
std::atomic<bool> g_running(true);
std::atomic<int> g_signal_received(0);

/**
 * @brief Signal handler for graceful shutdown
 * @param signal Signal number received
 */
void signalHandler(int signal) {
    g_signal_received.store(signal);
    g_running.store(false);
}

/**
 * @brief Generate a formatted timestamp string with millisecond precision
 * @return Timestamp string in format "YYYY-MM-DD HH:MM:SS.mmm"
 * 
 * Uses platform-specific time conversion functions for thread safety.
 * Used for logging telemetry data reception times.
 */
std::string GetTimestamp() {
    const auto now = std::chrono::system_clock::now();
    const auto time_t_now = std::chrono::system_clock::to_time_t(now);
    const auto duration = now.time_since_epoch();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(duration) % 1000;

    std::ostringstream oss;
    struct tm time_info;
    
    // Use platform-specific thread-safe time conversion
#if defined(_WIN32)
    localtime_s(&time_info, &time_t_now);
#else
    localtime_r(&time_t_now, &time_info);
#endif

    oss << std::put_time(&time_info, "%Y-%m-%d %H:%M:%S");
    oss << '.' << std::setfill('0') << std::setw(3) << ms.count();

    return oss.str();
}

/**
 * @brief Load configuration from service_config.json file
 * @param tcp_publish_port Reference to store TCP publish port
 * @param tcp_command_port Reference to store TCP command port
 * @param udp_camera_port Reference to store UDP camera port
 * @return true if configuration loaded successfully, false otherwise
 */
bool loadConfig(int& tcp_publish_port, int& tcp_command_port, int& udp_camera_port) {
    std::ifstream file("service_config.json");
    if (!file.is_open()) {
        std::cerr << "Warning: Could not open service_config.json, using default ports\n";
        tcp_publish_port = 5557;
        tcp_command_port = 5558;
        udp_camera_port = 5570;
        return false;
    }

    try {
        json j;
        file >> j;
        file.close();

        if (j.contains("ui_ports")) {
            tcp_publish_port = j["ui_ports"]["tcp_publish_port"];
            tcp_command_port = j["ui_ports"]["tcp_command_port"];
            udp_camera_port = j["ui_ports"]["udp_camera_port"];
            return true;
        } else {
            std::cerr << "Warning: ui_ports section not found in config, using defaults\n";
            tcp_publish_port = 5557;
            tcp_command_port = 5558;
            udp_camera_port = 5570;
            return false;
        }
    } catch (const std::exception& e) {
        std::cerr << "Error parsing config file: " << e.what() << ", using defaults\n";
        tcp_publish_port = 5557;
        tcp_command_port = 5558;
        udp_camera_port = 5570;
        return false;
    }
}

/**
 * @brief Main entry point for the camera UI application
 * @param argc Number of command line arguments
 * @param argv Array of command line argument strings
 * @return Exit code (0 for success)
 * 
 * Usage:
 * ./camera_ui --protocol <tcp|udp> [--send UAV_NAME]
 * 
 * Required arguments:
 * --protocol tcp|udp : Communication protocol (TCP or UDP)
 * 
 * Optional arguments:
 * --send UAV_NAME    : Enable command sending to specified UAV
 * 
 * This function:
 * 1. Parses command line arguments to determine protocol and options
 * 2. Sets up TCP or UDP receiver based on protocol selection
 * 3. Optionally sets up a command sender if --send flag is provided
 * 4. Continuously receives and displays telemetry data
 * 5. Allows sending commands to UAVs via stdin input
 */
int main(int argc, char* argv[]) {
    // Set up signal handlers for graceful shutdown
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
    
    std::string protocol = "";  // No default - explicit choice required
    std::string target = "UAV_1";
    bool enableSender = false;
    
    // Load configuration from file
    int tcp_publish_port, tcp_command_port, udp_camera_port;
    loadConfig(tcp_publish_port, tcp_command_port, udp_camera_port);
    
    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "--protocol" && i + 1 < argc) {
            protocol = argv[++i];
        } else if (std::string(argv[i]) == "--send" && i + 1 < argc) {
            enableSender = true;
            target = argv[++i];
        }
    }
    
    // Validate required protocol argument
    if (protocol.empty()) {
        std::cerr << "Error: Protocol must be specified!\n";
        std::cerr << "Usage: ./camera_ui --protocol <tcp|udp> [--send UAV_NAME]\n";
        std::cerr << "  --protocol tcp|udp : Communication protocol (required)\n";
        std::cerr << "  --send UAV_NAME    : Enable command sending to specified UAV\n";
        return 1;
    }
    
    if (protocol != "tcp" && protocol != "udp") {
        std::cerr << "Error: Protocol must be 'tcp' or 'udp'\n";
        return 1;
    }
    
    if (protocol == "udp") {
        // UDP-based telemetry reception (read-only)
        std::cout << "[Camera UI] Using UDP protocol on port " << udp_camera_port << " (telemetry reception only)\n";
        
        if (enableSender) {
            std::cerr << "Warning: Command sending is not supported via UDP protocol. UDP is for telemetry reception only.\n";
            std::cerr << "Use TCP protocol (--protocol tcp) for bidirectional communication with command sending.\n";
            enableSender = false;
        }
        
        boost::asio::io_context io_context;
        udp::socket socket(io_context, udp::endpoint(udp::v4(), udp_camera_port));
        
        // Main UDP telemetry reception loop (read-only)
        std::cout << "Press Ctrl+C to stop the camera UI..." << std::endl;
        while (g_running) {
            char buffer[2048];  // Increased buffer size for safety
            udp::endpoint sender_endpoint;
            
            try {
                // Set socket to non-blocking mode for responsive shutdown
                socket.non_blocking(true);
                
                size_t length = 0;
                boost::system::error_code ec;
                
                // Non-blocking receive
                length = socket.receive_from(boost::asio::buffer(buffer), sender_endpoint, 0, ec);
                
                if (ec) {
                    if (ec == boost::asio::error::would_block) {
                        // No data available, check for shutdown signal
                        std::this_thread::sleep_for(std::chrono::milliseconds(10));
                        continue;
                    } else {
                        std::cerr << "UDP receive error: " << ec.message() << std::endl;
                        continue;
                    }
                }
                
                if (length > 0 && length < sizeof(buffer)) {
                    std::string message(buffer, length);
                    
                    // Parse message format: "topic|data"
                    size_t separator = message.find('|');
                    if (separator != std::string::npos) {
                        std::string topic = message.substr(0, separator);
                        std::string data = message.substr(separator + 1);
                        
                        // Only display camera-related topics
                        if (topic.find("camera") == 0) {
                            std::cout << "[" << GetTimestamp() << "] Topic: " << topic << " | Data: " << data << std::endl;
                        }
                    }
                }
            } catch (const std::exception& e) {
                if (g_running) {
                    std::cerr << "UDP receive error: " << e.what() << std::endl;
                }
            }
        }
        
        // Cleanup UDP resources
        try {
            socket.close();
        } catch (const std::exception&) {
            // Ignore cleanup errors
        }
        
    } else {
        // TCP-based telemetry reception (original implementation)
        std::cout << "[Camera UI] Using TCP protocol\n";
        
        // Create ZeroMQ context with 1 I/O thread
        zmq::context_t context(1);
        
        // Create subscriber socket to receive telemetry data
        zmq::socket_t subscriber(context, zmq::socket_type::sub);

        // Connect to the telemetry service's publish port
        subscriber.connect("tcp://localhost:" + std::to_string(tcp_publish_port));
        
        // Subscribe to all messages with "camera" topic prefix
        // This will receive messages like "camera_UAV_1", "camera_UAV_2", etc.
        subscriber.set(zmq::sockopt::subscribe, "camera");

        std::cout << "[Camera UI] Subscribed to 'camera' topic\n";

        // Optional command sender functionality
        std::unique_ptr<zmq::socket_t> push;
        std::thread senderThread;
        
        if (enableSender) {
            // Create PUSH socket for sending commands to the service
            push = std::make_unique<zmq::socket_t>(context, zmq::socket_type::push);
            push->connect("tcp://localhost:" + std::to_string(tcp_command_port));  // Service command port
            
            // Start background thread to read commands from stdin
            senderThread = std::thread([p = push.get(), target]() {
                std::string line;
                std::cout << "[Camera UI] Type commands for " << target << " (press Enter to send, Ctrl+C to exit):" << std::endl;
                
                while (g_running) {
                    // Check if input is available without blocking
                    fd_set readfds;
                    FD_ZERO(&readfds);
                    FD_SET(STDIN_FILENO, &readfds);
                    
                    struct timeval timeout;
                    timeout.tv_sec = 0;
                    timeout.tv_usec = 100000; // 100ms timeout
                    
                    int result = select(STDIN_FILENO + 1, &readfds, nullptr, nullptr, &timeout);
                    
                    if (result > 0 && FD_ISSET(STDIN_FILENO, &readfds)) {
                        if (std::getline(std::cin, line)) {
                            if (!g_running) break;
                            try {
                                // Format: "TARGET_UAV:[camera-ui] user_command"
                                const std::string msg = target + ":[camera-ui] " + line;
                                p->send(zmq::buffer(msg), zmq::send_flags::dontwait);
                                std::cout << "[Camera UI] Sent command: " << line << std::endl;
                            } catch (const zmq::error_t& e) {
                                if (g_running && e.num() != EAGAIN) {
                                    std::cerr << "TCP send error: " << e.what() << std::endl;
                                }
                            }
                        } else {
                            break; // EOF or error
                        }
                    } else if (result < 0) {
                        break; // Error in select
                    }
                    // If result == 0, timeout occurred, loop continues and checks g_running
                }
            });
            
            std::cout << "[Camera UI] TCP Sender enabled to " << target << " via port " << tcp_command_port << "\n";
        }

        // Main TCP telemetry reception loop
        std::cout << "Press Ctrl+C to stop the camera UI..." << std::endl;
        while (g_running) {
            zmq::message_t topic, data;
            
            try {
                // Use non-blocking receive to allow periodic checks of g_running
                if (!subscriber.recv(topic, zmq::recv_flags::dontwait)) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    continue;
                }
                
                // Receive data (second frame of multipart message)
                if (!subscriber.recv(data, zmq::recv_flags::dontwait)) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    continue;
                }

                // Convert TCP messages to strings
                std::string msg(static_cast<char*>(data.data()), data.size());
                std::string topic_str(static_cast<char*>(topic.data()), topic.size());

                // Display received telemetry data with timestamp
                std::cout << "[" << GetTimestamp() << "] Topic: " << topic_str << " | Data: " << msg << std::endl;
                
            } catch (const zmq::error_t& e) {
                if (g_running && e.num() != EAGAIN) {
                    std::cerr << "TCP receive error: " << e.what() << std::endl;
                }
            }
        }
        
        // Cleanup TCP resources
        if (senderThread.joinable()) {
            senderThread.join();
        }
        if (push) {
            try {
                push->close();
            } catch (const zmq::error_t&) {
                // Ignore cleanup errors
            }
        }
        try {
            subscriber.close();
        } catch (const zmq::error_t&) {
            // Ignore cleanup errors
        }
    }
    
    // Log shutdown reason if caused by signal
    int signal_num = g_signal_received.load();
    if (signal_num > 0) {
        std::cout << "Camera UI shutdown initiated by signal: " << signal_num << std::endl;
    }
    
    std::cout << "Camera UI stopped." << std::endl;
    return 0;
}
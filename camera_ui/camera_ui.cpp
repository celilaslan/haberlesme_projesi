/**
 * @file camera_ui.cpp
 * @brief Camera UI application for receiving and displaying camera telemetry data
 * 
 * This application connects to the telemetry service and subscribes to camera-related
 * telemetry data from UAVs. It can also optionally send commands back to UAVs
 * via the telemetry service.
 */

#include <zmq.hpp>
#include <iostream>
#include <chrono>
#include <ctime>
#include <sstream>
#include <cstdlib>
#include <memory>
#include <thread>

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
 * @brief Main entry point for the camera UI application
 * @param argc Number of command line arguments
 * @param argv Array of command line argument strings
 * @return Exit code (0 for success)
 * 
 * This function:
 * 1. Sets up a ZMQ subscriber to receive camera telemetry data
 * 2. Optionally sets up a command sender if --send flag is provided
 * 3. Continuously receives and displays telemetry data
 * 4. Allows sending commands to UAVs via stdin input
 */
int main(int argc, char* argv[]) {
    // Create ZeroMQ context with 1 I/O thread
    zmq::context_t context(1);
    
    // Create subscriber socket to receive telemetry data
    zmq::socket_t subscriber(context, zmq::socket_type::sub);

    // Connect to the telemetry service's publish port
    subscriber.connect("tcp://localhost:5557");
    
    // Subscribe to all messages with "camera" topic prefix
    // This will receive messages like "camera_UAV_1", "camera_UAV_2", etc.
    subscriber.set(zmq::sockopt::subscribe, "camera");

    std::cout << "[Camera UI] Subscribed to 'camera' topic\n";

    // Optional command sender functionality
    // Usage: ./camera_ui --send UAV_1
    std::unique_ptr<zmq::socket_t> push;
    std::string target = "UAV_1";  // Default target UAV
    
    if (argc >= 3 && std::string(argv[1]) == "--send") {
        target = argv[2];
        
        // Create PUSH socket for sending commands to the service
        push = std::make_unique<zmq::socket_t>(context, zmq::socket_type::push);
        push->connect("tcp://localhost:5558");  // Service command port
        
        // Start background thread to read commands from stdin
        std::thread([p = push.get(), target]() {
            std::string line;
            while (std::getline(std::cin, line)) {
                // Format: "TARGET_UAV:[camera-ui] user_command"
                const std::string msg = target + ":[camera-ui] " + line;
                p->send(zmq::buffer(msg), zmq::send_flags::none);
            }
        }).detach();  // Detach thread to run independently
        
        std::cout << "[Camera UI] Sender enabled to " << target << " via 5558\n";
    }

    // Main telemetry reception loop
    while (true) {
        zmq::message_t topic, data;
        
        // Receive topic (first frame of multipart message)
        if (!subscriber.recv(topic)) continue;
        
        // Receive data (second frame of multipart message)
        if (!subscriber.recv(data)) continue;

        // Convert ZMQ messages to strings
        std::string msg(static_cast<char*>(data.data()), data.size());
        std::string topic_str(static_cast<char*>(topic.data()), topic.size());

        // Display received telemetry data with timestamp
        std::cout << "[" << GetTimestamp() << "] Topic: " << topic_str << " | Data: " << msg << std::endl;
    }
    
    return 0;
}
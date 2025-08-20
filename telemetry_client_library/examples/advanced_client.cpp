/**
 * @file advanced_client.cpp
 * @brief Advanced example showing filtering, commands, and full client features
 *
 * This example demonstrates advanced features of the TelemetryClient library:
 * - Filtering by UAV name and data type
 * - Sending commands to UAVs
 * - Interactive user interface
 * - Error handling
 */

#include "TelemetryClient.h"
#include <iostream>
#include <csignal>
#include <atomic>
#include <thread>
#include <chrono>
#include <string>
#include <sstream>

using namespace TelemetryAPI;

// Global flag for graceful shutdown
std::atomic<bool> g_running(true);

/**
 * @brief Signal handler for graceful shutdown
 */
void signalHandler(int signal) {
    std::cout << "\nShutdown signal received..." << std::endl;
    g_running = false;
}

/**
 * @brief Callback function to handle received telemetry data
 * @param data The received telemetry data
 */
void onTelemetryReceived(const TelemetryData& data) {
    // Parse the raw data if it's in the standard format
    std::string uav_name_parsed;
    int numeric_code;

    std::string data_type_str = (data.data_type == DataType::MAPPING ? "MAPPING" :
                                data.data_type == DataType::CAMERA ? "CAMERA" : "UNKNOWN");
    std::string protocol_str = (data.received_via == Protocol::TCP_ONLY ? "TCP" :
                               data.received_via == Protocol::UDP_ONLY ? "UDP" : "BOTH");

    if (parseTelemetryMessage(data.raw_data, uav_name_parsed, numeric_code)) {
        std::cout << "[" << data_type_str << "] " << data.uav_name
                  << " -> Code: " << numeric_code
                  << " (via " << protocol_str << ")" << std::endl;
    } else {
        std::cout << "[" << data_type_str << "] " << data.uav_name
                  << " -> " << data.raw_data
                  << " (via " << protocol_str << ")" << std::endl;
    }
}

/**
 * @brief Callback function to handle errors
 * @param error_message Description of the error
 */
void onError(const std::string& error_message) {
    std::cerr << "ERROR: " << error_message << std::endl;
}

/**
 * @brief Display help information
 */
void showHelp() {
    std::cout << "\nAvailable commands:" << std::endl;
    std::cout << "  help                    - Show this help" << std::endl;
    std::cout << "  status                  - Show client status" << std::endl;
    std::cout << "  filter uav <UAV_NAME>   - Filter by specific UAV" << std::endl;
    std::cout << "  filter type <mapping|camera> - Filter by data type" << std::endl;
    std::cout << "  send <UAV_NAME> <COMMAND> - Send command to UAV" << std::endl;
    std::cout << "  debug <on|off>          - Toggle debug mode" << std::endl;
    std::cout << "  quit                    - Exit the application" << std::endl;
    std::cout << std::endl;
}

int main(int argc, char* argv[]) {
    // Set up signal handler
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    std::cout << "=== Advanced Telemetry Client ===" << std::endl;
    std::cout << "TelemetryClient Library Version: " << getLibraryVersion() << std::endl;
    std::cout << std::endl;

    // Parse command line arguments
    std::string protocol_str = "both";
    std::string service_host = "localhost";

    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "--protocol" && i + 1 < argc) {
            protocol_str = argv[++i];
        } else if (std::string(argv[i]) == "--host" && i + 1 < argc) {
            service_host = argv[++i];
        } else if (std::string(argv[i]) == "--help") {
            std::cout << "Usage: " << argv[0] << " [--protocol tcp|udp|both] [--host hostname]" << std::endl;
            std::cout << "  --protocol: Communication protocol (default: both)" << std::endl;
            std::cout << "  --host:     Telemetry service hostname (default: localhost)" << std::endl;
            return 0;
        }
    }

    // Convert protocol string to enum
    Protocol protocol = Protocol::BOTH;
    if (protocol_str == "tcp") {
        protocol = Protocol::TCP_ONLY;
    } else if (protocol_str == "udp") {
        protocol = Protocol::UDP_ONLY;
    } else if (protocol_str == "both") {
        protocol = Protocol::BOTH;
    } else {
        std::cerr << "Invalid protocol: " << protocol_str << std::endl;
        return 1;
    }

    // Create and initialize the client
    TelemetryClient client;
    client.setDebugMode(false); // Start with debug off for cleaner output

    if (!client.initialize(service_host)) {
        std::cerr << "Failed to initialize client: " << client.getLastError() << std::endl;
        return 1;
    }

    std::cout << "✓ Client initialized successfully." << std::endl;

    auto uavs = client.getAvailableUAVs();
    std::cout << "Available UAVs: ";
    if (uavs.empty()) {
        std::cout << "None found (using defaults)" << std::endl;
    } else {
        for (const auto& uav : uavs) {
            std::cout << uav << " ";
        }
        std::cout << std::endl;
    }

    // Start receiving telemetry data
    if (!client.startReceiving(protocol, onTelemetryReceived, onError)) {
        std::cerr << "Failed to start receiving: " << client.getLastError() << std::endl;
        return 1;
    }

    std::cout << "✓ Started receiving telemetry data." << std::endl;
    std::cout << "Connection Status: " << client.getConnectionStatus() << std::endl;
    std::cout << std::endl;

    showHelp();

    // Interactive command loop
    std::cout << "Enter commands (type 'help' for options, 'quit' to exit):" << std::endl;

    std::string line;
    while (g_running && client.isReceiving()) {
        std::cout << "> ";
        std::cout.flush();

        // Check if input is available (simple polling)
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        if (std::getline(std::cin, line)) {
            if (!g_running) break;

            // Parse command
            std::istringstream iss(line);
            std::string command;
            iss >> command;

            if (command == "quit" || command == "exit") {
                break;
            } else if (command == "help") {
                showHelp();
            } else if (command == "status") {
                std::cout << "Status: " << client.getConnectionStatus() << std::endl;
                std::cout << "Receiving: " << (client.isReceiving() ? "Yes" : "No") << std::endl;
                if (!client.getLastError().empty()) {
                    std::cout << "Last Error: " << client.getLastError() << std::endl;
                }
            } else if (command == "filter") {
                std::string filter_type;
                iss >> filter_type;

                if (filter_type == "uav") {
                    std::string uav_name;
                    iss >> uav_name;
                    if (!uav_name.empty()) {
                        if (client.subscribeToUAV(uav_name)) {
                            std::cout << "✓ Now filtering for UAV: " << uav_name << std::endl;
                        } else {
                            std::cout << "✗ Failed to set UAV filter" << std::endl;
                        }
                    } else {
                        std::cout << "Usage: filter uav <UAV_NAME>" << std::endl;
                    }
                } else if (filter_type == "type") {
                    std::string data_type_str;
                    iss >> data_type_str;

                    if (data_type_str == "mapping") {
                        if (client.subscribeToDataType(DataType::MAPPING)) {
                            std::cout << "✓ Now filtering for MAPPING data" << std::endl;
                        } else {
                            std::cout << "✗ Failed to set data type filter" << std::endl;
                        }
                    } else if (data_type_str == "camera") {
                        if (client.subscribeToDataType(DataType::CAMERA)) {
                            std::cout << "✓ Now filtering for CAMERA data" << std::endl;
                        } else {
                            std::cout << "✗ Failed to set data type filter" << std::endl;
                        }
                    } else {
                        std::cout << "Usage: filter type <mapping|camera>" << std::endl;
                    }
                } else {
                    std::cout << "Usage: filter <uav|type> <value>" << std::endl;
                }
            } else if (command == "send") {
                std::string uav_name, cmd_text;
                iss >> uav_name;
                std::getline(iss, cmd_text);

                if (!uav_name.empty() && !cmd_text.empty()) {
                    // Remove leading whitespace from command text
                    size_t start = cmd_text.find_first_not_of(" \t");
                    if (start != std::string::npos) {
                        cmd_text = cmd_text.substr(start);
                    }

                    if (client.sendCommand(uav_name, cmd_text, "AdvancedClient")) {
                        std::cout << "✓ Command sent to " << uav_name << ": " << cmd_text << std::endl;
                    } else {
                        std::cout << "✗ Failed to send command: " << client.getLastError() << std::endl;
                    }
                } else {
                    std::cout << "Usage: send <UAV_NAME> <COMMAND>" << std::endl;
                }
            } else if (command == "debug") {
                std::string debug_mode;
                iss >> debug_mode;

                if (debug_mode == "on") {
                    client.setDebugMode(true);
                    std::cout << "✓ Debug mode enabled" << std::endl;
                } else if (debug_mode == "off") {
                    client.setDebugMode(false);
                    std::cout << "✓ Debug mode disabled" << std::endl;
                } else {
                    std::cout << "Usage: debug <on|off>" << std::endl;
                }
            } else if (!command.empty()) {
                std::cout << "Unknown command: " << command << " (type 'help' for options)" << std::endl;
            }
        } else {
            // EOF or error
            break;
        }
    }

    // Clean shutdown
    std::cout << "\nStopping client..." << std::endl;
    client.stopReceiving();

    std::cout << "Advanced client stopped." << std::endl;
    return 0;
}

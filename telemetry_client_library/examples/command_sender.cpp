/**
 * @file command_sender.cpp
 * @brief Example application for sending commands to UAVs
 *
 * This example demonstrates how to use the TelemetryClient library
 * to send commands to UAVs via the telemetry service.
 */

#include "TelemetryClient.h"
#include <iostream>
#include <string>
#include <vector>
#include <sstream>

using namespace TelemetryAPI;

/**
 * @brief Display usage information
 */
void showUsage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [options]" << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  --host <hostname>    Telemetry service hostname (default: localhost)" << std::endl;
    std::cout << "  --uav <UAV_NAME>     Target UAV name (default: UAV_1)" << std::endl;
    std::cout << "  --command <COMMAND>  Command to send (interactive mode if not specified)" << std::endl;
    std::cout << "  --client <NAME>      Client identifier (default: CommandSender)" << std::endl;
    std::cout << "  --help               Show this help" << std::endl;
    std::cout << std::endl;
    std::cout << "Examples:" << std::endl;
    std::cout << "  " << program_name << " --uav UAV_1 --command \"takeoff\"" << std::endl;
    std::cout << "  " << program_name << " --uav UAV_2 --command \"land immediately\"" << std::endl;
    std::cout << "  " << program_name << "  # Interactive mode" << std::endl;
}

/**
 * @brief Send a single command and exit
 */
bool sendSingleCommand(TelemetryClient& client, const std::string& uav_name,
                      const std::string& command, const std::string& client_name) {
    std::cout << "Sending command to " << uav_name << ": " << command << std::endl;

    if (client.sendCommand(uav_name, command, client_name)) {
        std::cout << "✓ Command sent successfully." << std::endl;
        return true;
    } else {
        std::cout << "✗ Failed to send command: " << client.getLastError() << std::endl;
        return false;
    }
}

/**
 * @brief Interactive command sending mode
 */
void interactiveMode(TelemetryClient& client, const std::string& client_name) {
    std::cout << "=== Interactive Command Mode ===" << std::endl;
    std::cout << "Available UAVs: ";

    auto uavs = client.getAvailableUAVs();
    if (uavs.empty()) {
        std::cout << "UAV_1, UAV_2, UAV_3 (defaults)" << std::endl;
    } else {
        for (const auto& uav : uavs) {
            std::cout << uav << " ";
        }
        std::cout << std::endl;
    }

    std::cout << std::endl;
    std::cout << "Enter commands in format: <UAV_NAME> <COMMAND>" << std::endl;
    std::cout << "Examples:" << std::endl;
    std::cout << "  UAV_1 takeoff" << std::endl;
    std::cout << "  UAV_2 land immediately" << std::endl;
    std::cout << "  UAV_3 status report" << std::endl;
    std::cout << "Type 'quit' to exit." << std::endl;
    std::cout << std::endl;

    std::string line;
    while (true) {
        std::cout << "cmd> ";
        std::cout.flush();

        if (!std::getline(std::cin, line)) {
            break; // EOF
        }

        // Trim whitespace
        size_t start = line.find_first_not_of(" \t");
        if (start == std::string::npos) {
            continue; // Empty line
        }
        line = line.substr(start);

        if (line == "quit" || line == "exit") {
            break;
        }

        // Parse UAV name and command
        std::istringstream iss(line);
        std::string uav_name;
        iss >> uav_name;

        std::string command;
        std::getline(iss, command);

        if (uav_name.empty()) {
            std::cout << "Please specify UAV name and command." << std::endl;
            continue;
        }

        if (command.empty()) {
            std::cout << "Please specify a command." << std::endl;
            continue;
        }

        // Remove leading whitespace from command
        start = command.find_first_not_of(" \t");
        if (start != std::string::npos) {
            command = command.substr(start);
        }

        if (command.empty()) {
            std::cout << "Please specify a command." << std::endl;
            continue;
        }

        // Send the command
        std::cout << "Sending to " << uav_name << ": " << command << std::endl;

        if (client.sendCommand(uav_name, command, client_name)) {
            std::cout << "✓ Command sent successfully." << std::endl;
        } else {
            std::cout << "✗ Failed to send command: " << client.getLastError() << std::endl;
        }

        std::cout << std::endl;
    }
}

int main(int argc, char* argv[]) {
    std::cout << "=== UAV Command Sender ===" << std::endl;
    std::cout << "TelemetryClient Library Version: " << getLibraryVersion() << std::endl;
    std::cout << std::endl;

    // Parse command line arguments
    std::string service_host = "localhost";
    std::string uav_name = "UAV_1";
    std::string command = "";
    std::string client_name = "CommandSender";

    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "--host" && i + 1 < argc) {
            service_host = argv[++i];
        } else if (std::string(argv[i]) == "--uav" && i + 1 < argc) {
            uav_name = argv[++i];
        } else if (std::string(argv[i]) == "--command" && i + 1 < argc) {
            command = argv[++i];
        } else if (std::string(argv[i]) == "--client" && i + 1 < argc) {
            client_name = argv[++i];
        } else if (std::string(argv[i]) == "--help") {
            showUsage(argv[0]);
            return 0;
        }
    }

    // Test service connection
    std::cout << "Testing connection to telemetry service at " << service_host << "..." << std::endl;
    if (!testServiceConnection(service_host, 5558, 3000)) {
        std::cout << "Warning: Could not connect to telemetry service command port." << std::endl;
        std::cout << "Make sure the telemetry service is running and accessible." << std::endl;

        char choice;
        std::cout << "Continue anyway? (y/n): ";
        std::cin >> choice;
        if (choice != 'y' && choice != 'Y') {
            return 1;
        }
        std::cin.ignore(); // Clear input buffer
    } else {
        std::cout << "✓ Service connection test passed." << std::endl;
    }

    // Create and initialize the client
    TelemetryClient client;
    client.setDebugMode(true);

    if (!client.initialize(service_host)) {
        std::cerr << "Failed to initialize client: " << client.getLastError() << std::endl;
        return 1;
    }

    std::cout << "✓ Client initialized successfully." << std::endl;
    std::cout << "Connection Status: " << client.getConnectionStatus() << std::endl;
    std::cout << std::endl;

    // Determine mode: single command or interactive
    if (!command.empty()) {
        // Single command mode
        bool success = sendSingleCommand(client, uav_name, command, client_name);
        return success ? 0 : 1;
    } else {
        // Interactive mode
        interactiveMode(client, client_name);
        std::cout << "Command sender stopped." << std::endl;
        return 0;
    }
}

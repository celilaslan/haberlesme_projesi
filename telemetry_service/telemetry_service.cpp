#include "TelemetryService.h"
#include <iostream>
#include <csignal>
#include <atomic>

std::atomic<bool> g_running(true);

void signalHandler(int signum) {
    std::cout << "Signal " << signum << " received. Shutting down..." << std::endl;
    g_running = false;
}

int main() {
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    try {
        TelemetryService service;
        service.run(g_running);
    } catch (const std::exception& e) {
        std::cerr << "A fatal error occurred: " << e.what() << std::endl;
        return 1;
    }

    std::cout << "Application terminated gracefully." << std::endl;
    return 0;
}
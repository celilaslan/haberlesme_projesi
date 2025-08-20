/**
 * @file MockUAV.cpp
 * @brief Implementation of the MockUAV class
 *
 * Mock UAV simulation for testing and development.
 */

#include "TelemetryClient.h"
#include <thread>
#include <atomic>
#include <mutex>
#include <chrono>
#include <random>
#include <queue>

namespace TelemetryAPI {

/**
 * @class MockUAV::Impl
 * @brief Private implementation for MockUAV
 */
class MockUAV::Impl {
public:
    Impl() : running_(false), data_loss_rate_(0.0), additional_latency_(0),
             message_counter_(1000) {}

    bool createMockUAV(const std::string& name, const std::map<std::string, std::string>& config) {
        std::lock_guard<std::mutex> lock(config_mutex_);

        uav_name_ = name;
        config_ = config;

        // Parse configuration
        auto it = config.find("data_rate_ms");
        if (it != config.end()) {
            try {
                data_rate_ms_ = std::stoi(it->second);
            } catch (...) {
                data_rate_ms_ = 1000; // Default 1 second
            }
        }

        it = config.find("base_code");
        if (it != config.end()) {
            try {
                base_code_ = std::stoi(it->second);
            } catch (...) {
                base_code_ = 1000; // Default base code
            }
        }

        return true;
    }

    bool simulateDataLoss(double loss_percentage) {
        if (loss_percentage < 0.0 || loss_percentage > 1.0) {
            return false;
        }

        data_loss_rate_ = loss_percentage;
        return true;
    }

    bool simulateLatency(int additional_ms) {
        if (additional_ms < 0) {
            return false;
        }

        additional_latency_ = additional_ms;
        return true;
    }

    bool injectTestData(const std::string& test_data) {
        std::lock_guard<std::mutex> lock(injection_mutex_);
        injected_data_queue_.push(test_data);
        return true;
    }

    bool start() {
        if (running_) {
            return false; // Already running
        }

        running_ = true;

        // Start simulation thread
        simulation_thread_ = std::thread([this]() {
            runSimulation();
        });

        return true;
    }

    bool stop() {
        if (!running_) {
            return false;
        }

        running_ = false;

        if (simulation_thread_.joinable()) {
            simulation_thread_.join();
        }

        return true;
    }

    bool isRunning() const {
        return running_;
    }

    void setDataCallback(std::function<void(const TelemetryData&)> callback) {
        data_callback_ = callback;
    }

    ~Impl() {
        stop();
    }

private:
    void runSimulation() {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_real_distribution<> loss_dist(0.0, 1.0);

        while (running_) {
            // Check for injected data first
            std::string data_to_send;
            bool has_injected_data = false;

            {
                std::lock_guard<std::mutex> lock(injection_mutex_);
                if (!injected_data_queue_.empty()) {
                    data_to_send = injected_data_queue_.front();
                    injected_data_queue_.pop();
                    has_injected_data = true;
                }
            }

            if (!has_injected_data) {
                // Generate normal telemetry data
                data_to_send = generateTelemetryData();
            }

            // Simulate data loss
            if (loss_dist(gen) > data_loss_rate_) {
                // Data not lost, proceed to send

                // Simulate latency
                if (additional_latency_ > 0) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(additional_latency_));
                }

                // Create TelemetryData structure
                TelemetryData telemetry_data;
                telemetry_data.uav_name = uav_name_;
                telemetry_data.raw_data = data_to_send;
                telemetry_data.timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();

                // Determine data type based on message content
                if (data_to_send.find("1") == data_to_send.length() - 4) { // Ends with 1xxx
                    telemetry_data.data_type = DataType::MAPPING;
                } else if (data_to_send.find("2") == data_to_send.length() - 4) { // Ends with 2xxx
                    telemetry_data.data_type = DataType::CAMERA;
                } else {
                    telemetry_data.data_type = DataType::UNKNOWN;
                }

                telemetry_data.received_via = Protocol::TCP_ONLY; // Mock as TCP

                // Send data via callback
                if (data_callback_) {
                    data_callback_(telemetry_data);
                }
            }

            // Wait for next data generation cycle
            std::this_thread::sleep_for(std::chrono::milliseconds(data_rate_ms_));
        }
    }

    std::string generateTelemetryData() {
        // Generate data similar to real UAV simulator
        std::string data_type_prefix;
        int current_code;

        // Alternate between mapping and camera data
        if (message_counter_ % 2 == 0) {
            current_code = base_code_ + (message_counter_ / 2);
        } else {
            current_code = (base_code_ + 1000) + (message_counter_ / 2);
        }

        message_counter_++;

        return uav_name_ + "  " + std::to_string(current_code);
    }

    mutable std::mutex config_mutex_;
    mutable std::mutex injection_mutex_;

    std::string uav_name_;
    std::map<std::string, std::string> config_;

    std::atomic<bool> running_;
    std::thread simulation_thread_;

    // Simulation parameters
    double data_loss_rate_;
    int additional_latency_;
    int data_rate_ms_ = 1000;
    int base_code_ = 1000;
    int message_counter_;

    // Data injection
    std::queue<std::string> injected_data_queue_;

    // Callback for generated data
    std::function<void(const TelemetryData&)> data_callback_;
};

// MockUAV implementation
MockUAV::MockUAV() : pImpl(std::make_unique<Impl>()) {}

MockUAV::~MockUAV() = default;

bool MockUAV::createMockUAV(const std::string& name, const std::map<std::string, std::string>& config) {
    return pImpl->createMockUAV(name, config);
}

bool MockUAV::simulateDataLoss(double loss_percentage) {
    return pImpl->simulateDataLoss(loss_percentage);
}

bool MockUAV::simulateLatency(int additional_ms) {
    return pImpl->simulateLatency(additional_ms);
}

bool MockUAV::injectTestData(const std::string& test_data) {
    return pImpl->injectTestData(test_data);
}

bool MockUAV::start() {
    return pImpl->start();
}

bool MockUAV::stop() {
    return pImpl->stop();
}

bool MockUAV::isRunning() const {
    return pImpl->isRunning();
}

} // namespace TelemetryAPI

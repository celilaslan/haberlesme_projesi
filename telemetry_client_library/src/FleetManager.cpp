/**
 * @file FleetManager.cpp
 * @brief Implementation of the FleetManager class
 *
 * Multi-UAV fleet management and coordination.
 */

#include <chrono>
#include <future>
#include <map>
#include <mutex>
#include <thread>

#include "TelemetryClient.h"

namespace TelemetryAPI {

    /**
     * @class FleetManager::Impl
     * @brief Private implementation for FleetManager
     */
    class FleetManager::Impl {
       public:
        Impl() : client_(nullptr) {}

        bool initialize(TelemetryClient* client) {
            if (!client) {
                return false;
            }

            client_ = client;

            // Initialize fleet monitoring
            startFleetMonitoring();

            return true;
        }

        bool broadcastCommand(const std::vector<std::string>& uav_names, const std::string& command) {
            if (!client_) {
                return false;
            }

            bool all_successful = true;
            std::vector<std::future<bool>> futures;

            // Send commands in parallel
            for (const auto& uav_name : uav_names) {
                auto future = std::async(std::launch::async, [this, uav_name, command]() {
                    return client_->sendCommand(uav_name, command, "FleetManager");
                });
                futures.push_back(std::move(future));
            }

            // Wait for all commands to complete
            for (auto& future : futures) {
                try {
                    if (!future.get()) {
                        all_successful = false;
                    }
                } catch (const std::exception&) {
                    all_successful = false;
                }
            }

            // Update last command for each UAV
            std::lock_guard<std::mutex> lock(fleet_mutex_);
            for (const auto& uav_name : uav_names) {
                auto it = fleet_status_.uav_statuses.find(uav_name);
                if (it != fleet_status_.uav_statuses.end()) {
                    it->second.last_command = command;
                    it->second.last_command_status = all_successful ? CommandStatus::SENT : CommandStatus::FAILED;
                }
            }

            return all_successful;
        }

        FleetStatus getFleetStatus() {
            std::lock_guard<std::mutex> lock(fleet_mutex_);

            // Update active UAV count and overall health
            fleet_status_.active_uavs = 0;
            double total_health = 0.0;

            auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count();

            for (auto& [uav_name, status] : fleet_status_.uav_statuses) {
                // Update connection status based on last seen time
                uint64_t time_since_last_seen = now - status.last_seen;
                status.connected = (time_since_last_seen < 30000);  // 30 seconds timeout

                if (status.connected) {
                    fleet_status_.active_uavs++;
                }

                // Calculate health score based on various factors
                double health = calculateUAVHealth(status, time_since_last_seen);
                status.health_score = health;
                total_health += health;
            }

            fleet_status_.total_uavs = fleet_status_.uav_statuses.size();
            fleet_status_.overall_health_score =
                fleet_status_.total_uavs > 0 ? total_health / fleet_status_.total_uavs : 0.0;
            fleet_status_.last_update = now;

            return fleet_status_;
        }

        bool executeCoordinatedCommand(const std::map<std::string, std::string>& uav_commands) {
            if (!client_) {
                return false;
            }

            bool all_successful = true;
            std::vector<std::future<bool>> futures;

            // Send all commands simultaneously
            for (const auto& [uav_name, command] : uav_commands) {
                auto future = std::async(std::launch::async, [this, uav_name, command]() {
                    return client_->sendCommand(uav_name, command, "FleetManager-Coordinated");
                });
                futures.push_back(std::move(future));
            }

            // Wait for all commands to complete
            for (auto& future : futures) {
                try {
                    if (!future.get()) {
                        all_successful = false;
                    }
                } catch (const std::exception&) {
                    all_successful = false;
                }
            }

            // Update command status for all UAVs
            std::lock_guard<std::mutex> lock(fleet_mutex_);
            for (const auto& [uav_name, command] : uav_commands) {
                auto it = fleet_status_.uav_statuses.find(uav_name);
                if (it != fleet_status_.uav_statuses.end()) {
                    it->second.last_command = command;
                    it->second.last_command_status = all_successful ? CommandStatus::SENT : CommandStatus::FAILED;
                }
            }

            return all_successful;
        }

        bool addUAV(const std::string& uav_name) {
            std::lock_guard<std::mutex> lock(fleet_mutex_);

            if (fleet_status_.uav_statuses.find(uav_name) != fleet_status_.uav_statuses.end()) {
                return false;  // Already exists
            }

            UAVStatus status;
            status.name = uav_name;
            status.connected = false;
            status.last_seen = 0;
            status.health_score = 0.0;
            status.last_command_status = CommandStatus::SENT;

            fleet_status_.uav_statuses[uav_name] = status;
            return true;
        }

        bool removeUAV(const std::string& uav_name) {
            std::lock_guard<std::mutex> lock(fleet_mutex_);

            auto it = fleet_status_.uav_statuses.find(uav_name);
            if (it == fleet_status_.uav_statuses.end()) {
                return false;  // Doesn't exist
            }

            fleet_status_.uav_statuses.erase(it);
            return true;
        }

        void updateUAVLastSeen(const std::string& uav_name) {
            std::lock_guard<std::mutex> lock(fleet_mutex_);

            auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count();

            auto it = fleet_status_.uav_statuses.find(uav_name);
            if (it != fleet_status_.uav_statuses.end()) {
                it->second.last_seen = now;
                it->second.connected = true;
            } else {
                // Auto-add new UAVs
                addUAV(uav_name);
                fleet_status_.uav_statuses[uav_name].last_seen = now;
                fleet_status_.uav_statuses[uav_name].connected = true;
            }
        }

       private:
        void startFleetMonitoring() {
            // This would typically register callbacks with the TelemetryClient
            // to receive notifications about UAV activity
            monitoring_active_ = true;
        }

        double calculateUAVHealth(const UAVStatus& status, uint64_t time_since_last_seen) {
            double health = 1.0;

            // Connection health (50% weight)
            if (!status.connected) {
                health *= 0.5;
            } else if (time_since_last_seen > 10000) {  // > 10 seconds
                health *= 0.8;
            } else if (time_since_last_seen > 5000) {  // > 5 seconds
                health *= 0.9;
            }

            // Data quality health (30% weight)
            double data_quality_factor =
                status.data_quality.data_freshness_score * 0.7 + (1.0 - status.data_quality.packet_loss_rate) * 0.3;
            health *= (0.7 + data_quality_factor * 0.3);

            // Command execution health (20% weight)
            double command_factor = 1.0;
            switch (status.last_command_status) {
                case CommandStatus::EXECUTED:
                    command_factor = 1.0;
                    break;
                case CommandStatus::ACKNOWLEDGED:
                    command_factor = 0.9;
                    break;
                case CommandStatus::SENT:
                    command_factor = 0.8;
                    break;
                case CommandStatus::TIMEOUT:
                    command_factor = 0.5;
                    break;
                case CommandStatus::FAILED:
                    command_factor = 0.3;
                    break;
            }
            health *= (0.8 + command_factor * 0.2);

            return std::max(0.0, std::min(1.0, health));
        }

        TelemetryClient* client_;
        mutable std::mutex fleet_mutex_;
        FleetStatus fleet_status_;
        bool monitoring_active_ = false;
    };

    // FleetManager implementation
    FleetManager::FleetManager() : pImpl(std::make_unique<Impl>()) {}

    FleetManager::~FleetManager() = default;

    bool FleetManager::initialize(TelemetryClient* client) { return pImpl->initialize(client); }

    bool FleetManager::broadcastCommand(const std::vector<std::string>& uav_names, const std::string& command) {
        return pImpl->broadcastCommand(uav_names, command);
    }

    FleetStatus FleetManager::getFleetStatus() { return pImpl->getFleetStatus(); }

    bool FleetManager::executeCoordinatedCommand(const std::map<std::string, std::string>& uav_commands) {
        return pImpl->executeCoordinatedCommand(uav_commands);
    }

    bool FleetManager::addUAV(const std::string& uav_name) { return pImpl->addUAV(uav_name); }

    bool FleetManager::removeUAV(const std::string& uav_name) { return pImpl->removeUAV(uav_name); }

}  // namespace TelemetryAPI

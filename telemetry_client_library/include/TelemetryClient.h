/**
 * @file TelemetryClient.h
 * @brief Simple C++ API for communicating with the telemetry service
 *
 * This header defines a clean, easy-to-use interface for developers who want
 * to communicate with the telemetry service without dealing with TCP or
 * UDP networking details directly.
 */

#ifndef TELEMETRY_CLIENT_H
#define TELEMETRY_CLIENT_H

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

// Export symbols for shared library
#ifdef _WIN32
#ifdef TELEMETRY_CLIENT_EXPORTS
#define TELEMETRY_API __declspec(dllexport)
#else
#define TELEMETRY_API __declspec(dllimport)
#endif
#else
#ifdef TELEMETRY_CLIENT_EXPORTS
#define TELEMETRY_API __attribute__((visibility("default")))
#else
#define TELEMETRY_API
#endif
#endif

namespace TelemetryAPI {

    /**
     * @enum Protocol
     * @brief Communication protocol options
     */
    enum class Protocol : std::uint8_t {
        TCP,  ///< Use TCP for reliable communication
        UDP,  ///< Use UDP for low-latency communication
        BOTH  ///< Use both protocols
    };

    /**
     * @enum DataType
     * @brief Type of telemetry data
     */
    enum class DataType : std::uint8_t {
        MAPPING,  ///< Mapping/navigation data
        CAMERA,   ///< Camera/vision data
        UNKNOWN   ///< Unknown or mixed data
    };

    /**
     * @enum ClientState
     * @brief Client lifecycle states for robust state management
     */
    enum class ClientState : std::uint8_t {
        IDLE,         ///< Client created but not initialized
        INITIALIZED,  ///< Client initialized, ready to start receiving
        RUNNING,      ///< Client actively receiving telemetry data
        STOPPED,      ///< Client stopped, can be restarted
        ERROR         ///< Client in error state, requires reinitialization
    };

    /**
     * @struct TelemetryData
     * @brief Structure representing received telemetry data
     */
    struct TELEMETRY_API TelemetryData {
        std::string uav_name;                        ///< UAV identifier (e.g., "UAV_1")
        DataType data_type = DataType::UNKNOWN;      ///< Type of data (mapping, camera, etc.)
        std::string raw_data;                        ///< Raw telemetry data
        std::string topic;                           ///< Topic name used for routing
        Protocol received_via = Protocol::TCP;       ///< Protocol used to receive this data
        uint64_t timestamp_ms = 0;                   ///< Timestamp when data was received (milliseconds since epoch)
    };

    /**
     * @typedef TelemetryCallback
     * @brief Callback function type for receiving telemetry data
     * @param data The received telemetry data
     */
    using TelemetryCallback = std::function<void(const TelemetryData& data)>;

    /**
     * @typedef ErrorCallback
     * @brief Callback function type for error notifications
     * @param error_message Description of the error
     */
    using ErrorCallback = std::function<void(const std::string& error_message)>;

    /**
     * @class TelemetryClient
     * @brief Main client class for telemetry communication
     *
     * This class provides a simple interface for:
     * - Subscribing to telemetry data from UAVs
     * - Sending commands to UAVs via the telemetry service
     * - Managing connections and protocols automatically
     */
    class TELEMETRY_API TelemetryClient {
       public:
        /**
         * @brief Constructor
         */
        TelemetryClient();

        /**
         * @brief Destructor - ensures clean shutdown
         */
        ~TelemetryClient();

        /**
         * @brief Initialize the client with service connection details
         * @param service_host Hostname or IP of the telemetry service
         * @param config_file Path to service configuration file
         * @return true if initialization succeeded, false otherwise
         *
         * This method loads the service configuration and prepares for communication.
         * If config_file is not provided, it will look for "service_config.json" in
         * the current directory or use environment variables.
         */
        bool initialize(const std::string& service_host = "localhost", const std::string& config_file = "");

        /**
         * @brief Start receiving telemetry data
         * @param protocol Communication protocol to use
         * @param callback Function to call when telemetry data is received
         * @param error_callback Function to call when errors occur (optional)
         * @return true if successfully started, false otherwise
         *
         * This method starts background threads to receive telemetry data.
         * The callback will be invoked for each received telemetry message.
         */
        bool startReceiving(Protocol protocol, TelemetryCallback callback, ErrorCallback error_callback = nullptr);

        /**
         * @brief Subscribe to specific UAV data
         * @param uav_name Name of the UAV to subscribe to (e.g., "UAV_1")
         * @param data_type Type of data to receive (MAPPING, CAMERA, or both if not specified)
         * @return true if subscription succeeded, false otherwise
         *
         * By default, the client receives data from all UAVs. Use this method
         * to filter data from specific UAVs or data types.
         */
        bool subscribeToUAV(const std::string& uav_name, DataType data_type = DataType::UNKNOWN);

        /**
         * @brief Subscribe to specific data type from all UAVs
         * @param data_type Type of data to receive (MAPPING or CAMERA)
         * @return true if subscription succeeded, false otherwise
         */
        bool subscribeToDataType(DataType data_type);

        /**
         * @brief Unsubscribe from specific UAV data
         * @param uav_name Name of the UAV to unsubscribe from (e.g., "UAV_1")
         * @param data_type Type of data to stop receiving (MAPPING, CAMERA, or both if not specified)
         * @return true if unsubscription succeeded, false otherwise
         *
         * This method removes subscriptions for the specified UAV and data type,
         * reducing network traffic and processing overhead.
         */
        bool unsubscribeFromUAV(const std::string& uav_name, DataType data_type = DataType::UNKNOWN);

        /**
         * @brief Unsubscribe from specific data type from all UAVs
         * @param data_type Type of data to stop receiving (MAPPING or CAMERA)
         * @return true if unsubscription succeeded, false otherwise
         */
        bool unsubscribeFromDataType(DataType data_type);

        /**
         * @brief Clear all subscriptions
         * @return true if successful
         *
         * This method removes all active subscriptions, effectively stopping
         * all filtered data reception. The client will still receive data
         * but no filtering will be applied.
         */
        bool clearAllSubscriptions();

        /**
         * @brief Send a command to a specific UAV
         * @param uav_name Target UAV name (e.g., "UAV_1")
         * @param command Command string to send
         * @param client_name Identifier for the client sending the command (e.g., "MyApp")
         * @return true if command was sent successfully, false otherwise
         *
         * Commands are always sent via TCP for reliability and security.
         * The telemetry service will route the command to the specified UAV.
         */
        bool sendCommand(const std::string& uav_name, const std::string& command,
                         const std::string& client_name = "TelemetryClient");

        /**
         * @brief Stop receiving telemetry data
         *
         * This method stops all background threads and cleans up resources.
         * The client can be restarted by calling startReceiving() again.
         */
        void stopReceiving();

        /**
         * @brief Check if the client is currently receiving data
         * @return true if actively receiving, false otherwise
         */
        [[nodiscard]] bool isReceiving() const;

        /**
         * @brief Get current client state
         * @return Current lifecycle state of the client
         */
        [[nodiscard]] ClientState getCurrentState() const;

        /**
         * @brief Get human-readable description of current state
         * @return String describing the current client state
         */
        [[nodiscard]] std::string getStateDescription() const;

        /**
         * @brief Reset client to IDLE state (for error recovery)
         * @return true if reset successful, false otherwise
         */
        bool resetClient();

        /**
         * @brief Get list of available UAVs from the service configuration
         * @return Vector of UAV names
         */
        [[nodiscard]] std::vector<std::string> getAvailableUAVs() const;

        /**
         * @brief Get connection status information
         * @return Human-readable string describing connection status
         */
        [[nodiscard]] std::string getConnectionStatus() const;

        /**
         * @brief Enable or disable debug logging
         * @param enable true to enable debug output, false to disable
         *
         * When enabled, the client will print detailed information about
         * network operations, received messages, and errors to stdout/stderr.
         */
        void setDebugMode(bool enable);

        /**
         * @brief Get the last error message
         * @return String containing the last error that occurred
         */
        [[nodiscard]] std::string getLastError() const;

        // Disable copy and move operations for PIMPL pattern
        TelemetryClient(const TelemetryClient&) = delete;
        TelemetryClient& operator=(const TelemetryClient&) = delete;
        TelemetryClient(TelemetryClient&&) = delete;
        TelemetryClient& operator=(TelemetryClient&&) = delete;

       private:
        // Forward declaration of implementation class (PIMPL pattern)
        class Impl;
        std::unique_ptr<Impl> pImpl;
    };

}  // namespace TelemetryAPI

#endif  // TELEMETRY_CLIENT_H

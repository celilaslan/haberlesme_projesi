/**
 * @file TelemetryClient.h
 * @brief Simple C++ API for communicating with the telemetry service
 *
 * This header defines a clean, easy-to-use interface for developers who want
 * to communicate with the telemetry service without dealing with ZeroMQ or
 * Boost.Asio networking details directly.
 */

#ifndef TELEMETRY_CLIENT_H
#define TELEMETRY_CLIENT_H

#include <string>
#include <functional>
#include <memory>
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
enum class Protocol {
    TCP_ONLY,    ///< Use TCP (ZeroMQ) for reliable communication
    UDP_ONLY,    ///< Use UDP for low-latency communication
    BOTH         ///< Use both protocols (recommended)
};

/**
 * @enum DataType
 * @brief Type of telemetry data
 */
enum class DataType {
    MAPPING,     ///< Mapping/navigation data
    CAMERA,      ///< Camera/vision data
    UNKNOWN      ///< Unknown or mixed data
};

/**
 * @struct TelemetryData
 * @brief Structure representing received telemetry data
 */
struct TELEMETRY_API TelemetryData {
    std::string uav_name;        ///< UAV identifier (e.g., "UAV_1")
    DataType data_type;          ///< Type of data (mapping, camera, etc.)
    std::string raw_data;        ///< Raw telemetry data
    std::string topic;           ///< Topic name used for routing
    Protocol received_via;       ///< Protocol used to receive this data
    uint64_t timestamp_ms;       ///< Timestamp when data was received (milliseconds since epoch)
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
     * @param service_host Hostname or IP of the telemetry service (default: "localhost")
     * @param config_file Path to service configuration file (optional)
     * @return true if initialization succeeded, false otherwise
     *
     * This method loads the service configuration and prepares for communication.
     * If config_file is not provided, it will look for "service_config.json" in
     * the current directory or use environment variables.
     */
    bool initialize(const std::string& service_host = "localhost",
                   const std::string& config_file = "");

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
    bool startReceiving(Protocol protocol,
                       TelemetryCallback callback,
                       ErrorCallback error_callback = nullptr);

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
     * @brief Send a command to a specific UAV
     * @param uav_name Target UAV name (e.g., "UAV_1")
     * @param command Command string to send
     * @param client_name Identifier for the client sending the command (e.g., "MyApp")
     * @return true if command was sent successfully, false otherwise
     *
     * Commands are always sent via TCP for reliability and security.
     * The telemetry service will route the command to the specified UAV.
     */
    bool sendCommand(const std::string& uav_name,
                    const std::string& command,
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
    bool isReceiving() const;

    /**
     * @brief Get list of available UAVs from the service configuration
     * @return Vector of UAV names
     */
    std::vector<std::string> getAvailableUAVs() const;

    /**
     * @brief Get connection status information
     * @return Human-readable string describing connection status
     */
    std::string getConnectionStatus() const;

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
    std::string getLastError() const;

private:
    // Forward declaration of implementation class (PIMPL pattern)
    class Impl;
    std::unique_ptr<Impl> pImpl;

    // Disable copy constructor and assignment operator
    TelemetryClient(const TelemetryClient&) = delete;
    TelemetryClient& operator=(const TelemetryClient&) = delete;
};

/**
 * @brief Get library version information
 * @return Version string (e.g., "1.0.0")
 */
TELEMETRY_API std::string getLibraryVersion();

/**
 * @brief Check if the telemetry service is reachable
 * @param service_host Hostname or IP of the service
 * @param tcp_port TCP port to test (from service configuration)
 * @param timeout_ms Timeout in milliseconds (default: 5000)
 * @return true if service is reachable, false otherwise
 *
 * This is a utility function to test connectivity before initializing
 * the full client.
 */
TELEMETRY_API bool testServiceConnection(const std::string& service_host = "localhost",
                                        int tcp_port = 5557,
                                        int timeout_ms = 5000);

/**
 * @brief Parse a raw telemetry message
 * @param raw_message The raw telemetry string (e.g., "UAV_1  1001")
 * @param uav_name Output parameter for extracted UAV name
 * @param numeric_code Output parameter for extracted numeric code
 * @return true if parsing succeeded, false otherwise
 *
 * Utility function to parse the standard telemetry message format used
 * by the UAV simulators.
 */
TELEMETRY_API bool parseTelemetryMessage(const std::string& raw_message,
                                        std::string& uav_name,
                                        int& numeric_code);

} // namespace TelemetryAPI

#endif // TELEMETRY_CLIENT_H

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

// Forward declarations to hide implementation details
class TelemetryClientImpl;

/**
 * @brief Protocol types supported by the telemetry service
 */
enum class Protocol {
    TCP,  ///< TCP using ZeroMQ - reliable, supports commands
    UDP   ///< UDP using Boost.Asio - low latency, telemetry only
};

/**
 * @brief Telemetry packet header structure (same as service)
 */
#pragma pack(push, 1)
struct PacketHeader {
    uint8_t targetID;    ///< Target ID (1: Camera, 2: Mapping)
    uint8_t packetType;  ///< Packet type (4: Location, 5: Status, 6: IMU, 7: Battery)
};
#pragma pack(pop)

/**
 * @brief Target IDs for telemetry routing (must match TelemetryPackets.h)
 */
enum TargetIDs : uint8_t {
    CAMERA = 1,
    MAPPING = 2
};

/**
 * @brief Packet types for different telemetry data (must match TelemetryPackets.h)
 */
enum PacketTypes : uint8_t {
    LOCATION = 4,    // Changed from LOCATION_PACKET to match service
    STATUS = 5,      // Changed from STATUS_PACKET to match service
    IMU_PACKET = 6,
    BATTERY_PACKET = 7
};

/**
 * @brief Callback function type for receiving telemetry data
 * @param topic The topic the data was published on (e.g., "telemetry.UAV_1.camera.location")
 * @param data The raw binary telemetry data
 */
using TelemetryCallback = std::function<void(const std::string& topic, const std::vector<uint8_t>& data)>;

/**
 * @brief Callback function type for connection status changes
 * @param connected True if connected, false if disconnected
 * @param error_message Error message if disconnected due to error (empty if normal disconnect)
 */
using ConnectionCallback = std::function<void(bool connected, const std::string& error_message)>;

/**
 * @brief Simple telemetry client for UI applications
 *
 * This class provides a simplified interface for connecting to the telemetry service
 * and receiving telemetry data. It handles all the networking complexity internally.
 *
 * Example usage:
 * ```cpp
 * TelemetryClient client("my_camera_ui");
 * client.setTelemetryCallback([](const std::string& topic, const std::vector<uint8_t>& data) {
 *     std::cout << "Received " << data.size() << " bytes on topic: " << topic << std::endl;
 * });
 *
 * if (client.connect("localhost", 5555, Protocol::TCP)) {
 *     client.subscribe("telemetry.*.camera.*");  // Subscribe to all camera data
 *     // ... your application logic ...
 *     client.disconnect();
 * }
 * ```
 */
class TELEMETRY_API TelemetryClient {
public:
    /**
     * @brief Constructor
     * @param client_id Unique identifier for this client (e.g., "camera_ui", "mapping_ui")
     */
    explicit TelemetryClient(const std::string& client_id);

    /**
     * @brief Destructor - automatically disconnects if connected
     */
    ~TelemetryClient();

    // Rule of 5: Disable copy/move for resource management safety
    TelemetryClient(const TelemetryClient&) = delete;
    TelemetryClient& operator=(const TelemetryClient&) = delete;
    TelemetryClient(TelemetryClient&&) = delete;
    TelemetryClient& operator=(TelemetryClient&&) = delete;

    /**
     * @brief Connect to the telemetry service
     * @param host Hostname or IP address of the service (e.g., "localhost", "192.168.1.100")
     * @param port Port number to connect to
     * @param protocol Protocol to use (TCP or UDP)
     * @return True if connection successful, false otherwise
     *
     * For TCP: Connects to the service's subscriber port
     * For UDP: Sets up UDP socket for receiving published data
     */
    bool connect(const std::string& host, int port, Protocol protocol = Protocol::TCP);

    /**
     * @brief Connect using configuration file
     * @param config_file Path to service_config.json file
     * @param protocol Protocol to use (TCP or UDP)
     * @return True if connection successful, false otherwise
     *
     * Automatically reads the UI ports from the configuration file
     */
    bool connectFromConfig(const std::string& config_file = "service_config.json", Protocol protocol = Protocol::TCP);

    /**
     * @brief Disconnect from the service
     *
     * Stops all background threads and closes network connections.
     * Safe to call multiple times.
     */
    void disconnect();

    /**
     * @brief Check if currently connected
     * @return True if connected, false otherwise
     */
    bool isConnected() const;

    /**
     * @brief Subscribe to a telemetry topic
     * @param topic Topic pattern to subscribe to (supports wildcards with '*')
     * @return True if subscription successful, false otherwise
     *
     * Examples:
     * - "telemetry.*" - All telemetry data
     * - "telemetry.UAV_1.*" - All data from UAV_1
     * - "telemetry.*.camera.*" - All camera data from all UAVs
     * - "telemetry.UAV_1.camera.location" - Specific data type
     */
    bool subscribe(const std::string& topic);

    /**
     * @brief Unsubscribe from a telemetry topic
     * @param topic Topic pattern to unsubscribe from
     * @return True if unsubscription successful, false otherwise
     */
    bool unsubscribe(const std::string& topic);

    /**
     * @brief Send a command to a UAV (TCP only)
     * @param uav_name Name of the UAV to send command to (e.g., "UAV_1")
     * @param command Command string to send
     * @return True if command sent successfully, false otherwise
     *
     * Only works with TCP protocol. Commands are forwarded by the service to the UAV.
     */
    bool sendCommand(const std::string& uav_name, const std::string& command);

    /**
     * @brief Set callback for receiving telemetry data
     * @param callback Function to call when telemetry data is received
     *
     * The callback will be called from a background thread, so ensure thread safety.
     */
    void setTelemetryCallback(TelemetryCallback callback);

    /**
     * @brief Set callback for connection status changes
     * @param callback Function to call when connection status changes
     */
    void setConnectionCallback(ConnectionCallback callback);

    /**
     * @brief Get the client ID
     * @return The unique client identifier
     */
    const std::string& getClientId() const;

    /**
     * @brief Get current protocol
     * @return The protocol currently in use
     */
    Protocol getProtocol() const;

    /**
     * @brief Utility function to parse packet header from telemetry data
     * @param data Raw telemetry data
     * @return Packet header if data is large enough, nullptr otherwise
     *
     * Helper function to extract packet information from received telemetry data.
     */
    static const PacketHeader* parseHeader(const std::vector<uint8_t>& data);

    /**
     * @brief Utility function to get target name from ID
     * @param targetId Target ID from packet header
     * @return Human-readable target name
     */
    static std::string getTargetName(uint8_t targetId);

    /**
     * @brief Utility function to get packet type name from ID
     * @param packetType Packet type ID from packet header
     * @return Human-readable packet type name
     */
    static std::string getPacketTypeName(uint8_t packetType);

private:
    std::unique_ptr<TelemetryClientImpl> impl_;  ///< PIMPL to hide implementation details
};

}  // namespace TelemetryAPI

#endif  // TELEMETRY_CLIENT_H

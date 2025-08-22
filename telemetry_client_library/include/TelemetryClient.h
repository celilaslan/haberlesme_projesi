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
        TCP_ONLY,  ///< Use TCP for reliable communication
        UDP_ONLY,  ///< Use UDP for low-latency communication
        BOTH       ///< Use both protocols (recommended)
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
        Protocol received_via = Protocol::TCP_ONLY;  ///< Protocol used to receive this data
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
         * @param service_host Hostname or IP of the telemetry service (default: "localhost")
         * @param config_file Path to service configuration file (optional)
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

    // ============================================================================
    // ADVANCED API ENHANCEMENTS
    // ============================================================================

    /**
     * @enum CommandStatus
     * @brief Status of a command execution
     */
    enum class CommandStatus : std::uint8_t {
        SENT,          ///< Command has been sent
        ACKNOWLEDGED,  ///< Command acknowledged by UAV
        EXECUTED,      ///< Command successfully executed
        FAILED,        ///< Command execution failed
        TIMEOUT        ///< Command timed out
    };

    /**
     * @enum StreamMode
     * @brief Data streaming modes
     */
    enum class StreamMode : std::uint8_t {
        REALTIME,  ///< Immediate delivery, may drop packets
        RELIABLE,  ///< Guaranteed delivery with buffering
        ADAPTIVE   ///< Automatically adjust based on network conditions
    };

    /**
     * @enum OperationMode
     * @brief Operation modes for different scenarios
     */
    enum class OperationMode : std::uint8_t {
        DEVELOPMENT,   ///< Full logging, relaxed timeouts
        PRODUCTION,    ///< Optimized performance
        EMERGENCY,     ///< Maximum reliability
        LOW_BANDWIDTH  ///< Minimal data transfer
    };

    /**
     * @enum Permission
     * @brief User permission levels
     */
    enum class Permission : std::uint8_t {
        READ_ONLY,          ///< Can only receive telemetry
        BASIC_COMMANDS,     ///< takeoff, land, status
        ADVANCED_COMMANDS,  ///< navigation, system config
        ADMIN               ///< full control
    };

    /**
     * @enum TelemetryEvent
     * @brief Types of telemetry events
     */
    enum class TelemetryEvent : std::uint8_t {
        UAV_CONNECTED,          ///< UAV has connected
        UAV_DISCONNECTED,       ///< UAV has disconnected
        DATA_QUALITY_DEGRADED,  ///< Data quality issues detected
        COMMAND_FAILED,         ///< Command execution failed
        NETWORK_ISSUES,         ///< Network connectivity problems
        EMERGENCY_STATUS        ///< Emergency situation detected
    };

    /**
     * @enum DataFormat
     * @brief Supported data formats
     */
    enum class DataFormat : std::uint8_t {
        JSON,      ///< JSON format
        PROTOBUF,  ///< Protocol Buffers
        MSGPACK,   ///< MessagePack
        CUSTOM     ///< Custom format
    };

    /**
     * @enum CompressionType
     * @brief Compression algorithms
     */
    enum class CompressionType : std::uint8_t {
        NONE,  ///< No compression
        GZIP,  ///< GZIP compression
        LZ4,   ///< LZ4 compression
        ZSTD   ///< Zstandard compression
    };

    /**
     * @struct CommandResponse
     * @brief Response from a command execution
     */
    struct TELEMETRY_API CommandResponse {
        std::string command_id;                      ///< Unique command identifier
        bool acknowledged = false;                   ///< Whether command was acknowledged
        std::string response_data;                   ///< Response data from UAV
        uint64_t response_time_ms = 0;               ///< Time taken for response
        CommandStatus status = CommandStatus::SENT;  ///< Current status of command
        std::string error_message;                   ///< Error message if failed
    };

    /**
     * @struct DataQuality
     * @brief Data quality metrics
     */
    struct TELEMETRY_API DataQuality {
        double packet_loss_rate = 0.0;      ///< Packet loss percentage (0.0-1.0)
        double average_latency_ms = 0.0;    ///< Average latency in milliseconds
        int missing_sequences = 0;          ///< Number of missing sequence numbers
        double data_freshness_score = 1.0;  ///< Data freshness score (0.0-1.0)
        uint64_t last_update_time = 0;      ///< Timestamp of last update
    };

    /**
     * @struct BandwidthStats
     * @brief Bandwidth usage statistics
     */
    struct TELEMETRY_API BandwidthStats {
        double bytes_per_second_in = 0.0;   ///< Incoming bytes per second
        double bytes_per_second_out = 0.0;  ///< Outgoing bytes per second
        double peak_bandwidth_in = 0.0;     ///< Peak incoming bandwidth
        double peak_bandwidth_out = 0.0;    ///< Peak outgoing bandwidth
        uint64_t total_bytes_received = 0;  ///< Total bytes received
        uint64_t total_bytes_sent = 0;      ///< Total bytes sent
    };

    /**
     * @struct UAVStatus
     * @brief Status of a single UAV
     */
    struct TELEMETRY_API UAVStatus {
        std::string name;                                         ///< UAV name
        bool connected = false;                                   ///< Connection status
        uint64_t last_seen = 0;                                   ///< Last seen timestamp
        DataQuality data_quality;                                 ///< Data quality metrics
        double health_score = 1.0;                                ///< Overall health score (0.0-1.0)
        std::string last_command;                                 ///< Last command sent
        CommandStatus last_command_status = CommandStatus::SENT;  ///< Status of last command
    };

    /**
     * @struct FleetStatus
     * @brief Status of entire UAV fleet
     */
    struct TELEMETRY_API FleetStatus {
        std::map<std::string, UAVStatus> uav_statuses;  ///< Individual UAV statuses
        int active_uavs = 0;                            ///< Number of active UAVs
        int total_uavs = 0;                             ///< Total number of UAVs
        double overall_health_score = 1.0;              ///< Overall fleet health (0.0-1.0)
        uint64_t last_update = 0;                       ///< Last update timestamp
    };

    /**
     * @struct NetworkStats
     * @brief Network performance statistics
     */
    struct TELEMETRY_API NetworkStats {
        double latency_ms;           ///< Current latency
        double jitter_ms;            ///< Network jitter
        double packet_loss_percent;  ///< Packet loss percentage
        int reconnection_count;      ///< Number of reconnections
        bool is_primary_connection;  ///< Using primary or backup connection
    };

    /**
     * @struct AuthConfig
     * @brief Authentication configuration
     */
    struct TELEMETRY_API AuthConfig {
        std::string username;           ///< Username
        std::string password;           ///< Password
        std::string certificate_path;   ///< Path to certificate file
        bool enable_encryption = true;  ///< Enable encryption
    };

    /**
     * @struct ProtocolSettings
     * @brief Protocol-specific settings
     */
    struct TELEMETRY_API ProtocolSettings {
        int tcp_keepalive_interval;             ///< TCP keepalive interval (seconds)
        int udp_max_packet_size;                ///< Maximum UDP packet size
        bool enable_compression;                ///< Enable data compression
        CompressionType compression_algorithm;  ///< Compression algorithm to use
    };

    /**
     * @struct ConfigurationProfile
     * @brief Current configuration profile
     */
    struct TELEMETRY_API ConfigurationProfile {
        OperationMode mode = OperationMode::PRODUCTION;  ///< Current operation mode
        std::map<std::string, std::string> settings;     ///< Configuration settings
        uint64_t last_modified = 0;                      ///< Last modification timestamp
    };

    /**
     * @struct PerformanceMetrics
     * @brief Performance monitoring metrics
     */
    struct TELEMETRY_API PerformanceMetrics {
        double cpu_usage_percent = 0.0;           ///< CPU usage percentage
        size_t memory_usage_mb = 0;               ///< Memory usage in MB
        int messages_per_second = 0;              ///< Messages processed per second
        double average_processing_time_ms = 0.0;  ///< Average message processing time
        uint64_t uptime_seconds = 0;              ///< Library uptime in seconds
    };

    // Forward declarations for advanced classes
    class DataAnalyzer;
    class FleetManager;
    class DataBuffer;
    class MockUAV;

    /**
     * @typedef EventCallback
     * @brief Callback function type for telemetry events
     */
    using EventCallback = std::function<void(TelemetryEvent event, const std::string& details)>;

    /**
     * @typedef CommandResponseCallback
     * @brief Callback function type for asynchronous command responses
     */
    using CommandResponseCallback = std::function<void(const CommandResponse& response)>;

    /**
     * @typedef AlertCallback
     * @brief Callback function type for custom alerts
     */
    using AlertCallback = std::function<void(const std::string& uav_name, const std::string& parameter, double value)>;

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
    TELEMETRY_API bool testServiceConnection(const std::string& service_host = "localhost", int tcp_port = 5557,
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
    TELEMETRY_API bool parseTelemetryMessage(const std::string& raw_message, std::string& uav_name, int& numeric_code);

    // ============================================================================
    // ADVANCED API CLASSES
    // ============================================================================

    /**
     * @class DataAnalyzer
     * @brief Advanced data analysis and quality monitoring
     */
    class TELEMETRY_API DataAnalyzer {
       public:
        /**
         * @brief Constructor
         */
        DataAnalyzer();

        /**
         * @brief Destructor
         */
        ~DataAnalyzer();

        // Disable copy and move operations for PIMPL pattern
        DataAnalyzer(const DataAnalyzer&) = delete;
        DataAnalyzer& operator=(const DataAnalyzer&) = delete;
        DataAnalyzer(DataAnalyzer&&) = delete;
        DataAnalyzer& operator=(DataAnalyzer&&) = delete;

        /**
         * @brief Get data quality metrics for specific UAV and data type
         * @param uav_name Name of the UAV
         * @param type Type of data (MAPPING, CAMERA, or UNKNOWN for both)
         * @return Data quality metrics
         */
        DataQuality getDataQuality(const std::string& uav_name, DataType type = DataType::UNKNOWN);

        /**
         * @brief Enable or disable data validation
         * @param enable true to enable validation, false to disable
         * @return true if successful
         */
        bool enableDataValidation(bool enable);

        /**
         * @brief Get historical telemetry data
         * @param uav_name Name of the UAV
         * @param start_time Start timestamp (milliseconds since epoch)
         * @param end_time End timestamp (milliseconds since epoch)
         * @return Vector of historical telemetry data
         */
        std::vector<TelemetryData> getHistoricalData(const std::string& uav_name, uint64_t start_time,
                                                     uint64_t end_time);

        /**
         * @brief Set data rate limit for a UAV
         * @param uav_name Name of the UAV
         * @param max_messages_per_second Maximum messages per second
         * @return true if successful
         */
        bool setDataRateLimit(const std::string& uav_name, int max_messages_per_second);

        /**
         * @brief Get bandwidth usage statistics
         * @return Current bandwidth statistics
         */
        BandwidthStats getBandwidthUsage();

       private:
        class Impl;
        std::unique_ptr<Impl> pImpl;
    };

    /**
     * @class FleetManager
     * @brief Multi-UAV fleet management and coordination
     */
    class TELEMETRY_API FleetManager {
       public:
        /**
         * @brief Constructor
         */
        FleetManager();

        /**
         * @brief Destructor
         */
        ~FleetManager();

        // Disable copy and move operations for PIMPL pattern
        FleetManager(const FleetManager&) = delete;
        FleetManager& operator=(const FleetManager&) = delete;
        FleetManager(FleetManager&&) = delete;
        FleetManager& operator=(FleetManager&&) = delete;

        /**
         * @brief Initialize fleet manager with telemetry client
         * @param client Pointer to initialized TelemetryClient
         * @return true if successful
         */
        bool initialize(TelemetryClient* client);

        /**
         * @brief Broadcast command to multiple UAVs
         * @param uav_names List of UAV names to send command to
         * @param command Command to broadcast
         * @return true if all commands were sent successfully
         */
        bool broadcastCommand(const std::vector<std::string>& uav_names, const std::string& command);

        /**
         * @brief Get fleet status
         * @return Current status of all UAVs in the fleet
         */
        FleetStatus getFleetStatus();

        /**
         * @brief Execute coordinated commands on multiple UAVs
         * @param uav_commands Map of UAV names to specific commands
         * @return true if all commands were sent successfully
         */
        bool executeCoordinatedCommand(const std::map<std::string, std::string>& uav_commands);

        /**
         * @brief Add UAV to fleet monitoring
         * @param uav_name Name of the UAV to add
         * @return true if successful
         */
        bool addUAV(const std::string& uav_name);

        /**
         * @brief Remove UAV from fleet monitoring
         * @param uav_name Name of the UAV to remove
         * @return true if successful
         */
        bool removeUAV(const std::string& uav_name);

       private:
        class Impl;
        std::unique_ptr<Impl> pImpl;
    };

    /**
     * @class DataBuffer
     * @brief Data buffering, recording, and replay functionality
     */
    class TELEMETRY_API DataBuffer {
       public:
        /**
         * @brief Constructor
         */
        DataBuffer();

        /**
         * @brief Destructor
         */
        ~DataBuffer();

        // Disable copy and move operations for PIMPL pattern
        DataBuffer(const DataBuffer&) = delete;
        DataBuffer& operator=(const DataBuffer&) = delete;
        DataBuffer(DataBuffer&&) = delete;
        DataBuffer& operator=(DataBuffer&&) = delete;

        /**
         * @brief Enable data buffering
         * @param max_buffer_size_mb Maximum buffer size in megabytes
         * @return true if successful
         */
        bool enableBuffering(size_t max_buffer_size_mb);

        /**
         * @brief Start recording telemetry data to file
         * @param filename Path to output file
         * @return true if recording started successfully
         */
        bool startRecording(const std::string& filename);

        /**
         * @brief Stop recording telemetry data
         * @return true if recording stopped successfully
         */
        bool stopRecording();

        /**
         * @brief Replay recorded telemetry data
         * @param filename Path to recorded data file
         * @param speed_multiplier Playback speed (1.0 = normal speed)
         * @return true if replay started successfully
         */
        bool replayData(const std::string& filename, double speed_multiplier = 1.0);

        /**
         * @brief Check if currently recording
         * @return true if recording is active
         */
        [[nodiscard]] bool isRecording() const;

        /**
         * @brief Check if currently replaying
         * @return true if replay is active
         */
        [[nodiscard]] bool isReplaying() const;

        /**
         * @brief Get current buffer usage
         * @return Buffer usage as percentage (0.0-1.0)
         */
        [[nodiscard]] double getBufferUsage() const;

       private:
        class Impl;
        std::unique_ptr<Impl> pImpl;
    };

    /**
     * @class MockUAV
     * @brief Mock UAV simulation for testing and development
     */
    class TELEMETRY_API MockUAV {
       public:
        /**
         * @brief Constructor
         */
        MockUAV();

        /**
         * @brief Destructor
         */
        ~MockUAV();

        // Disable copy and move operations for PIMPL pattern
        MockUAV(const MockUAV&) = delete;
        MockUAV& operator=(const MockUAV&) = delete;
        MockUAV(MockUAV&&) = delete;
        MockUAV& operator=(MockUAV&&) = delete;

        /**
         * @brief Create a mock UAV
         * @param name Name of the mock UAV
         * @param config Configuration for the mock UAV
         * @return true if successful
         */
        bool createMockUAV(const std::string& name, const std::map<std::string, std::string>& config);

        /**
         * @brief Simulate data loss
         * @param loss_percentage Percentage of data to drop (0.0-1.0)
         * @return true if successful
         */
        bool simulateDataLoss(double loss_percentage);

        /**
         * @brief Simulate network latency
         * @param additional_ms Additional latency in milliseconds
         * @return true if successful
         */
        bool simulateLatency(int additional_ms);

        /**
         * @brief Inject custom test data
         * @param test_data Custom data to inject
         * @return true if successful
         */
        bool injectTestData(const std::string& test_data);

        /**
         * @brief Start mock UAV operation
         * @return true if successful
         */
        bool start();

        /**
         * @brief Stop mock UAV operation
         * @return true if successful
         */
        bool stop();

        /**
         * @brief Check if mock UAV is running
         * @return true if running
         */
        [[nodiscard]] bool isRunning() const;

       private:
        class Impl;
        std::unique_ptr<Impl> pImpl;
    };

    // ============================================================================
    // ENHANCED TELEMETRY CLIENT WITH ADVANCED FEATURES
    // ============================================================================

    /**
     * @brief Enhanced TelemetryClient with advanced command support
     */
    class TELEMETRY_API TelemetryClientAdvanced : public TelemetryClient {
       public:
        /**
         * @brief Constructor
         */
        TelemetryClientAdvanced();

        /**
         * @brief Destructor
         */
        ~TelemetryClientAdvanced();

        // Disable copy and move operations
        TelemetryClientAdvanced(const TelemetryClientAdvanced&) = delete;
        TelemetryClientAdvanced& operator=(const TelemetryClientAdvanced&) = delete;
        TelemetryClientAdvanced(TelemetryClientAdvanced&&) = delete;
        TelemetryClientAdvanced& operator=(TelemetryClientAdvanced&&) = delete;

        // ========================================================================
        // COMMAND RESPONSE & ACKNOWLEDGMENT SYSTEM
        // ========================================================================

        /**
         * @brief Send command asynchronously with callback
         * @param uav_name Target UAV name
         * @param command Command to send
         * @param callback Callback function for response
         * @param timeout_ms Timeout in milliseconds
         * @return Command ID for tracking
         */
        std::string sendCommandAsync(const std::string& uav_name, const std::string& command,
                                     CommandResponseCallback callback, int timeout_ms = 5000);

        /**
         * @brief Send command synchronously and wait for response
         * @param uav_name Target UAV name
         * @param command Command to send
         * @param timeout_ms Timeout in milliseconds
         * @return Command response
         */
        CommandResponse sendCommandSync(const std::string& uav_name, const std::string& command, int timeout_ms = 5000);

        /**
         * @brief Get status of a specific command
         * @param command_id Command ID to check
         * @return Command response with current status
         */
        CommandResponse getCommandStatus(const std::string& command_id);

        // ========================================================================
        // STREAMING & BUFFERING
        // ========================================================================

        /**
         * @brief Set streaming mode
         * @param mode Streaming mode to use
         * @return true if successful
         */
        bool setStreamMode(StreamMode mode);

        /**
         * @brief Get current streaming mode
         * @return Current streaming mode
         */
        [[nodiscard]] StreamMode getStreamMode() const;

        // ========================================================================
        // NETWORK RESILIENCE & FAILOVER
        // ========================================================================

        /**
         * @brief Add backup service endpoint
         * @param backup_host Backup service hostname
         * @param priority Priority level (lower = higher priority)
         * @return true if successful
         */
        bool addBackupService(const std::string& backup_host, int priority = 1);

        /**
         * @brief Enable automatic failover
         * @param enable true to enable failover
         * @return true if successful
         */
        bool enableAutoFailover(bool enable);

        /**
         * @brief Get network statistics
         * @return Current network performance statistics
         */
        NetworkStats getNetworkStats();

        /**
         * @brief Set connection pool size
         * @param max_connections Maximum number of connections
         * @return true if successful
         */
        bool setConnectionPoolSize(int max_connections);

        // ========================================================================
        // EVENT SYSTEM
        // ========================================================================

        /**
         * @brief Subscribe to telemetry events
         * @param event Type of event to subscribe to
         * @param callback Callback function for events
         * @return true if successful
         */
        bool subscribeToEvents(TelemetryEvent event, EventCallback callback);

        /**
         * @brief Unsubscribe from telemetry events
         * @param event Type of event to unsubscribe from
         * @return true if successful
         */
        bool unsubscribeFromEvents(TelemetryEvent event);

        /**
         * @brief Set custom data threshold alert
         * @param uav_name UAV name to monitor
         * @param parameter Parameter to monitor
         * @param threshold Threshold value
         * @param alert_callback Callback when threshold is exceeded
         * @return true if successful
         */
        bool setDataThreshold(const std::string& uav_name, const std::string& parameter, double threshold,
                              AlertCallback alert_callback);

        // ========================================================================
        // SECURITY & AUTHENTICATION
        // ========================================================================

        /**
         * @brief Authenticate with service
         * @param config Authentication configuration
         * @return true if authentication successful
         */
        bool authenticate(const AuthConfig& config);

        /**
         * @brief Set encryption key
         * @param key Encryption key
         * @return true if successful
         */
        bool setEncryptionKey(const std::string& key);

        /**
         * @brief Set user permissions
         * @param level Permission level
         * @return true if successful
         */
        bool setUserPermissions(Permission level);

        /**
         * @brief Get current permission level
         * @return Current permission level
         */
        [[nodiscard]] Permission getUserPermissions() const;

        // ========================================================================
        // CONFIGURATION & PROFILES
        // ========================================================================

        /**
         * @brief Set operation mode
         * @param mode Operation mode to use
         * @return true if successful
         */
        bool setOperationMode(OperationMode mode);

        /**
         * @brief Get current operation mode
         * @return Current operation mode
         */
        [[nodiscard]] OperationMode getOperationMode() const;

        /**
         * @brief Update configuration setting
         * @param key Configuration key
         * @param value Configuration value
         * @return true if successful
         */
        bool updateConfiguration(const std::string& key, const std::string& value);

        /**
         * @brief Get current configuration profile
         * @return Current configuration profile
         */
        ConfigurationProfile getCurrentProfile();

        // ========================================================================
        // DATA FORMAT & PROTOCOL SETTINGS
        // ========================================================================

        /**
         * @brief Set data format
         * @param format Data format to use
         * @return true if successful
         */
        bool setDataFormat(DataFormat format);

        /**
         * @brief Get current data format
         * @return Current data format
         */
        [[nodiscard]] DataFormat getDataFormat() const;

        /**
         * @brief Set protocol-specific settings
         * @param protocol Protocol to configure
         * @param settings Protocol settings
         * @return true if successful
         */
        bool setProtocolSettings(Protocol protocol, const ProtocolSettings& settings);

        /**
         * @brief Get protocol settings
         * @param protocol Protocol to query
         * @return Current protocol settings
         */
        ProtocolSettings getProtocolSettings(Protocol protocol);

        // ========================================================================
        // PERFORMANCE MONITORING
        // ========================================================================

        /**
         * @brief Get performance metrics
         * @return Current performance metrics
         */
        PerformanceMetrics getPerformanceMetrics();

        /**
         * @brief Enable performance monitoring
         * @param enable true to enable monitoring
         * @return true if successful
         */
        bool enablePerformanceMonitoring(bool enable);

        // ========================================================================
        // ADVANCED COMPONENT ACCESS
        // ========================================================================

        /**
         * @brief Get data analyzer instance
         * @return Weak pointer to data analyzer (safe access, no ownership transfer)
         * @warning Check if the pointer is valid before use: if (auto ptr = getDataAnalyzer().lock()) { ... }
         */
        std::weak_ptr<DataAnalyzer> getDataAnalyzer();

        /**
         * @brief Get fleet manager instance
         * @return Weak pointer to fleet manager (safe access, no ownership transfer)
         * @warning Check if the pointer is valid before use: if (auto ptr = getFleetManager().lock()) { ... }
         */
        std::weak_ptr<FleetManager> getFleetManager();

        /**
         * @brief Get data buffer instance
         * @return Weak pointer to data buffer (safe access, no ownership transfer)
         * @warning Check if the pointer is valid before use: if (auto ptr = getDataBuffer().lock()) { ... }
         */
        std::weak_ptr<DataBuffer> getDataBuffer();

        /**
         * @brief Get mock UAV instance for testing
         * @return Weak pointer to mock UAV (safe access, no ownership transfer)
         * @warning Check if the pointer is valid before use: if (auto ptr = getMockUAV().lock()) { ... }
         */
        std::weak_ptr<MockUAV> getMockUAV();

       private:
        class AdvancedImpl;
        std::unique_ptr<AdvancedImpl> pAdvancedImpl;
    };

}  // namespace TelemetryAPI

#endif  // TELEMETRY_CLIENT_H

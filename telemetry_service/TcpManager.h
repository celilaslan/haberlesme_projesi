/**
 * @file TcpManager.h
 * @brief TCP communication manager for the telemetry service
 *
 * This file defines the TcpManager class which handles all TCP-based
 * communication with UAVs and UI components using ZeroMQ. It manages multiple socket
 * types and implements the publish-subscribe and push-pull patterns.
 */

#ifndef TCPMANAGER_H
#define TCPMANAGER_H

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>
#include <zmq.hpp>

#include "Config.h"

// Callback function type for handling incoming TCP messages
// Parameters: source description, binary message data
using TcpMessageCallback = std::function<void(const std::string&, const std::vector<uint8_t>&)>;

/**
 * @class TcpManager
 * @brief Manages all TCP communication for the telemetry service
 *
 * The TcpManager handles:
 * - Receiving telemetry data from UAVs (PULL sockets)
 * - Publishing telemetry data to UI components (PUB socket)
 * - Receiving commands from UI components (PULL socket)
 * - Forwarding commands to UAVs (PUSH sockets)
 *
 * Uses two background threads:
 * - Receiver thread: Handles incoming telemetry from UAVs
 * - Forwarder thread: Handles command forwarding from UI to UAVs
 */
class TcpManager {
   public:
    /**
     * @brief Constructor
     * @param ctx ZeroMQ context to use for all sockets
     * @param cfg Configuration containing UAV and UI port settings
     * @param callback Function to call when telemetry messages are received
     *
     * Initializes the TcpManager with the necessary configuration and sets up
     * the callback for handling incoming telemetry messages.
     */
    TcpManager(zmq::context_t& ctx, const Config& cfg, TcpMessageCallback callback);

    /**
     * @brief Destructor - ensures proper cleanup
     */
    ~TcpManager();

    // Delete copy and move operations for thread safety
    TcpManager(const TcpManager&) = delete;
    TcpManager& operator=(const TcpManager&) = delete;
    TcpManager(TcpManager&&) = delete;
    TcpManager& operator=(TcpManager&&) = delete;

    /**
     * @brief Start the TCP communication threads
     *
     * Creates and binds all necessary sockets and starts the background
     * threads for message processing.
     */
    void start();

    /**
     * @brief Stop the TCP communication threads
     *
     * Signals the background threads to stop processing messages.
     * Call join() after this to wait for threads to complete.
     */
    void stop();

    /**
     * @brief Wait for background threads to complete
     *
     * Blocks until both receiver and forwarder threads have finished.
     * Should be called after stop().
     */
    void join();

    /**
     * @brief Publish telemetry data to UI subscribers
     * @param topic The topic to publish on (e.g., "target.camera.UAV_1" or "type.location.UAV_1")
     * @param data The binary telemetry data to publish
     *
     * Sends telemetry data to all UI components subscribed to the given topic.
     * This method is thread-safe and can be called from callback functions.
     */
    void publishTelemetry(const std::string& topic, const std::vector<uint8_t>& data);

   private:
    /**
     * @brief Main loop for the receiver thread
     *
     * Continuously polls UAV telemetry sockets for incoming data.
     * When data is received, it calls the registered callback function.
     */
    void receiverLoop();

    /**
     * @brief Main loop for the forwarder thread
     *
     * Receives commands from UI components and forwards them to the
     * appropriate UAV based on the command prefix.
     */
    void forwarderLoop();

    /**
     * @brief Helper to set up polling items for UAV telemetry sockets
     * @return Vector of polling items for zmq::poll
     */
    std::vector<zmq::pollitem_t> setupTelemetryPolling() const;

    /**
     * @brief Helper to process incoming telemetry from a specific UAV socket
     * @param socket_index Index of the socket that received data
     */
    void processIncomingTelemetry(size_t socket_index);

    /**
     * @brief Helper to parse UI command and extract target UAV and command
     * @param message The raw UI command message
     * @return Pair of target_uav and actual_command
     */
    static std::pair<std::string, std::string> parseUICommand(const std::string& message);

    /**
     * @brief Helper to forward command to specific UAV
     * @param target_uav The UAV name to send command to
     * @param command The command to send
     * @return true if command was forwarded successfully
     */
    bool forwardCommandToUAV(const std::string& target_uav, const std::string& command);

    /**
     * @brief Extract the target UAV name from a UI command message
     * @param message The complete command message
     * @return The UAV name that should receive the command
     *
     * Parses command messages to determine which UAV they should be sent to.
     * Expected format: "UAV_NAME:command_data"
     */
    static std::string extractUISource(const std::string& message);

    // Core components
    zmq::context_t& context;              ///< Reference to the ZeroMQ context
    const Config& config;                 ///< Reference to configuration data
    std::atomic<bool> running{false};     ///< Flag controlling thread execution
    TcpMessageCallback messageCallback_;  ///< Callback for incoming messages
    mutable std::mutex socketMutex;       ///< Mutex for thread-safe socket operations

    // ZeroMQ sockets for different communication patterns
    std::unique_ptr<zmq::socket_t> pubToUi;                           ///< PUB socket for publishing to UI
    std::unique_ptr<zmq::socket_t> pullFromUi;                        ///< PULL socket for receiving UI commands
    std::vector<std::unique_ptr<zmq::socket_t>> uavTelemetrySockets;  ///< PULL sockets for UAV telemetry
    std::vector<std::unique_ptr<zmq::socket_t>> uavCommandSockets;    ///< PUSH sockets for UAV commands

    // Background processing threads
    std::thread receiverThread;   ///< Thread for receiving telemetry data
    std::thread forwarderThread;  ///< Thread for forwarding commands
};

#endif  // TCPMANAGER_H

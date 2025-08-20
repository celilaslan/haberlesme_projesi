/**
 * @file ZmqManager.h
 * @brief ZeroMQ communication manager for the telemetry service
 *
 * This file defines the ZmqManager class which handles all ZeroMQ-based
 * communication with UAVs and UI components. It manages multiple socket
 * types and implements the publish-subscribe and push-pull patterns.
 */

#ifndef ZMQMANAGER_H
#define ZMQMANAGER_H

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <zmq.hpp>

#include "Config.h"

// Callback function type for handling incoming ZMQ messages
// Parameters: source description, message data
using ZmqMessageCallback = std::function<void(const std::string&, const std::string&)>;

/**
 * @class ZmqManager
 * @brief Manages all ZeroMQ communication for the telemetry service
 *
 * The ZmqManager handles:
 * - Receiving telemetry data from UAVs (PULL sockets)
 * - Publishing telemetry data to UI components (PUB socket)
 * - Receiving commands from UI components (PULL socket)
 * - Forwarding commands to UAVs (PUSH sockets)
 *
 * Uses two background threads:
 * - Receiver thread: Handles incoming telemetry from UAVs
 * - Forwarder thread: Handles command forwarding from UI to UAVs
 */
class ZmqManager {
   public:
    /**
     * @brief Constructor
     * @param ctx ZeroMQ context to use for all sockets
     * @param config Configuration containing UAV and UI port settings
     * @param callback Function to call when telemetry messages are received
     *
     * Initializes the ZmqManager with the necessary configuration and sets up
     * the callback for handling incoming telemetry messages.
     */
    ZmqManager(zmq::context_t& ctx, const Config& config, ZmqMessageCallback callback);

    /**
     * @brief Destructor - ensures proper cleanup
     */
    ~ZmqManager();

    /**
     * @brief Start the ZMQ communication threads
     *
     * Creates and binds all necessary sockets and starts the background
     * threads for message processing.
     */
    void start();

    /**
     * @brief Stop the ZMQ communication threads
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
     * @param topic The topic to publish on (e.g., "camera_UAV_1")
     * @param data The telemetry data to publish
     *
     * Sends telemetry data to all UI components subscribed to the given topic.
     * This method is thread-safe and can be called from callback functions.
     */
    void publishTelemetry(const std::string& topic, const std::string& data);

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
     * @brief Extract the target UAV name from a UI command message
     * @param message The complete command message
     * @return The UAV name that should receive the command
     *
     * Parses command messages to determine which UAV they should be sent to.
     * Expected format: "UAV_NAME:command_data"
     */
    std::string extractUISource(const std::string& message);

    // Core components
    zmq::context_t& context;              ///< Reference to the ZeroMQ context
    const Config& config;                 ///< Reference to configuration data
    std::atomic<bool> running{false};     ///< Flag controlling thread execution
    ZmqMessageCallback messageCallback_;  ///< Callback for incoming messages
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

#endif  // ZMQMANAGER_H
/**
 * @file UdpManager.h
 * @brief UDP communication manager for the telemetry service
 *
 * This file defines classes for managing UDP-based telemetry communication,
 * providing an alternative to TCP for UAVs that prefer UDP for
 * lower latency or simpler networking requirements.
 */

#ifndef UDPMANAGER_H
#define UDPMANAGER_H

#include <array>
#include <atomic>
#include <boost/asio.hpp>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>
#include <cstdio>
#include <unordered_map>
#include <unordered_set>

#include "Config.h"

using boost::asio::ip::udp;

// Callback function type for handling incoming UDP messages
// Parameters: source description, binary message data
using UdpMessageCallback = std::function<void(const std::string&, const std::vector<uint8_t>&)>;

/**
 * @class UdpServer
 * @brief Manages a single UDP listening socket for one UAV
 *
 * Each UdpServer instance handles UDP communication with one specific UAV.
 * It listens on a dedicated port and uses asynchronous I/O to receive
 * telemetry data without blocking.
 */
class UdpServer {
   public:
    /**
     * @brief Constructor - sets up UDP server for one UAV
     * @param io_context Boost.Asio I/O context for async operations
     * @param address IP address to bind to
     * @param port UDP port number to listen on
     * @param uav_name Name of the UAV this server handles
     * @param callback Function to call when messages are received
     *
     * Creates a UDP socket bound to the specified address and port,
     * and starts the asynchronous receive loop.
     */
    UdpServer(boost::asio::io_context& io_context, const std::string& address, short port, const std::string& uav_name,
              UdpMessageCallback callback);

   private:
    /**
     * @brief Start asynchronous receive operation
     *
     * Sets up an async_receive_from operation that will call itself
     * recursively to continuously listen for incoming UDP packets.
     */
    void doReceive();

    udp::socket socket_;                         ///< UDP socket for receiving data
    udp::endpoint remote_endpoint_;              ///< Endpoint of the last sender
    enum : std::uint16_t { max_length = 1024 };  ///< Maximum UDP packet size
    std::array<char, max_length> data_{};        ///< Buffer for incoming data
    std::string uav_name_;                       ///< Name of the UAV this server handles
    UdpMessageCallback messageCallback_;         ///< Callback for received messages
};

/**
 * @class UdpManager
 * @brief Manages multiple UDP servers for all configured UAVs
 *
 * The UdpManager creates and manages one UdpServer for each UAV that
 * has UDP telemetry enabled. It runs a single I/O context in a background
 * thread to handle all UDP communication asynchronously.
 */
class UdpManager {
   public:
    /**
     * @brief Constructor
     * @param config Configuration containing UAV settings
     * @param callback Function to call when UDP messages are received
     *
     * Initializes the UDP manager with configuration data and sets up
     * the callback for handling incoming messages.
     */
    UdpManager(const Config& config, UdpMessageCallback callback);

    /**
     * @brief Destructor - ensures clean shutdown
     */
    ~UdpManager();

    // Rule of 5: Disable copy/move operations for resource management safety
    UdpManager(const UdpManager&) = delete;
    UdpManager& operator=(const UdpManager&) = delete;
    UdpManager(UdpManager&&) = delete;
    UdpManager& operator=(UdpManager&&) = delete;

    /**
     * @brief Start UDP communication system
     *
     * Creates UDP servers for all configured UAVs and starts the
     * background I/O thread to handle async operations.
     */
    void start();

    /**
     * @brief Stop UDP communication system
     *
     * Stops the I/O context, causing all async operations to complete
     * and the background thread to exit.
     */
    void stop();

    /**
     * @brief Wait for background thread to complete
     *
     * Blocks until the I/O thread has finished execution.
     * Should be called after stop().
     */
    void join();

    /**
     * @brief Publish telemetry data to UI components via UDP
     * @param topic The topic to publish on (e.g., "target.camera.UAV_1" or "type.location.UAV_1")
     * @param data The binary telemetry data to send
     *
     * Sends telemetry data to UI endpoints using UDP multicast.
     * This method is thread-safe and can be called from callback functions.
     */
    void publishTelemetry(const std::string& topic, const std::vector<uint8_t>& data);

   private:
    boost::asio::io_context io_context_;  ///< Boost.Asio I/O context for async operations
    const Config& config_;                ///< Reference to configuration data
    UdpMessageCallback messageCallback_;  ///< Callback for incoming messages
    std::atomic<bool> running_{false};    ///< Flag controlling thread execution
    mutable std::mutex socketMutex_;      ///< Mutex for thread-safe socket operations

    std::vector<std::unique_ptr<UdpServer>> servers_;  ///< UDP servers for each UAV
    std::thread serviceThread_;                        ///< Background thread running I/O context

    // Publishing socket
    std::unique_ptr<udp::socket> publishSocket_;  ///< Socket for publishing telemetry data to UI
    udp::endpoint publishEndpoint_;               ///< Multicast endpoint for UI communication

    // Simple subscription management
    std::unique_ptr<udp::socket> subscriptionSocket_;  ///< Socket for receiving subscription requests
    mutable std::mutex subscriptionMutex_;             ///< Mutex for subscription data
    std::unordered_map<std::string, std::unordered_set<std::string>> subscriptions_;  ///< topic -> set of client_endpoints
    std::unordered_map<std::string, udp::endpoint> clients_;  ///< client_id -> endpoint

    // Helper methods for subscription
    void startSubscriptionReceive();
    void handleSubscriptionRequest(const std::vector<uint8_t>& data, const udp::endpoint& sender);
    std::vector<udp::endpoint> getSubscribers(const std::string& topic) const;
    std::string endpointToString(const udp::endpoint& endpoint) const;

    // Wildcard pattern matching for efficient subscriptions
    bool matchesWildcardPattern(const std::string& pattern, const std::string& topic) const;
};

#endif  // UDPMANAGER_H

# Telemetry Client Library

A simple C++ library for connecting to the UAV telemetry service and receiving real-time telemetry data. This library hides the complexity of ZeroMQ and Boost.Asio networking, providing a clean API for UI applications.

## Features

- **Easy-to-use API**: Simple connect, subscribe, and receive pattern
- **Dual Protocol Support**: TCP (ZeroMQ) for reliable communication, UDP for low-latency
- **Wildcard Subscriptions**: Subscribe to multiple topics with pattern matching
- **Command Support**: Send commands to UAVs (TCP only)
- **Thread-Safe**: Safe to use from multiple threads
- **Cross-Platform**: Works on Linux, Windows, and macOS

## Quick Start

### Basic Usage

```cpp
#include "TelemetryClient.h"
#include <iostream>

int main() {
    // Create client with unique ID
    TelemetryAPI::TelemetryClient client("my_ui_app");

    // Set up data callback
    client.setTelemetryCallback([](const std::string& topic, const std::vector<uint8_t>& data) {
        std::cout << "Received " << data.size() << " bytes on topic: " << topic << std::endl;
    });

    // Connect to service
    if (client.connectFromConfig()) {
        // Subscribe to all camera data
        client.subscribe("telemetry.*.camera.*");

        // Keep running...
        std::this_thread::sleep_for(std::chrono::seconds(30));

        client.disconnect();
    }

    return 0;
}
```

### Advanced Usage with Command Sending

```cpp
#include "TelemetryClient.h"
#include <iostream>

int main() {
    TelemetryAPI::TelemetryClient client("control_panel");

    // Connection status callback
    client.setConnectionCallback([](bool connected, const std::string& error) {
        if (connected) {
            std::cout << "Connected to telemetry service" << std::endl;
        } else {
            std::cout << "Disconnected: " << error << std::endl;
        }
    });

    // Telemetry data callback with packet parsing
    client.setTelemetryCallback([](const std::string& topic, const std::vector<uint8_t>& data) {
        const auto* header = TelemetryAPI::TelemetryClient::parseHeader(data);
        if (header) {
            std::cout << "Topic: " << topic << std::endl;
            std::cout << "Target: " << TelemetryAPI::TelemetryClient::getTargetName(header->targetID) << std::endl;
            std::cout << "Type: " << TelemetryAPI::TelemetryClient::getPacketTypeName(header->packetType) << std::endl;
        }
    });

    // Connect using TCP for command support
    if (client.connect("localhost", 5556, TelemetryAPI::Protocol::TCP)) {
        // Subscribe to all telemetry
        client.subscribe("telemetry.*");

        // Send a command to UAV_1
        client.sendCommand("UAV_1", "GET_STATUS");

        // Keep running...
        std::this_thread::sleep_for(std::chrono::seconds(60));

        client.disconnect();
    }

    return 0;
}
```

## API Reference

### TelemetryClient Class

#### Constructor
```cpp
TelemetryClient(const std::string& client_id)
```
Creates a new telemetry client with the specified unique identifier.

#### Connection Methods

**connect()**
```cpp
bool connect(const std::string& host, int port, Protocol protocol = Protocol::TCP)
```
Connect to telemetry service at specific host and port.

**connectFromConfig()**
```cpp
bool connectFromConfig(const std::string& config_file = "service_config.json", Protocol protocol = Protocol::TCP)
```
Connect using configuration file (recommended).

**disconnect()**
```cpp
void disconnect()
```
Disconnect from service and stop all background threads.

**isConnected()**
```cpp
bool isConnected() const
```
Check current connection status.

#### Subscription Methods

**subscribe()**
```cpp
bool subscribe(const std::string& topic)
```
Subscribe to telemetry topic with wildcard support.

**unsubscribe()**
```cpp
bool unsubscribe(const std::string& topic)
```
Unsubscribe from telemetry topic.

#### Command Methods

**sendCommand()** (TCP only)
```cpp
bool sendCommand(const std::string& uav_name, const std::string& command)
```
Send command to specific UAV.

#### Callback Methods

**setTelemetryCallback()**
```cpp
void setTelemetryCallback(TelemetryCallback callback)
```
Set callback for received telemetry data.

**setConnectionCallback()**
```cpp
void setConnectionCallback(ConnectionCallback callback)
```
Set callback for connection status changes.

### Topic Patterns

The library supports wildcard patterns in topic subscriptions:

- `"telemetry.*"` - All telemetry data
- `"telemetry.UAV_1.*"` - All data from UAV_1
- `"telemetry.*.camera.*"` - All camera data from all UAVs
- `"telemetry.*.*.location"` - All location data
- `"telemetry.UAV_1.camera.location"` - Specific data type

### Protocols

#### TCP (ZeroMQ)
- **Reliable delivery**: Messages guaranteed to arrive
- **Command support**: Can send commands to UAVs
- **Subscription filtering**: Done at transport level
- **Default port**: 5556 (subscriber), 5557 (commands)

#### UDP (Boost.Asio)
- **Low latency**: Faster message delivery
- **Telemetry only**: No command support
- **Client-side filtering**: Receives all subscribed data
- **Default port**: 5558

### Utility Functions

**parseHeader()**
```cpp
static const PacketHeader* parseHeader(const std::vector<uint8_t>& data)
```
Extract packet header from telemetry data.

**getTargetName()**
```cpp
static std::string getTargetName(uint8_t targetId)
```
Convert target ID to human-readable name.

**getPacketTypeName()**
```cpp
static std::string getPacketTypeName(uint8_t packetType)
```
Convert packet type ID to human-readable name.

## Building

The library is built automatically as part of the main project:

```bash
mkdir build
cd build
cmake ..
make telemetry_client
```

### Example Application

Build and run the example:

```bash
make example_client
./telemetry_client_library/example_client
```

## Dependencies

- **ZeroMQ**: TCP communication and subscription management
- **Boost.Asio**: UDP communication
- **nlohmann/json**: Configuration file parsing
- **C++17**: Required for modern C++ features

## Thread Safety

- All public methods are thread-safe
- Callbacks are called from background threads
- Use proper synchronization in your callback functions

## Error Handling

- Connection errors are reported via ConnectionCallback
- Failed operations return false
- Exceptions are caught internally and converted to error callbacks

## Configuration File

The library can automatically read connection settings from `service_config.json`:

```json
{
  "ui_ports": {
    "tcp_subscribe_port": 5556,
    "tcp_command_port": 5557,
    "udp_publish_port": 5558
  },
  "service": {
    "ip": "localhost"
  }
}
```

## Examples Directory

See `example_client.cpp` for a comprehensive example showing:
- Connection establishment
- Multiple subscription patterns
- Telemetry data parsing
- Command sending
- Error handling
- Clean shutdown

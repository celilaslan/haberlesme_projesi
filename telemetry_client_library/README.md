# Telemetry Client Library

A simple C++ library for connecting to the UAV telemetry service and receiving real-time telemetry data. This library hides the complexity of ZeroMQ and Boost.Asio n- **Subscription method**: ZeroMQ prefix matching + TelemetryClient library wildcard filtering
- **Wildcard implementation**:
  - Converts `telemetry.*` to prefix `telemetry.`
  - Converts `telemetry.*.camera.*` to prefix `telemetry.` + library internal filtering
  - TelemetryClient library handles pattern matching for complex wildcards
- **Performance**: Efficient prefix subscription, detailed filtering within librarying, providing a clean API for UI applications.

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

The library supports wildcard patterns in topic subscriptions with different implementations per protocol:

**Common Patterns:**
- `"telemetry.*"` - All telemetry data
- `"telemetry.UAV_1.*"` - All data from UAV_1
- `"telemetry.*.camera.*"` - All camera data from all UAVs
- `"telemetry.*.*.location"` - All location data
- `"telemetry.UAV_1.camera.location"` - Specific data type

**TCP (ZeroMQ) Wildcard Implementation:**
- **Method**: ZeroMQ prefix matching + TelemetryClient library filtering
- **Behavior**: Library subscribes to broader prefix, then filters received data internally
- **Example**: `telemetry.*.camera.*` becomes ZeroMQ subscription to `telemetry.` + library filtering
- **Reason**: ZeroMQ only supports prefix matching natively, not complex wildcard patterns
- **Performance**: May receive more data than needed, but reliable delivery guaranteed
- **Use Case**: When reliability is critical and some extra bandwidth is acceptable

**UDP Wildcard Implementation:**
- **Method**: Server-side pattern matching before transmission
- **Behavior**: Service filters before sending, client receives only matching data
- **Example**: `telemetry.*.camera.*` is matched at service level, only matching topics sent
- **Reason**: Full control over UDP implementation allows efficient server-side filtering
- **Performance**: Minimal bandwidth usage, optimal for high-frequency data
- **Use Case**: When low latency and minimal bandwidth are priorities

**Why Different Approaches?**
This hybrid design leverages the strengths of each protocol:
- **TCP**: Uses ZeroMQ's reliable delivery with library-level wildcard enhancement
- **UDP**: Custom implementation allows optimal bandwidth efficiency with server-side filtering
- **Result**: Both protocols provide the same wildcard API to applications while optimizing for their respective strengths

### Protocols

#### TCP (ZeroMQ)
- **Reliable delivery**: Messages guaranteed to arrive
- **Command support**: Can send commands to UAVs
- **Subscription method**: ZeroMQ prefix matching + client-side wildcard filtering
- **Wildcard implementation**:
  - Converts `telemetry.*` to prefix `telemetry.`
  - Converts `telemetry.*.camera.*` to prefix `telemetry.` + client filtering
  - Application-level pattern matching for complex wildcards
- **Performance**: Efficient prefix subscription, detailed filtering after receipt
- **Default port**: 5556 (subscriber), 5557 (commands)

#### UDP (Boost.Asio)
- **Low latency**: Faster message delivery
- **Telemetry only**: No command support
- **Subscription method**: Server-side wildcard pattern matching at service level
- **Wildcard implementation**: Full pattern matching before transmission
- **Performance**: Reduces network traffic by filtering at source
- **Automatic port assignment**: Clients automatically get OS-assigned ports
- **Option A Architecture**: Service publishes to registered client endpoints
- **Default port**: 5572 (subscription management)

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
    "udp_publish_port": 5572
  },
  "service": {
    "ip": "localhost"
  }
}
```

## Usage Examples

The camera_ui and mapping_ui applications provide comprehensive examples showing:
- Connection establishment with both TCP and UDP protocols
- Multiple subscription patterns and wildcard usage
- Real-time telemetry data parsing and display
- Command sending capabilities (TCP only)
- Proper error handling and connection management
- Clean shutdown procedures

See the `camera_ui/camera_ui.cpp` and `mapping_ui/mapping_ui.cpp` source files for practical implementation examples.

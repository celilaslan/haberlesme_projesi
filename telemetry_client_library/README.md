# Telemetry Client Library

A comprehensive C++ shared library (.so/.dll) that provides both basic and advanced APIs for communicating with the telemetry service. This library offers everything from simple telemetry reception to advanced fleet management, data analysis, and real-time monitoring capabilities.

## Features

### Basic Features
- **Simple API**: Clean, intuitive interface for telemetry communication
- **Protocol Support**: TCP (ZeroMQ), UDP (Boost.Asio), or both simultaneously
- **Filtering**: Subscribe to specific UAVs or data types (mapping/camera)
- **Command Sending**: Send commands to UAVs via the telemetry service
- **Thread-Safe**: Safe to use from multiple threads
- **Cross-Platform**: Works on Linux and Windows
- **Error Handling**: Comprehensive error reporting and callbacks

### Advanced Features ✨
- **Asynchronous Commands**: Send commands with response callbacks and timeout handling
- **Fleet Management**: Multi-UAV coordination and status monitoring
- **Data Analysis**: Real-time quality metrics, bandwidth monitoring, and historical data
- **Event System**: Subscribe to UAV events (connection, disconnection, emergencies)
- **Security**: Authentication, encryption, and permission-based access control
- **Performance Monitoring**: CPU, memory, and throughput metrics
- **Data Recording/Replay**: Record telemetry sessions and replay for analysis
- **Mock UAV Simulation**: Built-in UAV simulator for testing and development
- **Network Resilience**: Automatic failover, backup connections, and quality monitoring
- **Advanced Configuration**: Operation modes, protocol optimization, and dynamic settings

## Quick Start

### 1. Build the Library

```bash
# From the project root
./dev.sh build
```

This builds the shared library and example applications.

### 2. Simple Usage Example

```cpp
#include "TelemetryClient.h"
#include <iostream>

using namespace TelemetryAPI;

void onTelemetryReceived(const TelemetryData& data) {
    std::cout << "UAV: " << data.uav_name
              << " Data: " << data.raw_data << std::endl;
}

int main() {
    TelemetryClient client;

    // Initialize and connect to service
    if (!client.initialize("localhost")) {
        std::cerr << "Failed to initialize: " << client.getLastError() << std::endl;
        return 1;
    }

    // Start receiving telemetry data
    if (!client.startReceiving(Protocol::TCP_ONLY, onTelemetryReceived)) {
        std::cerr << "Failed to start: " << client.getLastError() << std::endl;
        return 1;
    }

    // Send a command to UAV_1
    client.sendCommand("UAV_1", "status report", "MyApp");

    // Keep running...
    std::this_thread::sleep_for(std::chrono::seconds(30));

    client.stopReceiving();
    return 0;
}
```

### 3. Link Against the Library

```cmake
# CMakeLists.txt
find_package(TelemetryClient REQUIRED)
target_link_libraries(your_app TelemetryClient::telemetry_client)
```

Or manually:
```bash
g++ -std=c++17 your_app.cpp -ltelemetry_client -o your_app
```

## API Reference

### Core Classes

#### `TelemetryClient`
Main client class for telemetry communication.

**Key Methods:**
- `bool initialize(host, config_file)` - Initialize client with service connection
- `bool startReceiving(protocol, callback, error_callback)` - Start receiving data
- `bool sendCommand(uav_name, command, client_name)` - Send command to UAV
- `bool subscribeToUAV(uav_name, data_type)` - Filter by specific UAV
- `bool subscribeToDataType(data_type)` - Filter by data type
- `void stopReceiving()` - Stop and cleanup

#### `TelemetryData`
Structure containing received telemetry data.

**Fields:**
- `std::string uav_name` - UAV identifier (e.g., "UAV_1")
- `DataType data_type` - MAPPING, CAMERA, or UNKNOWN
- `std::string raw_data` - Raw telemetry data
- `Protocol received_via` - TCP_ONLY, UDP_ONLY, or BOTH
- `uint64_t timestamp_ms` - Reception timestamp

### Enums

#### `Protocol`
- `TCP_ONLY` - Use TCP (ZeroMQ) for reliable communication
- `UDP_ONLY` - Use UDP for low-latency communication
- `BOTH` - Use both protocols simultaneously

#### `DataType`
- `MAPPING` - Mapping/navigation data
- `CAMERA` - Camera/vision data
- `UNKNOWN` - Unknown or mixed data

### Utility Functions

- `std::string getLibraryVersion()` - Get library version
- `bool testServiceConnection(host, port, timeout)` - Test connectivity
- `bool parseTelemetryMessage(raw_message, uav_name, numeric_code)` - Parse telemetry format

## Example Applications

The library includes several example applications:

### simple_receiver
Basic telemetry data receiver.
```bash
./telemetry_client_library/examples/simple_receiver --protocol tcp
./telemetry_client_library/examples/simple_receiver --protocol udp
./telemetry_client_library/examples/simple_receiver --protocol both
```

### advanced_client
Interactive client with filtering and command sending.
```bash
./telemetry_client_library/examples/advanced_client --protocol both
```

Commands:
- `filter uav UAV_1` - Filter for specific UAV
- `filter type mapping` - Filter for mapping data only
- `send UAV_1 takeoff` - Send command to UAV
- `debug on` - Enable debug output

### command_sender
Dedicated command sending utility.
```bash
# Single command
./telemetry_client_library/examples/command_sender --uav UAV_1 --command "takeoff"

# Interactive mode
./telemetry_client_library/examples/command_sender
```

### advanced_telemetry_client ✨
**NEW**: Comprehensive demonstration of all advanced features.
```bash
./telemetry_client_library/examples/advanced_telemetry_client [service_host]
```

This advanced example demonstrates:
- **Fleet Management**: Multi-UAV coordination and status monitoring
- **Asynchronous Commands**: Non-blocking command execution with callbacks
- **Data Recording**: Automatic telemetry session recording
- **Performance Monitoring**: Real-time metrics and network statistics
- **Mock UAV Simulation**: Built-in test UAV with configurable network conditions
- **Event Handling**: Real-time notifications for UAV events
- **Data Quality Analysis**: Packet loss, latency, and freshness metrics

Example output:
```
=== FLEET STATUS ===
Active UAVs: 3/3
Overall Health: 87.5%
  UAV_1: ONLINE (Health: 92.3%)
  UAV_2: ONLINE (Health: 85.1%)
  UAV_3: OFFLINE (Health: 45.2%)

=== PERFORMANCE METRICS ===
CPU Usage: 5.2%
Memory Usage: 84 MB
Messages/sec: 156
Avg Processing Time: 1.23 ms
```

## Advanced API Classes

The library automatically loads configuration from:
1. `SERVICE_CONFIG` environment variable
2. `service_config.json` in current directory
3. Default ports if no config found

Default ports:
- TCP Publish: 5557
- TCP Command: 5558
- UDP Camera: 5570
- UDP Mapping: 5571

## Error Handling

The library provides comprehensive error handling:

```cpp
// Error callback for async errors
void onError(const std::string& error_message) {
    std::cerr << "Async error: " << error_message << std::endl;
}

client.startReceiving(Protocol::TCP_ONLY, onTelemetry, onError);

// Synchronous error checking
if (!client.sendCommand("UAV_1", "test")) {
    std::cout << "Error: " << client.getLastError() << std::endl;
}
```

## Threading

The library is thread-safe and uses background threads for:
- TCP telemetry reception (ZeroMQ subscriber)
- UDP telemetry reception (Boost.Asio async I/O)
- Command sending (ZeroMQ push socket)

All callbacks are invoked from background threads, so ensure your callback functions are thread-safe.

## Building and Installation

### Development Build
```bash
# Configure and build
./dev.sh configure
./dev.sh build

# Run examples
./dev.sh run simple_receiver --protocol tcp
```

### System Installation
```bash
# Install library and headers
sudo make install

# Or using the dev script
./dev.sh install /usr/local
```

### Package Creation
```bash
# Create distribution packages
./dev.sh package
```

## Platform Support

- **Linux**: Builds as `libtelemetry_client.so`
- **Windows**: Builds as `telemetry_client.dll`
- **macOS**: Builds as `libtelemetry_client.dylib`

## Dependencies

The library encapsulates these dependencies (not exposed to users):
- ZeroMQ (for TCP communication)
- Boost.Asio (for UDP communication)
- nlohmann/json (for configuration parsing)

## License

Same license as the main telemetry project.

## Troubleshooting

### Common Issues

1. **"Failed to initialize"**
   - Check if `service_config.json` exists
   - Verify service hostname is correct
   - Ensure telemetry service is running

2. **"Failed to start receiving"**
   - Check if ports are available
   - Verify firewall settings
   - Try different protocol (TCP vs UDP)

3. **"No telemetry data received"**
   - Ensure UAV simulators are running
   - Check if telemetry service is running
   - Verify port configuration matches

### Debug Mode

Enable debug mode to see detailed networking information:
```cpp
client.setDebugMode(true);
```

This will print connection details, received messages, and error information.

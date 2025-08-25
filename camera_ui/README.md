# Camera UI Application

A specialized telemetry visualization application focused on camera systems and imaging data from UAVs. This application uses the TelemetryClient library to provide real-time monitoring of camera-related telemetry.

## Features

- **📸 Camera-Focused Display**: Specialized visualization for camera telemetry data
- **📍 Location Tracking**: GPS coordinates with precision formatting for photography context
- **🔋 System Status**: Health monitoring, mission state, and resource usage
- **📡 Command Interface**: Send commands to UAVs (TCP mode only)
- **🎯 Smart Filtering**: Automatically subscribes to camera-relevant topics
- **⚡ Real-time Updates**: Live telemetry data with timestamp precision

## Usage

### Basic Monitoring
```bash
# Monitor all camera telemetry (TCP - recommended)
./camera_ui

# Use UDP for low-latency monitoring
./camera_ui --protocol udp
```

### Data Filtering Options
```bash
# Default: Camera target data only
./camera_ui --protocol tcp

# Filter by data type across all targets
./camera_ui --protocol udp --location-only    # Only location data from all UAVs
./camera_ui --protocol tcp --status-only      # Only status data from all UAVs

# Monitor all telemetry from all targets
./camera_ui --protocol tcp --all-targets
```

### Interactive Command Mode
```bash
# Enable command sending to specific UAV
./camera_ui --send UAV_1

# Example commands you can send:
# - CAPTURE_PHOTO
# - START_RECORDING
# - STOP_RECORDING
# - ADJUST_FOCUS
# - SET_ZOOM 2.5
# - GIMBAL_TILT -15
```

### Command Line Options
- `--protocol tcp|udp` : Communication protocol (default: tcp)
- `--send UAV_NAME` : Enable command interface for specified UAV
- `--location-only` : Subscribe only to location data from all targets
- `--status-only` : Subscribe only to status data from all targets
- `--all-targets` : Subscribe to all telemetry from all targets
- `--help` : Show help message

## Telemetry Data Display

### Location Data
```
📍 Location: 41.013840, 28.949660 | Alt: 120.0m | Heading: 45° | Speed: 12.5m/s
```
- High-precision GPS coordinates (7 decimal places)
- Altitude, heading, and speed for context
- Essential for geotagging photos/videos

### Status Data
```
🔋 Status: ✅ Good | 🎯 Mission | Flight: 450s | CPU: 35.2% | Mem: 42.1%
```
- System health indicators with emoji status
- Mission state tracking
- Resource monitoring for camera processing

### Camera-Specific Data
The application subscribes to:
- `telemetry.*.camera.*` - All camera-targeted telemetry
- `telemetry.*.*.location` - Location data for all UAVs
- Additional camera data types as they become available

## Protocol Support

### TCP Mode (Default - Recommended)
- **Reliable delivery**: No lost telemetry data, critical for camera operations
- **Command support**: Full bidirectional communication for camera control
- **Wildcard subscriptions**: ZeroMQ prefix matching + TelemetryClient library filtering
  - Efficient for patterns like `telemetry.*.camera.*`
  - Subscribes to `telemetry.` prefix, TelemetryClient library filters `camera` topics internally
  - Guarantees all relevant camera data is received
- **Security**: Encrypted command channel for camera controls

### UDP Mode (Low Latency)
- **Fast updates**: Minimal latency for real-time camera monitoring
- **No commands**: Telemetry reception only (security limitation)
- **Server-side filtering**: Service filters wildcard patterns before transmission
  - Only matching camera data sent over network
  - Optimal bandwidth usage for high-frequency telemetry
- **Real-time focus**: Best for live camera feed monitoring

## Sample Session

```bash
$ ./camera_ui --send UAV_1
📸 =========================================== 📸
    Camera UI - UAV Telemetry Visualization
📸 =========================================== 📸
Protocol: tcp
Command target: UAV_1

Connecting to telemetry service...
✅ Connected to telemetry service

Subscribing to camera telemetry...
✅ Subscribed to all camera telemetry: telemetry.*.camera.*
✅ Subscribed to location data: telemetry.*.*.location

🎥 Camera UI ready - monitoring UAV camera systems...
Press Ctrl+C to stop
===============================================

📡 Command interface enabled for UAV_1
Type commands and press Enter (or Ctrl+C to exit):

📡 [1] 2025-08-24 14:30:15.123 - telemetry.UAV_1.camera.location
   📦 Size: 45 bytes
   🎯 Target: Camera
   📋 Type: Location
   📍 Location: 41.013840, 28.949660 | Alt: 120.0m | Heading: 45° | Speed: 12.5m/s

CAPTURE_PHOTO
✅ [2025-08-24 14:30:18.456] Sent command to UAV_1: CAPTURE_PHOTO

📡 [2] 2025-08-24 14:30:20.789 - telemetry.UAV_1.camera.status
   📦 Size: 32 bytes
   🎯 Target: Camera
   📋 Type: Status
   🔋 Status: ✅ Good | 🎯 Mission | Flight: 450s | CPU: 35.2% | Mem: 42.1%
```

## Integration with Telemetry Service

The Camera UI automatically:
1. **Connects** to the telemetry service using configuration file
2. **Subscribes** to camera-relevant topics with wildcard patterns
3. **Displays** formatted telemetry data with camera context
4. **Sends commands** through the service to UAVs (TCP only)
5. **Handles errors** gracefully with automatic reconnection

## Dependencies

- **TelemetryClient Library**: Handles all networking complexity
- **ZeroMQ**: TCP communication (linked via library)
- **Boost.Asio**: UDP communication (linked via library)
- **C++17**: Modern C++ features for clean code

## Use Cases

- **Real-time monitoring** of UAV camera systems
- **Photography missions** with location tracking
- **Video recording** session management
- **Camera diagnostics** and health monitoring
- **Remote camera control** and configuration
- **Aerial photography** workflow integration

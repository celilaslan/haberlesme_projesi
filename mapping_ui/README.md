# Mapping UI Application

A specialized telemetry visualization application focused on navigation, location tracking, and mapping data from UAVs. This application uses the TelemetryClient library to provide comprehensive real-time mapping and navigation monitoring.

## Features

- **🗺️ Mapping-Focused Display**: Specialized visualization for navigation and location data
- **📍 Precision GPS Tracking**: High-precision coordinate display with reference distance
- **🧭 Navigation Context**: Mission tracking, heading, speed, and altitude monitoring
- **📡 Mission Commands**: Send navigation and waypoint commands to UAVs
- **🎯 Multi-UAV Support**: Track multiple UAVs simultaneously with identification
- **⚡ Real-time Updates**: Live position updates with mapping context

## Usage

### Basic Location Tracking
```bash
# Monitor all mapping/location telemetry (TCP - recommended)
./mapping_ui

# Use UDP for low-latency position updates
./mapping_ui --protocol udp
```

### Navigation Command Mode
```bash
# Enable navigation command sending to specific UAV
./mapping_ui --send UAV_1

# Example navigation commands:
# - SET_WAYPOINT 41.0392 29.0352
# - GET_MISSION_STATUS
# - RETURN_HOME
# - SET_ALTITUDE 150
# - FOLLOW_PATH waypoint1.json
```

### Command Line Options
- `--protocol tcp|udp` : Communication protocol (default: tcp)
- `--send UAV_NAME` : Enable navigation command interface for specified UAV
- `--help` : Show help message

## Telemetry Data Display

### Location Data with Mapping Context
```
🗺️ [UAV_1] GPS: 41.0138400, 28.9496600
       Alt: 120.0m | Course: 045° | Speed: 12.5m/s
       Distance from reference: 1.23 km
```
- Ultra-high precision GPS coordinates (7 decimal places)
- Navigation context: altitude, course, speed
- Distance calculation from reference point (configurable)

### Mission Status
```
📊 System: 💚 | Mission: 🗺️ Mapping
       Flight time: 7m 30s | Resources: CPU 28.5%, RAM 31.2%
```
- Real-time mission state tracking
- Flight time in human-readable format
- System resource monitoring for navigation processing

### Multi-UAV Tracking
Each UAV is tracked individually with clear identification:
- UAV name extraction from topics
- Individual position tracking
- Mission state per UAV
- Performance metrics per vehicle

## Subscription Patterns

The Mapping UI subscribes to:
- `telemetry.*.mapping.*` - All mapping-targeted telemetry
- `telemetry.*.*.location` - All location data from all UAVs
- `telemetry.*.*.status` - System status for mission context

## Protocol Support

### TCP Mode (Default - Recommended)
- **Reliable delivery**: Critical for navigation data
- **Command support**: Full navigation command interface
- **Subscription filtering**: Efficient data filtering at service level

### UDP Mode (Low Latency)
- **Real-time updates**: Minimal latency for live tracking
- **High frequency**: Suitable for rapid position updates
- **No commands**: Position monitoring only

## Navigation Commands

### Waypoint Management
```bash
SET_WAYPOINT 41.0392 29.0352    # Set GPS waypoint
SET_ALTITUDE 150                # Set target altitude
RETURN_HOME                     # Initiate return to home
```

### Mission Control
```bash
GET_MISSION_STATUS              # Query current mission state
PAUSE_MISSION                   # Pause current mission
RESUME_MISSION                  # Resume paused mission
ABORT_MISSION                   # Emergency mission abort
```

### Path Planning
```bash
FOLLOW_PATH waypoints.json      # Follow predefined path
SET_SPEED 8.5                  # Set cruise speed (m/s)
SET_HEADING 270                # Set target heading (degrees)
```

## Sample Session

```bash
$ ./mapping_ui --send UAV_1
🗺️ =========================================== 🗺️
    Mapping UI - UAV Location Tracking
🗺️ =========================================== 🗺️
Protocol: tcp
Command target: UAV_1

Connecting to telemetry service...
✅ Connected to telemetry service

Subscribing to mapping telemetry...
✅ Subscribed to mapping telemetry: telemetry.*.mapping.*
✅ Subscribed to all location data: telemetry.*.*.location
✅ Subscribed to status data: telemetry.*.*.status

🗺️ Mapping UI ready - tracking UAV locations and missions...
Press Ctrl+C to stop
===============================================

📡 Command interface enabled for UAV_1
Sample commands: 'SET_WAYPOINT 41.0392 29.0352', 'GET_MISSION_STATUS', 'RETURN_HOME'
Type commands and press Enter (or Ctrl+C to exit):

📍 [1] 2025-08-24 14:30:15.123 - telemetry.UAV_1.mapping.location
   📦 Size: 45 bytes
   🎯 Target: Mapping
   📋 Type: Location
   🗺️ [UAV_1] GPS: 41.0138400, 28.9496600
       Alt: 120.0m | Course: 045° | Speed: 12.5m/s
       Distance from reference: 1.23 km

SET_WAYPOINT 41.0392 29.0352
✅ [2025-08-24 14:30:18.456] Sent navigation command to UAV_1: SET_WAYPOINT 41.0392 29.0352

📍 [2] 2025-08-24 14:30:20.789 - telemetry.UAV_2.general.location
   📦 Size: 45 bytes
   🎯 Target: General
   📋 Type: Location
   🗺️ [UAV_2] GPS: 41.0140200, 28.9498400
       Alt: 95.0m | Course: 180° | Speed: 8.2m/s
       Distance from reference: 1.18 km

📍 [3] 2025-08-24 14:30:22.456 - telemetry.UAV_1.mapping.status
   📦 Size: 32 bytes
   🎯 Target: Mapping
   📋 Type: Status
   📊 System: 💚 | Mission: 🗺️ Mapping
       Flight time: 7m 30s | Resources: CPU 28.5%, RAM 31.2%
```

## Reference Point Configuration

The application calculates distances from a configurable reference point:
- **Default**: Bosphorus Bridge, Istanbul (41.0392°N, 29.0352°E)
- **Customizable**: Can be modified in source code for your area of operations
- **Distance calculation**: Approximate great-circle distance in kilometers

## Integration with Mapping Systems

The Mapping UI is designed to integrate with:
- **GIS software**: Export coordinate data for mapping applications
- **Flight planning tools**: Real-time position feedback for mission planning
- **Surveying applications**: Precision coordinate tracking for survey missions
- **Emergency services**: Real-time UAV location for search and rescue

## Use Cases

- **Survey missions** with precise coordinate tracking
- **Search and rescue** operations with real-time positioning
- **Infrastructure inspection** with navigation monitoring
- **Agricultural mapping** with field boundary tracking
- **Emergency response** with multi-UAV coordination
- **Scientific research** with GPS data logging
- **Delivery operations** with route optimization

## Dependencies

- **TelemetryClient Library**: Handles all networking complexity
- **ZeroMQ**: TCP communication for reliable navigation data
- **Boost.Asio**: UDP communication for real-time updates
- **C++17**: Modern C++ with mathematical libraries for distance calculations

## Future Enhancements

- **Map overlay**: Visual map display with UAV positions
- **Path visualization**: Show planned and actual flight paths
- **Geofencing**: Boundary violation alerts and warnings
- **Data logging**: Export flight tracks for analysis
- **Multi-format export**: KML, GPX, CSV output formats

/**
 * @file telemetry_packets.h
 * @brief Binary packet definitions for telemetry communication
 *
 * This file defines the binary packet structures used throughout the telemetry system.
 * All packets use a common header format followed by type-specific payloads.
 * This replaces the old text-based format with an efficient, extensible binary protocol.
 */

#ifndef TELEMETRY_PACKETS_H
#define TELEMETRY_PACKETS_H

#include <chrono>
#include <cstdint>
#include <cstring>

// Ensure struct packing without padding for network compatibility
#pragma pack(push, 1)

/**
 * @brief Common header for all telemetry packets
 *
 * This header allows the service to route packets based on target and type
 * without needing to understand the specific payload content.
 */
struct PacketHeader {
    uint8_t targetID;        ///< Primary target (1: Camera, 2: Mapping, 3: General)
    uint8_t packetType;      ///< Packet type (4: Location, 5: Status, 6: IMU, 7: Battery)
    uint16_t payloadLength;  ///< Length of payload in bytes
    uint64_t timestamp;      ///< UTC timestamp in milliseconds since epoch
};

/**
 * @brief Location/Position data payload
 *
 * Contains GPS coordinates and altitude information
 */
struct LocationPayload {
    double latitude;   ///< Latitude in decimal degrees
    double longitude;  ///< Longitude in decimal degrees
    float altitude;    ///< Altitude in meters above sea level
    float heading;     ///< Heading in degrees (0-359)
    float speed;       ///< Ground speed in m/s
};

/**
 * @brief System status payload
 *
 * Contains general system health and operational status
 */
struct StatusPayload {
    uint8_t systemHealth;  ///< System health (0: Critical, 1: Warning, 2: Good, 3: Excellent)
    uint8_t missionState;  ///< Mission state (0: Idle, 1: Takeoff, 2: Mission, 3: Landing, 4: Emergency)
    uint16_t flightTime;   ///< Flight time in seconds
    float cpuUsage;        ///< CPU usage percentage (0.0-100.0)
    float memoryUsage;     ///< Memory usage percentage (0.0-100.0)
};

/**
 * @brief IMU (Inertial Measurement Unit) data payload
 *
 * Contains accelerometer, gyroscope, and magnetometer readings
 */
struct IMUPayload {
    float accel_x, accel_y, accel_z;  ///< Acceleration in m/s² (X, Y, Z axes)
    float gyro_x, gyro_y, gyro_z;     ///< Angular velocity in rad/s (X, Y, Z axes)
    float mag_x, mag_y, mag_z;        ///< Magnetic field in µT (X, Y, Z axes)
    float temperature;                ///< IMU temperature in Celsius
};

/**
 * @brief Battery status payload
 *
 * Contains battery health and power information
 */
struct BatteryPayload {
    float voltage;        ///< Battery voltage in volts
    float current;        ///< Current draw in amperes
    float remaining;      ///< Remaining capacity percentage (0.0-100.0)
    uint16_t cycleCount;  ///< Number of charge cycles
    uint8_t cellCount;    ///< Number of battery cells
    float temperature;    ///< Battery temperature in Celsius
};

// Complete packet types combining header with specific payloads
struct LocationPacket {
    PacketHeader header;
    LocationPayload payload;
};

struct StatusPacket {
    PacketHeader header;
    StatusPayload payload;
};

struct IMUPacket {
    PacketHeader header;
    IMUPayload payload;
};

struct BatteryPacket {
    PacketHeader header;
    BatteryPayload payload;
};

#pragma pack(pop)

// Packet type constants
namespace PacketTypes {
    constexpr uint8_t LOCATION = 4;
    constexpr uint8_t STATUS = 5;
    constexpr uint8_t IMU = 6;
    constexpr uint8_t BATTERY = 7;
}  // namespace PacketTypes

// Target ID constants
namespace TargetIDs {
    constexpr uint8_t CAMERA = 1;
    constexpr uint8_t MAPPING = 2;
    constexpr uint8_t GENERAL = 3;
}  // namespace TargetIDs

/**
 * @brief Utility function to get current timestamp in milliseconds
 * @return Current UTC timestamp in milliseconds since epoch
 */
inline uint64_t getCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
}

/**
 * @brief Create a location packet with specified target
 * @param targetID Primary target for this packet
 * @param lat Latitude in decimal degrees
 * @param lon Longitude in decimal degrees
 * @param alt Altitude in meters
 * @param heading Heading in degrees
 * @param speed Ground speed in m/s
 * @return Complete location packet ready for transmission
 */
inline LocationPacket createLocationPacket(uint8_t targetID, double lat, double lon, float alt, float heading = 0.0f,
                                           float speed = 0.0f) {
    LocationPacket packet;
    packet.header.targetID = targetID;
    packet.header.packetType = PacketTypes::LOCATION;
    packet.header.payloadLength = sizeof(LocationPayload);
    packet.header.timestamp = getCurrentTimestamp();

    packet.payload.latitude = lat;
    packet.payload.longitude = lon;
    packet.payload.altitude = alt;
    packet.payload.heading = heading;
    packet.payload.speed = speed;

    return packet;
}

/**
 * @brief Create a status packet with specified target
 * @param targetID Primary target for this packet
 * @param health System health level
 * @param mission Mission state
 * @param flightTime Flight time in seconds
 * @param cpu CPU usage percentage
 * @param memory Memory usage percentage
 * @return Complete status packet ready for transmission
 */
inline StatusPacket createStatusPacket(uint8_t targetID, uint8_t health, uint8_t mission, uint16_t flightTime,
                                       float cpu, float memory) {
    StatusPacket packet;
    packet.header.targetID = targetID;
    packet.header.packetType = PacketTypes::STATUS;
    packet.header.payloadLength = sizeof(StatusPayload);
    packet.header.timestamp = getCurrentTimestamp();

    packet.payload.systemHealth = health;
    packet.payload.missionState = mission;
    packet.payload.flightTime = flightTime;
    packet.payload.cpuUsage = cpu;
    packet.payload.memoryUsage = memory;

    return packet;
}

#endif  // TELEMETRY_PACKETS_H

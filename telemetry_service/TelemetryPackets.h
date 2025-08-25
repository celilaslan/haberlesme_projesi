/**
 * @file telemetry_packets.h
 * @brief Binary packet header definitions for telemetry routing
 *
 * This file defines only the packet header structure needed by the telemetry service
 * for routing binary packets. The service treats payload data as opaque binary data.
 */

#ifndef TELEMETRY_PACKETS_H
#define TELEMETRY_PACKETS_H

#include <cstdint>

// Ensure struct packing without padding for network compatibility
#pragma pack(push, 1)

/**
 * @brief Common header for all telemetry packets
 *
 * This header allows the service to route packets based on target and type
 * without needing to understand the specific payload content.
 */
struct PacketHeader {
    uint8_t targetID;    ///< Primary target (1: Camera, 2: Mapping)
    uint8_t packetType;  ///< Packet type (4: Location, 5: Status)
};

#pragma pack(pop)

// Packet type constants
namespace PacketTypes {
    constexpr uint8_t LOCATION = 4;
    constexpr uint8_t STATUS = 5;
}  // namespace PacketTypes

// Target ID constants
namespace TargetIDs {
    constexpr uint8_t CAMERA = 1;
    constexpr uint8_t MAPPING = 2;
}  // namespace TargetIDs

#endif  // TELEMETRY_PACKETS_H

# ============================================================================
# MAPPING UI BUILD CONFIGURATION
# ============================================================================
#
# This CMakeLists.txt file defines the build configuration for the mapping UI
# application. This application subscribes to mapping-related telemetry data
# from UAVs via the telemetry service.
#
# FEATURES:
#   - ZeroMQ subscriber for mapping telemetry topics
#   - Optional command sending to UAVs via stdin
#   - Real-time telemetry data display with timestamps
#   - Multi-UAV support through topic filtering
# ============================================================================

# Create the mapping UI executable
add_executable(mapping_ui
  ${CMAKE_CURRENT_LIST_DIR}/mapping_ui.cpp    # Complete mapping UI implementation
)

# Link required libraries
link_with_zmq(mapping_ui)   # TCP (ZeroMQ) for subscribing to telemetry and sending commands
link_with_boost(mapping_ui) # Boost.Asio for UDP communication
link_with_json(mapping_ui)  # nlohmann/json for JSON data parsing

# Place the executable in the source directory for easier development
set_target_to_source_dir(mapping_ui)

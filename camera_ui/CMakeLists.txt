# ============================================================================
# CAMERA UI BUILD CONFIGURATION
# ============================================================================
#
# This CMakeLists.txt file defines the build configuration for the camera UI
# application. This application subscribes to camera-related telemetry data
# from UAVs via the telemetry service.
#
# FEATURES:
#   - ZeroMQ subscriber for camera telemetry topics
#   - Optional command sending to UAVs via stdin
#   - Real-time telemetry data display with timestamps
#   - Multi-UAV support through topic filtering
# ============================================================================

# Create the camera UI executable
add_executable(camera_ui
  ${CMAKE_CURRENT_LIST_DIR}/camera_ui.cpp    # Complete camera UI implementation
)

# Link required libraries
link_with_zmq(camera_ui)    # ZeroMQ for subscribing to telemetry and sending commands

# Place the executable in the source directory for easier development
set_target_to_source_dir(camera_ui)

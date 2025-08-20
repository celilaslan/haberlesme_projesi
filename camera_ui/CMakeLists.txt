# ============================================================================
# CAMERA UI BUILD CONFIGURATION
# ============================================================================
#
# This CMakeLists.txt file defines the build configuration for the camera UI
# application. This application uses the TelemetryClient library to subscribe
# to camera-related telemetry data from UAVs via the telemetry service.
#
# FEATURES:
#   - TelemetryClient library for simplified communication
#   - Camera telemetry filtering and display
#   - Optional command sending to UAVs via stdin
#   - Multi-protocol support (TCP, UDP, or both)
#   - UAV-specific filtering capabilities
# ============================================================================

# Create the camera UI executable
add_executable(camera_ui
  ${CMAKE_CURRENT_LIST_DIR}/camera_ui.cpp    # New library-based camera UI implementation
)

# Link with the telemetry client library
target_link_libraries(camera_ui
  telemetry_client    # Our new telemetry client library (handles all networking)
)

# Add include directory for TelemetryClient.h
target_include_directories(camera_ui PRIVATE
  ${CMAKE_SOURCE_DIR}/telemetry_client_library/include
)

# Place the executable in the source directory for easier development
set_target_to_source_dir(camera_ui)

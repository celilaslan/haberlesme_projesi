# ============================================================================
# MAPPING UI BUILD CONFIGURATION
# ============================================================================
#
# This CMakeLists.txt file defines the build configuration for the mapping UI
# application. This application uses the TelemetryClient library to subscribe
# to mapping-related telemetry data from UAVs via the telemetry service.
#
# FEATURES:
#   - TelemetryClient library for simplified communication
#   - Mapping telemetry filtering and display
#   - Optional command sending to UAVs via stdin
#   - Multi-protocol support (TCP, UDP, or both)
#   - UAV-specific filtering capabilities
# ============================================================================

# Create the mapping UI executable
add_executable(mapping_ui
  ${CMAKE_CURRENT_LIST_DIR}/mapping_ui.cpp    # New library-based mapping UI implementation
)

# Link with the telemetry client library
target_link_libraries(mapping_ui
  telemetry_client    # Our new telemetry client library (handles all networking)
)

# Add include directory for TelemetryClient.h
target_include_directories(mapping_ui PRIVATE
  ${CMAKE_SOURCE_DIR}/telemetry_client_library/include
)

# Place the executable in the source directory for easier development
set_target_to_source_dir(mapping_ui)

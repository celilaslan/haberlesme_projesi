# ============================================================================
# UAV SIMULATOR BUILD CONFIGURATION
# ============================================================================
#
# This CMakeLists.txt file defines the build configuration for the UAV
# simulator application. The simulator generates telemetry data and can
# send it via either ZeroMQ or UDP protocols.
#
# FEATURES:
#   - Dual protocol support (TCP via ZeroMQ and UDP)
#   - Configurable telemetry data generation
#   - Command reception from UI components (TCP only)
#   - Multi-UAV simulation support
# ============================================================================

# Create the UAV simulator executable
add_executable(uav_sim
  ${CMAKE_CURRENT_LIST_DIR}/uav_sim.cpp    # Complete UAV simulator implementation
)

# Link required libraries
link_with_zmq(uav_sim)      # TCP (ZeroMQ) for telemetry and commands
link_with_boost(uav_sim)    # Boost.Asio for UDP-based telemetry
link_with_json(uav_sim)     # nlohmann/json for configuration parsing

# Place the executable in the source directory for easier development
set_target_to_source_dir(uav_sim)

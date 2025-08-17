# ============================================================================
# TELEMETRY SERVICE BUILD CONFIGURATION
# ============================================================================
#
# This CMakeLists.txt file defines the build configuration for the main
# telemetry service executable. The service is composed of multiple
# source files that handle different aspects of the system.
#
# COMPONENTS:
#   - main.cpp             : Main entry point and signal handling
#   - Logger.cpp           : Thread-safe logging system
#   - Config.cpp           : JSON configuration file parsing
#   - ZmqManager.cpp       : ZeroMQ communication management
#   - UdpManager.cpp       : UDP communication management  
#   - TelemetryService.cpp : Main service coordination logic
# ============================================================================

# Create the telemetry service executable
add_executable(telemetry_service
  ${CMAKE_CURRENT_LIST_DIR}/main.cpp                 # Main entry point
  ${CMAKE_CURRENT_LIST_DIR}/Logger.cpp               # Logging system
  ${CMAKE_CURRENT_LIST_DIR}/Config.cpp               # Configuration management
  ${CMAKE_CURRENT_LIST_DIR}/ZmqManager.cpp           # ZeroMQ communications
  ${CMAKE_CURRENT_LIST_DIR}/UdpManager.cpp           # UDP communications
  ${CMAKE_CURRENT_LIST_DIR}/TelemetryService.cpp     # Main service logic
)

# Link required libraries using helper functions from main CMakeLists.txt
link_with_zmq(telemetry_service)      # ZeroMQ messaging library
link_with_boost(telemetry_service)    # Boost.Asio for UDP networking
link_with_json(telemetry_service)     # nlohmann/json for configuration parsing

# Place the executable in the source directory for easier development
set_target_to_source_dir(telemetry_service)
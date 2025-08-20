# ============================================================================
# TELEMETRY CLIENT LIBRARY BUILD CONFIGURATION
# ============================================================================
#
# This CMakeLists.txt builds a shared library (.so on Linux, .dll on Windows)
# that provides a simplified C++ API for communicating with the telemetry service.
#
# The library hides the complexity of ZeroMQ and Boost.Asio networking,
# providing a clean interface for developers.
# ============================================================================

cmake_minimum_required(VERSION 3.12)

# Create the shared library
add_library(telemetry_client SHARED
    ${CMAKE_CURRENT_LIST_DIR}/src/TelemetryClient.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/TelemetryClientAdvanced.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/DataAnalyzer.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/FleetManager.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/DataBuffer.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/MockUAV.cpp
)

# Set target properties for shared library
set_target_properties(telemetry_client PROPERTIES
    VERSION 1.0.0
    SOVERSION 1
    PUBLIC_HEADER ${CMAKE_CURRENT_LIST_DIR}/include/TelemetryClient.h
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

# Define export macro for symbol visibility
target_compile_definitions(telemetry_client PRIVATE TELEMETRY_CLIENT_EXPORTS)

# Include directories
target_include_directories(telemetry_client
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/src
)

# Link required libraries using helper functions from main CMakeLists.txt
link_with_zmq(telemetry_client)      # ZeroMQ for TCP communication
link_with_boost(telemetry_client)    # Boost.Asio for UDP communication
link_with_json(telemetry_client)     # nlohmann/json for configuration parsing

# Platform-specific settings
if(WIN32)
    # Windows-specific settings
    target_compile_definitions(telemetry_client PRIVATE _WIN32_WINNT=0x0A00)

    # Set output name for Windows DLL
    set_target_properties(telemetry_client PROPERTIES
        OUTPUT_NAME "telemetry_client"
        SUFFIX ".dll"
    )
else()
    # Linux/Unix-specific settings
    target_compile_options(telemetry_client PRIVATE -fPIC)

    # Set output name for Linux shared library
    set_target_properties(telemetry_client PROPERTIES
        OUTPUT_NAME "telemetry_client"
        PREFIX "lib"
        SUFFIX ".so"
    )
endif()

# Install configuration
install(TARGETS telemetry_client
    EXPORT TelemetryClientTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

# Note: Packaging removed - library used internally within same project

# Build examples if requested
option(BUILD_TELEMETRY_CLIENT_EXAMPLES "Build telemetry client examples" ON)
if(BUILD_TELEMETRY_CLIENT_EXAMPLES)
    add_subdirectory(examples)
endif()

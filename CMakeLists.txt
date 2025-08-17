# @file CMakeLists.txt
# @brief Main CMake configuration for the telemetry communication project
# 
# This file sets up the build system for a cross-platform telemetry service
# and associated applications. It handles dependency management, compiler
# configuration, and provides helper functions for consistent linking.

cmake_minimum_required(VERSION 3.12)
project(haberlesme_projesi CXX)

# Export compilation database for better IDE support (VS Code IntelliSense, etc.)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Use C++17 standard for modern language features
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Platform-specific compiler and API configurations
if (WIN32)
  # Target Windows 10 APIs and silence MSVC deprecation warnings for getenv usage
  add_compile_definitions(_WIN32_WINNT=0x0A00 _CRT_SECURE_NO_WARNINGS)
endif()

if (MSVC)
  # Ensure source files are interpreted as UTF-8 to fix unicode literal warnings
  add_compile_options(/utf-8)
endif()

# === Dependency Management ===

# Find Boost libraries (used for UDP networking via Boost.Asio)
# The 'system' component is required for network operations
set(Boost_NO_BOOST_CMAKE ON)
find_package(Boost 1.83.0 REQUIRED COMPONENTS system)

# Find threading library (required for multi-threaded operations)
find_package(Threads REQUIRED)

# Find nlohmann/json library (header-only JSON parsing library)
# Typically installed via vcpkg or system package manager
find_package(nlohmann_json CONFIG REQUIRED)

# Find ZeroMQ library (messaging library for inter-process communication)
# Handle different package configurations across platforms
find_package(ZeroMQ QUIET)
if(NOT ZeroMQ_FOUND)
  # Fallback: try to find libzmq directly
  find_library(ZMQ_LIB zmq)
  if(NOT ZMQ_LIB)
    message(FATAL_ERROR "ZeroMQ library not found. Install libzmq (e.g., libzmq3-dev) and headers.")
  endif()
endif()

# === Helper Functions ===

# @brief Helper function to link a target with ZeroMQ and threading libraries
# @param target The CMake target to link libraries to
# 
# This function handles the various ways ZeroMQ might be packaged across
# different platforms and package managers, providing a consistent interface.
function(link_with_zmq target)
  if(TARGET libzmq)
    target_link_libraries(${target} PRIVATE libzmq Threads::Threads)
  elseif(TARGET libzmq-static)
    target_link_libraries(${target} PRIVATE libzmq-static Threads::Threads)
  elseif(TARGET ZeroMQ::ZeroMQ)
    target_link_libraries(${target} PRIVATE ZeroMQ::ZeroMQ Threads::Threads)
  elseif(DEFINED ZMQ_LIB)
    target_link_libraries(${target} PRIVATE ${ZMQ_LIB} Threads::Threads)
  else()
    message(FATAL_ERROR "No ZeroMQ target/library available for target ${target}")
  endif()
endfunction()

# @brief Helper function to link a target with Boost libraries
# @param target The CMake target to link libraries to
# 
# Links the target with Boost system library and sets up include directories.
# Used for UDP communication via Boost.Asio.
function(link_with_boost target)
    # Link against the found Boost libraries and include directories
    target_include_directories(${target} PRIVATE ${Boost_INCLUDE_DIRS})
    target_link_libraries(${target} PRIVATE ${Boost_LIBRARIES})
endfunction()

# @brief Helper function to link a target with nlohmann/json library
# @param target The CMake target to link libraries to
# 
# Links the header-only JSON library. Uses the modern CMake interface
# target if available.
function(link_with_json target)
  if (TARGET nlohmann_json::nlohmann_json)
    target_link_libraries(${target} PRIVATE nlohmann_json::nlohmann_json)
  endif()
endfunction()

# @brief Helper function to place executables in their source directories
# @param target The CMake target to configure
# 
# This function sets the runtime output directory to be the same as the
# source directory, making it easier to run executables from their
# component directories during development.
function(set_target_to_source_dir target)
  get_target_property(srcs ${target} SOURCES)
  if(NOT srcs)
    return()
  endif()
  
  # Get the directory of the first source file
  list(GET srcs 0 first_src)
  get_filename_component(src_dir ${first_src} DIRECTORY)
  
  # Set output directory for all build configurations
  set_target_properties(${target} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${src_dir}
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${src_dir}
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${src_dir}
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${src_dir}
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL ${src_dir}
  )
endfunction()

# === Subdirectory Configuration ===

# Build each component in its own subdirectory
# Each subdirectory contains its own CMakeLists.txt with specific build rules
add_subdirectory(telemetry_service)  # Main telemetry service
add_subdirectory(uav_sim)           # UAV simulator
add_subdirectory(camera_ui)         # Camera UI application
add_subdirectory(mapping_ui)        # Mapping UI application

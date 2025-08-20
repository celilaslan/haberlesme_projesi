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

# Build configuration options
option(BUILD_WITH_DEBUG_INFO "Include debug information in Release builds" ON)
option(ENABLE_WARNINGS "Enable additional compiler warnings" ON)
option(TREAT_WARNINGS_AS_ERRORS "Treat warnings as compilation errors" OFF)

# Set default build type if not specified
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of build" FORCE)
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()

# Configure compiler warnings and optimizations
if(ENABLE_WARNINGS)
  if(MSVC)
    add_compile_options(/W4)
    if(TREAT_WARNINGS_AS_ERRORS)
      add_compile_options(/WX)
    endif()
  else()
    add_compile_options(-Wall -Wextra -Wpedantic)
    if(TREAT_WARNINGS_AS_ERRORS)
      add_compile_options(-Werror)
    endif()
  endif()
endif()

# Add debug information to Release builds if requested
if(BUILD_WITH_DEBUG_INFO AND CMAKE_BUILD_TYPE STREQUAL "Release")
  if(MSVC)
    add_compile_options(/Zi)
    add_link_options(/DEBUG)
  else()
    add_compile_options(-g)
  endif()
endif()

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

# Check for required compiler features
include(CheckCXXCompilerFlag)
include(CheckIncludeFileCXX)

# Verify C++17 features are available
check_cxx_compiler_flag("-std=c++17" COMPILER_SUPPORTS_CXX17)
if(NOT COMPILER_SUPPORTS_CXX17 AND NOT MSVC)
  message(FATAL_ERROR "The compiler ${CMAKE_CXX_COMPILER} has no C++17 support. Please use a different C++ compiler.")
endif()

# Check for atomic operations support (required for thread safety)
check_include_file_cxx("atomic" HAVE_ATOMIC_HEADER)
if(NOT HAVE_ATOMIC_HEADER)
  message(FATAL_ERROR "C++ atomic operations not available. Required for thread-safe operations.")
endif()

# Find Boost libraries (used for UDP networking via Boost.Asio)
# The 'system' component is required for network operations
set(Boost_NO_BOOST_CMAKE ON)
find_package(Boost 1.70.0 REQUIRED COMPONENTS system)

# Validate Boost version for compatibility
if(Boost_VERSION_STRING VERSION_LESS "1.70.0")
  message(WARNING "Boost version ${Boost_VERSION_STRING} detected. Version 1.70.0 or newer is recommended for optimal compatibility.")
endif()

# Add modern CMake target if not available
if(NOT TARGET Boost::system)
  add_library(Boost::system INTERFACE IMPORTED)
  set_target_properties(Boost::system PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${Boost_INCLUDE_DIRS}"
    INTERFACE_LINK_LIBRARIES "${Boost_SYSTEM_LIBRARY}"
  )
endif()

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
# Used for UDP communication via Boost.Asio. Prefers modern CMake targets.
function(link_with_boost target)
  if(TARGET Boost::system)
    # Use modern CMake target (preferred)
    target_link_libraries(${target} PRIVATE Boost::system)
  else()
    # Fallback to legacy variables
    target_include_directories(${target} PRIVATE ${Boost_INCLUDE_DIRS})
    target_link_libraries(${target} PRIVATE ${Boost_LIBRARIES})
  endif()

  # Add compile definitions for Boost.Asio
  target_compile_definitions(${target} PRIVATE
    BOOST_ASIO_STANDALONE
    BOOST_ASIO_DISABLE_DEPRECATED
  )
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
add_subdirectory(telemetry_client_library)  # Telemetry client shared library

# === Installation Configuration ===

# Set installation directories following GNU conventions
include(GNUInstallDirs)

# Install executables
install(TARGETS telemetry_service uav_sim camera_ui mapping_ui
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  COMPONENT Runtime
)

# Install configuration files
install(FILES service_config.json
  DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/telemetry_service
  COMPONENT Configuration
)

# Install systemd service file (Linux only)
if(UNIX AND NOT APPLE)
  install(FILES telemetry_service/telemetry_service.service
    DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/systemd/system
    COMPONENT SystemdService
  )
endif()

# Create installation package components
set(CPACK_PACKAGE_NAME "telemetry-service")
set(CPACK_PACKAGE_VERSION_MAJOR 1)
set(CPACK_PACKAGE_VERSION_MINOR 0)
set(CPACK_PACKAGE_VERSION_PATCH 0)
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Multi-UAV Telemetry Service")
set(CPACK_PACKAGE_VENDOR "Telemetry Project")

# Configure package components
set(CPACK_COMPONENTS_ALL Runtime Configuration SystemdService TelemetryClientLibrary)
set(CPACK_COMPONENT_RUNTIME_DISPLAY_NAME "Telemetry Service Executables")
set(CPACK_COMPONENT_CONFIGURATION_DISPLAY_NAME "Configuration Files")
set(CPACK_COMPONENT_SYSTEMDSERVICE_DISPLAY_NAME "SystemD Service Files")
set(CPACK_COMPONENT_TELEMETRYCLIENTLIBRARY_DISPLAY_NAME "Telemetry Client Library")

include(CPack)

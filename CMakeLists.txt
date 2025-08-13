cmake_minimum_required(VERSION 3.12)
project(haberlesme_projesi CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Use the module-based find for Boost.
# We look for the 'system' component, as 'asio' is often header-only.
set(Boost_NO_BOOST_CMAKE ON)
find_package(Boost 1.83.0 REQUIRED COMPONENTS system)

find_package(Threads REQUIRED)

# Try to find ZeroMQ (imported target varies across platforms)
find_package(ZeroMQ QUIET)
if(NOT ZeroMQ_FOUND)
  find_library(ZMQ_LIB zmq)
  if(NOT ZMQ_LIB)
    message(FATAL_ERROR "ZeroMQ library not found. Install libzmq (e.g., libzmq3-dev) and headers.")
  endif()
endif()

# Helper to link a target with ZeroMQ and Threads portably
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

# Helper to link with Boost libraries
function(link_with_boost target)
    # Link against the found Boost libraries and include directories
    target_include_directories(${target} PRIVATE ${Boost_INCLUDE_DIRS})
    target_link_libraries(${target} PRIVATE ${Boost_LIBRARIES})
endfunction()

# Helper to place runtime next to the source directory (as requested)
function(set_target_to_source_dir target)
  get_target_property(srcs ${target} SOURCES)
  if(NOT srcs)
    return()
  endif()
  list(GET srcs 0 first_src)
  get_filename_component(src_dir ${first_src} DIRECTORY)
  set_target_properties(${target} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${src_dir}
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${src_dir}
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${src_dir}
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${src_dir}
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL ${src_dir}
  )
endfunction()

add_subdirectory(telemetry_service)
add_subdirectory(uav_sim)
add_subdirectory(camera_ui)
add_subdirectory(mapping_ui)

# ============================================================================
# TELEMETRY CLIENT LIBRARY EXAMPLES
# ============================================================================
#
# This CMakeLists.txt builds example applications that demonstrate how to use
# the telemetry client library.
# ============================================================================

# Simple telemetry receiver example
add_executable(simple_receiver
    ${CMAKE_CURRENT_LIST_DIR}/simple_receiver.cpp
)

target_link_libraries(simple_receiver
    PRIVATE telemetry_client
)

target_include_directories(simple_receiver
    PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../include
)

# Advanced client example with filtering and commands
add_executable(advanced_client
    ${CMAKE_CURRENT_LIST_DIR}/advanced_client.cpp
)

target_link_libraries(advanced_client
    PRIVATE telemetry_client
)

target_include_directories(advanced_client
    PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../include
)

# UAV command sender example
add_executable(command_sender
    ${CMAKE_CURRENT_LIST_DIR}/command_sender.cpp
)

target_link_libraries(command_sender
    PRIVATE telemetry_client
)

target_include_directories(command_sender
    PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../include
)

# Advanced telemetry client example with all enhanced features
add_executable(advanced_telemetry_client
    ${CMAKE_CURRENT_LIST_DIR}/advanced_telemetry_client.cpp
)

target_link_libraries(advanced_telemetry_client
    PRIVATE telemetry_client
)

target_include_directories(advanced_telemetry_client
    PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../include
)

# Place examples in their source directory
set_target_to_source_dir(simple_receiver)
set_target_to_source_dir(advanced_client)
set_target_to_source_dir(command_sender)
set_target_to_source_dir(advanced_telemetry_client)
set_target_to_source_dir(command_sender)

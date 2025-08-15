# telemetry_service
add_executable(telemetry_service
  ${CMAKE_CURRENT_LIST_DIR}/telemetry_service.cpp
  ${CMAKE_CURRENT_LIST_DIR}/Logger.cpp
  ${CMAKE_CURRENT_LIST_DIR}/Config.cpp
  ${CMAKE_CURRENT_LIST_DIR}/ZmqManager.cpp
  ${CMAKE_CURRENT_LIST_DIR}/UdpManager.cpp
  ${CMAKE_CURRENT_LIST_DIR}/TelemetryService.cpp
)
link_with_zmq(telemetry_service)
link_with_boost(telemetry_service)
link_with_json(telemetry_service)
set_target_to_source_dir(telemetry_service)
# camera_ui
add_executable(camera_ui
  ${CMAKE_CURRENT_LIST_DIR}/camera_ui.cpp
)
link_with_zmq(camera_ui)
set_target_to_source_dir(camera_ui)

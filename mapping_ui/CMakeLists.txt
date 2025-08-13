# mapping_ui
add_executable(mapping_ui
  ${CMAKE_CURRENT_LIST_DIR}/mapping_ui.cpp
)
link_with_zmq(mapping_ui)
set_target_to_source_dir(mapping_ui)

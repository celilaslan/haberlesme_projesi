# uav_sim
add_executable(uav_sim
  ${CMAKE_CURRENT_LIST_DIR}/uav_sim.cpp
)
link_with_zmq(uav_sim)
link_with_boost(uav_sim)
set_target_to_source_dir(uav_sim)

# Layer 5: automation
cmake_minimum_required(VERSION 3.20)

add_library(layer5_automation STATIC 
    iautomation_lane_manager.h
    iautomation_lane.h
    automation_lane_impl.cpp
    automation_lane_manager_impl.cpp
    iautomation_capture_engine.h
    automation_capture_engine_impl.h
    automation_capture_engine_impl.cpp
)

target_include_directories(layer5_automation PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(layer5_automation PUBLIC 
    common_utils
)

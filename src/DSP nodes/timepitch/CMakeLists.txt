# Layer4: timepitch
cmake_minimum_required(VERSION 3.20)

add_library(layer4_timepitch STATIC
    time_pitch_node.h
    time_pitch_node.cpp
    sound_touch_node.h
    sound_touch_node.cpp
)

target_include_directories(layer4_timepitch PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(layer4_timepitch PUBLIC 
    common_utils 
    soundtouch_lib
    rubberband_lib
)

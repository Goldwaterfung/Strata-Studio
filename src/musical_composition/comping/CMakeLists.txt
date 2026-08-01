# Layer 5: comping
cmake_minimum_required(VERSION 3.20)

add_library(layer5_comping STATIC 
    icomping_engine.h
    comping_engine_impl.cpp
    take_lane.h
    crossfade_generator.h
)

target_include_directories(layer5_comping PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(layer5_comping PUBLIC 
    common_utils
    layer5_playlist
)

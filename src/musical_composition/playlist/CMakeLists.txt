# Layer 5: playlist
cmake_minimum_required(VERSION 3.20)

add_library(layer5_playlist STATIC 
    iplaylist.h
    playlist_impl.h
    playlist_impl.cpp
    region_operations.h
)

target_include_directories(layer5_playlist PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(layer5_playlist PUBLIC 
    common_utils
    layer5_command_history
)

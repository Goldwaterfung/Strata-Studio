# Layer4: channelstrip
cmake_minimum_required(VERSION 3.20)

add_library(layer4_channelstrip STATIC 
    channel_strip_node.h
    channel_strip_node.cpp
)

target_include_directories(layer4_channelstrip PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(layer4_channelstrip PUBLIC common_utils)

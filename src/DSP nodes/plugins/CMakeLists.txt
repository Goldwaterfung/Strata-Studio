# Layer4: plugins
cmake_minimum_required(VERSION 3.20)

add_library(layer4_plugins STATIC
    insert_plugin_node.h
    insert_plugin_node.cpp
    plugin_slot_node.h
    plugin_slot_node.cpp
    instrument_slot_node.h
    instrument_slot_node.cpp
)

target_include_directories(layer4_plugins PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(layer4_plugins PUBLIC 
    common_utils
    layer3_plugin
)

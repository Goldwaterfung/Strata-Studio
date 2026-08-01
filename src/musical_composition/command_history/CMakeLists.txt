# Layer 5: command_history
cmake_minimum_required(VERSION 3.20)

add_library(layer5_command_history STATIC 
    icommand_history.h
    command_history_impl.h
    command_history_impl.cpp
    delta_primitives.h
)

target_include_directories(layer5_command_history PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(layer5_command_history PUBLIC 
    common_utils
)

# Layer2: clock
cmake_minimum_required(VERSION 3.20)

add_library(layer2_clock STATIC
    iclock_service.h
    clock_service.cpp
)

target_include_directories(layer2_clock 
    PUBLIC 
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${PROJECT_SOURCE_DIR}/src/common
)

target_link_libraries(layer2_clock 
    PUBLIC 
        layer1_interface
)

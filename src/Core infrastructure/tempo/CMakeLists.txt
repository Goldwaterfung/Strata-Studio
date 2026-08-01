# src/Core infrastructure/tempo/CMakeLists.txt
# Layer 2: Tempo Service

cmake_minimum_required(VERSION 3.20)

add_library(layer2_tempo INTERFACE)
add_library(${PROJECT_NAMESPACE}::layer2_tempo ALIAS layer2_tempo)

target_include_directories(layer2_tempo INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src/common
)

target_link_libraries(layer2_tempo INTERFACE
    common_utils
    layer2_state
)

# Source files
target_sources(layer2_tempo INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/itempo_service.h
    ${CMAKE_CURRENT_SOURCE_DIR}/tempo_service.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tempo_map.h
    ${CMAKE_CURRENT_SOURCE_DIR}/tempo_map.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/meter_map.h
    ${CMAKE_CURRENT_SOURCE_DIR}/meter_map.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/time_converter.h
    ${CMAKE_CURRENT_SOURCE_DIR}/time_converter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/bbt_calculator.h
)


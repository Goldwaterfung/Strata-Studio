# Layer3: sidechain
cmake_minimum_required(VERSION 3.20)

add_library(layer3_sidechain INTERFACE)

target_include_directories(layer3_sidechain INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src/common
    ${CMAKE_SOURCE_DIR}/src/Core\ infrastructure
)

target_link_libraries(layer3_sidechain INTERFACE
    common_utils
)

target_compile_features(layer3_sidechain INTERFACE cxx_std_20)

# Source files for sidechain manager
set(SIDECHAIN_SOURCES
    sidechain_manager_impl.cpp
)

# Create implementation library
add_library(layer3_sidechain_impl STATIC ${SIDECHAIN_SOURCES})
target_link_libraries(layer3_sidechain_impl PUBLIC
    layer3_sidechain
)
target_include_directories(layer3_sidechain_impl PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src/common
    ${CMAKE_SOURCE_DIR}/src/Core\ infrastructure
)

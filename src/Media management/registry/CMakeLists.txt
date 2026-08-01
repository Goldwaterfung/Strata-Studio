# Layer 6: Media Registry
cmake_minimum_required(VERSION 3.20)

add_library(layer6_registry STATIC
    media_primitives.h
    imedia_registry.h
    media_registry_impl.h
    media_registry_impl.cpp
)

target_include_directories(layer6_registry PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(layer6_registry PUBLIC 
    common_utils
)

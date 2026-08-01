# Layer 6: Export Service
cmake_minimum_required(VERSION 3.20)

add_library(layer6_export STATIC
    export_primitives.h
    iexport_service.h
    export_service_impl.cpp
)

# Find libebur128 via vcpkg/system paths
find_path(EBUR128_INCLUDE_DIR NAMES ebur128.h)
find_library(EBUR128_LIBRARY NAMES ebur128 libebur128)

target_include_directories(layer6_export PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${EBUR128_INCLUDE_DIR}
)

target_link_libraries(layer6_export PUBLIC 
    common_utils
    layer6_registry
    layer6_codecs
    ${EBUR128_LIBRARY}
)

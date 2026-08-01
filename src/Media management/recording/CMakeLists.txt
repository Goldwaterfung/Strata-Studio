# Layer 6: Recording Service
cmake_minimum_required(VERSION 3.20)

add_library(layer6_recording STATIC
    idisk_writer_service.h
    disk_writer_service_impl.h
    disk_writer_service_impl.cpp
)

target_include_directories(layer6_recording PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(layer6_recording PUBLIC 
    common_utils
    layer6_codecs
    layer2
)

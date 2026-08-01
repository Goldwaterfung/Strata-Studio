# src/Core audio engine/transport/CMakeLists.txt

add_library(layer3_transport_interface INTERFACE)
target_sources(layer3_transport_interface INTERFACE
    itransport.h
)
target_include_directories(layer3_transport_interface INTERFACE 
    ${CMAKE_CURRENT_SOURCE_DIR}
)

add_library(layer3_transport STATIC
    transport_impl.cpp
)

target_link_libraries(layer3_transport PUBLIC
    layer3_transport_interface
    layer2_interface
    layer1_interface
)

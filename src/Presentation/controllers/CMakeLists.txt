# Layer7: controllers
cmake_minimum_required(VERSION 3.20)

add_library(layer7_controllers STATIC
    presentation_director.cpp
)

target_include_directories(layer7_controllers PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(layer7_controllers PUBLIC common_utils layer7_views Qt6::Widgets Qt6::Core Qt6::Gui Qt6::Svg)

# Layer 6: Project Browser
cmake_minimum_required(VERSION 3.20)

add_library(layer6_browser STATIC
    import_primitives.h
    iproject_browser.h
    project_browser_impl.h
    project_browser_impl.cpp
    ipreview_pipeline_builder.h
)

target_include_directories(layer6_browser PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(layer6_browser PUBLIC 
    common_utils
    layer2
    layer6_registry
    layer6_codecs
    layer6_analysis
    layer6_intake
)

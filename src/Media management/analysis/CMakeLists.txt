# Layer 6: Audio Analysis
cmake_minimum_required(VERSION 3.20)

add_library(layer6_analysis STATIC
    analysis_primitives.h
    iaudio_analysis_engine.h
    audio_analysis_engine_impl.h
    audio_analysis_engine_impl.cpp
    bpm_analyzer.h
    bpm_analyzer.cpp
)

# Find libebur128 via vcpkg/system paths
find_path(EBUR128_INCLUDE_DIR NAMES ebur128.h)
find_library(EBUR128_LIBRARY NAMES ebur128 libebur128)

target_include_directories(layer6_analysis PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${EBUR128_INCLUDE_DIR}
    ${CMAKE_SOURCE_DIR}/src/third_party/eigen-5.0.0
)

target_link_libraries(layer6_analysis PUBLIC 
    common_utils
    layer6_registry
    layer6_codecs
    soundtouch_analysis
    ${EBUR128_LIBRARY}
)

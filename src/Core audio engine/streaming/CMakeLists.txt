# Layer3: streaming
cmake_minimum_required(VERSION 3.20)

add_library(layer3_streaming STATIC
    ibutler_thread.h
    streaming_buffer_impl.h
    streaming_buffer_impl.cpp
    butler_thread_impl.h
    butler_thread_impl.cpp
    pre_baker.h
    pre_baker.cpp
)

target_include_directories(layer3_streaming 
    PUBLIC 
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${PROJECT_SOURCE_DIR}/src/common
)

find_package(SndFile REQUIRED)
find_package(SampleRate REQUIRED)

target_link_libraries(layer3_streaming 
    PUBLIC 
        common_utils
        layer2
        rubberband_lib
        SndFile::sndfile
        SampleRate::samplerate
)

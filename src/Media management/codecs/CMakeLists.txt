# Layer6: codecs
cmake_minimum_required(VERSION 3.20)

add_library(layer6_codecs STATIC
    codec_factory.cpp
)

target_include_directories(layer6_codecs PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
)

# SndFile is provided via vcpkg
find_package(SndFile REQUIRED)

target_link_libraries(layer6_codecs PUBLIC 
    common_utils 
    SndFile::sndfile
)

# Header-only for now, but we list them for IDE visibility
# icodec_reader.h
# icodec_writer.h
# sndfile_reader.h
# sndfile_writer.h

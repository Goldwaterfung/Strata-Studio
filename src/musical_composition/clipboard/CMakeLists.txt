# Layer 5: clipboard
cmake_minimum_required(VERSION 3.20)

add_library(layer5_clipboard STATIC 
    iclipboard.h
    clipboard_impl.h
    clipboard_impl.cpp
)

target_include_directories(layer5_clipboard PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(layer5_clipboard PUBLIC 
    common_utils
)

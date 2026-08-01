# Layer 6: Sample Library
cmake_minimum_required(VERSION 3.20)

add_library(layer6_library STATIC
    isample_library_browser.h
    sample_library_browser_impl.cpp
)

target_include_directories(layer6_library PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
)

# SQLite from vcpkg
find_package(unofficial-sqlite3 CONFIG REQUIRED)

target_link_libraries(layer6_library PUBLIC 
    common_utils
    unofficial::sqlite3::sqlite3
)

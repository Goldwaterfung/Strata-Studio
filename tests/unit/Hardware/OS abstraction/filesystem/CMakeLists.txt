# Filesystem Hardware Unit Tests
cmake_minimum_required(VERSION 3.20)

add_executable(filesystem_unit_tests
    filesystem_test.cpp
)

target_link_libraries(filesystem_unit_tests PRIVATE
    layer1_filesystem
    Catch2::Catch2WithMain
)

target_include_directories(filesystem_unit_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

include(Catch)
catch_discover_tests(filesystem_unit_tests)

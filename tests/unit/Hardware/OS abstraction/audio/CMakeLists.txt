# Audio Hardware Unit Tests
cmake_minimum_required(VERSION 3.20)

add_executable(audio_unit_tests
    audio_driver_test.cpp
)

target_link_libraries(audio_unit_tests PRIVATE
    layer1_audio
    Catch2::Catch2WithMain
)

target_include_directories(audio_unit_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

# Register tests with CTest
include(Catch)
catch_discover_tests(audio_unit_tests)

# MIDI Hardware Unit Tests
cmake_minimum_required(VERSION 3.20)

add_executable(midi_unit_tests
    midi_driver_test.cpp
)

target_link_libraries(midi_unit_tests PRIVATE
    layer1_midi
    Catch2::Catch2WithMain
)

target_include_directories(midi_unit_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

include(Catch)
catch_discover_tests(midi_unit_tests)

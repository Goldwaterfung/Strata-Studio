# tests/unit/Core audio engine/streaming/CMakeLists.txt

add_executable(streaming_test streaming_test.cpp)

target_link_libraries(streaming_test PRIVATE
    layer3_streaming
    layer1_filesystem
    layer2_memory
    Catch2::Catch2WithMain
    spdlog::spdlog
)

target_include_directories(streaming_test PRIVATE
    ${PROJECT_SOURCE_DIR}/src
)

# Set IDE folder
set_target_properties(streaming_test PROPERTIES FOLDER "Tests/Core audio engine")

# Add to CTest
add_test(NAME streaming_test COMMAND streaming_test)

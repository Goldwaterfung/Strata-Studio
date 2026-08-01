# tests/unit/Core infrastructure/memory/CMakeLists.txt

add_executable(memory_test
    memory_test.cpp
    "${CMAKE_SOURCE_DIR}/src/Core infrastructure/memory/buffer_pool.cpp"
    "${CMAKE_SOURCE_DIR}/src/Core infrastructure/memory/priority_pool.cpp"
    "${CMAKE_SOURCE_DIR}/src/Core infrastructure/memory/memory_coordinator.cpp"
    "${CMAKE_SOURCE_DIR}/src/Core infrastructure/memory/aligned_allocator.cpp"
    "${CMAKE_SOURCE_DIR}/src/Core infrastructure/memory/audio_buffer_handle.cpp"
)

target_include_directories(memory_test PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_SOURCE_DIR}/src/Core infrastructure/memory"
)

target_link_libraries(memory_test PRIVATE
    Catch2::Catch2WithMain
    layer1_threading
    layer1_common
    Threads::Threads
)

add_test(NAME memory_test COMMAND memory_test)

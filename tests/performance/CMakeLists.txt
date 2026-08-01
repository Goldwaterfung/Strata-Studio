# Performance Tests (disabled by default)
cmake_minimum_required(VERSION 3.20)

if(BUILD_PERFORMANCE_TESTS)
    add_executable(performance_tests
        primitive_benchmarks.cpp
    )

    target_link_libraries(performance_tests PRIVATE
        common_utils
        benchmark::benchmark
        benchmark::benchmark_main
    )

    target_include_directories(performance_tests PRIVATE
        ${CMAKE_SOURCE_DIR}/src
    )
endif()

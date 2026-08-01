# clock unit tests
add_executable(clock_test clock_test.cpp)

target_link_libraries(clock_test 
    PRIVATE 
        Catch2::Catch2WithMain
        layer2_clock
)

add_test(NAME clock_test COMMAND clock_test)

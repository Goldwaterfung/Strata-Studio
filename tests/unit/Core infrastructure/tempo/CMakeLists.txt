# tempo unit tests
add_executable(tempo_test tempo_test.cpp)

target_link_libraries(tempo_test 
    PRIVATE 
        Catch2::Catch2WithMain
        layer2_tempo
)

add_test(NAME tempo_test COMMAND tempo_test)

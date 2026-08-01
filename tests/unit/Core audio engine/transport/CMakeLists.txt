# tests/unit/Core audio engine/transport/CMakeLists.txt

add_executable(transport_test transport_test.cpp)
target_link_libraries(transport_test PRIVATE
    Catch2::Catch2WithMain
    layer3_transport
    layer2_tempo
    layer2_interface
)

add_test(NAME transport_test COMMAND transport_test)

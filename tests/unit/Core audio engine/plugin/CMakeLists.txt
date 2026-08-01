# tests/unit/Core audio engine/plugin/CMakeLists.txt

add_executable(plugin_test plugin_test.cpp)
target_link_libraries(plugin_test PRIVATE
    Catch2::Catch2WithMain
    layer3_plugin
    layer2
)

add_test(NAME plugin_test COMMAND plugin_test)

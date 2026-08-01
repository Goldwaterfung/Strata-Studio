# plugin unit tests
add_executable(plugin_category_test plugin_category_test.cpp)

target_include_directories(plugin_category_test PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)

target_link_libraries(plugin_category_test 
    PRIVATE 
        Catch2::Catch2WithMain
        layer2_plugins
        layer1_threading
        layer1_common
        Threads::Threads
)

add_test(NAME plugin_category_test COMMAND plugin_category_test)

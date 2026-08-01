# tests/unit/Core audio engine/sidechain/CMakeLists.txt

add_executable(sidechain_test sidechain_test.cpp)

target_link_libraries(sidechain_test PRIVATE
    layer3_sidechain_impl
    layer3_scheduler
    layer2_bridges
    Catch2::Catch2WithMain
    spdlog::spdlog
)

target_include_directories(sidechain_test PRIVATE
    ${PROJECT_SOURCE_DIR}/src
)

# Set IDE folder
set_target_properties(sidechain_test PROPERTIES FOLDER "Tests/Core audio engine")

# Add to CTest
add_test(NAME sidechain_test COMMAND sidechain_test)

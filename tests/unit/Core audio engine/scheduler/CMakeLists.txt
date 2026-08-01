# tests/unit/Core audio engine/scheduler/CMakeLists.txt

add_executable(topological_sort_test topological_sort_test.cpp)
target_link_libraries(topological_sort_test PRIVATE
    Catch2::Catch2WithMain
    layer3_scheduler
)
add_test(NAME topological_sort_test COMMAND topological_sort_test)

add_executable(parameter_mapping_test parameter_mapping_test.cpp)
target_link_libraries(parameter_mapping_test PRIVATE
    Catch2::Catch2WithMain
    layer3_scheduler
    layer2_bridges
)
add_test(NAME parameter_mapping_test COMMAND parameter_mapping_test)

add_executable(rt_safety_test rt_safety_test.cpp)
target_link_libraries(rt_safety_test PRIVATE
    Catch2::Catch2WithMain
    layer3_scheduler
    layer2_bridges
)
add_test(NAME rt_safety_test COMMAND rt_safety_test)

add_executable(multithreading_test multithreading_test.cpp)
target_link_libraries(multithreading_test PRIVATE
    Catch2::Catch2WithMain
    layer3_scheduler
    layer2_bridges
)
add_test(NAME multithreading_test COMMAND multithreading_test)

add_executable(timeline_snapshot_test timeline_snapshot_test.cpp)
target_link_libraries(timeline_snapshot_test PRIVATE
    Catch2::Catch2WithMain
    layer3_scheduler
    layer2_bridges
)
add_test(NAME timeline_snapshot_test COMMAND timeline_snapshot_test)

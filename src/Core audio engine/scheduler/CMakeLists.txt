# src/Core audio engine/scheduler/CMakeLists.txt

#-------------------------------------------------------------------------------
# Scheduler Interface (headers only)
#-------------------------------------------------------------------------------

add_library(layer3_scheduler_interface INTERFACE)
target_sources(layer3_scheduler_interface INTERFACE
    idsp_kernel.h
)
target_include_directories(layer3_scheduler_interface INTERFACE 
    ${CMAKE_CURRENT_SOURCE_DIR}
)

#-------------------------------------------------------------------------------
# Scheduler Implementation
#-------------------------------------------------------------------------------

add_library(layer3_scheduler STATIC
    scheduler_impl.cpp
    topological_sort.cpp
    offline_dsp_thread_pool.cpp
)

target_link_libraries(layer3_scheduler PUBLIC
    layer3_scheduler_interface
    layer2_interface
    layer1_interface
)

if(APPLE)
    target_link_libraries(layer3_scheduler PRIVATE "-framework Accelerate")
endif()



# src/Core infrastructure/memory/CMakeLists.txt
# Layer 2: Memory Management

cmake_minimum_required(VERSION 3.20)

add_library(layer2_memory STATIC)
add_library(${PROJECT_NAMESPACE}::layer2_memory ALIAS layer2_memory)

target_include_directories(layer2_memory PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src/common
)

target_link_libraries(layer2_memory PUBLIC
    common_utils
    layer1_threading
)

# Memory locking support
if(UNIX)
    target_compile_definitions(layer2_memory PUBLIC HAVE_MLOCK)
elseif(WIN32)
    target_compile_definitions(layer2_memory PUBLIC HAVE_VIRTUALLOCK)
endif()

# Source files
target_sources(layer2_memory PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/imemory_coordinator.h
    ${CMAKE_CURRENT_SOURCE_DIR}/memory_coordinator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/buffer_pool.h
    ${CMAKE_CURRENT_SOURCE_DIR}/buffer_pool.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/priority_pool.h
    ${CMAKE_CURRENT_SOURCE_DIR}/priority_pool.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/aligned_allocator.h
    ${CMAKE_CURRENT_SOURCE_DIR}/aligned_allocator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/audio_buffer_handle.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/istring_registry.h
    ${CMAKE_CURRENT_SOURCE_DIR}/string_registry_impl.cpp
)

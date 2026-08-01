# CMakeLists.txt
# Layer 1: Hardware/OS Abstraction - Threading Subsystem

cmake_minimum_required(VERSION 3.20)

# =============================================================================
# THREADING SUBSYSTEM INTERFACE
# =============================================================================

add_library(layer1_threading_interface INTERFACE)
target_include_directories(layer1_threading_interface INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_sources(layer1_threading_interface INTERFACE
    ithread_manager.h
    thread_utils.h
)
target_compile_features(layer1_threading_interface INTERFACE cxx_std_20)

# =============================================================================
# PLATFORM-SPECIFIC IMPLEMENTATIONS
# =============================================================================

set(LAYER1_THREADING_SOURCES 
    thread_manager_base.cpp
    thread_manager_factory.cpp
)
set(LAYER1_THREADING_LIBS "")

# Detect platform and add appropriate sources
if(WIN32)
    # Windows implementations
    list(APPEND LAYER1_THREADING_SOURCES
        windows/windows_thread_manager.cpp
    )
    list(APPEND LAYER1_THREADING_LIBS
        avrt
    )
    target_compile_definitions(layer1_threading_interface INTERFACE WINDOWS_THREADING=1)

elseif(APPLE)
    # macOS implementations
    list(APPEND LAYER1_THREADING_SOURCES
        macos/macos_thread_manager.cpp
    )
    list(APPEND LAYER1_THREADING_LIBS
        pthread
    )
    target_compile_definitions(layer1_threading_interface INTERFACE MACOS_THREADING=1)

endif()

# =============================================================================
# THREADING LIBRARY
# =============================================================================

add_library(layer1_threading STATIC ${LAYER1_THREADING_SOURCES})
target_link_libraries(layer1_threading PUBLIC
    layer1_threading_interface
    layer1_common
    ${LAYER1_THREADING_LIBS}
)

# Threading factory is now included in LAYER1_THREADING_SOURCES

# =============================================================================
# INSTALLATION
# =============================================================================

if(INSTALL_DEVELOPMENT_FILES)
    install(TARGETS layer1_threading
        ARCHIVE DESTINATION lib COMPONENT Development
        LIBRARY DESTINATION lib COMPONENT Development
    )

    install(FILES ithread_manager.h
        DESTINATION include/layer1/threading COMPONENT Development
    )
endif()

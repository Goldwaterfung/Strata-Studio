# CMakeLists.txt
# Layer 1: Hardware/OS Abstraction - Filesystem Subsystem

cmake_minimum_required(VERSION 3.20)

# =============================================================================
# FILESYSTEM SUBSYSTEM INTERFACE
# =============================================================================

add_library(layer1_filesystem_interface INTERFACE)
target_include_directories(layer1_filesystem_interface INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_sources(layer1_filesystem_interface INTERFACE
    ifile_system.h
    file_buffer_pool.h
    async_file_operation.h
)
target_compile_features(layer1_filesystem_interface INTERFACE cxx_std_20)

# =============================================================================
# PLATFORM-SPECIFIC IMPLEMENTATIONS
# =============================================================================

set(LAYER1_FILESYSTEM_SOURCES 
    std_file_system.cpp
    filesystem_factory.cpp
    codecs/wav_codec.cpp
    codecs/flac_codec.cpp
    codecs/mp3_codec.cpp
)
set(LAYER1_FILESYSTEM_LIBS "")

if(WIN32)
    list(APPEND LAYER1_FILESYSTEM_SOURCES
        workers/windows_io_worker.cpp
    )
    target_compile_definitions(layer1_filesystem_interface INTERFACE WINDOWS_FILESYSTEM=1)

elseif(APPLE)
    list(APPEND LAYER1_FILESYSTEM_SOURCES
        workers/macos_io_worker.cpp
    )
    list(APPEND LAYER1_FILESYSTEM_LIBS
        "-framework Foundation"
    )
    target_compile_definitions(layer1_filesystem_interface INTERFACE MACOS_FILESYSTEM=1)

endif()

# =============================================================================
# FILESYSTEM LIBRARY
# =============================================================================

add_library(layer1_filesystem STATIC ${LAYER1_FILESYSTEM_SOURCES})
target_link_libraries(layer1_filesystem PUBLIC
    layer1_filesystem_interface
    layer1_common
    ${LAYER1_FILESYSTEM_LIBS}
)

# Filesystem factory
# target_sources(layer1_filesystem PRIVATE filesystem_factory.cpp)

# =============================================================================
# AUDIO CODEC SUPPORT (Optional)
# =============================================================================

if(TARGET libsndfile::sndfile)
    target_link_libraries(layer1_filesystem PRIVATE libsndfile::sndfile)
    target_compile_definitions(layer1_filesystem INTERFACE HAVE_SNDFILE=1)
endif()

# =============================================================================
# INSTALLATION
# =============================================================================

if(INSTALL_DEVELOPMENT_FILES)
    install(TARGETS layer1_filesystem
        ARCHIVE DESTINATION lib COMPONENT Development
        LIBRARY DESTINATION lib COMPONENT Development
    )

    install(FILES ifile_system.h
        DESTINATION include/layer1/filesystem COMPONENT Development
    )
endif()

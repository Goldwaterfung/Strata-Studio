# CMakeLists.txt for Agentic Interface Layer (`src/Agentic layer/`)
cmake_minimum_required(VERSION 3.20)

# Include directories
include_directories(
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/Agentic\ layer
)

# 1. Common & Server Library Target (linked into main DAW app)
add_library(agentic_server STATIC
    common/parsed_args.cpp
    server/ipc_server.cpp
    server/command_dispatcher.cpp
    server/result_formatter.cpp
    server/handlers/transport_handler.cpp
    server/handlers/track_handler.cpp
    server/handlers/plugin_handler.cpp
    server/handlers/clip_handler.cpp
    server/handlers/routing_handler.cpp
    server/handlers/analysis_handler.cpp
    server/handlers/rendering_handler.cpp
)

target_link_libraries(agentic_server PRIVATE
    middle_bridge
)

target_compile_features(agentic_server PUBLIC cxx_std_20)

# 2. Standalone Lightweight `daw-cli` Executable Target
add_executable(daw-cli
    client/main_client.cpp
)

target_compile_features(daw-cli PUBLIC cxx_std_20)

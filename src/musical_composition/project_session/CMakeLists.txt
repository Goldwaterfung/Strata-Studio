# Layer 5: project_session
cmake_minimum_required(VERSION 3.20)

add_library(layer5_project_session STATIC 
    iproject_session.h
    project_session_impl.cpp
    project_state.h
    project_state_bridge.h
    project_state_bridge.cpp
    project_serializer.h
    project_serializer.cpp
    project_json_serializer.h
    project_json_serializer.cpp
    marker_manager_impl.h
    marker_manager_impl.cpp
    region_metadata_manager_impl.h
    region_metadata_manager_impl.cpp
    ikey_signature_map.h
    key_signature_map_impl.h
    key_signature_map_impl.cpp
)

target_include_directories(layer5_project_session PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(layer5_project_session PUBLIC 
    common_utils
    layer5_track_manager
    layer5_command_history
    layer5_region_manager
    layer5_arranger
    layer5_chord_track
    layer5_clipboard
    layer5_comping
)
target_link_libraries(layer5_project_session PRIVATE
    nlohmann_json::nlohmann_json
)

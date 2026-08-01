set(VST3_SDK_DIR "${CMAKE_SOURCE_DIR}/src/third_party/VST3_SDK")

add_library(layer3_plugin STATIC
    iplugin_manager.cpp
    vst3_plugin_impl.cpp
    au_plugin_impl.mm
    clap_plugin_impl.cpp
    ${VST3_SDK_DIR}/pluginterfaces/base/funknown.cpp
    ${VST3_SDK_DIR}/pluginterfaces/base/coreiids.cpp
    ${VST3_SDK_DIR}/public.sdk/source/vst/vstinitiids.cpp
    ${VST3_SDK_DIR}/public.sdk/source/common/commoniids.cpp
    ${VST3_SDK_DIR}/public.sdk/source/vst/hosting/hostclasses.cpp
    ${VST3_SDK_DIR}/public.sdk/source/vst/utility/stringconvert.cpp
    ${VST3_SDK_DIR}/public.sdk/source/common/commonstringconvert.cpp
    ${VST3_SDK_DIR}/public.sdk/source/vst/hosting/pluginterfacesupport.cpp
)

target_link_libraries(layer3_plugin PRIVATE
    layer2_interface
)

if(NOT MSVC)
    target_compile_options(layer3_plugin PRIVATE 
        -Wno-old-style-cast
        -Wno-conversion
        -Wno-sign-conversion
        -Wno-shadow
        -Wno-pedantic
    )
endif()

# VST3 SDK
target_include_directories(layer3_plugin PRIVATE ${VST3_SDK_DIR})

# CLAP SDK
set(CLAP_SDK_DIR "${CMAKE_SOURCE_DIR}/src/third_party/CLAP_SDK/include")
target_include_directories(layer3_plugin PRIVATE ${CLAP_SDK_DIR})

# Audio Unit (macOS)
if(APPLE)
    find_library(COREAUDIO_LIBRARY CoreAudio)
    find_library(AUDIOTOOLBOX_LIBRARY AudioToolbox)
    find_library(AUDIOUNIT_LIBRARY AudioUnit)
    find_library(QUARTZCORE_LIBRARY QuartzCore)
    target_link_libraries(layer3_plugin PRIVATE 
        ${COREAUDIO_LIBRARY} 
        ${AUDIOTOOLBOX_LIBRARY}
        ${AUDIOUNIT_LIBRARY}
        ${QUARTZCORE_LIBRARY}
    )
    target_compile_options(layer3_plugin PRIVATE -fobjc-arc)
endif()

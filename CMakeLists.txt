cmake_minimum_required(VERSION 3.22)
project(DoesitSampler VERSION 0.1.0)

# Statische Runtime, damit auf deinem PC nichts nachinstalliert werden muss
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4
    GIT_SHALLOW ON)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(DoesitSampler
    COMPANY_NAME "Daniel Doesit"
    PLUGIN_MANUFACTURER_CODE Dods
    PLUGIN_CODE Dsmp
    FORMATS VST3
    PRODUCT_NAME "Doesit Sampler"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(DoesitSampler PRIVATE
    Source/GrainVoice.h
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h)

target_compile_definitions(DoesitSampler PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(DoesitSampler
    PRIVATE juce::juce_audio_utils
    PUBLIC juce::juce_recommended_config_flags
           juce::juce_recommended_warning_flags)

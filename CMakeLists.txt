cmake_minimum_required(VERSION 3.22)

# Static MSVC runtime -> the .dll runs on any Windows PC without the VC++ redistributable
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" CACHE STRING "" FORCE)

project(DnBMixDoctor VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE: use ./JUCE if present, otherwise download the pinned release
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt")
    add_subdirectory(JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        9.0.5
        GIT_SHALLOW    ON)
    FetchContent_MakeAvailable(JUCE)
endif()

juce_add_plugin(DnBMixDoctor
    COMPANY_NAME               "MixDoctor"
    PRODUCT_NAME               "DnB Mix Doctor"
    PLUGIN_MANUFACTURER_CODE   MxDr
    PLUGIN_CODE                DnBM
    FORMATS                    VST3
    VST3_CATEGORIES            Fx Analyzer
    IS_SYNTH                   FALSE
    NEEDS_MIDI_INPUT           FALSE
    NEEDS_MIDI_OUTPUT          FALSE
    IS_MIDI_EFFECT             FALSE
    COPY_PLUGIN_AFTER_BUILD    FALSE)

target_sources(DnBMixDoctor PRIVATE
    Source/MixDoctorDSP.h
    Source/PluginProcessor.cpp  Source/PluginProcessor.h
    Source/PluginEditor.cpp     Source/PluginEditor.h)

target_include_directories(DnBMixDoctor PRIVATE Source)

target_compile_definitions(DnBMixDoctor PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(DnBMixDoctor
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Native DSP test-bench (no JUCE needed):  cmake --build . --target dsp_tests && ctest
add_executable(dsp_tests EXCLUDE_FROM_ALL tests/test_dsp.cpp)
target_include_directories(dsp_tests PRIVATE Source)

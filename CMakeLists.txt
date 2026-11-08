cmake_minimum_required(VERSION 3.22)

# Static MSVC runtime -> the plugin runs on any Windows PC without the VC++ redistributable
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" CACHE STRING "" FORCE)
# Universal binary (Apple Silicon + Intel) on macOS
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")

project(JlpMixFixer VERSION 1.0.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---- JUCE: ./JUCE if present, -DJUCE_DIR=<path> if given, otherwise download the pinned release
set(JUCE_DIR "" CACHE PATH "Path to a local JUCE checkout (optional)")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt")
    add_subdirectory(JUCE)
elseif(JUCE_DIR AND EXISTS "${JUCE_DIR}/CMakeLists.txt")
    add_subdirectory("${JUCE_DIR}" "${CMAKE_BINARY_DIR}/JUCE")
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        9.0.5
        GIT_SHALLOW    ON)
    FetchContent_MakeAvailable(JUCE)
endif()

set(MCDSP_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND MCDSP_FORMATS AU)
endif()

# App icon (standalone .exe), rendered by: DnBMixDoctorSnapshot --icon Resources/icon.png
set(MCDSP_ICON_ARGS "")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Resources/icon.png")
    set(MCDSP_ICON_ARGS ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/Resources/icon.png" ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/Resources/icon.png")
endif()

juce_add_plugin(JlpMixFixer
    COMPANY_NAME               "Mod Con DSP"
    COMPANY_WEBSITE            ""
    PRODUCT_NAME               "JLP Mix Fixer"
    VERSION                    1.0.0
    PLUGIN_MANUFACTURER_CODE   MCDS
    PLUGIN_CODE                JLMF
    FORMATS                    ${MCDSP_FORMATS}
    VST3_CATEGORIES            Fx Mastering Dynamics
    AU_MAIN_TYPE               kAudioUnitType_Effect
    BUNDLE_ID                  com.modcondsp.jlpmixfixer
    IS_SYNTH                   FALSE
    NEEDS_MIDI_INPUT           FALSE
    NEEDS_MIDI_OUTPUT          FALSE
    IS_MIDI_EFFECT             FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD    FALSE
    ${MCDSP_ICON_ARGS})

target_sources(JlpMixFixer PRIVATE
    Source/Params.h
    Source/PluginProcessor.cpp      Source/PluginProcessor.h
    Source/PluginEditor.cpp         Source/PluginEditor.h
    Source/PresetManager.cpp        Source/PresetManager.h
    Source/dsp/DspUtils.h
    Source/dsp/LoudnessMeter.h
    Source/dsp/TruePeakLimiter.h
    Source/dsp/MixEngine.cpp        Source/dsp/MixEngine.h
    Source/analysis/VisualFifo.h
    Source/analysis/AutoFix.cpp     Source/analysis/AutoFix.h
    Source/ui/Theme.cpp             Source/ui/Theme.h
    Source/ui/Widgets.h
    Source/ui/VUMeter.h
    Source/ui/EqPanel.h
    Source/ui/Visualizer.cpp        Source/ui/Visualizer.h)

target_include_directories(JlpMixFixer PRIVATE Source)

target_compile_definitions(JlpMixFixer PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(JlpMixFixer
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---- Offline DSP tests (no host needed):  cmake --build build --target DnBMixDoctorTests
juce_add_console_app(DnBMixDoctorTests PRODUCT_NAME "DnBMixDoctorTests")
target_sources(DnBMixDoctorTests PRIVATE tests/EngineTests.cpp Source/dsp/MixEngine.cpp)
target_include_directories(DnBMixDoctorTests PRIVATE Source)
target_compile_definitions(DnBMixDoctorTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(DnBMixDoctorTests PRIVATE juce::juce_dsp juce::juce_audio_basics
                      PUBLIC juce::juce_recommended_config_flags)
set_target_properties(DnBMixDoctorTests PROPERTIES EXCLUDE_FROM_ALL TRUE)

# ---- Renders the editor to PNG with audio flowing (UI review without a DAW)
juce_add_console_app(DnBMixDoctorSnapshot PRODUCT_NAME "DnBMixDoctorSnapshot")
get_target_property(MCDSP_PLUGIN_SOURCES JlpMixFixer SOURCES)
list(FILTER MCDSP_PLUGIN_SOURCES INCLUDE REGEX "\\.cpp$")
list(FILTER MCDSP_PLUGIN_SOURCES INCLUDE REGEX "^Source/")
target_sources(DnBMixDoctorSnapshot PRIVATE tests/UISnapshot.cpp ${MCDSP_PLUGIN_SOURCES})
target_include_directories(DnBMixDoctorSnapshot PRIVATE Source)
target_compile_definitions(DnBMixDoctorSnapshot PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
                           JucePlugin_Name="JLP Mix Fixer")
target_link_libraries(DnBMixDoctorSnapshot PRIVATE juce::juce_audio_utils juce::juce_dsp
                      PUBLIC juce::juce_recommended_config_flags)
set_target_properties(DnBMixDoctorSnapshot PROPERTIES EXCLUDE_FROM_ALL TRUE)

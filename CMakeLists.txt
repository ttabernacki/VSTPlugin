cmake_minimum_required(VERSION 3.22)

# Windows: link the C++ runtime statically so the plug-in has no dependency on the Visual C++
# redistributable (a missing VCRUNTIME140_1.dll makes a host's plug-in scan stall on a hidden dialog).
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(OrbitPan VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- DSP tests and demo renderer (no JUCE needed) ---------------------------------
option(ORBITPAN_BUILD_TOOLS "Build dsp_tests and render_demo" ON)
if(ORBITPAN_BUILD_TOOLS)
    add_executable(dsp_tests tests/dsp_tests.cpp)
    add_executable(render_demo tools/render_demo.cpp)
    enable_testing()
    add_test(NAME dsp COMMAND dsp_tests ${CMAKE_SOURCE_DIR}/Resources/hrtf_d1_5deg.bin)
endif()

# --- Plugin -----------------------------------------------------------------------
option(ORBITPAN_BUILD_PLUGIN "Build the JUCE VST3 plugin" ON)
if(ORBITPAN_BUILD_PLUGIN)
    include(FetchContent)
    # Point FETCHCONTENT_SOURCE_DIR_JUCE at a local JUCE checkout to avoid the download.
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.4
        GIT_SHALLOW ON)
    FetchContent_MakeAvailable(JUCE)

    # Ableton Live loads VST3 on Windows and macOS (and AU on macOS).
    set(ORBITPAN_FORMATS VST3 Standalone)
    if(APPLE)
        list(APPEND ORBITPAN_FORMATS AU)
    endif()

    juce_add_plugin(OrbitPan
        COMPANY_NAME "OrbitPan"
        PLUGIN_MANUFACTURER_CODE Orbp
        PLUGIN_CODE Orb1
        FORMATS ${ORBITPAN_FORMATS}
        PRODUCT_NAME "OrbitPan"
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT FALSE
        NEEDS_MIDI_OUTPUT FALSE
        COPY_PLUGIN_AFTER_BUILD FALSE
        VST3_CATEGORIES Fx Spatial
        AU_MAIN_TYPE kAudioUnitType_Effect)

    juce_add_binary_data(OrbitPanData SOURCES Resources/hrtf_d1_5deg.bin)

    target_sources(OrbitPan PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp)

    target_compile_definitions(OrbitPan PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

    # Offscreen renderer of the real editor, for visual checks without a display.
    juce_add_console_app(UiSnapshot PRODUCT_NAME "ui_snapshot")
    target_sources(UiSnapshot PRIVATE tools/ui_snapshot.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp)
    target_compile_definitions(UiSnapshot PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
        JUCE_STANDALONE_APPLICATION=1)
    target_link_libraries(UiSnapshot PRIVATE OrbitPanData juce::juce_audio_utils juce::juce_recommended_config_flags)

    # Host-style tests of the real processor (parameters, state, block sizes, automation).
    juce_add_console_app(PluginTests PRODUCT_NAME "plugin_tests")
    target_sources(PluginTests PRIVATE tests/plugin_tests.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp)
    target_compile_definitions(PluginTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1)
    target_link_libraries(PluginTests PRIVATE OrbitPanData juce::juce_audio_utils juce::juce_recommended_config_flags)
    add_test(NAME plugin COMMAND PluginTests)

    target_link_libraries(OrbitPan
        PRIVATE
            OrbitPanData
            juce::juce_audio_utils
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags)
endif()

# --- Bass Note Leveler (second plug-in in this repo) ---------------------------------
add_subdirectory(bass-leveler)

# --- Low-End Definition (third plug-in in this repo) ----------------------------------
add_subdirectory(lowend-definition)

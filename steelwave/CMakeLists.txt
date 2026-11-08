# Steelwave: physically modelled E9 pedal steel. DSP core tests (no JUCE) and the JUCE instrument.
# Included from the top-level CMakeLists.txt after JUCE has been made available.

if(ORBITPAN_BUILD_TOOLS)
    add_executable(steelwave_tests tests/steelwave_tests.cpp)
    add_test(NAME steelwave_core COMMAND steelwave_tests)
    add_executable(steelwave_engine_tests tests/engine_tests.cpp)
    add_test(NAME steelwave_engine COMMAND steelwave_engine_tests)
    add_executable(steelwave_demo tools/render_demo.cpp)
endif()

if(TARGET juce::juce_audio_utils)
    set(SWV_FORMATS VST3 Standalone)
    if(APPLE)
        list(APPEND SWV_FORMATS AU)
    endif()

    juce_add_plugin(Steelwave
        COMPANY_NAME "OrbitPan"
        PLUGIN_MANUFACTURER_CODE Orbp
        PLUGIN_CODE Stw1
        FORMATS ${SWV_FORMATS}
        PRODUCT_NAME "Steelwave"
        IS_SYNTH TRUE
        NEEDS_MIDI_INPUT TRUE
        NEEDS_MIDI_OUTPUT FALSE
        COPY_PLUGIN_AFTER_BUILD FALSE
        VST3_CATEGORIES Instrument Synth
        AU_MAIN_TYPE kAudioUnitType_MusicDevice)

    target_sources(Steelwave PRIVATE Source/PluginProcessor.cpp Source/PluginEditor.cpp)
    target_compile_definitions(Steelwave PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
    target_link_libraries(Steelwave
        PRIVATE juce::juce_audio_utils
        PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags)

    # Host-style tests of the real processor (parameters, state, MIDI, blocks, rates, editor)
    juce_add_console_app(SteelwavePluginTests PRODUCT_NAME "steelwave_plugin_tests")
    target_sources(SteelwavePluginTests PRIVATE tests/plugin_tests.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp)
    target_compile_definitions(SteelwavePluginTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1)
    target_link_libraries(SteelwavePluginTests PRIVATE juce::juce_audio_utils juce::juce_recommended_config_flags)
    add_test(NAME steelwave_plugin COMMAND SteelwavePluginTests)

    # Offscreen renderer of the real editor, for visual checks without a display
    juce_add_console_app(SteelwaveUiSnapshot PRODUCT_NAME "steelwave_ui_snapshot")
    target_sources(SteelwaveUiSnapshot PRIVATE tools/ui_snapshot.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp)
    target_compile_definitions(SteelwaveUiSnapshot PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1)
    target_link_libraries(SteelwaveUiSnapshot PRIVATE juce::juce_audio_utils juce::juce_recommended_config_flags)
endif()

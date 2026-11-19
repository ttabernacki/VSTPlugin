# Pump organ: a physically modelled suction reed organ (chord organ / parlour organ). The DSP core and
# its tests (no JUCE), the demo renderer, and the instrument (VST3 + standalone, MPE).

if(ORBITPAN_BUILD_TOOLS)
    add_executable(pumporgan_tests tests/organ_tests.cpp)
    add_test(NAME pumporgan_core COMMAND pumporgan_tests)
    add_executable(pumporgan_demo tools/render_demo.cpp)
endif()

if(TARGET juce::juce_audio_utils)
    juce_add_plugin(PumpOrgan
        COMPANY_NAME "OrbitPan"
        PLUGIN_MANUFACTURER_CODE Orbp
        PLUGIN_CODE Pmp1
        FORMATS VST3 Standalone
        PRODUCT_NAME "Pump Organ"
        VERSION 1.0.0
        IS_SYNTH TRUE
        NEEDS_MIDI_INPUT TRUE
        NEEDS_MIDI_OUTPUT FALSE
        COPY_PLUGIN_AFTER_BUILD FALSE
        VST3_CATEGORIES Instrument Synth)
    target_sources(PumpOrgan PRIVATE Source/PluginProcessor.cpp Source/PluginEditor.cpp)
    target_compile_definitions(PumpOrgan PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
    target_link_libraries(PumpOrgan
        PRIVATE juce::juce_audio_utils
        PUBLIC juce::juce_recommended_config_flags orbitpan_lto)

    # Host-style tests of the real processor
    juce_add_console_app(PumpOrganPluginTests PRODUCT_NAME "pumporgan_plugin_tests")
    target_sources(PumpOrganPluginTests PRIVATE tests/plugin_tests.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp)
    target_compile_definitions(PumpOrganPluginTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1)
    target_link_libraries(PumpOrganPluginTests PRIVATE juce::juce_audio_utils juce::juce_recommended_config_flags)
    add_test(NAME pumporgan_plugin COMMAND PumpOrganPluginTests)

    # Offscreen renderer of the real editor, for visual checks without a display
    juce_add_console_app(PumpOrganUiSnapshot PRODUCT_NAME "pumporgan_ui_snapshot")
    target_sources(PumpOrganUiSnapshot PRIVATE tools/ui_snapshot.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp)
    target_compile_definitions(PumpOrganUiSnapshot PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1)
    target_link_libraries(PumpOrganUiSnapshot PRIVATE juce::juce_audio_utils juce::juce_recommended_config_flags)
endif()

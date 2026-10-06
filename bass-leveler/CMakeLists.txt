# Bass Note Leveler: DSP core tests (no JUCE) and the JUCE VST3 plug-in.
# Included from the top-level CMakeLists.txt after JUCE has been made available.

if(ORBITPAN_BUILD_TOOLS)
    add_executable(leveler_tests tests/leveler_tests.cpp)
    add_test(NAME leveler_core COMMAND leveler_tests)
    add_executable(bass_demo tools/render_demo.cpp)
endif()

if(TARGET juce::juce_audio_utils)
    set(BNL_FORMATS VST3)
    if(APPLE)
        list(APPEND BNL_FORMATS AU)
    endif()

    juce_add_plugin(BassNoteLeveler
        COMPANY_NAME "OrbitPan"
        PLUGIN_MANUFACTURER_CODE Orbp
        PLUGIN_CODE Bnl1
        FORMATS ${BNL_FORMATS}
        PRODUCT_NAME "Bass Note Leveler"
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT FALSE
        NEEDS_MIDI_OUTPUT FALSE
        COPY_PLUGIN_AFTER_BUILD FALSE
        VST3_CATEGORIES Fx EQ
        AU_MAIN_TYPE kAudioUnitType_Effect)

    target_sources(BassNoteLeveler PRIVATE Source/PluginProcessor.cpp Source/PluginEditor.cpp)
    target_include_directories(BassNoteLeveler PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/core)
    target_compile_definitions(BassNoteLeveler PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
    target_link_libraries(BassNoteLeveler
        PRIVATE juce::juce_audio_utils
        PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags)

    # Host-style tests of the real processor (parameters, state incl. the learned table, blocks, stress)
    juce_add_console_app(BassLevelerPluginTests PRODUCT_NAME "bass_leveler_plugin_tests")
    target_sources(BassLevelerPluginTests PRIVATE tests/plugin_tests.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp)
    target_compile_definitions(BassLevelerPluginTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1)
    target_link_libraries(BassLevelerPluginTests PRIVATE juce::juce_audio_utils juce::juce_recommended_config_flags)
    add_test(NAME bass_leveler_plugin COMMAND BassLevelerPluginTests)
endif()

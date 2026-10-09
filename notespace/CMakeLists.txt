# Note Space: DSP core tests (no JUCE) and the JUCE VST3 plug-in.
# Included from the top-level CMakeLists.txt after JUCE has been made available.

if(ORBITPAN_BUILD_TOOLS)
    add_executable(notespace_tests tests/notespace_tests.cpp)
    add_test(NAME notespace_core COMMAND notespace_tests)
    add_executable(notespace_demo tools/render_demo.cpp)
endif()

if(TARGET juce::juce_audio_utils)
    set(NSP_FORMATS VST3)
    if(APPLE)
        list(APPEND NSP_FORMATS AU)
    endif()

    juce_add_plugin(NoteSpace
        COMPANY_NAME "OrbitPan"
        PLUGIN_MANUFACTURER_CODE Orbp
        PLUGIN_CODE Nsp1
        FORMATS ${NSP_FORMATS}
        PRODUCT_NAME "Note Space"
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT FALSE
        NEEDS_MIDI_OUTPUT FALSE
        COPY_PLUGIN_AFTER_BUILD FALSE
        VST3_CATEGORIES Fx EQ
        AU_MAIN_TYPE kAudioUnitType_Effect)

    target_sources(NoteSpace PRIVATE Source/PluginProcessor.cpp Source/PluginEditor.cpp)
    target_include_directories(NoteSpace PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/core)
    target_compile_definitions(NoteSpace PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
    target_link_libraries(NoteSpace
        PRIVATE juce::juce_audio_utils
        PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags)

    # Host-style tests of the real processor (parameters, state, blocks, stress)
    juce_add_console_app(NoteSpacePluginTests PRODUCT_NAME "note_space_plugin_tests")
    target_sources(NoteSpacePluginTests PRIVATE tests/plugin_tests.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp)
    target_compile_definitions(NoteSpacePluginTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1)
    target_link_libraries(NoteSpacePluginTests PRIVATE juce::juce_audio_utils juce::juce_recommended_config_flags)
    add_test(NAME note_space_plugin COMMAND NoteSpacePluginTests)
endif()

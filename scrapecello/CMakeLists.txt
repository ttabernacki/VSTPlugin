# Scrape Cello: a physically modelled bowed string played for texture (pressure into grind, position from
# fingerboard to bridge, bow speed). The DSP core and its tests (no JUCE), the demo renderer, and the
# instrument (VST3 + standalone, MPE).

if(ORBITPAN_BUILD_TOOLS)
    add_executable(scrapecello_tests tests/cello_tests.cpp)
    add_test(NAME scrapecello_core COMMAND scrapecello_tests)
    add_executable(scrapecello_demo tools/render_demo.cpp)
endif()

if(TARGET juce::juce_audio_utils)
    juce_add_plugin(ScrapeCello
        COMPANY_NAME "OrbitPan"
        PLUGIN_MANUFACTURER_CODE Orbp
        PLUGIN_CODE Scr1
        FORMATS VST3 Standalone
        PRODUCT_NAME "Scrape Cello"
        VERSION 0.1.0
        IS_SYNTH TRUE
        NEEDS_MIDI_INPUT TRUE
        NEEDS_MIDI_OUTPUT FALSE
        COPY_PLUGIN_AFTER_BUILD FALSE
        VST3_CATEGORIES Instrument Synth)
    target_sources(ScrapeCello PRIVATE Source/PluginProcessor.cpp)
    target_compile_definitions(ScrapeCello PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
    target_link_libraries(ScrapeCello
        PRIVATE juce::juce_audio_utils
        PUBLIC juce::juce_recommended_config_flags orbitpan_lto)

    juce_add_console_app(ScrapeCelloPluginTests PRODUCT_NAME "scrapecello_plugin_tests")
    target_sources(ScrapeCelloPluginTests PRIVATE tests/plugin_tests.cpp Source/PluginProcessor.cpp)
    target_compile_definitions(ScrapeCelloPluginTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1)
    target_link_libraries(ScrapeCelloPluginTests PRIVATE juce::juce_audio_utils juce::juce_recommended_config_flags)
    add_test(NAME scrapecello_plugin COMMAND ScrapeCelloPluginTests)
endif()

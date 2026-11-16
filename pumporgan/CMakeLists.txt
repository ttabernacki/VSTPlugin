# Pump organ: a physically modelled American suction reed organ. For now the DSP core (no JUCE):
# its tests and the demo renderer. The plug-in comes once the sound is right.

if(ORBITPAN_BUILD_TOOLS)
    add_executable(pumporgan_tests tests/organ_tests.cpp)
    add_test(NAME pumporgan_core COMMAND pumporgan_tests)
    add_executable(pumporgan_demo tools/render_demo.cpp)
endif()

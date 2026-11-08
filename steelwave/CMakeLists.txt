# Steelwave: physically modelled pedal steel. Milestone 1 = the string model (no JUCE yet).
# Included from the top-level CMakeLists.txt.

if(ORBITPAN_BUILD_TOOLS)
    add_executable(steelwave_tests tests/steelwave_tests.cpp)
    add_test(NAME steelwave_core COMMAND steelwave_tests)
    add_executable(steelwave_demo tools/render_demo.cpp)
endif()

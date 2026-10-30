# Low-End Evaluator: headless analysis core (no JUCE), the `lee` command-line tool and tests.
# Included from the top-level CMakeLists.txt. The plug-in shell comes later and will call the same core.

if(ORBITPAN_BUILD_TOOLS)
    add_executable(lee tools/lee.cpp)
    add_executable(lee_tests tests/lee_tests.cpp)
    if(NOT MSVC)
        # analysis is offline and long; unoptimised builds make the tests crawl
        target_compile_options(lee_tests PRIVATE $<$<CONFIG:Debug>:-O1>)
    endif()
    add_test(NAME lee_core COMMAND lee_tests)
endif()

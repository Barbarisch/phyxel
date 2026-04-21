# Stress Tests for Phyxel
# High-load and endurance testing

option(PHYXEL_HEAVY_TESTS "Enable heavy stress tests" OFF)

add_executable(phyxel_stress_tests
    StressTestFixture.h
    ChunkStressTests.cpp
)

# All include directories, link libraries, and compile definitions
# are provided transitively by phyxel_core (PUBLIC)
target_link_libraries(phyxel_stress_tests PRIVATE
    phyxel_core
    GTest::gtest
    GTest::gtest_main
)

# Enable heavy tests if requested
if(PHYXEL_HEAVY_TESTS)
    target_compile_definitions(phyxel_stress_tests PRIVATE PHYXEL_HEAVY_TESTS)
    message(STATUS "Stress tests configured for HEAVY load")
else()
    message(STATUS "Stress tests configured for LIGHT load (development mode)")
endif()

# Compiler settings
set_target_properties(phyxel_stress_tests PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

# Register with CTest
add_test(NAME phyxel_stress_tests COMMAND phyxel_stress_tests)
set_tests_properties(phyxel_stress_tests PROPERTIES
    LABELS "stress"
    TIMEOUT 1800  # Stress tests can take a long time (30 minutes)
)

# Output message
message(STATUS "Configured stress tests: phyxel_stress_tests")

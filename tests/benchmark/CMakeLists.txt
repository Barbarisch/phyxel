# Benchmark Tests for Phyxel
# Performance and stress testing

add_executable(phyxel_benchmarks
    BenchmarkFixture.h
    ChunkBenchmarks.cpp
    PhysicsBenchmarks.cpp
)

# All include directories, link libraries, and compile definitions
# are provided transitively by phyxel_core (PUBLIC)
target_link_libraries(phyxel_benchmarks PRIVATE
    phyxel_core
    GTest::gtest
    GTest::gtest_main
)

# Compiler settings
set_target_properties(phyxel_benchmarks PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

# Register with CTest
add_test(NAME phyxel_benchmarks COMMAND phyxel_benchmarks)
set_tests_properties(phyxel_benchmarks PROPERTIES
    LABELS "benchmark"
    TIMEOUT 600  # Benchmarks may take longer
)

# Output message
message(STATUS "Configured benchmark tests: phyxel_benchmarks")

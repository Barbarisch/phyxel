# Unit Tests CMakeLists.txt
# This builds the test executable with all unit tests

# Collect unit test source files (exclude integration/benchmark/stress/e2e)
file(GLOB_RECURSE TEST_SOURCES
    "core/*.cpp"
    "physics/*.cpp"
    "scene/*.cpp"
    "ui/*.cpp"
    "utils/*.cpp"
    "mocks/*.cpp"
)

# Create test executable
add_executable(phyxel_tests ${TEST_SOURCES})

# Link against Core Library and Google Test
target_link_libraries(phyxel_tests PRIVATE
    phyxel_core
    GTest::gtest_main
    GTest::gmock
)

# Enable Sanitizers
enable_sanitizers(phyxel_tests)

# Copy shaders to build directory for tests
add_custom_command(TARGET phyxel_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/shaders
    ${CMAKE_BINARY_DIR}/shaders
    COMMENT "Copying shaders to build directory"
)

# Platform-specific settings
if(PLATFORM_WINDOWS AND MSVC)
    set_property(TARGET phyxel_tests PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

# Register tests with CTest
include(GoogleTest)
gtest_discover_tests(phyxel_tests)

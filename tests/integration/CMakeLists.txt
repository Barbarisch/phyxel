# Integration Tests CMakeLists.txt
# Separate from unit tests - these require Vulkan/Physics initialization

cmake_minimum_required(VERSION 3.15)

# Collect integration test source files
set(INTEGRATION_TEST_SOURCES
    IntegrationTestFixture.cpp
    ChunkManagerIntegrationTest.cpp
    PhysicsIntegrationTest.cpp
    VulkanIntegrationTest.cpp
)

# Create integration test executable
add_executable(phyxel_integration_tests ${INTEGRATION_TEST_SOURCES})

# Link against Core Library and Google Test
target_link_libraries(phyxel_integration_tests PRIVATE
    phyxel_core
    GTest::gtest_main
    GTest::gmock
)

# Include directories (integration tests need their own headers)
target_include_directories(phyxel_integration_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/tests/integration
)

# Enable Sanitizers
enable_sanitizers(phyxel_integration_tests)

# Platform-specific settings
if(PLATFORM_WINDOWS AND MSVC)
    set_property(TARGET phyxel_integration_tests PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

# Register integration tests with CTest
include(GoogleTest)
gtest_discover_tests(phyxel_integration_tests
    PROPERTIES 
        LABELS "integration"
        TIMEOUT 300
)

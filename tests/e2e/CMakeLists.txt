# End-to-End Tests CMakeLists.txt
# Tests complete application workflows from initialization to shutdown

cmake_minimum_required(VERSION 3.15)

set(E2E_TEST_SOURCES
    ApplicationE2ETests.cpp
    WorldInteractionE2ETests.cpp
)

add_executable(phyxel_e2e_tests ${E2E_TEST_SOURCES})

# Link phyxel_game (not just phyxel_core) because E2E tests use Application
# phyxel_game transitively provides phyxel_core's includes and libs
target_link_libraries(phyxel_e2e_tests PRIVATE
    phyxel_game
    GTest::gtest_main
    GTest::gmock
)

target_include_directories(phyxel_e2e_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

enable_sanitizers(phyxel_e2e_tests)

# MSVC runtime
if(PLATFORM_WINDOWS AND MSVC)
    set_property(TARGET phyxel_e2e_tests PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

# Copy shaders to test output directory
add_custom_command(TARGET phyxel_e2e_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/shaders
        $<TARGET_FILE_DIR:phyxel_e2e_tests>/shaders
    COMMENT "Copying shaders to E2E test directory"
)

# Copy resources
add_custom_command(TARGET phyxel_e2e_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/resources
        $<TARGET_FILE_DIR:phyxel_e2e_tests>/resources
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/worlds
        $<TARGET_FILE_DIR:phyxel_e2e_tests>/worlds
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/logging.ini
        $<TARGET_FILE_DIR:phyxel_e2e_tests>/logging.ini
    COMMENT "Copying resources to E2E test directory"
)

# Register E2E tests with CTest
include(GoogleTest)
gtest_discover_tests(phyxel_e2e_tests
    PROPERTIES
        LABELS "e2e"
        TIMEOUT 600
        WORKING_DIRECTORY $<TARGET_FILE_DIR:phyxel_e2e_tests>
)

message(STATUS "Configured E2E tests: phyxel_e2e_tests")

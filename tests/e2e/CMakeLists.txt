# End-to-End Tests CMakeLists.txt
# Tests complete application workflows from initialization to shutdown

cmake_minimum_required(VERSION 3.15)

# Collect E2E test source files
set(E2E_TEST_SOURCES
    ApplicationE2ETests.cpp
    WorldInteractionE2ETests.cpp
)

# Collect main application source files (excluding main.cpp)
file(GLOB_RECURSE APP_SOURCES 
    "${CMAKE_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_SOURCE_DIR}/src/*.h"
)
list(FILTER APP_SOURCES EXCLUDE REGEX ".*main\\.cpp$")

# Add ImGui sources
set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/external/imgui)
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp
)

# Add stb_image
set(STB_IMAGE_DIR ${CMAKE_SOURCE_DIR}/external/bullet3/examples/ThirdPartyLibs/stb_image)
set(STB_IMAGE_SOURCES
    ${STB_IMAGE_DIR}/stb_image.cpp
)

# Add SQLite3 sources if available
if(SQLITE3_FOUND AND DEFINED SQLITE3_SOURCES)
    list(APPEND APP_SOURCES ${SQLITE3_SOURCES})
endif()

# Create E2E test executable
add_executable(phyxel_e2e_tests
    ${E2E_TEST_SOURCES}
    # No need to add app sources, we link against phyxel_core
)

# Include directories
target_include_directories(phyxel_e2e_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/tests/e2e
    ${Vulkan_INCLUDE_DIRS}
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${STB_IMAGE_DIR}
    ${CMAKE_SOURCE_DIR}/external/bullet3/src
    ${CMAKE_SOURCE_DIR}/external/miniaudio
)

# Platform-specific includes
if(PLATFORM_WINDOWS)
    target_include_directories(phyxel_e2e_tests PRIVATE 
        ${CMAKE_SOURCE_DIR}/external/glm
        ${CMAKE_SOURCE_DIR}/external/glfw/include
    )
elseif(PLATFORM_LINUX)
    target_include_directories(phyxel_e2e_tests PRIVATE 
        ${CMAKE_SOURCE_DIR}/external/glm
    )
endif()

# Add SQLite3 include dirs if available
if(SQLITE3_FOUND)
    target_include_directories(phyxel_e2e_tests PRIVATE ${SQLITE3_INCLUDE_DIRS})
endif()

# Link libraries
if(PLATFORM_WINDOWS)
    target_link_libraries(phyxel_e2e_tests PRIVATE
        phyxel_core
        GTest::gtest_main
        GTest::gmock
        Vulkan::Vulkan
        glfw3
        ${BULLET_LIBS}
    )
    
    if(SQLITE3_FOUND AND DEFINED SQLITE3_LIBRARIES)
        target_link_libraries(phyxel_e2e_tests PRIVATE ${SQLITE3_LIBRARIES})
    endif()
    
elseif(PLATFORM_LINUX)
    target_link_libraries(phyxel_e2e_tests PRIVATE
        phyxel_core
        GTest::gtest_main
        GTest::gmock
        Vulkan::Vulkan
        glfw
        ${BULLET_LIBS}
    )
    
    if(SQLITE3_FOUND AND DEFINED SQLITE3_LIBRARIES)
        target_link_libraries(phyxel_e2e_tests PRIVATE ${SQLITE3_LIBRARIES})
    endif()
endif()

# Compile definitions
target_compile_definitions(phyxel_e2e_tests PRIVATE 
    GLM_ENABLE_EXPERIMENTAL
    $<$<CONFIG:Debug>:DEBUG>
    $<$<CONFIG:Release>:NDEBUG>
)

# Add SQLite3 support if available
if(SQLITE3_FOUND)
    target_compile_definitions(phyxel_e2e_tests PRIVATE ENABLE_WORLD_STORAGE)
endif()

# Platform-specific settings
if(PLATFORM_WINDOWS AND MSVC)
    set_property(TARGET phyxel_e2e_tests PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

# Copy shader files to test output directory so tests can find them
add_custom_command(TARGET phyxel_e2e_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/shaders
        $<TARGET_FILE_DIR:phyxel_e2e_tests>/shaders
    COMMENT "Copying shaders to E2E test directory"
)

# Copy all required resources to test output directory in one command
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
# E2E tests may take longer as they run complete workflows
include(GoogleTest)
gtest_discover_tests(phyxel_e2e_tests
    PROPERTIES 
        LABELS "e2e"
        TIMEOUT 600
        WORKING_DIRECTORY $<TARGET_FILE_DIR:phyxel_e2e_tests>
)

message(STATUS "Configured E2E tests: phyxel_e2e_tests")

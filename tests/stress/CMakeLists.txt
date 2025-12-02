# Stress Tests for Phyxel
# High-load and endurance testing

# Create stress test executable
add_executable(phyxel_stress_tests
    # Stress test fixture
    StressTestFixture.h
    
    # Stress test files
    ChunkStressTests.cpp
    PhysicsStressTests.cpp
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
    set(SQLITE3_SRC ${SQLITE3_SOURCES})
else()
    set(SQLITE3_SRC)
endif()

# Add all sources to test executable
target_sources(phyxel_stress_tests PRIVATE
    ${APP_SOURCES}
    ${IMGUI_SOURCES}
    ${STB_IMAGE_SOURCES}
    ${SQLITE3_SRC}
)

# Link against main libraries
if(PLATFORM_WINDOWS)
    target_link_libraries(phyxel_stress_tests PRIVATE
        # GoogleTest
        GTest::gtest
        GTest::gtest_main
        
        # Bullet Physics
        BulletDynamics
        BulletCollision
        LinearMath
        
        # Vulkan
        Vulkan::Vulkan
        
        # GLFW
        glfw3
    )
    
    if(SQLITE3_FOUND AND DEFINED SQLITE3_LIBRARIES)
        target_link_libraries(phyxel_stress_tests PRIVATE ${SQLITE3_LIBRARIES})
    endif()
    
elseif(PLATFORM_LINUX)
    target_link_libraries(phyxel_stress_tests PRIVATE
        # GoogleTest
        GTest::gtest
        GTest::gtest_main
        
        # Bullet Physics
        BulletDynamics
        BulletCollision
        LinearMath
        
        # Vulkan
        Vulkan::Vulkan
        
        # GLFW
        glfw
    )
    
    if(SQLITE3_FOUND AND DEFINED SQLITE3_LIBRARIES)
        target_link_libraries(phyxel_stress_tests PRIVATE ${SQLITE3_LIBRARIES})
    endif()
endif()

# Include directories
target_include_directories(phyxel_stress_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/external/bullet3/src
    ${CMAKE_SOURCE_DIR}/external/imgui
    ${CMAKE_SOURCE_DIR}/external/imgui/backends
    ${CMAKE_SOURCE_DIR}/external/sqlite3
    ${STB_IMAGE_DIR}
    ${Vulkan_INCLUDE_DIRS}
)

# Platform-specific GLM setup
if(PLATFORM_WINDOWS)
    target_include_directories(phyxel_stress_tests PRIVATE
        ${CMAKE_SOURCE_DIR}/external/glm
    )
elseif(PLATFORM_LINUX)
    target_include_directories(phyxel_stress_tests PRIVATE
        ${CMAKE_SOURCE_DIR}/external/glm
    )
endif()

# Compile definitions
target_compile_definitions(phyxel_stress_tests PRIVATE
    GLM_FORCE_RADIANS
    GLM_FORCE_DEPTH_ZERO_TO_ONE
    GLM_ENABLE_EXPERIMENTAL
    $<$<BOOL:${SQLITE3_FOUND}>:WORLD_STORAGE_ENABLED>
)

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

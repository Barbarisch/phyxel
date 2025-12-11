# Benchmark Tests for Phyxel
# Performance and stress testing

# Create benchmark test executable
add_executable(phyxel_benchmarks
    # Benchmark fixture
    BenchmarkFixture.h
    
    # Benchmark test files
    ChunkBenchmarks.cpp
    PhysicsBenchmarks.cpp
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
target_sources(phyxel_benchmarks PRIVATE
    ${APP_SOURCES}
    ${IMGUI_SOURCES}
    ${STB_IMAGE_SOURCES}
    ${SQLITE3_SRC}
)

# Link against main libraries
if(PLATFORM_WINDOWS)
    target_link_libraries(phyxel_benchmarks PRIVATE
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
        target_link_libraries(phyxel_benchmarks PRIVATE ${SQLITE3_LIBRARIES})
    endif()
    
elseif(PLATFORM_LINUX)
    target_link_libraries(phyxel_benchmarks PRIVATE
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
        target_link_libraries(phyxel_benchmarks PRIVATE ${SQLITE3_LIBRARIES})
    endif()
endif()

# Include directories
target_include_directories(phyxel_benchmarks PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/external/bullet3/src
    ${CMAKE_SOURCE_DIR}/external/imgui
    ${CMAKE_SOURCE_DIR}/external/imgui/backends
    ${CMAKE_SOURCE_DIR}/external/sqlite3
    ${STB_IMAGE_DIR}
    ${Vulkan_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/external/miniaudio
)

# Platform-specific GLM setup (matches integration tests)
if(PLATFORM_WINDOWS)
    target_include_directories(phyxel_benchmarks PRIVATE
        ${CMAKE_SOURCE_DIR}/external/glm
    )
elseif(PLATFORM_LINUX)
    target_include_directories(phyxel_benchmarks PRIVATE
        ${CMAKE_SOURCE_DIR}/external/glm
    )
endif()

# Compile definitions
target_compile_definitions(phyxel_benchmarks PRIVATE
    GLM_FORCE_RADIANS
    GLM_FORCE_DEPTH_ZERO_TO_ONE
    GLM_ENABLE_EXPERIMENTAL
    $<$<BOOL:${SQLITE3_FOUND}>:WORLD_STORAGE_ENABLED>
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

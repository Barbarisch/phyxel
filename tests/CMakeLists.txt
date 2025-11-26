# Unit Tests CMakeLists.txt
# This builds the test executable with all unit tests

# Collect all test source files
file(GLOB_RECURSE TEST_SOURCES
    "*.cpp"
    "*.h"
)

# Collect main application source files (excluding main.cpp)
file(GLOB_RECURSE APP_SOURCES 
    "${CMAKE_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_SOURCE_DIR}/src/*.h"
)

# Remove main.cpp from app sources (tests have their own main)
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

# Add stb_image for texture loading
set(STB_IMAGE_DIR ${CMAKE_SOURCE_DIR}/external/bullet3/examples/ThirdPartyLibs/stb_image)
set(STB_IMAGE_SOURCES
    ${STB_IMAGE_DIR}/stb_image.cpp
)

# Add SQLite3 sources if using amalgamation
if(SQLITE3_FOUND AND DEFINED SQLITE3_SOURCES)
    list(APPEND APP_SOURCES ${SQLITE3_SOURCES})
endif()

# Create test executable
add_executable(phyxel_tests 
    ${TEST_SOURCES}
    ${APP_SOURCES}
    ${IMGUI_SOURCES}
    ${STB_IMAGE_SOURCES}
)

# Include directories
target_include_directories(phyxel_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${Vulkan_INCLUDE_DIRS}
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${STB_IMAGE_DIR}
    ${CMAKE_SOURCE_DIR}/external/bullet3/src
)

# Platform-specific includes
if(PLATFORM_WINDOWS)
    target_include_directories(phyxel_tests PRIVATE 
        ${CMAKE_SOURCE_DIR}/external/glm
        ${CMAKE_SOURCE_DIR}/external/glfw/include
    )
elseif(PLATFORM_LINUX)
    target_include_directories(phyxel_tests PRIVATE 
        ${CMAKE_SOURCE_DIR}/external/glm
    )
endif()

# Add SQLite3 include dirs if available
if(SQLITE3_FOUND)
    target_include_directories(phyxel_tests PRIVATE ${SQLITE3_INCLUDE_DIRS})
endif()

# Link against Google Test and application dependencies
if(PLATFORM_WINDOWS)
    target_link_libraries(phyxel_tests PRIVATE
        GTest::gtest_main
        GTest::gmock
        Vulkan::Vulkan
        glfw3
        ${BULLET_LIBS}
    )
    
    # Add SQLite3 if available
    if(SQLITE3_FOUND AND DEFINED SQLITE3_LIBRARIES)
        target_link_libraries(phyxel_tests PRIVATE ${SQLITE3_LIBRARIES})
    endif()
    
elseif(PLATFORM_LINUX)
    target_link_libraries(phyxel_tests PRIVATE
        GTest::gtest_main
        GTest::gmock
        Vulkan::Vulkan
        glfw
        ${BULLET_LIBS}
    )
    
    # Add SQLite3 if available
    if(SQLITE3_FOUND AND DEFINED SQLITE3_LIBRARIES)
        target_link_libraries(phyxel_tests PRIVATE ${SQLITE3_LIBRARIES})
    endif()
endif()

# Compile definitions (same as main executable)
target_compile_definitions(phyxel_tests PRIVATE 
    GLM_ENABLE_EXPERIMENTAL
    $<$<CONFIG:Debug>:DEBUG>
    $<$<CONFIG:Release>:NDEBUG>
)

# Add SQLite3 support if available
if(SQLITE3_FOUND)
    target_compile_definitions(phyxel_tests PRIVATE ENABLE_WORLD_STORAGE)
endif()

# Platform-specific settings
if(PLATFORM_WINDOWS AND MSVC)
    set_property(TARGET phyxel_tests PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreadedDebug$<$<CONFIG:Debug>:Debug>")
endif()

# Register tests with CTest
include(GoogleTest)
gtest_discover_tests(phyxel_tests)

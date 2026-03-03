cmake_minimum_required(VERSION 3.10)
project(VulkanCube)

set(CMAKE_CXX_STANDARD 17)

# Include custom CMake modules
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
include(Sanitizers)

# Prevent pybind11 from auto-linking python library via pragma
# This fixes the issue where it asks for python313t.lib when we only have python313.lib
add_compile_definitions(PYBIND11_NOPYTHON_LIB_PRAGMA)
# Force disable GIL disabled mode (free-threading) to match standard python313.lib
add_compile_definitions(Py_GIL_DISABLED=0)

# Platform detection
if(WIN32)
    message(STATUS "Building for Windows")
    set(PLATFORM_WINDOWS TRUE)
elseif(UNIX AND NOT APPLE)
    message(STATUS "Building for Linux")
    set(PLATFORM_LINUX TRUE)
endif()

# Vulkan (required on all platforms)
find_package(Vulkan REQUIRED)

if(NOT Vulkan_FOUND)
    message(FATAL_ERROR "Vulkan SDK not found! Please install Vulkan SDK from https://vulkan.lunarg.com/ and restart your terminal/IDE")
endif()

# SQLite3 (for world storage)
if(PLATFORM_WINDOWS)
    # Windows: Check for local SQLite3 installation first
    find_path(SQLITE3_INCLUDE_DIR sqlite3.h PATHS external/sqlite3/include)
    
    # Check if we have the amalgamation source
    if(EXISTS ${CMAKE_SOURCE_DIR}/external/sqlite3/include/sqlite3.c)
        set(SQLITE3_FOUND TRUE)
        set(SQLITE3_INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/external/sqlite3/include)
        set(SQLITE3_SOURCES ${CMAKE_SOURCE_DIR}/external/sqlite3/include/sqlite3.c)
        message(STATUS "Found SQLite3 amalgamation source")
    else()
        find_library(SQLITE3_LIBRARY sqlite3 PATHS external/sqlite3/lib)
        
        if(SQLITE3_INCLUDE_DIR AND SQLITE3_LIBRARY)
            set(SQLITE3_FOUND TRUE)
            set(SQLITE3_INCLUDE_DIRS ${SQLITE3_INCLUDE_DIR})
            set(SQLITE3_LIBRARIES ${SQLITE3_LIBRARY})
            message(STATUS "Found local SQLite3: ${SQLITE3_LIBRARY}")
        else()
            message(WARNING "SQLite3 not found. Download from https://www.sqlite.org/download.html and extract to external/sqlite3/")
            message(WARNING "Or world storage will be disabled.")
            set(SQLITE3_FOUND FALSE)
        endif()
    endif()
    
elseif(PLATFORM_LINUX)
    # Linux: Use pkg-config
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(SQLITE3 sqlite3)
    endif()
    
    if(NOT SQLITE3_FOUND)
        # Fallback: try to find SQLite3 manually
        find_path(SQLITE3_INCLUDE_DIR sqlite3.h)
        find_library(SQLITE3_LIBRARY sqlite3)
        
        if(SQLITE3_INCLUDE_DIR AND SQLITE3_LIBRARY)
            set(SQLITE3_FOUND TRUE)
            set(SQLITE3_INCLUDE_DIRS ${SQLITE3_INCLUDE_DIR})
            set(SQLITE3_LIBRARIES ${SQLITE3_LIBRARY})
            message(STATUS "Found SQLite3: ${SQLITE3_LIBRARY}")
        else()
            message(WARNING "SQLite3 not found. Install with: sudo apt install libsqlite3-dev")
            set(SQLITE3_FOUND FALSE)
        endif()
    else()
        message(STATUS "Found SQLite3 via pkg-config")
    endif()
endif()

# Bullet Physics (build from submodule on all platforms)
if(PLATFORM_WINDOWS)
    # Windows: Configure Bullet3 to use DLL runtime for GLFW compatibility
    set(USE_MSVC_RUNTIME_LIBRARY_DLL ON CACHE BOOL "Use MSVC DLL runtime" FORCE)
endif()

# Always disable Bullet demos and unit tests to avoid build errors
set(BUILD_BULLET2_DEMOS OFF CACHE BOOL "Disable Bullet2 demos" FORCE)
set(BUILD_UNIT_TESTS OFF CACHE BOOL "Disable Bullet unit tests" FORCE)
add_subdirectory(external/bullet3)

# ========================================================================
# Google Test Framework Setup (for unit testing)
# ========================================================================
# Download Google Test automatically using FetchContent (cross-platform)
include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0  # Latest stable release
)

# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

# Make Google Test available
FetchContent_MakeAvailable(googletest)

# Pybind11 for scripting
FetchContent_Declare(
  pybind11
  GIT_REPOSITORY https://github.com/pybind/pybind11.git
  GIT_TAG v2.12.0
)
FetchContent_MakeAvailable(pybind11)

# Debug Python detection
get_target_property(PYBIND11_INCLUDE_DIRS pybind11::headers INTERFACE_INCLUDE_DIRECTORIES)
message(STATUS "pybind11 include dirs: ${PYBIND11_INCLUDE_DIRS}")
# pybind11::embed links to Python, let's see if we can find where
get_target_property(PYBIND11_EMBED_LIBS pybind11::embed INTERFACE_LINK_LIBRARIES)
message(STATUS "pybind11::embed libs: ${PYBIND11_EMBED_LIBS}")


# Enable testing with CTest
enable_testing()
include(GoogleTest)

# Set Bullet library names with proper debug suffixes
if(PLATFORM_WINDOWS)
    set(BULLET_LIBS 
        $<$<CONFIG:Debug>:BulletDynamics_Debug>$<$<NOT:$<CONFIG:Debug>>:BulletDynamics>
        $<$<CONFIG:Debug>:BulletCollision_Debug>$<$<NOT:$<CONFIG:Debug>>:BulletCollision>
        $<$<CONFIG:Debug>:LinearMath_Debug>$<$<NOT:$<CONFIG:Debug>>:LinearMath>
    )
else()
    # Linux: Standard library names
    set(BULLET_LIBS BulletDynamics BulletCollision LinearMath)
endif()

# Platform-specific dependencies
if(PLATFORM_WINDOWS)
    # Windows: Use local copies of dependencies
    # GLM (header-only, from submodule)
    include_directories(${CMAKE_SOURCE_DIR}/external/glm)
    
    # GLFW (precompiled libraries)
    include_directories(${CMAKE_SOURCE_DIR}/external/glfw/include)
    link_directories(${CMAKE_SOURCE_DIR}/external/glfw/lib-vc2022)
    
    # Bullet3 libraries (built by CMake into our build directory)
    link_directories(${CMAKE_BINARY_DIR}/lib/$<CONFIG>)
    
elseif(PLATFORM_LINUX)
    # Linux: Use GLM from submodule and system GLFW
    include_directories(${CMAKE_SOURCE_DIR}/external/glm)
    find_package(glfw3 REQUIRED)
endif()

# Include directories
include_directories(include)

# ImGui setup
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

# Collect all source files
file(GLOB_RECURSE SOURCES 
    "src/*.cpp"
    "src/*.h"
)

# Add stb_image for texture loading
set(STB_IMAGE_DIR ${CMAKE_SOURCE_DIR}/external/bullet3/examples/ThirdPartyLibs/stb_image)
set(STB_IMAGE_SOURCES
    ${STB_IMAGE_DIR}/stb_image.cpp
)

# Add SQLite3 sources if using amalgamation
if(SQLITE3_FOUND AND DEFINED SQLITE3_SOURCES)
    list(APPEND SOURCES ${SQLITE3_SOURCES})
endif()

# Separate main.cpp from core sources
set(CORE_SOURCES ${SOURCES})
list(FILTER CORE_SOURCES EXCLUDE REGEX ".*main\\.cpp$")

# Create Core Library
add_library(phyxel_core STATIC ${CORE_SOURCES} ${IMGUI_SOURCES} ${STB_IMAGE_SOURCES})

# Enable Sanitizers for Core
enable_sanitizers(phyxel_core)

# Include directories for Core
target_include_directories(phyxel_core PUBLIC 
    ${Vulkan_INCLUDE_DIRS}
    include
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${STB_IMAGE_DIR}
    external/bullet3/src
    external/miniaudio
)

# Platform-specific linking for Core
if(PLATFORM_WINDOWS)
    target_link_libraries(phyxel_core PUBLIC 
        Vulkan::Vulkan 
        glfw3
        ${BULLET_LIBS}
        pybind11::embed
    )
    
    # Add SQLite3 if available
    if(SQLITE3_FOUND)
        if(DEFINED SQLITE3_LIBRARIES)
            target_link_libraries(phyxel_core PUBLIC ${SQLITE3_LIBRARIES})
        endif()
        target_include_directories(phyxel_core PUBLIC ${SQLITE3_INCLUDE_DIRS})
    endif()
    
elseif(PLATFORM_LINUX)
    target_link_libraries(phyxel_core PUBLIC 
        Vulkan::Vulkan 
        glfw
        ${BULLET_LIBS}
        pybind11::embed
    )
    
    # Add SQLite3 if available
    if(SQLITE3_FOUND)
        if(DEFINED SQLITE3_LIBRARIES)
            target_link_libraries(phyxel_core PUBLIC ${SQLITE3_LIBRARIES})
        endif()
        target_include_directories(phyxel_core PUBLIC ${SQLITE3_INCLUDE_DIRS})
    endif()
endif()

# Common compile definitions for Core
target_compile_definitions(phyxel_core PUBLIC 
    GLM_ENABLE_EXPERIMENTAL
    $<$<CONFIG:Debug>:DEBUG>
    $<$<CONFIG:Release>:NDEBUG>
    # Prevent pybind11 from auto-linking python library via pragma
    # This fixes the issue where it asks for python313t.lib when we only have python313.lib
    PYBIND11_NOPYTHON_LIB_PRAGMA
    # Force disable GIL disabled mode (free-threading)
    Py_GIL_DISABLED=0
)

# Add SQLite3 support if available
if(SQLITE3_FOUND)
    target_compile_definitions(phyxel_core PUBLIC ENABLE_WORLD_STORAGE)
    message(STATUS "World storage enabled with SQLite3")
else()
    message(STATUS "World storage disabled - SQLite3 not found")
endif()

# Create executable
add_executable(VulkanCube src/main.cpp)

# Link to Core
target_link_libraries(VulkanCube PRIVATE phyxel_core)

# Ignore python313t.lib if it's being requested by mistake
if(MSVC)
    set_target_properties(VulkanCube PROPERTIES LINK_FLAGS "/NODEFAULTLIB:python313t.lib")
endif()

# Enable Sanitizers for Executable
enable_sanitizers(VulkanCube)

# Platform-specific compiler settings
if(PLATFORM_WINDOWS)
    # MSVC runtime library (adjust as needed)
    if(MSVC)
        set_property(TARGET VulkanCube PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
        set_property(TARGET phyxel_core PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    endif()
    
    # Copy executable to project root on Windows
    add_custom_command(TARGET VulkanCube POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:VulkanCube> ${CMAKE_SOURCE_DIR}/
        COMMENT "Copying VulkanCube.exe to project root"
    )
endif()

# ========================================================================
# Unit and Integration Tests (Google Test)
# ========================================================================
# Add tests subdirectory (contains unit tests)
add_subdirectory(tests)
# Add integration tests subdirectory (requires Vulkan/Physics initialization)
add_subdirectory(tests/integration)
# Add benchmark tests subdirectory (performance and stress tests)
add_subdirectory(tests/benchmark)
# Add stress tests subdirectory (extreme load and endurance tests)
add_subdirectory(tests/stress)
# Add end-to-end tests subdirectory (complete application workflows)
add_subdirectory(tests/e2e)
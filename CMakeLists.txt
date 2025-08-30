cmake_minimum_required(VERSION 3.10)
project(VulkanCube)

set(CMAKE_CXX_STANDARD 17)

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

# Add SQLite3 sources if using amalgamation
if(SQLITE3_FOUND AND DEFINED SQLITE3_SOURCES)
    list(APPEND SOURCES ${SQLITE3_SOURCES})
endif()

# Create executable
add_executable(VulkanCube ${SOURCES} ${IMGUI_SOURCES})

# Platform-specific compiler settings
if(PLATFORM_WINDOWS)
    # Windows-specific settings
    target_compile_definitions(VulkanCube PRIVATE GLM_ENABLE_EXPERIMENTAL)
    
    # MSVC runtime library (adjust as needed)
    if(MSVC)
        set_property(TARGET VulkanCube PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreadedDebug$<$<CONFIG:Debug>:Debug>")
    endif()
    
    # Copy executable to project root on Windows
    add_custom_command(TARGET VulkanCube POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:VulkanCube> ${CMAKE_SOURCE_DIR}/
        COMMENT "Copying VulkanCube.exe to project root"
    )
endif()

# Include directories for all platforms
target_include_directories(VulkanCube PRIVATE 
    ${Vulkan_INCLUDE_DIRS}
    include
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)

# Platform-specific linking
if(PLATFORM_WINDOWS)
    target_link_libraries(VulkanCube PRIVATE 
        Vulkan::Vulkan 
        glfw3
        ${BULLET_LIBS}
    )
    
    # Add SQLite3 if available
    if(SQLITE3_FOUND)
        if(DEFINED SQLITE3_LIBRARIES)
            target_link_libraries(VulkanCube PRIVATE ${SQLITE3_LIBRARIES})
        endif()
        target_include_directories(VulkanCube PRIVATE ${SQLITE3_INCLUDE_DIRS})
    endif()
    
elseif(PLATFORM_LINUX)
    target_link_libraries(VulkanCube PRIVATE 
        Vulkan::Vulkan 
        glfw
        ${BULLET_LIBS}
    )
    
    # Add SQLite3 if available
    if(SQLITE3_FOUND)
        if(DEFINED SQLITE3_LIBRARIES)
            target_link_libraries(VulkanCube PRIVATE ${SQLITE3_LIBRARIES})
        endif()
        target_include_directories(VulkanCube PRIVATE ${SQLITE3_INCLUDE_DIRS})
    endif()
endif()

# Include Bullet headers for all platforms
target_include_directories(VulkanCube PRIVATE external/bullet3/src)

# Common compile definitions
target_compile_definitions(VulkanCube PRIVATE 
    GLM_ENABLE_EXPERIMENTAL
    $<$<CONFIG:Debug>:DEBUG>
    $<$<CONFIG:Release>:NDEBUG>
)

# Add SQLite3 support if available
if(SQLITE3_FOUND)
    target_compile_definitions(VulkanCube PRIVATE ENABLE_WORLD_STORAGE)
    message(STATUS "World storage enabled with SQLite3")
else()
    message(STATUS "World storage disabled - SQLite3 not found")
endif()
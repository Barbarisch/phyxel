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

# Bullet Physics (build from submodule on all platforms)
if(PLATFORM_WINDOWS)
    # Windows: Configure Bullet3 to use DLL runtime for GLFW compatibility
    set(USE_MSVC_RUNTIME_LIBRARY_DLL ON CACHE BOOL "Use MSVC DLL runtime" FORCE)
endif()

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

# Collect all source files
file(GLOB_RECURSE SOURCES 
    "src/*.cpp"
    "src/*.h"
)

# Create executable
add_executable(VulkanCube ${SOURCES})

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
)

# Platform-specific linking
if(PLATFORM_WINDOWS)
    target_link_libraries(VulkanCube PRIVATE 
        Vulkan::Vulkan 
        glfw3
        ${BULLET_LIBS}
    )
    
elseif(PLATFORM_LINUX)
    target_link_libraries(VulkanCube PRIVATE 
        Vulkan::Vulkan 
        glfw
        ${BULLET_LIBS}
    )
endif()

# Include Bullet headers for all platforms
target_include_directories(VulkanCube PRIVATE external/bullet3/src)

# Common compile definitions
target_compile_definitions(VulkanCube PRIVATE 
    GLM_ENABLE_EXPERIMENTAL
    $<$<CONFIG:Debug>:DEBUG>
    $<$<CONFIG:Release>:NDEBUG>
)
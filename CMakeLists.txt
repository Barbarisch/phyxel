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

# Platform-specific dependencies
if(PLATFORM_WINDOWS)
    # Windows: Use local copies of dependencies
    set(BULLET_ROOT "${CMAKE_SOURCE_DIR}/external/bullet3")
    set(BULLET_INCLUDE_DIR ${BULLET_ROOT}/src)
    
    # GLM (header-only, local copy)
    include_directories(${CMAKE_SOURCE_DIR}/external/glm)
    
    # GLFW (precompiled libraries)
    include_directories(${CMAKE_SOURCE_DIR}/external/glfw/include)
    link_directories(${CMAKE_SOURCE_DIR}/external/glfw/lib-vc2022)
    
    # Bullet libraries path (adjust based on your build configuration)
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        link_directories(${BULLET_ROOT}/build/lib/Debug)
        set(BULLET_LIBS BulletDynamics_Debug BulletCollision_Debug LinearMath_Debug)
    else()
        link_directories(${BULLET_ROOT}/build/lib/Release)
        set(BULLET_LIBS BulletDynamics BulletCollision LinearMath)
    endif()
    
    include_directories(${BULLET_INCLUDE_DIR})
    
elseif(PLATFORM_LINUX)
    # Linux: Use system packages
    find_package(glfw3 REQUIRED)
    find_package(glm REQUIRED)
    
    # Bullet Physics (build from submodule)
    add_subdirectory(external/bullet3)
    set(BULLET_LIBS BulletDynamics BulletCollision LinearMath)
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
    
    # Include Bullet headers for Windows
    target_include_directories(VulkanCube PRIVATE external/bullet3/src)
    
elseif(PLATFORM_LINUX)
    target_link_libraries(VulkanCube PRIVATE 
        Vulkan::Vulkan 
        glfw
        ${BULLET_LIBS}
    )
    
    # Include Bullet headers for Linux
    target_include_directories(VulkanCube PRIVATE external/bullet3/src)
endif()

# Common compile definitions
target_compile_definitions(VulkanCube PRIVATE 
    GLM_ENABLE_EXPERIMENTAL
    $<$<CONFIG:Debug>:DEBUG>
    $<$<CONFIG:Release>:NDEBUG>
)
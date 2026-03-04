cmake_minimum_required(VERSION 3.15)
project(phyxel VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Include custom CMake modules
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
include(Sanitizers)

# Prevent pybind11 from auto-linking python library via pragma
add_compile_definitions(PYBIND11_NOPYTHON_LIB_PRAGMA)
# Force disable GIL disabled mode (free-threading)
add_compile_definitions(Py_GIL_DISABLED=0)

# ==========================================================================
# Platform Detection
# ==========================================================================
if(WIN32)
    message(STATUS "Building for Windows")
    set(PLATFORM_WINDOWS TRUE)
elseif(UNIX AND NOT APPLE)
    message(STATUS "Building for Linux")
    set(PLATFORM_LINUX TRUE)
endif()

# ==========================================================================
# Dependencies
# ==========================================================================

# Vulkan (required on all platforms)
find_package(Vulkan REQUIRED)

if(NOT Vulkan_FOUND)
    message(FATAL_ERROR "Vulkan SDK not found! Please install from https://vulkan.lunarg.com/")
endif()

# SQLite3 (for world storage)
if(PLATFORM_WINDOWS)
    find_path(SQLITE3_INCLUDE_DIR sqlite3.h PATHS external/sqlite3/include)

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
            message(WARNING "SQLite3 not found.")
            set(SQLITE3_FOUND FALSE)
        endif()
    endif()

elseif(PLATFORM_LINUX)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(SQLITE3 sqlite3)
    endif()

    if(NOT SQLITE3_FOUND)
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

# Bullet Physics (build from submodule)
if(PLATFORM_WINDOWS)
    set(USE_MSVC_RUNTIME_LIBRARY_DLL ON CACHE BOOL "Use MSVC DLL runtime" FORCE)
endif()
set(BUILD_BULLET2_DEMOS OFF CACHE BOOL "Disable Bullet2 demos" FORCE)
set(BUILD_UNIT_TESTS OFF CACHE BOOL "Disable Bullet unit tests" FORCE)
add_subdirectory(external/bullet3)

# ==========================================================================
# FetchContent Dependencies
# ==========================================================================
include(FetchContent)

# Google Test
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# Pybind11 for scripting
FetchContent_Declare(
    pybind11
    GIT_REPOSITORY https://github.com/pybind/pybind11.git
    GIT_TAG v2.12.0
)
FetchContent_MakeAvailable(pybind11)

# nlohmann/json (header-only JSON library)
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)

# cpp-httplib (header-only HTTP client/server)
set(HTTPLIB_REQUIRE_OPENSSL OFF CACHE BOOL "Disable OpenSSL" FORCE)
set(HTTPLIB_REQUIRE_BROTLI OFF CACHE BOOL "Disable Brotli" FORCE)
set(HTTPLIB_REQUIRE_ZLIB OFF CACHE BOOL "Disable Zlib" FORCE)
FetchContent_Declare(
    httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG v0.18.3
)
FetchContent_MakeAvailable(httplib)

# Debug Python detection
get_target_property(PYBIND11_INCLUDE_DIRS pybind11::headers INTERFACE_INCLUDE_DIRECTORIES)
message(STATUS "pybind11 include dirs: ${PYBIND11_INCLUDE_DIRS}")
get_target_property(PYBIND11_EMBED_LIBS pybind11::embed INTERFACE_LINK_LIBRARIES)
message(STATUS "pybind11::embed libs: ${PYBIND11_EMBED_LIBS}")

# ==========================================================================
# Platform-specific link/search paths
# ==========================================================================
if(PLATFORM_WINDOWS)
    set(BULLET_LIBS
        $<$<CONFIG:Debug>:BulletDynamics_Debug>$<$<NOT:$<CONFIG:Debug>>:BulletDynamics>
        $<$<CONFIG:Debug>:BulletCollision_Debug>$<$<NOT:$<CONFIG:Debug>>:BulletCollision>
        $<$<CONFIG:Debug>:LinearMath_Debug>$<$<NOT:$<CONFIG:Debug>>:LinearMath>
    )
    link_directories(${CMAKE_SOURCE_DIR}/external/glfw/lib-vc2022)
    link_directories(${CMAKE_BINARY_DIR}/lib/$<CONFIG>)
elseif(PLATFORM_LINUX)
    set(BULLET_LIBS BulletDynamics BulletCollision LinearMath)
    find_package(glfw3 REQUIRED)
endif()

# ==========================================================================
# Common source definitions (used by engine/CMakeLists.txt)
# ==========================================================================
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

set(STB_IMAGE_DIR ${CMAKE_SOURCE_DIR}/external/bullet3/examples/ThirdPartyLibs/stb_image)
set(STB_IMAGE_SOURCES ${STB_IMAGE_DIR}/stb_image.cpp)

# ==========================================================================
# Build Targets
# ==========================================================================
add_subdirectory(engine)    # phyxel_core  — engine static library
add_subdirectory(game)      # phyxel_game  — game static library
                            # phyxel       — game executable
add_subdirectory(editor)    # phyxel_editor — editor executable (stub)

# ==========================================================================
# Testing
# ==========================================================================
enable_testing()
include(GoogleTest)

add_subdirectory(tests)
add_subdirectory(tests/integration)
add_subdirectory(tests/benchmark)
add_subdirectory(tests/stress)
add_subdirectory(tests/e2e)

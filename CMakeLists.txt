cmake_minimum_required(VERSION 3.15)
project(phyxel VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Phyxel source root — works when used standalone or as add_subdirectory
set(PHYXEL_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}" CACHE INTERNAL "Phyxel engine source root")

# Include custom CMake modules
list(APPEND CMAKE_MODULE_PATH "${PHYXEL_ROOT_DIR}/cmake")
include(Sanitizers)

# Address Sanitizer: apply globally before external deps so all libraries match ABI.
# Pass -DENABLE_ASAN=ON for a Debug ASan build. Requires clean rebuild.
option(ENABLE_ASAN "Enable Address Sanitizer (Debug builds)" OFF)
if(ENABLE_ASAN)
    if(MSVC)
        add_compile_options($<$<CONFIG:Debug>:/fsanitize=address>)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        if(CMAKE_BUILD_TYPE STREQUAL "Debug")
            add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
            add_link_options(-fsanitize=address)
        endif()
    endif()
    message(STATUS "Address Sanitizer ENABLED for Debug builds")
endif()

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

    if(EXISTS ${PHYXEL_ROOT_DIR}/external/sqlite3/include/sqlite3.c)
        set(SQLITE3_FOUND TRUE)
        set(SQLITE3_INCLUDE_DIRS ${PHYXEL_ROOT_DIR}/external/sqlite3/include)
        set(SQLITE3_SOURCES ${PHYXEL_ROOT_DIR}/external/sqlite3/include/sqlite3.c)
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

# Bullet Physics submodule kept as reference but NOT built or linked.
# Bullet-dependent character classes have been moved to engine/deprecated/bullet/.

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
    set(PHYXEL_GLFW_LIB_DIR ${PHYXEL_ROOT_DIR}/external/glfw/lib-vc2022 CACHE INTERNAL "")
    set(PHYXEL_BUILD_LIB_DIR ${CMAKE_BINARY_DIR}/lib/$<CONFIG> CACHE INTERNAL "")
elseif(PLATFORM_LINUX)
    find_package(glfw3 REQUIRED)
endif()

# ==========================================================================
# Common source definitions (used by engine/CMakeLists.txt)
# ==========================================================================
set(IMGUI_DIR ${PHYXEL_ROOT_DIR}/external/imgui)
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp
)

set(STB_IMAGE_DIR ${PHYXEL_ROOT_DIR}/external/bullet3/examples/ThirdPartyLibs/stb_image)
set(STB_IMAGE_SOURCES ${STB_IMAGE_DIR}/stb_image.cpp)

# ==========================================================================
# Build Targets
# ==========================================================================
add_subdirectory(engine)    # phyxel_core  — engine static library

# Game, editor, examples: only when building Phyxel standalone
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    add_subdirectory(editor)    # phyxel_editor — editor/dev-tool library + phyxel executable
    # add_subdirectory(examples/minimal_game)  # removed from working copy
endif()

# ==========================================================================
# Install targets — basic header/lib install for external projects
# For now, the recommended approach is add_subdirectory() (see create_project.py).
# Full find_package(Phyxel) support is future work (requires exporting all
# transitive dependencies: Vulkan, Bullet, pybind11, nlohmann_json, etc.).
# ==========================================================================
include(GNUInstallDirs)

install(TARGETS phyxel_core
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(DIRECTORY engine/include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/phyxel
)

# ==========================================================================
# Testing
# ==========================================================================
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    enable_testing()
    include(GoogleTest)

    add_subdirectory(tests)
    add_subdirectory(tests/integration)
    add_subdirectory(tests/benchmark)
    add_subdirectory(tests/stress)
    add_subdirectory(tests/e2e)
endif()

# ==========================================================================
# phyxel_core — Engine static library
# ==========================================================================

file(GLOB_RECURSE ENGINE_SOURCES "src/*.cpp" "src/*.h")

# Add SQLite3 amalgamation if available
if(SQLITE3_FOUND AND DEFINED SQLITE3_SOURCES)
    list(APPEND ENGINE_SOURCES ${SQLITE3_SOURCES})
endif()

add_library(phyxel_core STATIC
    ${ENGINE_SOURCES}
    ${IMGUI_SOURCES}
    ${STB_IMAGE_SOURCES}
)

enable_sanitizers(phyxel_core)

# Public include directories — anything linking phyxel_core gets these
target_include_directories(phyxel_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${Vulkan_INCLUDE_DIRS}
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${STB_IMAGE_DIR}
    ${PHYXEL_ROOT_DIR}/external/bullet3/src
    ${PHYXEL_ROOT_DIR}/external/glm
    ${PHYXEL_ROOT_DIR}/external/miniaudio
)

if(PLATFORM_WINDOWS)
    target_include_directories(phyxel_core PUBLIC
        ${PHYXEL_ROOT_DIR}/external/glfw/include
    )
endif()

if(SQLITE3_FOUND)
    target_include_directories(phyxel_core PUBLIC ${SQLITE3_INCLUDE_DIRS})
endif()

# Link libraries
if(PLATFORM_WINDOWS)
    target_link_libraries(phyxel_core PUBLIC
        Vulkan::Vulkan
        glfw3
        ${BULLET_LIBS}
        pybind11::embed
        nlohmann_json::nlohmann_json
        httplib::httplib
        winhttp
    )
    # Propagate library search paths to consumers (needed for add_subdirectory builds)
    target_link_directories(phyxel_core PUBLIC
        ${PHYXEL_GLFW_LIB_DIR}
        ${PHYXEL_BUILD_LIB_DIR}
    )
elseif(PLATFORM_LINUX)
    target_link_libraries(phyxel_core PUBLIC
        Vulkan::Vulkan
        glfw
        ${BULLET_LIBS}
        pybind11::embed
        nlohmann_json::nlohmann_json
        httplib::httplib
    )
endif()

if(SQLITE3_FOUND AND DEFINED SQLITE3_LIBRARIES)
    target_link_libraries(phyxel_core PUBLIC ${SQLITE3_LIBRARIES})
endif()

# Compile definitions
target_compile_definitions(phyxel_core PUBLIC
    GLM_ENABLE_EXPERIMENTAL
    $<$<CONFIG:Debug>:DEBUG>
    $<$<CONFIG:Release>:NDEBUG>
    PYBIND11_NOPYTHON_LIB_PRAGMA
    Py_GIL_DISABLED=0
)

if(SQLITE3_FOUND)
    target_compile_definitions(phyxel_core PUBLIC ENABLE_WORLD_STORAGE)
    message(STATUS "World storage enabled with SQLite3")
else()
    message(STATUS "World storage disabled - SQLite3 not found")
endif()

# MSVC runtime
if(PLATFORM_WINDOWS AND MSVC)
    set_property(TARGET phyxel_core PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

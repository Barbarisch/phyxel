# ==========================================================================
# phyxel_editor — Editor/dev-tool static library
# phyxel        — Editor executable (main Phyxel application)
# ==========================================================================

# Collect editor source files (everything except main.cpp)
file(GLOB_RECURSE EDITOR_SOURCES "src/*.cpp" "src/*.h")
list(FILTER EDITOR_SOURCES EXCLUDE REGEX ".*main\\.cpp$")

# Editor static library (Application, InputController, AI, scene entities, scripting bindings)
add_library(phyxel_editor STATIC ${EDITOR_SOURCES})

target_include_directories(phyxel_editor PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# phyxel_editor depends on phyxel_core (engine); PUBLIC so anything
# linking phyxel_editor also gets phyxel_core's headers and libs
target_link_libraries(phyxel_editor PUBLIC phyxel_core)

enable_sanitizers(phyxel_editor)

if(PLATFORM_WINDOWS AND MSVC)
    set_property(TARGET phyxel_editor PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

# ==========================================================================
# Editor executable
# ==========================================================================

add_executable(phyxel src/main.cpp)
target_link_libraries(phyxel PRIVATE phyxel_editor)
enable_sanitizers(phyxel)

if(MSVC)
    set_property(TARGET phyxel PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    set_target_properties(phyxel PROPERTIES LINK_FLAGS "/NODEFAULTLIB:python313t.lib")
endif()

# Copy executable to project root on Windows
if(PLATFORM_WINDOWS)
    add_custom_command(TARGET phyxel POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:phyxel> ${PHYXEL_ROOT_DIR}/
        COMMENT "Copying phyxel.exe to project root"
    )
endif()

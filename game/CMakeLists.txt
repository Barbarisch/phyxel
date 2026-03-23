# ==========================================================================
# phyxel_game — Game-specific static library
# phyxel      — Game executable
# ==========================================================================

# Collect game-specific source files (everything except main.cpp)
file(GLOB_RECURSE GAME_SOURCES "src/*.cpp" "src/*.h")
list(FILTER GAME_SOURCES EXCLUDE REGEX ".*main\\.cpp$")

# Game static library (game-specific logic: Application, Player, Enemy, etc.)
add_library(phyxel_game STATIC ${GAME_SOURCES})

target_include_directories(phyxel_game PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# phyxel_game depends on phyxel_core (engine); PUBLIC so anything
# linking phyxel_game also gets phyxel_core's headers and libs
target_link_libraries(phyxel_game PUBLIC phyxel_core)

enable_sanitizers(phyxel_game)

if(PLATFORM_WINDOWS AND MSVC)
    set_property(TARGET phyxel_game PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

# ==========================================================================
# Game executable
# ==========================================================================

add_executable(phyxel src/main.cpp)
target_link_libraries(phyxel PRIVATE phyxel_game)
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

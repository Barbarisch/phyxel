# ==========================================================================
# phyxel_minimal_game — Example standalone game using EngineRuntime
# ==========================================================================
# Demonstrates the simplest way to build a game against phyxel_core
# without the Application monolith.

add_executable(phyxel_minimal_game
    main.cpp
    MinimalGame.cpp
    MinimalGame.h
)

# Link directly against phyxel_core — no need for phyxel_game
target_link_libraries(phyxel_minimal_game PRIVATE phyxel_core)

# Include path for local headers
target_include_directories(phyxel_minimal_game PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

if(MSVC)
    set_property(TARGET phyxel_minimal_game PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    set_target_properties(phyxel_minimal_game PROPERTIES
        LINK_FLAGS "/NODEFAULTLIB:python313t.lib")
endif()

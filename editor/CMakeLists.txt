# ==========================================================================
# phyxel_editor — Game development editor (stub)
# ==========================================================================

add_executable(phyxel_editor src/main.cpp)
target_link_libraries(phyxel_editor PRIVATE phyxel_core)

if(MSVC)
    set_property(TARGET phyxel_editor PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

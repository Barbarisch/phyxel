---
name: phyxel-package
description: Use when saving, exporting, or packaging a Phyxel game for distribution — persisting world/player state, exporting a reusable game definition, and building a standalone, self-contained game executable (single- or multi-scene). Invoke for "save / export / package / ship / build a standalone of the game" tasks.
---

# Saving, exporting & packaging

## Save & export (in-engine)
- `save_world` — persist chunks to the project's `worlds/default.db` (`{"all": true}` for every
  loaded chunk).
- `save_player` — camera, health, spawn point, inventory.
- `export_game_definition` — write the current setup back to reusable JSON.
- `save_template` — save a structure as a reusable `.voxel` template.

> `load_game_definition` is an **editor preview**, not a project. To ship, build a standalone
> (below).

## Standalone game (distributable)
A self-contained project with its own C++ source + executable (no Python/MCP/dev tools). From
the engine repo:
```
# 1. Scaffold
python tools/create_project.py MyGame --game-definition path/to/my_game.json   # -> Documents/PhyxelProjects/MyGame
# 2. Copy assets the scaffolder leaves empty: shaders/*.spv, resources/textures/cube_atlas.png/.json,
#    and any .anim files the NPCs use.
# 3. Pre-bake the world: save_world {all:true}, then copy worlds/default.db into the project.
# 4. Build (CMake not on PATH — add MSVC's CMake first):
cmake -B build -S . ; cmake --build build --config Debug   # (from the project dir)
# 5. Package:
python tools/package_game.py MyGame --project-dir <project>
```
Useful `package_game.py` flags: `--prebake-world`, `--world-db <db>`, `--all-resources`,
`--config Release` (final), `--output <path>`.

## Multi-scene
`create_project.py` auto-detects a `"scenes"` array and generates SceneManager wiring +
`onSceneLoad`/`onSceneUnload`/`onSceneReady` stubs. `package_game.py` copies every scene's
`worldDatabase` into the output `worlds/`.

Full detail + the output directory layout: `docs/GameCreationGuide.md` (Step 10) and
`docs/SceneSystem.md`.

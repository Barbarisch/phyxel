# CLAUDE.md — Phyxel Voxel Game Engine

## Project Overview

Phyxel is a voxel game engine built with C++17, Vulkan, and Bullet Physics. It features a 32³ chunk system, physics-based entity characters, animated voxel characters, embedded Python scripting, and an MCP server for AI agent integration.

## Build System

- **CMake 3.15+**, C++17 required, MSVC 2022
- **Dependencies**: Vulkan SDK, GLFW, GLM, Bullet Physics, ImGui, pybind11, nlohmann/json, cpp-httplib, Google Test, SQLite3, miniaudio
- **CMake is NOT in system PATH**. Before running cmake, add it:
  ```powershell
  $env:PATH += ";C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
  ```
- **Build commands**:
  ```powershell
  cmake -B build -S .
  cmake --build build --config Debug
  ```
- **Build + test shortcut**: `./build_and_test.ps1 -Config Debug -RunTests`
  - `-UnitOnly`, `-IntegrationOnly`, `-BenchmarkOnly`, `-StressOnly`, `-E2EOnly` flags available
- **Targets**: `phyxel_core` (engine static lib), `phyxel_editor` (editor/dev-tool lib + executable)
- **Test suites**: `tests/`, `tests/integration/`, `tests/benchmark/`, `tests/stress/`, `tests/e2e/`

## Coordinate System

- **Right-handed**: X = right (east), **Y = up**, Z = toward viewer (north)
- **Chunk size**: 32×32×32 = 32,768 voxels per chunk
- **Index formula** (Z-minor, matches X-major loop order):
  ```cpp
  size_t index = z + y*32 + x*1024;   // Z stride=1, Y stride=32, X stride=1024
  ```
- **Conversions**:
  ```cpp
  chunkCoord = worldPos / 32;
  localPos   = worldPos % 32;
  chunkOrigin = chunkCoord * 32;
  ```
- World coordinates can be negative. Local coordinates are always 0–31.

## Materials

Predefined in `MaterialManager` (engine/src/physics/Material.cpp). Names are **case-sensitive**.
Each material has its own 6-face texture set in the atlas (72 textures total, 256x256 atlas).

| Name     | Mass | Friction | Restitution | Color Tint (RGB)        | Notes                    |
|----------|------|----------|-------------|-------------------------|--------------------------|
| Wood     | 0.7  | 0.6      | 0.2         | (0.8, 0.6, 0.3) brown   | Light, natural feel      |
| Metal    | 4.0  | 0.7      | 0.1         | (0.7, 0.7, 0.8) silver  | Heavy, high density      |
| Glass    | 1.5  | 0.2      | 0.6         | (0.9, 0.95, 1.0) clear  | Brittle, bouncy          |
| Rubber   | 0.5  | 0.9      | 0.9         | (0.3, 0.3, 0.3) dark    | Very bouncy              |
| Stone    | 6.0  | 0.8      | 0.05        | (0.6, 0.5, 0.4) gray    | Heaviest, rough          |
| Ice      | 0.9  | 0.1      | 0.4         | (0.8, 0.9, 1.0) blue    | Very slippery            |
| Cork     | 0.2  | 0.5      | 0.4         | (0.8, 0.7, 0.5) tan     | Ultra-light              |
| glow     | 1.0  | 0.5      | 0.5         | (1.0, 1.0, 1.0) white   | Emissive material        |
| Default  | 1.0  | 0.5      | 0.3         | (1.0, 1.0, 1.0)         | Balanced defaults        |

### Texture Pipeline
- Source textures: `textures/` dir, 18x18 PNG per face per material (e.g. `stone_side_n.png`)
- Atlas builder: `python tools/texture_atlas_builder.py textures/ cube_atlas.png`
- Outputs: `resources/textures/cube_atlas.{png,json,h,glsl}`
- Texture generation: `python tools/generate_material_textures.py` (regenerates procedural textures)
- After atlas changes, rebuild shaders: `.\build_shaders.bat`
- Material→texture lookup: `TextureConstants::getTextureIndexForMaterial(materialName, faceID)` in `Types.h`

## Entity Types

Spawnable via MCP `spawn_entity` or keybindings:

| Type       | Class                    | Capabilities |
|------------|--------------------------|--------------|
| `physics`  | `PhysicsCharacter`       | Humanoid ragdoll with Bullet Physics, WASD movement, camera-attached |
| `spider`   | (spider enemy)           | Spider-type enemy entity |
| `animated` | `AnimatedVoxelCharacter` | Voxel character loaded from `.anim` files. States: Idle, Walk, Run, Jump, Fall, Land, Crouch, CrouchIdle, CrouchWalk, StandUp, Attack, TurnLeft/Right, StrafeLeft/Right, WalkStrafeLeft/Right, Preview |

Control mode toggled with **K** key. Animated character supports Jump (Space), Attack (Left Click), Crouch (Ctrl), Sprint (Shift), Derez/explode (X).

## Chunk Layout

- Each chunk is 32×32×32. Chunks are positioned by their world-space origin (ivec3).
- **Default startup**: Loads all chunks from `worlds/default.db`. If database is empty, starts with an **empty world** (for Python scripting).
- Initial camera: position (50, 50, 50), looking toward (16, 16, 16), yaw=-135°, pitch=-30°.
- Chunks can be created dynamically: `MultiChunkDemo::createLinearChunks(n)`, `createGridChunks(w,h)`, `create3DGridChunks(x,y,z)`.
- World storage: SQLite database at `worlds/default.db`. Save with `save_world` MCP tool.

## World Generation

`WorldGenerator` supports these generation types (enum `GenerationType`):

| Type       | Description |
|------------|-------------|
| `Random`   | 70% fill rate, deterministic from seed |
| `Perlin`   | Height-map terrain using Perlin noise, base level Y=16 |
| `Flat`     | Solid below Y=16, empty above |
| `Mountains`| Multi-octave noise, base level Y=20, peaks up to ~60 |
| `Caves`    | Perlin terrain with 3D cave carving |
| `City`     | Flat ground at Y=15, procedural 16×16 buildings (height 20-60) |
| `Custom`   | User-supplied generation function |

**Multi-material terrain**: Generated terrain uses materials based on depth from surface:
- Surface layer: Default (or Ice on mountain peaks above Y=45)
- Sub-surface (1-4 blocks deep): Cork (earthy brown)
- Deep underground: Stone
- City buildings: Metal, ground: Stone/Default

The Python script `scripts/world_gen.py` provides demo functions: `generate_pyramid()`, `generate_platform()`, `generate_glow_pillars()`, `run_demo()`.

## Object Templates

Files in `resources/templates/` (spawnable via **T** / **Shift+T** or MCP `spawn_template`):

- `tree.txt`, `tree2.txt`, `tree2_hollow.txt`
- `sphere.txt`, `sphere_hollow.txt`
- `test_castle_optimized.txt`, `test_castle_optimized_lossy.txt`
- `my_model.txt`

**T** = spawn static (merged into terrain), **Shift+T** = spawn dynamic (physics objects).

## Animation Files

Root-level: `character.anim`, `character_box.anim`, `character_complete.anim`

In `resources/animated_characters/`:
- `character_wolf.anim`
- `character_spider.anim`, `character_spider2.anim`, `character_spider3.anim`
- `character_female.anim`, `character_female2.anim`, `character_female3.anim`
- `character_dragon.anim`

## Keybindings

### General
| Key | Action |
|-----|--------|
| ESC | Exit |
| F1 | Toggle Performance Overlay |
| F3 | Toggle Force Debug Vis |
| F4 | Toggle Debug Rendering |
| Ctrl+F4 | Cycle Debug Vis Mode |
| F5 | Toggle Raycast Vis |
| F6 | Toggle Lighting Controls |
| F7 | Toggle Profiler |
| ` (backtick) | Toggle Scripting Console |

### Camera & World
| Key | Action |
|-----|--------|
| V | Toggle Camera Mode (First/Third/Free) |
| C | Place Cube |
| Ctrl+C | Place Subcube |
| Alt+C | Place Microcube |
| Left Click | Break Voxel |
| Middle Click | Subdivide Cube |
| T / Shift+T | Spawn Static / Dynamic Template |
| P | Toggle Template Preview |
| -/= | Decrease/Increase Ambient Light |
| [/] | Decrease/Increase Spawn Speed |

### Character
| Key | Action |
|-----|--------|
| K | Toggle Character Control (Physics/Spider/Animated) |
| W/A/S/D | Movement |
| Shift | Sprint |
| Space | Jump (Animated) |
| Left Click | Attack (Animated) |
| Ctrl | Crouch (Animated) |
| X | Derez Character (explode into physics objects) |
| N/B | Next/Previous Animation (Preview Mode) |

## MCP Server (AI Agent Bridge)

Server: `scripts/mcp/phyxel_mcp_server.py` — connects to engine HTTP API at `localhost:8090`.

**Setup** (Claude Code config):
```json
{
  "mcpServers": {
    "phyxel": {
      "command": "python",
      "args": ["scripts/mcp/phyxel_mcp_server.py"],
      "cwd": "<path-to-phyxel-repo>"
    }
  }
}
```

**Requirements**: `pip install mcp httpx`. Engine must be running.

### MCP Tools

| Tool | Description |
|------|-------------|
| `engine_status` | Check if engine is running and responsive |
| `get_world_state` | Full snapshot: entities, camera, counts |
| `get_camera` | Camera position, yaw/pitch, front vector |
| `list_entities` | All entities with IDs, positions, types |
| `get_entity` | Detailed info for one entity |
| `spawn_entity` | Spawn physics/spider/animated entity at position |
| `move_entity` | Teleport entity to new position |
| `remove_entity` | Remove entity from world |
| `update_entity` | Update entity position/rotation/scale/color |
| `place_voxel` | Place single voxel with optional material |
| `remove_voxel` | Remove voxel at coordinates |
| `query_voxel` | Check if voxel exists at coordinates |
| `place_voxels_batch` | Place multiple voxels efficiently |
| `fill_region` | Fill 3D box with material (optional hollow). Max 100k |
| `clear_region` | Remove all voxels in 3D box. Max 100k |
| `scan_region` | Read all voxels in 3D box |
| `list_templates` | List available object templates |
| `spawn_template` | Place template at position (static or dynamic) |
| `list_materials` | List all materials with properties |
| `get_chunk_info` | Loaded chunk count, origins, stats |
| `set_camera` | Move/orient camera |
| `run_script` | Execute Python in engine interpreter |
| `screenshot` | Capture current game view as PNG |
| `save_world` | Save world to SQLite (dirty or all chunks) |
| `poll_events` | Poll game events since cursor (entity spawn/remove/move, voxel place/remove, region fill/clear, world save) |
| `create_snapshot` | Capture named snapshot of a region for undo. Max 100k voxels, 50 snapshots |
| `restore_snapshot` | Restore a snapshot (clears region, replaces voxels with saved state) |
| `list_snapshots` | List all stored snapshots with metadata |
| `delete_snapshot` | Delete a named snapshot to free memory |
| `copy_region` | Copy voxel region to clipboard (relative offsets). Max 100k voxels |
| `paste_region` | Paste clipboard at new position with optional Y-axis rotation (0/90/180/270) |
| `get_clipboard` | Check clipboard status (has data, size, voxel count) |
| `generate_world` | Generate procedural terrain (Random/Perlin/Flat/Mountains/Caves/City) for chunks. Max 64 chunks |
| `save_template` | Save a region as a reusable .txt template file, immediately available for spawn_template |
| `load_game_definition` | Load a complete game from a single JSON definition (world, structures, player, NPCs, story) |
| `export_game_definition` | Export current game state as a game definition JSON |
| `validate_game_definition` | Validate a game definition JSON without loading |
| `create_game_npc` | Composite: spawn NPC + dialogue + story character in one call |
| `build_project` | Build the Phyxel engine (cmake configure + build) |
| `launch_engine` | Launch the engine executable as background process |
| `engine_running` | Check if engine process is alive and API is responsive |
| `package_game` | Package game into standalone distributable directory (no source/build files) |
| `project_info` | Get info about the game project loaded via --project (dir, exe, game.json) |
| `build_game` | Build the game project from within the running engine (cmake) |
| `run_game` | Launch the built game executable from within the running engine |
| `list_entity_animations` | List all animation clips on an animated entity |
| `play_entity_animation` | Play a named animation clip (puts entity in Preview mode) |
| `get_animation_state` | Get FSM state, current clip, progress, duration, blend duration |
| `set_animation_state` | Set FSM state (Idle, Walk, Run, Jump, Attack, Preview, etc.) |
| `set_blend_duration` | Set crossfade blend duration (seconds) for transitions |
| `reload_entity_animation` | Hot-reload animation clips from .anim file (preserves skeleton/model) |

### AI Game Development Workflow

The `load_game_definition` tool enables creating entire games from a single JSON payload:

```json
{
  "name": "My Game",
  "world": {"type": "Perlin", "from": {"x":-1,"y":0,"z":-1}, "to": {"x":1,"y":0,"z":1}},
  "player": {"type": "animated", "position": {"x":16,"y":20,"z":16}},
  "camera": {"position": {"x":50,"y":50,"z":50}, "yaw": -135, "pitch": -30},
  "npcs": [
    {
      "name": "Guard", "position": {"x":10,"y":20,"z":10},
      "behavior": "patrol",
      "waypoints": [{"x":10,"y":20,"z":10}, {"x":20,"y":20,"z":10}],
      "dialogue": {
        "id": "guard_talk", "startNodeId": "start",
        "nodes": [{"id":"start","speaker":"Guard","text":"Hello traveler!"}]
      },
      "storyCharacter": {
        "id": "guard", "faction": "town_guard", "agencyLevel": 1,
        "traits": {"openness": 0.3, "extraversion": 0.7},
        "goals": [{"id":"protect","description":"Protect the town","priority":0.9}]
      }
    }
  ],
  "structures": [
    {"type": "fill", "from": {"x":0,"y":0,"z":0}, "to": {"x":31,"y":15,"z":31}, "material": "Stone"}
  ],
  "story": {
    "arcs": [{"id":"main","name":"Main Quest","constraintMode":"Guided",
              "beats":[{"id":"b1","description":"Meet the guard","type":"Hard"}]}]
  }
}
```

**Typical AI workflow:**
1. `build_project` — Build the engine
2. `launch_engine` — Start the engine (use `args: ["--project", "<path>"]` to open a game project)
3. `engine_running` — Verify it's ready
4. `load_game_definition` — Load the full game definition
5. `screenshot` — See the result
6. Iterate with `create_game_npc`, `fill_region`, `place_voxel`, etc.
7. `build_game` — Build the game from within the engine
8. `run_game` — Launch the built game to test it
9. `package_game` — Package into standalone distributable directory

**Engine as Editor (--project mode):**
Launch the engine with `--project <dir>` to open a game project for development:
```
phyxel.exe --project C:\Users\jack\Documents\PhyxelProjects\FrozenHighlands
```
- Loads world from the project's `worlds/default.db`
- Auto-loads game definition from project's `game.json`
- Window title set from project's `engine.json`
- `save_world` writes back to the project's database
- `build_game` / `run_game` API endpoints build and launch the standalone game

**Game Project Scaffolding:**
Each game gets its own C++ source that links against `phyxel_core`. Games are
standalone executables — no Python scripting, MCP server, or dev tools included.

- Scaffold: `python tools/create_project.py MyGame --game-definition game.json`
- Build: `cd <project_dir> && cmake -B build -S . && cmake --build build --config Debug`
- The generated code uses `GameCallbacks` + `EngineRuntime` (same pattern as `examples/minimal_game/`)
- World terrain is pre-baked in `worlds/default.db` (instant startup)
- NPCs, dialogue, story, and camera are loaded from `game.json` at runtime (world key is stripped)

**Game Packaging:**
- `python tools/package_game.py MyGame --project-dir path/to/MyGame` — Package a game project
- `python tools/package_game.py MyGame --prebake-world --from-engine` — Pre-bake world + package
- `python tools/package_game.py MyGame --definition game.json` — Legacy: use engine exe
- Output in `Documents/PhyxelProjects/<GameName>/` — self-contained, no engine source
- See `docs/GameCreationGuide.md` for full workflow
- Use `@game-creator` Copilot agent for AI-assisted game creation

## Project Structure

```
engine/          # phyxel_core static library
  include/       # Public headers (core/, graphics/, physics/, scene/, ui/, input/, utils/)
  src/           # Implementation
editor/          # phyxel_editor lib + phyxel executable (dev tool / world editor)
  include/       # Editor-specific headers (Application, InputController, AI, scene entities)
  src/           # Application.cpp, scripting bindings, etc.
examples/        # Standalone game examples
  minimal_game/  # Reference: GameCallbacks + EngineRuntime pattern
tests/           # Unit tests (Google Test)
  integration/   # Integration tests
  benchmark/     # Benchmark tests
  stress/        # Stress tests
  e2e/           # End-to-end tests
tools/           # Development tools
  create_project.py  # Scaffold new game projects
  package_game.py    # Package games into distributable directories
  anim_editor.py     # .anim file editor (list, add, remove, merge, export, generate, mirror, scale)
scripts/         # Python scripts (world_gen.py, startup.py, audio_demo.py)
  mcp/           # MCP server for AI agents
resources/       # Templates, animations, textures, sounds, recipes
shaders/         # GLSL shaders + compiled SPIR-V
external/        # Third-party: bullet3, glfw, glm, imgui, goose, miniaudio, sqlite3
docs/            # Documentation
```

## Key Engine Subsystems

- **ChunkManager**: Manages all 32³ chunks, Vulkan buffers, voxel maps, face culling
- **ChunkStreamingManager**: SQLite-backed chunk persistence and streaming
- **WorldGenerator**: Procedural terrain generation (Perlin, Flat, Mountains, Caves, City)
- **PhysicsWorld**: Bullet Physics integration for rigid body dynamics
- **EntityRegistry**: Central O(1) entity lookup by string ID, spatial queries, type queries
- **ForceSystem**: Applies forces to physics entities
- **RenderPipeline / RenderCoordinator**: Vulkan rendering with instanced drawing
- **ScriptingSystem**: Embedded Python (pybind11) with `phyxel` module
- **EngineAPIServer**: HTTP API on port 8090 for external tool integration
- **InputManager**: GLFW input handling, camera control, keybindings

## Common Patterns

- **Namespace**: `Phyxel::Core`, `Phyxel::Physics`, `Phyxel::Graphics`, `Phyxel::Scene`, `Phyxel::UI`, `Phyxel::Input`, `Phyxel::Utils`
- **Logging**: `LOG_INFO("Tag", "message")`, `LOG_DEBUG`, `LOG_WARN`, `LOG_ERROR`, `LOG_TRACE_FMT`
- **Entity registration**: `registry.registerEntity(entity, "my_id", "type_tag")`
- **Voxel placement**: `chunkManager->addCube(worldX, worldY, worldZ)` or with material: `addCubeWithMaterial(x, y, z, "Stone")`

## Testing

### Automated Tests
```powershell
# All unit tests (1070+, 3 AI E2E auto-skip without API key)
.\build\tests\Debug\phyxel_tests.exe --gtest_brief=1

# Integration tests (36)
.\build\tests\integration\Debug\phyxel_integration_tests.exe --gtest_brief=1

# Or use the build script
./build_and_test.ps1 -Config Debug -RunTests
```

### Manual Standalone Game Testing
Use `samples/game_definitions/testing_baseline.json` and follow the checklist in `docs/StandaloneGameTesting.md`.

Quick scaffold-and-test:
```powershell
python tools/create_project.py TestGame --game-definition samples/game_definitions/testing_baseline.json
```

The test definition exercises: player spawn + third-person camera, NPC patrol movement with gravity, gentle Perlin terrain (heightScale=4), menus (main + pause), input gating, and dialogue/story.

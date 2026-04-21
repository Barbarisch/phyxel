# CLAUDE.md — Phyxel Voxel Game Engine

## Auto-Context on Startup

When starting a new conversation in the engine terminal, **proactively check engine state** before the user asks. Call `engine_status` — if the engine is running, gather world context by running `/context` (or manually calling the same MCP tools). This saves the user from having to explain what's loaded. If the engine is not running, skip and wait for instructions.

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

| Name    | Mass | Friction | Restitution | Notes            |
|---------|------|----------|-------------|------------------|
| Wood    | 0.7  | 0.6      | 0.2         | Light, natural   |
| Metal   | 4.0  | 0.7      | 0.1         | Heavy            |
| Glass   | 1.5  | 0.2      | 0.6         | Brittle, bouncy  |
| Rubber  | 0.5  | 0.9      | 0.9         | Very bouncy      |
| Stone   | 6.0  | 0.8      | 0.05        | Heaviest, rough  |
| Ice     | 0.9  | 0.1      | 0.4         | Very slippery    |
| Cork    | 0.2  | 0.5      | 0.4         | Ultra-light      |
| Dirt    | 2.0  | 0.7      | 0.1         | Earth/terrain sub-surface |
| glow    | 1.0  | 0.5      | 0.5         | Emissive         |
| Leaf    | 0.1  | 0.3      | 0.1         | Light foliage for trees/bushes |
| Default | 1.0  | 0.5      | 0.3         | Balanced         |

Atlas: 84 textures total, 512×512. Source PNGs in `resources/textures/source/`. Rebuild after atlas changes: `.\build_shaders.bat` (also manually recompile `voxel.frag` since glslc doesn't track `#include` deps)
Lookup: `MaterialRegistry::instance().getTextureIndex(materialID, faceID)` — data-driven via `resources/materials.json`

## Voxel Rendering Pipelines

Three separate vertex shaders handle voxels in different states. All share `voxel.frag`.

| Shader | Purpose | Instance Data | Sub-tile UV Method |
|--------|---------|---------------|--------------------|
| `static_voxel.vert` | Chunk voxels (baked into 32³ grid) | `InstanceData` (8B) — packed position, face, scaleLevel, subcube/microcube grid positions in bits 20-31 | GPU decodes grid positions per face |
| `dynamic_voxel.vert` | GPU particle debris (compute-expanded) | `DynamicSubcubeInstanceData` (64B) — world pos, scale, rotation, localPosition for grid | GPU decodes localPosition per face |
| `kinematic_voxel.vert` | Moving rigid groups (doors, furniture, fragments) | `KinematicFaceData` (40B) — local pos, scale, pre-computed uvOffset | CPU pre-computes uvOffset in `buildFaces()` |

**Texture mapping for subcubes/microcubes:** Each voxel face shows only its portion of the parent cube's texture. A subcube (1/3 scale) at grid position (1,2,0) gets UV offset (1/3, 2/3) on applicable axes. Per-face axis mapping and flips ensure seamless tiling. The three shaders achieve the same visual result via different encoding strategies (packed bits, grid positions, or pre-computed offsets).

**Compiling shaders:** `.\build_shaders.bat` — compiles all `.vert`/`.frag`/`.comp` to SPIR-V in `shaders/*.spv`. The CMake build copies compiled shaders to `build/shaders/`.

## Entity Types

Spawnable via MCP `spawn_entity` or keybindings. Control mode toggled with **K**.

| Type       | Class                    | Notes |
|------------|--------------------------|-------|
| `physics`  | `PhysicsCharacter`       | Humanoid ragdoll, Bullet Physics, WASD |
| `spider`   | —                        | Spider-type enemy |
| `animated` | `AnimatedVoxelCharacter` | .anim-based FSM: Idle/Walk/Run/Jump/Fall/Attack/Crouch/etc. |

Animated: Jump (Space), Attack (Left Click), Crouch (Ctrl), Sprint (Shift), Derez (X).

## Chunk Layout & World Storage

- Chunks positioned by world-space origin (ivec3). Default DB: `worlds/default.db`.
- Initial camera: position (50, 50, 50), looking toward (16, 16, 16), yaw=-135°, pitch=-30°.
- World storage: SQLite. Save with `save_world` MCP tool.

## World Generation

`WorldGenerator` generation types (enum `GenerationType`):

| Type        | Description |
|-------------|-------------|
| `Random`    | 70% fill rate, deterministic from seed |
| `Perlin`    | Height-map terrain, base level Y=16 |
| `Flat`      | Solid below Y=16 |
| `Mountains` | Multi-octave noise, peaks up to ~60 |
| `Caves`     | Perlin terrain with 3D cave carving |
| `City`      | Flat ground, procedural 16×16 buildings |
| `Custom`    | User-supplied generation function |

Demo script: `scripts/world_gen.py` (`generate_pyramid`, `generate_platform`, `generate_glow_pillars`).

## Scene System (Multi-Level Games)

Each scene has its own world DB, entities, and NPCs. The story engine persists across scenes.

Multi-scene game definitions use a `"scenes"` array instead of a top-level `"world"` key:
- `worldDatabase`: per-scene SQLite DB (resolved to `worlds/<id>.db` if relative)
- `playerDefaults`: merges into scenes without their own `"player"` key
- `globalStory`: arcs that persist across transitions
- `transitionStyle`: `"cut"` | `"fade"` | `"loading_screen"`
- Detection: `GameDefinitionLoader::isMultiScene(json)` checks for `"scenes"` array

**Sample:** `samples/game_definitions/multi_scene_demo.json`
**MCP tools:** `list_scenes`, `get_active_scene`, `transition_scene`, `add_scene`, `remove_scene`, `save_scene_manifest`

## Object Templates

Files in `resources/templates/` — spawnable via **T** (static) / **Shift+T** (dynamic physics) or MCP `spawn_template`.

### BlockSmith AI Model Generation

Generates voxel templates from text prompts via LLM → .bbmodel → .voxel pipeline.
Fork at `external/blocksmith/`. Env vars: `PHYXEL_AI_API_KEY` or `ANTHROPIC_API_KEY`.

```bash
python tools/blocksmith_generate.py "a wooden chair" --name chair --size 2 --material Wood
python tools/blocksmith_generate.py --building --name tavern --building-type tavern --style medieval \
  --width 14 --depth 18 --stories 2 --materials '{"wall":"Stone","floor":"Wood","roof":"Wood"}'
python tools/blocksmith_generate.py --list
```

Key params: `--size` (furniture: 2-3, buildings: 8-15), `--material`, `--model` (anthropic/claude-sonnet-4-20250514, gemini/gemini-2.5-pro, openai/gpt-4o)
Templates cached permanently in `resources/templates/`. Catalog: `resources/templates/template_catalog.json`.
**MCP tools**: `generate_template`, `build_building`, `search_templates`, `list_generated_templates`

## Animation Files

Root-level: `character.anim`, `character_box.anim`, `character_complete.anim`

In `resources/animated_characters/`:
- `character_wolf.anim`
- `character_spider.anim`, `character_spider2.anim`, `character_spider3.anim`
- `character_female.anim`, `character_female2.anim`, `character_female3.anim`
- `character_dragon.anim`

## Keybindings

| Key | Action |
|-----|--------|
| ESC | Pause Menu |
| F1 | Performance Overlay |
| F3/F4 | Debug Vis / Debug Rendering (Ctrl+F4 cycles mode) |
| F5 | Raycast Vis + NPC FOV cones |
| F6 | Lighting Controls |
| F7 | Profiler |
| ` | Scripting Console |
| V | Toggle Camera Mode (First/Third/Free) |
| C / Ctrl+C / Alt+C | Place Cube / Subcube / Microcube |
| Left Click | Break Voxel |
| Middle Click | Subdivide Cube |
| T / Shift+T | Spawn Static / Dynamic Template |
| P | Toggle Template Preview |
| -/= | Ambient Light |
| [/] | Spawn Speed |
| K | Toggle Character Control (Physics/Spider/Animated) |
| W/A/S/D | Movement |
| Space | Jump (Animated) |
| Shift | Sprint |
| Ctrl | Crouch (Animated) |
| Left Click | Attack (Animated) |
| X | Derez character |
| N/B | Next/Prev Animation (Preview Mode) |

## MCP Server (AI Agent Bridge)

Server: `scripts/mcp/phyxel_mcp_server.py` — HTTP API at `localhost:8090`.
Requirements: `pip install mcp httpx`. Engine must be running.

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

### AI Game Development Workflow

**Typical workflow:**
1. `build_project` — Build the engine
2. `launch_engine` — Start the engine (`args: ["--project", "<path>"]` for a game project)
3. `engine_running` — Verify it's ready
4. `load_game_definition` — Load full game definition JSON (world + player + camera + npcs + structures + story)
5. `screenshot` — See the result
6. Iterate with `create_game_npc`, `fill_region`, `place_voxel`, etc.
7. `build_game` → `run_game` → `package_game`

### Engine Launch Modes

**`--project <dir>`** — Open a game project for development. Loads `worlds/default.db` + `game.json`. `save_world` writes back to the project DB.

**`--asset-editor <file.voxel>`** — Edit a voxel template on a flat Stone floor. H = reference character, Ctrl+S = save.

**`--anim-editor <file.anim>`** — Inspect/resize character bones. Per-bone scale sliders, Ctrl+S = save MODEL section.

### Game Project Scaffolding & Packaging

```bash
python tools/create_project.py MyGame --game-definition game.json   # scaffold
python tools/package_game.py MyGame --project-dir path/to/MyGame    # package
```

- Projects link against `phyxel_core`, no Python/MCP/dev tools in output
- World terrain pre-baked in `worlds/default.db`; NPCs/story loaded from `game.json` at runtime
- Output: `Documents/PhyxelProjects/<GameName>/` (self-contained)
- See `docs/GameCreationGuide.md` for full workflow

## Project Structure

```
engine/          # phyxel_core static library
  include/       # Public headers (core/, graphics/, physics/, scene/, ui/, input/, utils/)
  src/           # Implementation
  deprecated/    # Archived experimental code — not compiled
editor/          # phyxel_editor lib + phyxel executable (dev tool / world editor)
examples/minimal_game/  # Reference: GameCallbacks + EngineRuntime pattern
tests/           # Unit + integration + benchmark + stress + e2e
tools/           # create_project.py, package_game.py, anim_editor.py, etc.
scripts/mcp/     # MCP server for AI agents
resources/       # Templates, animations, textures, sounds, recipes, rpg/
shaders/         # GLSL shaders + compiled SPIR-V
external/        # Third-party: bullet3, glfw, glm, imgui, goose, miniaudio, sqlite3
docs/            # Documentation
```

## Key Engine Subsystems

**Core:** ChunkManager (32³ chunks, Vulkan buffers, face culling), ChunkStreamingManager (SQLite persistence), WorldGenerator, SceneManager (multi-scene transitions), EntityRegistry (O(1) lookup), EngineAPIServer (HTTP port 8090), ScriptingSystem (pybind11), PhysicsWorld (Bullet), RenderPipeline/RenderCoordinator (Vulkan instanced)

**Gameplay:** HealthComponent, RespawnSystem, CombatSystem (sphere+cone hit detection), EquipmentSystem (6 slots), CraftingSystem, DayNightCycle, HazardSystem, AchievementSystem, MusicPlaylist, PlayerProfile, ObjectiveTracker

**NPC/AI:** NPCManager, NavGrid + AStarPathfinder, DialogueSystem, StoryEngine, AIConversationService (Claude/OpenAI/Ollama), BehaviorTree/UtilityAI

**Voxel Physics:** DynamicObjectManager (Bullet, 300 cap), GpuParticlePhysics (Vulkan XPBD, 10000 cap), VoxelManipulationSystem (hybrid routing — see `docs/DynamicVoxelPhysics.md`)

**D&D RPG:** DiceSystem, CharacterAttributes (6 ability scores + modifiers), ProficiencySystem, CharacterSheet/CharacterProgression (class/race/XP/level, data-driven JSON in `resources/rpg/`), ActionEconomy/InitiativeTracker, AttackResolver/ConditionSystem (15 conditions), SpellDefinition/SpellcasterComponent/SpellResolver, RpgItem/CurrencySystem/AttunementSystem/EncumbranceSystem, ReputationSystem/DialogueSkillCheck/SocialInteractionResolver, RestSystem, WorldClock (360-day calendar, lunar cycle), Party, LootTable, EncounterBuilder, CampaignJournal

## Common Patterns

- **Namespace**: `Phyxel::Core`, `Phyxel::Physics`, `Phyxel::Graphics`, `Phyxel::Scene`, `Phyxel::UI`, `Phyxel::Input`, `Phyxel::Utils`
- **Logging**: `LOG_INFO("Tag", "message")`, `LOG_DEBUG`, `LOG_WARN`, `LOG_ERROR`, `LOG_TRACE_FMT`
- **Entity registration**: `registry.registerEntity(entity, "my_id", "type_tag")`
- **Voxel placement**: `chunkManager->addCube(worldX, worldY, worldZ)` or `addCubeWithMaterial(x, y, z, "Stone")`

## Testing

```powershell
# Unit tests (1614)
.\build\tests\Debug\phyxel_tests.exe --gtest_brief=1

# Integration tests (36)
.\build\tests\integration\Debug\phyxel_integration_tests.exe --gtest_brief=1

# Or use the build script
./build_and_test.ps1 -Config Debug -RunTests
```

Manual standalone game testing: `samples/game_definitions/testing_baseline.json` + checklist in `docs/StandaloneGameTesting.md`.

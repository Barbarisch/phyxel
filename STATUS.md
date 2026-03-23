# Phyxel — Project Status & Next Steps

*Last updated: March 23, 2026*

## What's Been Built

### Engine Extraction (Phases E1–E2)

#### E1: Config & Asset Management (commit `baa747c`, 802 tests / 80 suites)
- **EngineConfig**: JSON-serializable engine configuration (`engine.json`) with defaults for window, rendering, physics, audio, world, API
- **AssetManager**: Singleton that resolves asset paths (textures, shaders, templates, sounds, worlds, dialogues, recipes) from config
- **23 new tests** across EngineConfigTest (15) and AssetManagerTest (8)

#### E2: EngineRuntime Extraction (commit `848417b`, 818 tests / 82 suites)
- **EngineRuntime**: New class that owns all 16 core engine subsystems (window, Vulkan, physics, chunks, audio, input, timing, camera, profiling, etc.) via `unique_ptr`. Provides `initialize(config)`, `run(GameCallbacks&)`, `shutdown()`, `quit()`, `beginFrame()`/`endFrame()`, 16 subsystem accessors.
- **GameCallbacks**: Virtual interface with `onInitialize`, `onUpdate`, `onRender`, `onHandleInput`, `onShutdown` — all with default no-op implementations. Allows standalone games to hook into the engine loop without the Application monolith.
- **WorldInitializer moved** from `game/` to `engine/` — reusable by any project linking `phyxel_core`
- **Application refactored**: Delegates core init to `EngineRuntime`, holds non-owning raw pointer aliases. Cleanup delegated to `runtime->shutdown()`.
- **ScriptingSystem/Bindings decoupled**: Replaced cross-library `extern` with callback pattern (`registerAppInstanceSetter`) to break phyxel_game→phyxel_core circular dependency.
- **16 new tests**: EngineRuntimeTest (8) + GameCallbacksTest (8)

#### E3: Project System (826 tests / 83 suites)
- **Minimal example game** (`examples/minimal_game/`): Standalone game using `EngineRuntime` + `GameCallbacks` — builds a voxel platform, free camera, renders via `RenderCoordinator`. Links only `phyxel_core`, no `Application` monolith.
- **CMake install targets**: Basic `cmake --install` for `phyxel_core` lib + headers
- **Project scaffolding**: `python tools/create_project.py MyGame` generates a ready-to-build standalone game directory (CMakeLists.txt, GameCallbacks stub, main.cpp, engine.json)
- **8 new tests**: ProjectSystemTest (C++) + 6 Python tests for create_project.py

#### AGD: AI Game Development (852 tests / 84 suites)
- **GameDefinitionLoader**: Single JSON document describes a complete game — world generation, structures, player, NPCs (with dialogue + story characters), camera, story arcs. Engine loads it atomically via one API call.
- **6 new MCP tools**: `load_game_definition`, `export_game_definition`, `validate_game_definition`, `create_game_npc` (composite NPC creation), `build_project` (cmake build), `launch_engine` (start engine process), `engine_running` (health check)
- **HTTP endpoints**: `/api/game/load_definition`, `/api/game/export_definition`, `/api/game/validate_definition`, `/api/game/create_npc`
- **AI workflow**: build → launch → load_game_definition → screenshot → iterate
- **26 new tests**: GameDefinitionTest (validation, result serialization, null-subsystem handling, complete schema validation)

#### E4: Game Project Workflow (852 tests / 84 suites)
- **`--project` flag**: Engine as editor — `phyxel.exe --project <dir>` opens a game project for development. Loads world from project's `worlds/default.db`, reads project's `engine.json` for window title, auto-loads `game.json` with smart world skip (skips generation when chunks are pre-baked in SQLite).
- **In-engine build/run API**: Three new endpoints for building and running game projects from within the running engine:
  - `GET /api/project/info` — project metadata (dir, game.json, CMakeLists.txt, exe status)
  - `POST /api/project/build` — cmake configure + build with full output capture (300s timeout)
  - `POST /api/project/run` — launch built game exe as detached process
- **MCP tools**: `project_info`, `build_game`, `run_game`, `package_game`
- **Game project scaffolding** (`tools/create_project.py`): Enhanced with `--game-definition` flag — generates full game C++ (NPCManager, DialogueSystem, StoryEngine, RenderCoordinator) from a game definition JSON. CMakeLists uses `add_subdirectory(phyxel)` for phyxel_core linkage. Default output to `Documents/PhyxelProjects/<name>/`.
- **Game packaging** (`tools/package_game.py`): Packages standalone distributable directory — game exe, compiled shaders, texture atlas, pre-baked world DB, game.json. Scans definition for resource dependencies. Supports `--project-dir` and `--prebake-world` workflows.
- **CMake improvements**: `PHYXEL_ROOT_DIR` for correct paths when Phyxel is used as `add_subdirectory`. Game/editor/tests/examples guarded behind `CMAKE_SOURCE_DIR` check. Link directories propagated via `target_link_directories` on phyxel_core.
- **Rendering fix**: `RenderCoordinator::drawFrame()` always refreshes `cachedViewMatrix` from camera — fixes white screen in standalone games using `EngineRuntime`.
- **Game creator agent**: `.github/agents/game-creator.agent.md` — Copilot agent for AI-assisted game creation
- **Documentation**: `docs/GameCreationGuide.md`, updated `CLAUDE.md` with full --project workflow

### AI Agent Control Surface (commits `34fd5a7` → `340fbe5`)
- **EngineAPIServer**: HTTP/JSON API on `localhost:8090` with 34+ endpoints
- **MCP Server**: `scripts/mcp/phyxel_mcp_server.py` — stdio-based MCP server for Claude Code / Goose
- **EntityRegistry**: Central O(1) entity lookup by string ID, spatial queries, type queries
- **APICommandQueue**: Thread-safe command queue for main-thread execution of API requests
- **Screenshots**: Vulkan framebuffer capture → PNG, returned via `/api/screenshot`
- **Event Polling**: Entity spawn/remove/move, voxel place/remove, region fill/clear, world save events
- **Snapshots/Undo**: Named region snapshots with restore capability (max 50)
- **Copy/Paste**: Region clipboard with Y-axis rotation (0/90/180/270°)
- **World Generation**: Procedural terrain via API (Random/Perlin/Flat/Mountains/Caves/City)
- **Template Save**: Save voxel regions as reusable `.txt` templates

### Per-Material Textures (commit `aca6517`)
- **9 materials** with unique 6-face texture sets: Wood, Metal, Glass, Rubber, Stone, Ice, Cork, glow, Default
- **72 textures** in a 256×256 atlas (12 material slots × 6 faces)
- **Texture pipeline**: `tools/generate_material_textures.py` → `tools/texture_atlas_builder.py` → atlas files
- **Material→texture lookup**: `TextureConstants::getTextureIndexForMaterial(materialName, faceID)` in Types.h
- **Multi-material terrain**: WorldGenerator assigns materials by depth (surface=Default, sub-surface=Cork, deep=Stone, peaks=Ice, buildings=Metal)

### Material Persistence (commit `8d336d0`)
- SQLite `cubes` table now has `material TEXT DEFAULT 'Default'` column
- Save path binds `cube->getMaterialName()` as 9th parameter
- Load path reads material column and passes to `chunk.addCube(pos, material)`
- Auto-migration for existing databases (`ALTER TABLE ADD COLUMN`)

### MCP Server Async Fix (commit `340fbe5`)
- Switched from `httpx.Client` (sync, blocked event loop) to `httpx.AsyncClient`
- All `api_get`/`api_post`/`_dispatch_tool` are now properly async
- Fixes hanging behavior that confused Claude Code

### NPC System (current session)

#### NPCEntity & NPCManager
- **NPCEntity**: Wraps `AnimatedVoxelCharacter`, delegates to a pluggable `NPCBehavior` strategy. Supports `IdleBehavior` and `PatrolBehavior` (waypoints, walk speed, wait time).
- **NPCManager**: Owns all NPC entities, registers them with `EntityRegistry` as type `"npc"`. Handles spawn/remove lifecycle and wires NPC context (physics, registry, light manager).
- **Behaviors**: `IdleBehavior` (stationary), `PatrolBehavior` (ordered waypoints with configurable speed/wait)
- **Dialogue**: `DialogueSystem`, `SpeechBubbleManager`, `InteractionManager` for NPC interaction
- **API endpoints**: `POST /api/npc/spawn`, `POST /api/npc/remove`, `GET /api/npcs`, `POST /api/npc/behavior`, `POST /api/npc/dialogue`

#### NPC Rendering Fix
- **Root cause**: `NPCManager` had no render path — NPCs were updated but never drawn. `AnimatedVoxelCharacter::render()` is a no-op; rendering requires `RenderCoordinator` to iterate the character's `parts`.
- **Fix**: `RenderCoordinator` now holds a `Core::NPCManager*` (set via `setNPCManager()`). `renderEntities()` pulls each NPC's `AnimatedVoxelCharacter*` via `getAnimatedCharacter()` and adds it to the instanced character batch alongside regular entities.
- **Spawn position fix**: `spawn_npc` handler now accepts flat `x`/`y`/`z` fields in addition to nested `"position"` object.

### Phase 3: Feature & Polish (previous session)

#### Subcube/Microcube Per-Material Textures
- Subcube and Microcube now store a `materialName` field (defaults to "Default")
- Added `getMaterialName()` / `setMaterialName()` + material-accepting constructors
- `addSubcube()` / `addMicrocube()` accept optional `const std::string& material = "Default"`
- `subdivideAt()` inherits parent cube's material; `subdivideSubcubeAt()` inherits parent subcube's material
- Rendering uses `getTextureIndexForMaterial(getMaterialName(), faceID)` in both ChunkRenderManager and FaceUpdateCoordinator
- All 6 template spawn paths in ObjectTemplateManager pass material through

#### Improved Procedural Textures
- 7 new texture generation algorithms: `stratified_texture` (stone layers), `growth_ring_texture` (wood end-grain), `brushed_metal_texture` (directional streaks), `glass_edge_texture` (bright edges), `dimple_texture` (rubber bumps), `cracked_ice_texture` (crystalline cracks), `porous_texture` (cork pores)
- All materials updated to use improved algorithms
- Atlas rebuilt with new textures (72 textures, 256×256)

#### Test Suite Expansion (366 → 412 tests)
- **DebrisSystem**: 18 tests — spawning, Verlet integration, gravity, floor collision, lifetime/dt clamping, ring buffer, independent lifetimes
- **CollisionSpatialGrid**: 28 tests — add/remove/clear operations, entity type detection, grid validation, out-of-bounds handling, stress test (32³ fill). Heap-allocated via `std::make_unique` to avoid stack overflow.
- **ForceSystem**: 36 tests — ForceConfig, click force calculation, static utilities, MouseVelocityTracker, Bond/Cube integration
- **ChunkStreamingManager**: 15 tests — storage init, save operations, dirty chunk tracking, round-trip verification
- **ChunkManager**: 31 tests — coordinate helpers, chunk data operations, materials, dirty tracking, boundary positions
- **E2E stubs**: 12 stubs converted to `GTEST_SKIP()` with clear descriptions

#### Build System Fix
- Executable renamed from `VulkanCube` to `phyxel` (matches namespace rename)
- Build target: `cmake --build build --config Debug --target phyxel`
- Executable copied to project root post-build
- Stale `build/VulkanCube.sln` and `build/phyxel_core.vcxproj` are leftover artifacts from old layout; correct files are under `build/engine/` and `build/game/`

## Current State of the Build

- **Build**: Clean, all targets compile (`phyxel_core`, `phyxel_game`, `phyxel`)
- **Tests**: All 938 tests pass (0 failures). 88 test suites.
- **Executable**: `phyxel.exe` at project root (copied post-build) or `build/game/Debug/phyxel.exe`
- **Example**: `phyxel_minimal_game.exe` at `build/examples/minimal_game/Debug/`
- **Game projects**: Scaffolded via `python tools/create_project.py <Name>`, built via in-engine API or cmake directly
- **Frozen Highlands**: Test game project at `Documents/PhyxelProjects/FrozenHighlands/` (built + verified)

### Build Commands
```powershell
# CMake isn't in PATH, use full path:
$cmakePath = "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
& $cmakePath -B build -S .
& $cmakePath --build build --config Debug

# Or use the build script:
.\build_and_test.ps1 -Config Debug -RunTests
```

## Known Issues / Action Items

### Must Do Before Testing with MCP
- **Regenerate world**: Existing `worlds/default.db` has all cubes saved as "Default" material (pre-migration data). Delete the DB or use MCP `generate_world` to regenerate terrain and see multi-material results.

### Phase 4: Dialogue & Conversation System (current session)

#### Completed
- **Input suppression during dialogue**: WASD/mouse/camera input blocked when dialogue box is active (prevents character movement during conversation)
- **SpeechBubbleManager in NPCContext**: Behaviors can now call `ctx.speechBubbleManager->say()` for ambient NPC chatter
- **PatrolBehavior ambient speech**: NPCs on patrol may speak random phrases when arriving at waypoints (configurable `setArrivalPhrases()`, `setSpeechChance()`)
- **Dialogue file I/O**: `loadDialogueFile(path)` and `listDialogueFiles(dir)` utility functions for loading JSON dialogue trees from disk
- **Sample dialogue files**: `resources/dialogues/guard_intro.json` (branching guard conversation), `resources/dialogues/merchant_shop.json` (merchant with quest hook)
- **New API endpoints**: `GET /api/dialogue/state`, `POST /api/dialogue/advance`, `POST /api/dialogue/choice`, `POST /api/dialogue/load`, `GET /api/dialogue/files`
- **New MCP tools**: `get_dialogue_state`, `advance_dialogue`, `select_dialogue_choice`, `load_dialogue_file`, `list_dialogue_files`
- **Unit tests**: 7 new tests for dialogue file loading/listing (total 511 tests, 44 suites)

#### Already existed from Phase 3
- DialogueSystem state machine (Typing/WaitingForInput/ChoiceSelection), typewriter effect, choice conditions
- SpeechBubbleManager with world-to-screen projection, fade-out, max 8 bubbles
- InteractionManager with proximity detection, E key interaction, cooldown
- renderDialogueBox() (RPG box at bottom 25% of screen), renderSpeechBubbles(), renderInteractionPrompt()
- Input bindings: E (interact), Enter (advance), 1-4 (choices), ESC (end conversation)
- API endpoints: `/api/npc/dialogue`, `/api/dialogue/start`, `/api/dialogue/end`, `/api/speech/say`
- MCP tools: `set_npc_dialogue`, `start_dialogue`, `end_dialogue`, `say_bubble`

### Open Gaps

### Game Mechanics System (commit `4b526b4`, 511 tests)

#### Phase 1: Crafting System
- **CraftingRecipe, CraftingSystem** — JSON recipe loading, resource validation, crafting with byproducts
- **MCP tools**: `list_recipes`, `get_recipe`, `craft_item`, `add_recipe`, `reload_recipes`
- **Sample recipes**: `resources/recipes/` (wood_planks, stone_bricks, glass_pane, etc.)

#### Phase 2: Environmental Hazards
- **HazardZone, HazardSystem** — 6 hazard types (Fire, Poison, Ice, Electric, Void, Radiation), stackable effects, resistance, DOT/slow/stun
- **MCP tools**: `create_hazard`, `remove_hazard`, `list_hazards`, `get_hazard`

#### Phase 3: Day/Night Cycle
- **DayNightCycle** — configurable day length, time phases (Dawn/Day/Dusk/Night), ambient/sun color interpolation, time speed control
- **MCP tools**: `get_time`, `set_time`, `set_day_length`, `set_time_speed`

#### Phase 4: Achievements
- **Achievement, AchievementSystem** — types (Counter/Threshold/OneShot/Composite), JSON persistence, progress tracking, unlock callbacks
- **MCP tools**: `list_achievements`, `get_achievement`, `create_achievement`, etc.

### Story Engine (Phases S1–S5 + Application wiring)

Full design: `docs/StoryEngineDesign.md` | Progress: `docs/StoryEngineProgress.md`

- **S1 — World State & Character Profiles**: WorldState, Faction, Location, CharacterProfile (Big Five personality), CharacterMemory (knowledge asymmetry), StoryEngine facade (72 tests)
- **S2 — Event System & Knowledge Propagation**: EventBus (thread-safe), KnowledgePropagator (witnessing/dialogue/rumors), confidence decay (36 tests)
- **S3 — Story Director Core**: StoryArc with 4 constraint modes (Scripted/Guided/Emergent/Freeform), beat evaluation, tension/pacing management (47 tests)
- **S4 — Character AI Agents**: CharacterAgent interface, RuleBasedCharacterAgent (personality-driven), LLMCharacterAgent (AI-backed with fallback chain), StoryDrivenBehavior (NPCBehavior bridge) (64 tests)
- **S5 — Developer API & Tooling**: StoryWorldLoader (JSON world definitions), 14 HTTP API routes, 14 MCP tools (49 tests)
- **Application wiring**: StoryEngine instantiated in Application, 6 read-only handlers + 8 mutation commands. All 14 endpoints verified via manual testing.

### Gameplay Infrastructure (current session, 938 tests / 88 suites)

#### Async JobSystem (commit `1903616`)
- **JobSystem**: Background thread pool for non-blocking MCP operations. `submitJob()` returns `JobRecord` with progress/status. `JobContext` provides `setProgress()`, `setMessage()`, `setResult()` for reporting.
- **MCP tools**: `list_jobs`, `get_job`, `cancel_job`
- **HTTP endpoints**: `GET /api/jobs`, `GET /api/job/:id`, `POST /api/job/:id/cancel`
- **18 unit tests**

#### Audio & Lighting MCP Tools (commit `5868a1d`)
- **Audio MCP tools**: `list_sounds`, `play_sound`, `set_volume` — 3 HTTP endpoints, 3 MCP tools
- **Lighting MCP tools**: `list_lights`, `add_point_light`, `add_spot_light`, `remove_light`, `update_light`, `set_ambient_light` — 6 HTTP endpoints, 6 MCP tools
- Ambient light read handler, light list handler (all point + spot lights with full properties)

#### Inventory/Hotbar System (commit `f6263e9`)
- **Inventory**: 36 slots (4 rows of 9), 9-slot hotbar, creative mode (items never consumed)
- **ItemStack**: material + count + maxStack (64), merge/split logic
- Default hotbar loaded with all 9 materials at stack of 64
- **MCP tools**: `get_inventory`, `give_item`, `take_item`, `select_hotbar_slot`, `set_inventory_slot`, `clear_inventory`, `set_creative_mode`
- **24 unit tests**

#### Health/Damage System (commit `a083936`)
- **HealthComponent**: health/maxHealth/alive/invulnerable, takeDamage()/heal()/kill()/revive(), JSON serialization
- Virtual `getHealthComponent()` on Entity base class — `RagdollCharacter` and `NPCEntity` auto-create (100 HP default)
- `EntityRegistry::entityToJson()` includes health state in entity JSON output
- `GameDefinitionLoader` supports `health`, `maxHealth`, `invulnerable` in NPC definitions
- **MCP tools**: `damage_entity`, `heal_entity`, `set_entity_health`, `kill_entity`, `revive_entity`
- **Game events**: `entity_damaged`, `entity_healed`, `entity_killed`, `entity_revived`
- **25 unit tests**

#### Day/Night Cycle (commit `4b555ba`)
- **DayNightCycle**: Animates sun direction, sun color, ambient light over configurable day cycle
- Time model: 0–24h (0=midnight, 6=dawn, 12=noon, 18=dusk), configurable day length (default 600s), time scale
- Warm orange at dawn/dusk, white noon, dark night. Integrated into RenderCoordinator (updates each frame via existing UBO pipeline, no shader changes needed)
- **MCP tools**: `get_day_night`, `set_day_night`
- **18 unit tests**

### Open Gaps
- **Subcube/microcube material persistence**: SQLite schema only stores material for full cubes. Subcube/microcube material defaults to "Default" on save/load. Need to add `material` column to subcube/microcube tables.
- **ScriptingSystem tests**: Skipped — requires Python/pybind11 runtime which isn't available in unit test context.
- **`project_build` runs on game loop thread**: Build subprocess blocks the main loop (via `queueAndWait` with 300s timeout). Should use the new JobSystem for background builds.
- **`fs::current_path()` in project_build is not thread-safe**: Could be an issue if anything else reads CWD during a build.

### Texture / Visual Polish
- **Subcube/microcube textures**: ✅ Per-material rendering implemented. Material inherited on subdivision, passed through from templates.
- **Procedural textures**: ✅ 7 new algorithms (stratified, growth rings, brushed metal, glass edges, dimples, cracked ice, porous). All materials use improved generators.
- **Shader rebuild**: After any atlas changes, run `.\build_shaders.bat`

### Potential Feature Work
- Entity animation control via MCP
- Entity AI behavior scripting
- Chunk LOD system for distant terrain
- Better physics integration for subcube/microcube interactions

## Key File Locations

| What | Where |
|------|-------|
| MCP Server | `scripts/mcp/phyxel_mcp_server.py` |
| Engine API Server | `engine/src/core/EngineAPIServer.cpp` |
| World Storage (SQLite) | `engine/src/core/WorldStorage.cpp` |
| Texture Constants | `engine/include/core/Types.h` (TextureConstants namespace) |
| Material Manager | `engine/src/physics/Material.cpp` |
| World Generator | `engine/src/core/WorldGenerator.cpp` |
| Texture Generator | `tools/generate_material_textures.py` |
| Atlas Builder | `tools/texture_atlas_builder.py` |
| Project Instructions | `CLAUDE.md` |
| Issue Tracker | `TODO.md` |
| World Database | `worlds/default.db` |
| Project Scaffolding | `tools/create_project.py` |
| Game Packaging | `tools/package_game.py` |
| Game Creator Agent | `.github/agents/game-creator.agent.md` |
| Game Creation Guide | `docs/GameCreationGuide.md` |

## MCP Setup (for Claude Code on new machine)

Add to `~/.claude/claude_code_config.json`:
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

**Requirements**: `pip install mcp httpx` (MCP SDK v1.9.x+, httpx v0.27+)

The game (`phyxel.exe`) must be running for MCP tools to work. The engine listens on `localhost:8090`.

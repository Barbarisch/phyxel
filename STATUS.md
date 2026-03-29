# Phyxel — Project Status & Next Steps

*Last updated: March 28, 2026*

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
- **ScriptingSystem/Bindings decoupled**: Replaced cross-library `extern` with callback pattern (`registerAppInstanceSetter`) to break phyxel_editor→phyxel_core circular dependency.
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

- **Build**: Clean, all targets compile (`phyxel_core`, `phyxel_editor`, `phyxel`)
- **Tests**: 1510 unit tests (1506 pass, 3 AI E2E skipped, 1 pre-existing skeleton failure). 155 test suites. 36 integration tests.
- **MCP Tools**: 166 total
- **Executable**: `phyxel.exe` at project root (copied post-build) or `build/editor/Debug/phyxel.exe`
- **Example**: `phyxel_minimal_game.exe` at `build/examples/minimal_game/Debug/`
- **Game projects**: Scaffolded via `python tools/create_project.py <Name>`, built via in-engine API or cmake directly
- **VillageChat**: Active test game project at `Documents/PhyxelProjects/VillageChat/`

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

### LLM Integration — Direct API Client (current session)

Replacing the Goose-based AI pipeline with a direct LLM client so shipped games can call Claude/OpenAI/Ollama APIs without external dependencies. Players provide their own API key in game settings. Goose stays as an editor dev tool only.

#### Completed
- **LLMClient** (`engine/include/ai/LLMClient.h`, `engine/src/ai/LLMClient.cpp`): Direct HTTPS via WinHTTP (Windows) — zero external dependencies. Supports three providers: Anthropic Claude, OpenAI, and Ollama (local). Async support via `completeAsync()`, token usage tracking, configurable timeouts.
- **ContextManager** (`engine/include/ai/ContextManager.h`, `engine/src/ai/ContextManager.cpp`): Assembles optimal LLM prompts from game state. Pulls character personality (Big Five traits), emotional state, active goals, relationships, nearby entities, character knowledge/memories, conversation summaries, active story arcs, world variables. Token budget management with automatic trimming.
- **ConversationMemory** (`engine/include/ai/ConversationMemory.h`, `engine/src/ai/ConversationMemory.cpp`): SQLite-backed conversation persistence (shares WorldStorage's DB). Tables: `conversation_turns`, `conversation_summaries`. LLM-powered summarization of old conversations. Automatic pruning of old turns.
- **AIConversationService** (`engine/include/ai/AIConversationService.h`, `engine/src/ai/AIConversationService.cpp`): Orchestrator wiring LLMClient + ContextManager + ConversationMemory into `DialogueSystem::startAIConversation()`. Player message → ContextManager builds prompt → LLMClient calls LLM on background thread → response delivered to DialogueSystem (thread-safe). Falls back to "[NPC seems lost in thought...]" on LLM errors.
- **Application wiring**: AIConversationService initialized in `editor/src/Application.cpp` from EngineConfig settings (aiProvider/aiModel/aiApiKey + PHYXEL_AI_API_KEY env fallback). InteractionManager callback prioritizes: 1) Full AI conversation (direct LLM) for AI-mode NPCs with configured API key, 2) AI-enhanced tree dialogue via AIEnhancer, 3) Plain tree dialogue.
- **WorldStorage::getDb()**: Public accessor added to share SQLite db handle with ConversationMemory.
- **Unit Tests**: 31 new tests (LLMClientTest, ConversationMemoryTest, ContextManagerTest, AIConversationServiceTest) — 1064 total.
- **CMake**: `winhttp` added to `phyxel_core` link libraries. Test glob extended with `ai/*.cpp`.
- **GameSettings AI fields**: `aiProvider`, `aiModel`, `aiApiKey` added to GameSettings struct. Provider/model serialized to settings.json; API key excluded for security (loaded from PHYXEL_AI_API_KEY env var). 3 new tests.
- **Settings screen AI section**: Provider dropdown (anthropic/openai/ollama), model text input, masked API key input, status indicator. `onAISettingsChanged` callback in SettingsCallbacks struct.
- **Game template integration**: Scaffolded projects (`create_project.py`) now include AIConversationService initialization, full settings screen with AI section, and runtime config update callback. Settings auto-saved on shutdown.
- **AI E2E tests**: 3 live LLM pipeline tests (auto-skip when `PHYXEL_AI_API_KEY` not set). Direct LLM call, context assembly + call, full service pipeline. 1070 total tests (1067 pass + 3 skipped).
- **InteractionManager in game template**: Standalone games now have full NPC interaction support — InteractionManager with proximity detection, E-key interaction, AI conversation priority (direct LLM → tree dialogue fallback), speech bubble rendering, interaction prompts, and input suppression during dialogue. RenderCoordinator view/projection matrix getters added.
- **Auto-summarization**: ConversationMemory now auto-summarizes old turns when conversation length exceeds a configurable threshold (default: 20 turns). After summarization, old turns are pruned keeping only recent ones (default: 6). Enhanced summary prompt includes emotion/importance metadata and structured guidance. Wired into AIConversationService — happens automatically after each NPC response on the background thread. 1077 tests (1074 pass + 3 skipped).

#### Remaining
- End-to-end manual test: run standalone VillageChat, walk to NPC, press E to test interaction

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

### NPC Character Systems — Phase 1: Item Definition & World Time (1221 tests)

#### Completed (March 28, 2026)
- **ItemDefinition** (`engine/include/core/ItemDefinition.h`): Item type system with 6 types (Material, Tool, Weapon, Consumable, Quest, Equippable), tool sub-types (Pickaxe, Axe, Sword, Hoe, Shovel), equipment slots (MainHand, OffHand, Head, Chest, Legs, Feet). Full JSON serialization.
- **ItemRegistry** (`engine/include/core/ItemRegistry.h`, `engine/src/core/ItemRegistry.cpp`): Singleton registry for item definitions. `registerItem()`, `getItem()`, `hasItem()`, `getItemsByType()`, `loadFromFile()`, `loadFromJson()`, `registerMaterialItems()` (auto-registers all engine materials). Loaded from `resources/items.json`.
- **Inventory refactored**: `ItemStack.material` → `ItemStack.itemId` (backward-compat `material()` alias retained). Added `ItemStack.durability` field (-1 = N/A). `canMerge()` respects durability. All callers updated (GameMenus.cpp, Inventory.cpp, tests).
- **DayNightCycle extended**: Added `m_dayNumber` (auto-increments on midnight wrap), `getHour()`, `getMinute()`, `getDayNumber()`, `setDayNumber()`, `isNight()` (18:00-06:00), `isDay()`. Day counter + helpers serialized in toJson/fromJson.
- **resources/items.json**: 10 starter items — iron/wooden sword, iron/wooden pickaxe, iron axe, bread, health potion, iron helmet, iron chestplate, quest key.
- **API endpoints**: `GET /api/items` (list all), `GET /api/items/:id` (single item detail).
- **MCP tools**: `list_items`, `get_item`, `dayNumber` parameter in `set_day_night`.
- **24 new tests**: ItemDefinition (4), ItemRegistry (8), ItemStack (4), DayNightCycle day counter (8). Total: 1221 tests (1217 pass + 3 AI E2E skipped + 1 GLFW skip).

#### Phase 2: Equipment, Weapons & Combat (COMPLETE)
**33 new tests** (20 EquipmentSystem + 13 CombatSystem). Total: **1254 tests** (1250 pass + 3 AI E2E skipped + 1 pre-existing skeleton test).

**Files created:**
- **engine/include/core/EquipmentSystem.h + .cpp**: `EquipmentSlots` class — slot-based equip/unequip with type+slot validation, stat aggregation (totalDamage/reach/speed), OnEquipmentChanged callback, JSON serialization.
- **engine/include/core/CombatSystem.h + .cpp**: `CombatSystem` with `AttackParams` struct — sphere+cone spatial query via EntityRegistry, damage delivery through HealthComponent, knockback via setMoveVelocity(), invulnerability timers (0.5s default), DamageEvent structs with toJson(), OnDamage callback.
- **resources/templates/weapons/**: `sword.txt` (5 cubes), `pickaxe.txt` (6 cubes), `axe.txt` (6 cubes) — Wood handle + Metal blade voxel models.
- **tests/core/EquipmentSystemTest.cpp**: 20 tests — equip/unequip/clear, type validation, slot mismatch, stats, callback, JSON, string roundtrip.
- **tests/core/CombatSystemTest.cpp**: 13 tests — hit/miss, cone check, range, invulnerability, kill, knockback, callback, multi-target, DamageEvent JSON.

**Files modified:**
- **AnimatedVoxelCharacter.h/.cpp**: `attachToBone(name, size, offset, color, label)` / `detachFromBone(id)` / `detachAll()` for weapon visuals. Hit frame system: `setHitFrameFraction()`, `setOnHitFrame()` — fires callback once per Attack animation at configurable fraction (default 0.4).
- **NPCEntity.h**: `EquipmentSlots m_equipment` member + `getEquipment()` accessor.
- **EngineAPIServer.h/.cpp**: `EquipmentGetHandler` + 4 routes: `GET /api/entity/:id/equipment`, `POST /api/entity/:id/equip`, `POST /api/entity/:id/unequip`, `POST /api/combat/attack`.
- **Application.cpp**: Equipment handler, command handlers for `equip_item`/`unequip_item`/`combat_attack`, CombatSystem instance + update in game loop, weapon bone attachment on equip.
- **ItemDefinition.h**: Added `equipSlotFromString()` inverse of `equipSlotToString()`.
- **MCP server**: 5 new tools — `get_equipment`, `equip_item`, `unequip_item`, `attack`.

#### Phase 3: NPC Intelligence Architecture (COMPLETE)
**49 new tests** (50 AI tests, 1 removed from adjustments). Total: **1303 tests** (1299 pass + 3 AI E2E skipped + 1 pre-existing skeleton test).

Behavior tree + utility AI system for NPC decision-making. NPCs can now use perception-driven, blackboard-mediated behavior trees with utility-based action selection.

**Files created:**
- **engine/include/ai/Blackboard.h**: Header-only per-NPC typed key-value store (`variant<bool, int, float, string, vec3>`). Typed getters with defaults, overloaded `set()`, `toJson()` serialization.
- **engine/include/ai/PerceptionSystem.h + .cpp**: `SenseResult` struct + `PerceptionComponent` class — vision cone (dot product), hearing radius (omnidirectional), memory decay, configurable ranges/angles/update intervals.
- **engine/include/ai/ActionSystem.h + .cpp**: `NPCAction` base class + 7 concrete actions: `MoveToAction`, `LookAtAction`, `WaitAction`, `SpeakAction`, `SetBlackboardAction`, `MoveToEntityAction`, `FleeAction`. Actions follow PatrolBehavior movement patterns (XZ-plane setMoveVelocity + yaw setRotation).
- **engine/include/ai/BehaviorTree.h**: Header-only behavior tree framework. `BTNode` base, composites (`SequenceNode`, `SelectorNode`, `ParallelNode`), decorators (`InverterNode`, `RepeaterNode`, `CooldownNode`, `SucceederNode`), leaves (`ActionNode` wrapping NPCAction, `ConditionNode`, `BBConditionNode`, `BBHasKeyNode`). Builder helpers in `BT::` namespace. `BehaviorTree` root container.
- **engine/include/ai/UtilityAI.h**: Header-only utility AI layer. `Consideration` (scorer + weight), `UtilityAction` (behavior tree + multiplicative scoring), `UtilityBrain` (evaluates all actions, selects highest, runs BT).
- **engine/include/scene/behaviors/BehaviorTreeBehavior.h + .cpp**: `NPCBehavior` adapter — bridges engine's NPCBehavior interface to BT/UtilityAI. Owns `Blackboard`, `PerceptionComponent`, optional `UtilityBrain` or `BehaviorTree`. Updates perception → writes to blackboard → ticks brain/tree. Handles `onInteract`/`onEvent` by setting blackboard keys.
- **tests/ai/BehaviorTreeTest.cpp**: 50 tests — Blackboard (12), Perception (6), BT composites/decorators/leaves (25), UtilityAI (5), integration (2).

**Files modified:**
- **NPCManager.h**: Added `NPCBehaviorType::BehaviorTree` to enum.
- **NPCManager.cpp**: Added `BehaviorTreeBehavior` include + case in all 5 switch statements.
- **Application.cpp**: Added `BehaviorTreeBehavior` include, `"behavior_tree"` behavior string parsing for spawn + set_behavior, 3 new command handlers: `get_npc_blackboard`, `get_npc_perception`, `set_npc_blackboard`.
- **EngineAPIServer.cpp**: 3 new routes: `GET /api/npc/:name/blackboard`, `GET /api/npc/:name/perception`, `POST /api/npc/:name/blackboard`.
- **MCP server**: 3 new tools — `get_npc_blackboard`, `get_npc_perception`, `set_npc_blackboard`.

### Texture / Visual Polish

### Playability Polish (PP1–PP5, 1510 tests / 155 suites)

**42 new tests** (HealthComponentCallback: 4, RespawnSystem: 10, MusicPlaylist: 9, ObjectiveTracker: 12, PlayerProfile: 7).

#### PP1: Pause Menu
- **ESC key** now toggles pause instead of exiting. Pause freezes DayNightCycle and skips update/handleInput.
- Pause menu overlay with Resume/Settings/Quit buttons rendered via GameMenus.
- `toggle_pause` / `get_pause_state` MCP tools + API endpoints.

#### PP2: Death & Respawn
- **HealthComponent** extended with `setOnDeathCallback()` — fires on `kill()` and fatal `takeDamage()`.
- **RespawnSystem** (`engine/include/core/RespawnSystem.h`): Header-only. Death sequence timer (default 3s), spawn point management, death counter, auto-respawn with `setOnRespawnCallback()`.
- Death overlay (full-screen dark red, "YOU DIED", countdown timer, death count) via GameMenus.
- MCP tools: `get_player_health`, `damage_player`, `heal_player`, `kill_player`, `revive_player`, `get_respawn_state`, `set_spawn_point`, `force_respawn`.

#### PP3: Background Music
- **AudioSystem** extended with `playMusic()` (looping via miniaudio), `stopMusic()`, `isMusicPlaying()`, `getMusicTrack()`.
- **MusicPlaylist** (`engine/include/core/MusicPlaylist.h`): Header-only. Sequential/Shuffle modes, track add/remove/clear, volume control (0–1), auto-advance on track end.
- MCP tools: `get_music_state`, `control_music` (play/stop/next/add_track/remove_track/clear/set_volume/set_mode).

#### PP4: Player Save/Load
- **PlayerProfile** (`engine/include/core/PlayerProfile.h`): Header-only. Persists camera pos/yaw/pitch, health/maxHealth, spawn point, death count, inventory (JSON). SQLite persistence via `saveToDb()`/`loadFromDb()` with `CREATE TABLE IF NOT EXISTS player_state` + UPSERT.
- MCP tools: `save_player`, `load_player`.

#### PP5: Objective/Progression System
- **ObjectiveTracker** (`engine/include/core/ObjectiveTracker.h`): Header-only. Objective struct with id/title/description/Status (Active/Completed/Failed)/hidden/priority/category. Active objectives sorted by priority, JSON round-trip.
- **Objective HUD**: Top-right panel in GameMenus (260px, max 5 objectives, "+N more..." overflow).
- MCP tools: `get_objectives`, `add_objective`, `complete_objective`, `fail_objective`, `remove_objective`.

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
| LLM Client | `engine/include/ai/LLMClient.h`, `engine/src/ai/LLMClient.cpp` |
| Context Manager | `engine/include/ai/ContextManager.h`, `engine/src/ai/ContextManager.cpp` |
| Conversation Memory | `engine/include/ai/ConversationMemory.h`, `engine/src/ai/ConversationMemory.cpp` |
| AI Conversation Service | `engine/include/ai/AIConversationService.h`, `engine/src/ai/AIConversationService.cpp` |
| Respawn System | `engine/include/core/RespawnSystem.h` |
| Music Playlist | `engine/include/core/MusicPlaylist.h` |
| Player Profile | `engine/include/core/PlayerProfile.h` |
| Objective Tracker | `engine/include/core/ObjectiveTracker.h` |
| Goose Bridge (editor only) | `engine/include/ai/GooseBridge.h`, `scripts/goose_bridge.py` |

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

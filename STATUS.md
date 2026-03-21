# Phyxel — Project Status & Next Steps

*Last updated: March 21, 2026*

## What's Been Built

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
- **Tests**: All 511 tests pass (0 failures). 44 test suites.
- **Executable**: `phyxel.exe` at project root (copied post-build) or `build/game/Debug/phyxel.exe`

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
- **Subcube/microcube material persistence**: SQLite schema only stores material for full cubes. Subcube/microcube material defaults to "Default" on save/load. Need to add `material` column to subcube/microcube tables.
- **ScriptingSystem tests**: Skipped — requires Python/pybind11 runtime which isn't available in unit test context.

### Texture / Visual Polish
- **Subcube/microcube textures**: ✅ Per-material rendering implemented. Material inherited on subdivision, passed through from templates.
- **Procedural textures**: ✅ 7 new algorithms (stratified, growth rings, brushed metal, glass edges, dimples, cracked ice, porous). All materials use improved generators.
- **Shader rebuild**: After any atlas changes, run `.\build_shaders.bat`

### Potential Feature Work
- Add more MCP tools (e.g., lighting control, entity animation control)
- Audio system integration via MCP
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

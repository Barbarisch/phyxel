# Phyxel — Project Status & Next Steps

*Last updated: March 10, 2026*
*Latest commit: `340fbe5` (main)*

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

## Current State of the Build

- **Build**: Clean, all targets compile (`phyxel_core`, `phyxel_game`, `phyxel`)
- **Tests**: 5 pre-existing `WorldGeneratorTest` failures (`std::bad_function_call` — Chunk constructed in tests doesn't set up `m_addCollision` callback). No new failures from recent work.
- **Working tree**: Clean (only untracked: `.claude/`, `screenshots/`)

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

### Open TODO Items (from TODO.md)

| # | Item | Priority |
|---|------|----------|
| 19 | WorldStorage needs round-trip save/load tests (especially with new material column) | Medium |
| 20 | ChunkManager has zero unit tests | Medium |
| 21 | E2E tests are ~80% stubs (no real assertions) | Low |
| 23 | Sanitizers (ASan/TSan) off by default | Low |
| 24 | No tests for ChunkStreamingManager, ForceSystem, DebrisSystem, ScriptingSystem, CollisionSpatialGrid | Medium |

### Pre-existing Test Failures to Fix
- **5 WorldGeneratorTest failures**: `std::bad_function_call` because `Chunk` objects created in tests don't have `m_addCollision` callback set. Need to either mock the callback or refactor Chunk construction.

### Texture / Visual Polish
- **Subcube/microcube textures**: Still use placeholder texture indices, not per-material. Would need similar `getTextureIndexForMaterial()` wiring in the subcube/microcube render paths.
- **Procedural texture appearance**: The generated textures in `tools/generate_material_textures.py` could use visual tuning (colors, patterns, detail).
- **Shader rebuild**: After any atlas changes, run `.\build_shaders.bat`

### Potential Feature Work
- Add more MCP tools (e.g., lighting control, entity animation control)
- Audio system integration via MCP
- Better world persistence (subcube/microcube materials)

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

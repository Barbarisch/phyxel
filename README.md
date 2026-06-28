# Phyxel

A voxel game engine and development application built with **C++17** and **Vulkan**. Phyxel provides
a reusable engine library (`phyxel_core`) for building voxel-based games, plus a full-featured editor
application for world building, scripting, and AI-assisted game creation.

Physics is an in-house stack — a **GPU compute solver** (`GpuParticlePhysics`, Vulkan XPBD/AVBD, for
large-scale debris/destruction) plus a **custom CPU rigid-body world** (`VoxelDynamicsWorld`, for
furniture, character grounding, and break debris). Bullet Physics has been removed.

> **Full documentation lives in [`docs/`](docs/README.md)** — start there for architecture, systems,
> and the AI game-dev workflow. New AI engine-dev sessions should read [`docs/AgentContext.md`](docs/AgentContext.md) first.

## Architecture

```
┌──────────────────────────────────────────────────────┐
│  phyxel_core  (engine/)                              │
│  Reusable static library — the game engine           │
│                                                      │
│  Rendering (Vulkan)  ·  In-house Physics  ·  Audio   │
│  Chunk System (32³)  ·  Entity Registry  ·  Camera   │
│  World Gen + Biomes  ·  UI System  ·  Scripting (Py) │
│  Story Engine  ·  Dialogue  ·  NPC Behaviors  ·  RPG │
│  HTTP API  ·  SQLite World Storage                   │
├──────────────────────────────────────────────────────┤
│  phyxel_editor  (editor/)                            │
│  Development application — world editing, debugging, │
│  MCP server, Python console, AI integration          │
├──────────────┬───────────────────────────────────────┤
│  Your Game   │  examples/minimal_game/               │
│  (standalone)│  Reference GameCallbacks impl          │
│  Links only  │                                       │
│  phyxel_core │                                       │
└──────────────┴───────────────────────────────────────┘
```

| Build Target | Directory | What It Is |
|---|---|---|
| `phyxel_core` | `engine/` | The game engine — a reusable C++ static library |
| `phyxel_editor` | `editor/` | Editor/dev-tool library (Application, input, scripting, AI) |
| `phyxel` | `editor/src/main.cpp` | Editor executable (world building, debugging, MCP) |
| `phyxel_minimal_game` | `examples/minimal_game/` | Reference standalone game using `GameCallbacks` |

## Making a Game

Standalone games link only against `phyxel_core` and implement the `GameCallbacks` interface:

```cpp
#include "core/GameCallbacks.h"
#include "core/EngineRuntime.h"

class MyGame : public Phyxel::Core::GameCallbacks {
    bool onInitialize(Phyxel::Core::EngineRuntime& engine) override;
    void onUpdate(Phyxel::Core::EngineRuntime& engine, float dt) override;
    void onRender(Phyxel::Core::EngineRuntime& engine) override;
    void onShutdown() override;
};
```

Scaffold a new project: `python tools/create_project.py MyGame`.
See [docs/GameCreationGuide.md](docs/GameCreationGuide.md) for the full AI-assisted workflow.

## Key Features

**Engine (`phyxel_core`)**
- **Vulkan rendering** — instanced voxels (static / kinematic / GPU-particle pipelines), face culling, shadow maps, SSAO, baked per-voxel lighting, day/night cycle
- **32³ chunk system** — SQLite persistence, streaming, biome-aware world generation + flora
- **In-house physics** — GPU compute XPBD/AVBD debris + custom CPU rigid-body world (furniture, character grounding, destruction)
- **29 data-driven materials** with per-face PBR textures (mixed-resolution `sampler2DArray`, BC7-compressed)
- **Animated characters** — `.anim` FSM (Idle/Walk/Run/Jump/Attack/Crouch/…), animation blending
- **NPC & narrative** — behaviors, navigation, branching dialogue, story engine, LLM-driven conversations
- **D&D RPG ruleset** — dice, attributes, classes/races, spells, items, combat, currency
- **Data-driven UI** — custom-Vulkan `UISystem` HUD + menus (no ImGui in shipped games); configurable via `game.json`
- **Audio** — spatial audio (miniaudio) + music playlist · **Python scripting** via pybind11 · **HTTP API** (port 8090)

**Editor (`phyxel.exe`)**
- World/voxel editing, entity & NPC spawning, Python console (backtick)
- **MCP server** — AI-agent bridge (~275 tools) for Claude Code / Copilot
- Debug visualization, performance profiling, template & animation editors
- Project mode (`--project`), asset editor (`--asset-editor`), anim editor (`--anim-editor`)

## Building

**Prerequisites:** C++17 (MSVC 2022 on Windows), Vulkan SDK 1.3+ ([LunarG](https://vulkan.lunarg.com/)), CMake 3.15+, Python 3.x.

```powershell
git clone <repository-url>
cd phyxel
git submodule update --init --recursive

# Build (default: fast, no tests)
.\build_and_test.ps1

# Build + run tests
.\build_and_test.ps1 -RunTests

.\phyxel.exe
```

Manual build: `cmake -B build -S . && cmake --build build --config Debug`.

Test suites (`-IntegrationOnly` / `-BenchmarkOnly` / `-StressOnly` / `-E2EOnly`): ~2,300 unit, 47 integration, plus benchmark/stress/e2e. See [docs/GoogleTestIntegration.md](docs/GoogleTestIntegration.md).

## Controls

Common: **W/A/S/D** move · **V** camera mode · **K** character control · **Left Click** break/attack ·
**C** place cube · **T** spawn template · **`** Python console · **ESC** pause · **F1/F7** overlays.
Full reference: [docs/Keybindings.md](docs/Keybindings.md).

## Project Structure

```
engine/      phyxel_core — engine library (include/ + src/, by domain: core graphics physics scene ui input utils)
editor/      phyxel_editor — development application (Application, scripting, AI, MCP)
examples/    minimal_game — reference GameCallbacks standalone
tests/       unit + integration/ + benchmark/ + stress/ + e2e/
tools/       create_project.py, package_game.py, gen_tree.py, phyxel-cli, phyxel-gamedev skills, asset pipeline
scripts/     mcp/ (MCP server), world gen
resources/   templates, textures, animated_characters, rpg, ui, sounds, biomes.json, materials.json
shaders/     GLSL + compiled SPIR-V
external/    stb, glfw, glm, imgui, goose, blocksmith, miniaudio, sqlite3
docs/        documentation (start at docs/README.md)
```

## License

GPL v3 — see [LICENSE](LICENSE).

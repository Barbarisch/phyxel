# Phyxel

A voxel game engine and development application built with C++17, Vulkan, and Bullet Physics. Phyxel provides a reusable engine library (`phyxel_core`) for building voxel-based games, and a full-featured editor application for world building, scripting, and AI-assisted game creation.

## Architecture

Phyxel follows a clean engine/editor separation:

```
┌──────────────────────────────────────────────────────┐
│  phyxel_core  (engine/)                              │
│  Reusable static library — the game engine           │
│                                                      │
│  Rendering (Vulkan)  ·  Physics (Bullet)  ·  Audio   │
│  Chunk System (32³)  ·  Entity Registry  ·  Camera   │
│  World Gen  ·  UI System  ·  Scripting (Python)      │
│  Story Engine  ·  Dialogue  ·  NPC Behaviors         │
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

### Making a Game

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

int main() {
    Phyxel::Core::EngineRuntime engine;
    Phyxel::Core::EngineConfig config;
    engine.initialize(config);
    MyGame game;
    engine.run(game);
}
```

Scaffold a new project: `python tools/create_project.py MyGame`

See [docs/GameCreationGuide.md](docs/GameCreationGuide.md) for the full workflow.

## Features

### Engine (`phyxel_core`)
- **Vulkan Rendering** — Instanced voxel rendering, dual pipeline (static + dynamic), face culling, shadow maps, day/night cycle
- **32³ Chunk System** — Scalable world with SQLite persistence, streaming, cross-chunk occlusion
- **Bullet Physics** — Rigid body dynamics, ragdoll characters, physics-based entities
- **Materials** — 9 built-in materials (Wood, Metal, Glass, Rubber, Stone, Ice, Cork, Glow, Default) with per-face textures
- **Animated Characters** — 19-state FSM, animation blending, multiple character types (humanoid, spider, dragon, wolf)
- **World Generation** — Perlin noise, flat, mountains, caves, city, random — with multi-material terrain
- **NPC System** — Behaviors (idle, patrol, story-driven), interaction triggers, speech bubbles
- **Dialogue System** — Branching dialogue trees, typewriter effect, choice selection
- **Story Engine** — Story arcs, character agents, event bus, LLM integration
- **Direct LLM Client** — WinHTTP-based API client for Claude/OpenAI/Ollama, context-aware NPC conversations, SQLite conversation memory
- **UI System** — Custom menu screens (main menu, pause, HUD, inventory, settings) + ImGui overlays
- **Audio** — Spatial audio via miniaudio, background music playlist (Sequential/Shuffle)
- **Health & Respawn** — Per-entity health, death callbacks, automatic respawn with configurable delay
- **Player Profile** — Camera, health, spawn point, inventory persistence to SQLite
- **Objectives** — Priority-sorted quest/objective tracking with HUD rendering
- **Inventory & Crafting** — 36-slot inventory, hotbar, creative mode, JSON recipes
- **Equipment & Combat** — 6-slot equipment, sphere+cone attack, knockback, invulnerability
- **Day/Night Cycle** — Sun/ambient animation, time phases, day counter
- **Behavior Trees** — Composable BT framework, utility AI, perception, blackboard
- **Python Scripting** — Embedded interpreter via pybind11
- **HTTP API** — REST API on port 8090 for external tool integration

### Editor (`phyxel_editor` / `phyxel.exe`)
- **World Editing** — Place/break voxels, fill regions, spawn templates
- **Entity Spawning** — Physics characters, animated characters, NPCs
- **Python Console** — In-app scripting console (backtick key)
- **MCP Server** — AI agent bridge (166 tools) for Copilot, Claude Code, etc.
- **Debug Visualization** — Wireframe, normals, physics, raycast, force system overlays
- **Performance Profiling** — Real-time FPS, frame timing, memory, draw call stats
- **Template System** — Import/spawn voxel objects (trees, castles, spheres)
- **Camera Modes** — Free, first-person, third-person with named slots and transitions
- **Project Mode** — `phyxel.exe --project <dir>` to develop a game project in the editor

## Controls

### Movement & Camera
| Key | Action |
|-----|--------|
| W/A/S/D | Move |
| Space | Jump (character) / Move up (free camera) |
| V | Cycle camera mode (Free / First Person / Third Person) |
| K | Cycle character control (Physics / Spider / Animated) |
| Shift | Sprint |

### World Interaction
| Key | Action |
|-----|--------|
| Left Click | Break voxel / Attack (animated character) |
| C | Place cube |
| Ctrl+C / Alt+C | Place subcube / microcube |
| Middle Click | Subdivide cube |
| T / Shift+T | Spawn static / dynamic template |
| P | Toggle template preview |
| -/= | Decrease/increase ambient light |

### Debug & UI
| Key | Action |
|-----|--------|
| F1 | Performance overlay |
| F4 | Debug rendering |
| Ctrl+F4 | Cycle debug mode (Wireframe / Normals / Hierarchy / UV / Emissive) |
| F5 | Raycast visualization |
| F6 | Lighting controls |
| F7 | Profiler |
| F10 | Game menu |
| ` (backtick) | Python scripting console |
| ESC | Toggle Pause Menu (freeze world, resume/settings/quit) |

## Building

### Prerequisites
- **C++17** compiler (MSVC 2022 recommended on Windows)
- **Vulkan SDK** 1.3+ — [LunarG](https://vulkan.lunarg.com/)
- **CMake** 3.15+
- **Python** 3.x (for embedded scripting)

### Quick Start (Windows)

```powershell
git clone <repository-url>
cd phyxel
git submodule update --init --recursive

# Build (default: no tests, fast iteration)
.\build_and_test.ps1

# Build + run tests
.\build_and_test.ps1 -RunTests

# Run the editor
.\phyxel.exe
```

### Manual Build

```powershell
cmake -B build -S .
cmake --build build --config Debug

# Run
.\phyxel.exe
```

### Linux

```bash
sudo apt install build-essential cmake git libvulkan-dev vulkan-utils libglfw3-dev libglm-dev
git clone <repository-url> && cd phyxel
git submodule update --init --recursive
mkdir build && cd build
cmake .. && make -j$(nproc)
./phyxel
```

### Test Suites

```powershell
.\build_and_test.ps1 -RunTests          # Unit tests (fast)
.\build_and_test.ps1 -IntegrationOnly   # Integration tests
.\build_and_test.ps1 -BenchmarkOnly     # Benchmarks
.\build_and_test.ps1 -StressOnly        # Stress tests
.\build_and_test.ps1 -E2EOnly           # End-to-end tests
```

| Suite | Count | Typical Time |
|-------|-------|-------------|
| Unit | 1016 | ~8s |
| Integration | 36 | ~13s |
| Benchmark | 11 | ~20s |
| Stress | 24 | ~45s |
| E2E | 15 | ~105s |

## Project Structure

```
engine/              phyxel_core — the game engine library
  include/           Public headers (core/, graphics/, physics/, scene/, ui/, input/, utils/)
  src/               Implementation
editor/              phyxel_editor — development application
  include/           Editor headers (Application, InputController, AI, scene entities)
  src/               Application.cpp, scripting bindings, main.cpp
examples/
  minimal_game/      Reference game using GameCallbacks pattern
tests/               Unit tests (Google Test)
  integration/       Integration tests
  benchmark/         Benchmark tests
  stress/            Stress tests
  e2e/               End-to-end tests
tools/               create_project.py, package_game.py, texture tools
scripts/             Python scripts, MCP server
resources/           Templates, animations, dialogues, sounds, recipes
shaders/             GLSL shaders + compiled SPIR-V
external/            Third-party (Bullet3, GLFW, GLM, ImGui, pybind11, miniaudio, SQLite3)
docs/                Documentation
```

## AI-Assisted Game Development

Phyxel includes an MCP (Model Context Protocol) server that exposes 50+ tools for AI agents to build games programmatically:

```
@game-creator Create a frozen highlands game with mountains terrain,
ice material buildings, patrol guard NPCs, and a quest storyline.
```

The typical AI workflow:
1. `build_project` — Build the engine
2. `launch_engine` — Start the editor
3. `load_game_definition` — Load a complete game from JSON
4. `screenshot` — See the result
5. Iterate with `fill_region`, `place_voxel`, `create_game_npc`, etc.
6. `package_game` — Package into a standalone distributable

See [docs/GameCreationGuide.md](docs/GameCreationGuide.md) and [docs/MCPIntegration.md](docs/MCPIntegration.md) for details.

## Documentation

Detailed documentation in `docs/`:

- [GameCreationGuide.md](docs/GameCreationGuide.md) — AI-driven game creation workflow
- [MCPIntegration.md](docs/MCPIntegration.md) — MCP server setup and tools
- [ArchitectureOverview.md](docs/ArchitectureOverview.md) — Engine architecture diagrams
- [SubsystemArchitecture.md](docs/SubsystemArchitecture.md) — Subsystem design patterns
- [CoordinateSystem.md](docs/CoordinateSystem.md) — Coordinate systems and conversions
- [MultiChunkSystem.md](docs/MultiChunkSystem.md) — Chunk-based world management
- [EntitySystem.md](docs/EntitySystem.md) — Entity and character system
- [AnimatedCharacter.md](docs/AnimatedCharacter.md) — Character animation guide
- [StoryEngineDesign.md](docs/StoryEngineDesign.md) — Narrative and story system
- [GameMechanicsRoadmap.md](docs/GameMechanicsRoadmap.md) — Gameplay systems roadmap
- [TestingGuide.md](docs/TestingGuide.md) — Testing setup and best practices
- [Keybindings.md](docs/Keybindings.md) — Full keybinding reference

## License

GPL v3 — see [LICENSE](LICENSE).

## Troubleshooting

**"Could NOT find Vulkan"** — Install Vulkan SDK from [LunarG](https://vulkan.lunarg.com/) and restart your terminal.

**"glfw3.lib not found"** — Download GLFW pre-compiled binaries from [glfw.org](https://www.glfw.org/download.html) and extract to `external/glfw/`.

**Linux missing deps** — `sudo apt install libvulkan-dev vulkan-utils libglfw3-dev vulkan-validationlayers-dev`

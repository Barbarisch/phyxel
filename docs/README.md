# Phyxel Engine Documentation

## Overview

Phyxel is a voxel game engine and development application. The engine (`phyxel_core`) is a reusable C++ static library providing rendering, physics, world management, UI, scripting, and narrative systems. The editor (`phyxel_editor` / `phyxel.exe`) is a full-featured development application for world building, debugging, and AI-assisted game creation. Standalone games link only against `phyxel_core` via the `GameCallbacks` interface.

---

## Getting Started

1. **Building a game** — Read the [GameCreationGuide.md](GameCreationGuide.md) for the AI-driven workflow, or scaffold a project with `python tools/create_project.py MyGame`
2. **Engine architecture** — [ArchitectureOverview.md](ArchitectureOverview.md) and [SubsystemArchitecture.md](SubsystemArchitecture.md)
3. **Core systems** — [CoordinateSystem.md](CoordinateSystem.md) → [MultiChunkSystem.md](MultiChunkSystem.md) → [VoxelSystem.md](VoxelSystem.md) → [VoxelRenderPipelines.md](VoxelRenderPipelines.md)
4. **AI integration** — [MCPIntegration.md](MCPIntegration.md) for the MCP server and 166 tools
5. **Prompt catalog** — [GameDevPromptCatalog.md](GameDevPromptCatalog.md) for ready-to-use game creation and feature testing prompts

---

## Game Development

- **[GameCreationGuide.md](GameCreationGuide.md)** — AI-driven game creation workflow (MCP / Copilot / Claude Code)
- **[GameDevPromptCatalog.md](GameDevPromptCatalog.md)** — 40 ready-to-use prompts for game creation and engine feature testing
- **[GameMechanicsRoadmap.md](GameMechanicsRoadmap.md)** — Gameplay systems roadmap (lights, cameras, NPCs, dialogue)
- **[StoryEngineDesign.md](StoryEngineDesign.md)** — Story arc, character agent, and narrative system design
- **[StoryEngineProgress.md](StoryEngineProgress.md)** — Story engine implementation progress

## Engine Architecture

- **[ArchitectureOverview.md](ArchitectureOverview.md)** — Visual architecture diagrams
- **[SubsystemArchitecture.md](SubsystemArchitecture.md)** — Callback-based subsystem design pattern guide
- **[EntitySystem.md](EntitySystem.md)** — Entity types, characters, physics, and AI

## Rendering & World

- **[VoxelSystem.md](VoxelSystem.md)** — Voxel sizes (cube/subcube/microcube), static/kinematic/dynamic lifecycle states
- **[VoxelRenderPipelines.md](VoxelRenderPipelines.md)** — Three-pipeline Vulkan rendering (static, kinematic, GPU particle)
- **[MultiChunkSystem.md](MultiChunkSystem.md)** — 32³ chunk-based world architecture
- **[ChunkUpdateOptimization.md](ChunkUpdateOptimization.md)** — Face culling, instance batching, GPU optimization
- **[ObjectTemplateSystem.md](ObjectTemplateSystem.md)** — Voxel object import and spawning

## Coordinates & Math

- **[CoordinateSystem.md](CoordinateSystem.md)** — World, chunk, and local coordinate transformations
- **[CoordinateQuickRef.md](CoordinateQuickRef.md)** — Quick lookup for conversion formulas
- **[CoordinateSystemDetailed.md](CoordinateSystemDetailed.md)** — In-depth coordinate math
- **[IndexingReference.md](IndexingReference.md)** — Array indexing patterns and formulas

## Characters & Animation

- **[AnimatedCharacter.md](AnimatedCharacter.md)** — Animated voxel character system
- **[CharacterAnimationGuide.md](CharacterAnimationGuide.md)** — Animation states, naming conventions, offsets
- **[PhysicsCharacter.md](PhysicsCharacter.md)** — Physics-based character controller
- **[AssetPipeline.md](AssetPipeline.md)** — Importing 3D models and animations

## AI & Integration

- **[MCPIntegration.md](MCPIntegration.md)** — MCP server setup and tool reference
- **[GooseIntegration.md](GooseIntegration.md)** — Goose AI NPC integration

## Development & Testing

- **[TestingGuide.md](TestingGuide.md)** — Google Test setup, test templates, mocking
- **[GoogleTestIntegration.md](GoogleTestIntegration.md)** — Test framework configuration
- **[IntegrationTesting.md](IntegrationTesting.md)** — Integration test patterns
- **[LoggingReference.md](LoggingReference.md)** — Logging usage guide
- **[LoggingSystem.md](LoggingSystem.md)** — Logging system internals
- **[Keybindings.md](Keybindings.md)** — Full keybinding reference

## Quick Reference

- **[QUICKSTART_WindowManager.md](QUICKSTART_WindowManager.md)** — WindowManager extraction guide

---

## Architecture Diagram

```
┌──────────────────────────────────────────────────────────┐
│  phyxel_core  (engine/)  — The Game Engine Library       │
├──────────────────────────────────────────────────────────┤
│                                                          │
│  Core        Rendering        Physics       Scene        │
│  ─────       ─────────        ───────       ─────        │
│  ChunkMgr    RenderCoord      PhysicsWorld  Entity       │
│  WorldGen    VulkanDevice     Materials     Character    │
│  EntityReg   RenderPipeline   Collision     NPCEntity    │
│  EngineRT    Camera/CamMgr    ForceSystem   AnimatedChar │
│  AudioSys    Light/DayNight                 VoxelInteract│
│  APIServer   ImGuiRenderer                  Raycaster    │
│  JobSystem   PostProcessor                               │
│              ShadowMap                                    │
│                                                          │
│  UI           Scripting       Story          Input       │
│  ──           ─────────       ─────          ─────       │
│  UISystem     ScriptingSys    StoryEngine    InputMgr    │
│  Dialogue     pybind11        CharAgent                  │
│  GameScreen                   EventBus                   │
│  GameMenus                    StoryDirector              │
│  SpeechBubble                                            │
│                                                          │
├──────────────────────────────────────────────────────────┤
│  phyxel_editor  (editor/)  — Development Application     │
├──────────────────────────────────────────────────────────┤
│  Application    InputController    AISystem               │
│  Python REPL    MCP Server         StoryDirector          │
│  Debug Overlays Template Editing   Entity Spawning        │
├──────────────────────────────────────────────────────────┤
│  Standalone Games  (examples/ or scaffolded projects)    │
│  Link phyxel_core, implement GameCallbacks               │
└──────────────────────────────────────────────────────────┘
```

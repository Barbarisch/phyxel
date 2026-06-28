# Phyxel Engine Documentation

Phyxel is a voxel game engine and development application. The engine (`phyxel_core`) is a
reusable C++17/Vulkan static library — rendering, physics, world management, UI, scripting,
and narrative systems. The editor (`phyxel_editor` / `phyxel.exe`) is the development
application for world building, debugging, and AI-assisted game creation. Standalone games
link only against `phyxel_core` via the `GameCallbacks` interface.

> This index is curated, not exhaustive — it points at the docs worth opening, grouped by what
> you're trying to do. Every link below has been verified to exist and to match current code.

---

## Start here

| You are… | Read first |
|---|---|
| **An AI engine-dev session** (new machine / fresh context) | **[AgentContext.md](AgentContext.md)** — portable working context: operational gotchas, engine ground truth, current workstreams + roadmap, user preferences. This is the substitute for per-machine memory. |
| **Building a game on the engine** | [GameCreationGuide.md](GameCreationGuide.md) → [GameDevWorkflow.md](GameDevWorkflow.md). Or just use the `phyxel-gamedev` skills (world / characters / assets / mechanics / playtest / package). |
| **New to the engine internals** | [ArchitectureOverview.md](ArchitectureOverview.md) → [SubsystemArchitecture.md](SubsystemArchitecture.md) → [CoordinateSystem.md](CoordinateSystem.md) → [VoxelRenderPipelines.md](VoxelRenderPipelines.md) |
| **Changing engine code** | [ForwardingSurface.md](ForwardingSurface.md) — what docs/skills/tools must stay in sync (enforced by `tools/check_doc_sync.py`). |

The repo-root **[`CLAUDE.md`](../CLAUDE.md)** is the canonical quick-reference (build pipeline,
materials, coordinate system, MCP overview). When in doubt, it wins over any doc here.

---

## Game development

- **[GameCreationGuide.md](GameCreationGuide.md)** — AI-driven game creation workflow (MCP / Claude Code)
- **[GameDevWorkflow.md](GameDevWorkflow.md)** — per-project session workflow, the `phyxel` CLI, per-machine setup
- **[GameDevPromptCatalog.md](GameDevPromptCatalog.md)** — ready-to-use game-creation & feature-testing prompts
- **[GameMechanicsRoadmap.md](GameMechanicsRoadmap.md)** — gameplay systems status (lights, cameras, NPCs, dialogue — all shipped)
- **[StandaloneGameTesting.md](StandaloneGameTesting.md)** — manual standalone-game test checklist
- **[MCPIntegration.md](MCPIntegration.md)** — MCP server + ~275 AI-agent tools (authoritative per-tool docs live in each tool's own description)

## World & Structure Generation

- **[structure-generation/](structure-generation/README.md)** — the **structure generator** (buildings/settlements): design, the grounded `StructureBrief` intake, placers, room/archetype data sheets, known issues, and the standing validation discipline. **Start at its README** — the canonical entry point for all structure-gen work.
- **[structure-generation/DimensionReference.md](structure-generation/DimensionReference.md)** — generated, grounded dimension canon (every furniture/typology size + its citation).
- **[TerrainGenerationBiomes.md](TerrainGenerationBiomes.md)** — terrain/biome world generation (the sibling pipeline).
- **[WorldRecipeAndFlora.md](WorldRecipeAndFlora.md)** — per-world generation recipe + flora.

## Engine architecture

- **[ArchitectureOverview.md](ArchitectureOverview.md)** — layered architecture + diagrams
- **[SubsystemArchitecture.md](SubsystemArchitecture.md)** — callback-based subsystem pattern
- **[EntitySystem.md](EntitySystem.md)** — entity types, characters, AI (note: Bullet ragdoll path deprecated)
- **[SceneSystem.md](SceneSystem.md)** — multi-scene games (per-scene world DB, transitions)
- **[EngineArchitectureAudit.md](EngineArchitectureAudit.md)** / **[EngineRobustnessAudit.md](EngineRobustnessAudit.md)** — decomplexification + defect audits (decision records)

## Rendering & world

- **[VoxelSystem.md](VoxelSystem.md)** — voxel sizes (cube/subcube/microcube) + static/kinematic/dynamic lifecycle
- **[VoxelRenderPipelines.md](VoxelRenderPipelines.md)** — three Vulkan voxel pipelines (static / kinematic / GPU particle)
- **[MultiChunkSystem.md](MultiChunkSystem.md)** — 32³ chunk world architecture
- **[ChunkUpdateOptimization.md](ChunkUpdateOptimization.md)** — face culling, instance batching, dirty-chunk tracking
- **[LightingPipeline.md](LightingPipeline.md)** — shadows, SSAO, baked per-voxel light field
- **[ObjectTemplateSystem.md](ObjectTemplateSystem.md)** — voxel object import & spawning
- **[TextureSystemOverhaul.md](TextureSystemOverhaul.md)** — PBR texture-array system (Phases 1–2 merged)

## Terrain, structures & assets

- **[TerrainGenerationBiomes.md](TerrainGenerationBiomes.md)** — streaming + data-driven biomes (implemented on main)
- **[WorldRecipeAndFlora.md](WorldRecipeAndFlora.md)** — per-world generation recipe + flora decoration
- **[StructureGenerationPipeline.md](StructureGenerationPipeline.md)** — LLM-architect → deterministic C++ realizer for buildings
- **[StructurePipelineGaps.md](StructurePipelineGaps.md)** — running log of pipeline gaps to implement
- **[AssetPipeline.md](AssetPipeline.md)** — importing 3D models / animations into voxel templates
- **[MaterialTextureNeeds.md](MaterialTextureNeeds.md)** — standing list of missing materials/textures

## Coordinates & math

- **[CoordinateSystem.md](CoordinateSystem.md)** — world/chunk/local transforms, indexing, bit-packing (the comprehensive doc)
- **[CoordinateQuickRef.md](CoordinateQuickRef.md)** — one-page conversion-formula lookup

## Physics

- **[DynamicVoxelPhysics.md](DynamicVoxelPhysics.md)** — GpuParticlePhysics (GPU compute) + VoxelDynamicsWorld (CPU); break routing
- **[DestructionSystem.md](DestructionSystem.md)** — voxel destruction design (bonds → coherent fragments)
- **[WaterSystem.md](WaterSystem.md)** — water design + roadmap (Phase 0+ scaffolding on main)
- **[PhysicsCharacter.md](PhysicsCharacter.md)** — ⚠️ deprecated (Bullet character archived; see EntitySystem.md)

## Characters & animation

- **[AnimatedCharacter.md](AnimatedCharacter.md)** — `AnimatedVoxelCharacter` (.anim FSM, the primary character)
- **[CharacterAnimationGuide.md](CharacterAnimationGuide.md)** — animation states, naming, offsets
- **[InteractionPipeline.md](InteractionPipeline.md)** — character ↔ object interaction (sitting, etc.) tuning pipeline
- **[LessonsLearned_ProceduralAnimation.md](LessonsLearned_ProceduralAnimation.md)** — why the current animation approach won (history)
- **[NavigationArchitecture.md](NavigationArchitecture.md)** — NPC navigation (Layer-1 NavGraph + async PathService on main; HPA* deferred)

## Story, RPG & combat

- **[StoryEngineDesign.md](StoryEngineDesign.md)** — story arcs, character agents, narrative system design
- **[StoryEngineProgress.md](StoryEngineProgress.md)** — story engine implementation log (S1–S5 complete)
- **[DnDRPGSystem.md](DnDRPGSystem.md)** — D&D ruleset (dice, attributes, classes, spells, items)
- **[TurnBasedCombat.md](TurnBasedCombat.md)** — BG3-style turn-based combat (HUD via UISystem)
- **[HudSystem.md](HudSystem.md)** — data-driven HUD/UISystem (includes remaining-work section)

## Cameras, UI & debug

- **[CameraControlSystem.md](CameraControlSystem.md)** — camera rigs + control schemes (implemented; live switching)
- **[DebugVisualizationGuide.md](DebugVisualizationGuide.md)** — debug-vis buffer architecture & rules
- **[Keybindings.md](Keybindings.md)** — full keybinding reference (authoritative)

## Integration, AI & testing

- **[GooseIntegration.md](GooseIntegration.md)** — Goose AI NPC integration (design intent; not yet wired)
- **[GoogleTestIntegration.md](GoogleTestIntegration.md)** — test framework setup (FetchContent / GoogleTest)
- **[IntegrationTesting.md](IntegrationTesting.md)** — integration-test fixtures & patterns
- **[LoggingSystem.md](LoggingSystem.md)** — logging system internals + migration guide
- **[LoggingReference.md](LoggingReference.md)** — logging quick-reference card

---

## Architecture diagram

```
┌──────────────────────────────────────────────────────────┐
│  phyxel_core  (engine/)  — The Game Engine Library       │
├──────────────────────────────────────────────────────────┤
│  Core        Rendering        Physics       Scene        │
│  ─────       ─────────        ───────       ─────        │
│  ChunkMgr    RenderCoord      GpuParticle   Entity       │
│  WorldGen    VulkanDevice     VoxelDynWorld Character    │
│  EntityReg   RenderPipeline   Materials     NPCEntity    │
│  EngineRT    Camera/CamRig    Collision     AnimatedChar │
│  AudioSys    Light/DayNight                 VoxelInteract│
│  APIServer   PostProcessor                  Raycaster    │
│  JobSystem   ShadowMap / SSAO                            │
│                                                          │
│  UI           Scripting       Story          Input       │
│  ──           ─────────       ─────          ─────       │
│  UISystem     ScriptingSys    StoryEngine    InputMgr    │
│  Dialogue     pybind11        CharAgent                  │
│  GameScreen                   EventBus                   │
│  GameMenus                    StoryDirector              │
├──────────────────────────────────────────────────────────┤
│  phyxel_editor  (editor/)  — Development Application     │
│  Application · Python REPL · MCP Server · Debug Overlays │
│  Template/Anim Editing · Entity Spawning · AISystem      │
├──────────────────────────────────────────────────────────┤
│  Standalone Games  (examples/ or scaffolded projects)    │
│  Link phyxel_core, implement GameCallbacks               │
└──────────────────────────────────────────────────────────┘
```

Physics note: **Bullet Physics has been removed** from active builds. The live stack is
`GpuParticlePhysics` (Vulkan compute XPBD/AVBD, large-scale debris) + `VoxelDynamicsWorld`
(custom CPU rigid-body world: furniture, character grounding, break debris).

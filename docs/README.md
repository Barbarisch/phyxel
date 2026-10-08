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
| **New to the engine internals** | [SubsystemArchitecture.md](SubsystemArchitecture.md) → [CoordinateSystem.md](CoordinateSystem.md) → [VoxelRenderPipelines.md](VoxelRenderPipelines.md) |
| **Changing engine code** | [ForwardingSurface.md](ForwardingSurface.md) — what docs/skills/tools must stay in sync (enforced by `tools/check_doc_sync.py`). |

The repo-root **[`CLAUDE.md`](../CLAUDE.md)** is the canonical quick-reference (build pipeline,
materials, coordinate system, MCP overview). When in doubt, it wins over any doc here.

---

## Game development

- **[GameCreationGuide.md](GameCreationGuide.md)** — AI-driven game creation workflow (MCP / Claude Code)
- **[GameDevWorkflow.md](GameDevWorkflow.md)** — per-project session workflow, the `phyxel` CLI, per-machine setup
- **[Multiplayer.md](Multiplayer.md)** — server-authoritative co-op for **shipped games** (designed 2026-07-07; the editor API is explicitly NOT the network protocol)
- **[StandaloneGameTesting.md](StandaloneGameTesting.md)** — manual standalone-game test checklist

## World & Structure Generation

- **[structure-generation/](structure-generation/README.md)** — the **structure generator** (buildings/settlements): design, the grounded `StructureBrief` intake, placers, room/archetype data sheets, known issues, and the standing validation discipline. **Start at its README** — the canonical entry point for all structure-gen work.
- **[structure-generation/DimensionReference.md](structure-generation/DimensionReference.md)** — generated, grounded dimension canon (every furniture/typology size + its citation).
- **[TerrainGenerationBiomes.md](TerrainGenerationBiomes.md)** — terrain/biome world generation (the sibling pipeline).
- **[WorldModel.md](WorldModel.md)** — world semantics: one kind of world (recipe + DB overlay), streaming-flag retirement plan, the world recipe (absorbed WorldRecipeAndFlora).

## Engine architecture

- **[SubsystemArchitecture.md](SubsystemArchitecture.md)** — callback-based subsystem pattern
- **[EntitySystem.md](EntitySystem.md)** — entity types, characters, AI (note: Bullet ragdoll path deprecated)
- **[SceneSystem.md](SceneSystem.md)** — multi-scene games (per-scene world DB, transitions)
- **[EngineArchitectureAudit.md](EngineArchitectureAudit.md)** / **[EngineRobustnessAudit.md](EngineRobustnessAudit.md)** — decomplexification + defect audits (decision records)

## Rendering & world

- **[VoxelSystem.md](VoxelSystem.md)** — voxel sizes (cube/subcube/microcube) + static/kinematic/dynamic lifecycle
- **[VoxelRenderPipelines.md](VoxelRenderPipelines.md)** — three Vulkan voxel pipelines (static / kinematic / GPU particle)
- **[LightingPipeline.md](LightingPipeline.md)** — THE lighting reference: the atmosphere model (sun,
  moon, sky, haze), the baked per-voxel light field, the three shadow cascades, exposure + AgX, and
  the known gaps
- **[GlassTransparency.md](GlassTransparency.md)** — current-state reference for transparent materials (glass): the WBOIT pass, material/texture and the T ≈ 0.80 target, no shadow, frosted cracks, face culling around glass, and the chunk-border ripple that keeps every edit route's borders correct
- **[TextureSystemOverhaul.md](TextureSystemOverhaul.md)** — PBR texture-array system (Phases 1–2 merged)
- **[LargeWorldScalePlan.md](LargeWorldScalePlan.md)** — active workstream: chunk RAM (`ChunkVoxelStore` palette storage), region GPU buffer arenas, sealed/uniform chunks
- **[RegionArenaPlan.md](RegionArenaPlan.md)** — region-keyed GPU buffer arena suballocation (`ChunkArenaAllocator`/`ChunkArenaSystem`), shipped/default-on
- **[CameraRelativeRendering.md](CameraRelativeRendering.md)** — camera-at-origin rendering, the continental-coordinate float-precision fix

## Terrain, structures & assets

- **[TerrainGenerationBiomes.md](TerrainGenerationBiomes.md)** — streaming + data-driven biomes (implemented on main)
- **[WorldModel.md](WorldModel.md)** — world semantics + the per-world recipe (flora decoration details: TerrainGenerationBiomes.md)
- **[StructurePipelineGaps.md](StructurePipelineGaps.md)** — running log of pipeline gaps to implement
- **[AssetPipeline.md](AssetPipeline.md)** — importing 3D models / animations into voxel templates
- **[MaterialTextureNeeds.md](MaterialTextureNeeds.md)** — standing list of missing materials/textures

## Coordinates & math

- **[CoordinateSystem.md](CoordinateSystem.md)** — world/chunk/local transforms, indexing, bit-packing (the comprehensive doc)

## Physics

- **[DynamicVoxelPhysics.md](DynamicVoxelPhysics.md)** — GpuParticlePhysics (GPU AVBD debris: live pipeline, contact model, sleep, test gate) + VoxelDynamicsWorld (CPU); break routing
- **[DebrisSettlingPlan.md](DebrisSettlingPlan.md)** — GPU debris settling: §R = what was wrong, what fixed it, the measurement (DebrisLab + `tools/debris_settle_bench.py`), before/after evidence, and why it took six months. READ before touching `solver_*.comp`
- **[DebrisInteractionPlan.md](DebrisInteractionPlan.md)** — **COMPLETE 2026-10-07 (phases 0–6 on main; open items in its status header).** PLAN rev 4 (2026-10-04): full code inventory of every debris system / collision writer / mover / shader; simplification FIRST (delete legacy XPBD, CPU DebrisSystem, ForceSystem, dead paths), then build-script & constant hygiene, debris adopts the lighting occupancy (one occupancy for physics/lighting/debris), all break debris on GPU with texture parity, all movers as real contacts, impulses in both worlds, debris in shipped games
- **[PhysicsRestOverhaul.md](PhysicsRestOverhaul.md)** — CPU `VoxelDynamicsWorld` Box3D-style rest (current); its GPU Phase-2 claims are corrected by DebrisSettlingPlan.md
- **[DestructionSystemV2.md](DestructionSystemV2.md)** — THE destruction doc (active workstream: coherent fracture/topple, tool-driven impact, gatherable aftermath). Absorbed the v1 design as its **Appendix A** on 2026-09-22 (`DestructionSystem.md` deleted; git-hash ledger in that appendix)
- **[VoxelDamageVisualization.md](VoxelDamageVisualization.md)** — current-state reference for damage cracks on damaged-but-unbroken voxels (7 world-seeded stages, per-material style, debug view 19). ⚠️ **Full cubes only — generated buildings have sub-voxel walls and cannot crack until V2** (see its §5 / §9)
- **[WaterCore.md](WaterCore.md)** — 2026-10-08 THE design of record for water: small-scale function/feel as the core (14 measured scenarios), 3-D Eulerian voxel liquid in sub-voxel active volumes, large bodies on top; gated phases A–G; decisions pending in §12
- **[WaterRethink.md](WaterRethink.md)** — 2026-10-07 full stock-take + root causes + the design-keys gates + the WP0 ledger (benches, tooling, baselines); its work-package ordering is superseded by WaterCore.md
- **[Water.md](Water.md)** — THE water current-state doc (layers, constants, traps, history; supersedes WaterSystem v1/v2/v3, PhysicalFeelPlan, AppearanceV4, WaterAsWorldData)
- **[SubcubeCollisionPlan.md](SubcubeCollisionPlan.md)** — subcube-resolution character collision (user directive 2026-07-16: collision shape must match what you see)
- **[PhysicsCharacter.md](PhysicsCharacter.md)** — ⚠️ deprecated (Bullet character fully removed, git-history-only; see EntitySystem.md)

## Characters & animation

- **[AnimatedCharacter.md](AnimatedCharacter.md)** — `AnimatedVoxelCharacter` (.anim FSM, the primary character)
- **[CharacterAnimationGuide.md](CharacterAnimationGuide.md)** — animation states, naming, offsets
- **[InteractionPipeline.md](InteractionPipeline.md)** — character ↔ object interaction (sitting, etc.) tuning pipeline
- **[LessonsLearned_ProceduralAnimation.md](LessonsLearned_ProceduralAnimation.md)** — why the current animation approach won (history)
- **[HumanoidAnimationMigration.md](HumanoidAnimationMigration.md)** — live review ledger for replacing combat/gathering clips in `humanoid.anim`
- **[NavigationArchitecture.md](NavigationArchitecture.md)** — NPC navigation (Layer-1 NavGraph + async PathService on main; HPA* deferred)

## Story, RPG & combat

- **[StoryEngineDesign.md](StoryEngineDesign.md)** — story arcs, character agents, narrative system design
- **[DnDRPGSystem.md](DnDRPGSystem.md)** — D&D ruleset (dice, attributes, classes, spells, items)
- **[TurnBasedCombat.md](TurnBasedCombat.md)** — BG3-style turn-based combat (HUD via UISystem)
- **[RealTimeCombatAI.md](RealTimeCombatAI.md)** — real-time combat stack (`CombatBehavior` melee, `RangedCasterBehavior` casters): cover, chain of command, per-combatant intelligence
- **[HudSystem.md](HudSystem.md)** — data-driven HUD/UISystem (includes remaining-work section)

## Cameras, UI & debug

- **[CameraControlSystem.md](CameraControlSystem.md)** — camera rigs + control schemes (implemented; live switching)
- **[Keybindings.md](Keybindings.md)** — full keybinding reference (authoritative)

## Integration, AI & testing

- **[GooseIntegration.md](GooseIntegration.md)** — Goose AI NPC integration (Phase 1 + parts of 2/3 shipped and live-wired)
- **[IntegrationTesting.md](IntegrationTesting.md)** — integration-test fixtures & patterns
- **[LoggingSystem.md](LoggingSystem.md)** — logging system internals + migration guide

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
`GpuParticlePhysics` (Vulkan compute AVBD, large-scale debris) + `VoxelDynamicsWorld`
(custom CPU rigid-body world: furniture, character grounding, break debris).

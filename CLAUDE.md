# CLAUDE.md — Phyxel Voxel Game Engine

> **New session? Read [`docs/AgentContext.md`](docs/AgentContext.md) first.** It holds the
> portable, hard-won working context (operational gotchas, engine ground truth, current
> workstreams + roadmap, user preferences) — the substitute for per-machine agent memory
> when picking up on a different computer.

## 🎯 Current focus — Structure Generation v2 (as of 2026-06-28)

**What we're doing:** building the **structure generator** — the engine procedurally generates
*grounded, validated* buildings & settlements (the engine builds them, not Claude hand-placing voxels).
All on `main` now. Pipeline: `StructureBrief → BuildingProgram → autofillRoomLayout → StructureRealizer
→ place`. Build at runtime via `POST /api/structure/build {"schema":"v2",...}`.

**Recently shipped (grounded + red-before-green + auditor-verified):** first functional typology — the
`tavern` (taproom/kitchen/service, L3-navigable); **generative multi-story** (inn upstairs guest
chambers + auto-generated stair); **inn asset depth** (grounded bar/stools/back-bar/lighting/mugs/
bottles via `tools/regen_furniture.py`); silent-furniture-drop fix; all 16 furniture conformant;
build-freeze perf fix (place 13.8s→0.9s).

**#1 known issue: render density — MECHANISM SHIPPED, the original scenario NOT re-measured
(corrected 2026-07-29, then scoped down after a solution-auditor FAIL).** The old entry here
("sub/micro faces aren't greedy-merged; fix deferred") was **stale by ~3 weeks**: fine greedy
merging shipped **default ON 2026-07-07** (`ChunkRenderManager::s_fineGreedyMerge = true`), and the
main-pass 36-index amplifier is fixed (`s_quadDraw`, main pass only).
**But do not read that as "solved."** What is actually proven is narrower than a first correction
of this entry claimed:
- **Proven:** on *synthetic flat multi-tavern grids* (an explicit face-bound proxy), 12× fewer faces
  and ~5–8× FPS recovery (`docs/RenderOptimization.md:387-388`).
- **NOT proven:** the scenario this entry was always about — **one furnished tavern at 412,298
  faces / 49 FPS — was never re-measured for FPS**; at that scale the doc says FPS is *noise*
  (±20% restart variance, sign flips Debug vs Release, `RenderOptimization.md:352`). And the
  documented worst case, the **3.4M-face Perlin-hills settlement, "remains not run"**
  (`RenderOptimization.md:376-380`).
- **The falsifiable test that would settle it:** regenerate the 3.4M-face settlement via the
  engine's own generator, measure `total_visible_faces` + FPS at a fixed verified pose on a Release
  build. Queued as M4 in [`docs/ContinuousLodPlan.md`](docs/ContinuousLodPlan.md) §7b.

**Separately, the measured #1 render cost today is the SHADOW PASS: 24–26 ms of a 34.8 ms frame
(~75%)** (`docs/RenderDensityPlan.md` §2d). Three levers are exhausted — primitives, resolution
(~30%), light-frustum cull (no effect). The residue is **~17 ms of per-draw overhead across ~131
draws**, so the remaining fixes are structural: batch the shadow draws, update cadence, cascades.
Carried by [`docs/ContinuousLodPlan.md`](docs/ContinuousLodPlan.md) (C2 → C5).
⚠️ **Shadow cull-mode: the record has been wrong TWICE — do not trust either version.**
`RenderCoordinator.cpp:1241` claims the shadow pipeline front-culls. An earlier fix here said it is
`VK_CULL_MODE_NONE`; that named the WRONG pipeline (`buildDepthOnlyPipelineState`, used only by
character/kinematic/dynamic shadows). The **main chunk shadow pipeline** (`createPipeline`) sets
**`VK_CULL_MODE_BACK_BIT`** at `ShadowMap.cpp:388` — same winding rules as the main pass. So the
recorded reason the shadow pass needs a 36-index draw is still unexplained, and the ~1.1% pixel
break D1 measured when applying the 6-index quad to all passes has an unknown cause. Re-derive it
empirically (M5); do not guess a fourth time.
Open visual defects: T-junction cracks at greedy-merge borders, character/grass sub-pixel speckle
(`docs/RenderOptimization.md:489,513`).

**Full state + next steps:** [`docs/AgentContext.md`](docs/AgentContext.md) (top workstream) →
[`docs/structure-generation/README.md`](docs/structure-generation/README.md) (canonical entry) →
`ValidationLedger.md` + generated `DimensionReference.md`. **Discipline (enforced):** ground every
dimension (grounding-auditor); red-before-green + solution-auditor on every "works/fixed" claim
(Stop-hook gate); "reachable" must mean physically walkable.

> **HARD RULE — provenance / no substitution (enforced by the Stop-hook):** When asked to *use the
> engine* to generate something (a town, structure, terrain…), the deliverable is the **engine's
> generator producing it** (e.g. `POST /api/settlement/build` for a town). If the generator errors or
> looks wrong, **say so and fix the generator** — NEVER hand-assemble a lookalike (individual
> `build_structure` calls, hand-placed voxels) and present it as the engine's output. **State exactly
> what produced every artifact** (which generator/route, or "hand-placed") — no burying it. A
> screenshot is never evidence on its own; cite the command run + its raw result. Presenting hand-work
> as generator output is a fabrication, same as a fake test pass.

## 🚦 BEFORE ADDING ANY FEATURE — read [`docs/FeatureDesignKeys.md`](docs/FeatureDesignKeys.md)

Standing gate. These must be answered **in the plan**, not discovered during implementation.

**Design keys.** Voxel-based engine — new features must match the voxel aesthetic. Aiming well above
Minecraft on detail (smaller voxels), lighting, and physics. Voxels are static or dynamic; static
ones come in cubes, subcubes (1/3), microcubes (1/9), and are stored/rendered via **chunks**, which
exist for optimization. Dynamic voxels are physics bodies and must behave physically.

**Before you continue?**
- Does this fit a **procedural generation pipeline**? If so put it there, and check it does not
  disturb other stages.
- Is it exposed over an **API**? Is that interface robust and thought through?
- Does the plan include **how it will be visually tested**?
- Does the test plan include an appropriately **small, simplified test world**?

⚠️ **Chunks must not be visible.** Appearance is a pure function of world position + persistent
world state; per-chunk quantities may only bound **cost**, never looks. `bladesForDistance` is the
model (conservative bound from the chunk's nearest corner). Reaching for cross-chunk lookups to
patch a chunk-local dependency is usually wrong — derive it from world position or from the
generator instead. Pin it with a chunked-vs-whole-region equality test (`FloraMarginTest`,
`FaunaPlanTest`). Known live violation: grass/foliage are skipped entirely at chunk LOD ≠ 0, which
is why distance-driven chunk LOD stays default-OFF.

⚠️ **Test rigs:** small, inside ONE chunk, one variable, a written-down prediction, and a control.
Verify the world, not the API response (`/api/world/fill` is async and returns no placed count).
State how rig settings differ from shipped defaults. Never trust a test run started before your
edits.

## Auto-Context on Startup

When starting a new conversation in the engine terminal, **proactively check engine state** before the user asks. Call `engine_status` — if the engine is running, gather world context by running `/context` (or manually calling the same MCP tools). This saves the user from having to explain what's loaded. If the engine is not running, skip and wait for instructions.

## Project Overview

Phyxel is a voxel game engine built with C++17 and Vulkan. Physics is an in-house stack — a GPU compute solver (`GpuParticlePhysics`, Vulkan XPBD/AVBD, primary for large-scale debris/destruction) plus a custom CPU rigid-body world (`VoxelDynamicsWorld`, used for furniture, character grounding, and left-click break debris). **Bullet Physics has been removed** entirely (the `external/bullet3` submodule was dropped; the `stb_image`/`stb_truetype` headers it used to provide are now vendored at `external/stb`). It features a 32³ chunk system, animated voxel characters, GPU/CPU voxel physics, embedded Python scripting, and an MCP server for AI agent integration.

## Build & Test Pipeline (REQUIRED)

**Always use MCP tools for the build/test/debug cycle. Never run cmake or PowerShell build commands directly.**

### Build
Use `build_project` MCP tool (not cmake in PowerShell). It runs the build off the MCP event loop
and handles the CMake path correctly. **For long/full builds (especially `reconfigure: true`),
call `build_project` with `background: true`, then poll `build_status`** before `launch_engine` —
this returns immediately instead of tying up one tool call for many minutes. The foreground form
(default) still works for quick incremental builds.

### Engine Lifecycle
Use `launch_engine` / `stop_engine` / `restart_engine` / `engine_running`. The linker cannot overwrite `phyxel.exe` while it is running — always `stop_engine` before rebuilding.

### Verifying a Fix — MANDATORY
After applying any code change, you MUST verify it works at runtime, not just that it compiled:
1. `stop_engine` (if running)
2. `build_project`
3. `launch_engine` with appropriate args
4. Poll `engine_running` until `api_responsive: true`
5. Trigger the specific scenario that was broken
6. Capture evidence: `get_visual_diagnostic` (rendering fixes) or `screenshot` + `get_engine_logs` (crash/behavior fixes)
7. `stop_engine` when done (unless engine was pre-running)

**A fix is not done until the engine runs it successfully. "Compiled clean" is not verification.**

### Stress Test Phase — MANDATORY for every new feature

A single happy-path proof (N=1) is **not** verification. Designing the adversarial test that
breaks a feature is the agent's job — the user should never have to invent the esoteric case
(e.g. "build a 10-story tower"). For every feature/increment, **before calling it done**, identify
the dimension along which it scales and push that to the extreme, asserting the invariant holds at
*every* step (not just in aggregate):

- **Count / quantity** — N=1 → N=many (10 stories, 100 NPCs, a full chunk, 1000 entities).
- **Size / extent** — minimal → maximal, especially crossing an internal boundary (a structure
  taller than one chunk, a path across the world).
- **Depth / nesting** — flat → deeply nested (deep object trees, recursion).
- **Repetition / churn** — once → many cycles (spawn+despawn ×1000, rebuild loops).
- **Boundary crossing** — values straddling a hidden limit (the y=31→32 vertical-chunk seam,
  voxel caps, buffer sizes).
- **Degenerate / extreme inputs** — empty, zero, negative, max, malformed.

A good stress test asserts the **invariant at scale** ("every floor reachable", "true placed/failed
counts", "no overlap", "no silent drop") — not merely "it ran". Where practical, encode it as a
deterministic **unit test** (geometric/scale) AND exercise it at **runtime** (functional), then
commit it alongside the feature. The 10-story tower didn't just confirm "a building builds" — it
surfaced three bugs (the silent voxel-cap ghost, the vertical-chunk placement gap, cross-story
furniture stacking) that the N=1 proof hid completely. **Make this a standing phase, not a prompt.**

### Validation Layers — plan the depth up front

Validation is a **planned deliverable, not an afterthought** (the stress-test phase is the *scale*
axis; this is the *depth* axis). For every placer / bit of generation logic, name in the plan:
1. the functional **contract** — what "works" actually means;
2. the **required validation layer**, set by what the output is *used for* — **L1** artifact exists ·
   **L2** structural invariant measured on the real output (no overlap, continuous, clearance —
   validator / canvas scans) · **L3** functional agent simulation (a character-box can *use* it, via
   `TraversalProbe`) · **L4** live-engine runtime;
3. the **red test** that proves it, shown failing first.

Pick depth by use: usability-critical + silent-failure + scaling placers get L3 first; spatial-only →
L2; cosmetic → L1. A placer isn't done until **current layer ≥ required**, red-before-green, auditor-
confirmed. Track every placer's required-vs-current depth in
[`docs/structure-generation/ValidationLedger.md`](docs/structure-generation/ValidationLedger.md);
`place_stairs` is the worked exemplar (L3, geometry + gate + traversal).

### Diagnostics Without a Project
`screenshot`, `get_visual_diagnostic`, `get_engine_logs`, `get_render_stats`, `set_log_level`, `set_debug_overlay`, `stop_engine`, `restart_engine`, and `clear_engine_logs` all work in any engine mode — including `--asset-editor`, `--anim-editor`, and no-project states.

### Rendering / Visual Fixes
Use the `/visual-test` skill — it handles the full lifecycle (build check → launch → scene setup → multi-source capture → report).

### Raw Build Reference (fallback only)
- **CMake is NOT in system PATH**. Add it first:
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
- **Character facing: model-space face = +Z** (`getForwardDirection()` = +Z at yaw 0; face
  features on rigs go at positive Z). Authoritative: `docs/CoordinateSystem.md` §Character
  Facing; pinned by `CharacterFacingTest`. Never read facing off screenshots of patrolling NPCs.

## Materials

Data-driven from `resources/materials.json` (102 materials — the table below is the core subset;
roofing/finish/state materials live in the JSON). Names are **case-sensitive**; an unknown name
renders as a magenta missing-texture checkerboard. Confirm live with the `list_materials` MCP
tool. (Cork and Rubber no longer exist.) CC0 photo sources + asset IDs:
`tools/fetch_cc0_textures.py` (ambientCG) and `tools/fetch_polyhaven.py` (Poly Haven; StoneBricks +
the roof family, with measured course-crops so roof courses land on the ⅓-voxel subcube-step grid) —
provenance in `resources/textures/source/CC0_SOURCES.json`. LogBirch is procedural on top of the
fetched base: re-run `tools/gen_birch_bark.py` after any LogBirch re-fetch. `varied` (per-voxel hash
rotation) is for soft isotropic naturals ONLY — never on coursed/blocky patterns (masonry, layers),
where it breaks pattern continuity at voxel edges.

| Name         | Mass | Friction | Restitution | Notes |
|--------------|------|----------|-------------|-------|
| Default      | 1.0  | 0.5      | 0.3         | Error/fallback indicator |
| Dirt         | 2.0  | 0.7      | 0.1         | Terrain sub-surface |
| Grass        | 1.8  | 0.7      | 0.1         | Grass-topped dirt (terrain surface) |
| GrassForest  | 1.8  | 0.7      | 0.1         | Deep forest grass (biome variant) |
| GrassSavanna | 1.8  | 0.7      | 0.1         | Dry golden savanna grass (biome variant) |
| SnowGrass    | 2.0  | 0.6      | 0.1         | Snow-dusted taiga ground — Snow-biome surface, keeps conifers (matte; white top, snow-crust dirt sides) |
| Snow         | 0.3  | 0.2      | 0.1         | Bare permanent snowpack — alpine cap above the treeline, blocks flora (matte white all faces) |
| Stone        | 6.0  | 0.8      | 0.05        | Dense smooth stone |
| Cobblestone  | 5.0  | 0.9      | 0.05        | Rounded street setts, grassy joints (settlement `dry_stone`) |
| StoneBricks  | 6.0  | 0.8      | 0.05        | Rough-hewn dressed blocks (settlement `ashlar`, Poly Haven, 1024px) |
| StoneTiles   | 6.0  | 0.8      | 0.05        | Smooth rectangular cut-stone floor tiles (interiors) |
| Sand         | 1.5  | 0.5      | 0.1         | Light granular |
| Gravel       | 2.5  | 0.7      | 0.1         | Loose gravel |
| Wood         | 0.7  | 0.6      | 0.2         | Oak planks — FLOOR wood (1024px) |
| WoodPlanks   | 0.7  | 0.6      | 0.2         | Lapped plank siding — building-WALL wood (`timber_frame`, structure/cladding/trim; Poly Haven, 1024px) |
| Log          | 0.7  | 0.6      | 0.2         | Oak bark (all faces — no ring caps yet) |
| LogBirch     | 0.7  | 0.6      | 0.2         | White paper birch + lenticels (procedural, `gen_birch_bark.py`) |
| LogSpruce    | 0.7  | 0.6      | 0.2         | Grey plated spruce bark |
| LogPine      | 0.7  | 0.6      | 0.2         | Scaly-plate pine bark (pine + fir presets) |
| LogJungle    | 0.7  | 0.6      | 0.2         | Smooth green-grey tropical bole |
| LogPalm      | 0.7  | 0.6      | 0.2         | Pale fibrous palm trunk |
| LogRedwood   | 0.7  | 0.6      | 0.2         | Deep-furrowed warm bark (redwood + elder_oak megaflora) |
| Bricks       | 4.0  | 0.8      | 0.05        | Red clay bricks (1024px) |
| Sandstone    | 3.0  | 0.7      | 0.1         | Layered sandstone |
| Glass        | 1.5  | 0.2      | 0.6         | Brittle, transparent |
| Metal        | 4.0  | 0.7      | 0.1         | Heavy iron (metallic) |
| Gold         | 8.0  | 0.4      | 0.2         | Densest, lustrous (metallic) |
| Ice          | 0.9  | 0.1      | 0.4         | Very slippery |
| Leaf         | 0.1  | 0.3      | 0.1         | Light oak foliage |
| LeafBirch    | 0.1  | 0.3      | 0.1         | Birch foliage (biome variant) |
| LeafSpruce   | 0.1  | 0.3      | 0.1         | Spruce needles (biome variant) |
| LeafJungle   | 0.1  | 0.3      | 0.1         | Jungle canopy (biome variant) |
| LeafAutumn   | 0.1  | 0.3      | 0.1         | Orange autumn foliage (biome variant) |
| glow         | 1.0  | 0.5      | 0.5         | Emissive (warm white) |
| glow_blue    | 1.0  | 0.5      | 0.5         | Emissive blue |
| glow_green   | 1.0  | 0.5      | 0.5         | Emissive green |
| Mirror       | 2.5  | 0.4      | 0.1         | Reflective (mirror pass) |

Textures are authored per-face per-material (source PNGs in `resources/textures/source/`) and packed
into a **mixed-resolution `sampler2DArray`** (512px terrain / 1024px detail objects, BC7-compressed) —
not a single fixed atlas. Materials with `"resolution": 1024` (e.g. StoneBricks, Wood, Bricks) use the
high-res array. Full design: `docs/TextureSystemOverhaul.md`. Rebuild after texture changes:
`.\build_shaders.bat` (also manually recompile `voxel.frag` since glslc doesn't track `#include` deps).
Lookup: `MaterialRegistry::instance().getTextureIndex(materialID, faceID)` — data-driven via `resources/materials.json`

## Voxel Rendering Pipelines

Three separate vertex shaders handle voxels in different states. All share `voxel.frag`.

| Shader | Purpose | Instance Data | Sub-tile UV Method |
|--------|---------|---------------|--------------------|
| `static_voxel.vert` | Chunk voxels (baked into 32³ grid) | `InstanceData` (24B) — packed position/face/scale + texture/flags + tint + 3 per-corner lighting words (skylight + block light) | GPU decodes grid positions per face |
| `dynamic_voxel.vert` | GPU particle debris (compute-expanded) | `DynamicSubcubeInstanceData` (64B) — world pos, scale, rotation, localPosition for grid | GPU decodes localPosition per face |
| `kinematic_voxel.vert` | Moving rigid groups (doors, furniture, fragments) | `KinematicFaceData` (40B) — local pos, scale, pre-computed uvOffset | CPU pre-computes uvOffset in `buildFaces()` |

**Texture mapping for subcubes/microcubes:** Each voxel face shows only its portion of the parent cube's texture. A subcube (1/3 scale) at grid position (1,2,0) gets UV offset (1/3, 2/3) on applicable axes. Per-face axis mapping and flips ensure seamless tiling. The three shaders achieve the same visual result via different encoding strategies (packed bits, grid positions, or pre-computed offsets).

**Compiling shaders:** `.\build_shaders.bat` — compiles all `.vert`/`.frag`/`.comp` to SPIR-V in `shaders/*.spv`. The CMake build copies compiled shaders to `build/shaders/`.

## Entity Types

Spawnable via MCP `spawn_entity` or keybindings. Control mode toggled with **K**.

| Type       | Class                    | Notes |
|------------|--------------------------|-------|
| `physics`  | _(removed)_              | Bullet ragdoll `PhysicsCharacter` was deleted with the Bullet removal (recoverable from git history). The primary character is `animated`. |
| `spider`   | _(removed)_              | `SpiderCharacter` was deleted with the Bullet removal — non-functional. Use `animated`. |
| `animated` | `AnimatedVoxelCharacter` | .anim-based FSM: Idle/Walk/Run/Jump/Fall/Attack/Crouch/etc. Kinematic capsule; grounds against `VoxelDynamicsWorld` occupancy grids. |

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

### Terrain Generation v2 — procedural mountains, rivers & lakes (`Perlin`/`Mountains` + `world.streaming: true`)

Height-based worlds layer an **art-directable, hydrology-aware terrain system** on top of the base
noise. Full design: [`docs/TerrainGenerationV2.md`](docs/TerrainGenerationV2.md); the water runtime that
fills it: [`docs/Water.md`](docs/Water.md) (THE single water doc — architecture, status, traps;
supersedes the six retired water docs). Two tiers:

- **Layer-0 CoarseWorldModel** (`engine/{include,src}/core/CoarseWorldModel.*`) — a bounded, pure,
  deterministic coarse model of continentalness + base elevation. **Continentalness** (a third climate
  axis, contrast-expanded) drives landmass size; base elevation runs through a **recipe-overridable
  height `Spline`** (`WorldRecipe::heightSpline`, round-trips through `world.db`) — the "how tall"
  art-direction knob. **Mountains** are **ridged multifractal** (Musgrave), a broad massif band +
  fine detail so peaks slope to the angle of repose instead of forming sheer columns (grandest peaks
  ~384 voxels above sea level). `kSeaLevelY = 16` is the shared sea-level constant
  (`engine/include/core/WorldConstants.h`) — consumed, never re-declared.
- **GOTCHA — the surface is HIGH, so static generation must reach it.** Because the surface can sit
  hundreds of voxels above `kSeaLevelY=16`, **`generate_world` / a `game.json` `world` from/to range
  only fills the chunk range you name** — a shallow *low* range (e.g. chunk `y=0` only, world y 0-31)
  generates the **deep underground** (solid `Stone`, `materialForColumn` depth≥4 = deepMaterial), NOT
  the grass surface, which may be up at y~75-380. Symptoms of this mistake: a "flat stone plane" whose
  `surface_y` equals the top of your generated range. Fixes: use **`world.streaming: true`** (streams
  chunks at the real surface — also required for hydrology), OR generate a tall enough vertical chunk
  range, OR query `get_terrain_height` and spawn/aim the camera at the true `surfaceY`.
- **Hydrology bake** (`HydrologyMap`, `FlowField`, `PriorityFlood` — terrain-v2 §P2) — over a bounded
  256×256-cell / 128 m-per-cell region (~32 km), a Priority-Flood pass computes flat **lake & sea
  water-surface levels** (basins fill to their spill), and flow accumulation + Strahler ordering trace
  the **river network** (drainage → carve valleys → meander; order 5-6 trunk rivers across ~6.7 km
  continents). River channels of **Strahler order ≥ 3** carve a parabolic bed; width/depth by order
  (Doll et al. hydraulic geometry). Read from the live generator via
  `ChunkManager::getStreamingGenerator()->hydrology()` / `->riverNetwork()`. Bake is memoized +
  worker-copy-safe. **Requires `world.streaming: true`** (the bake lives on the streaming generator).
- **Water runtime fills it** (Water System v2, `WaterManager`) — a player-following CA region reads the
  bake and pins **oceans at sea level + lakes at their spill + rivers along their carved beds**; a
  **runtime shoreline snap** conforms the coarse wet/dry boundary to the per-voxel carved contour;
  evaporation bounds off-channel spill. game.json `water` block: `enabled`, `seaLevel`, `bakedTable`
  (default true when a bake exists), `oceanBoundary`, `evaporation`. Debug: `water_stats`,
  `water_table_level {x,z}`, `water_validate {x1,z1,x2,z2,maxY}` (L3 bake-vs-terrain rim-leak check).
  **NOT built yet:** far/near render LOD for water beyond the sim region (Phase B); generator-side
  coastal beach material. See the design docs for open items.

### Biomes, Flora & the World Recipe

Height-based terrain (Perlin/Flat/Mountains/Caves) is **biome-aware** and gets **flora**:

- **Biome categories** are data-driven in `resources/biomes.json` (climate ranges, materials,
  per-biome flora). Selected per column by temperature+moisture+continentalness climate fields;
  `params.climateFrequency` sets biome size (lower = bigger). Design: `docs/TerrainGenerationBiomes.md`.
- **Flora decoration** scatters biome-appropriate vegetation (trees/bushes) on the surface.
  Placement is order-independent (deterministic local-maxima Poisson), so it's seam-free and
  works per-chunk. `flora.spacing` = min distance (slot grid), `flora.density` = fraction of slots
  filled. Two modes: **`pool`** stamps the sub-voxel `gen_tree.py` `.voxel` templates (default);
  **`procedural`** generates unique trees in-engine via `ProceduralTree` (a C++ port of
  `gen_tree.py`) — currently streaming-only.
- **Trees** are branch-driven (organic, not spheres). Author/regenerate the template library with
  `python tools/gen_tree.py --batch tools/tree_library.json` (use `--preview` for fast ASCII shape
  iteration); restart the engine to reload templates. Height/fullness "extremeness" = weighting
  template variants (e.g. `tree_oak_lush` vs `tree_oak_sparse`).
- **World recipe**: each world's generation tuning (seed, biome size, extremeness, flora) is
  persisted in `world.db` (`world_meta` table) on first load — the DB becomes the source of truth,
  so editing global `biomes.json` no longer changes existing worlds. Full design + remaining work:
  `docs/WorldModel.md`.

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

> **HARD RULE — detail assets use sub-voxel resolution, NEVER full cubes.** The engine has
> three voxel sizes: cube (1 unit), subcube (1/3), microcube (1/9). Full cubes are for coarse
> mass only. **Any small/detailed object — especially a handtool held in the fist (axe, sword,
> pickaxe, dagger…) — MUST be authored in microcubes** (`M px py pz sx sy sz mx my mz Mat`),
> skinny (≈1 microcube thick), sized at true scale so the item's `held.scale` is ~1.0. The
> canonical example is `resources/templates/weapons/sword_fine.voxel`; copy its convention.
> A handtool built from `C` (full-cube) lines is a **defect** — it renders as an oversized
> blocky plank in the hand. **Verify every new/edited asset visually** (spawn it, screenshot
> in-hand at scale) before calling it done — a full-cube-only detail asset must never ship.
> (The crude `axe.voxel`/`pickaxe.voxel`/`sword.voxel` are the anti-pattern; `sword_fine.voxel`
> and the remodeled `axe.voxel` are the pattern.)

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
**MCP tools**: `generate_template`, `search_templates`, `list_generated_templates`
(`build_building` was REMOVED 2026-07-08 — generated buildings come from the engine's
procedural pipeline only: `build_structure` schema v2 / `build_settlement`, never an LLM.
The v1 composite generators (house/tavern/tower, BuildingSpec) were removed the same day;
plain `build_structure type:house|tavern` aliases onto v2 typologies.)

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
| B | Break Voxel (plain Left Click no longer breaks — see below) |
| Middle Click | Subdivide Cube |
| T / Shift+T | Spawn Static / Dynamic Template (via Template Spawner panel or MCP `spawn_template` — not a live keybind) |
| P | Toggle Template Preview |
| -/= | Ambient Light |
| [/] | Spawn Speed |
| K | Toggle Character Control (only one control target — AnimatedCharacter — exists now; effectively a no-op) |
| W/A/S/D | Movement |
| Space | Jump (Animated) |
| Shift | Sprint |
| Ctrl | Crouch (Animated) |
| Left Click | Attack / Cast / Furniture Throw+Activate (Animated) |
| X | Derez character |
| N/B | Next/Prev Animation (Preview Mode only — B breaks voxels outside preview mode) |

> Full, authoritative keybinding list (incl. F2/Shift+F5/O/G, asset- and anim-editor modes): [`docs/Keybindings.md`](docs/Keybindings.md).

## MCP Server (AI Agent Bridge)

Server: `scripts/mcp/phyxel_mcp_server.py` — HTTP API at `localhost:8090` (default; the server
honors `PHYXEL_API_PORT`/`PHYXEL_API_URL`). **Multiple engines can run at once, each on its own
port** — game projects get a per-project port via the `phyxel` CLI (`.phyxel/config.json` +
`.mcp.json` → `phyxel-mcp`); see `docs/GameDevWorkflow.md`. Don't assume 8090 / don't kill other
sessions' engines.
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

**Standalone test API (`--test`):** a packaged game can optionally host the HTTP API so an automated
harness (the game-production runtime validators / adversarial playtest) drives + observes the **real
shipped build** — not the editor proxy. Run `MyGame.exe --test [port]` (default off; localhost bind;
dev/test builds only — never ship it enabled). Implemented by `Core::GameApiService` (hosted by
`GameShell`, gated on `EngineConfig::testApiEnabled`), which serves the harness subset (`/api/state`,
`inject_input`, `get_screen_state`, `fire_trigger`, `navgrid_*`, `ui_click`, `engine_timing`, …) against
the game's own subsystems. Unlike the editor host, the standalone owns a real `GameScreen`/`ScreenState`,
so win/lose validation can reach genuine L4. Design: `docs/game-production/README.md` §6.6.

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
external/        # Third-party: stb (vendored image/font headers), glfw, glm, imgui, goose, blocksmith, miniaudio, sqlite3
docs/            # Documentation
```

## Key Engine Subsystems

**Core:** ChunkManager (32³ chunks, Vulkan buffers, face culling), ChunkStreamingManager (SQLite persistence), WorldGenerator, SceneManager (multi-scene transitions), EntityRegistry (O(1) lookup), EngineAPIServer (HTTP port 8090; hosted by the editor, and — via `GameApiService`/`GameShell` behind `--test` — optionally by a standalone game), ScriptingSystem (pybind11), PhysicsWorld (thin wrapper over the custom CPU `VoxelDynamicsWorld` — Bullet removed), RenderPipeline/RenderCoordinator (Vulkan instanced)

**Gameplay:** HealthComponent, RespawnSystem, CombatSystem (sphere+cone hit detection), EquipmentSystem (6 slots), CraftingSystem, DayNightCycle, HazardSystem, AchievementSystem, MusicPlaylist, PlayerProfile, ObjectiveTracker

**NPC/AI:** NPCManager, NavGrid + AStarPathfinder, DialogueSystem, StoryEngine, AIConversationService (Claude/OpenAI/Ollama), BehaviorTree/UtilityAI

**Voxel Physics (NO Bullet — removed):** GpuParticlePhysics (Vulkan compute XPBD/AVBD, ~10000 cap; the LIVE primary path for large-scale debris/destruction, warm-started), VoxelDynamicsWorld (custom CPU sequential-impulse rigid-body world; furniture, static-terrain occupancy grids + character grounding, and the left-click break-debris path via DynamicObjectManager/DebrisSystem), VoxelManipulationSystem (break routing — see `docs/DynamicVoxelPhysics.md`). Static voxel collision = per-chunk occupancy grids registered in VoxelDynamicsWorld; see `docs/AgentContext.md` for the "every DB-load path must call buildAllChunkPhysics()" rule.

**D&D RPG:** DiceSystem, CharacterAttributes (6 ability scores + modifiers), ProficiencySystem, CharacterSheet/CharacterProgression (class/race/XP/level, data-driven JSON in `resources/rpg/`), ActionEconomy/InitiativeTracker, AttackResolver/ConditionSystem (15 conditions), SpellDefinition/SpellcasterComponent/SpellResolver, RpgItem/CurrencySystem/AttunementSystem/EncumbranceSystem, ReputationSystem/DialogueSkillCheck/SocialInteractionResolver, RestSystem, WorldClock (360-day calendar, lunar cycle), Party, LootTable, EncounterBuilder, CampaignJournal

## Common Patterns

- **Namespace**: `Phyxel::Core`, `Phyxel::Physics`, `Phyxel::Graphics`, `Phyxel::Scene`, `Phyxel::UI`, `Phyxel::Input`, `Phyxel::Utils`
- **Logging**: `LOG_INFO("Tag", "message")`, `LOG_DEBUG`, `LOG_WARN`, `LOG_ERROR`, `LOG_TRACE_FMT`
- **Entity registration**: `registry.registerEntity(entity, "my_id", "type_tag")`
- **Voxel placement**: `chunkManager->addCube(worldX, worldY, worldZ)` or `addCubeWithMaterial(x, y, z, "Stone")`

## Testing

```powershell
# Unit tests (~2,300)
.\build\tests\Debug\phyxel_tests.exe --gtest_brief=1

# Integration tests (47)
.\build\tests\integration\Debug\phyxel_integration_tests.exe --gtest_brief=1

# Or use the build script
./build_and_test.ps1 -Config Debug -RunTests
```

Manual standalone game testing: `samples/game_definitions/testing_baseline.json` + checklist in `docs/StandaloneGameTesting.md`.

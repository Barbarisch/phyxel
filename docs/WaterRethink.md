# Water Rethink — full stock-take and the v4 plan (2026-10-07)

> **Status: PLAN, nothing built.** `docs/Water.md` remains the current-state reference for the
> code as it exists today (layers, constants, traps, history). This document is the honest audit of
> that code against the goal, the diagnosis of why four months of work did not hit the mark, and the
> work-package plan that replaces Water.md §6 "Open work" as the ordering of record.
>
> Audit method: full read of `docs/Water.md` + the four water memory files, then three parallel code
> inventories (simulation/data · rendering · API/config/tests/docs) over every file that mentions
> water. Every claim below has a `file:line` or a measured number behind it in those inventories;
> items marked **[derived]** are a reading of the code, not something seen on screen.

---

## 0. The brief (user, 2026-10-07, paraphrased closely)

Think about water as **several independent aspects, each across many scenarios**:

1. **Functionality and physics are the primary concern.** Water must *flow*, have *volume*, and
   *inhabit the world*. Large bodies obviously bound how physical this can be.
2. **Appearance.** Oceans range from still and calm to rough with waves. Water must look
   *physically present at shorelines* — waves that *collide with the voxels*.
3. **Take full stock of every bit of water logic.** Up to now we have failed to hit the mark.
4. **Divide the work into manageable pieces.**
5. **Where appearance applies, and how, depends on the size of the body of water and the weather.**
6. **For flowing water, find what the host can simulate** with a realistic feel without setting the
   GPU on fire.

The earlier standing directives (Water.md §1 — water only in terrain basins, basin-first, no floating
water, camera-independent existence) all still hold; nothing here relaxes them.

---

## 1. Stock-take — what exists today

### 1.1 Fourteen answers to "where is the water?"

| # | Representation | Resolution | Who writes | Who reads | Persisted |
|---|---|---|---|---|---|
| A | **Chunk water spans** `Chunk::WaterSpanLocal` | 1 voxel column | `WorldGenerator::generateChunk` (lakes/ocean only), `water_ground_sync` | span render grid (L), `rebuildGroundedWaterFromSpans` | **yes** (ChunkBlobCodec v2) |
| B | Coarse hydrology bake `HydrologyMap` | 128 m cells, 256² (~32 km) | generator (seed+recipe) | span flood seeds, sim table, flora/fauna gates, WorldForge, look texture G/B/A | no (re-derived) |
| C | `FlowField` river network | 128 m cells + channel segments | generator | carve, `channelHitAt`, sim river pins, kinematic current | no |
| D | `WaterBodyIndex` (Ocean/Lake/Pond) | 128 m cells | generator | body query (**every bake body forced infinite**, Application.cpp:6592), look texture | no |
| E | Fine ponds `finePondsForCell` | sub-cell, 64² window | generator (FIFO 256) | sim finite bodies **inside the window only** | deltas only (I) |
| F | **CA mass field** `WaterSimulation` | 1 voxel, 64×32×64 camera window | sim | `sampleWater`, `columnWater`, `flowAtWorld`, `rebuildSurface` | no |
| G | Implicit flat sea `m_seaLevel` | scalar | game.json | `sampleWater` out of window when no table | game.json |
| H | Column overrides (poured water) | 1 column, cap 65 536 | recenter capture | `applyOverrides` | **yes** (`world_meta.water_overrides`) |
| I | Finite-body deltas | per body | `scoopWater` | `applyFiniteBodies`, `sampleWater` | yes (same key) |
| J | Edge-outflow bank | 1 column, cap 4.0 | window-edge bleed | `applyOutflowBank` | yes (same key) |
| K | Authored seeds / springs / channels | world lists | game.json, API | `rebuildOcean` | game.json via `water_save` |
| L | Span/grounded **render grid** (RGBA32F) | 1 column, clamp 2048² | from A | water shaders | no |
| M | Debris water tiles | 32×32 per chunk column, ≤256 | `DebrisRuntime::updateWater` from `columnWater` | `solver_integrate.comp` | no |
| N | `RippleField` | 128² × 0.5 u | `addRipple` | cell shader only | no |

Nine of these are opinions about *existence*; at rest they are supposed to agree and nothing checks
that they do except the manual camera-walk probe. **The sim (F) and every entity query never read
the spans (A)** — out of the window `sampleWater` falls back to the 128 m bake (B), so a fine pond
reads DRY the moment the camera leaves (WaterManager.cpp:1192). Voxel edits update the sim's solid
mask but **never the spans** — the only post-generation span writer is `water_ground_sync`.

### 1.2 Rivers are invisible in baked worlds

Rivers and creeks are **never written into spans** (`waterSpansForBlock` seeds only from bake lake/
ocean levels, WorldGenerator.cpp:808-877). They exist only as pinned CA cells, and cell rendering is
**disabled wherever a baked table is bound** (WaterManager.cpp:213, the 2026-08-04 camera-invariant
deletion). Net effect, confirmed by both inventories: in every streaming world, river carves render
dry, and splashes, pours and waterfall mist are invisible. Rivers "work" only in the sense that
`water_find_river` finds them and a crate drifts in the gorge.

### 1.3 The motion layer has no motion

`WaterSimulation` is a mass-conserving diffusion CA: gravity split, horizontal `(Δm)×0.25`
equalisation, upward pressure, evaporation, MIN_HOLD 0.3. **There is no velocity field and nothing
is advected** — `m_flow` is a smoothed net-transfer proxy ("NOT a velocity", WaterSimulation.h:96),
and "momentum" only re-weights which neighbour receives mass (dissipative by design, rate < 0.5).
Rivers are **pinned full end-to-end**, so the CA moves nothing in them; their current is a
kinematic axis-aligned direction baked per 128 m cell (`flowDirAt`, 4-connected; the comment
claiming 8-neighbourhood is wrong, FlowField.cpp:237). Nothing floats downstream except by the
hand-written `kinematicRiverFlow` push.

The GPU port (`water_flow.comp`) is compiled and allocated at boot but **opt-in off**, lacks
MIN_HOLD / momentum / floors / outflow / sleep, uses K=0.1 vs CPU 0.25, submits-and-waits every
step and calls `wake()` every step so the field never settles. It is a dead end as written.

Measured: settled ~0.002 ms/frame, active step ~150-220 µs (Release). 256×32×256 ran at 19 FPS and
never settled (Water.md §7 #1). `Update/Water` produced 6 of the 10 worst frame spikes in the
2026-09 perf program (full-window solidity re-read on recentre, PerfProgram2026-09.md:1391).

### 1.4 Appearance: one shading file, three draws, four regimes smeared into one

- **Three draws** in a dedicated post-scene water pass (colour LOAD, depth read-only, half-res
  refraction copy): sea clipmap sheet (`SeaMesh`, 64² cells/level, up to 7 levels at the editor's
  4096 render distance ≈ 46k tris **[derived]**), per-cell instanced surface (≤100k instances, one
  host-coherent buffer re-memcpy'd per draw, not per-frame-in-flight), fullscreen underwater fog.
- **One model** in `water_common.glsl` (the design invariant worth keeping): Pope-&-Fry extinction,
  turbidity endpoint, Schlick Fresnel, 24-step SSR (sea only), 4 Gerstner components (steepness Σ
  0.87, per-component Nyquist fade), 4 detail-normal octaves, flow-map cycle, McCowan surf band,
  rim foam, whitecap dead zone. No caustics anywhere. Sky colours are hardcoded day/night constants —
  the atmosphere UBO's haze and moon fields are unused.
- **Sea state is a constant**: amplitude 0.45 / wavelength 14 / wind 0.6 rad / 6.7 m s⁻¹, changed only
  by `water_waves`. Per-body energy/roughness come from `WaterProfile` (SMB fetch, Cox-Munk) baked
  into the texture. **`WindSystem` (the engine's global wind) is not connected to water** — grass
  bends in one wind, the sea rolls in another. There is **no weather system**; "rain/storm" exists
  only in comments.
- **Shader time** is a wall clock started at pipeline construction (ignores pause and game time).
- **Camera dependence that still exists**: clipmap vertex spacing (4 u core → 256 u outer) point-
  samples the level texture, so a lake narrower than the local spacing gets the sea-level fallback
  and is rim-killed — **small lakes vanish with distance [derived]**; Nyquist fade keys on camera
  distance, so wave amplitude at a fixed world point changes as you approach.
- **Override hazard [derived]**: `setWaterLook`, `setWindSpeed` and a wind-direction change in
  `setWaves` reset `m_lastHydroUploaded`; the next frame re-uploads the **defective 128 m bake**
  (606/606 rim leaks) as placement and `updateSpanWaterGrid` will not rebuild until the chunk count
  changes. In an editor world the same reset drops back to flat-sea mode.
- **Far field**: beyond chunk residency there is no water at all — far-terrain tiles (tier 4, ON)
  have zero water code, so a coastline floats over a dry seabed and the horizon has no ocean.
  LodTierLedger.md:35 "No far tier exists for: water".
- Open visual defects on record: Gerstner pull-away from walls (one fix made it worse, reverted);
  shallow-water foam quilt (mitigated, real fix = shore field); 128 u stepped lake edges whenever the
  bake becomes placement again; underwater fog is single-alpha; flow smoothness never assessed.

### 1.5 Coupling (the part that mostly works)

`sampleWater`/`submergedFraction` is the one shared query. CPU rigid bodies get buoyancy (default
1.6), drag, current coupling, never sleep wet; GPU debris gets the same law via tiles (Phase 6c,
2026-10-07); player and NPCs get wading slowdown, current push, stride ripples, entry splash; camera
submergence drives the underwater pass. **No consumer of spans exists for physics or gameplay.**
Swimming is user-deferred.

### 1.6 Surface area: API, config, tests, testbeds, docs

- **30 debug commands**, all `POST /api/debug/water_*`-style, handlers in `Application.cpp:12709-13467`.
  **Zero MCP tools expose water.** Nothing under `/api/world/*`. No `tools/*.py` drives water.
- game.json `water` block: `enabled`, `seaLevel`, `bakedTable`, `oceanBoundary`, `evaporation`,
  `edgeOutflow`, `oceanSeeds`, `springs`, `channels`. `seaLevel` on an existing world is **ignored**
  (stored recipe wins, WARN only). No wind, no weather, no sea-state field.
- **216 unit tests** across 14 files, none disabled. Two `WaterOccupancyTest` cases cost **411 s +
  428 s** in Debug and double the whole unit run; cause unestablished (column-cache size was ruled
  out). No integration/stress/e2e water tests.
- **Testbeds contradict the doc.** Water.md §5 names `PhyxelProjects/WaterTest` as THE water world —
  **it does not exist on disk.** The three labs it says were deleted (WaterLab, CreekLab, RiverLab)
  **still exist with water enabled.** The SoundSystemV2 L4 evidence cites WaterTest, so it existed
  once and was lost or moved.
- Stale docs: `AgentContext.md:1398-1441` describes a fixed region and a short route list;
  `WorldLookBacklog.md` §B1/B2 (endless ocean under the world; bake upload bypasses the shoreline
  snap) are confirmed and unfixed; `StructurePipelineGaps.md:316` big rivers (>96 u) get no bridge
  deck; wells and fountains hold no water (FunctionalWiringBacklog.md:161).

---

## 2. Why we missed — root causes, not symptoms

1. **Existence, motion and look were never separated in the code**, only in the doc. Water was
   first *drawn from the sim*, so the renderer inherited the sim's camera window (three shipped
   camera-invariant violations). The fix deleted cell rendering in baked worlds, which took rivers
   and splashes with it. Each layer must have its own truth and its own test.
2. **The motion engine cannot produce the brief.** A diffusion CA has no inertia, so it cannot
   slosh, pile against a wall, carry a crate, or make a wave break — the behaviours the user named.
   Rivers had to be faked as pinned reservoirs because the CA cannot transport mass down a slope at
   a steady rate. Momentum was bolted on as a bias and stayed dissipative.
3. **Three generations of placement coexist** (flat sea · bake · spans) with implicit precedence
   rules in `RenderCoordinator`; a look tweak can silently flip which one is live.
4. **Look work was steered by metrics** (Nyquist, perf, grounding) and regressed visibly — the user
   had to call it ("this looks like shit", 2026-07-28). Reference-screenshot discipline exists in the
   doc but not in the tooling.
5. **No regime model.** One Gerstner constant set serves a pond, a mountain lake and an ocean in a
   storm; appearance never consulted body size or weather because neither is a first-class input.
6. **No far tier.** Water stops where chunks stop, so no scene ever reads as a *world* with water.
7. **Verification surface is thin and manual**: no MCP tools, the camera-walk probe is prose,
   the named testbed is gone, the slowest tests in the repo are water tests nobody can run.

---

## 3. The frame — three concerns, one regime matrix

Each concern below has its **own truth, its own tests, and talks to the others through one
interface**. That is the structural change; everything in §4 follows from it.

| Concern | Truth | Interface out | Must never |
|---|---|---|---|
| **A. Existence & volume** (world data) | per-column **water spans** in chunks, generation-derived, edit-updated, persisted | `waterSurfaceAt(x,z)`, `waterSpansAt(x,z)`; far tiles derived from the same generator query | depend on camera, sim window or residency for *whether* |
| **B. Motion** (simulation) | a bounded **active region** seeded from spans, writing settled state back to spans | surface height + velocity field; sources/sinks; `sampleWater` inside the region | be the renderer's source of existence; run where nothing moves |
| **C. Appearance** (rendering) | `water_common.glsl` + a **regime table** keyed by body class × distance band × weather | one shading model over three geometry sources (far tiles · span sheet · active-region heightfield) | choose a look per chunk; let placement follow the camera |
| **D. Coupling** | `sampleWater` / `flowAt` | buoyancy, wading, debris, sound, wetness | read anything but A and B |
| **E. Weather driver** | one `WeatherState` (wind from `WindSystem`, sea-state, precipitation) | per-body `WaterProfile`, Gerstner params, vegetation, sky | be constants in a render pipeline |
| **F. Verification** | WaterBench worlds + automated probes + GPU timing gates | red-before-green evidence in `docs/evidence/` | be prose |

### The regime matrix (what applies where)

**Body class** (from `WaterBodyIndex` + spans + channel order): Ocean (infinite boundary) · Lake
(≥ 4 bake cells, pinned, optional slow recharge) · Pond (finite, conserved) · River (order ≥ 3,
carved, transporting) · Creek (order 1-2, fractional) · Transient (pour, spill, splash, waterfall).

**Distance band** (resolution only, never existence): **Near** = inside the active sim region,
surface from the simulation heightfield · **Mid** = resident chunks, surface from spans ·
**Far** = beyond residency, surface from far-water tiles derived from the generator span query.

**Weather** (from `WeatherState`): Calm · Breeze · Fresh · Gale · Storm, mapped to wind speed U and
hence to fetch-limited wave energy per body (SMB already implemented in `WaterProfile`).

| | Near (sim) | Mid (spans) | Far (tiles) |
|---|---|---|---|
| **Ocean** | simulated heightfield driven at its boundary by the analytic swell → real reflection, run-up, pile-up at walls; spray VFX at breakers | Gerstner swell on the span sheet, surf band from the shore field, whitecaps by U | Gerstner long components only on far tiles, horizon fog |
| **Lake** | as ocean but fetch-limited (small lake ≈ ripples only) | fetch-limited chop, no surf band below `breakDepth` floor | flat tile at level, no waves |
| **Pond** | ripples + sim slosh, finite mass | flat + RippleField | flat tile (or nothing below tile resolution — cost bound, not look) |
| **River** | simulated flow down the carved bed: velocity, standing waves at obstacles, foam at drops | flow-map ripples along `channelFlowDir`, bank foam | ribbon tile along the channel (spans give the sloped top) |
| **Creek** | fractional-depth flow on the shelf | flow-map only | not drawn (below tile resolution) |
| **Transient** | sim only (pours, waterfalls, spills) + splash particles | written back to spans when settled → becomes Pond/Puddle | — |

Weather scales the **Ocean/Lake** rows only (Calm collapses them to the Pond row; Storm raises
amplitude to the SMB fully-developed cap and whitecap coverage by Monahan `3.84e-6·U^3.41`, which is
grounded in Water.md §4 but not yet in code). Rivers and creeks respond to weather only through
discharge (a later knob), never through swell.

---

## 4. Work packages

Each package states its **contract**, **required validation layer** (L1 artifact · L2 structural
invariant on real output · L3 functional simulation · L4 live engine), the **red test**, the
**stress axis**, the **perf gate**, and the **testbed**. Dependencies are explicit; packages marked
∥ can run in parallel with their neighbours. Nothing ships without the Stop-hook discipline.

### WP0 — Measure, baseline, and tooling (prerequisite for everything)

**Contract:** we can state the current cost and look of water in numbers and pictures, and can
detect a camera-existence violation automatically.

- **Step 0 — there is currently NO water test world (2026-10-07).** The old labs are deleted and
  `WaterTest` was already gone. **The first deliverable of WP0, before any measurement, is
  creating the three WaterBench projects** (`tools/create_project.py` / `create_project` MCP, then
  generate, carve the Basin rig with `fill_region`/`clear_region`, pin camera poses in each
  game.json, `save_world`, and record each world's `water_bake_info` + measured `surface_y` at
  spawn in `docs/evidence/water_v4_benches.json`). A bench is done when a cold `launch_engine
  --project` reaches `api_responsive`, `get_camera` returns the pinned pose, and `water_spans_stored`
  on its documented rect returns the predicted count. Nothing else in WP0–WP9 runs without them.
- **WaterBench worlds** (brand new, small, one variable each — decision 4; full spec in §6):
  `WaterBench_Basin` (Flat 4×4 chunks, no bake, carved basin + channel + ramp + wall — the SWE and
  shoreline rig), `WaterBench_Coast` (streaming Perlin, the old `WaterTest` recipe seed 20260804,
  shore at (165,16,890)), `WaterBench_River` (Mountains seed 20260729, regenerated). The old labs
  are already deleted.
- **GPU timing gate**: `GET /api/debug/gpu_timing` already records scopes `WaterRefractCapture`,
  `Water`, `WaterCells`, `WaterUnderwater` — add `WaterSim` and `WaterFarTiles` later. Record Release
  medians at three fixed vantages per bench (open sea · shore · river gorge), n ≥ 30, into
  `docs/evidence/water_v4_baseline.jsonl`. This is the number every later package is gated on.
- **Camera-walk probe as a tool**: `tools/water_camera_probe.py` — for a fixed rect, read
  `water_spans_stored` (camera-independent) and a rendered wet/dry mask (`screenshot` + pixel class,
  or a new `water_render_mask` debug route that returns the span grid's wet set as the renderer saw
  it) from a far and a near vantage; diff; non-zero = fail. Runs in `build_and_test.ps1`
  when a bench is available.
- **Reference captures** at the same vantages (look-first rule) — the "is it prettier than this"
  baseline.
- **Fix the two 14-minute tests**: measure column-cache hit rate during
  `GeneratedChunksHoldTheirWaterSpans`; the suspect is the ring-walk's locality. Target < 10 s each
  or move them to the stress suite behind a flag. **No lowering of `kWaterExtentSteps`.**
- **API hygiene start**: expose `water_stats`, `water_probe`, `water_spans_stored`, `water_find_river`,
  `water_validate`, `water_ripple`, `place_water`, `water_scoop` as MCP tools (today: none).

Validation: L4 for the probe (must catch a deliberately injected violation — set
`water_cell_render 1` in a baked world is a known violation; the tool must flag it). Perf: n/a.
Stress: probe over a 1 km rect.

### WP1 — One truth: spans everywhere (Concern A)

**Contract:** every column's water existence and surface come from chunk spans; every other
representation either derives from spans or is deleted.

1. **River and creek spans at generation.** `waterSpansForBlock` consults `channelHitAt` and emits
   a span per river/creek column with top = bed + channel depth (sloped along the carve, so the
   per-column representation holds a sloping channel — this is what the flat 128 m grid could not
   do). Creeks get fractional tops on the ⅔ shelf. Red test: a known order-3 column in
   `WaterBench_River` holds zero spans today.
2. **Fine-pond spans at generation** (`finePondsForCell` → spans), with a body id so finiteness
   survives.
3. **Edits update spans.** Break/place/blast → re-flood the affected column stack locally
   (`buildOpenWaterSpan` against the new surface, bounded) and mark the chunk dirty. Red test: dig
   below a lake rim, `water_spans_stored` still reports the old top.
4. **Queries read spans.** `sampleWater`/`columnWater` out of the active region read spans, not the
   bake. Fine ponds stop reading DRY when the camera leaves.
5. **Retire** the implicit flat sea (`invCellSize == 0`), the bake-as-placement upload path, and the
   `m_lastHydroUploaded` reset hazards. Authored (un-baked) worlds get spans from `water_ground_sync`
   at save time and at generation for Flat worlds with `water.enabled` (sea level fill of open
   columns ≤ seaLevel is just `buildOpenWaterSpan`).
6. **Span grid rebuild** keys on residency *content* (chunk set hash), not `chunkMap.size()`, and
   the `vkDeviceWaitIdle` per rebuild is measured and replaced by a per-frame-in-flight upload.

Validation: L2 — `water_validate` rim leaks **0** on the WaterBench_Coast shore rect (today's red:
257/257) and on a river rect; L4 — camera probe passes at coast, lake, river, pond. Stress: a
1024×1024 column rect; 10 000 edits in a loop with span count tracked (no silent drop, no growth).
Perf gate: generation cost per chunk with river spans ≤ +15 % over today's measured 7.9 ms/chunk-eq.

### WP2 — Far water tiles (Concern A/C, ∥ with WP1 after step 1) 

**Contract:** water is visible to the horizon exactly where the generator says it is, identical
from every vantage.

- `FarTerrainMesher` gains a water surface per tile column from the generator's span query
  (same private generator copy that already stamps roads), emitting a second mesh per tile at the
  water top; rivers become ribbons where tile step ≤ channel half-width (cost bound: below that
  they are simply not drawn — a resolution rule, not an existence rule).
- Rendered by the sea sheet pipeline's shading (same `water_common.glsl`), long-wavelength Gerstner
  components only, fog-faded like terrain tiles. New LodTierLedger row (tier 9).
- Ocean horizon: tiles at sea level wherever the generator's coarse model is below sea level.

Validation: L4 camera probe far↔near agree; the tile wet set equals the span wet set at tile
resolution (L2, `FarTerrainMesherTest.WaterShowsInFarTiles`, same shape as the roads test). Perf
gate: far-water draw ≤ 0.3 ms GPU at the coast vantage.

### WP3 — Flow engine decision: GPU shallow-water prototype (Concern B)

**Contract:** decide, by measurement on `WaterBench_Basin`, the motion engine for v4.

**Recommendation to prototype:** a GPU **2.5-D shallow-water heightfield** over the span surface
(virtual-pipes / staggered-grid flux formulation — O'Brien & Hodgins 1995; Mei, Decaudin & Hu 2007;
Chentanez & Müller 2010 for the SWE + particle hybrid). Per column: height h, flux on 4 faces (or
2-D velocity). It has **inertia**: water sloshes, piles against a wall, reflects, runs down a bed at
a steady rate, carries bodies by its velocity field, and reproduces wave reflection off voxels
*physically* — the shoreline brief falls out of the sim instead of being painted. It is the model
every real-time game water system uses at scale, it maps one-thread-per-column, and it is natively
the same per-column shape as the spans (A), so seed and write-back are trivial.

**What it cannot do and how we cover it:** fully 3-D cases (a tunnel under terrain, water pouring
through a hole into a cavity, a cave lake under a lid). Options, to be chosen in this WP: (a) layered
columns — spans are already multi-run, so a bounded number of stacked heightfield layers per column
handles overhangs and pours; (b) keep the existing 3-D CA as a *local* solver for a small 3-D event
box only; (c) accept the limit for v4 and log it. Splash, spray, waterfall mist stay particles
(VFX), per the standing "particles = splash only" rule.

**Prototype deliverables:** `water_swe.comp` (flux pass, height pass, boundary pass) over a
512×512 column region with `vkCmdDispatch` in the frame's compute slot (no submit-and-wait, no
per-step readback; a staged readback every N ticks for queries), ping-pong buffers, 30 Hz fixed
step with interpolation. Scenarios with falsifiable predictions: dam break (front speed `√(g·h)`
within 10 %), basin fill to spill level (mass conserved to 1e-4), channel flow (steady discharge),
wall slosh (reflection, decays), pour from above (source). Compare to the CA on the same rig.

**Budget framing (what the host can handle).** Host: RTX 4090 / Ryzen 3950X / 64 GB — the top
tier, so the real question is what *scales down*. Hypotheses to be measured, not assumed: a
512² region = 262 k columns × 3 passes at 30 Hz ≈ 0.1-0.3 ms GPU on this card; 1024² ≈ 1 ms. The
CPU CA's whole 64×32×64 window is 131 k cells for ~0.2 ms *active* on one core. **Target budget for
all water (sim + every draw, SSR excluded)** ≤ 1.5 ms GPU at 1440p on the 4090, with quality tiers
`low` (256² region, no SSR, 2 Gerstner components), `medium` (512², SSR 12 steps), `high` (1024²,
SSR 24, spray). Tiers bound *cost* only: the region size changes where motion is simulated, never
where water exists (spans). Measured per-tier numbers go in the baseline file.

Validation: L3 (predictions above hit on the bench), L2 conservation, perf per tier. **Decision
gate:** the user picks SWE / layered SWE / CA-GPU-done-properly from the measured table. Nothing
in WP4 starts before.

### WP4 — Motion integration (Concern B, after WP1 + WP3)

**Contract:** the chosen engine runs in a camera-following *active region* seeded from spans,
writes settled columns back to spans, and is the only source of water motion.

- Region lifecycle: seed from spans on (re)centre; boundary columns held at span level (the ocean
  pin generalised); settled columns (|Δh| < ε for N ticks) write back to spans and go inactive;
  region shrinks to the active set (the window gets *smaller* once rendering reads world data —
  Water.md §7 #1, finally honoured).
- Sources/sinks: springs, river inflow at the region's upstream boundary (discharge by Strahler
  order — a grounded table, Leopold & Maddock hydraulic geometry already cited for width/depth),
  ocean/lake boundary, evaporation for films, finite-body accounting via spans + body id (replaces
  H/I/J overrides, bank, deltas — all three stores retire into spans).
- Voxel edits → solid mask + span re-flood (WP1 step 3) → the region wakes locally.
- Velocity field → `flowAt` for rigid bodies, debris tiles, characters: things **drift downstream
  because the water moves**, not because of `kinematicRiverFlow` (delete it).
- Rivers: no longer pinned reservoirs — the bed carries a steady flow between inflow and outflow
  boundaries; settled rivers write back their steady surface as spans so they exist and render
  outside the region.
- Persistence: spans already persist; `water_overrides` key is migrated once and removed.

Validation: L3 bench predictions survive integration; L4 on WaterBench_River: a crate placed in
the gorge moves downstream at the sim's velocity (today's L4 was 22 u on a kinematic push); L4 on
WaterBench_Coast: dig a channel from the sea into a dry basin and watch it fill to sea level and
stop; a pour into a basin becomes a persistent pond across restart with mass within 1 %. Stress:
10 000 edits; region recentre ×1000 with total span mass tracked; a 300 u river reach fully active.
Perf gate: per-tier budget from WP3; `Update/Water` CPU spike ≤ 1 ms (today's recentre spikes).

### WP5 — Shorelines that collide (Concern C, after WP4)

**Contract:** at any shore inside the active region, waves arrive from the analytic far field,
shoal, break, run up the voxels and slop against walls because the sim does it; in the mid band the
same look is approximated from a shore field.

- **Boundary coupling**: the analytic Gerstner field is the Dirichlet/flux boundary of the active
  region on open-water edges, so swell *enters* the sim and meets the voxels physically.
  Prediction: run-up height on a 1:10 slope within 20 % of Hunt's formula; standing-wave antinode
  at a vertical wall ≈ 2× incident amplitude.
- **Shore field** (mid band): depth / shore distance / shore direction / cliffiness at 2 m cells
  over a 512 m window, built from spans + terrain (designed in the retired `WaterPhysicalFeelPlan`,
  recover from git); drives refraction toward shore (Snell), shoaling (Green's law), breaker band
  (McCowan, already in), spray VFX at breakers (reuse the waterfall-mist pattern, cap 48). The
  run-up **wet band on the beach material is deferred** (decision 5) — run-up is geometry and foam
  only in v4.
- **The pull-away defect** is retired by construction near-field (the sim surface does not slide
  off walls) and bounded mid-field by damping the Gerstner horizontal displacement with the shore
  field's wall proximity — judged by the look-first A/B, not a metric (Water.md §7 #6).
- Delete the "cheap stand-in" depth dither once the shore field exists.

Validation: L3 predictions above on `WaterBench_Coast` with a built 1:10 beach and a vertical
quay; L4 same-vantage A/B against the WP0 reference captures, user sign-off. Perf gate: shore field
build ≤ 2 ms amortised per 64 u of camera travel; no change to the water GPU budget tier.

### WP6 — Weather driver (Concern E, ∥ with WP4/5)

**Contract:** one `WeatherState` drives water, vegetation and sky; sea state is a function of it
and of body size, never a pipeline constant.

- `WindSystem` becomes the single wind source: water's wind direction/speed read its `State` (unify
  the 15° / 1.0 vegetation defaults with the sea's 0.6 rad / 6.7 m s⁻¹ via one calibration — the
  `Settings.speed 0..2` scale maps to U in m s⁻¹ with the Beaufort table, grounded).
- `WeatherState {wind, seaStateOverride?, precipitation, storminess}` with slow drift (reuse
  `WindSystem` noise), game.json `weather` block (defaults = today's look), `POST /api/weather`,
  persisted in the recipe; day/night can modulate it.
- `WaterProfile` recomputes per-body energy/roughness from U on change (SMB fetch is already there;
  add Monahan whitecap coverage to the shader as the storm knob); the regime matrix row for
  Ocean/Lake is a table in `resources/water_regimes.json`, not constants.
- Rain: **out of v4** (decision 6). Listed as a follow-on: ripple density on surfaces plus a sky/fog
  hook.

Validation: L2 — the same lake under Calm and Gale differs by the predicted SMB ratio in the
profile; L4 — one frame, two bodies, both respond; vegetation and sea agree on wind direction (probe
both). Stress: U sweep 0→30 m s⁻¹, steepness Σ ≤ 1 holds at every step (red test on the clamp).

### WP7 — Appearance regime pass (Concern C, after WP2/WP6)

**Contract:** the regime matrix is what the renderer does; every body class × band × weather cell
has a reference capture.

- Per-class look: ponds/creeks lose surf and swell entirely; rivers get bank foam + obstacle wakes
  from the velocity field (WP4) and flow-map ripples in the mid band; lakes fetch-limited.
- Fix the small-lake-vanishes-with-distance defect: the sheet samples the span grid with a
  conservative (max-over-footprint) fetch at coarse levels, or lakes below spacing hand off to tiles
  (cost rule, same answer every vantage).
- Shading debts: sky/haze/moon from the atmosphere UBO (night water currently reflects a hardcoded
  sky); caustics (projected, span-depth-gated, cheap); SSR roughness-aware blur and a tiered step
  count; underwater per-channel extinction; pause-aware shader time (game clock); delete the planar
  mirror descriptor that `render()` still refuses without; per-frame-in-flight cell instance buffer
  (today one host-coherent buffer is rewritten under the GPU).
- Water-vs-OIT ordering tested once and recorded.

Validation: L4 look-first A/B per matrix cell at fixed vantages; the WP0 GPU gate per tier.
Stress: 50 bodies of mixed class in frame (the span grid clamp 2048² and tile fallback must agree).

### WP8 — Coupling and gameplay (Concern D, after WP4)

- Swimming (user-deferred — list it, do not build until asked). NPC water hooks parity already
  closed; verify under WP4's velocity. Items and debris drift by the real velocity (WP4). Sound:
  water ambience beds and splash cues from the active region (SoundSystemV2 already names them).
  Wetness on characters and ground (shader sign-off). Wells/fountains hold water (spans in
  structures — the pier rule generalised: structure placers emit or clear spans explicitly).

Validation: L4 per feature on the benches; stress: 100 NPCs wading, 1000 debris in a river.

### WP9 — API, MCP and docs (Concern F, continuous; final consolidation after WP7)

- Collapse the 30 `water_*` debug commands into a deliberate surface: `/api/water/{state, probe,
  spans, bodies, rivers, validate, place, scoop, ripple, sim, look, weather}` with the
  omitted-means-unchanged convention and echoed resulting state; keep old routes as aliases for one
  release. MCP tools for each. `water.*` game.json fields documented with units.
- Water.md becomes the v4 current-state doc (rewrite §2-§4 when WP4 lands); this plan's §4 is the
  ledger until then. Fix the stale AgentContext block, WorldLookBacklog §B, LodTierLedger water
  rows, StructurePipelineGaps river items at the package that closes each.

---

## 5. Order and parallelism

```
WP0 measure/tooling ─┬─► WP1 spans everywhere ─┬─► WP2 far tiles ──────────┐
                     │                         │                           ├─► WP7 regime pass ─► WP9 consolidate
                     └─► WP3 SWE prototype ────┴─► WP4 motion integration ─┼─► WP5 shorelines ──┘
                                                                           └─► WP8 coupling
                               WP6 weather driver runs ∥ from WP3 onward
```

WP0 and WP3 start immediately and independently (WP3's bench is the no-bake Flat basin, so it does
not wait for span work). WP1 is the longest pole and the one that makes rivers *exist*; WP2 is
the cheapest visible win ("the world has an ocean"). WP4 is the decision-gated core. WP5 is the
headline visual the brief asks for and deliberately sits *after* motion exists, because the honest
way to make waves collide with voxels is to simulate it.

Each package is one to three sessions. Every package ends with: red test shown failing → green,
evidence file, camera probe run, GPU gate recorded, Water.md delta, memory note.

---

## 6. Decisions — SETTLED by the user 2026-10-07

1. **Motion engine: 2.5-D GPU shallow water.** Layered columns only if the WP3 bench surfaces a
   3-D case that matters in v4. The GPU-CA option is closed. WP3 still runs: its job is now the
   cost table, the tier sizes and the layered-or-not call, not the engine choice.
2. **Big lakes: slowly recharging, conserved.** Drained lakes visibly drop and recover over time
   (rate = a grounded knob in the regime table). Ponds strictly conserved, ocean a boundary
   condition — unchanged.
3. **Quality tiers from the start:** `low` / `medium` / `high` as defined in WP3. Tiers bound cost
   only (active-region size, SSR steps, spray); existence never varies by tier.
4. **Test worlds: brand new.** WaterLab, CreekLab and RiverLab were **deleted from disk
   2026-10-07** (their recipes are recorded under WP0 below for reuse). WP0 creates three fresh
   WaterBench projects on the current engine.
5. **Shore wetness (`voxel.frag` wet band): deferred — nice-to-have, not v4.** WP5 ships foam,
   spray and run-up geometry only; the rock stays dry-looking. Logged, not planned.
6. **Weather scope v4: wind + sea state only** (agent's call, offered by the user). Rain stays a
   listed follow-on; WP6 drops its rain bullet.

### Recipes of the deleted labs (for WP0's benches)

| Lab (deleted) | World | Water | Keep for |
|---|---|---|---|
| WaterLab | Perlin seed 4242, chunks (-2..2, 0..1, -2..2), heightScale 7 / freq 0.035 / oct 3 / pers 0.45, renderDistance 512, farTerrain on 2048, **no streaming** | enabled, seaLevel 54 (measured — surface y≈49-70), oceanBoundary | the un-baked authored-sea case; sea level must be re-measured on regen |
| CreekLab | Perlin seed 7311, **streaming**, renderDistance 256, climateFrequency 0.0018 | enabled, bakedTable, default sea 16 (real outlet) | creeks (order 1-2) near spawn |
| RiverLab | Mountains seed 20260729, **streaming**, renderDistance 384, climateFrequency 0.0012 | enabled, bakedTable, oceanBoundary false | an order-3 gorge at world (704, −1152); max order 5; high spill lakes ~272-346 |

WP0 bench spec, updated for the decisions: **`WaterBench_Basin`** (Flat 4×4 chunks, no bake, one
carved basin + one carved channel + a 1:10 ramp and a vertical wall — the SWE and shoreline rig),
**`WaterBench_Coast`** (streaming Perlin; try the old WaterTest seed 20260804 first since its shore
and 62 965-span rect are already documented in Water.md §5), **`WaterBench_River`** (Mountains seed
20260729 regenerated — the gorge coordinates above are a property of the seed and should
reproduce; verify with `water_find_river`). Each ships with pinned camera poses in game.json, the
way proving_grounds does.

---

## 7. What this plan does not do

World-scale Navier-Stokes, FFT oceans, volumetric 3-D fluid everywhere, bulk water as particles —
still out of scope, permanently (Water.md §6). It also does not promise swimming, boats, or
erosion; those are regimes the shallow-water engine makes *possible* and are listed, not planned.

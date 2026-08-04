# Water — the single defining document

> **This is the only water design doc.** It consolidates and supersedes six earlier docs
> (ledger in §10; every one is recoverable from git history). Written 2026-08-04 from a full
> read of all six plus the live code. If a statement here contradicts a code comment, the code
> comment is older — fix the comment.
>
> Standing discipline applies to all water work: ground every dimension, red-before-green on
> every claim, a stress phase per feature, validation depth named per deliverable
> (L1 artifact · L2 structural invariant on real output · L3 functional simulation · L4 live engine).

---

## 1. The goal (the user's own words, 2026-08-04)

> *"I want water to be similar to Minecraft in that it physically inhabits the world, but I want
> it to look better. I also want to improve on the flowing mechanics compared to Minecraft."*

Named features: **opacity variance · reflections · realistic waves against shorelines · smooth
flowing appearance · respected volume across draining bodies of water (to an extent).**

Standing directives (2026-08-03, verbatim — these are constraints, not aspirations):

- *"water must exist on top of terrain. it should never be possible to create a body of water
  that isn't tied to the physical boundaries of a terrain"*
- *"water can rest on itself and static voxel terrain"*
- *"Water bodies should be defined by the terrain holding them, not the other way around"*
- *"when creating a lake, the engine should first create a large basin that is then 'filled'"*
- *"It should be impossible to just add water without a 'basin' to put it in"*

**Volume policy ("to an extent"):** finite/infinite body classes. Ponds and small lakes are
strictly conserved mass — scooping/draining persists. The ocean is a bottomless boundary
condition. Large baked lakes are pinned (infinite) today; whether they become slowly-recharging
conserved bodies is an open decision (§9).

---

## 2. Architecture — three layers, one rule

**⚑THE CAMERA INVARIANT — violated THREE TIMES, now a hard gate (user, 2026-08-04):**
*"WHETHER water exists at a column must NEVER depend on camera position. Resolution may follow
the camera; existence may not."* Violations to date, all shipped and all caught by the USER
walking toward water: (1) the CA region rendering only simulated cells; (2) the grounded
grid's off-window fallback; (3) the fine span window's edge against a disagreeing coarse bake
(`c1f36b9c` — the seam was a camera-following waterline). The common failure: each design was
verified against its data source ("reads from spans ✓") instead of against the observable.
**Before ANY change to water placement or rendering ships: answer, in the commit message, "does
wet/dry at any column change with camera position?" — and run the camera-walk probe (§8 #8b):
capture water extent from two distant vantages and diff. Two sources may only be composed at a
camera-relative boundary if they AGREE on wet/dry everywhere.**

**The rule that governs everything: water that is DRAWN is not water that is SIMULATED.**
Almost all water is at rest. World data says where water is; the simulation only adds motion
where something is changing; the renderer reads world data and lets the sim perturb it. Every
expensive failure in this system's history (§7) came from violating this.

### Layer 1 — WORLD DATA (the truth)

**Target state:** per-column **water spans** stored in chunks (`WaterSpan{bottomY, topY}`,
float surface for fractional fill), persisted with the world. Full-3D occupancy encoded as
runs; cave lakes are later multi-run columns. A span's bottom is derived from the terrain
surface — floating water is *unrepresentable*, not merely detected.

**STORAGE + GENERATION + PERSISTENCE SHIPPED (2026-08-04, `b5906ad0`).**
`Chunk::WaterSpanLocal` (chunk-local, clipped per vertical chunk, sorted-(x,z) contract);
**ChunkBlobCodec v2** appends a water section — v1 blobs load unchanged, malformed data is
rejected never clamped; generation writes spans via a memoized per-chunk-column flood
(`waterSpansForChunkColumn`, one flood per column stack); `water_ground_sync` writes them for
un-baked editor worlds (clearing gone-dry chunks); `water_spans_stored` reads back only what
chunks hold. L4: 1,681 predicted spans in 4 chunks survived save → cold restart bit-identical
from disk. **Nothing renders from spans yet** — that is step 2 in §6.

**The occupancy API already exists and is tested** (`engine/include/core/WaterOccupancy.h`,
commit `e643a814`, 25 unit tests, mutation-checked):

- `buildOpenWaterSpan(surfaceY, level)` — containment structural; INT_MAX overflow bounded.
- `fillBasinAt` — **no level parameter exists**; the surface is derived from the container's
  spill via local Priority-Flood. Flat ground holds nothing, by construction.
- `floodBodiesOverGrid` / `WorldGenerator::waterSpansForBlock` — one multi-source flood per
  padded block, seeded from every baked-wet column. **The step bound is a correctness
  requirement** (window-independence = seam-freedom), red-verified. Measured: 100.9 ms/chunk
  cold, **7.9 ms/chunk-equivalent** at 128×128, 0.39 ms warm — vs 3,500 ms/chunk per-column.
- Division of labour, settled by measurement (§7 #4): the **coarse bake owns global
  connectivity** (32 km Priority-Flood; which bodies exist), the **fine pass owns the
  boundary** (per-voxel shoreline), `fillBasinAt` owns the API guarantee.

**Interim state — three placement paths coexist until spans land** (extend NONE of them):

| Path | Worlds | State |
|---|---|---|
| Implicit flat sea (`invCellSize == 0`) | Authored, `water.enabled` | Kept deliberately; retire when spans render. |
| Coarse 128 m hydrology bake | Streaming/baked | **Defective as placement**: `water_validate` measured 606/606 rim leaks, worst 38 voxels. Kills it: rendering from spans. |
| **Grounded grid** (`invCellSize < 0` = off-grid DRY) | Un-baked/editor | Shipped `7cd72001`. `water_ground_sync` builds per-voxel-column grid from live terrain via `buildOpenWaterSpan`; water cannot draw over void or through rock. Red/green at a camera inside rock; exact-count prediction (1,681) hit. |

### Layer 2 — MOTION (the CA is a motion layer, nothing more)

`WaterManager` + `WaterSimulation`: mass-conserving CPU CA in a **64×32×64
camera-following window**. The window is a *simulation* optimization and must never define
where water exists or renders — that coupling caused water appearing/disappearing with camera
distance, condemned by the user. Target: seed from spans, write settled state back to spans.

What the CA has (all shipped, commits in git history; compressed from the small-scale arc):
- **MIN_HOLD 0.3** donor gate — puddles/shallow water exist and persist under evaporation.
- **Creeks** (orders 1–2): fractional 0.33/0.66 pins on a ⅔-subcube bed shelf inside a
  parabolic swale; meander-aligned (warped-coordinate contract — raw coords covered only 33%
  of the carve).
- **RippleField** — 128×128 half-voxel damped wave heightfield, player-following;
  displaces per-cell water; `addRipple` on disturbances.
- **Entity coupling** — `sampleWater`/`submergedFraction` (the one shared query),
  buoyancy + drag on rigid bodies (wet bodies never sleep; slept bodies re-check ~1 Hz),
  wading slowdown + stride rings + entry splash, currents push entities (`flowAtWorld`).
- **Persistence of deviations** — poured ponds survive recenter + restart
  (`world_meta["water_overrides"]`); edge-outflow banks (window edge is not a wall).
- **Finite bodies** — `WaterBodyIndex` CC-labeling (Ocean/Lake ≥4 cells infinite, Pond
  finite); fine-scale ponds via `discoverFinePonds` (bounded depression fill, spill−0.15
  freeboard); `scoopWater` conserves to the gram on finite bodies, refills on pinned.
  ⚑Bake-cell "ponds" CANNOT be finite — coarse levels fragment against fine terrain
  (measured 6.5k→17.8 mass round trip).
- Settled cost ~0.002 ms/frame; active step ~150 µs (Release).

### Layer 3 — LOOK (appearance is per-body, physically grounded)

One `WaterProfile{turbidity, waveEnergy, roughness}` per body, derived in
`core/WaterProfile.*`, transported in the RGBA32F hydrology texture
(R=level, G=energy, B=turbidity, A=roughness; **NEAREST** — bilinear across a basin divide
tilts water), consumed by `water_common.glsl` (shared by sea + cell pipelines — the reason
they cannot drift apart).

- **W2 opacity variance:** turbidity → spectral extinction reconstructed in-shader from one
  scalar; clear `WATER_EXTINCTION(0.42,0.09,0.045)`, turbid endpoint `(0.95,0.65,1.20)`
  (B>R>G — blue dies first in turbid water). Ocean reads opaque from *depth*, not sediment
  (open ocean is Jerlov type I, the clearest water on Earth — §4).
- **W3 wind-driven sea state:** fetch along the wind chord (`min(w/|ux|, d/|uz|)` — support
  width was a measured bug), SMB tanh growth, closed-form fully-developed ocean scale,
  Cox–Munk roughness.
- **W4 reflections:** SSR in `shadeWaterSurface`, composed with Fresnel + ripple normal.
  The planar mirror path is dead (pre-existing broken pass + undersized descriptor pool).
- **Waves:** Gerstner sum (steepness Σ ≤ 1) on a Cartesian clipmap (`SeaMesh`, 4 levels,
  ~29k tris to 1024 u; per-component Nyquist fade — the polar mesh drew a camera vortex and
  cannot be tuned away). Whitecaps on crest steepness; shore surf band
  `breakDepth = max(2.56·amp, 2.5 voxels)`; rim-wall kill gate
  `|fragY − restLevel| > 2.5·amp + 0.5`.
- **Per-cell renderer** (`WaterCellRenderPipeline`): sloped top quads + edge skirts, flow
  vector per cell drives drift; near-field rivers/creeks/splashes.
- Post-scene water pass: colour LOADs the scene, depth bound read-only (tested + sampled,
  no copy); half-res refraction snapshot; underwater fog.

---

## 3. Status — the five goal features

| Feature | State | Evidence / gap |
|---|---|---|
| Opacity variance | **Shipped** (W2) | Two bodies differ in one frame, L4. Gaps: per-body *spatial* variation has no single-frame L4; underwater-fog wiring unchecked (task #8). Turbidity is per-body constant. |
| Reflections | **Shipped** (W4 SSR) | Counterbalanced perf protocol. Headroom: roughness-aware blur. |
| Waves vs shorelines | **Weakest visual, open** | Surf band exists. The Gerstner horizontal-displacement pull-away from walls is UNFIXED (one attempt made it worse, reverted — §7 #6). Water at a wall should pile up/slop, not slide off. |
| Smooth flowing appearance | **Partial, unassessed** | Cell renderer has slopes + flow drift; nobody has judged whether moving water reads smooth vs stepped. Needs an honest look first. |
| Volume on draining | **Partial, matches policy** | Finite ponds conserve exactly; ocean/lakes/rivers pinned. Draining is only physical inside the CA window until spans land. |

---

## 4. Grounded constants (do not re-derive; grounding-auditor verified sources)

| Constant | Value | Source |
|---|---|---|
| Clear-water extinction | `(0.42, 0.09, 0.045)` /m ×nudge | Pope & Fry 1997 |
| Turbid extinction endpoint | `(0.95, 0.65, 1.20)` /m | Akkaynak & Treibitz 2017 (blue attenuates fastest turbid) |
| Secchi → Kd | `Kd ≈ 1.7/Z_SD` (clear), `1.4/Z_SD` (turbid) | Poole & Atkins 1929; Holmes 1970 |
| Trophic anchor | Secchi 0.5–50 m span | Carlson 1977 TSI; Jerlov types |
| Sea-surface slope | `mss = 0.003 + 0.00512·U` | Cox & Munk 1954 (12.5 m wind) |
| Whitecap coverage | `W = 3.84e-6·U^3.41` | Monahan & O'Muircheartaigh 1980 |
| Fetch-limited growth | SMB tanh family | CERC / SPM |
| Breaking criterion | `H = 0.78·d` (H = 2·amp → breakDepth 2.56·amp, floor 2.5 vox) | McCowan |
| Gerstner stability | steepness sum ≤ 1 | standard |
| Body wave energy | `clamp(log2(area+1)/10, 0.15, 1)` — superseded by real fetch (W3) | |
| Sea level | `Core::kSeaLevelY = 16` — consumed, never re-declared; per-world override persists in `world_meta` (stored wins, loader WARNs) | |
| Hydrology bake | 256² cells × 128 m (~32 km) | terrain-v2 P2 |
| CA window | 64×32×64, follows camera (+vertically) | measured: 256³ = 19 FPS, never settles |
| MIN_HOLD / ripple field | 0.3 · 128×128 half-voxel | |

---

## 5. Testbeds — ONE world (fresh 2026-08-04)

**`PhyxelProjects/WaterTest`** — THE water test world, created fresh on the current engine
(chunk-span blobs v2, terrain-v2 bake, W1-W4 look). Streaming Perlin, seed `20260804`,
`water.enabled`, engine-default sea level 16. Facts (measured at creation):
- Boot to API: 9 s cold, including the hydrology bake (256², outlet TRUE, drainage complete,
  max river order 5, min terrain −24).
- Spawn (32, 60, 80) is DRY highland — 65,536 columns around it, zero baked-wet. The nearest
  water is the sea at **(165, 16, 890)**; camera preset for it is in this doc's history and the
  world was SAVED with ~976 chunks streamed there.
- **Streaming L4 of spans:** the shore rect (37,708)–(293,964) holds **62,965 chunk-resident
  spans in 81 chunks**, every top at 16.0 — generation-time spans work under real streaming.
- **The standing RED baseline** for render-from-spans (§6 step 2b), measured on this world at
  that rect, 0 unloaded / 50,629 wet: **rim_leaks 257/257 (100%), worst 6 voxels at (59,767)**.
  Gate: the same rect reads 0.

⚑Every earlier water testbed is DELETED (2026-08-04, user decision — they predated
water-as-world-data and carried stale engine state): WaterTableTest, WaterBasinTest, WaterLab,
CreekLab, RiverLab, RiverDemo. Numbers measured in them remain in this doc as history; the
reference screenshots in `docs/water-refs/` are historical evidence. Recreating a small bounded
basin world takes two commands (generate Flat 4×4 + carve — see the WaterBasinTest recipe in
git history) and should be done fresh when needed rather than kept.

## 6. Open work, in order

1. ~~**Spans in chunks** — storage + persistence~~ **DONE** (`b5906ad0`, 2026-08-04 — §2 layer 1).
   Still owed from it: streaming-world L4 (generation-time spans at scale + the added
   per-chunk-column flood cost measured on a real streaming boot).
2. **Render from spans** — EDITOR PATH DONE (2026-08-04): `rebuildGroundedWaterFromSpans()`
   derives the grounded render grid from what chunks HOLD, called at boot (`--project` and
   dialog paths) and by `water_ground_sync` — one derivation everywhere; a saved basin shows
   its water at boot with no command (L4: boot log `1681 wet columns from chunk spans` +
   screenshot). **Streaming worlds still render from the coarse bake — rim_leaks 606 stands**;
   replacing that upload with a span-derived fine grid is the remaining half of this step.
2b. **Render from spans (streaming)** — retires the coarse bake as a placement source, the implicit-sea
   special cases, and the camera-dependence. **Acceptance gate: `water_validate`
   `rim_leaks == 0`** (currently 606; two re-measure attempts were VOID — see §7 traps #1).
3. **CA over spans** — seed from, write back to; state the finite/infinite policy per class.
4. **Shoreline wave behavior** — the pull-away defect; piling/slop at walls. Open-water and
   water-vs-wall shading likely need genuinely different paths (user's instinct, 2026-08-03).
5. **Flow smoothness** — assess with reference captures, then improve.

**Backlog** (all designed in superseded docs; recover detail from git when picked up):
shore field (depth/shoreDist/shoreDir/cliffiness, 2 m cells × 512 m window) → wave
refraction/shoaling/compression toward coasts (Snell + Green's law) → breaker/impact spray
(reuse waterfall-mist pattern, `MAX_WATERFALLS`-class cap 48) → grass swash + terrain wetness
(needs `voxel.frag` sign-off) → river polish (bank foam, obstacle wakes, plunge pools; honest
limit: pinned reservoirs don't advect — nothing floats downstream without real transport) →
W5 weather (wind drifts over day/night; a driver, not new shading) → SSR roughness blur →
task #8 W2 verification gaps → NPC `WaterHooks` wiring (player factory only today) →
GPU compute port of the CA (design in git: v1 doc).

**Out of scope, permanently:** world-scale Navier–Stokes; FFT ocean + volumetric flow
everywhere; bulk water as particles (particles = splash/spray only).

---

## 7. Measured failures — do NOT retry as-is

1. **Growing the CA region to render more water** (256×32×256): 19 FPS, mass oscillating
   531k→889k→571k, never settles. The window should get *smaller* once rendering reads world
   data. Simulating rest is pure waste.
2. **One flat far-plane at a basin level**: flooded the whole frame — the dry-land gate can
   only ask "is terrain below the level I was handed"; no bbox helps because terrain varies
   inside one basin's box. Per-column levels are mandatory, not an optimization.
3. **Reusing the per-cell renderer for far water**: cells are hardcoded 1×1 (no scale field);
   ~90k instances for a modest lake.
4. **Pure terrain-derived basin fill in generation** (`fillBasinAt` alone, no bake): found
   **14 wet columns vs the bake's 1,079** (99.3% miss) at the inland lake — a bounded local
   flood walks its whole budget inside a large lake bed without ever finding the rim, and an
   unbounded one is unaffordable (3.4 ms/column). Bake = connectivity, fine pass = boundary.
5. **Full-voxel creek pins on uncarved ground**: a hillside sheet (fix was fractional pins on
   a real bed shelf, both facts from the same `channelAt` hit — the cell/segment split at
   junctions caused a measured ~900-mass flood).
6. **Shore-taper fix for wall pull-away** (2026-08-03): optimized a narrow metric, frame got
   worse (user: "way worse"); the dark band was the carve's wall shadow. Reverted. Next
   attempt must be judged by §8 rules.
7. **Unbounded batch flood** (no step cap): answer depended on the window it was computed in
   — chunk seams would tear. Red-verified before fix; and the first seam test **passed on the
   bug** because both fixture bodies sat at the same level (equal-by-coincidence — §8 #6).

---

## 8. Traps and process rules (each has cost at least a session)

**Verification traps:**
1. **A zero from `water_validate` is not a pass** — check `unloaded` and `wet`: an unstreamed
   region returns `rim_leaks: 0`, and so does a region with no water. Both hit in one session.
2. **The sealed-cavity fixture**: `Flat` generation caps its surface AT y=16 (not "below").
   A carve that leaves the cap leaves a LID — the open-sky rule correctly calls a roofed
   cavity dry. `scan_region` the fixture before blaming the code.
3. **State a falsifiable count before running** (the 1,681 prediction caught the lid
   instantly). A screenshot alone is never evidence; pixel probes + numeric predictions.
4. **Equal-by-coincidence tests**: when asserting two paths agree, the fixture must make
   disagreement produce *different values*, or mutation passes silently.
5. **Evidence must postdate the code**; never cite numbers from un-archived sessions;
   archive raw HTTP responses in `docs/evidence/`.
6. **Look-first rule for visuals**: keep a reference screenshot at a fixed camera; every
   visual change gets a same-vantage A/B; **a metric improving is not evidence it looks
   better**. Slope (amp×freq), not amplitude, is what the eye sees — corduroy comes from
   adding octaves without holding slope sum.
7. **Screenshot latency can miss transient effects** (a ripple ring lives 4–6 s — pump
   impulses while capturing); Σ|h| is not monotone for waves; multiplicative verlet damping
   needs `exp(−2k·dt)`.

8b. **The camera-walk probe** (the user's manual detection of the camera invariant, encoded):
   from a vantage far from a shoreline, record wet/dry for a fixed rect (`water_spans_stored`
   is camera-independent; the RENDERED extent needs a screenshot or pixel probe); teleport
   close; diff. Any column that changed is a camera-existence violation. Run it on every
   placement/render change — three shipped bugs would each have failed it.

**Operational traps:**
8. `build_shaders.bat` does NOT track `#include` deps — after editing `water_common.glsl`,
   force-compile `water.frag` AND `water_cell.frag`. `git checkout` does not rebuild.
9. `set_camera` right after `launch_engine` is silently overwritten by the project config —
   `get_camera` and confirm before trusting a frame.
10. A restart empties the sim (~1 min refill) — check `total_mass` is rising first. A probe
    right after launch can read `floor 0.0` before solidity resync.
11. Debug is not a perf measurement (Release only, warm frames); ±20% restart variance; the
    editor's near-chunk + vertical streaming band can leave a high camera with ZERO visible
    chunks — an "all-water wash" may be sky+sheet with no terrain drawable
    (`visible_chunk_count` disambiguates).
12. Descriptor rewrites need `vkDeviceWaitIdle`. Cube `exists` probes (`query_voxel`) read
    materialized overlay Cubes, not the voxel store — `hasVoxelAt` is the store truth.
13. Water renders in three pipelines but shades in ONE file (`water_common.glsl`) — that
    sharing is a design invariant, not an accident.
14. `.db` alone loses data (rows live in `-wal`) — copy db+wal+shm or checkpoint stopped.

**Open decisions (user):** big-lake recharge vs pinned (§1) · wind speed/direction in
`game.json` · per-body override keyed by id (unstable across regen) vs world-point ·
`voxel.frag` edit for shore wetness · how physical rivers get (advection is real work) ·
water-vs-OIT ordering (untested).

---

## 9. Where the code is

`engine/include/core/`: `WaterManager.h` · `WaterSimulation` · `WaterOccupancy.h` ·
`WaterProfile.h` · `WaterBodyIndex.h` · `RippleField.h` · `WorldConstants.h` (kSeaLevelY).
`engine/src/graphics/`: `WaterRenderPipeline` (sea clipmap) · `WaterCellRenderPipeline` ·
`SeaMesh` · `RenderCoordinator` (hydro upload, grounded grid).
`shaders/`: `water.vert/.frag` · `water_cell.*` · `water_underwater.frag` ·
**`water_common.glsl`** (the shared shading).
Debug API: `water_stats` · `water_probe` · `water_footprint` · `water_validate` ·
`water_span_scan` · `water_ground_sync` · `water_spans_stored` · `water_bake_info` · `water_bodies` ·
`water_find_river` · `water_look` · `water_ssr` · `water_waves` · `water_ripple` ·
`water_table_level` · `water_scoop` · `water_ocean_boundary` · `water_gpu` · `water_save`.
Tests: `WaterOccupancyTest` (25) · `WaterManagerTest` · `WaterProfileTest` · `SeaMeshTest` ·
`RippleFieldTest` · water+hydrology suites ~200 green as of 2026-08-04.

---

## 10. Superseded documents (deleted 2026-08-04; recover via `git show <hash>:docs/<name>.md`)

| Doc | Last hash | What it was | What survived where |
|---|---|---|---|
| `WaterSystem.md` | `0058d0c7` | v1 rationale: implicit sea + sparse CA, sources/sinks, channel tags, reach bounding, GPU-CA plan | §2 layer model, §6 backlog (GPU port), finite/infinite policy |
| `WaterSystemV2.md` | `9308597e` | Scale: following region, baked table, rivers, shoreline snap | §2 layers 1–2, traps; open APPCRASH noted in memory |
| `WaterSystemV3.md` | `b78ff489` | Look & flow: water pass, refraction, absorption, swell, surf | §2 layer 3 |
| `WaterPhysicalFeelPlan.md` | `924fce74` | Shore field, refraction/spray/swash plans + the small-scale arc ledger + §7 judging rules | §2 CA inventory, §6 backlog, §7 failures, §8 rules |
| `WaterAppearanceV4.md` | `2b4acb99` | W1–W5 profile arc, optics grounding, stress axes | §2 layer 3, §4 constants, §8 traps |
| `WaterAsWorldData.md` | `e643a814` | The architecture inversion: spans, basin-first, batch flood, grounded grid | §1 directives, §2 layer 1, §6 order, §7 failures |

The recurring silent APPCRASH (0xc0000005, ucrtbased.dll, post-teleport streaming) and other
cross-system issues live in agent memory and `docs/AgentContext.md`, not here.

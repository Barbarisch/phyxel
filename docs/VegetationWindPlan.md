# Vegetation Plan — realistic grass & foliage (wind, geometry, interaction)

**Status:** Phase 1 SHIPPED (2026-07-11) — shared WindSystem + travelling gust field live in
grass/foliage/foliage-shadow; `/api/debug/wind` up. Phases 2–4 planned. · **Owner workstream:**
rendering / vegetation
**Related:** `docs/RenderOptimization.md`, grass blade layer (`GrassRenderPipeline`), leaf cards
(`FoliageRenderPipeline`), `/api/debug/grass`, `/api/debug/foliage`.
**User intent (verbatim goals):** wind that isn't "randomish"; grass that isn't "just a single
stretched-out quad"; flat-square construction that matches the voxel aesthetic; grass that moves
out of the player's way. **The full plan exists even if perf gates shelve parts of it** — each
phase carries its own gate + explicit shelving criterion.

## Problem

### Motion (wind)

| # | Defect | Where |
|---|--------|-------|
| 1 | **Single sine wave** — one frequency, constant amplitude, perfectly periodic. Real wind is broadband: slow gust fronts + mid turbulence + fine flutter. | `grass.vert:147` (`sin(t*1.7+phase)`), `foliage.vert:101` (`sin(t*1.3+phase)`) |
| 2 | **No traveling gusts** — amplitude never varies in space or time, so no waves sweep across a field (the single strongest realism cue for grass). | both |
| 3 | **Desynchronized neighbors** — foliage adds `+ float(card)` to phase, so adjacent cards jitter independently → reads as noise, not coherent canopy motion. | `foliage.vert:100` |
| 4 | **Leaf cards translate rigidly** — the whole quad drifts in XZ (`center.xz += sway`); nothing pivots or bends, so the canopy looks like it's floating, not being pushed. | `foliage.vert:102` |
| 5 | **Two hardcoded, mismatched wind directions** — grass `(0.85,0.35)`, foliage `(0.8,0.35)`; neither controllable, and no other system (trees, particles, weather) can share them. | `grass.vert:145`, `foliage.vert:99` |

### Geometry

| # | Defect | Where |
|---|--------|-------|
| 6 | **A blade is one stretched quad** (6 verts, 2 tris, fragment-taper silhouette). It cannot *curve* — wind bending is a shear of a flat rectangle, and up close every blade is visibly a flat ribbon. | `grass.vert:113-152` |
| 7 | **No LOD in the blade model** — the same 6-vert quad renders at 1u and at 47u; the only distance response is height fade. Budget is spent uniformly instead of where the camera looks. | `grass.vert:132-135` |

### Interaction

| # | Defect | Where |
|---|--------|-------|
| 8 | **World-blind** — the player, NPCs, and debris move through grass and low foliage without disturbing a single blade. | — |

Current budget baseline: `bladesPerVoxel = 20` × 6 verts = **120 verts per grass voxel**, radius
48u, clumped ~7/tuft (`GrassRenderPipeline.h:35-43`). Foliage: `cardsPerVoxel` quads per exposed
leaf subcube.

## Reference techniques (grounded)

- **Ghost of Tsushima — "Procedural Grass" (GDC 2021)**: unified 2D-Perlin wind field sampled by
  CPU+GPU; per-blade **cubic Bézier** shape — 15 verts along the curve, controlled by
  height/width/tilt/bend parameters; player-interaction bend; LOD by vertex count.
  Slides: <https://archive.thedatadungeon.com/ghost_of_tsushima_2020/documents/gdc_2021/gdc_2021_procedural_grass_in_got.pdf>
  · GDC Vault: <https://gdcvault.com/play/1027033/>
- **GPU Gems 3 ch. 16 — Crysis vegetation**: split motion into **main bending** (whole plant
  along wind vector, height-scaled) + **detail bending** (leaf/edge flutter from per-vertex data).
  <https://developer.nvidia.com/gpugems/gpugems3/part-iii-rendering/chapter-16-vegetation-procedural-animation-and-shading-crysis>
- **GPU Gems 3 ch. 6 — procedural tree wind**: hierarchical trunk/branch bending (future).
  <https://developer.nvidia.com/gpugems/gpugems3/part-i-geometry/chapter-6-gpu-generated-procedural-wind-animations-trees>
- Open-source GoT-style implementations for cross-checking: 2Retr0/GodotGrass,
  donguklim/Ghost-of-Tsushima-Grass-plus-Rotational-Dynamics (adds rotational spring dynamics).

## Design

### Phase 1 — shared procedural wind field (the realism jump)

A single **`WindSystem`** (CPU, `Phyxel::Graphics`) owns global wind state and feeds every consumer:

- **State:** direction θ(t), base speed, gustiness — each drifting smoothly via low-frequency 1D
  noise on the CPU tick (a few flops/frame; deterministic from world seed + time).
- **GPU evaluation (in-shader, analytic — no texture needed at first):**
  `windAt(p, t) = dir * (base + gust * fbm2(p.xz * gustScale − dir * gustSpeed * t))`
  2-octave value noise scrolled **along the wind direction** = gust fronts that visibly travel
  across the field. Small high-frequency term for flutter. Shared `wind.glsl` include used by
  both `grass.vert` and `foliage.vert` (glslc gotcha: `#include` deps need manual recompile —
  same rule as `voxel.frag`).
- **Plumbing:** extend the grass/foliage **push constants** (windDirXZ, gustAmp, gustScale,
  gustSpeed; keep `windStrength` as master amplitude). Deliberately **avoid touching the shared
  `UniformBufferObject`** — it's mirrored in VulkanDevice.h + six shaders and is a known sync
  footgun. `WindSystem` writes both pipelines' params each frame so they can never diverge.
- **Grass:** bend along `windAt(root)`; keep the v² root-planted profile; **approximate length
  preservation** (drop tip Y as the tip displaces laterally: `y −= 0.4·|offset|²/H`) so gusts
  read as *bending*, not stretching; **per-blade stiffness** from the existing hash (stiff blades
  lag gusts, soft blades overshoot — variation without desynchronized noise); perpendicular
  high-frequency flutter with amplitude ∝ gust strength (calm air = calm grass).
- **Foliage:** replace rigid XZ translation with a **pivot** about the bottom of the subcube
  sprig (displacement ∝ height within card → base anchored, tip sways) + a few degrees of
  oscillation of the card basis around `nrm` for glinting flutter. Phase from **subcube position
  only** (drop `+ float(card)`): one sprig moves together; sprig-to-sprig variation comes from
  the gust field — the Crysis main/detail split. `foliage_shadow.vert` must get identical motion.
- **API:** `/api/debug/wind {dirDegrees, speed, gustiness}`.

**Gate:** vertex-cost delta within noise on the CharacterTestbed forest (engine-perf loop).
**Shelve if:** never — this phase is pure math on existing vertices; no plausible perf failure.

> **SHIPPED 2026-07-11.** `WindSystem.{h,cpp}` (ticked in `RenderCoordinator::drawFrame`, state
> copied into both pipelines' `Params::wind` each frame), `shaders/wind.glsl` (`windGustAt`,
> 2-octave value noise scrolled downwind), push-constant extension in all three vegetation
> shaders + both pipeline `.cpp`s (`static_assert`ed sizes 60/52), `POST /api/debug/wind`
> (`set_wind` in Application.cpp). Verified on CharacterTestbed: 5 CPU unit tests
> (`tests/graphics/WindSystemTest.cpp` — determinism, zero-speed stillness, unit-dir wander
> bounds, frame continuity, monotone scaling); API round-trip echoes settings + derived state
> (state echo is one frame stale by design); **wind 0 → two viewport captures 2 s apart were
> bit-identical (max pixel delta 0)**, storm → 7 % of viewport pixels moving; canopy shadows
> track displaced cards; extremes (speed 100→clamped 2, gustiness 100→1, 10× master strengths,
> negative dirDegrees) stable, no NaN/vanish; FPS calm 456 avg vs storm 470 avg = within noise.
> Known v1 look: extreme bend rigidly offsets a whole canopy (sprig pivot, not trunk bending —
> that's Phase 5); imperceptible at sane strengths. Gust-front *speed* (≈ gustSpeed·Δt) was
> eyeballed via the storm captures, not measured — measure properly when Phase 2 tunes weather.
>
> **Post-ship tuning (2026-07-11, user feedback "very jittery"):** the first cut ran the
> per-blade stiffness lag (−0.15..0.35 s) and flutter (6.1 rad/s grass / 5.3 rad/s foliage,
> random phase) too hot — neighbors desynchronized back into shimmer. Now lag 0..0.12 s,
> grass flutter 2.7 rad/s @ 0.10, foliage card flutter 2.1 rad/s @ 0.05. Treat these as
> floors: motion realism must come from the travelling gust field, never from fast
> per-blade/per-card noise.
>
> **Blade aesthetic (same day):** `bladeStyle` shipped in `/api/debug/grass` — **1 = boxy
> (default): thin elongated crisp RECTANGLE** (vSide 0 defeats the frag taper; rest height
> quantized to microcube steps; width 0.06), 0 = smooth tapered ribbon. Both share the SAME
> smooth v² wind motion. Density default bladesPerVoxel 20 → 28. **The Phase 3 "stepped"
> variant (stacked squares + quantized offsets) was BUILT AND REJECTED by the user** — rigid
> stacked squares with popping quantized motion read as "super janky and pixelated." The
> voxel aesthetic the user wants = boxy *silhouette* + smooth *motion*. Phase 3's remaining
> value is the segmented/Bézier *smooth* blade + LOD bands; drop the stepped-motion idea.

### Phase 2 — gusts as weather

Gust envelope with lulls (noise-gated bursts, not constant churn); optional DayNightCycle
coupling (calm dawn, gusty afternoon); persist per-world wind tuning in the world recipe
(`world_meta`). **Gate:** none needed (CPU-side scalars).

### Phase 3 — grass geometry v2: segmented blades, voxel-aesthetic option, LOD

Replaces defect #6/#7. The blade becomes a **3-segment strip** (4 rows × 2 verts = 8 verts,
6 tris — GoT-lite; GoT uses 15 verts) whose rows sit on a quadratic Bézier from root to tip:

- **Curved at rest:** per-blade `tilt` + `bend` params from the existing hash — blades arc
  naturally instead of standing as flat ribbons; wind displaces the Bézier control points
  (P1 mid-blade, P2 tip) instead of shearing, so gust bending curves the blade. Length is
  conserved by the curve itself (drops the Phase 1 approximation for near blades).
- **Voxel-aesthetic variant (`bladeStyle = "stepped"`):** the same 3 segments rendered as
  **stacked flat squares with quantized offsets** — each segment a small quad whose XZ offset
  snaps to the 1/9-voxel microcube step grid, giving deliberately "pixelated" blades that read
  as engine-native voxel geometry (the flat-square look the user has seen elsewhere). Same
  vertex count as the smooth variant; one push-constant switch, per-world-recipe choice.
- **Sprig upgrades for foliage (small, same PR):** optional 2-quad crossed cards for large
  canopies (cards already ARE flat squares with cutout masks — the voxel aesthetic is native
  there; this just thickens sparse sprigs).
- **LOD bands (keeps the budget flat instead of 3×):**
  - **Near** (< 16u): segmented blades, 8 verts.
  - **Mid** (16u–radius): current single-quad blades, 6 verts — but drawn as **crossed-quad
    tufts** (2 quads per ~7-blade clump = 12 verts/clump vs 42) beyond ~24u.
  - **Far** (≥ radius): existing height-fade collapse (unchanged).
  - Selection in-shader from camera distance (vertex count fixed per draw → band by
    degenerating unused rows to zero area; no CPU re-bucketing needed, same instance buffers).
- **Budget math:** near band 20 blades × 8 = 160 verts/voxel (+33 % over today, only within
  16u ≈ π·16²/π·48² ≈ 11 % of grass area); mid band drops to ~36 verts/voxel (−70 %). Net
  expected **below current cost** at equal density — the LOD pays for the segments.

**Gate (engine-perf):** grass pass GPU time within +10 % of today on the forest world at default
density; visual capture confirms band transitions are invisible at the 16u/24u seams.
**Shelve if:** the seams can't be hidden without matching band budgets (fall back: ship stepped
blades at 2 segments, keep single-quad mid unchanged).

### Phase 4 — interaction: grass (and low foliage) moves out of the way

Two stages, cheapest first:

- **v1 — stateless displacer SSBO (ships the feature):** a small SSBO (≤ 16 spheres: player
  capsule feet, NPCs within grass radius, kinematic debris ≥ ~1 voxel) written per frame by
  `WindSystem` from EntityRegistry positions. Per blade root:
  `push = Σ dir(root−d.xz) · (1 − dist/r)² · d.strength` (r ≈ 1.2u), applied as an extra bend
  on the Bézier control points, plus a height squash factor (~0.85 at full push) so trodden
  grass flattens rather than just leaning. Clamp total bend. Cards on the lowest subcube layer
  of bushes get the same push at reduced amplitude (walking through a bush rustles it).
  Stateless = instant spring-back on exit; acceptable v1 (this is what most "grass parts around
  the player" implementations ship).
- **v2 — bend-field with memory (trails + spring-back):** a camera-centered R16G16 **bend
  texture** (128², covering ~64×64u, snapped to voxel grid to avoid swimming) ping-ponged by a
  tiny compute pass each frame: `field = field·decay + Σ stamp(displacers)` (decay ≈ 0.92 →
  ~½ s spring-back with overshoot via a velocity channel if wanted). Blades sample it instead
  of the SSBO loop. Buys: persistent trails behind the player, wakes behind rolling debris,
  many displacers for free. Cost: one 128² compute pass (< 0.05 ms) + one texture fetch/blade.

**Gate:** v1 adds ≤ 16 mul-adds per blade — must be within perf noise; v2 gated on the compute
pass staying < 0.1 ms. **Shelve if:** v2 texture sampling in the vertex stage measurably hurts
(unlikely; fall back to v1 permanently).

### Phase 5 (unscheduled) — trees

Solid leaf/log voxels are baked static geometry and **cannot move**; the card layer already
carries all visible canopy motion. True trunk/branch sway = GPU Gems 3 ch. 6 hierarchical
bending, needing a vertex-displacement channel for tree voxels — out of scope until a driving
need. (If traced deforming vegetation ever matters under the RT plan, the tet-cage literature —
`RayTracingPlan.md` §B — applies here.)

## Validation (depth per CLAUDE.md)

Cosmetic feature → **required L1–L2 + runtime visual + perf gate**:

1. **L1/L2:** shaders compile; push-constant layouts match CPU structs (`static_assert` sizes);
   `/api/debug/wind` round-trips; Bézier evaluation unit-tested CPU-side (row positions for
   known control points, length conservation within 2 %).
2. **Runtime visual:** `/visual-test` — grass field + canopy at wind 0 (must be perfectly
   still), calm, storm; gust fronts travel in the set direction (two screenshots Δt apart,
   front moved ≈ gustSpeed·Δt); walk-through capture shows parting + (v2) trail.
3. **Perf (engine-perf skill):** grass pass GPU time recorded here per phase, forest world,
   fixed camera path; each phase's gate above.
4. **Stress:** windStrength ×10, gustSpeed ×10, zero values; 16 displacers at once; radius-edge
   blades (fade + wind + push compose without popping); LOD seams under camera dolly.

## Sequencing

1. **Phase 1** wind field + both shaders + API — one PR, biggest visible win.
2. **Phase 2** gust envelope/weather — small follow-up.
3. **Phase 3** blade geometry v2 + LOD — one PR; do before interaction so pushes bend curves,
   not shear flat quads.
4. **Phase 4** interaction v1 (SSBO) → v2 (bend field) — after Phase 3 settles.
5. **Phase 5** trees — unscheduled.

# Vegetation Plan — realistic grass & foliage (wind, geometry, interaction)

**Status:** Phase 1 SHIPPED (2026-07-11) — shared WindSystem + travelling gust field live in
grass/foliage/foliage-shadow; `/api/debug/wind` up. **Phase 4 v1 SHIPPED (2026-07-18)** —
stateless character displacers part + flatten grass (`/api/debug/grass {pushStrength}`), plus
full-face tuft distribution fix. Phases 2–3 + 4 v2 (trail bend-field) planned. · **Owner workstream:**
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

Budget baseline (**superseded 2026-08-01** — see the density-LOD note below): `bladesPerVoxel = 20`
× 6 verts = **120 verts per grass voxel**, radius 48u, clumped ~7/tuft. Foliage: `cardsPerVoxel`
quads per exposed leaf subcube.

### Density LOD — the current baseline (2026-08-01, corrected 2026-08-05)

Defaults are `bladesPerVoxel = 140`, `radius = 224`, `fadeRange = 40`, `bladeHeight = 0.44`, blade
width **0.040** (was 0.016 — widened 2.5× because at the old width a blade was ~0.6 screen px and
leaned entirely on the sub-pixel floor to stay visible), and the meadow height field's dominant
octave is ~72 voxels with per-blade jitter cut to ±6%, so stand height reads as varying across a
*field* rather than per voxel.

⚠️ **The five-band per-chunk falloff described here until 2026-08-05 no longer exists.** Deciding
density per chunk made two adjacent chunks in different bands draw different densities, and the
boundary showed as a hard seam through open field ("disjointed grass"). Density is now **per blade
and continuous** in the blade's own world distance (`densityFrac = 1/(1 + 140·u²)`, floored at
1/18, `grass.vert:250`), so it is identical either side of any voxel or chunk boundary by
construction. `GrassRenderPipeline::bladesForDistance` survives only as a **conservative upper
bound on the vertex count** — it must never be tighter than the shader's own test anywhere in the
chunk, which is why the caller passes the chunk's *nearest* point, not its centre. **Nothing
per-chunk may influence how a blade looks, or the seam comes straight back.**

Two properties make draw-shortening safe, and both are pinned by
`tests/graphics/GrassDensityLodTest.cpp`:

- **Survivors are bit-identical across a tier change.** `pc.bladesPerVoxel` is pushed at the FULL
  count regardless of tier, so every blade's clump, seed and height still derive from its own
  `gl_VertexIndex` — shortening the draw omits the highest-indexed clumps and disturbs nothing
  else. Re-deriving the hashes from a reduced count would re-roll every blade at each band edge.
- ~~**Counts are whole clumps.**~~ **Superseded 2026-08-05 — see the placement section below.**
  Blades are no longer clumped, so any count is a legal well-spread distribution and 1 is as valid
  as 7. `bladesForDistance` still quantises to 7 (harmless, keeps draw counts tidy) and
  `GrassDensityLodTest.EveryCountIsAWholeNumberOfClumps` still pins it, with the reason recorded
  in that test.

### Blades cannot overlap — one blade per lattice cell (2026-08-05)

Placement was: tufts of 7 whose roots jitter inside a **fixed ±0.08 u box regardless of blade
width**, each voxel clamping its roots into its own `[0.005, 0.995]`. Both guaranteed overlap —
7 blades of width `w` in a 0.16 u box collide for any `w > ~0.023`, and a root at 0.995 sits
**0.01 u** from its neighbour's at 0.005 across the voxel border, which no voxel-local scheme can
fix because neither voxel can see the other.

Measured before the change (`docs/evidence/pack_before.json`): 28 blades resolved to 10
distinguishable regions, 112 blades to 8 — blade pixels grew 29× while visible structure saturated.

Each blade now owns one cell of a **world-aligned 16×16 lattice** (0.0625 u pitch,
`shaders/grass_sites.glsl`, generated by `tools/gen_grass_site_order.py`). Aligned to the voxel
grid rather than to each voxel, so cell centres tile continuously across voxel *and* chunk borders
and the cross-border case needs no handling. The `[0.005,0.995]` clamp is deleted — it *was* the
mechanism.

The ordering is **progressive blue-noise**, so every prefix is well-spread. Load-bearing, not
decoration: the LOD thins by drawing only the first N blades, so every prefix ships at some
distance — and survivors keep their cells, preserving the stability contract above for free.

⚑**Use the continuous `Cseq/√N` envelope, never the per-N staircase.** Measured `Cseq = 0.7099`,
worst prefix at **N=129** — one past a refinement boundary. Two *adjacent* voxels can keep
different N, and the staircase would let the sparser one assume a separation its neighbour does not
honour. Same bug class as the chunk seam.

⚑**`bladesPerVoxel` is clamped to 256.** `grass.vert` indexes the lattice with `blade & 255`; a
257th blade wraps onto cell 0 and lands exactly on blade 0 — silent, exact overlap, no error.

⚑**Tufting was deleted deliberately, not compensated for** (user, 2026-08-05: *"more spread out
with less grouping should be the default"*). The old `grass.vert:136` comment — "even spacing reads
as isolated spikes; clustered blades read as grass" — is overruled by direct observation. If
spread-out ever reads as spiky, the lever is **more blades**, not re-clustering them.

Guarantee and proof: `tests/graphics/GrassBladePackingTest.cpp` (red-before-green; 4 of 8 failed on
the tuft placement, worst pair 0.00022 u where 0.246 u was required). Demonstration harness:
`tools/grass_packing_lab.py` — its metric **must** be an A/B against blades-off, because a
green-colour mask reads the grass voxel's own green top face and reported "all blades distinct" at
every count until the control caught it.

`widthCompensation` widens the survivors by `1/sqrt(frac)` (capped 2.6×) so thinning doesn't read
as the meadow disappearing, and a sub-pixel floor (`dist * 0.0011`, ~1 px at the reference config)
holds far blades at about a pixel — aimed at the long-standing grass speckle
(`RenderOptimization.md:513`), which is sub-pixel quads flickering across sample points.

⚠️ **Recompute the vertex budget before touching `radius` or `bladesPerVoxel`** — the total is
quadratic in the radius and eyeballing the curve fails badly. The worked figures are in the comment
on `bladesForDistance`: a first cut at 98 blades / radius 320 with a *plausible-looking*
100/55/30/15% falloff came to **143M verts/frame, 40× the old baseline**, with the outermost band
alone costing 52M. The shipped config is 22.8M spread evenly across bands (3.3/3.9/4.7/4.6/6.4M);
"no single band dominates" is the property to preserve.

⚠️ **Grass is skipped entirely on chunks at LOD level != 0** (`RenderCoordinator::renderGrass`).
Grass instances are emitted during the FINE rebuild at true surface positions and a LOD swap does
not touch them, while the coarse mesh's OR-occupancy surface sits at or ABOVE the fine one — so
blades on a coarsened chunk are buried inside it. This is why distance-driven chunk LOD stays
default-OFF: its working window (~136–352u) is precisely the band the 192u grass radius occupies.

### Grass blades cast shadows (2026-08-05)

Blades render into the shadow map via a second pipeline (`GrassRenderPipeline::renderShadow`) using
`grass_shadow.vert` — a **generated** sibling of `grass.vert` (`tools/regen_grass_shadow.py`;
regenerate after every `grass.vert` edit, guarded by `tests/graphics/GrassShaderMirrorTest.cpp`).

Blade shadows were dead for the feature's whole life, from **two independent bugs** that masked
each other — which is why five successive single-cause diagnoses were all wrong:

1. **The blade never rasterized.** A 0.04 u blade against a 0.125 u shadow texel at the default
   420 u shadow distance covers no texel centre and writes nothing.
2. **The bias was authored in normalized depth**, so its *physical* size scaled with the shadow
   distance — 0.26 u at 40 u, but **0.85 u at 420 u**, taller than a grass blade. Even a blade that
   did rasterize was rejected. Bias is now authored in **world units** and divided by
   `ubo.shadowDepthRange` (`lighting.glsl`), so it is distance-invariant.

Two shadow-pass-only behaviours, both gated by the single `kShadowWidthGate` constant so the
visible blade is provably untouched:

- **The blade turns broadside to the sun.** Blade yaw is a per-blade hash, so a blade pointing
  along the light projects *zero* width and casts nothing regardless of bias or size — shadow
  strength depended on the hash, not the blade. Facing the light gives every blade its full
  projected width. It is a deliberate bias to compensate for the shadow map not resolving blades;
  real edge-on blades genuinely cast almost nothing.
- **Width × `shadowWidthScale`** (default 2.0, live via `/api/debug/grass`). This replaced a
  shadow-*texel* clamp that sized the shadow to the shadow map rather than to the blade, so it grew
  with shadow distance: 0.50 u of shadow for a ~0.10 u blade at 420 u — a smudge ~5× wider than its
  caster. **Consequence, by design:** shadow width now tracks the blade, so a blade under one texel
  stops casting rather than smearing. Fixing *that* needs a near cascade, not a wider blade.

**Verified** on the single-blade rig (`tools/grass_shadow_verify.py`, project `GrassShadowLab`) at
shadow distances 40/80/160/420 with a grass-removed control changing 0 px. **Not verified:** the
shipped configuration — the rig runs at `bladesPerVoxel = 1`, where the density compensation sits
at its 2.6× cap making the blade ~0.104 u, about **2.6× wider than a dense-field blade (~0.040 u)**.
Thresholds measured on the rig are optimistic for real grass.

⚠️ **Instrumenting the shadow pass:** do not write counters into `lastFrameStats` — `drawFrame()`
clears it *after* the shadow pass, so the numbers read back as zero and look like "nothing was
submitted" (`RenderCoordinator.cpp:1791`). This cost a wrong "0 grass casters" conclusion when 189
were real.

### Wind debug view — `POST /api/debug/shadow {"mode":2}` (2026-08-05)

**Use this before arguing about wind by eye.** Every blade is coloured by how far the wind is
pushing it *right now*, as a fraction of its own arc length, so a passing gust reads as a coloured
band crossing the field and a dead field reads as uniformly dark. All non-grass geometry goes flat
dark in this mode — an earlier version left terrain rendering the shadow-only view underneath and
the shadows completely drowned the signal ("is the black and white the movement or shadows?").

```
near-black → blue → green → yellow → red     (upright → at the 64° lean cap)
```

The ramp is `sqrt`-scaled: linear against the cap put the entire still-to-peak range of ordinary
wind inside the first segment, so everything read as one flat blue — true, but useless.

Debug view selector (the UBO field is still named `debugShadowMode` historically):
`0` off · `1` shadow-only · `2` grass wind.
⚠️ The handler used to collapse every non-zero value to `1`, so `mode: 2` silently selected the
shadow view and *appeared to work*. It now clamps to the valid range instead.

**What it found in one sitting**, none of which was visible by staring at the grass:
- Blades were leaning **11% of the available range** — motion too small for any frequency to rescue.
  `windStrength` had been 0.13 since blades were a third of their current height.
- Roughly **two thirds of the lean was constant**, not moving: a steady `base` of 0.55·speed plus
  the gust field's own DC term (raw value noise averages 0.5). Cutting `base` to 0.10·speed and
  squaring the gust flipped the constant:varying ratio from ~3:1 to ~1:1.
- The anisotropic bands exist and are correct — they simply could not be seen until amplitude
  allowed them.

Also expose `scrollX`/`scrollZ` on `/api/debug/wind`: that is the gust field's *position*, so
"is the wind actually moving?" is answerable from the API. Without it the question cost a whole
debugging round and produced a confidently wrong answer.

### Shipped wind defaults (approved 2026-08-05)

Tuned live against the debug view and signed off by eye. The derivations in `WindSystem::tick` are
**calibrated so these Settings land on these numbers** — change one and the other must move.

| knob | default | meaning |
|---|---|---|
| `speed` | 0.35 | drives `base`, `gustAmp`, `gustSpeed` together |
| `gustiness` | 0.45 | swell vs steady lean; also sets front size |
| `windStrength` (grass) | **0.50** | master amplitude; was 0.13 → ~11% of the lean cap |
| `flutterFreq` | **1.8 Hz** | local blade quiver — NOT the front travel rate |
| `aniso` | 5.0 | crosswind stretch; 1.0 = the old isotropic blobs |
| derived `gustSpeed` | 2.5 u/s | front travel |
| derived `gustScale` | 0.045 | fronts ~22 u deep, ~110 u crosswind |

⚑ **Two frequencies, easily confused.** `gustSpeed` is how fast a *front crosses the field*;
`flutterFreq` is how fast a *blade quivers in place*. Slow fronts with no flutter read as static
grass that occasionally leans.

⚑ **A varying direction must never enter a transform scaled by position or time.** This bug class
appeared twice in one session: the scroll was `dir × gustSpeed × elapsedTime` (a heading change
displaced the field proportionally to uptime — 0.05 noise cells/s at 10 s, 3.2 at 10 min), and the
anisotropy rotated the sample point about the world origin (~3 cells per *degree* at the far end of
the 2048 hash domain). Both read as "smooth for a while, then springs rapidly." Integrate the
direction, or hold it constant — `State::dir` is now static and the wander survives only inside the
integrated `scroll`, which is never published to a shader.

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

> **v1 SHIPPED 2026-07-18** (plus a tuft-distribution fix the same day). Implementation
> deviates from the sketch in one way: the displacer set rides as **trailing fields on the
> shared `UniformBufferObject`** (`grassDisplacers[16]` vec4 xyz = camera-relative feet pos,
> w = push radius; `grassDisplacerMeta.x` = count), NOT a separate SSBO — appending after
> `cameraWorld` keeps every other shader's truncated std140 block valid (same precedent as
> `elapsedTime`/`cameraWorld`), and `VulkanDevice::setGrassDisplacers` patches it AFTER
> `updateUniformBuffer` zero-fills each frame. Collection is engine-side in
> `RenderCoordinator::drawFrame` (player/animated entities + NPC characters within grass
> radius, 16 nearest kept), so editor and standalones share it. Shader (`grass.vert`):
> radial push with (1−d/r)² falloff, vertical gate fading over ±2 u of the displacer's feet
> Y, height squash ×0.70 at full tread, push composed into the wind `swayDir` so the
> tip-drop length preservation makes trodden blades hug the ground; total bend clamped at
> 1.4. `pushStrength` push-constant (default 0.9, 0 = off) exposed via `/api/debug/grass
> {pushStrength}` (`GrassPush` 76→80 B). **Distribution fix:** tuft centers had a 0.18 edge
> margin confining every voxel's grass to a middle island (per-cube clumps + bare grid
> seams); centers now span the full top face (0.02..0.98, jitter ±0.08, roots clamped
> on-face so nothing overhangs a step-down edge).
> **Verified on CharacterTestbed (Debug, second instance on --port 8091):** parted+flattened
> ring at a spawned character's feet; pushStrength 0 → blades stand through the feet
> (A/B); teleport → parting follows instantly, old spot springs back (stateless); 20
> characters → per-character trampling, >16 displacer clamp path exercised, no crash;
> FPS 28.07 (push off) vs 27.71 (push on) = within noise at 20 idle characters; **wind 0 +
> no characters in view → two viewport crops 2 s apart bit-identical (0/1.6 M bytes
> differ)** — the interaction path is exactly inert when unused. Not yet runtime-tested:
> the vertical gate with grass on a ledge directly above/below a character (geometric by
> construction). v2 (bend-field trails) remains open; foliage/bush push (reduced-amplitude
> card response) also still open.
>
> **2026-07-19 follow-up — MEADOW height field (user-set look):** blade height must vary over
> LARGE distances but be uniform + dense over short ones. `grass.vert` now derives a smooth
> 2-octave value-noise field (`vnoise2`, wavelengths ~26 and ~9 voxels, hash-domain coords)
> that drives BOTH stand height (×0.55 short zones .. ×1.5 lush zones, smoothstepped) and
> coverage (0.78..1.08 — only slight thinning in short zones, never bald tuft-scale holes).
> Per-blade height jitter cut to ±10% (was ±~46%) so neighbors read as one even stand. The
> old per-5-voxel/per-2-voxel hash patch gate (short-scale holes) is REMOVED — replaced by
> the meadow-coupled coverage. Verified live: waist-high stand at bladeHeight 0.85 /
> bladesPerVoxel 32 / radius 64, dense + locally uniform, trample ring reads clearly,
> ~246 FPS steady (Debug CharacterTestbed).
>
> **2026-07-19 — FLEX + gentle wind (Phase 3 partial; user: "too stiff", "moves too much out
> of the way", "jittery, not gentle", "thinner"):**
> - **Segmented blades ship the flex.** A single quad CANNOT curve — displacement shears a rigid
>   parallelogram (the two single-quad fakes, normal-only bending and fragment arc-discard, buy
>   nothing for flat cutout blades). Each blade is now **3 stacked quads** (`SEGMENTS` in
>   grass.vert, `vertsPerBlade = 18` in GrassRenderPipeline.cpp — **keep these in sync**), with
>   `v` remapped to the whole-blade fraction so segment rows share boundaries. Per-blade **flex
>   exponent** `bendExp = mix(1.6, 2.4, stiffness)` replaces the fixed v²: soft blades yield along
>   their length, stiff ones hold their base and give at the tip.
> - **Wind inertia kills the jitter.** Blades were reading the gust field *instantaneously*.
>   `gust` is now the mean of **three time-lagged taps** (t, t−0.25, t−0.50 → ~0.5 s box filter),
>   so fronts arrive as smooth swells; travelling-front realism survives (taps scroll with the
>   same field), only high-frequency content is removed. Flutter 2.7→1.9 rad/s @ 0.10→0.055;
>   stiffness lag spread 0.12→0.06 s (wide spreads desync neighbors into shimmer — same failure
>   as the 2026-07-11 first cut).
> - **Push is gentler + damped.** Default `pushStrength` 0.9→0.55; displacers are now **stateful**
>   (`RenderCoordinator::GrassDisplacerState`, keyed by character pointer, eased attack 0.07 s /
>   exponential release 0.28 s, strength in a new `grassDisplacersAux[16]` UBO array) so grass
>   rises back gently instead of popping upright; trodden blades are **pinned against wind**
>   (`windDamp = 1 − 0.6·tread`) so they stop waving while flat. Blade width 0.06→0.045.
> - **Perf:** 266 FPS calm / 259 strong wind at bladeHeight 0.85 / 32 blades / radius 64 — the
>   3× vertex count cost nothing measurable, so the Phase 3 LOD bands stay unneeded for now.
>   Wind-0 stillness invariant **re-verified: 0/1.6 M bytes differ** (character-free view; with an
>   idle character in frame its own animation moves both body and grass, which is correct).

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

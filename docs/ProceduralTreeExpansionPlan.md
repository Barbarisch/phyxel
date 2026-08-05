# Procedural Tree Expansion Plan — Sharp Conifers + Enchanted Megaflora

_Status: Increment A DONE (2026-07-03, auditor-verified) — crisp engine + `pine`/`fir`/`edge=crisp`
spruce in `tools/gen_tree.py`; tests in `tools/test_tree_sharpness.py` (8/8). What the tests actually
guarantee, after two solution-auditor passes closed real holes:_
- _**Leaf-only** rim metric, **min across rows** (a single bubbly tier fails the whole tree — the
  averaging + occupancy-laundering holes are closed; `test_per_row_metric_catches_single_fuzzy_tier`
  proves it fires)._
- _**Bare-pole continuity** check for continuous cones (fir/crisp-spruce): no leaf-free gap >1 sub in
  the crown; `test_bare_pole_guard_is_falsifiable` proves it fires. Fixed the fir/spruce defect where a
  narrow upper cone collapsed into the plus-trunk — now a single-sub leader keeps the cone continuous
  (`crisp_cone` helper)._
- _**Live fullness**: wired to crisp_disc interior thinning; test asserts leaf-count strictly drops
  1.0→0.3 while the edge stays crisp (no longer a dead-parameter sweep)._
- _Exhaustively crisp for pine+fir at **every** height 4–40 (not just sampled). Red cases (fuzzy
  spruce/oak) stay red. Existing 21 library trees regen content-identical (LF/CRLF only)._

_Perf: shipped conifers use **fullness 1.0** so solid interiors compress to whole cubes — crisp
spruce_l = 2762 primitives (183C+2578S), lighter than the fuzzy spruce_l (3058). Verified live in
StructGenTest (FPS 235–362)._

_Grounding (grounding-auditor, 2 passes): whorl cadence relabeled — "one whorl/year" is cited (USFS
Silvics, Scotch Pine) but 1-cube spacing is stated as a voxel-legibility choice, not a 1 m/yr claim;
pine models Scots pine, fir models balsam fir (Abies balsamea, USFS 12–18 m); heights game-compressed
with stated rationale (render density + legibility). OPEN (NEEDS-RESEARCH, non-blocking): exact
closed-stand crown-width:height ratio for a single pinned silvics figure._

_Open follow-ups (non-blocking, both solution-audits PASS): (1) a **fully-dropped** pine whorl (vs a
partially-dithered one) is not caught — pine's inter-whorl gaps are intentional, so the continuity
check is skipped for it; add a pine-specific "no >2 consecutive missing whorls" check before any future
edit could silently drop a tier. (2) exact closed-stand crown-width:height ratios for pine (0.15) and
fir (0.125), and the pine crown-base fraction (0.45), remain generation choices labeled NEEDS-RESEARCH
rather than pinned to a single silvics figure._

_C++ `ProceduralTree` parity for pine/fir: DEFERRED (no biome uses procedural mode — `"pool"` default
stamps the .voxel templates). Owner track: flora/world-gen. Prereq: `tools/gen_tree.py`,
`engine/src/core/ProceduralTree.cpp`, `docs/WorldModel.md`._

## Goal

Two user-driven capabilities the current generator cannot produce:

1. **Sharp-edged conifers** — the current trees read as "bubbly"; real pines/spruces/firs have
   crisp conical silhouettes, tiered branch whorls, and a spire tip. The generator must be able
   to produce hard silhouettes, not only dithered blobs.
2. **Enchanted-forest megaflora** — a fantasy biome with *very* large trees (redwood-class and
   beyond: 60–120 m), glowing accents, and a giant-canopy + understory forest structure.

## Why current trees are bubbly (root causes, cited)

| Cause | Where | Effect |
|---|---|---|
| Dithered shell band on every canopy | `gen_tree.py:61` `SOLID, FUZZ = 0.92, 1.06` + `shell_keep()` (`:97`) | every silhouette gets a probabilistic fuzz rim — nothing can be crisp |
| Oak-family crown = union of ellipsoid blobs | `branched_crown()` + `leaf_cluster()` (`gen_tree.py:208,260`) | inherently round/lumpy — correct for oak, wrong for conifers |
| Spruce cone gets per-layer jitter + dither | `gen_spruce()` (`gen_tree.py:301`): `r += rng.uniform(-0.4, 0.4)` then `shell_keep` | the one conical archetype is deliberately fuzzed |
| Only one radius law per archetype | radius = f(height) hardcoded per `gen_*` | no way to express spire / columnar / tiered profiles |

## Hard engine constraints on giant trees (discovered, cited)

| # | Constraint | Where | Impact on a giant tree |
|---|---|---|---|
| 1 | **`kMargin = 12`** — a chunk only considers flora whose trunk column is ≤12 columns outside it | `ObjectTemplateManager.cpp:401` | any canopy with radius >12 cubes is **silently clipped at chunk seams** (chunks too far from the trunk never stamp their slice). Hard blocker. |
| 2 | Flora spacing is one value per biome (6–14 today) | `biomes.json` `flora.spacing`; local-maxima test `WorldGenerator.cpp:219` | giants need spacing 24–48 **and** a dense understory at spacing 4–6 in the same biome — impossible with one layer |
| 3 | Procedural-mode height is `base ± 2` hardcoded | `ObjectTemplateManager.cpp:440` (`% 5 - 2`) | no way to request an 80-cube tree procedurally; per-item height ranges needed |
| 4 | Procedural mode regenerates the whole tree **per clipping chunk** | `decorateChunk` → `ProceduralTree::generate` per placement per chunk | a giant clipping ~30 chunks regenerates ~30×; fine for h≤15 trees, needs a cache or pool mode for giants |
| 5 | C++ port parity is partial (oak/autumn/birch/bush/spruce/acacia/palm/dead; **no jungle/willow**) | `ProceduralTree.h:16` | new archetypes must be added to `gen_tree.py` always, and to C++ only if used in `"procedural"` flora mode |
| 6 | Render density is the engine's #1 open issue — **sub/micro faces are not greedy-merged** (cube faces are) | `docs/RenderOptimization.md` #40; tavern = 412k faces → 49 FPS | a dithered sub-resolution shell at canopy radius 20 cubes ≈ 60-sub radius ≈ 45k shell subs ≈ 10⁵+ unmerged faces **per tree**. Giant canopies must be cube-resolution shells with sparse sub/micro accents. |

Vertical extent is **not** a blocker: streaming loads a full sphere of chunks including Y
(`ChunkStreamingManager.cpp:75`), `planFlora` is column-based, and `decorateChunk` clip-stamps
per Y-slice deterministically — a 100-cube tree spans ~4 vertical chunks and each stamps its
slice. This must still be stress-tested (the y=31→32 seam is a known trap class).

## Grounding (1 cube = 1 m per `docs/structure-generation/DimensionReference.md`)

All shipped dimensions go through the **grounding-auditor** before regen; anchors to cite:

| Value | Real-world anchor (to be cited properly at implementation) |
|---|---|
| **Shipped** pine height 10/12/18 cubes; fir 14/18; crisp spruce 9/13 | Game-compressed for render density + legibility (same compression the oak/spruce library uses). Scots pine field 13.7–29 m, balsam fir 12–18 m (USFS Silvics: scotch-pine, balsam-fir). **pine_l=18, fir_m=14, fir_l=18 are inside the cited ranges; pine_s=10 + pine_m=12 are BELOW the pine floor (13.7 m)** — deliberately shortened past the real data for small/medium assets, stated as such in `tree_library.json` (not passed off as real young-stand heights). |
| Pine crown width = height × 0.30 (radius factor **0.15**); fir width × 0.25 (radius factor **0.125**) | narrow closed-stand conic form; qualitative (not pinned to one silvics figure) — flagged NEEDS-RESEARCH by grounding-auditor for an exact ratio |
| Tiered whorls, **one whorl per year** (cited), 1-cube tier spacing | USFS Silvics (Scotch Pine): "one whorl per year" is real; the 1-cube SPACING is a voxel-legibility choice, NOT a claim of 1 m/yr leader growth (real ~0.15–1.0 m, variable) |
| Bare lower trunk (pine crown base 45%; fir 22%) | closed-stand self-pruning keeps live-crown ratio ≥30% (pine); shade-tolerant firs hold crown to ≤30% base, balsam LCR ≥0.7 (USFS Silvics) |
| Redwood-class giant: height 60–115 cubes, trunk dia 4–8 | coast redwood (Sequoia sempervirens), tallest living ~115.9 m (Hyperion) |
| Sequoia-class giant: height ~80, trunk dia 8–11 at base | General Sherman: 83.8 m tall, base diameter ~11 m |
| Fantasy "world tree" cap: height ≤ 120, canopy radius ≤ 24 | engine-grounded: 120 = ~4 vertical chunks within default load radius; 24 = new margin cap (below), face budget (below) |

Fantasy sizes are expressed as **multipliers of the redwood/sequoia anchors**, capped by the
two engine-grounded ceilings (chunk streaming + face budget) — never invented free-hand.

## Design

### A. Silhouette-profile engine (makes sharpness *possible*)

Refactor canopy deposition in `gen_tree.py` around two orthogonal controls:

1. **`edge` parameter** — `fuzzy` (today's `SOLID..FUZZ` dither, default, unchanged output)
   vs `crisp` (keep iff `d <= 1.0`; no dither band, no per-layer jitter). Threaded through
   `shell_keep`/`ellipsoid_canopy`/`disc_canopy`/`leaf_cluster` and the cone loop.
2. **Radius profile** — per-archetype piecewise-linear `(height_fraction → radius)` control
   points replacing the hardcoded radius laws, so cone / spire / columnar / vase / umbrella
   silhouettes are all expressible. Existing archetypes keep their current curves as the
   default control points (regression: same seeds → same output).

New archetypes on top of it:

- **`pine`** — bare trunk to ~⅓–½ height, then discrete **branch-whorl tiers**: flat rings of
  short crisp fronds whose radius follows a linear cone profile, with 1-sub gaps between tiers,
  sharp spire tip. `LogSpruce` + `LeafSpruce` (materials exist; no texture work).
- **`fir`** — very narrow spire variant (radius factor 0.125, width ~0.25×height), continuous crisp cone, drooping
  tier tips (each tier's outer ring 1 sub lower than its root).
- `spruce` gains `edge: crisp` variants in the library (existing fuzzy ones stay).

### B. Megaflora capability (engine changes)

1. **Footprint-driven margin** — replace `kMargin = 12` with a value computed at decoration
   time: `max half-footprint across (loaded flora templates ∪ procedural max radius)`, cached on
   the template manager and refreshed on template load. Cap at **24 columns** (the grounded
   canopy ceiling); cost is a larger `planFlora` window per chunk — measure, it's hash-cheap.
2. **Per-item flora params** — biome flora items gain optional `heightMin/heightMax` (procedural
   mode) so a biome can ask for 60–90-cube redwoods; plumb through `FloraPlacement`.
3. **Flora layers** — `biomes.json` flora becomes a list of layers, each with its own
   `spacing/density/items` (giants at spacing 32, understory at spacing 5). Backward-compatible:
   a single `flora` object = one layer. The local-maxima test already runs per (biome, spacing);
   layers just run it per layer with a per-layer hash salt.
4. **Giants are pool-mode templates** (pre-generated `.voxel` via `gen_tree.py`), sidestepping
   constraint #4 (per-chunk regeneration). Procedural giants deferred until a (pos,seed)→template
   LRU cache exists.
5. **New giant archetypes** in `gen_tree.py`:
   - **`redwood`** — columnar crisp trunk 3–5 cubes dia (C-compressed), narrow high crown,
     cube-resolution foliage tiers.
   - **`elder_oak`** (fantasy) — sequoia-proportioned trunk (6–10 dia) with buttress root flares,
     `branched_crown` scaled up but with **cube-resolution leaf blobs** (see perf rule) and
     radius ≤ 24.

**Perf rule for giants (non-negotiable until BinaryGreedyMeshing ships):** canopy interiors AND
shells at **cube resolution** (greedy-merged path); sub/micro reserved for sparse surface accents
(≤ a measured budget). Gate: `get_render_stats` face count per giant measured at Increment B start;
a giant tree's face budget is set from that measurement against the 412k-faces→49-FPS tavern
datum, target ≥60 FPS with a full giant-forest scene on the dev 4090.

### C. Enchanted forest (content + biome)

1. **`EnchantedForest` biome** in `biomes.json` — climate cell carved from the wet-temperate
   range (verify no regression to Forest selection: nearest-centre test), or delivered as a
   **world-recipe preset** (`worlds` opt in; existing recipe machinery, no global default change).
   Decision point at implementation; recipe-preset is the safer default.
2. **Flora**: layer 1 = giants (`elder_oak`, `redwood`, glowing willow variant) at spacing
   ~32; layer 2 = understory (existing bushes/ferns + new `glow_bush`, `mushroom_giant`) at
   spacing ~5.
3. **Magic accents with existing materials** — `glow`, `glow_blue`, `glow_green` already exist:
   micro-sprig fireflies/lantern-fruit on canopy surfaces, glowing root veins on elder oaks
   (subcube veins on the C trunk), glow mushroom caps. **No new materials in v1** (new leaf
   colors = texture authoring + materials.json — deferred, listed as v2).
4. Demo world: recipe JSON + screenshot set via `/visual-test` flow.

## Increments (each: red test first → implement → stress → auditors)

### Increment A — sharp silhouettes (`gen_tree.py` only, no engine changes)
- **Red test**: a deterministic *edge-roughness metric* (per-layer silhouette radius deviation +
  count of dither-band voxels) run on current `tree_spruce_m` — shown failing a "crisp" threshold.
- Implement `edge` control + radius profiles + `pine`/`fir`; regression-test existing archetypes
  byte-identical for existing seeds; iterate shapes with `--preview`.
- **Stress axis (scale)**: pine at height 4 → 40 (grounded max), crisp invariant + connectivity
  (`prune_floaters` survivors) at every height; fullness 0.3 → 1.0 sweep must never break the
  crisp silhouette (fullness thins interior, not edge).
- Library entries (`tools/tree_library.json`) + Snow/Forest biome weights; regen; in-engine
  screenshot verification.
- Validation depth: **L2** (metric on real generator output) + L4 screenshot. C++ parity for
  `pine` ported to `ProceduralTree.cpp` only if a biome uses procedural mode with it (else logged
  as deferred in the plan's ledger).

### Increment B — megaflora engine capability

**Status (2026-07-03): CORE DONE + solution-auditor PASS.** Shipped & verified:
- **Footprint-driven flora margin** (the #1 blocker). `ObjectTemplateManager::decorateChunk` no longer
  uses a hardcoded `kMargin=12` that silently clipped any canopy wider than 12 cubes at chunk seams;
  it now uses `m_floraMarginColumns` = widest loaded template's half-footprint, clamped `[12, 24]`
  (`kFloraMarginCap`), updated per `loadTemplate` via `templateFootprintRadius`. Red→green proven by
  `tests/core/FloraMarginTest.cpp` (WideCanopyNotClippedAtChunkSeams: radius-24 per-chunk union ==
  whole-region reference, was RED −38 voxels at margin 12; GiantSpansVerticalChunksNoSeam: real
  120-cube `tree_redwood_xl` across a vertical chunk stack, exercises x/z margin + y-seam). Auditor
  reverted the fix to confirm both tests fail at margin 12, and loaded all 261 real templates → margin
  20 (footprint-driven, not a blanket cap). No regressions (9 failing tests are pre-existing on HEAD).
- **Giant archetypes** `redwood` (Sequoia sempervirens; xl=115 ≈ Hyperion 115.55 m, Sillett 2006) +
  `elder_oak` (fantasy world-tree: sequoia-SCALE trunk ≈ General Sherman's 0.13 dia ratio, but a
  deliberately fantasy-broadened crown ~1.6× Sherman's real crown ratio; glowing root veins).
  CUBE resolution (`cube_ellipsoid`/`cube_trunk_column`/`buttress_roots` → `fill_cube`) so the mesher
  culls interiors; canopy radius capped at `GIANT_CANOPY_CAP=24`. Library: `tree_redwood_l/xl`,
  `tree_elder_oak/_xl`. **Perf gate PASSED live**: 2 giants = 24 chunks, ~3001 visible faces, FPS
  391–530 — vs the 412k-face/49-FPS tavern datum (giants ~200× cheaper in faces). Each giant spans
  13–24 chunks with no seam clipping.

**Flora layers (B3) — DONE.** A biome now carries an optional `floraLayers` array (each band with its
own density/spacing/items/mode), placed by an independent per-layer local-maxima pass; the legacy flat
`flora` fields are "layer 0" (backward-compatible — absent = single-layer, existing worlds unchanged).
Additive across `Biome` (`extraFloraLayers`), `loadBiomes`, `floraCellLayer`/`planFlora`, and
`WorldRecipe` (persisted). Red→green: `tests/core/FloraLayersTest.cpp` (dense understory spacing 4 +
sparse giants spacing 24 → both pools appear, understory ≫ giants; RED before `floraLayers` parsed).

**EnchantedForest content (C) — DONE + verified live.** A new `EnchantedForest` biome (biomes.json) in
a narrow wet-temperate niche (temp 0.35–0.55, moist 0.90–1.0; centre 0.45/0.95 vs Forest 0.5/0.8 — a
selection check confirms Forest still wins its core + wet range, EnchantedForest only the very-wet
extreme). Layer 0 = dense glowing understory (`bush_glow_green`/`bush_glow_blue` — emissive `glow_green`/
`glow_blue`, generated via new `gen_tree.py --leaf` material override — plus ferns/leafy bushes/oaks);
layer 1 = sparse giants (`tree_elder_oak`/`tree_redwood_l`, spacing 26). Verified live: giant towering
over a floor of luminous glow bushes + understory, FPS 114–158, no seam clipping.

**Audits (both closed):**
- _solution-auditor on layers/enchanted:_ layers feature CONFIRMED real (red-green with teeth,
  backward-compat, independent per-layer placement). Caught a **critical bug**: EnchantedForest was
  UNREACHABLE — biome selection is nearest-centroid on (temp,moisture) and the noise field only spans
  ≈[0.21,0.79] on each axis, so the original moist-0.95 centre was selected 0× over ~1.5M samples (my
  earlier live shot was a *temporarily-widened* version — the shipped narrow niche was dead content).
  **Fixed:** retuned to a reachable cool-wet niche (temp 0.25–0.45, moist 0.62–0.82; centre 0.35/0.72),
  proven by `tests/core/BiomeReachabilityTest.cpp` (samples the real `sampleSurface` selection path;
  EnchantedForest 0→653 selections; Forest core still selects Forest). Re-audited PASS with teeth
  (revert to moist 0.9 → test fails "unreachable, count 0"). The test also surfaced a **pre-existing**
  unreachable biome — **Desert** (centre 0.8/0.175, outside the achievable range — deserts never
  generate); documented + allowlisted, not fixed here (separate world-gen concern).
- _grounding-auditor on giants:_ redwood_xl=115 ≈ Hyperion (115.55 m, Sillett 2006) and elder_oak
  trunk factor 0.13 ≈ General Sherman's 0.1325 → GROUNDED. Caught a MISMATCH: elder_oak crown factor
  0.32 is ~1.6× Sherman's real crown ratio (0.19). **Fixed the claim** (not the geometry — a
  world-tree reads as broad): docstring/comment now state the crown is *intentionally fantasy-broad*,
  not sequoia-proportioned. Unsourced redwood crown-ratio / self-pruning values relabeled as
  stylizations (NEEDS-RESEARCH), matching the Increment-A discipline.

**Crown + canopy quality pass (user feedback, solution-auditor PASS):** the original giant crowns
were a few separate `cube_ellipsoid` lobes that read as distinct floating spheres, and giants placed
too sparsely to form a canopy. Fixed: a `dense_crown` helper builds one SOLID oblate ellipsoid (the
gap-free canopy body) plus many small bumps straddling its rim/top — every bump overlaps the base, so
the union is one cohesive canopy (auditor's 6-connected-component scan: 99.75–99.97% one mass across
many seeds). elder_oak crown flattened+widened (rv=cr·0.42, spreads horizontally); EnchantedForest
giant spacing tightened 26→18 so the wide crowns overlap into a continuous canopy. Verified live: a
top-down view shows a closed canopy, an under-canopy view shows trunks rising into overlapping crowns;
dense forest = 39k faces / FPS 126–180 (vs the 412k tavern). Footprint cap held (giants ≤24), all 4
flora/reachability tests still pass.

**Wide-short-dense pass (user feedback, solution-auditor PASS after a caught regression):** giants
remade wide + short + hollow for a dense enchanted canopy. `kFloraMarginCap` 24→40 (engine) +
`GIANT_CANOPY_CAP` 24→40 so crowns can be ~74 cubes wide; heights halved (elder_oak 32/38, redwood
46/56); `cube_ellipsoid` gained a `hollow` param and `dense_crown` builds hollow shells (~3-4× cheaper
to stamp — critical for dense placement); EnchantedForest giant spacing 36→24 (density 1.0) so the wide
crowns overlap into a **closed canopy**. View distance raised: `loadDistance` 160→320, far plane
`maxChunkRenderDistance` 256→512. Verified live (bounded world): top-down shows a near-solid closed
canopy, FPS 76–269, ~33k faces. **Perf lesson:** streaming with a large `loadRadius` + heavy giants
thrashes the gen pump (FPS 3) — heavy content wants a bounded world or a modest load radius; render
itself is cheap (surface-only). **Caught + fixed a determinism regression:** the earlier `--leaf`/`--log`
override had appended materials to the RNG seed key unconditionally, silently reshaping every default
tree on the next `--batch`; fixed to append only on override, re-verified byte-identical regen of the
committed library (auditor PASS).

**Branch-driven giant crowns (user feedback, solution-auditor PASS):** the giants were "thin trunk +
solid ellipsoid blob," which reads as wrong. Rebuilt them as real trees: new `cube_grow_branch`
(recursive cube-space limb, **radius-clamped to `max_r`** so branches can't escape the footprint cap —
the first attempt hit 126×155 before the clamp) + `cube_branched_crown` (major limbs fork off the upper
trunk, recurse, and a leaf blob is dropped at every branch cluster; blobs clamped inside crown_r). Trunks
thickened (elder_oak dia factor 0.13→0.34, redwood 0.05→0.18). Result: a thick bole splitting into
visible limbs, each carrying a foliage cluster, wood showing in the gaps — leaves-on-branches, not a
sphere. Footprints ≤40 (half 23–29), crowns 99.7–99.9% one connected component, determinism intact,
4/4 flora + 8/8 sharpness tests pass, FPS 364 single-tree. Heights came out ~55–76 (branches add
vertical reach — a touch taller than the earlier solid wide-short versions; trim via library heights if
"shorter" is reprioritized).

**Fuller 3D branching (user feedback, solution-auditor PASS):** the branch armature fanned out in a
flat skirt at the crown base. Rewrote `cube_branched_crown` to aim limbs at points across a DOME
surface (varied azimuth AND elevation) with more limbs (9–14), so branches climb into the crown volume
and carry foliage at many heights. Added a `max_y` VERTICAL clamp to `cube_grow_branch` (a bug made
up-biased branches shoot to y=126+; the auditor reproduced this — disabling `max_y` → H 123–147, fix →
66–88). Footprints ≤40, crowns 99.75–99.90% connected, determinism intact, 4/4 + 8/8 tests pass.
Confirmed via a bare-skeleton render (foliage stripped): branches fork and climb through the crown.
Cost: heights ~66–88 (branches add vertical reach); the wide/short vs branch-fullness trade is tunable
via library heights + `crown_h_frac`.

**Render distance doubled (user request):** the *actual* default was `Application::maxChunkRenderDistance
= 96` (far chunks frustum-culled beyond it) — earlier `EngineConfig`/`ChunkManager` edits were dead
config (their setters aren't wired). Doubled the real knob 96→192 (chunkInclusionDistance 128→288),
`ChunkManager::loadDistance`→256 so streamed chunks load within render range. Verified live: a 49-chunk
world renders 38 visible chunks to the horizon (was ~9–12 at 96).

**Remaining (non-blocking):** per-item procedural height range (B2 — deferrable). Pre-existing
follow-ups: (1) Desert biome unreachable; (2) `buttress_roots` tiny ground fragments; (3) a C++
crown-connectivity test; (4) streaming + heavy giants is gen-bound; (5) EngineConfig's rendering fields
are loaded/saved but never applied to the coordinator (dead config — wire or remove).


- **Red tests**: (1) unit test stamping a radius-20 template via per-chunk `decorateChunk` over a
  region vs one whole-region reference stamp — shown **losing voxels** with `kMargin=12`;
  (2) procedural item with `heightMax: 80` shown clamped to base±2.
- Implement footprint-driven margin, per-item height range, flora layers; add `redwood` +
  `elder_oak` archetypes (pool mode).
- **Stress axes**: seam-identity test at radius 24 across all chunk-permutation orders; a 120-cube
  tree across 4 vertical chunks (y-seam invariant: stamped voxel set identical to reference);
  count axis 1 → 25 giants with `get_render_stats` FPS/face curve recorded (the perf gate).
- Validation depth: **L2** (seam-identity + connectivity validators) + **L4** (live streamed world
  fly-through, FPS evidence). Grounding-auditor on every shipped dimension; solution-auditor on
  the seam fix.

### Increment C — enchanted forest content
- Recipe preset + biome/layers JSON, glow accents, demo world.
- **Red test**: recipe applied → validator asserts both layers present at expected densities
  (chi-squared-ish count check over a fixed region), zero clipped canopies (seam validator from
  B re-run on the real world).
- **Stress axis**: full load-radius giant forest at recipe densities — FPS + chunk-generation
  time budget (decoration must not blow the streaming pump budget; measure `decorateChunk` ms).
- Validation depth: L2 + L4 with screenshot set.

## Non-goals / deferred
- Sub/micro greedy meshing (render-perf track owns it; giants get cheaper for free when it lands).
- Procedural-mode giants (needs the generate-cache; pool templates cover v1).
- New leaf/glow materials + textures (v2 of enchanted content).
- `jungle`/`willow` C++ parity (pre-existing gap, unchanged).

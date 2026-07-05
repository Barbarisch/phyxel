# tree_forge — unified multi-resolution procedural tree generator

Ground-up replacement for the bespoke per-archetype functions in `gen_tree.py` (which were hand-tuned
magic constants — the "LLM driving a simple algorithm" trap). **One algorithm**; archetypes are
parameter presets; detail and resolution fall out of the algorithm.

## Architecture (`tools/tree_forge.py`)

1. **Space-colonization skeleton** (`grow_skeleton`) — branches grow from the root toward attractor
   points scattered in a tunable canopy **envelope** (dome/cone/column/sphere + size). Produces a
   genuinely recursive trunk → limbs → branches → twigs; branch COUNT is set by attractor density,
   not a hand-placed limb count. Radii via Murray's law (`assign_radii`, affine-remapped so the trunk
   hits `trunk_r` while twigs stay at `r_min`).
2. **Multi-resolution voxelization** (`rasterize_branches` + `_res_for_radius`) — each segment is
   rasterized at the resolution its RADIUS earns: thick → whole **cubes**, medium → **subcubes**,
   thin → **microcubes**. A limb tapers cube→sub→micro along its length.
3. **Hierarchical emit** (`emit`) — everything lives in a micro grid (9 micro/cube/axis); on output,
   729 same-material micros in a cube collapse to one `C`, 27 in a subcube to one `S`, else `M`. So
   thickness auto-picks resolution with no per-type switch. (`test_tree_forge.py`, 5/5.)
4. **Foliage** (`add_foliage`) — leaf clusters on the thin twig tips at `leaf_res` (the perf lever;
   wood drawn first wins, so twigs poke through — leaves ON branches, not a shell).

## Status (2026-07-04) — foundational milestone, NOT finished

**Proven (in-engine, screenshotted):**
- The **skeleton is a real recursive tree** — trunk tapers, splits into major limbs, forks into
  progressively smaller branches/twigs filling a 3D crown. This was the core goal.
- **Dense forest renders at ~110 FPS** — 54 forest-profile trees (subcube leaves, ~18k primitives
  each) on the 4090. Dense-forest feasibility = YES.
- **Thick tapered trunk** (after the affine radius remap + `trunk_r` bump).
- Multi-res works: trunk→C, branches→S, twigs→M all in one tree.

**Perf envelope:** full-detail hero giant (h40, thin twigs + full leaves) ≈ 144k primitives; small
forest oak ≈ 8–20k. S and M are NOT greedy-merged (only C is), so **leaf resolution + attractor
count are the perf dials**: `leaf_res=cube` + fewer attractors for forest-fill; `leaf_res=sub/micro`
+ more attractors for hero trees. This is the sweet-spot lever the user asked for (detail is a
parameter, not a hardcoded skip).

**Open / needs steering:**
- **Default leaf density** — very sensitive to `leaf_below_r`; oscillated between bald and 144k.
  Needs a good per-archetype default (aesthetic call).
- **Trunk roundness** at cube res is still blocky for small trees (inherent: a <1-cube-radius trunk
  is 1–2 cubes). Subcube trunks would round it at a perf cost.
- **Archetype visual verification** — pine/conifer, birch, redwood presets exist but only oak +
  elder_oak have been eyeballed.
- **Wiring into flora/biomes** — `tree_forge` output isn't yet used by `WorldGenerator` flora; the
  old `gen_tree.py` templates still ship. Keep both until forge is proven, then migrate.
- **Solution-auditor + grounding** pass on the shipped defaults (per discipline) — not yet run.
- **Generator speed** — space colonization is O(attractors × nodes)/iter; a hero giant takes ~10s.
  Fine for offline template baking; add a spatial grid if it needs to be faster.

## Params (all tunable; presets in `PRESETS`, defaults in `default_params`)
`envelope`, `canopy_r/h`, `attractors` (detail/perf dial), `up_tropism` (leader strength: high=conifer
spire, low=spreading oak), `jitter`, `crook` (trunk/branch wander, 0=straight..1=gnarled), `trunk_r`,
`round_trunk` (cube vs subcube trunk), `r_min`, `murray_n`, `leaf_r`, `leaf_below_r`, `leaf_density`,
`leaf_res` (perf lever 9/3/1), `root_flare`/`root_flare_h`/`root_count`/`root_len` (exposed root
splay — on for every tree), materials.

## Profiles / tiers (baked, user-chosen defaults 2026-07-04)
- **`tier="forest"`** (default): medium canopy + **cube trunk** (cheap, greedy-merged) — the standard
  workhorse. oak ≈14–19k, pine ≈15–22k primitives. Proven 50+ per forest at ~110 FPS.
- **`tier="hero"`**: **round subcube trunk** + denser leaves + more attractors — a few per scene.
  oak ≈60k, pine ≈42k.
- **Presets** = archetype feel: `oak` (broad dome, weak leader, gnarled), `pine` (conical, strong
  leader, straight, spruce mats), plus `birch`/`redwood`/`elder_oak`. Verified in-engine: oak vs pine
  read as clearly distinct; **root flare visible** (base splays wider than the straight trunk).

## Session-2 additions (2026-07-04)
Added `crook` (trunk lean via damped random walk + branch wander), `round_trunk`, the `forest`/`hero`
tier system, and **exposed root flare** (`add_roots` + elephant-foot base widening) on every tree.
Comparison matrix (sparse/medium/dense canopy, straight/gnarled/round trunk, oak/pine) shown to user;
they chose medium + both trunk tiers. **Still open:** crook reads as blocky *steps* on a cube trunk
(smooth it, or round_trunk); root spurs are a bit blobby (distinct ridges would be nicer); wire the
baked profiles into `WorldGenerator` biome flora (replacing the `gen_tree.py` pool); auditor pass.

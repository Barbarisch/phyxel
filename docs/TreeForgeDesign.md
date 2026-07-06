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
2. **Multi-resolution voxelization** (`rasterize_branches` + `_res_for_radius`) — wood rasterizes
   at **subcube** resolution (twigs at **micro**); `emit` re-compresses solid interiors to whole
   cubes. Resolution is therefore **per-voxel by surface proximity** — a thick trunk = cheap C
   interior + subcube surface shell (slopes, not 1-cube stairsteps). (Reworked 2026-07-05 after
   the user caught trunks rendering as pure full cubes; the old per-segment cube path and the
   `round_trunk` flag are gone — trunks are round at every tier. `test_thick_trunk_has_subvoxel_shell`
   + `test_real_trunk_band_has_subcube_shell`, red-first. Cost: hero oak 35361→36217 prims, +2.4%.)
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

## Roadmap — forge as THE standard tree generator (agreed with user 2026-07-05)

tree_forge becomes the engine's standard procedural tree/organic generator, **pool-mode first**
(Python bakes templates offline; no C++ port until a biome actually needs runtime-unique trees —
today none uses `procedural` mode). Migration gate: every archetype in `tree_library.json` has a
forge equivalent that survives visual A/B — not merely "forge exists".

1. **Fix known bugs** (found in review 2026-07-05) — **DONE 2026-07-05**, red-before-green
   (`test_tree_forge.py` 14/14; the 3 new tests shown failing first: 6129 ground-leaf micros,
   21870 below-ground micros, CLI crash). Also found+fixed (d): `os.path.relpath` crashed the CLI
   whenever `--out` was on a different drive than the repo (Windows). Root dip softened to
   0.05–0.3 so root ridges survive the ground clip. Baked forest vs hero oak now differ
   (16068 vs 35361 primitives) with tier recorded in the provenance header.
   (a) CLI `--tier` is parsed but never passed to `build_tree` (tree_forge.py:476) — hero tier
   unreachable from the command line; also record tier + attractor overrides in the `# generator:`
   provenance header.
   (b) `add_foliage` runs over root-spur nodes: root tips taper below `leaf_below_r` AND have empty
   `children` (density roll never applies) → every tree grows always-on leaf blobs at/below ground
   level around its base (measured: 5/10 root nodes on a stock oak, y ≈ −0.5..−0.7). Likely much of
   the "blobby roots" complaint. Fix: tag root nodes, skip in foliage.
   (c) `emit` rebases to the lowest voxel, so below-ground roots/leaves lift the trunk ~1–1.5 cubes
   above the template floor — at placement the ground-contact point is a leaf blob, inverting the
   "roots dip into the ground" intent. Needs an explicit ground-plane anchor in the header or emit.
2. **`forge_library.json` batch mode** mirroring `gen_tree.py --batch` — reproducible full-library
   regen with provenance. **DONE 2026-07-05**: `tree_forge.py --batch tools/forge_library.json
   [--outdir DIR]`; per-entry provenance headers (tier + attractor flags + JSON overrides line);
   byte-identical regen pinned by `test_batch_mode_bakes_manifest` (red-first). Starter manifest
   ships 9 entries (giants carry `leaf_res: 9` per the megaflora perf rule); actual bake into
   resources/ is step 4.
3. **Close archetype gaps** — user decisions 2026-07-05: **organic/natural is the goal, crisp
   silhouettes explicitly de-prioritized** (no whorl mode; the Increment-A crisp-conifer
   aesthetic is NOT a migration requirement — forge pines/firs are tuned natural cones), and
   the retirement gate is **FULL parity**: every gen_tree archetype needs a forge equivalent —
   oak, autumn, birch, spruce, pine, fir, acacia (umbrella envelope), palm (frond crown),
   willow (weeping envelope), jungle, dead (leafless), bush + glow-bush variants, redwood,
   elder_oak. Each verified visually in-engine before step 4 retires gen_tree.
   **DONE 2026-07-05 (first pass):** all 14 archetypes shipped as presets — new `umbrella` +
   `weeping` envelopes, `add_fronds()` palm crown (per-node material override in the
   rasterizer), `crown_y_frac`/`attractors_mult` preset keys. Red-first structural tests
   (acacia flat crown + bare bole, willow droop, palm bare-trunk frond reach, dead leafless,
   bush low+leafy, per-preset material guard) — suite 22/22. Full 21-entry library baked via
   --batch; 8 new archetypes visually surveyed in the asset editor: acacia/willow/palm/dead/
   glow-bush/spruce/jungle read correctly; minor polish candidates: fir mid-cone patchiness,
   willow curtain a bit monolithic. (Engine gap hit + logged in StructurePipelineGaps.md:
   asset-editor crashes after ~9 hot-reloads.)
4. **Bake + wire**: full forge library into `biomes.json` pool flora side-by-side with gen_tree,
   A/B per biome in-engine, then retire gen_tree in one commit. Giant-tier presets must ENFORCE
   `leaf_res=cube` (the megaflora perf rule) rather than rely on remembering.
   **DONE 2026-07-05 (first pass):** 29 templates baked into resources/ (giants at leaf_res=9;
   xxl sizes match gen_tree scale — redwood_xxl 39x72x42 vs old 41x74x41); every biome flora
   pool swapped to forge_* (hand-made tree_apple/bush_flower/fern kept); forge_enchanted_oak
   added to the EnchantedForest giant layer (masked-emissive trunks now in world gen);
   gen_tree.py marked DEPRECATED (file + old templates stay — existing worlds' persisted
   recipes reference them). Verified live in a fresh LodTest streaming world: oaks stamp
   grounded on slopes (no floating/leaf blobs), spawned redwood_xxl towers correctly.
   **Perf datum:** one redwood_xxl ≈ +40k visible faces (81k total vs 25k baseline; Debug FPS
   unchanged ~24). CAVEAT: old cube-shell giants were ~3k faces — forge giants are ~13x
   heavier (subcube trunk/branch shells). EnchantedForest giant spacing 24 could reach
   tavern-class face counts (412k→49FPS datum) in Release worst case — if it does, options:
   widen giant spacing, coarse-shell override for giant boles, or wait for greedy meshing
   (#40). Verify with a Release EnchantedForest flythrough before shipping a demo.
   **Verification lessons:** /api/world/generate does NOT run flora decoration (streaming is
   the decorated path); a no-project empty world has no recipe so streaming never generates —
   use a --project launch with a fresh worlds/default.db. Forge-verified world kept at
   LodTest/worlds/forge_flora_test.db.
5. **Extract `MicroVoxels` + `emit()`** as the shared substrate for future organic generators
   (rocks, vines, roots, stalagmites) — the multi-res emit is the reusable standard, forge is its
   first client. **DONE 2026-07-05:** `tools/forge_core.py` = MicroVoxels canvas + hierarchical
   C/S/M emit + `rasterize_capsule`/`rasterize_sphere`/`fill_voxel` grid primitives (with the
   determinism contract documented in its docstring); tree_forge imports/re-exports it.
   Proven a PURE refactor: `test_substrate_extraction_is_pure` rebakes forge_oak_m through the
   extracted code byte-identical to the committed template (suite 23/23). Future generators
   (rock_forge, vine_forge, ...) start from forge_core, pick a grid one level finer than their
   bulk, and get cube-cheap interiors + fine surface shells for free.

**ROADMAP COMPLETE (2026-07-05).** tree_forge is the engine's standard tree/organic generator.
Remaining parked work: the foliage-appearance track below (cutout leaf masks, foliage shadow
pass), fir mid-cone patchiness + willow curtain uniformity polish, the EnchantedForest giant
Release-build perf flythrough, and the asset-editor reload-crash engine bug
(StructurePipelineGaps.md).

## Foliage appearance track (user vision 2026-07-05 — parallel to the roadmap above)

Forge decides WHERE leaf voxels go; this track decides what a leaf voxel LOOKS like. They meet
only at `leaf_res`. Aim lofty, optimize later (user directive).

- **Today**: leaf cards are a procedural ellipse discard (`foliage.frag:20`) — flat circles
  sampling the leaf texture for colour only. All cutout machinery (discard, per-card hash,
  wind) already exists; only the SHAPE is primitive.
- **Tier 1 — voxel-native cutout masks**: per-species alpha masks (oak lobed cluster / birch
  sparse / spruce needle tuft / jungle frond) in the leaf textures' alpha channel (atlas already
  loads RGBA; BC7 carries alpha). Author on a chunky hard-edged pixel grid — or GENERATE masks by
  rendering micro-voxel leaf clusters ("leaf_forge") — so the see-through gaps read as voxel-shaped
  holes, leaning into the engine aesthetic. Multiple variants per species, picked by the existing
  per-card hash.
  **DONE 2026-07-05:** `tools/leaf_forge.py` bakes deterministic 32×32-cell cluster masks into
  all 30 leaf source PNGs' alpha (RGB untouched; coverage oak ~52% / spruce ~38% / jungle ~39% /
  birch ~25% of the card); `foliage.frag` alpha-tests texel.a<0.5 (ellipse discard deleted);
  `foliage.vert` adds a per-card flip/swap variant (8 orientations × 6 face textures = 48 mask
  variants per species). BC7 alpha confirmed end-to-end (AtlasManager encodes RGBA; cache
  re-keys on source hash). Verified live at three ranges in the forge world: close = real
  see-through leaf clusters w/ serrated edges (bilinear + 0.5-test rounds the chunky cells
  nicely over the painted albedo), mid = fuller ragged canopies vs the old ovals, far = NO mip
  balding. Debug FPS inside a canopy ~14 (discard overdraw) — check Release before worrying.
- **Tier 2 — canopy lights like a volume**: card normal = direction from cluster/crown centre
  (volume shading), sun-behind-leaf transmission, interior darkening. **Foliage shadow pass** —
  currently canopies cast NO shadows (mesher skips solid leaf faces; no foliage shadow pipeline;
  trunk-only shadows — arguably a live bug). `foliage_shadow.vert` + same alpha discard = dappled
  light.
- **Tier 3 — lofty**: per-species card geometry + per-material card size/count (now global push
  constants), flutter/gusts/leaf-fall, distance impostors + alpha-to-coverage (explicitly
  deferred perf work).

## Session-2 additions (2026-07-04)
Added `crook` (trunk lean via damped random walk + branch wander), `round_trunk`, the `forest`/`hero`
tier system, and **exposed root flare** (`add_roots` + elephant-foot base widening) on every tree.
Comparison matrix (sparse/medium/dense canopy, straight/gnarled/round trunk, oak/pine) shown to user;
they chose medium + both trunk tiers. **Still open:** crook reads as blocky *steps* on a cube trunk
(smooth it, or round_trunk); root spurs are a bit blobby (distinct ridges would be nicer); wire the
baked profiles into `WorldGenerator` biome flora (replacing the `gen_tree.py` pool); auditor pass.

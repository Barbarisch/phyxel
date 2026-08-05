# World Rendering v2 — continuous LOD to the horizon, natural light

**Written 2026-08-02, after the user set the bar** (verbatim): *"what i want is a smooth, scaling
transition. so if a tree costs 1000 voxels up close, as you get away it would step cleanly
between 950 voxels, 900 voxels, 850 voxels…"* — *"i expect to be able to see much more of the
world at all times"* — *"much more natural lighting"* — *"we need to aim for more."* The
reference is the class of large-map voxel-engine demos (multi-kilometer view, no visible LOD
seams, GI-like soft lighting).

This plan replaces the far-field patchwork accumulated so far. It keeps what earned its place
and names what failed and why, so we do not rebuild the same dead ends.

---

## 0. What the demo-class engines actually do — and the four things we're missing

User (verbatim): *"I just feel like we are missing some trick or optimization that allows for
truly detailed large worlds… dense forest with lots of vegetation, with no lag or visual
artifacts. what are we doing wrong."*

There is no single trick; there are four, and the demos that look like magic use most of them
together. Grounded in the public engineering record (Vercidium's voxel optimization writeups,
John Lin's engine essays, GPU-driven rendering literature) plus first principles:

1. **The world is a voxel MIP pyramid, not a pile of full-res meshes.** The whole world exists
   (or is derivable on demand) at voxel sizes 1, 2, 4, 8, 16… Rendering picks the level whose
   voxel ≈ one pixel. LOD is then *inherent and continuous* — not a bolted-on far system.
   Raster engines mesh each region at its level; raymarch engines descend the hierarchy per
   pixel and stop when a node is sub-pixel. **Us today:** exactly one meshed resolution (L0)
   plus two unrelated bolt-ons (heightmap tiles, cards) that share nothing with the near field.
2. **Repeated content is stored once and referenced everywhere.** Dense vegetation is the
   showcase *because* it is repeated: a forest of template trees is a few kilobytes of unique
   data plus positions (DAG subtree sharing in raymarchers; instanced per-template LOD meshes
   in rasterizers). A million trees ≈ the cost of twenty. **Us today:** every stamped tree is
   re-meshed into its chunk's buffer as unique geometry; leaf cards emitted per subcube.
3. **GPU-driven submission.** One persistent mega-buffer, compute-shader culling + LOD
   selection, a single indirect draw (`vkCmdDrawIndexedIndirectCount`-class). Draw count stops
   scaling with world size. **Us today:** one CPU `vkCmdDraw*` per chunk / far tile / card
   batch — ~500+ CPU draws at the horizon and it grows with every tier we add (this same
   pattern is why the shadow pass costs 24 ms).
4. **Lighting comes from the voxel structure, not from range-limited passes.** Sun visibility
   and sky occlusion are sampled *from the mip pyramid itself* (a coarse ray/cone into level-N
   voxels answers "is this point in shadow / how open is its sky?" at ANY distance), plus
   aerial perspective and tone mapping. That is why those forests look naturally lit to the
   horizon: one lighting model, evaluated against one world structure. **Us today:** shadow
   maps that end a few hundred units out, baked light only where chunks are resident, flat
   albedo beyond — three lighting regimes stitched together.

The strategic conclusion: **§3's template mip chains + instanced tier are precisely tricks
1+2 in raster form** — the plan stands, now explicitly aimed at this ceiling — and two
additions are promoted into the roadmap: **GPU-driven culling/submission** (trick 3, new M6)
and **pyramid-sampled far lighting** (trick 4, extends M3; long-term: evaluate a raymarched
far-field compute pass over generator-derived brick mips as a tech spike, M-spike, before any
commitment — that is the John-Lin-style endgame and must be measured on our hardware, not
assumed).

## 0.5 Case study: the "Knightland" demo, reverse-engineered (2026-08-02)

The user supplied a free demo (`D:\vr\knightland`) representative of the look we're chasing.
Teardown findings (from its build manifests, asset catalog, and shipping-binary strings — no
code decompiled):

- **It is not a voxel engine.** It is a ~583 MB standard **Unreal Engine 5** game: `Engine/` +
  IoStore paks, D3D12, five small handcrafted maps (`arena_map`, `tutorial_map`, …), UE
  Landscape terrain (~164 asset refs), and **hand-authored low-poly trees**
  (`KnightLand_assets/foliage/low_Tree_1/2/3`) with pixel-art textures (`grass_pixel`). The
  "voxel style" is an *art direction* — blocky meshes + pixel textures — not voxel data.
- **The vegetation density trick is UE's instanced foliage** (painted instances of a handful of
  authored meshes; GPU instancing + automatic per-instance mesh LOD + distance culling for
  free). That is trick #2 from §0, productized.
- **The "realistic lighting" is the stock UE5 stack**: the shipping binary carries the full
  Lumen GI subsystem (`LumenGI`, `LumenCardSharing`), atmosphere/tonemapper, and **TSR /
  TemporalAA / FXAA**. Soft GI + filmic tonemap + *temporal anti-aliasing smoothing every
  transition* is most of the perceived quality.
- No voxel plugin, no PCG, no world partition/HLOD — the world is small and handcrafted, not
  multi-kilometer procedural.

**What this changes for us:**
1. **The bar we're chasing does not require an exotic raymarcher.** It is instanced repeated
   meshes + mature lighting + temporal AA. That is precisely M1–M3 — with one addition this
   teardown exposes: **we have NO anti-aliasing pass at all.** Hard voxel edges alias and
   shimmer at every distance, and that raw edge crawl is a large share of the "ours looks
   rough" delta. AA (TAA-lite or at minimum FXAA, evaluated against our reverse-Z depth)
   joins M3 as a first-class deliverable.
2. Our problem is still *harder* than theirs in one honest way: their trees are authored
   300-triangle props; ours are true editable voxels. The template mip chain (M1) is the
   true-voxel equivalent of their authored LOD chain — same economics, kept voxel-native.
3. Their world is small; ours is unbounded procedural. The levers that transfer are the look
   levers (instancing, GI-ish ambient, fog, tonemap, AA) — the M-spike raymarcher remains the
   *beyond-this-bar* option, not a prerequisite for it.

## 1. Where we actually are (honest audit, 2026-08-02)

| Band | Representation today | Verdict |
|---|---|---|
| 0–320 u (residency) | Full voxel chunks + leaf-card foliage + grass blades | Good. This is the look the far field must *degrade from continuously* |
| 320–360 u | **The deadzone**: real trees gone (chunks evicted), impostors still fading in at half scale | Rejected by user — reads as a hole ringed by shrunken trees |
| 360 u–2 km | Billboard impostor cards on far-terrain tiles | Works mechanically; **wrong aesthetic** — paler, flatter, obviously a different renderer |
| 2–4 km | Far-terrain heightmap only, flat-lit | Empty of objects; lighting doesn't match near field |
| Anywhere far | Chunk-squash LOD (`EvictedLodCache`) | **QUARANTINED** — OR-occupancy squash turns canopies into floating cells ("weird floating voxels"). Kept default-off for *saved structures* only |

Two structural sins cause everything the user rejects:

1. **Renderer swaps instead of decimation.** A tree is voxels, then suddenly a card. Any two
   renderers will disagree in color, lighting, and silhouette; the seam between them is
   unhideable. The fix is not calibration — it is *one renderer whose input gets smaller*.
2. **Lighting is per-tier, not per-world.** Near voxels get baked skylight + shadow maps;
   far tiles get flat albedo; impostors get a hardcoded gradient. Real engines make distance
   itself a lighting effect (aerial perspective), which *unifies* tiers instead of exposing them.

## 2. North-star principles (the invariants this plan optimizes for)

- **P-ONE: One representation family.** Everything solid renders as lit voxel geometry at some
  resolution. Billboards may exist only past the point where a whole tree is ≤ a few pixels —
  and must be indistinguishable at that size.
- **P-CONTINUOUS: No band may pop.** Every LOD step is either sub-pixel at the distance it
  happens, or hidden by a dithered cross-fade (screen-door alpha between adjacent levels).
  "Deadzone" is a bug class, not a tuning issue: **at every camera distance, the expected tree
  crown coverage is continuous** — and we will *measure* that (see §6).
- **P-LIT: One lighting model, evaluated everywhere.** Sun + hemispheric sky ambient + aerial
  perspective applied to every tier from the same constants. A far tree must be a darker,
  hazier version of the same tree, never a differently-shaded object.
- **P-DERIVED: Far content comes from the generator, not from residency history.** (The one
  lesson of the eviction cache that stands: deterministic derivation — `planFlora` — is the
  right source. Chunk-history-driven LOD is not, for generated content.)

## 3. The architecture

### 3.1 Tree/object LOD: template voxel mip chains, rendered instanced (THE core ask)

Trees are template stamps at deterministically-known positions. That makes the user's
"1000 → 950 → 900 voxels" vision *cheap*, because decimation can be done **once per template,
not per tree**:

- **Build a voxel mip chain per tree template** (at template load / first use, cached to disk):
  `L0` = the authored template (microcube/subcube detail), then `L1, L2, L3…` — each level a
  voxel-space downsample at ~70% linear resolution per step (so voxel *count* falls roughly
  smoothly: 1000 → ~700 → ~490…, honoring the user's "clean steps" intent with useful spacing).
  **The downsample operator is the make-or-break piece** and must be tree-aware — the exact
  place the old squash failed:
  - *majority-material color*, not OR-occupancy (no more solid white/leaf slabs);
  - *support-aware*: never emit a filled cell whose column to the trunk/ground is empty
    (kills floating canopy debris **by construction**);
  - *silhouette-preserving*: canopy boundary cells keep partial-coverage → dithered presence
    instead of hard swelling (bounds the "fattening" defect);
  - *trunk-preserving*: the trunk column is always kept at ≥1 cell so no lollipop-without-stick.
- **Render distant trees as instanced LOD meshes**: per (template, level), one static mesh
  (built by the existing greedy face emitter); per far-terrain tile, instance lists from the
  deterministic plan (already built for impostors — positions, species, jitter). One instanced
  draw per (template-level, tile-batch). This is real 3D geometry: it shadows itself with the
  vertical gradient of the *actual shape*, silhouettes rotate correctly as you move (billboards
  never did), and it eats the same lighting as near chunks.
- **Level selection by projected voxel size** (screen-space, reuse `LodService`), with
  hysteresis; **dithered cross-fade** over ~15% of each band so steps never pop.
- **The handoff at the residency edge becomes exact**: the L0/L1 instanced tree *is* the same
  template the chunk stamped, at the same position. When the chunk evicts, the instanced tier
  takes over with sub-voxel visual delta — deadzone gone by construction. (Later, we can go
  further: stop stamping leaf cards for trees beyond ~150 u and let the instanced tier own
  them even while resident — a large near-field perf win.)
- Billboard cards (`FarTreeRenderPipeline`) survive only as the final tail (> ~1.5–2 km,
  where a tree is a few pixels), recolored through the same lighting/fog path — or retired
  entirely if instanced L4+ proves cheap enough.

### 3.2 Terrain: keep the 2.5-D far tiles, close their quality gap

The heightmap far-terrain tier is the right economics for multi-km terrain. Its gaps are look,
not structure:

- Feed it the **same lighting/fog model** (§3.3) instead of flat albedo.
- **Slope-aware shading + material blending** at ring boundaries (dither, again).
- Cliff/overhang loss stays accepted at >1 km (invisible in practice); the mid-band
  (320–700 u) can graduate to true coarse *voxel* tiles later using the §3.1 downsample
  operator on terrain columns — measured before adopted (this is the C5 lesson: coarsening
  the mid-band bought nothing measurable last time).

### 3.3 Light & atmosphere: where "natural" actually comes from

Ordered by perceptual value per engineering day — this is the cheapest 70% of "that demo look":

1. **Aerial perspective** (single post-process using scene depth): distance-based blend toward
   a sun/sky-tinted haze color, wavelength-biased so far greens cool toward blue-grey. This one
   effect *unifies every tier* — the near/far seam becomes a gradient the eye reads as air. It
   also softens whatever LOD steps remain. (PostProcessor already owns scene depth; this is a
   fragment-shader addition, not an architecture change.)
2. **Hemispheric sky ambient everywhere**: replace per-tier ambient constants with one
   sky-color term (up-facing brighter, down-facing darker) shared by chunks, far tiles, and
   instanced trees. Far field stops looking flatter than near field.
3. **Tone mapping + sun warmth pass** in PostProcessor (filmic curve, slight sun tint):
   voxel demos read "realistic" largely because of this.
4. **SSAO default-on** for the near field (it exists, converted to reverse-Z, currently off —
   needs its perf/quality pass), and *baked-in AO* for template mip levels (darken interior
   cells at build time — free at runtime).
5. Later: far-field sun occlusion approximation (heightmap horizon shadowing on far tiles),
   cloud shadow scroll. Explicitly not phase 1.

### 3.4 What is explicitly rejected (do not rebuild)

- **OR-occupancy squash for organic content** — floating canopies are structural, not tunable.
- **Aesthetic-mismatched billboard band as the *primary* mid-field representation** — cards are
  a tail, not a band the player stares at.
- **Residency-history-driven far content** (eviction cache) for generated things — fresh boots
  and never-visited terrain must look identical to visited terrain.

## 4. Milestones

| # | Deliverable | Acceptance (measured, poses fixed) |
|---|---|---|
| **M1** | Template mip-chain builder + disk cache; the tree-aware downsample operator with unit-pinned invariants (support-aware / majority-color / trunk-preserved / silhouette-bounded) | Red→green tests per invariant; visual contact sheet of every template at L0–L4 (elevated + eye-level) |
| **M2** | Instanced tree tier replacing billboards ≤1.5 km, fed by the existing per-tile plan; dithered level cross-fade; exact residency handoff | **Deadzone metric** (§6) flat across 250–500 u; same-pose A/B vs today; FPS budget: ≥ today's at pass-2 poses |
| **M3** | Aerial perspective + unified sky ambient + tone map | Same-pose A/B contact sheet; near/far tint delta (sampled pixel stats on canopy colors near vs 800 u) under a threshold instead of today's obvious shift |
| **M4** | Terrain far-tile lighting alignment + ring-boundary dither | Elevated pans show no ring seams; screenshot diffs at ring edges |
| **M5** | Tail policy: instanced-to-card fade >1.5 km (or card retirement if L4 instancing measures cheap); far draw batching (the ~500-draws issue) | Horizon pose ≥ 90 FPS Release; draw-call count budget |
| **M6** | GPU-driven far field (trick 3): persistent buffers, compute cull + LOD select, indirect submission for far tiles + instanced trees | Far-field CPU draw calls ≤ ~10 regardless of horizon; measured frame-time win at horizon poses |
| **M-spike** | Tech spike (timeboxed, throwaway): compute-pass raymarch over generator-derived brick mips for the >300 u field; measure vs the raster tier on our hardware | A number, not a vibe: FPS + memory at the pinned poses; go/no-go on the raymarched endgame |

Order rationale: M1/M2 are the user's core ask (continuous scaling trees); M3 is the largest
cheap perceptual jump and hides residual steps of M2; M4/M5 are consolidation.

## 5. Perf & memory envelope (phase-1 estimates, to be re-measured)

- Template mip chain: ~20 templates × 5 levels × (decimated meshes ≪ L0) — trivially small
  on disk and in RAM (< a few MB total; levels shrink geometrically).
- Instanced tier: same instance lists as impostors today (≈0.5–4 k trees/tile, subsampled per
  ring) but drawn per (template-level, batch) — target ≤ ~40 instanced draws per frame via
  per-level batching of tile lists, replacing today's per-tile card draws.
- The known ~500-draw far-tile pattern must be batched in M5 before any cap raise.

## 6. Verification discipline (what 2026-08-02 taught us, encoded)

- **Elevated cameras are mandatory** for any far-field look change: ground poses compress the
  mid-band into the horizon and hid both the floating-voxel disaster and the deadzone.
  Every milestone verifies at minimum: eye-level (60 u), overlook (90 u alt), plan view
  (250 u alt), horizon pan (ground, pitch −5).
- **The deadzone metric is a number, not an opinion**: **SHIPPED 2026-08-02** — engine
  counters `far_tree_mesh_annuli` / `far_tree_card_annuli` on `/api/render/stats` (tree
  instances per 50 u annulus, per tier), read by `tools/proving_grounds_probe.py --annuli`
  which prints the histogram + a continuity verdict (no annulus below 60% of its neighbor
  mean inside the populated band). ⚠️ Counts are spread across each tile's XZ extent, NOT
  bucketed at tile centers — center-bucketing aliases against the 64 u tile grid and fakes
  deadzones (whole annuli read 0 with trees on screen; observed before the fix).
  First PG run at the 300 u-altitude pose: **PASS**, 250–500 u flat at 110–140
  instances/annulus (`docs/evidence/pg_tree_annuli_20260802.txt`) — the M2 acceptance
  criterion, measured.
- Same-pose A/B screenshot pairs archived under `docs/evidence/` for every tier change.

## 7. M2 hardening — 2026-08-02 (user-reported: shrink, correspondence, lag spikes)

Three defects reported after the first M2 flight, all fixed and re-verified:

1. **LOD trees shrank while scrolling in, then popped.** The fade scaled geometry. Fixed:
   fades are now **4×4 Bayer screen-door dither** in `far_tree_mesh.frag` / `far_tree.frag`
   (`if (vFade < bayer4(gl_FragCoord.xy)) discard;`) — size NEVER changes, trees dissolve.
2. **LOD trees didn't correspond to the near trees that replaced them.** Instances carried a
   hashed 8-way yaw + ±15% scale jitter, but `decorateChunk` stamps templates unrotated and
   unscaled — the two tiers could never line up. Both removed from `far_tree_mesh.vert`; a far
   instance is now the same template at the same anchor, so the handoff is an in-place dissolve.
3. **Zoom-out froze the whole UI for seconds.** `TreeLodMeshRegistry::buildSpecies` ran the
   chain build + meshing on the main thread inside the draw loop. Measured off-thread times
   from the shipped log: `forge_redwood_xxl` **16.4–17.7 s**, `forge_elder_oak_xxl` 11.4–12.4 s
   — that WAS the freeze, one species per stutter. Now a background builder thread does chain +
   CPU meshing; `tick()` (called per frame in `renderFarTerrain`) finalizes at most ONE finished
   species per frame (GPU upload only); `level()` enqueues and returns null so the card tier
   covers until the mesh lands. Verified live: both giants built off-thread during an active
   camera ladder at 192–306 FPS.

**The test harness the defects demanded** (single tree, fixed distances — user ask):
- `samples/game_definitions/tree_lod_lab.json` → project **TreeLodLab**: FLAT streaming world,
  `world.floraOverride` (new GameDefinitionLoader knob, recipe-persisted) plans **one
  `forge_oak_m` per 128 u** — the same template the `forge_oak` species row meshes, so near
  stamp and far instance derive from identical voxels. (biomes.json understory layers survive
  via the C2 recipe guard; bushes prove non-trees get no far representation. Megaflora bands
  also survive — rare giant redwoods can appear.)
- `tools/tree_lod_harness.py`: picks the most isolated planned tree via the new
  `/api/debug/flora_plan` route (the deterministic `planFlora`, exposed), walks a fixed
  camera ladder (40→1500 u, `--band` for fine fade-band steps), archives labeled screenshots +
  counters. First full run: `docs/evidence/tree_ladder_run2/` — tree present and proportionate
  at every rung, fade-band rung shows the dither mid-dissolve, no shrink, no deadzone.
- A/B attribution knob: `POST /api/debug/far_terrain {"trees": false}` disables both far-tree
  tiers without touching terrain tiles.

**Fourth defect — found BY the harness on its first run (proof it was worth building):**
the fade-band rungs showed the LOD instance as a dithered ghost ~half a footprint BESIDE the
resident oak. Root cause: `buildLevelMesh` anchored the template's corner voxel at the mesh
origin (−0.5), but `decorateChunk` stamps at `base = worldPos − maxExtent/2` and the far
instance sits at the column center — a ~3.5-voxel lateral offset on an 8-wide oak. Fixed:
`TreeLodMeshRegistry::stampAnchorFor` replicates the stamp rule (anchor = −(mx/2 + 0.5) per
XZ axis), pinned by `TreeLodMeshTest.AnchorMatchesTheNearStamp`. The mesher's ±15%
height/canopy jitter was also removed (stamps are unscaled; jittered cards broke size
correspondence the same way). Re-verified: `docs/evidence/tree_ladder_band2/` — single clean
tree at every band rung, dissolve happens in place.

**Fifth defect — the handoff gap (user: "lower detail trees fade out before the detailed
trees render... for a bit of time there is nothing there").** The fade-out was driven by
DISTANCE, silently assuming chunks at that distance are loaded — but streaming is async, so
flying in dissolved the LOD tree while its real chunk was still generating. Fixed with a
**residency-gated fade**: RenderCoordinator checks per fade-band tile whether the chunks
under it actually exist (4 quadrant columns × vertical span, `getChunkAtFast`), smooths that
over ~0.3 s, and pushes `minFade = 1 − readiness` per draw; both tree shaders floor their
dither factor with it (`vFade = max(distanceFade, minFade)`), so a LOD tree cannot dissolve
until the real geometry is resident — then dithers out in place. Cards gate the NEAR fade
only (horizon dissolve stays distance-driven). Verified with a teleport-into-unloaded-land
burst (`docs/evidence/tree_handoff/`): tree present in every frame from +2.9 s (solid LOD
over barren streaming ground) through full residency.

**Sixth + seventh defects (user zoom-out repro, 2026-08-02 evening).**
- **Phantom trees through village buildings**: `planFlora` is the pristine generator plan;
  settlement builds EDIT chunks (persisted in world.db), so the near field has no trees
  there — but the far tier re-derived from the plan and grew trees through structures on
  zoom-out. Fixed: Application polls `PlacedObjectManager` (1 s cadence, structures only,
  4 u pad) → `FarTerrainManager::setTreeExclusions` → the mesher drops planned trees inside
  the rects (all tiles retired + rebuilt on change; steady-state no-op). Pinned by
  `FarTreeImpostorTest.ExclusionZonesDropTreesInsideStructureFootprints`.
- **Zoom-out handoff gap**: interior far tiles fully covered by real chunks were dropped
  from the wanted set entirely (dug-hole protection) — so when the camera flew away, the LOD
  instances for that ground didn't exist until the worker rebuilt the tile, while real
  chunks unloaded on schedule. Fixed by splitting residency from drawing: interior tiles
  stay resident with `terrainHidden` (terrain submission skipped, trees kept), and the
  renderer no longer early-outs when only hidden tiles are in frame. The readiness gate was
  also tightened from chunk-exists to chunk-has-rendered-geometry (`getNumInstances() > 0`)
  — an unmeshed chunk renders nothing and must not release the LOD tree.
  Evidence: `docs/evidence/zoomout_*.png` — identical full-forest frames from +1 s to +10 s
  after teleporting out 560 u.

**Distance ladder retune (user: "lower detail should kick in farther out… drop off longer
and more gradual"):** fade band now `load → min(load+90, unload−6)` — real trees own the
view to ~314 u (the load/unload hysteresis ring), safe in both directions ONLY because of
the residency gate; mesh ladder uses the full chain — L1 <360, L2 <560, L3 <820, L4 <1150,
L5 <1600 — with cards to ~2 km. Annuli metric (now 40 buckets/2 km): PASS, continuous 0–2 km,
mesh tier dominant to ~1750 u (`docs/evidence/pg_tree_annuli_extended_20260802.txt`), 63 FPS
at the elevated pose — no regression from the longer band.

## 7c. M3 lighting — design (2026-08-02, user: "far far far more realistic looking",
reference = the Reddit voxel video: natural light, dense forest, no artifacts)

Baseline (voxel.frag): Cook-Torrance sun key + gamma-curved baked skylight fill (monochrome!)
+ colored block light + points/spots. The four gaps vs the reference, in impact order:
1. **Colored hemisphere ambient** (voxel.frag ~line 327): replace the flat monochrome
   `skyAmbient * albedo` with a normal-dependent blend — cool sky tint (~vec3(0.55,0.70,1.0))
   for up-facing, warm ground bounce (~vec3(0.45,0.38,0.30)) for down-facing, scaled by the
   same curved skylight. This is THE "natural light" ingredient: shadows go cool, grass
   bounces warm.
2. **Aerial perspective**: distance haze toward a horizon color, sun-tinted near the sun
   direction (single-scatter approximation), applied consistently in voxel.frag AND
   far_terrain.frag/far_tree_mesh.frag (the far field must inherit the SAME curve or the
   near/far seam becomes a color wall). Exponential in distance, height-faded so high
   cameras see less haze on the ground below.
3. **Filmic tone map + exposure** in the PostProcessor final pass (check what it has —
   ssao.frag exists and was recently touched); ACES-fitted curve, slight saturation lift.
4. **AA**: FXAA in post first (cheap, ships today); TAA later (M6 territory).
Verify: same-pose A/B contact sheet at the PG vantages (village_overlook, lake_and_hills,
horizon_far), near/far seam continuity check, and the user's eyes on the reference vibe.

## 7d. Shadow quality overhaul (2026-08-02, user: "shadows need to get way way way better")

The shadow FIT was already good (4096² map, view-frustum bounding-sphere fit, world-anchored
texel snapping, 160 u cap, back-face-only casters so bias can be tiny). The quality gap was
entirely on the RECEIVING side in `voxel.frag`: a fixed 1.5-texel Poisson PCF. Three fixes:

1. **Contact-hardening penumbra (PCSS)** — an 8-tap blocker search estimates occluder depth,
   and the 16-tap PCF radius scales with occluder→receiver separation (clamped 1.5→14
   texels). Shadows are now sharp where objects meet the ground and progressively soft with
   height — a constant-width filter is the single most "CG" thing about a shadow.
   `kPenumbraScale = 600` is a LOOK constant (an exaggerated sun disc; the true 0.53° sun
   yields ~1 texel and reads aliased-hard), tuned against the ~536 u fitted depth range.
2. **Per-pixel disk rotation** (interleaved gradient noise) — a fixed Poisson pattern
   repeats across the screen and reads as banding once the filter is wide enough to matter.
   Rotating per pixel turns that into fine dither.
3. **Border fade + slope-scaled bias** — the shadow term now dissolves over the outer 12% of
   the map's UV footprint (the hard `inShadowMap` cutoff drew a visible line across the
   ground at the shadow distance), and bias grows with `1 - N·L` so grazing-lit surfaces
   don't acne.

Cost: fully-lit pixels early-out after the 8 blocker taps (the common case); shadowed pixels
pay 24 taps vs the old 16. The shadow pass's dominant cost is per-draw submission, not
sampling (`RenderDensityPlan` §2d), so this is a quality win at modest fragment cost.

### 7d.2 Vegetation shadow casting (2026-08-02)

Leaf materials **skip their solid faces entirely** (cutout cards replace them) and the shadow
pass reads those same chunk instance buffers — so canopies contributed *zero* geometry to the
shadow map. Foliage shadow shaders + pipeline already existed and were wired; they were dead
for two reasons: the NaN matrix in 7d.1, **and** `initializeShadow` used the SCENE's reverse-Z
compare (`GREATER`) while the shadow pass is forward-Z (clears 1.0, compares `LESS`) — no
fragment could pass. ⚠️ **Any shadow-pass pipeline must use `VK_COMPARE_OP_LESS`, never
`DepthConvention::sceneDepthCompareOp()`.**

Grass casting was built to match: `grass_shadow.vert` is generated from `grass.vert` with
**only** the final projection swapped to `ubo.lightSpaceMatrix` (blade placement, wind and
density-LOD math must stay identical or a blade's shadow detaches from the blade), and
`grass_shadow.frag` repeats `grass.frag`'s procedural taper discard — a plain depth-only pass
would stamp every blade as a solid **rectangle** and shade the ground like a wall. Draws use
the same radius cull and `bladesForDistance` tier as the visible pass, so a blade that isn't
drawn never casts.

Canopy density (`ChunkRenderManager::s_foliageDensity`) is now **0.25** — 0.5 still read as a
solid mass. Hashed on the absolute world/subcube cell, so thinning is seam-continuous and
stable across rebuilds. Live: `POST /api/debug/foliage {"density": N}` (re-meshes chunks).

### 7d.1 THE actual bug — shadow fit was NaN (2026-08-02)

None of the above was visible, because **no shadows were being cast at all**. Root cause: the
reverse-Z + infinite-far-plane migration (2026-08-01) silently broke the shadow volume fit.
The fit unprojected view-frustum corners through `inverse(proj*view)` at depth `0 = near,
1 = far`; under reverse-Z depth 0 IS the far plane, and an infinite far plane puts it at
`w = 0`, so the perspective divide yielded **NaN**. center/radius → NaN → `lightSpaceMatrix`
→ NaN → every `shadowCoord` → NaN. And **NaN compares false**, so the receiver's
`shadowCoord.z > -1.0` guard skipped shadowing entirely — the world rendered unshadowed with
no error, no crash, no log.

Diagnosis path (worth repeating): shadow-pass counters showed 544 chunks / 508 k instances
drawn, so casters were fine → forced a binary shadow in the receiver (still nothing) → dumped
`shadowCoord` as color (whole scene flagged out-of-range, then ~0 with w=1) → logged the fit
on the CPU, which printed `center=(-nan,-nan,-nan)`.

Fix: build the frustum corners **analytically** from the camera basis + FOV — convention-
agnostic, so a future depth-convention change cannot break it again — plus an `isfinite`
guard that logs loudly and falls back instead of poisoning the frame with NaN.

## 8. Structure LOD — the operator core (2026-08-02, user: "reduce structure complexity
without losing too much detail, like we did with trees")

**Shipped: the generalized decimation operator.** `TemplateLodChain` now takes a `Config`
(`treeConfig()` / `structureConfig()`) and a raw-raster entry point
(`buildFromSoup(MicroSoup, cfg)`) for subjects that aren't templates — a placed structure
extracted from chunks. The insight carried over from trees: *know what defines the subject
and protect it unconditionally*. For trees that was the trunk; for structures it is the
**exposed shell**:
- `protectExposedShell` — any cell containing air-exposed micros survives regardless of
  coverage. A 1-voxel wall is 11–33% of a 2–3-voxel cell — under tree rules it erodes
  into facade holes; under shell protection it cannot.
- `hollowInterior` — cells with zero exposed micros drop wholesale. Rooms and solid fill
  are invisible at range; a 9³ solid decimates to exactly its 386-cell shell (test-pinned).
- `islandCullDivisor 0` — fences/wells are legitimate detached islands; only single-cell
  floaters die (trees keep the aggressive n/50 debris cull).
- Ladder `{3, 9, 18, 27}` micros: sub-voxel (trim/roof courses readable at the handoff) →
  voxel → 2-voxel → 3-voxel.
Pinned by `StructureLodChainTest` (6 tests: thin-wall survival at every level, window
openings stay open at fine levels, interior hollowing exact, glass keeps its material,
detached fence posts survive, determinism). Tree output verified byte-identical after the
refactor (same oak arc).

**Not yet built (the rendering half — next session):**
1. **Extraction**: per placed structure, snapshot its AABB's voxels from resident chunks
   into a `MicroSoup` (at build time or lazily when resident), keyed by structure UUID.
2. **StructureLodRegistry**: off-thread chain + mesh per structure (reuse
   `TreeLodMeshRegistry::buildLevelMesh` — the FarVertex mesher is subject-agnostic),
   cached for the session (later: persisted like chunk_lod_blobs).
3. **Rendering**: draw through `TreeLodRenderPipeline` (one shared instance buffer, one
   `MeshDraw` per structure with `firstInstance=i, count=1`), same dither fade + residency
   gate as trees, level by distance. This REPLACES the parked chunk-squash far-LOD tier
   for structures and fills the tree-exclusion clearing with its buildings at range.
4. **Trees too** (user: "even the trees could use some work"): try
   `protectExposedShell=true` on the tree preset — canopy SURFACE cells would win the
   budget contest over buried interior cells, sharpening silhouettes at coarse levels.
   Needs the ladder harness re-run to judge (and the annuli/FPS check — shell protection
   adds cells at coarse levels).

**Open defects surfaced by the ladder (not tree-tier):**
- Thin dark diagonal **sliver artifacts** at far grazing angles — persist with trees OFF →
  far-terrain tile geometry (seam/skirt), pre-existing. Needs its own pass.
- Card tier (>900 u) tint is visibly darker than the mesh tier — the M5 fade/retirement item.
- Far-tile snow/biome patches hard-edge against near chunks (M4 material alignment).
- Tree tiers cost measurable frame time at high-tree poses (292 vs 678 FPS trees-on/off at one
  lab pose) — M5/M6 batching material.

## 7. Relationship to existing plans

- Extends `docs/ContinuousLodPlan.md` (C-series): C3's serve-from-storage machinery stays for
  *saved structures*; the "generated coarse tier" idea is superseded by §3.1/§3.2 for objects
  and terrain respectively. The C5 finding (mid-band chunk coarsening unmeasurable) stands.
- `docs/WorldLookBacklog.md` A-items terminate here; this doc is their continuation.
- The quarantined `EvictedLodCache` remains the structure path until M-later revisits saved
  chunks with the §3.1 operator (which would fix its tree problem too, if ever re-enabled).

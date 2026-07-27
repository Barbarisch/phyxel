# Build Phase — Known Issues

Honest tracker of issues observed during runtime verification of the engine build increments.
Stated here so they aren't lost; fixes are scheduled, not silent.

## Open

### KI-5 batch — USER visual observations, 2026-07-23 (procedural settlement quality)

Reported from live inspection of generated villages. Each gets the standard treatment:
root-cause → red validator/test shown failing → fix → auditor. Triage below is
HYPOTHESIS until confirmed in code.

- **KI-5a — Windows sometimes placed on the corner of a structure — FIXED at unit level
  (2026-07-24, solution-auditor PASS after a THREE-round audit).** Root cause: blocked-slot
  shifting clamped windows into the footprint-corner cube (red: hall_house 12xD placed a
  window at exactly (0,0)). Fix: a 1-cube corner margin (REASONED — masonry corner
  integrity, not sourced) on footprint-corner edge ends only; window COUNT still derives
  from the wall's full architectural length (per-bay rule) with placement confined to the
  corner-safe band + an exhaustive slot-scan fallback. The audit caught TWO silent-loss
  regressions in intermediate versions (18 cases 1→0, then 13 cases 2→1) — closed by the
  auditor-prescribed WINDOW CENSUS: all 260 sweep cases pinned to a reviewed golden
  (tests/core/golden/window_census.txt); any count drift fails with a reviewable diff.
  Reviewed reductions documented in-test: tavern Wx5 = 0 (5-wide gable front proven
  slotless), tavern Wx6 = 1 of 2 (band fits one window beside the door).
- **KI-5b — Objects not flush against walls, especially wall lanterns — FIXED at unit
  level (2026-07-23, solution-auditor PASS after a two-round audit).** Root cause: every
  wall-backed piece was inset by the EXTERIOR wall thickness; on thin interior partitions
  (2 micro) pieces floated extT−1 micro (~0.8-0.9 m on stone styles) off the wall —
  style/wall dependent, hence "sometimes". Fix: PER-AXIS insets on `FurniturePlacement`
  (`insetMicroX/Z`; exterior = full band, partition = the straddling band's half, corners
  flush to BOTH walls), gated to the modern calling convention (`extTMicro > 0`) after the
  auditor's round-1 FAIL reproduced a legacy-convention regression (furniture embedded in
  walls, `MicroPlacementOverlapTest` 0→234→0 overlaps across the fix). Guard:
  `FixtureFlushnessTest`. REMAINING: live L4 sconce confirmation on the next
  post-typology-reorder village (the current one has no wall lantern); winged-notch edges
  still read as interior (old over-inset behavior there, disclosed).
- **KI-5c — Rug texture doesn't fit / isn't centered on the rug object.** ROOT CAUSE FOUND
  (2026-07-24 investigation), DEFERRED by user to do walkability. The `rug_oriental.png` is one
  whole-rug image (single medallion + its own border); the rug is 18×13 micro = ~2 cubes wide,
  and **placed furniture renders through the STATIC chunk path** (`PlacedObjectManager` →
  `spawnTemplateMicro` → `chunk->addMicrocube` → `static_voxel.vert`, **per-cube UV tiling**),
  so the image repeats ~2× across the width and never centres. The engine's planar-projection
  feature (`# surface: texture=… projection=planar axis=y`) that would fit one image across the
  object is wired **ONLY into the kinematic path** (`KinematicVoxelManager::buildFaces`, used by
  `ItemPropManager`) — NOT into static-baked placed furniture. Confirmed 3 ways: code, design doc
  (`docs/VoxelAppearanceModel.md` §7: static path = "placed static props … per-cube tiling";
  projection "validated" only in ItemPropManager), and empirically — adding the surface header to
  `rug.voxel` rendered a **plain red slab** (header silently ignored; evidence
  screenshot_20260724_154106_744.png). An asset-only header therefore CANNOT fix this (and
  regresses to plain fill). Two real fix paths: **(A) asset-only** — redesign the rug texture as a
  seamless *tileable all-over field* (no single medallion/baked border) + keep the wool border
  geometry, so per-cube tiling reads as a continuous woven field (loses the centred-medallion
  look); **(B) engine** — wire placed furniture with an active `# surface:` to render via the
  kinematic Tier-2 projection path (matches the design-doc intent; keeps the medallion; also
  unlocks projected paintings/banners as placed props; touches furniture render/lifecycle). Not a
  1-micro sub-tile bug as originally guessed.
- **KI-5d — Stairs overlap furniture in some cases.** The furniture pass doesn't reserve the
  stair footprint + its well/landing; only door clearances are reserved. Fix: thread
  `StairPlanner` rects into the placer's reservation grid; L2 check: no fixture bbox
  intersects stair cells.
- **KI-5e — Generated paths should remove the grass.** Grass blades render through thin
  paving (the blade layer reads the Grass cube under the road — logged as a Phase-2
  follow-up, still open). Fix: paving/path stamping converts the underlying Grass-family
  cube to Dirt (same rule the building pad uses, V10 grass_under_house).
- **KI-5g — Furniture appears outside walls (USER, 2026-07-23) — SPLIT after audit
  (2026-07-23 evening).** The reproduced-and-fixed case: the L-plan tavern's UPPER story
  was laid out on the full RECT over an L-shaped ground floor, so upstairs chamber
  fixtures (wardrobe/stool at (65,21,13-14)) hovered over the empty notch — visually
  "furniture outside the building" (the user's coordinate (64,16,13) is the notch).
  Fixed red-before-green (auditor-verified via stash-mutation): typology plans beat the
  winged layout, and a winged ground floor truncates to ONE story
  (`FixtureInsideShellTest.WingedGroundFloorTruncatesUpperStories`). **The ORIGINAL
  description (a wardrobe on grass west of the wall) is NOT currently reproducible:** an
  auditor registry-wide scan of all 14 live structures found ZERO fixtures with any bbox
  outside their structure's footprint — the "wardrobe on grass" in
  screenshot_20260723_152444_484.png was most likely a legitimate yard WOODPILE, and the
  "rug tilted through the wall" is KI-5c's 1-micro-slab rendering, untouched so far.
  KEPT OPEN, narrowly: exterior fixture escape, if it exists, awaits the L2
  bbox⊂exterior-shell validator (the auditor's ad-hoc registry scan is the recipe) +
  wall-lantern flushness is KI-5b.
- **KI-5h — Interior walls sometimes FULL-CUBE thick, can block doorways (USER,
  2026-07-23).** Confirmed in scan data: the same tavern has an interior wall line at
  x=62 (z9-16, y18-19) built of full StoneBricks CUBES — the v2 rule says interior walls
  are subcube-thick (interior_wall 0.222 ≈ 2 micro). Suspect: the L-plan wing-joint wall
  (main-range + wing exterior walls overlapping into a cube-read band) or the winged
  layout emitting cube walls; a cube-thick partition also breaks the door-carve arithmetic
  (the carve clears the thin band, leaving cube remnants that narrow/block the doorway).
- **KI-5f — Fences don't always come to a neat corner — FIXED at unit level (2026-07-24,
  solution-auditor PASS after a two-round audit).** Two composition defects: W/E runs
  shortened by a cube left an 8-9 micro rail/picket gap beside every corner post, and
  whole-cube spans made the N/E fence planes MISS the perpendicular fence entirely at some
  corners (planes sit at micro row 0 of their boundary cubes). Fix:
  `Core::planParcelFenceRuns` — micro-precise runs ending exactly on the corner-plane
  intersections; single corner ownership (N/S own posts; W/E omit end posts AND end slats
  — the counting guard caught picket infill double-writing corners); gate window
  cube-aligned to exact legacy centering. Guard: `FenceCornerTest` (post ownership counted
  per contributing run; rails must reach every corner along both planes; auditor
  mutation-verified in both failure directions). Honestly unverified: the
  Application-embedded gate math has no unit test; live corner neatness is
  screenshot-subjective.

## Resolved

### KI-1 — Roof hovers above the walls (resolved; doc was stale)
- **Was:** a 1-micro air row between the wall/ceiling top and the roof's lowest course — the
  `eaveSub = ceil(ceilTopMicro/3)` rounding in `realizeShell` pass 5.
- **Fixed:** `eaveSub = ceilTopMicro / 3` (FLOOR-divide) — the eave rests ON the ceiling top
  (`StructureRealizer.cpp` ~line 377, comment documents the alignment arithmetic). Gated green by
  the V1 roof-eave-flush detector (`RealizedStructureValidatorTest`, `maxGapMicro=0`), which was
  proven RED on the hovering output first; the test's comment records the revert-repro
  (`ceilTopMicro/3` → `(ceilTopMicro+2)/3` re-fails it). This entry stayed "Open" after the fix
  landed — corrected 2026-07-09.

### KI-2 — Overlapping furniture across stories (resolved, audited)
- **Was:** the v2 build handler (`editor/src/Application.cpp`) called
  `FurniturePlacer::furnish(story, origin, v2FloorY)` for **each** story but passed the **same
  ground-floor `v2FloorY`** every time — so every story's furniture landed at the ground-floor Y and
  stacked. The user's framing: "every floor is exactly the same and therefore one floor blocks the
  one below it." Exposed by `stack_stories` (#36); the furniture loop pre-dated multi-story and
  assumed one floor.
- **Fixed:** the realizer already tracked per-story walkable micro-Y (`ShellResult.floorTopByStory`,
  surfaced for the harness). The handler now bridges that out of the v2Mode block
  (`v2FloorYByStory`, `oy + ft/9` per story, populated where `shell` is in scope) and the furniture
  loop indexes it per story (`storyFloorY = v2FloorYByStory[si]`), so each story is furnished at its
  own floor.
- **Red→green (`FurniturePlacerTest.PerStoryFloorYStopsCrossStoryStacking`):** reproduces the actual
  call shape — TWO distinct stories of one identical-floor building. Furnishing both at the **same**
  floorY collides (`sharedPositions > 0` = teeth, the real stack), at **distinct** per-story Y it
  does not (`== 0`). `furnish()` writes `worldPos.y = floorY` directly, so the test exercises the
  handler's exact before/after. Audited PASS by the solution-auditor (which also pushed the test from
  a same-object-twice tautology to this faithful two-story repro).
- **Runtime-verified:** a 2-story furnished house placed 6 fixtures across **two** distinct world Ys
  (`y=18` ground, `y=21` upper) — was all at `y=18` before.
- **Caveat:** the `Application.cpp` call site is runtime-observed only; an e2e guard that builds a
  multi-story v2 building and asserts ≥2 distinct fixture Ys remains the standing gap (shared with #05).

### KI-4 — Stairs functionally walkable (was falsely closed once; now real, audited)
- **Was:** stairs were geometry-only and unwalkable. Both forms stamped solid pillars from `y=0`, so
  a stacked upper flight filled the lower flight's emergence headroom (a solid column), and the
  "headroom" gate was a 2D `overlap()` exempt for switchback — it measured no clearance. (I closed
  this once on fabricated evidence and a dismissed walk-test failure; reopened after audit.)
- **Fixed, in three audited red→green layers:**
  1. **Geometry** — `StairPlanner` emits THIN treads/landings (a slab at each step surface, open
     underneath), not pillars. `StructureRealizerTest.SwitchbackEmergenceHasHeadroom` scans the built
     canvas for headroom above an intermediate-floor foothold: RED on the old pillars (clearance 1
     micro, need 16) → GREEN. `TenStoryTower...` asserts it at every intermediate floor (1..8).
  2. **Gate** — `BuildingProgramValidator` now calls `StairPlanner::stackedEmergenceClearance`
     (builds occupancy from two stacked plans, measures vertical air above the emergence), form-
     agnostic; fires `stair_no_headroom` on a solid-fill regression OR a too-short story. Red-first:
     `ShortStoriesLackStairHeadroom` failed on the old form==Straight gate → GREEN.
  3. **Traversal** — `TraversalProbe` (kinematic character-box: 0.5 m wide, 1.75 m tall, 0.44 m
     step-up) BFS-climbs a realized 3-story switchback from floor 0 to the top with collision +
     step-up + head-room. `AgentCanClimbSwitchbackToTopFloor` proves it, with a NEGATIVE CONTROL
     (fill the well solid → unreachable) so the test has teeth. Probe itself validated on synthetic
     wall/ledge/ceiling cases.
- **All three layers independently audited PASS by the solution-auditor.** Riser check
  (`stair_riser_too_steep`) was always real and stays.
- **Caveat / follow-ups:** the traversal proof is a faithful **simulation** (the `TraversalProbe`
  agent), not the live game character — by design (driving the live character is flaky; the probe is
  deterministic and matches the character's dimensions/step-up). Still TODO (enhancement, not a
  walkability bug): the **L-shaped** stair form for houses (only switchback + straight implemented).

### KI-3 — Tall structures failed to place above the generated chunk-Y range
- **Was:** the material/subcube/microcube placement paths (unlike `addCube`) returned `false`
  when the owning chunk didn't exist, so any voxel above the generated chunk-Y band silently
  failed. The 10-floor tower with only chunk-Y=0 present reported `45994 placed, 129006 failed`
  (~74% — exactly the height fraction above y=31). Invisible for short single-story builds.
- **Fix:** extracted `ChunkManager::addCube`'s chunk-existence guard into a public
  `ensureChunkAt(worldPos)` (also fixed its broken `%d` log — `LOG_*_FMT` is stream-style);
  `StructureGenerator::place()` calls it before every voxel, so placement auto-materializes its
  owning chunk and crosses any number of vertical seams.
- **Verified (two-seam stress test):** 15-story tower (~74 m, 259,229 voxels) built into a world
  with only chunk-Y=0 present → `Created new chunk at (_,32,_)` and `(_,64,_)` (both seams),
  `0 failed`, bbox to y=72. Drop-test: player from y=82 lands on the roof at **y=72.67** (solid
  above both seams). NOTE: an earlier 259k build left the engine unresponsive ~1–2 min later with
  no logged error; it did **not** reproduce on the clean re-run — watch for render/resource
  pressure from very large single structures (separate from this placement fix).

### KI-0 — 100k voxel cap silently dropped the whole structure (the "ghost")
- **Was:** `StructureGenerator::place()` early-returned when a structure exceeded 100,000 voxels,
  reporting every voxel as `failed` — yet the handler still set `success:true`, registered the
  bbox, and spawned furniture. Result: a registered, furnished building with **zero walls**. The
  10-floor tower (175,532 voxels) hit this and rendered nothing.
- **Fix:** (1) `place()` now places **incrementally in bounded steps** with no silent cap and
  reports true placed/failed counts; (2) the v2 build handler gates registration on
  `placement.placed > 0` — on `0 placed` it returns `success:false` and registers no ghost.
- **Verified:** tower now `175000 placed, 0 failed`; a build into ungenerated chunks returns
  `placement failed (0/25179 placed) — not registering ghost` (HTTP 400, no structure record).
  Runtime drop-test: player dropped from y=62 lands on the roof at y=56 (solid, full height).

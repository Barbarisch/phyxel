# Build Phase — Known Issues

Honest tracker of issues observed during runtime verification of the engine build increments.
Stated here so they aren't lost; fixes are scheduled, not silent.

## Open

### KI-5 batch — USER visual observations, 2026-07-23 (procedural settlement quality)

Reported from live inspection of generated villages. Each gets the standard treatment:
root-cause → red validator/test shown failing → fix → auditor. Triage below is
HYPOTHESIS until confirmed in code.

- **KI-5a — Windows sometimes placed on the corner of a structure.** Likely: the autofill
  window spec places along the front wall without a minimum jamb offset from wall ends;
  corner cells host two wall bands. Fix in the openings layout (min 1-cube corner clearance)
  + a `RealizedStructureValidator` window-position check.
- **KI-5b — Objects not flush against walls, especially wall lanterns.** Likely:
  `FurniturePlacer::mountFor` wall attachment inset vs the CLAMPED subcube wall band
  (thicknessMicro) drifts for some wall thicknesses/orientations. Fix at the mount-inset
  arithmetic + an L2 adjacency check (fixture bbox must touch the wall band via `featureAt`).
- **KI-5c — Rug texture doesn't fit / isn't centered on the rug object.** Asset-level: the
  1-micro rug slab's per-face sub-tiling doesn't align the rug_oriental field with the
  template extent (same micro-sampling family as the washed-out Log fences). Fix in
  `tools/regen_furniture.py` rug generation (size the field to the slab / center the motif),
  possibly renderer sub-tile origin.
- **KI-5d — Stairs overlap furniture in some cases.** The furniture pass doesn't reserve the
  stair footprint + its well/landing; only door clearances are reserved. Fix: thread
  `StairPlanner` rects into the placer's reservation grid; L2 check: no fixture bbox
  intersects stair cells.
- **KI-5e — Generated paths should remove the grass.** Grass blades render through thin
  paving (the blade layer reads the Grass cube under the road — logged as a Phase-2
  follow-up, still open). Fix: paving/path stamping converts the underlying Grass-family
  cube to Dirt (same rule the building pad uses, V10 grass_under_house).
- **KI-5g — Furniture generates OUTSIDE walls (USER, 2026-07-23).** Evidence captured live:
  the L-plan stone_keep tavern at (60,16,1) has a wardrobe standing on grass WEST of its
  x=60 exterior wall and a rug tilted through a wall opening (screenshot
  screenshot_20260723_152444_484.png + scan). Suspect: L-plan (`footprintShape:"L"` /
  generateWingedLayout) room rects disagree with the realized wing walls, so the placer's
  wall-backed positions fall outside the built shell — the L-plan cluster's fourth defect
  class (wrong-side door anchors, bbox-perimeter door misses, and the tall-tower read are
  the others). Fix likely = make winged room rects match realized geometry + an L2
  furniture-inside-shell validator (fixture bbox ⊂ interior cells via `featureAt`).
- **KI-5h — Interior walls sometimes FULL-CUBE thick, can block doorways (USER,
  2026-07-23).** Confirmed in scan data: the same tavern has an interior wall line at
  x=62 (z9-16, y18-19) built of full StoneBricks CUBES — the v2 rule says interior walls
  are subcube-thick (interior_wall 0.222 ≈ 2 micro). Suspect: the L-plan wing-joint wall
  (main-range + wing exterior walls overlapping into a cube-read band) or the winged
  layout emitting cube walls; a cube-thick partition also breaks the door-carve arithmetic
  (the carve clears the thin band, leaving cube remnants that narrow/block the doorway).
- **KI-5f — Fences don't always come to a neat corner.** `FenceBuilder` corner joins leave
  ragged meets (missing corner post or overlapping rails) on some parcel orientations,
  despite the earlier fence-corner fix (which covered a different defect). Fix in the corner
  join emission + a corner-topology unit test across all 4 orientations.

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

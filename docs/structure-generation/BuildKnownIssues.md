# Build Phase — Known Issues

Honest tracker of issues observed during runtime verification of the engine build increments.
Stated here so they aren't lost; fixes are scheduled, not silent.

## Open

### KI-1 — Roof hovers above the walls
- **Symptom:** a visible gap between the top of the walls and the underside of the roof (the roof appears to float).
- **Where:** `StructureRealizer::realizeShell` **pass 5** (`place_roof` / #13) — the `eaveSub` (roof eave subcube row) vs. the wall-top / ceiling-slab alignment. Likely the eave doesn't meet the wall top flush, or the gable-end band leaves a gap on the long (eave) sides.
- **Severity:** cosmetic/structural-lie (the roof should rest on the walls).
- **Fix sketch:** align the roof eave row to the ceiling/wall top; verify the eave course is continuous around the perimeter; check the gable-end fill meets the eave. Add a realizer unit test (`microBounds` continuity between wall top and roof base).

## Resolved

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

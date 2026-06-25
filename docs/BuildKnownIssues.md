# Build Phase — Known Issues

Honest tracker of issues observed during runtime verification of the engine build increments.
Stated here so they aren't lost; fixes are scheduled, not silent.

## Open

### KI-1 — Roof hovers above the walls
- **Symptom:** a visible gap between the top of the walls and the underside of the roof (the roof appears to float).
- **Where:** `StructureRealizer::realizeShell` **pass 5** (`place_roof` / #13) — the `eaveSub` (roof eave subcube row) vs. the wall-top / ceiling-slab alignment. Likely the eave doesn't meet the wall top flush, or the gable-end band leaves a gap on the long (eave) sides.
- **Severity:** cosmetic/structural-lie (the roof should rest on the walls).
- **Fix sketch:** align the roof eave row to the ceiling/wall top; verify the eave course is continuous around the perimeter; check the gable-end fill meets the eave. Add a realizer unit test (`microBounds` continuity between wall top and roof base).

### KI-2 — Overlapping furniture across stories
- **Symptom:** furniture/fixtures overlap inside a multi-story structure.
- **Cause (pinned):** the v2 build handler (`editor/src/Application.cpp`) calls
  `FurniturePlacer::furnish(story, glm::ivec3(posX,0,posZ), v2FloorY)` for **each** story in the
  loop but passes the **same ground-floor `v2FloorY`** every time — so every story's furniture is
  placed at the ground-floor Y and stacks/overlaps. Introduced/exposed by `stack_stories` (#36):
  the furniture loop pre-existed and assumed a single floor.
- **Severity:** functional (upper floors are unfurnished; ground floor is double-stacked).
- **Fix sketch:** compute a per-story walkable Y (the realizer already tracks `floorTopByStory`;
  surface it on `ShellResult`) and pass the correct floorY into `FurniturePlacer::furnish` per story.
  Small, well-scoped.

### KI-4 — Stairs are NOT functionally walkable (REOPENED — was falsely closed)
- **Status:** I marked this Resolved on fabricated evidence; an independent audit (solution-auditor)
  + re-reading the code confirm it is **not** fixed.
- **The switchback stacks solid too.** `StairPlanner::planStair` pushes every tread/landing as a
  `StairSolid` with base `y=0`, height `h=top` (`StairPlanner.cpp:65,68,73`) — a solid pillar from
  the floor up to the tread. Stacked stories fill the well as a solid column, so the next floor's
  flight sits on the lower floor's emergence with **zero headroom** — the same KI-4 failure, not
  fixed by changing forms.
- **The "headroom" gate is fake.** `stair_no_headroom` (`BuildingProgramValidator.cpp:231-243`) is a
  2D footprint `overlap()` gated to `form == StairForm::Straight`. It measures **no** vertical
  clearance and structurally exempts switchback — it can never fire for the shipped geometry.
- **The walk-test disproved it.** The simulated walk's final 2 waypoints jumped 0.67 m and 1.0 m
  (> 0.45 m step-up) at the emergence — the capsule hitting the next floor's solid column. That was
  waved off as a "teleport artifact"; it is the bug.
- **Real fix (RED-FIRST):** (1) a genuine clearance check that, on the BUILT MicroCanvas, scans up ≥
  character height above every walkable surface along the climb path and is shown FAILING on the
  current switchback before anything changes; (2) thin treads (open space under/above the flight) so
  the well isn't a solid column; (3) re-run the clearance scan + a real walk and report the numbers,
  not a narrative. `stair_riser_too_steep` IS a real check (measures riser vs. step-up) and stays.
- **What DID get built (and is real):** the riser check; a switchback generator that fits + keeps
  risers ≤ step-up; the `StairPlanner` shared-source-of-truth structure.
- **PROGRESS (geometry + gate done; runtime NOT — auditor-verified):**
  - DONE — real clearance check `StructureRealizerTest.SwitchbackEmergenceHasHeadroom` scans the
    built MicroCanvas for headroom above a foothold at an INTERMEDIATE floor. Shown RED first on the
    solid-pillar switchback (best clearance = 1 micro, need 16), then GREEN after the fix.
  - DONE — fix: `StairPlanner` now emits THIN treads/landings (a slab at each step surface, open
    underneath) instead of pillars from `y=0`. `TenStoryTower...` asserts emergence clearance at
    every intermediate floor (1..8) — the real reachability invariant at scale.
  - DONE — the validator gate is now REAL (solution-auditor verdict PASS). The `form==Straight`
    `overlap()` was replaced by `StairPlanner::stackedEmergenceClearance`, which builds occupancy
    from two stacked flights' plan solids and measures vertical air above the emergence; it fires
    `stair_no_headroom` on a solid-fill regression OR a too-short story, form-agnostic. Red-first:
    `ShortStoriesLackStairHeadroom` failed on the old gate (switchback exempt), green now. 39/39
    stair tests pass.
  - NOT DONE — no runtime/traversal proof: thin 2-micro tread slabs are unverified for character
    collision/step-up in the live engine; a real walk up a built thin-tread tower is still owed.
  So: the clearance bug is fixed in geometry AND guarded by a real gate (both red→green, both
  auditor-verified). KI-4 stays OPEN only on the runtime-traversal proof.

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

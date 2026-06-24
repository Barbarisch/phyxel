# Build Phase — Known Issues

Honest tracker of issues observed during runtime verification of the engine build increments.
Stated here so they aren't lost; fixes are scheduled, not silent.

## Open

### KI-3 — Tall structures fail to place above the generated chunk-Y range
- **Symptom:** a structure that extends above the currently-generated vertical chunk band
  places only its lower portion; the upper voxels are counted as `failed`. Surfaced by the
  10-floor tower stress test: with only chunk-Y=0 present (world y 0–31) a y=22→60 tower
  reported `45994 placed, 129006 failed` (~74% — exactly the fraction of its height above
  y=31). Pre-generating chunk-Y=1 made it `175000 placed, 0 failed`.
- **Cause:** `ChunkManager::addCube/addSubcube/addMicrocube` place into the **existing** chunk
  grid and return `false` when the target chunk isn't present; placement does not auto-create
  the chunk. Short single-story buildings never crossed a vertical chunk boundary, so this was
  invisible until a multi-story structure exceeded 32 m of height.
- **Severity:** functional (tall structures silently lose their top — now *honestly* reported as
  `failed`, no longer a wall-less ghost, but still incomplete).
- **Fix sketch:** before placing, ensure the chunk-Y span `[oy, bbox.maxY]` over the footprint
  exists (generate empty chunks), **or** make the placement path auto-create the owning chunk on
  demand. The v2 handler already knows the bbox; the cheap fix is a "materialize empty chunks for
  the structure's vertical span" pass ahead of `place()`.

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

## Resolved

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

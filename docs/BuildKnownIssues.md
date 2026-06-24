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

## Resolved
*(none yet)*

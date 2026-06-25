# 04 · place_floor (+ place_subfloor)

> Tier: Site & shell. Part-1 status: **P** (one slab). Schema: [`README.md`](README.md).

## Job
Lay the **structural floor** (joists/slab spanning the foundation) + the **finish floor** per room (material by
status/use), at the floor datum.

## Reads
- Foundation walls + datum from place_foundation (#3).
- `AssemblyPlan`: room rects + per-room `floorMat`/use (Part 3).
- Style: `floor` thickness + material.

## Emits
- A **structural deck** over the footprint at the datum (joists on the foundation / a slab).
- A **finish floor** per room — material from the room program (flag/earth/board/tile) — over the deck.
- Defers **stairwell / hatch holes** to place_stairs (#12) and the subterranean tier (cut later, not filled here).
- `AssemblyPlan.floors` entry (current `plan.floors`).

## Algorithm
1. Span the structural deck across the foundation load lines (joist direction = the shorter span).
2. Over the deck, fill each room's finish floor at its material (kitchen/byre = beaten earth or flag; hall = board; manor = tile/flag).
3. Leave **no hole** here; mark intended stairwell/hatch footprints as "reserved" for #12/#50.
4. Emit a threshold/step where finish levels differ between rooms (e.g. a sunken byre).

## Satisfies (checks)
E (floor thickness), F (per-room floor matches use), D (the deck spans, not floats), K (a walkable surface on the floor, not perched furniture).

## Engine capability needed
- Voxel fill at the datum — ✅ (`fillMicroBox`).
- Per-room material lookup — ✅ (room program / MaterialRegistry).
- "Reserved hole" marking for later cut — ⚠️ (a plan annotation; minor).

## Failure modes
- One uniform slab ignoring per-room material/use (the current behavior) → F-violation.
- Filling a stairwell that #12 must reopen → coordinate via the reservation.
- Finish floor not resting on the structural deck → a floating-floor bug.

## Function testers
- **F1** A continuous structural deck spans the foundation (no unsupported span).
- **F2** Each room's finish floor uses its program material.
- **F3** Level changes between rooms get a threshold/step.
- **F4** Stairwell/hatch footprints reserved (not solid-filled).
- **F5** The walkable surface sits exactly at the floor datum.

## Grounding
- Floor thickness — REUSE `structure_styles.json` (`floor`).
- Per-room floor material — REUSE Part 3 room programs.

## Open questions
- Suspended timber floor vs slab-on-ground by status/period — affects the deck model.

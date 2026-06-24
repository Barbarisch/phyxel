# 05 · generate_room_layout

> Tier: Site & shell. Part-1 status: **M** (rooms are currently hand-authored in the program). Schema: [`README.md`](README.md).

## Job
**Derive** the room rects from the typology + bay model + program — replacing hand-authoring. Turns "a hall_house
on a 4×8 footprint" into concrete, sized, connected rooms.

## Reads
- `AssemblyPlan`: archetype + room program (Part 3 + `room_program.json`), footprint, status.
- The archetype's **adjacency rules** + **required/typical rooms** (the data sheet).

## Emits
- The set of **room rects** (id, rect, purpose, floorMat) tiling the footprint.
- The **portal graph** (which rooms connect, where the doors go) — required for cut_openings (#8) + reachability.
- Writes them into the program the later placers read.

## Algorithm
1. Take the bay model (bay length + count from `room_program.json`) → divide the footprint into bays.
2. Assign rooms to bays per the typology (e.g. hall_house: service 1 bay | open hall 2 bays | solar 1 bay).
3. Size each room from its program (required rooms get their minimum; typical rooms fill remaining bays by status).
4. Place **circulation** (a cross-passage / corridor / stair-hall) per the archetype's adjacency rules.
5. Build the **portal graph**: every room reachable from the entrance; apply the archetype's access-tier adjacencies (e.g. bank: no public→vault edge).
6. Emit room rects + portals; validate against BuildingProgramValidator (reachability/scale/typology).

## Satisfies (checks)
F (room program/layout logic), F1–F (rooms fit the typology + period), G (circulation/reachability), W (archetype access-tier adjacency), and the per-archetype access-tier rules.

## Engine capability needed
- Bay/rect subdivision + graph build — ⚠️ (pure logic; not yet written — this is the placer that *replaces* hand-authoring).
- BuildingProgramValidator reachability/scale — ✅ partial (exists).

## Failure modes
- Cramped/oversized rooms (ignoring the bay model) → E/F-violation (the "rooms too cramped" complaint).
- A room unreachable from the entrance → G-violation.
- Violating an access-tier rule (a public door straight into a vault) → W-violation.

## Function testers
- **F1** Every required room (per the archetype) is present and ≥ its grounded minimum.
- **F2** Rooms tile the footprint on the bay grid (no leftover slivers, no overlaps).
- **F3** Every room is reachable from the entrance (the portal graph is connected).
- **F4** Access-tier adjacency holds (no forbidden public→secure edge).
- **F5** Circulation (passage/corridor/stair-hall) exists where the archetype needs it.

## Grounding
- Room sizes + bay model — REUSE `room_program.json` (cited) + the archetype data sheets.
- Where a sheet flags a room size `to_ground`, this placer must not invent it — use the bay default + flag.

## Open questions
- Auto-layout algorithm choice (bay-packing vs template-fit vs constraint-solve) — pick per archetype regularity.
- How free-form (organic) plans (a slum, an accreted manor) deviate from the clean bay tiling.

# 07 · place_interior_walls

> Tier: Site & shell. Part-1 status: **P** (fused into `realizeShell`). Schema: [`README.md`](README.md).

## Job
Raise the **partitions** on shared room boundaries — thinner than exterior walls — dividing the shell into the
rooms from generate_room_layout (#5).

## Reads
- Room rects + the portal graph from #5.
- Style: `interior_wall` thickness (`structure_styles.json`) — thinner than exterior.

## Emits
- A partition band straddling each shared room-boundary line (the current `sharedWall` + half-thickness straddle).
- `AssemblyPlan.walls` entries tagged `interior`.
- Reserves portal voids (door/arch) for cut_openings (#8).

## Algorithm
1. For each pair of adjacent rooms, find the **shared wall segment** (axis, coord, lo–hi) — the current `sharedWall()`.
2. Paint a partition of `intT` micro centered on the grid line, from wall base to wall top.
3. Where the portal graph says these rooms connect, leave/mark the door void (handed to #8).
4. Tag load-bearing partitions (those carrying an upper floor — stack_stories #36) vs non-bearing.

## Satisfies (checks)
E (interior thickness, thinner than exterior), F (partitions match the room layout), G (partitions don't sever reachability — doors where the graph says).

## Engine capability needed
- Straddle-band paint on a grid line — ✅ (`fillMicroBox`).
- Shared-wall detection — ✅ (`sharedWall()` exists).

## Failure modes
- Interior walls as thick as exterior (the fused default) → E-violation + wasted space.
- A partition sealing a room with no door → G-violation (coordinate the void with #8).
- A non-load partition placed where an upper floor needs bearing → D (stack_stories).

## Function testers
- **F1** Partitions = the interior thickness (< exterior).
- **F2** A partition on every shared boundary that the layout separates.
- **F3** A door void reserved wherever the portal graph connects two rooms.
- **F4** Load-bearing partitions identified for upper-floor support.

## Grounding
- Interior-wall thickness — REUSE `structure_styles.json` (`interior_wall`, e.g. 0.222 m; thinner than exterior).

## Open questions
- Stud/lath partition vs masonry partition by status/period — affects thickness + the load tag.

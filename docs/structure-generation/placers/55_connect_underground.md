# 55 · connect_underground

> Tier: Subterranean. Part-1 status: **M** (engine-cap missing). Schema: [`README.md`](README.md).

## Job
Link cellars ↔ tunnels ↔ sewers ↔ crypts ↔ dungeon into one **navigable graph** — the multi-level connectivity
the underground needs.

## Reads
- All the subterranean elements (#34/#51–54) + surface cellars (#35); the link stubs (#49).

## Emits
- The connecting passages/stairs/shafts/hatches that join the underground into one navigable graph; a **multi-level nav-mesh**.

## Algorithm
1. Connect the elements per the link stubs (cellar → sewer; crypt → catacomb; dungeon → tunnel).
2. Build the connecting passages/stairs/shafts.
3. Generate a multi-level nav-graph.

## Satisfies (checks)
BB3 (connected graph — every chamber reachable; vertical links), the **multi-level connectivity gap**.

## Engine capability needed
- **Multi-level void connectivity + nav-mesh** — ❌ MISSING (the second core subterranean gap, with #50).

## Failure modes
- Orphaned voids (unreachable); no vertical links between levels.

## Function testers
- **F1** Every subterranean chamber reachable from an entrance.
- **F2** Vertical links (stairs/ladders/shafts) between levels.
- **F3** A navigable multi-level graph.

## Grounding
- Walk/crawl clearances — Part 8; reuse #50.

## Open questions
- Nav-mesh across stacked underground levels (the same gap as multi-story navgrid, #30/#36).

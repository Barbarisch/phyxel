# 53 · excavate_dungeon

> Tier: Subterranean. Part-1 status: **M**. Schema: [`README.md`](README.md).

## Job
Carve the **dungeon** under a castle/keep — cells, oubliettes, corridors, chambers, a torture room — grim and
secure. The adventure-site layer.

## Reads
- The castle/keep (#46 / keep sheet); brief (a dungeon); the `gaol` sheet; Part 8.

## Emits
- A dungeon: **cells** (barred), **oubliettes** (top-entry), corridors, chambers, a torture room — under the keep/gatehouse, reached from above.

## Algorithm
1. Excavate (via #50) under the keep/gatehouse.
2. Lay cells + corridors + an **oubliette** (top-hatch) + chambers.
3. Reach **from above** (a stair from the keep/guardroom); secure (locked/barred, no easy egress).

## Satisfies (checks)
BB6 (grim + secure dungeons, fit under the keep), BB8 (playability), the `gaol` testers.

## Engine capability needed
- Chamber/corridor carve — ❌ (depends on #50); secret/trap mechanisms — ⚠️ (#56).

## Failure modes
- A floating dungeon (not under the keep); easy egress (not secure); not crawlable/playable.

## Function testers
- **F1** Cells (barred/locked) + an oubliette (top-entry) + corridors **under** the keep.
- **F2** Reached from above.
- **F3** Secure (no easy egress).
- **F4** Crawlable + playable (BB8).

## Grounding
- Cell dims — REUSE the `gaol` sheet (Lancaster etc., cited); oubliette — Part 8 / gaol.

## Open questions
- Natural-cave dungeon vs built; traps (ties to #56) as a playability layer.

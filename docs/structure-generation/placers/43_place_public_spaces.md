# 43 · place_public_spaces

> Tier: Settlement. Part-1 status: **M**. Schema: [`README.md`](README.md).

## Job
Lay the **public realm** — market square, public water, a green, justice furniture (gallows/pillory/stocks), the
churchyard.

## Reads
- The street crossing (#39 — the market); districts (#41); the church (#45); Part 7 public-realm.

## Emits
- A **market square** at the main crossing (the social heart); **public water** (wells/conduits/a fountain) reachable from every district; a green; **gallows/pillory/stocks** (justice); the churchyard.

## Algorithm
1. Place the market square at the high-street crossing.
2. Distribute public water so every district can reach it (AA1).
3. A green; justice furniture in/near the market; the churchyard by the church.

## Satisfies (checks)
AA1 (water reachable), AA10 (a public square at the crossing), AA5 (governance/justice furniture), W (public space).

## Engine capability needed
- Square/well/prop placement — ✅/⚠️.

## Failure modes
- No public water (AA1); no market square (AA10); a market not at the crossing.

## Function testers
- **F1** A market square at the main crossing.
- **F2** Public water reachable from every district.
- **F3** Justice furniture (gallows / pillory / stocks).
- **F4** A green; the churchyard.

## Grounding
- Market-square size (Kraków 3.79 ha exceptional; typical smaller — Part 7, flagged); water-per-N — `to_ground`.

## Open questions
- A market **cross** / conduit head as a focal monument; market-day stalls (ties to #48).

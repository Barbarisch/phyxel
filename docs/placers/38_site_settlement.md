# 38 · site_settlement

> Tier: Settlement (see Part 7). Part-1 status: **M**. Schema: [`README.md`](README.md).

## Job
Choose + **justify** the settlement location and set the growth seed + axis — the "why here" gate at town scale.

## Reads
- Brief (settlement type, why-here hints); region terrain (river / coast / hill / crossroads / resource).

## Emits
- The chosen site + its **justification** (water / defence / crossroads / harbour / resource); the **growth seed** (founding feature — a crossing, a market, a castle, a ford) + the primary **axis**.

## Algorithm
1. Evaluate candidate sites for water + defensibility + a route/crossroads/harbour + a resource.
2. Pick + record the reason.
3. Set the seed feature + axis (organic grows from it; a planned grid is laid from it).

## Satisfies (checks)
X1 (location justified), AA (the site can support the settlement — water at hand).

## Engine capability needed
- Site evaluation logic — ⚠️; terrain query — ✅.

## Failure modes
- An arbitrary site (no water/defence/route — X1); a town with no water source nearby (AA1).

## Function testers
- **F1** The location cites a real reason (water / defence / route / harbour / resource).
- **F2** A growth seed + axis are set.
- **F3** A water source is at hand.

## Grounding
- Settlement-siting logic — REUSE Part 7 (qualitative).

## Open questions
- Multi-reason sites (a defensible river crossing) — weight the factors.

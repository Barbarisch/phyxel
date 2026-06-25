# 44 · place_bridges

> Tier: Settlement. Part-1 status: **M**. Schema: [`README.md`](README.md).

## Job
Build **bridges** where streets cross the river/ditch — with bridge houses on a major span, a fortified gate
where a bridge meets the wall.

## Reads
- The river/ditch + the streets crossing it (#39); the wall/gate (#42); brief.

## Emits
- Bridges at street/water crossings; **houses/shops** on a major medieval bridge; a fortified **gate-bridge** where it meets the wall.

## Algorithm
1. At each street/water crossing, build a bridge (timber/stone by status/span).
2. A major bridge may carry houses/shops (the medieval bridge).
3. A bridge at the wall = a fortified gate-bridge (→ `compose_compound` #46).

## Satisfies (checks)
X5 (bridges on through-routes, passable), L, BB.

## Engine capability needed
- Bridge structure — ✅; **river context (water)** — ❌ (water gap; a **ditch** bridge works, a river bridge needs water).

## Failure modes
- A street crossing water with no bridge; a bridge too narrow for carts.

## Function testers
- **F1** A bridge at every street/water crossing.
- **F2** Cart-passable.
- **F3** A major bridge may carry buildings.
- **F4** A wall-crossing bridge is gated.

## Grounding
- Bridge width — REUSE Part 7 street widths; span/material by status — `to_ground`; **river = water = BLOCKED** (ditch bridge OK).

## Open questions
- Drawbridge / lifting span at the wall gate (ties to #31/#42).

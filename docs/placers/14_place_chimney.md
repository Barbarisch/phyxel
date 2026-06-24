# 14 · place_chimney

> Tier: Closure & roof. Part-1 status: **M**. Schema: [`README.md`](README.md).

## Job
Run a **flue from every hearth/oven/forge/furnace up through the roof** — the stack, the flue void, the cap, and
weathered roof penetration. (Where period-correct, a **smoke louvre** instead.)

## Reads
- Vented fixtures + positions (hearths, ovens, forges, furnaces) from place_fixtures (#17) / the room programs.
- Roof + floors (#13/#11); brief period (chimney vs louvre).

## Emits
- A **masonry stack** from each firebox up through floors + roof, with a **flue void** inside.
- A **pot/cap** above the ridge line; **flashing** at the roof penetration.
- Or (early/poor) a **smoke louvre/hole** in the roof over a central open hearth.

## Algorithm
1. For each vented fixture, trace a flue from the firebox straight up, through any floors + the roof.
2. Build the masonry stack around the flue; exit **above the roof**, clearing the ridge for draught.
3. Weather the penetration (flashing/saddle).
4. If the period uses an open central hearth (croft/early hall) → a **smoke louvre** in the roof instead of a chimney.

## Satisfies (checks)
I (roof penetration weathered), K (the kitchen / forge / bakery / brewery "vented" fixture testers), B (heating + smoke management).

## Engine capability needed
- Stack/flue paint through floors+roof — ✅.
- **Roof-penetration flashing** — ⚠️ (weathering detail).

## Failure modes
- A hearth with **no flue** → smoke-filled room (K-violation).
- A flue not clearing the ridge → downdraught.
- An unweathered penetration (leak).
- A chimney on a croft that historically used a louvre (A).

## Function testers
- **F1** Every vented fixture has a flue running to **above** the roof.
- **F2** The stack clears the ridge.
- **F3** The roof penetration is flashed.
- **F4** Period-correct: a smoke louvre where a central open hearth is used, a chimney otherwise.

## Grounding
- Chimney height above the ridge — `to_ground` (draught rule).
- Chimney vs smoke-louvre by period — REUSE the croft/hall_house sheets (open hearth + louvre early).

## Open questions
- Shared stacks (back-to-back hearths) in a townhouse; multiple flues in one stack.

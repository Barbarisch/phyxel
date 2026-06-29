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
- **Chimney top ≥ 0.610 m (2 ft / 610 mm) above the ridge / any roof part within 10 ft** (and ≥ 3 ft /
  914 mm above the penetration) — the **3-2-10 rule, IRC R1003.9** ([UpCodes IRC-2015 ch.10](https://up.codes/viewer/maine/irc-2015/chapter/10/chimneys-and-fireplaces)).
  MODERN code, but the clearance is **draught-physics driven** (period-invariant); the medieval principle
  is identically "clear the ridge for draught" (disclosed anachronism, like the anvil work-height). NOTE:
  historic thatch guidance (IHBC) wants ~1.8 m above a thatch ridge — so 0.610 m is a conservative FLOOR.
  Engine: chimney top = roof apex + **6 micro (0.667 m)** — clears the 2 ft minimum (5 micro / 0.556 m
  would FAIL it; grounding-auditor 2026-06-28).
- **Flue void ≥ 1/10 of the firebox opening area** (square flue; **IRC R1003.15.1** —
  [ICC R1003.15.1](https://codes.iccsafe.org/s/IRC2015V5.0/chapter-10-chimneys-and-fireplaces/IRC2015V5.0-Pt03-Ch10-SecR1003.15.1);
  *not* IBC 2113.16, the commercial code). Firebox ≈ 0.44 m² → flue ~0.2 m square → ~2 micro void
  (0.222 m → 0.049 m² > the 0.044 m² minimum). **Stack = flue + masonry ≈ 0.45–0.56 m (4–5 micro) square.**
  ⚠ **NEEDS-RESEARCH:** the **firebox opening area (0.44 m²)** is currently UNSOURCED — consult Neufert
  *Architects' Data* / a medieval hall-hearth survey; and the **stack wall thickness** (engine = 1 micro /
  0.11 m, thin for masonry) — consult Brunskill *Vernacular Architecture* (English medieval ≥215 mm brick /
  ≥300 mm rubble). Both flagged in `GroundingGaps.md`.
- Chimney vs smoke-louvre by period — REUSE the croft/hall_house sheets (open hearth + louvre early).

## Open questions
- Shared stacks (back-to-back hearths) in a townhouse; multiple flues in one stack.

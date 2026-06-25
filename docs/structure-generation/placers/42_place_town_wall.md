# 42 · place_town_wall

> Tier: Settlement. Part-1 status: **M**. Schema: [`README.md`](README.md). Reuses place_fortifications (#31).

## Job
Build the **wall circuit + towers + gates** around the core (reusing #31), with a ditch; mark intramural vs
extramural growth.

## Reads
- The built-up extent; brief defensibility; Part 7 wall dims; place_fortifications (#31).

## Emits
- A wall circuit enclosing the core (curtain + towers + gates via #31), **gates on the through-routes** (the high streets), a ditch; intramural (walled) vs extramural (suburb) growth marked.

## Algorithm
1. Trace the circuit around the built-up core.
2. Reuse #31 for the wall/towers/gates.
3. Place **gates where the high streets exit** (#39); a ditch outside.
4. Flag extramural suburbs (growth beyond the wall).

## Satisfies (checks)
X5 (gates on through-routes, passable), Q (town defense via #31), AA4 (defense sized to the settlement).

## Engine capability needed
- Reuse #31 — ✅ (spec); circuit-tracing — ⚠️.

## Failure modes
- Gates not on the through-routes (X5); a circuit that orphans part of the core; a town wall as thick as a castle curtain (Part 7: towns were **thinner**).

## Function testers
- **F1** A continuous circuit enclosing the core.
- **F2** Gates on the high-street through-routes (cart-passable).
- **F3** A ditch.
- **F4** Intramural vs extramural marked.

## Grounding
- Town wall **~9 m** high, **thinner** than a castle curtain (Part 7, flagged); gate (Byczyna 6.8 × 9.5 m, single-source); reuse #31.

## Open questions
- Successive circuits as the town grew (an inner + outer wall); reused Roman walls (spolia, CC).

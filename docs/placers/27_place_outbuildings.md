# 27 · place_outbuildings

> Tier: Parcel. Part-1 status: **M**. Schema: [`README.md`](README.md). Composes the Agriculture archetype sheets.

## Job
Site the **outbuildings** — barn, byre, stable, granary, dovecote, sty, well-house, privy, smithy — around the
yard, each per its archetype sheet, positioned sensibly relative to the dwelling.

## Reads
- The farmstead/manor plot; the dwelling; the **Agriculture archetype sheets** (byre/stable/barn/granary/dovecote/pigsty); the noxious-siting rule (Z3); fire rules.

## Emits
- The needed outbuildings (built per their sheets) placed around the yard with correct relationships.

## Algorithm
1. Determine the needed set from the brief/archetype (a farmstead → barn + byre + granary; a manor → + stable + dovecote).
2. Place each per its sheet at its sensible spot: **barn near the fields**; **byre/stable on the yard**; **sty/privy/midden downwind + away from the well**; **dovecote peripheral**; **smithy/bakehouse fire-clear** of thatch.

## Satisfies (checks)
P (outbuildings sited sensibly), Z3 (noxious downwind/downstream), AA (the farmstead functions — has what it needs).

## Engine capability needed
- **Compose the agriculture archetype builds** — ⚠️ (needs the archetype realizer + the `compose_compound`-style placement logic).

## Failure modes
- A sty/privy next to the well (Z3 / sanitation).
- A smithy/bakehouse under thatch beside the barn (fire).
- A barn far from the fields.

## Function testers
- **F1** Each outbuilding built per its sheet.
- **F2** Noxious buildings (sty / privy / midden / tannery) downwind + away from the well/dwelling.
- **F3** Smithy/bakehouse fire-clear.
- **F4** Barn near the fields/yard.
- **F5** The farmstead has the outbuildings it functionally needs (AA).

## Grounding
- Siting rules — REUSE Z3 + the agriculture archetype sheets (cited).
- Inter-building spacing — `to_ground`.

## Open questions
- Courtyard farmstead (buildings around a yard) vs scattered, by region/period.

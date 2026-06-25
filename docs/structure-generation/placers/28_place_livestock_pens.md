# 28 · place_livestock_pens

> Tier: Parcel. Part-1 status: **M**. Schema: [`README.md`](README.md).

## Job
Lay out **pens / folds / runs** adjacent to the farmstead — sized to the herd, with feed + water + a muck-out
sited away from the dwelling and water.

## Reads
- The farmyard; the animals (cattle / sheep / pigs / poultry); the byre + pigsty sheets; Z3.

## Emits
- Pens / folds / runs (fenced — reuse #22 — or walled), feed + water, a muck-out + midden; sited by species.

## Algorithm
1. Per species, a pen sized to the herd: a cattle yard, a sheep fold, a pig sty pen, a poultry run.
2. Enclose (hurdle / fence #22 / wall) with **feed + water** and a **muck-out to a midden**.
3. Site **downwind + away from the well/dwelling**, near the relevant byre/sty.

## Satisfies (checks)
P (pens), Z3 (muck away from dwelling/water), the byre/stable/pigsty function testers.

## Engine capability needed
- Pen/fence (reuse #22) — ✅; muck/midden prop — ⚠️.

## Failure modes
- A pen draining toward the well (Z3).
- No feed/water in the pen.
- Muck heaped against the dwelling.

## Function testers
- **F1** Pens sized to the herd, enclosed.
- **F2** Feed + water present.
- **F3** A muck-out to a midden, downwind + away from the well/dwelling.
- **F4** Sited by species near the relevant byre/sty.

## Grounding
- Siting — REUSE Z3 + the byre/pigsty sheets; pen sizes per head — `to_ground`.

## Open questions
- Seasonal folding (sheep folded on fallow to manure it) — ties to #26 + S.

# 41 · zone_districts

> Tier: Settlement. Part-1 status: **M**. Schema: [`README.md`](README.md).

## Job
Assign **districts** by wealth + trade — noxious trades to the edge (downwind + downstream), a mixed-use core —
the map that drives which archetype lands on each plot.

## Reads
- The plots (#40); the seed/centre (#38); brief character; checklist Z.

## Emits
- A district map: **wealth tiers** (rich near the centre/castle/minster; poor at the periphery/marsh/wall); **trade clusters** (a smiths' street, the shambles); **noxious trades** (tanners/dyers/slaughter) at the edge, downwind + downstream; the mixed-use core.

## Algorithm
1. Gradient wealth from the centre outward.
2. Cluster trades into quarters + **name** them (Tanner's Row…).
3. Banish **noxious** trades to the downwind/downstream edge.
4. Mark the mixed-use core. This map drives the per-plot archetype (#45).

## Satisfies (checks)
Z1 (district character), Z2 (trades cluster), Z3 (noxious downwind/downstream), Z4 (spatial wealth gradient), Z5 (mixed-use core), BB.

## Engine capability needed
- Zoning logic — ⚠️.

## Failure modes
- A uniform town (no rich/poor/craft distinction — Z1); tanners upwind of the centre (Z3); no wealth gradient (Z4).

## Function testers
- **F1** Distinct districts (wealth + trade).
- **F2** Trades clustered + named.
- **F3** Noxious trades downwind + downstream of dwellings + the water intake.
- **F4** A spatial wealth gradient.
- **F5** A mixed-use core.

## Grounding
- Noxious siting — REUSE Z3; wealth gradient — qualitative (Part 7).

## Open questions
- Ethnic/guild quarters (a Jewry, a Lombard street); a wealth gradient that follows topography (uphill = rich).

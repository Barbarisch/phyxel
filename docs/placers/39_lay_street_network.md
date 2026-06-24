# 39 · lay_street_network

> Tier: Settlement. Part-1 status: **M**. Schema: [`README.md`](README.md).

## Job
Lay the **street hierarchy** (high street → lane → alley → court) from the seed; market at the main crossing;
widths from canon.

## Reads
- The seed + axis (#38); organic vs planned (brief); the Part 7 street-width derivation.

## Emits
- A connected street graph: high street(s) linking gate → market, lanes branching, alleys/courts infilling; the **market at the main crossing**; widths per the hierarchy.

## Algorithm
1. **Organic** = curving lanes accreting around the seed; **planned** = a bastide grid.
2. The high street links the **gate → market crossing**; lanes branch; alleys/courts infill.
3. Assign widths: high street ≈ two carts pass; lane ≈ one cart; alley ≈ foot.

## Satisfies (checks)
X2 (hierarchy + widths), X3 (connected — every plot → street → gate), X4 (form matches origin).

## Engine capability needed
- Street graph + paint — ⚠️.

## Failure modes
- Orphaned blocks (X3); uniform-width streets (X2); a grid where the brief says organic (X4).

## Function testers
- **F1** A hierarchy (high / lane / alley).
- **F2** The high street links gate → market.
- **F3** Connected (no orphans).
- **F4** Form matches origin (organic / planned).
- **F5** Widths per the canon.

## Grounding
- Street widths — REUSE Part 7 (high ≈ two-cart, lane ≈ one-cart, alley ≈ foot; the **metric flagged**); market at the crossing.

## Open questions
- Topography-driven streets (following contours) on a hill town.

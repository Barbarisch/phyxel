# 46 · compose_compound

> Tier: Settlement. Part-1 status: **M**. Schema: [`README.md`](README.md).

## Job
Realize **compound archetypes** (castle / monastery / cathedral) as a walled mini-settlement of sub-buildings —
the composition the Part 6 sheets flag.

## Reads
- A compound archetype (castle / monastery / cathedral sheet); its sub-building list; the site.

## Emits
- The compound: its **enceinte/precinct** (wall + gate, reuse #42/#31), its **sub-buildings** (each via #45 / #1–37), its internal **circulation** (baileys / courts / cloister), composed per the sheet.

## Algorithm
1. From the compound sheet, lay the precinct + internal zones (castle = curtain + baileys; monastery = church + claustral ranges + outer court; cathedral = church + chapter house + cloister).
2. Place each sub-building (run the building pipeline per sub-building).
3. Connect them per the sheet's adjacencies.

## Satisfies (checks)
W5 (compound composes sub-buildings, **not a mega-room**), the castle/monastery/cathedral F-testers, Q/R as applicable.

## Engine capability needed
- Composition orchestration — ⚠️; depends on #45 + #42/#31.

## Failure modes
- A castle as one mega-building (W5).
- A monastery missing the cloister adjacency.
- Sub-buildings not connected.

## Function testers
- **F1** The compound = a precinct + sub-buildings + internal circulation (not one room).
- **F2** The sheet's adjacencies hold (bailey layers; cloister ranges).
- **F3** Each sub-building built via the pipeline.

## Grounding
- REUSE the castle / monastery / cathedral sheets (cited).

## Open questions
- Compound-within-compound (a castle with a chapel that is itself a small church program).

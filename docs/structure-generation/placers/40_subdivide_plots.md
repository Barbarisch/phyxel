# 40 · subdivide_plots

> Tier: Settlement. Part-1 status: **M**. Schema: [`README.md`](README.md).

## Job
Subdivide the street frontage into **burgage plots** (narrow × deep), with fractional-perch splits and back lanes.

## Reads
- The street net (#39); the Part 7 burgage canon.

## Emits
- Burgage plots (narrow frontage × deep) fronting the streets; fractional-perch subdivisions in dense areas; back lanes serving the rears.

## Algorithm
1. Along each street frontage, lay burgage plots (~5–10 m frontage, deep to ~60 m).
2. Subdivide in **fractional-perch** units where demand is high (the core).
3. Add **back lanes** serving the plot rears.

## Satisfies (checks)
Y1 (burgage plots + party walls, not freestanding), Y2 (frontage/depth from canon, fractional-perch), Y3 (density gradient), Y4 (back-plots).

## Engine capability needed
- Plot subdivision logic — ⚠️.

## Failure modes
- Freestanding cottages-with-yards in the core (Y1); uniform plots ignoring the density gradient (Y3).

## Function testers
- **F1** Burgage plots front the streets (narrow × deep).
- **F2** Subdivision in fractional-perch units; party walls in the core.
- **F3** Back lanes serving the rears.
- **F4** Density gradient (dense core → loose edge).

## Grounding
- Burgage **16–18 × 60 m**, frontage **~5–10 m**, perch **5.03 m** — REUSE Part 7 (cited).

## Open questions
- Amalgamation (rich owners merging plots) vs subdivision (poor splitting them) — the wealth signal.

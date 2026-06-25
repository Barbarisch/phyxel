# 26 · place_farm

> Tier: Parcel. Part-1 status: **M**. Schema: [`README.md`](README.md).

## Job
Lay out the farmland — **arable strips (ridge-and-furrow), pasture, and fallow** — sized to the plot, in the
field pattern of the period.

## Reads
- The farmstead plot (beyond the croft/manor); brief (arable/pasture/mixed, crops, season, region); terrain.

## Emits
- **Arable selions** (ridge-and-furrow strips) in furlongs; **pasture** (grazing); **fallow**; crop rows; the rotation pattern; field boundaries (handed to #22/#23).

## Algorithm
1. Lay **selions** (~1 furlong long × ~9–10 yd wide) in furlong blocks, as ridge-and-furrow.
2. Assign **rotation** (arable / fallow / pasture — the three-field pattern).
3. Size to the plot; crops by region + season.
4. Bound the fields with fences/walls (#22/#23).

## Satisfies (checks)
P (agriculture — fields/furrows/pasture/fallow), B (crops by climate/season), S (seasonal crop state).

## Engine capability needed
- Terrain shaping for ridge-and-furrow relief — ⚠️ (a mild terrain edit); crop/flora placement — ✅.

## Failure modes
- Square modern fields instead of strips (A).
- Crops out of climate/season (B).
- Flat "ridges" with no furrow relief.

## Function testers
- **F1** Arable as **ridge-and-furrow selions** (~furlong × ~9–10 yd; ridges up to ~0.61 m).
- **F2** Pasture **and** fallow present (a rotation, not all-arable).
- **F3** Crops by climate + season.
- **F4** Fields bounded (fence/wall/hedge).

## Grounding
- Selion **1 furlong (220 yd ≈ 200 m) long × 5–22 yd wide** (9–10 common) = 0.25–1 acre; ridges spaced 3–22 yd, up to **0.61 m (24") high** — CITED ([Ridge and furrow (Wikipedia)](https://en.wikipedia.org/wiki/Ridge_and_furrow); selion).
- Three-field rotation — standard medieval practice.

## Open questions
- Open-field (communal strips) vs enclosed (hedged closes) by period/region.
- Crop palette by region/season — a flora-canon expansion (ties to S).

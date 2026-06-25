# 06 · place_exterior_walls

> Tier: Site & shell. Part-1 status: **P** (fused into `realizeShell`). Schema: [`README.md`](README.md).

## Job
Raise the **perimeter walls** — style material + thickness, full story height, on the foundation — forming the
weather envelope.

## Reads
- Footprint perimeter + foundation from #3; story height from the program.
- Style: `exterior_wall` thickness + structure material (`structure_styles.json`); `thicknessMicro()` → micro band.

## Emits
- A perimeter wall band (thickness from style, e.g. 0.222 m → 2 micro; 0.667 m manor; 2–6 m castle) from the wall base to the wall top, at the correct resolution (subcube-thick coarsens cheaply).
- `AssemblyPlan.walls` entries tagged `exterior`.
- Leaves opening voids to cut_openings (#8) — or coordinates so #8 carves after.

## Algorithm
1. Walk the footprint perimeter (cells with an exterior-facing edge).
2. For each exterior edge, paint a wall band of `extT` micro inward from the edge, from `wallBase` to `wallTop` (the current `paintEdgeBand`).
3. Choose resolution: subcube-thick (≤ 3 micro) coarsens to subcubes (cheap); thinner stays micro (flag the cost).
4. Tag corners (quoins) + the material for place_trim (#15).
5. Record wall lines for load (place_foundation #3 must have a footing under each).

## Satisfies (checks)
E (wall thickness grounded + by style), D (walls bear on the foundation), A/J (period-correct construction + material), N (resolution/cost).

## Engine capability needed
- Micro/subcube band paint — ✅ (`fillMicroBox` / `addSubcube`, `thicknessMicro`).
- Subcube coarsening on export — ✅ (MicroCanvas greedy-coarsen).

## Failure modes
- 1 m (full-cube) walls where the style says 0.222 m (the original Minecraft-wall complaint) → E-violation.
- A wall with no footing below → D-violation.
- Micro-thick walls everywhere → instance-count blowup (N) — prefer subcube where the style allows.

## Function testers
- **F1** Perimeter walls = the style thickness (not a default cube).
- **F2** Walls run full story height, base to top, on the foundation.
- **F3** Subcube-thick walls coarsen to subcubes (cost check).
- **F4** A footing exists under every exterior wall line.
- **F5** Corners/quoins tagged for trim.

## Grounding
- Wall thickness — REUSE `structure_styles.json` (`exterior_wall`; e.g. timber_cottage 0.222 m, stone_manor 0.667 m, castle 2–6 m — all cited).

## Open questions
- Gable-end vs eaves walls (the gable triangle is place_roof #13's, not here) — boundary already drawn in `realizeShell`.
- Timber-frame expression (posts + infill panels) vs a solid band — a J/finish refinement.

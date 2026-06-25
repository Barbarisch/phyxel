# 01 · analyze_site

> Tier: Site & shell. Part-1 status: **P** (median seat only). Schema: [`README.md`](README.md).

## Job
Sample the terrain under (and around) the footprint → grade, slope, obstructions, water, approach direction —
the site facts every later placer needs. **Reads the world; emits no voxels.**

## Reads
- `AssemblyPlan`: footprint rect(s), orientation.
- Brief: site fields (approach, orientation), region/climate.
- World: terrain height + material under and just beyond the footprint (the chunk grid).

## Emits
- A **SiteReport** on the `AssemblyPlan`: per-cell ground height + material; min/median/max grade; slope vector + magnitude; water cells; obstruction cells (existing structures/voxels/trees); the open **approach** edge.
- **No voxels.**

## Algorithm
1. Sample ground height at every footprint cell (+ a 1–2 cell skirt) by ray/column probe down the chunk grid.
2. Compute min / median / max height and the dominant slope (plane fit or min↔max over run).
3. Classify cells: solid ground / water / void(overhang) / obstructed (non-terrain voxels = a prior structure, a tree).
4. Pick the **approach edge** = the lowest, most-open, road-facing side (brief orientation wins if set).
5. Write the SiteReport; flag "unbuildable" conditions (mid-water, extreme slope, fully obstructed) for the pipeline.

## Satisfies (checks)
L1 (siting justified), L (site/parcel context), D-pre (bearing data for foundations), and feeds prepare_pad (#2) + place_foundation (#3) + place_entry (#20).

## Engine capability needed
- **Terrain height/material probe under a footprint** — ⚠️ partial (`get_terrain_height` exists; a batched per-cell + material + obstruction scan is the gap).
- Existing-structure/voxel occupancy query (obstruction test) — ✅ (chunk/occupancy query).

## Failure modes
- Footprint over water / a cliff / another building → must flag, not silently seat (the prior "two stacked houses" bug came from sampling a *prior build's* voxels as ground — obstruction classification fixes it).
- Unloaded chunks → must force-load or fail, not read zeros.

## Function testers
- **F1** Every footprint cell has a ground height + material.
- **F2** Slope magnitude + vector computed.
- **F3** Water/obstruction cells flagged.
- **F4** An approach edge chosen.
- **F5** Prior-structure voxels are classified as *obstruction*, never as *ground*.

## Grounding
- Slope/grade are measured, not assumed. "Extreme slope" threshold → see prepare_pad (#2) `to_ground`.

## Open questions
- Skirt width (how far beyond the footprint to sample) — `to_ground` (function of eaves + path).

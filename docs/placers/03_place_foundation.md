# 03 · place_foundation

> Tier: Site & shell. Part-1 status: **P** (crawlspace ring; basement = 3-cube void stub). Schema: [`README.md`](README.md).

## Job
Carry the structure's load to bearing — **footings** (stepped on a slope) + **foundation/plinth walls** — and
form the chosen substructure: **slab | crawlspace | basement**.

## Reads
- The flat datum + bearing from prepare_pad (#2) / analyze_site (#1).
- `AssemblyPlan`: `substructure` (slab/crawlspace/basement), footprint, perimeter vs internal load lines.
- Style: foundation material + `foundation_wall` thickness (`structure_styles.json`).

## Emits
- **Footings** under every load line (perimeter + major internal walls), dug to bearing, **stepped** on a slope.
- **Foundation/plinth walls** from footing to the floor datum.
- The substructure void: **slab** (solid to grade), **crawlspace** (a shallow vented void — current `crawlH=1`), or **basement** (hand the occupiable void to excavate_basement #34 + place_basement #35).
- `AssemblyPlan.foundation` entries (the current `plan.foundation` columns).

## Algorithm
1. Identify load lines = the perimeter + the internal walls from generate_room_layout (#5).
2. Under each, dig a footing trench to bearing (below frost/topsoil); **step** the footing down a slope so each run stays level.
3. Raise foundation/plinth walls (style thickness — thicker than the wall above) to the floor datum.
4. Substructure: slab → fill solid; crawlspace → leave a vented ~1-cube void + perimeter ring (current behavior); basement → defer the occupiable box to #34/#35.
5. Emit a damp course / plinth offset where the wall above meets the foundation.

## Satisfies (checks)
D1–D4 (load path, bearing, no floating), E (foundation thickness), V4 (basement occupiability — via #34/#35).

## Engine capability needed
- **Trench excavation to bearing** — ❌ MISSING (terrain dig; the prepare_pad gap again).
- Stepped-footing emit on slope — ⚠️ (logic missing; voxel paint exists).
- Wall/box paint — ✅ (`fillMicroBox`).

## Failure modes
- Footing not reaching bearing on a slope → the perched-foundation look (the current 1-cube stub).
- Internal load lines ignored → upper floors/roof bear mid-span (D-violation).
- `basement` left as a sealed void (the current stub) → V1/V4 fail.

## Function testers
- **F1** A footing under every load line, dug to bearing.
- **F2** Footings **stepped** (each run level) on a slope.
- **F3** Foundation walls ≥ the wall-above thickness, to the floor datum.
- **F4** Substructure matches the brief (slab/crawlspace/basement), and `basement` is handed to #34/#35 (not a sealed void).
- **F5** A damp course / plinth offset present.

## Grounding
- **Footing depth to bearing / frost** — `to_ground` (region/frost-dependent).
- Foundation-wall thickness — REUSE `structure_styles.json` (`foundation_wall`, ≥ the exterior wall; ground/keep canon for stone).

## Open questions
- Frost depth by climate (ties to the brief's region/climate) — `to_ground`.
- Pad-and-trench interaction with prepare_pad (#2) — one excavation pass or two?

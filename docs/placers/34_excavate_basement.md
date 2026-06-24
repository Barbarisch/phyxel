# 34 · excavate_basement

> Tier: Vertical. Part-1 status: **M** (basement is a sealed 3-cube void stub today). Schema: [`README.md`](README.md). See Part 5.

## Job
Dig the **below-grade box** into terrain — spoil removal, drainage, window wells / a bulkhead — so place_basement
(#35) has a real void to line.

## Reads
- `substructure = basement` (handed off by place_foundation #3); footprint; the basement story height; the SiteReport water table (#1); Part 5 grounded dims.

## Emits
- An **excavated void** below the footprint (to basement depth); spoil removed; a **drainage** provision; **window wells** at grade (habitable) or an external **bulkhead** stair.

## Algorithm
1. Excavate the footprint box down to the basement floor depth (= headroom + floor + structure).
2. Remove spoil (or reuse as pad fill, #2).
3. Provide **drainage** (the damp constraint — V6).
4. Cut **window wells** at grade (habitable) or an external **bulkhead** stair; hand the void to #35.

## Satisfies (checks)
V4 (excavated into terrain, not perched), V5 (light/air — wells), V6 (damp/drainage), and the **core excavation gap**.

## Engine capability needed
- **Terrain excavation** — ❌ MISSING (the keystone gap, shared with prepare_pad #2 + the subterranean tier).

## Failure modes
- A **perched box** (not excavated — the current stub).
- No drainage (damp).
- No light/access for a *habitable* basement.

## Function testers
- **F1** The footprint excavated to basement depth (a real void in terrain).
- **F2** Spoil handled (removed or reused).
- **F3** Drainage present.
- **F4** Window wells (habitable) or a bulkhead (access).
- **F5** Not perched.

## Grounding
- Basement depth = headroom (**2.032 m** storage / **2.134 m** habitable, Part 5 cited) + floor + structure.
- Window well (IRC R310) — Part 5 cited.

## Open questions
- One excavation pass shared with prepare_pad (#2), or two? (the same engine capability).

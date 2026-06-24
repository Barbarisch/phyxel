# Placer Specs

Deep, per-placer specifications — the **HOW**. [`StructureGenerationPlacers.md`](../StructureGenerationPlacers.md)
Part 1 is the index (a one-line job + status per placer); the files here are the depth: enough that each placer
could be implemented deterministically, and each names the **exact engine capability it needs** so the set
doubles as a build-order. The placers realize the `AssemblyPlan` produced by the Part 10 pipeline.

## Why the depth

A one-line job ("`prepare_pad` — cut high / fill low to a level pad") doesn't say what it reads, what it emits,
the algorithm, the edge cases, the checks it must satisfy, or — critically — **which engine API it calls that may
not exist yet**. The spec makes all of that explicit, so implementation is transcription, not invention, and the
engine gaps surface as concrete line items.

## The placer-spec schema

1. **Identity** — #, id, tier, Part-1 implementation status (**P** partial / **M** missing / **D** done).
2. **Job** — one line.
3. **Reads** — brief fields, the `AssemblyPlan`, prior placer outputs, which canons.
4. **Emits** — voxels (at which resolution: cube / subcube / microcube), `AssemblyPlan`/plan entries, placed objects, registrations.
5. **Algorithm** — the deterministic steps.
6. **Satisfies (checks)** — the Part-2 checklist items this placer is responsible for / gated by.
7. **Engine capability needed** — the exact API/feature it calls (✅ exists / ⚠️ partial / ❌ missing). *The build-order signal.*
8. **Failure modes** — what goes wrong; edge cases.
9. **Function testers** — concrete pass/fail gates.
10. **Grounding** — cite / reuse-canon / to_ground for any dimension.
11. **Open questions.**

Coordinate convention (from the engine): **X = width, Y = up, Z = depth**; `rect = [x, z, w, d]`; 1 cube = 1 m;
the `MicroCanvas` is a 9×9×9 micro-grid per cube (greedy-coarsened on export).

## Index (59 placers, by tier, in build order)

**Site & shell (1–8)** — *this batch*
| # | placer | Part-1 status | spec |
|---|---|---|---|
| 1 | analyze_site | P | [01_analyze_site](01_analyze_site.md) |
| 2 | prepare_pad | M | [02_prepare_pad](02_prepare_pad.md) |
| 3 | place_foundation | P | [03_place_foundation](03_place_foundation.md) |
| 4 | place_floor (+ subfloor) | P | [04_place_floor](04_place_floor.md) |
| 5 | generate_room_layout | M | [05_generate_room_layout](05_generate_room_layout.md) |
| 6 | place_exterior_walls | P | [06_place_exterior_walls](06_place_exterior_walls.md) |
| 7 | place_interior_walls | P | [07_place_interior_walls](07_place_interior_walls.md) |
| 8 | cut_openings | P | [08_cut_openings](08_cut_openings.md) |

**Closure & roof (9–15)** — ✅ this batch
| # | placer | Part-1 status | spec |
|---|---|---|---|
| 9 | place_doors | M | [09_place_doors](09_place_doors.md) |
| 10 | place_windows | M | [10_place_windows](10_place_windows.md) |
| 11 | place_ceiling / intermediate_floor | P | [11_place_ceiling](11_place_ceiling.md) |
| 12 | place_stairs | M | [12_place_stairs](12_place_stairs.md) |
| 13 | place_roof | P | [13_place_roof](13_place_roof.md) |
| 14 | place_chimney | M | [14_place_chimney](14_place_chimney.md) |
| 15 | place_trim | M | [15_place_trim](15_place_trim.md) |
**Interior (16–20)** — ✅ this batch
| # | placer | Part-1 status | spec |
|---|---|---|---|
| 16 | place_furniture | **D** | [16_place_furniture](16_place_furniture.md) |
| 17 | place_fixtures | P | [17_place_fixtures](17_place_fixtures.md) |
| 18 | place_lights | M | [18_place_lights](18_place_lights.md) |
| 19 | place_clutter | M | [19_place_clutter](19_place_clutter.md) |
| 20 | place_entry | P | [20_place_entry](20_place_entry.md) |
**Parcel (21–29)** — zone_parcel, place_fence, place_boundary_wall, place_path, place_garden, place_farm, place_outbuildings, place_livestock_pens, place_yard_props — TODO
**Systems (30)** — register_systems — TODO
**Conditional structural (31–33)** — place_fortifications, place_graveyard, apply_seasonal_state — TODO
**Vertical (34–37)** — excavate_basement, place_basement, stack_stories, place_attic — TODO
**Settlement (38–49)** — site_settlement … link_subterranean — TODO
**Subterranean (50–57)** — excavate_subterrane … validate_crawlability — TODO
**Fantasy (58–59)** — author_world_bible, apply_setting_overlay — TODO

*(20 of 59 specced. Status mirrors Part 1; update a row when a spec lands.)*

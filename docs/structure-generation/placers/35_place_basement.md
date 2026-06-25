# 35 · place_basement

> Tier: Vertical. Part-1 status: **M**. Schema: [`README.md`](README.md). See Part 5.

## Job
Make the basement **occupiable** — retaining walls + a base slab + floor/ceiling — and run the per-story placers
at level −1 for the cellar rooms.

## Reads
- The excavated void (#34); style retaining-wall thickness (keep/foundation canon); the basement room program (cellar/undercroft/buttery).

## Emits
- Perimeter **retaining walls** (stone, thick — they hold back earth, *not* the thin timber wall); a **base slab** + drainage; a floor + the ceiling (= the ground floor's structural floor); then the cellar rooms via the per-story placers (#4–8); **access** (stair down #12 / bulkhead #34).

## Algorithm
1. Line the excavated box with **retaining walls** (≥ the masonry exterior thickness; stone).
2. Lay a **base slab** (flag / beaten-earth / brick) + drainage.
3. Run the per-story placers at **level −1** for the cellar rooms (floor/walls/openings).
4. Ensure **access** (an internal stair down via #12, or the bulkhead from #34).

## Satisfies (checks)
V4 (occupiable: retaining walls + base slab + headroom + access), the cellar/undercroft room programs.

## Engine capability needed
- Wall/slab paint — ✅; depends on **excavate_basement (#34)** ❌ + **stack_stories (#36)**.

## Failure modes
- Thin (non-retaining) walls "holding back earth".
- **No access** (a sealed void — the current stub, V1 fail).
- No base slab / damp control.

## Function testers
- **F1** Retaining walls (stone, thick) on all sides.
- **F2** A base slab + drainage.
- **F3** Headroom per use (Part 5).
- **F4** Reachable (stair / bulkhead).
- **F5** The cellar rooms built.

## Grounding
- Retaining-wall thickness — REUSE keep/foundation canon (≥ exterior masonry; ≥ 0.667 m stone) — Part 5/6 cited.
- Headroom — Part 5 cited.

## Open questions
- Vaulted undercroft (a stone barrel vault) as the high-status basement variant.

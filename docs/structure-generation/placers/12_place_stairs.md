# 12 · place_stairs

> Tier: Closure & roof. Part-1 status: **M** (the realizer never reads `ProgStair`). Schema: [`README.md`](README.md).

## Job
Build the **vertical circulation** — stairs between stories and down to a cellar — cutting the stairwell and
building the flight + landings + handrail. **The keystone of the vertical gap.**

## Reads
- `ProgStair` (fromStory, toStory, rect, kind: straight/spiral) — *currently parsed but never realized*.
- Floor + ceiling slabs (#11); story height; the stair canon (Part 5, IRC).

## Emits
- A **stairwell hole** cut through the floor **and** ceiling slab(s).
- The **flight**: steps (riser/tread), **landings** on long runs, a **balustrade/handrail**, and (spiral) a newel.

## Algorithm
1. For each `ProgStair`, get the rise = the story height; compute step count from the riser limit.
2. Geometry: riser ≤ 0.196 m, tread ≥ 0.254 m, width ≥ 0.914 m, headroom ≥ 2.032 m (IRC) — **or** a *grounded* period stair (medieval newel/ladder runs steeper + narrower).
3. **Cut the stairwell** through both the floor and the ceiling slab at the stair rect.
4. Build the steps; insert a **landing** where the run exceeds the limit or turns; add a handrail/balustrade; (spiral) build around a newel.
5. Register the stair as circulation (reachability).

## Satisfies (checks)
V1 (every story reachable), V2 (stair geometry / climbable), V3 (cut both slabs), G (circulation).

## Engine capability needed
- **Read `ProgStair` + realize it** — ❌ MISSING (the realizer ignores stairs entirely — the headline vertical gap).
- Slab-hole cut — ⚠️ (coordinate with #11's reservation).
- Step/landing/rail voxels — ✅ (paint).

## Failure modes
- `ProgStair` ignored → no stairs at all (current); upper floors/basement unreachable (V1).
- Un-climbable geometry (too steep / too narrow / headroom fouled).
- A floating flight; a hole in the floor but not the ceiling (or vice-versa).

## Function testers
- **F1** Every `ProgStair` is realized as a built flight.
- **F2** Geometry meets the comfort floor (IRC) **or** a grounded period stair — never un-climbable.
- **F3** The stairwell pierces **both** the floor and ceiling slab.
- **F4** A landing where the run is long / turns; a handrail present.
- **F5** The destination story is reachable (registered as circulation).

## Grounding
- Riser/tread/width/headroom — REUSE Part 5 (IRC R311.7, cited + auditor-confirmed).
- **Medieval steeper stair geometry** — `to_ground` (flagged in Part 5).

## Open questions
- Straight vs newel/spiral vs winder by space/status; ladder-stair for a loft/croft.

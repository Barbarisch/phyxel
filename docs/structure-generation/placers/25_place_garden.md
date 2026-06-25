# 25 · place_garden

> Tier: Parcel. Part-1 status: **M** (distinct from world-gen flora). Schema: [`README.md`](README.md).

## Job
Plant the yard — kitchen/herb garden, beds, hedges, ornamental planting, fruit trees/shrubs — as **placed**
flora, distinct from terrain world-gen.

## Reads
- Parcel zones (#21 — kitchen garden in back, ornamental in front); brief (status, climate → species); the dwelling (kitchen near the kitchen garden); `biomes.json` for climate-appropriate species.

## Emits
- A **kitchen/herb garden** (rows/beds) near the kitchen; **ornamental** planting (status, front); **hedges** as internal divisions; **orchard/fruit** trees/shrubs — all placed, distinct from `WorldGenerator` flora.

## Algorithm
1. Kitchen/herb garden in the **back near the kitchen** (edged beds / rows).
2. Ornamental in the **front**, scaled to status (a croft has none; a manor has a parterre/knot).
3. Orchard/fruit by space; hedges as internal divisions.
4. Species **climate-appropriate** (biomes); season-aware (ties to S).

## Satisfies (checks)
P (garden/planting), L, B (climate-appropriate species), C (ornamental = status), M (lived-in).

## Engine capability needed
- Placed flora/beds (distinct from `WorldGenerator`) — ✅; bed edging — ⚠️.

## Failure modes
- An ornamental parterre at a peasant croft (C).
- Tropical plants in a cold climate (B).
- Using terrain world-gen flora instead of a designed garden.

## Function testers
- **F1** A kitchen/herb garden near the kitchen (back zone).
- **F2** Ornamental planting scaled to status.
- **F3** Species climate-appropriate (biomes).
- **F4** Beds edged; distinct from terrain flora.

## Grounding
- Species by climate — REUSE `biomes.json`; kitchen-garden-near-kitchen — logical/functional.
- Bed sizes — `to_ground`.

## Open questions
- Period planting palettes (which herbs/vegetables/flowers by era) — a flora-canon expansion.

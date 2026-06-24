# cellar / undercroft — Room Data Sheet

> Schema: [`README.md`](README.md). Used by: townhouse, manor, tavern, warehouse, bank (vault is a secure cellar variant).

## Function
Cool below-grade storage (drink, food, goods). A **vaulted undercroft** is the high-status / let-separately form.

## Required fixtures (→ #17)
- **Storage** (barrels / crates / bins).

## Typical fixtures (→ #16)
- Vaulting (undercroft), racks, a cold slab.

## Service
- **Cool** (below grade); **drainage** (damp); stair/trapdoor access.

## Adjacency
- **Below** the building (place_basement #35 / excavate_basement #34); reached by a stair (place_stairs #12) or an external bulkhead; a townhouse undercroft may have its own street access.

## Dimensions
- Headroom ≥ 2.032 m (storage) — Part 5 (cited); below grade; otherwise `to_ground`. (Vaulted undercroft = a stone barrel vault.)

## Function testers
- **F1** Storage present.
- **F2** Below grade, cool.
- **F3** Drainage (damp control).
- **F4** Reachable (stair/trapdoor/bulkhead).

## Grounding ledger
- Below-grade headroom (2.032 m) + damp + access — REUSE Part 5 (cited); medieval undercroft — Part 5 (vaulted, ashlar).

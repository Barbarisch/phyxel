# forge floor — Room Data Sheet

> Schema: [`README.md`](README.md). Used by: blacksmith, armourer, foundry. Reuses the `blacksmith` archetype sheet.

## Function
Work iron/steel around the **forge → anvil → quench** triangle. Fire-managed workshop.

## Required fixtures (→ #17)
- A **forge/hearth** + an **anvil**.

## Typical fixtures (→ #16)
- Bellows, a **quench trough**, a tool rack, a workbench, a fuel store.

## Service
- Forge **vented** (chimney #14); anvil **within a step** of the forge; quench **beside** the anvil; **clear of thatch** (fire-safe).

## Adjacency
- Forge on the **back/exterior wall** (venting); the work triangle compact; storefront/yard to the street.

## Dimensions
- Anvil/hearth working height **~0.80 m** (cited, `blacksmith`); floor ~3.7 m sq (modern ref, FLAG) / `to_ground`.

## Function testers
- **F1** Forge on a back/exterior wall, vented.
- **F2** Anvil within a step (~0.80 m work height).
- **F3** A quench source adjacent.
- **F4** **No combustible over/around the fire** (fire-safe envelope).

## Grounding ledger
- Anvil height + forge venting — REUSE `blacksmith` (Beautiful Iron, cited); floor size `to_ground`.

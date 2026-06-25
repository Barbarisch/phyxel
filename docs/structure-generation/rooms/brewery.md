# brewery (brewhouse) — Room Data Sheet

> Schema: [`README.md`](README.md). Used by: tavern, inn, monastery, manor, alehouse.

## Function
Brew ale/beer. Work/service tier.

## Required fixtures (→ #17)
- **Vats / coppers** (mash tun + boiler).

## Typical fixtures (→ #16)
- A hearth/fire (to heat the copper), casks, a water source, a cooling trough.

## Service
- **Water + heat**; cask/cooling storage; drainage.

## Adjacency
- Near a **water source**; fire-safe; near the cellar (cask storage).

## Dimensions
- `to_ground` (no clean medieval brewhouse standard); ceiling ≥ 2.134 m.

## Function testers
- **F1** Vats/coppers present.
- **F2** Water + a heat source.
- **F3** Cask storage / cooling.

## Grounding ledger
- Vats + water + heat — REUSE Part 3 brewery program; size `to_ground`.

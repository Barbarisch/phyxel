# mill stone-floor — Room Data Sheet

> Schema: [`README.md`](README.md). Used by: mill (water/wind). Reuses the `mill` archetype sheet.

## Function
Grind grain — the floor carrying the **millstones**, fed by the gear train below.

## Required fixtures (→ #17)
- **Millstones** (a fixed bed + a driven runner) + a **hopper**.

## Typical fixtures (→ #16)
- Meal bins (below the stones), a sack hoist (above), tools to dress the stones.

## Service
- Driven by the **gear train** (from the wheel/sails below); **grain in at the top, meal out below** (gravity).

## Adjacency
- **Above** the wheel/gear pit; **below** the sack hoist (the vertical machine); grain + meal stores adjacent.

## Dimensions
- Stones turn **~120 rpm** (cited, `mill`); floor size `to_ground`.

## Function testers
- **F1** Millstones (bed + driven runner) with a hopper feed.
- **F2** Meal bins below; a sack hoist above (gravity flow).
- **F3** Driven by the gear train from the power source.

## Grounding ledger
- Stone speed + gear train + grain→meal flow — REUSE `mill` (Watermill/Gristmill, cited); floor size `to_ground`.

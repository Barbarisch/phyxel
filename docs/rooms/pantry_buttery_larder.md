# pantry / buttery / larder — Room Data Sheet

> Schema: [`README.md`](README.md). Used by: hall_house, manor_hall, manor, tavern, inn (the service end).

## Function
Food + drink storage. Service tier. (Three related rooms: **buttery** = drink/casks; **pantry** = dry/bread; **larder** = cold/meat.)

## Required fixtures (→ #17)
- **Shelving / storage** (the function-definer).

## Typical fixtures (→ #16)
- Barrels/casks (buttery), bins/crocks (pantry), a cold slab (larder).

## Service
- **Cool + dark**; near the kitchen + hall.

## Adjacency
- The **service (low) end**, off the screens passage (the manor_hall's three service doors: buttery / pantry / kitchen passage); near the kitchen.

## Dimensions
- 1 bay / small — REUSE `room_program.json` / `manor_hall`; `to_ground` for the split.

## Function testers
- **F1** Shelving/storage present.
- **F2** Cool/dark; near the kitchen/hall.
- **F3** (buttery) cask storage; (larder) a cold store.

## Grounding ledger
- Buttery/pantry/larder split + screens-passage position — REUSE `manor_hall` (Manor house, cited); size `to_ground`.

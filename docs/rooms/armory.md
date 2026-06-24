# armory — Room Data Sheet

> Schema: [`README.md`](README.md). Used by: castle, keep, town_hall, guildhall. Reuses `castle`.

## Function
Store weapons + armour — secure + dry. Secure.

## Required fixtures (→ #17)
- **Weapon / armour racks**.

## Typical fixtures (→ #16)
- A workbench (repair), shelving.

## Service
- **Secure** (lockable); **dry** (rust); near the guardroom.

## Adjacency
- Near the **guardroom / gate**; secure; controlled access.

## Dimensions
- `to_ground`.

## Function testers
- **F1** Weapon/armour racks.
- **F2** Secure (lockable) + dry.
- **F3** Near the guardroom.

## Grounding ledger
- Secure + dry arms store — REUSE `castle` (cited); size `to_ground`.

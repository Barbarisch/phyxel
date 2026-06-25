# stable stalls — Room Data Sheet

> Schema: [`README.md`](README.md). Used by: stable, inn, manor. Reuses the `stable` archetype sheet.

## Function
House + tend horses — stalls + hay/feed/water + tack.

## Required fixtures (→ #17)
- **Stalls** sized for horses.

## Typical fixtures (→ #16)
- A hayrack, feed + water troughs, tack racks, a hayloft.

## Service
- Hay + feed + water; a **muck-out** to a midden; **fire separation** (hay away from lamps/forge); dry.

## Adjacency
- Stalls in rows off a passage; a **dry, secure tack room**; a hayloft above; the yard for handling.

## Dimensions
- Standing stall ~1.5–1.8 m wide (reason from horse size) — `to_ground`.

## Function testers
- **F1** Horse stalls + hay/feed/water.
- **F2** A dry, secure tack room.
- **F3** A muck-out; fire separation (hay away from flame).

## Grounding ledger
- Stalls + tack + muck-out — REUSE `stable` (Part 3 program); stall width `to_ground`.

# guardroom — Room Data Sheet

> Schema: [`README.md`](README.md). Used by: gaol, castle, gatehouse, town_hall. Reuses `gaol`/`castle`.

## Function
Control the entrance — the watch posts here. Security (T0).

## Required fixtures (→ #17)
- **Weapon storage** (a rack) + **seating** (the guard's post).

## Typical fixtures (→ #16)
- A brazier, a table, a key-board (gaol).

## Service
- **Sightline on the entrance** (and the cells/vault approach where relevant); near the gate.

## Adjacency
- **At the gate / the single controlled entrance**; covers it; (gaol) controls the route to the cells.

## Dimensions
- `to_ground`.

## Function testers
- **F1** A weapon rack + a guard's seat.
- **F2** Sightline on the entrance (and the secure approach).
- **F3** At the controlled entrance.

## Grounding ledger
- By-the-gate + sightline — REUSE `gaol`/`castle` (cited); size `to_ground`.

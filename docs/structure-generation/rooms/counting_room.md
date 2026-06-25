# counting room — Room Data Sheet

> Schema: [`README.md`](README.md). Used by: bank / counting_house, civic_palace (treasury office). Reuses `bank`.

## Function
Reckon + record money. Staff (T1).

## Required fixtures (→ #17)
- A **counting board / table** + **ledger desks** + **coin scales/balance**.

## Typical fixtures (→ #16)
- Coin coffers, a strongbox, clerks' stools.

## Service
- Behind the **grille** (staff-only); the route to the vault runs **through here**, not the public hall.

## Adjacency
- **Behind the banking hall** (staff side); between the public hall and the vault; the **only** path to the vault.

## Dimensions
- `to_ground`.

## Function testers
- **F1** A counting board + ledger desks + coin scales.
- **F2** Staff-only (behind the grille).
- **F3** The vault is reachable **only** through here (no public→vault path).

## Grounding ledger
- Counting board (used openly) + position — REUSE `bank` (Counting house, cited); size `to_ground`.

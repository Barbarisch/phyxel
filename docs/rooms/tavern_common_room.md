# tavern common room — Room Data Sheet

> Schema: [`README.md`](README.md). Used by: tavern, inn, alehouse. Reuses the `tavern` archetype sheet.

## Function
Drink, eat, gather — the public heart, around the hearth. Public tier; the largest room.

## Required fixtures (→ #17)
- A **serving bar/counter** + a **hearth**.

## Typical fixtures (→ #16)
- Boards (tables) + forms (benches) + stools, a high-backed **settle** by the hearth, casks.

## Service
- Hearth **vented**; the **bar = the public/service boundary** (controls the drink); a route to the cellar/kitchen.

## Adjacency
- **Fronts the entrance/yard**; the largest room; the **bar** between it and the service (kitchen/cellar); guest stairs off it (inn).

## Dimensions
- Reuse the hall size (`room_program.json`); `to_ground` for the specific common-room dim.

## Function testers
- **F1** A serving bar + a hearth + seating.
- **F2** Drink storage (cellar/casks) reachable by staff, not the public.
- **F3** The bar forms the public/service boundary.

## Grounding ledger
- Room program (bar/hearth/boards/forms/settle) — REUSE `tavern` (Medievalists, cited); size REUSE hall / `to_ground`.

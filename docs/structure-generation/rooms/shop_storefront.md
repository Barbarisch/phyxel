# shop / storefront — Room Data Sheet

> Schema: [`README.md`](README.md). Used by: every shop archetype (general_store, apothecary, butcher, etc.). Reuses `general_store`.

## Function
Retail to the street — the customer-facing floor. Public (T0) tier.

## Required fixtures (→ #17)
- A **counter** (the public ↔ stock boundary).

## Typical fixtures (→ #16)
- Display shelving, **shutters** (shutter-down to the street), scales/measures.

## Service
- A **shutter-down opening** to the street; **lockable** at night; a back store/cellar behind.

## Adjacency
- **Fronts the street**; the **counter separates** customers from the stock; a back store/cellar behind; the dwelling above (mixed-use).

## Dimensions
- Shop-unit frontage **~2–2.5 m** (6–8 ft); a small lock-up **~2.3 × 3.4 m**; deeper shops run back the burgage plot — cited (`general_store`, Chester selds).

## Function testers
- **F1** A counter separating the public floor from the stock.
- **F2** A street-facing display / shuttered opening.
- **F3** Lockable at night.
- **F4** Back/cellar storage reachable by staff, not the public.

## Grounding ledger
- Shop-unit frontage + lock-up dims — REUSE `general_store` (Chester selds, British History Online, cited); counter dim `to_ground`.

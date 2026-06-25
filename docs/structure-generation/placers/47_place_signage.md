# 47 · place_signage

> Tier: Settlement. Part-1 status: **M**. Schema: [`README.md`](README.md).

## Job
Hang **pictorial trade/inn signs** at shopfronts + wayfinding — the city's legibility, for an illiterate
clientele.

## Reads
- The shops/taverns (#45) + their trades; a **sign vocabulary** (backlog); the decal system.

## Emits
- A hanging **pictorial sign** per shop/tavern (a horseshoe = smith, a bush = tavern, a mortar = apothecary…); wayfinding markers at junctions.

## Algorithm
1. For each commercial frontage, hang the trade's pictorial sign (from the sign vocabulary).
2. Inn signs; wayfinding at junctions.

## Satisfies (checks)
W2 (signature legibility — the shop reads as its trade), the "pictorial signs (illiterate)" rule, M.

## Engine capability needed
- Sign board + bracket — ✅; **the pictorial image = the DECAL system** — ❌ MISSING (backlog §2) — without it the sign can't show its picture.

## Failure modes
- No signs (illegible city); **text** signs for an illiterate populace (A); a **faked** picture (no decal system).

## Function testers
- **F1** A pictorial sign per shop/tavern matching its trade.
- **F2** Legible without text.
- **F3** Wayfinding at junctions.
- **F4** The sign image flagged (needs the decal system), **not faked**.

## Grounding
- The **sign vocabulary** (a horseshoe = smith, a bush = tavern…) — a backlog item; qualitative.

## Open questions
- Heraldry/guild arms as signage (ties to the banner decal, backlog §2).

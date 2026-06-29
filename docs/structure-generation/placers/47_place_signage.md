# 47 · place_signage

> Tier: Settlement. Part-1 status: **P** (board+bracket shipped, GATED + runtime-verified; pictorial
> DECAL still backlog). Schema: [`README.md`](README.md).

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
- Sign board + bracket — ✅ **DONE**: `hanging_sign` asset (Wood/Log board + Metal wrought-iron bracket,
  `tools/regen_furniture.py`) hung over a business typology's entry door by the v2 `build_structure`
  handler (`place_signage (#47)` block), GATED by V6 `RealizedStructureValidator::checkSignClearance`
  (clearance ≥ 8 ft, projection ≤ 48 in, board above the lintel) — skips + reports if there's no room.
  Runtime-verified on a tavern (`hanging_sign_2`, clearance 22 / above lintel 4 micro, rot 90).
- **The pictorial image = the DECAL system** — ❌ MISSING (backlog §2) — without it the sign shows a
  blank framed board (F4: flagged, not faked).

## Failure modes
- No signs (illegible city); **text** signs for an illiterate populace (A); a **faked** picture (no decal system).

## Function testers
- **F1** A pictorial sign per shop/tavern matching its trade.
- **F2** Legible without text.
- **F3** Wayfinding at junctions.
- **F4** The sign image flagged (needs the decal system), **not faked**.

## Grounding
- **Board + bracket geometry** — `hanging_sign` archetype in `resources/object_dimensions.json`:
  board 0.8 × 0.6 m (area 0.48 m² < the 12 sq ft / 1.11 m² historic cap); projection ≤ 0.8 m
  (< the 48 in / 1.22 m limit); the board's bottom hangs ≥ 2.44 m (8 ft) above grade.
  - Sources: clearance/area/projection are **modern historic-district / projecting-sign code**
    (Santa Clara UT 17.45.120; SLC historic sign guidelines) — disclosed as post-medieval *numbers*,
    but they are safety/anthropometric (clear the head/horse) and **period-invariant**. The medieval
    *form* — a pictorial board on a **wrought-iron bracket** over the entrance, clearing head height —
    is well attested (illiterate clientele; size/projection regulation began 16th–18th c.: 1669 French
    order, 1762–73 London). Materials: **Wood** board + **Metal/Log** bracket (existing materials only).
- The **sign vocabulary** (a horseshoe = smith, a bush = tavern…) — a backlog item; qualitative.
  The **pictorial image = the DECAL system** (backlog) — this archetype is BOARD + BRACKET only, never
  a faked picture (F4).

## Open questions
- Heraldry/guild arms as signage (ties to the banner decal, backlog §2).

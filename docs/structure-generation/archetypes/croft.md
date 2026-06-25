# Croft (Peasant Cottage) — Archetype Data Sheet

> Status: **DRAFT**. Schema: [`README.md`](README.md). Room program: `room_program.json` → `croft`.

## 1. Identity
- **id:** `croft`
- **function:** a peasant family's single-room dwelling
- **aka:** cottage, cot, one-bay/two-bay cottage
- **group:** Dwelling (Part 6)
- **extends:** `room_program.json` → `croft` (cited there)
- **genre/period:** real (vernacular medieval).

## 2. Essence
**One heated room** ("the house") in which a peasant family cooks, eats, sleeps, and works around a central
hearth. Defining quality: minimal single-cell shelter — no zoning, no privacy.

## 3. Threat model / failure modes
- **Weather** — keep rain/cold out (thatch + daub).
- **Fire** — an open central hearth under a thatch roof (the constant rural hazard).
- **Cold** — one hearth heats everything.

## 4. Access tiers / zoning
- One room (T0). No tiers — work/sleep/cook share the space. A partition may screen an "inner room" (sleep/dairy).

## 5. Required spaces (program)
| Space | Purpose | Required fixtures | Size |
|---|---|---|---|
| house (living) | everything | **central hearth**, board + bench, pallet/bed, chest, cooking pot | REUSE `croft`: width ≤ 6 m, ~2 bays, bay ~4 m |

## 6. Adjacency & circulation rules
1. One room; the **hearth is central** (a smoke louvre/hole — no chimney in the earliest form).
2. A single door opens to the **croft** (the garden/work plot around the house).
3. A partition (if any) screens a sleeping/dairy "inner room" at one end.

## 7. Construction & materials
- **Cruck** timber frame; **wattle-&-daub** or **cob** walls; **thatch** roof; beaten-earth floor.
- Span **≤ 6 m** (the cruck limit — cited via `room_program`).
- Central hearth → smoke escapes by a louvre/hole (no chimney early). WANTED: wattle-&-daub material.

## 8. Signature / legibility
Small, low, thatched; one door; smoke from a roof louvre/hole; a garden croft around it.

## 9. Status / period / setting scaling
- **Lowest:** a one-room cot.
- **Up:** a croft with a partitioned inner room → a `longhouse` (+ byre).

## 10. Function testers
- **F1** One heated living room with a hearth.
- **F2** Wall span ≤ 6 m (cruck).
- **F3** A smoke escape (louvre/hole) where the hearth is central + open.
- **F4** A door to the croft/garden.
- **F5** Thatch pitch ≥ 45–50° (existing roof canon).

## 11. Fixtures & assets needed (→ backlog)
Central hearth, board + bench, pallet bed, chest, cooking pot, wattle-&-daub material. →
[`WantedAssetsBacklog.md`](../../WantedAssetsBacklog.md).

## 12. Grounding ledger
| Claim | Provenance |
|---|---|
| single "house" room, ≤ 6 m, ~2 bays, ~4 m bay | REUSE-CANON — `room_program.json` `croft` (Wharram Percy / cruck ≤ 6 m, cited there) |
| central hearth + smoke louvre, cruck, wattle-&-daub/cob, thatch | reuse `structure_styles.json` + vernacular norm |
| thatch pitch ≥ 45–50° | existing roof canon (Part 5 / styles) |

## 13. Open questions / unknowns
- Beaten-earth vs flagged floor by region — to confirm.
- Smoke louvre vs a simple roof hole — minor.

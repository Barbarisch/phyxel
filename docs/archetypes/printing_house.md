# Printing House — Archetype Data Sheet

> Status: **DRAFT**. Schema: [`README.md`](README.md). **GENRE-FLAG: post-1450 (early-modern).** *(BG3: Baldur's Mouth.)*

## 1. Identity
- **id:** `printing_house`
- **function:** set type, print, and sell printed sheets/books/news
- **aka:** print shop, news office, stationer
- **group:** Civic & institutions (Part 6)
- **extends:** a workshop + a public counter (a `townhouse` shell)
- **genre/period:** **post-1450 (Gutenberg / early-modern)** — a strict-medieval brief uses a **scriptorium** instead (Part 3). Strong genre flag (F0).

## 2. Essence
A workshop around the **printing press(es)** + **type-setting** + paper/ink + drying, with a public counter.
Defining quality: the press + the composing room + dry paper handling.

## 3. Threat model / failure modes
- **Fire** — paper + oil-based ink + lamps.
- **Damp** — paper must stay dry (and printed sheets must dry).
- **Theft / order** — type is valuable and must be cased in order.

## 4. Access tiers / zoning
- **T0** shop counter — sell sheets/books/news.
- **T1** press room (the presses) + composing room (type cases).
- Paper/ink store (dry) + a drying area; dwelling above.

## 5. Required spaces (program)
| Space | Tier | Purpose | Required fixtures | Size |
|---|---|---|---|---|
| press_room | T1 | print | the **press(es)**, an imposing stone | `to_ground` |
| composing_room | T1 | set type | **type cases**, setting frames, good light | `to_ground` |
| paper + ink store | — | supplies | dry bins, ink | dry |
| drying area | — | dry printed sheets | drying lines/racks | airy |
| shop counter | T0 | sell | counter, display | `to_ground` |

## 6. Adjacency & circulation rules
1. The **composing room is well-lit** (setting type by hand).
2. The **press room** is adjacent; paper store dry; a **drying area** for printed sheets.
3. The shop counter fronts the street.

## 7. Construction & materials
- A `townhouse`/workshop shell; **dry** storage; fire care (ink/paper/lamps).
- WANTED: printing press, type cases, drying racks, trade sign.

## 8. Signature / legibility
The thump of the press; printed sheets drying on lines; a book/press sign; a counter of pamphlets/news.

## 9. Status / period / setting scaling
- **Medieval alternative:** a **scriptorium** (hand-copying — Part 3 / monastery) — use for a medieval brief.
- **Up:** a stationer + press (books) → a news office (BG3 Baldur's Mouth).

## 10. Function testers
- **F0 (genre):** post-1450 — for a medieval brief substitute a scriptorium.
- **F1** A press room with the press(es).
- **F2** A composing room with type cases (well-lit).
- **F3** Dry paper + ink storage.
- **F4** A drying area for printed sheets.
- **F5** A public counter / sales.

## 11. Fixtures & assets needed (→ backlog)
Printing press, imposing stone, type cases, drying racks, paper/ink bins, counter, sign. →
[`WantedAssetsBacklog.md`](../WantedAssetsBacklog.md).

## 12. Grounding ledger
| Claim | Provenance |
|---|---|
| the printing press | GENRE-FLAG — post-1450 (Gutenberg); early-modern |
| press → compose → dry → sell workflow | reasoned from the early-modern print shop |
| medieval alternative = scriptorium | Part 3 / `monastery` |
| press-room **footprint** | NEEDS-RESEARCH — searched, no surveyed early print-shop footprint found; it's a workshop in a `townhouse` shell (a single press output ~3,600 pages/day) |

## 13. Open questions / unknowns
- A surveyed early print-shop **footprint** — NEEDS-RESEARCH (searched this session; layout/workflow documented, dimensions not).
- Whether to model the scriptorium as its own period-correct sheet.

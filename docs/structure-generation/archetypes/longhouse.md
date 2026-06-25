# Longhouse — Archetype Data Sheet

> Status: **DRAFT**. Schema: [`README.md`](README.md). Room program: `room_program.json` → `longhouse`.

## 1. Identity
- **id:** `longhouse`
- **function:** a peasant dwelling **and byre under one roof** — people at one end, cattle at the other
- **aka:** Dartmoor longhouse, byre-dwelling
- **group:** Dwelling (Part 6)
- **extends:** `room_program.json` → `longhouse` (cited)
- **genre/period:** real (Dartmoor / Wharram Percy).

## 2. Essence
A single long range housing **family + cattle**, divided by a **cross-passage**: dwelling end (inner room +
hall) and byre end. Defining quality: humans and beasts share one roof, the byre's warmth a benefit, its muck
managed by drainage.

## 3. Threat model / failure modes
- **Weather + fire** (as `croft`).
- **Cattle** — warmth + smell + muck; the byre drains **downhill**, away from the dwelling.

## 4. Access tiers / zoning
- **Dwelling end** — inner room (sleep/dairy) + hall (living, central hearth).
- **Cross-passage** — the divide + the entrance(s).
- **Byre end** — cattle stalls.

## 5. Required spaces (program)
| Space | Purpose | Required fixtures | Size |
|---|---|---|---|
| inner | sleep/dairy | bed, chest, dairy gear | REUSE `longhouse` (1 bay) |
| hall | living | **central hearth**, board + bench | 1 bay |
| byre | cattle | stalls, drain channel | 2 bays |

*(Reuse `longhouse`: width ≤ 6 m, 4 bays, bay ~4 m.)*

## 6. Adjacency & circulation rules
1. **Dwelling end and byre end are separated by the cross-passage** (opposed doors).
2. The **byre is downhill** of the dwelling — muck drains away.
3. The hearth sits in the hall.

## 7. Construction & materials
- Cruck frame, span ≤ 6 m; thatch; beaten-earth floors; the byre floor drains out through the down-slope wall.

## 8. Signature / legibility
A **long low thatched range** with a cross-passage door and a byre end; smoke from the dwelling end.

## 9. Status / period / setting scaling
- **Down:** a `croft` (no byre).
- **Up:** a `hall_house` (the byre end becomes a service/solar wing).

## 10. Function testers
- **F1** A dwelling end (inner + hall) AND a byre end.
- **F2** A cross-passage separating them, with opposed doors.
- **F3** The byre is downhill of the dwelling (drainage).
- **F4** A hearth in the hall.
- **F5** Span ≤ 6 m (cruck).

## 11. Fixtures & assets needed (→ backlog)
Central hearth, board + bench, bed, chest, cattle stalls, drain channel. →
[`WantedAssetsBacklog.md`](../../WantedAssetsBacklog.md).

## 12. Grounding ledger
| Claim | Provenance |
|---|---|
| inner + hall + cross-passage + byre, 4 bays, ≤ 6 m | REUSE-CANON — `room_program.json` `longhouse` (Dartmoor / Wharram Percy, cited) |
| byre downhill for drainage; shared roof | Dartmoor longhouse norm (cited in the room program) |

## 13. Open questions / unknowns
- Whether the dwelling/byre share an internal door or only the cross-passage — varies.

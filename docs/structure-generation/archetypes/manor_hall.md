# Manor Hall (Stone Great Hall) — Archetype Data Sheet

> Status: **DRAFT**. Schema: [`README.md`](README.md). Room program: `room_program.json` → `manor_hall`.

## 1. Identity
- **id:** `manor_hall`
- **function:** the lord's great hall — the architectural seat of lordship
- **aka:** great hall, stone hall
- **group:** Dwelling (Part 6); the core of the `manor` complex
- **extends:** `room_program.json` → `manor_hall` (cited)
- **genre/period:** real (medieval great hall).

## 2. Essence
A grand, tall **great hall** — *1.5–3× as long as wide and higher than wide* — at the centre of lordship, with a
**screens passage** to service at the low end and a **dais** for the lord's high table at the high end. Defining
quality: monumental public hall + a clear low(service)→high(lord) axis.

## 3. Threat model / failure modes
- **Status display** (the primary driver — the hall *is* the statement).
- **Fire** — open hearth / large kitchen (kitchen often detached).
- **Privacy** — the family withdraws beyond the dais (solar/great chamber).

## 4. Access tiers / zoning
- **Service (low) end** — screens passage with three doors (buttery, pantry, kitchen passage).
- **Great hall** — public lordship: the dais, the high table, an oriel window.
- **Private (high) end** — solar / great chamber, chapel, lord's apartments (beyond the dais).

## 5. Required spaces (program)
| Space | Tier | Purpose | Required fixtures | Size |
|---|---|---|---|---|
| screens / service | low | serve the hall | buttery + pantry, 3 service doors | REUSE `manor_hall`: 1 bay |
| great_hall | mid | lordship, dine | dais, **high table**, benches, hearth, oriel | 3 bays; **1.5–3× long:wide, higher than wide** |
| (private end) | high | family withdraws | solar/great chamber (see `manor`) | beyond the dais |

*(Reuse `manor_hall`: width 8–17 m, bay ~5 m, 4 bays.)*

## 6. Adjacency & circulation rules
1. **Screens passage at the low end** with **three service doors** (buttery / pantry / kitchen passage).
2. The **dais + high table** are at the **high end**, opposite the screens (often with an oriel window).
3. The **family's private rooms** (solar/great chamber) lie **beyond the dais**.
4. The **kitchen is fire-separated** — frequently a **detached** building reached via the screens passage.
5. The hall's proportions hold: **1.5–3× long:wide and higher than wide**.

## 7. Construction & materials
- **Stone** (exterior wall ~0.667 m — existing canon); a tall open timber roof; large traceried + oriel windows; a porch.
- WANTED: dressed/ornamental stone, glazing (later stained/heraldic).

## 8. Signature / legibility
A tall stone hall with **big traceried/oriel windows**, a **porch**, and cross-wings at each end; reads as
lordship from its scale and fenestration.

## 9. Status / period / setting scaling
- **Down:** a timber `hall_house`.
- **Up:** the full `manor` complex (gatehouse, courtyard, chapel, gardens); → a castle `keep`'s great hall.

## 10. Function testers
- **F1** Great hall proportioned **1.5–3× long:wide and higher than wide**.
- **F2** A screens passage + **three service doors** at the low end.
- **F3** A **dais + high table** at the high end, opposite the screens.
- **F4** A solar/great chamber (private) **beyond the dais**.
- **F5** The kitchen is **fire-separated** (often detached).
- **F6** A chapel present (status).

## 11. Fixtures & assets needed (→ backlog)
Dais + high table, benches, hall hearth, oriel/traceried windows, buttery/pantry fittings, screens. →
[`WantedAssetsBacklog.md`](../../WantedAssetsBacklog.md).

## 12. Grounding ledger
| Claim | Provenance |
|---|---|
| great hall 1.5–3× long:wide, higher than wide; width 8–17 m; bay ~5 m | REUSE-CANON — `room_program.json` `manor_hall` (Stokesay, Winchester, *Great hall* Wikipedia — cited) |
| screens passage + 3 service doors; dais + high table; kitchen detached | CITED — [Manor house (Wikipedia)](https://en.wikipedia.org/wiki/Manor_house); great-hall norm |
| stone wall ~0.667 m | existing project canon |

## 13. Open questions / unknowns
- Aisled vs single-span hall by width (the canon caps cruck ~10 m; wider halls used aisles) — to encode.

# Hall House — Archetype Data Sheet

> Status: **DRAFT**. Schema: [`README.md`](README.md). Room program: `room_program.json` → `hall_house`.

## 1. Identity
- **id:** `hall_house`
- **function:** a yeoman/gentry dwelling centred on an open hall
- **aka:** open-hall house, Wealden house
- **group:** Dwelling (Part 6)
- **extends:** `room_program.json` → `hall_house` (cited)
- **genre/period:** real (medieval open-hall house).

## 2. Essence
A house built around a **full-height open hall** (central hearth) flanked by a **lower service end** and an
**upper solar end** — the classic tripartite medieval plan with a cross-passage. Defining quality: the open hall
as the household's heart, with a clear low↔high social gradient across it.

## 3. Threat model / failure modes
- **Fire** — the open central hearth (a smoke louvre, later a chimney).
- **Privacy gradient** — public hall ↔ private solar.

## 4. Access tiers / zoning
- **Service (low) end** — buttery, pantry, the screens passage; the entrance.
- **Open hall** — public, the household's main room.
- **Solar (high) end** — private chamber, often over a parlour.

## 5. Required spaces (program)
| Space | Tier | Purpose | Required fixtures | Size |
|---|---|---|---|---|
| service | low | drink/food store | buttery + pantry, screens | REUSE `hall_house`: 1 bay |
| open hall | mid | living, dining | **central hearth**, dais, high table, benches | 2 bays |
| solar | high | private chamber | bed, chest, fireplace | 1 bay (often over a parlour) |

*(Reuse `hall_house`: width 6–8 m, 4 bays.)*

## 6. Adjacency & circulation rules
1. The **open hall occupies the middle** (full height).
2. **Service (low) end and solar (high) end flank** it.
3. A **cross-passage / screens** at the low end has **opposed doors** (the through-draught entry).
4. The **dais / high end** is opposite the screens.
5. The hearth is in the hall (central, open → smoke louvre; later a side chimney).

## 7. Construction & materials
- Cruck or box frame, span 6–8 m (aisled if wider); thatch or tile; a jettied solar end is common (the Wealden form).

## 8. Signature / legibility
A tall open-hall block with a big roof, flanked by a lower service bay and an upper (often jettied) solar bay; a
cross-passage door; smoke louvre or a side chimney.

## 9. Status / period / setting scaling
- **Down:** a `longhouse` / `croft`.
- **Up:** a stone `manor_hall`.

## 10. Function testers
- **F1** A full-height open hall in the middle.
- **F2** A service (low) end **and** a solar (high) end flanking it.
- **F3** A cross-passage / screens at the low end with opposed doors.
- **F4** A dais / high end opposite the screens.
- **F5** A hearth in the hall.

## 11. Fixtures & assets needed (→ backlog)
Central hearth, dais + high table, benches, bed + chest (solar), buttery/pantry fittings. →
[`WantedAssetsBacklog.md`](../../WantedAssetsBacklog.md).

## 12. Grounding ledger
| Claim | Provenance |
|---|---|
| service + open-hall (2 bays) + solar, 4 bays, 6–8 m | REUSE-CANON — `room_program.json` `hall_house` (Wikipedia *Hall house*, cited) |
| screens passage + opposed doors + dais | medieval open-hall norm (cited in the room program / Great hall) |

## 13. Open questions / unknowns
- Open hearth vs an inserted chimney by date — affects the smoke tester.

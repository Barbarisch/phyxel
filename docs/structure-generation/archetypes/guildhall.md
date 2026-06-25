# Guildhall — Archetype Data Sheet

> Status: **DRAFT**. Schema: [`README.md`](README.md).

## 1. Identity
- **id:** `guildhall`
- **function:** a guild's meeting / business / feasting hall (often with a market below)
- **aka:** company hall, market house
- **group:** Civic & institutions (Part 6)
- **extends:** a great hall raised over an arcaded ground floor
- **genre/period:** real (15th-c. English examples; the hall-over-arcade plan traces to Como, 1215).

## 2. Essence
A **great hall** (guild business, feasts, court) raised over an **open arcaded ground floor** (a covered market).
Defining quality: the upper hall + the open market arcade below.

## 3. Threat model / failure modes
- **Civic display + assembly** (the hall hosts crowds, feasts, courts).
- **Records** — a guild's muniments need a secure, fire-safe room.
- **Fire.**

## 4. Access tiers / zoning
- **T0** open arcaded ground — a covered market for traders.
- **T1** great hall above — meetings, feasts, court.
- Muniment/records room (secure); kitchen/buttery (feasts); council chamber.

## 5. Required spaces (program)
| Space | Tier | Purpose | Required fixtures | Size |
|---|---|---|---|---|
| market_arcade | T0 | covered market | open **columned arcade** | `to_ground` |
| great_hall | T1 | assembly/feast | dais, tables, hearth, big windows | `to_ground` (reuse hall proportions) |
| muniment/records | — | keep records | secure, **fire-safe** chests | `to_ground` |
| kitchen/buttery | — | feasts | cook-hearth, store | `to_ground` |

## 6. Adjacency & circulation rules
1. The **open arcade is at ground level** (street market).
2. The **hall above** is reached by a stair.
3. The **muniment room is secure + fire-safe**.
4. The kitchen serves the hall.

## 7. Construction & materials
- Timber or stone; an **arcaded (columned) ground floor open to the street**; a fine hall above with big windows.
- WANTED: arcade columns, hall fittings, glazing.

## 8. Signature / legibility
A **fine first-floor hall over an open columned arcade**; civic grandeur; sometimes a clock/bell.

## 9. Status / period / setting scaling
- **Down:** a small market house.
- **Up:** a grand company hall (London livery halls).

## 10. Function testers
- **F1** A great hall (assembly/feast) raised over an **open arcaded ground floor**.
- **F2** The ground arcade is open/columned (a covered market).
- **F3** A secure, fire-safe muniment/records room.
- **F4** A kitchen for feasts.
- **F5** A stair linking the market to the hall.

## 11. Fixtures & assets needed (→ backlog)
Arcade columns, hall dais + tables, records chests, kitchen fittings. →
[`WantedAssetsBacklog.md`](../../WantedAssetsBacklog.md).

## 12. Grounding ledger
| Claim | Provenance |
|---|---|
| hall-over-open-arcade form; arcaded ground market (Faversham); 15th-c. English / Como 1215 origin | CITED — [Guildhall (Wikipedia)](https://en.wikipedia.org/wiki/Guildhall); [English market towns & halls (Heritage Calling)](https://heritagecalling.com/2024/03/14/the-timeless-charm-of-english-market-towns-and-halls/) |
| hall proportions | REUSE-CANON — `manor_hall` / great hall |
| **sizes** | `to_ground` |

## 13. Open questions / unknowns
- Guildhall vs `town_hall` overlap (both use the market-house form) — distinguished by function, not plan.

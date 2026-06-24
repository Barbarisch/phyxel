# Town Hall / Moot Hall — Archetype Data Sheet

> Status: **DRAFT**. Schema: [`README.md`](README.md). Shares the market-house form with [`guildhall`](guildhall.md).

## 1. Identity
- **id:** `town_hall` / `moot_hall`
- **function:** the seat of **town government** — council, court, public meetings (+ a market below)
- **aka:** moot hall, market house, town house
- **group:** Civic & institutions (Part 6)
- **extends:** the market-house form (hall over an arcaded ground floor)
- **genre/period:** real.

## 2. Essence
The **civic-government hall over a market arcade** — often the only public building a small town had, so
**multi-purpose**: council, court, public meeting, and frequently a lock-up + an armoury too.

## 3. Threat model / failure modes
- **Assembly** (the town gathers — a bell summons them).
- **Order / justice** — a court + a lock-up.
- **Records / civic property** — a secure muniment + armoury.

## 4. Access tiers / zoning
- **T0** open market arcade (ground).
- **T1** civic hall above — council, court, meetings.
- Lock-up / gaol cell + armoury + muniment (secure); a bell turret.

## 5. Required spaces (program)
| Space | Tier | Purpose | Required fixtures | Size |
|---|---|---|---|---|
| market_arcade | T0 | covered market | open columned arcade | `to_ground` |
| civic_hall | T1 | council/court/meeting | benches, a court/council seat, hearth | `to_ground` |
| lock-up / cell | — | hold offenders | a barred cell *(see `gaol`)* | small |
| armoury / muniment | — | town arms + records | racks, secure chests | secure |
| bell | — | summon the town | a bell turret | — |

## 6. Adjacency & circulation rules
1. The **open market arcade** at ground; the **civic hall above** by a stair.
2. A **lock-up/cell + armoury/muniment** are secure.
3. A **bell** to summon the town.

## 7. Construction & materials
- Timber or stone; arcaded ground floor; a civic hall above; a bell turret.
- WANTED: arcade columns, council/court seating, bell.

## 8. Signature / legibility
A civic hall over an **open market arcade**, topped by a **bell/clock turret**; the town's chief public building.

## 9. Status / period / setting scaling
- **Down:** a simple market house doubling as everything.
- **Up:** a grand town hall (council chamber + court + civic offices) → a `civic_palace` (a ruler's seat).

## 10. Function testers
- **F1** A civic hall (council/court/meetings) over an **open market arcade**.
- **F2** Court/council seating in the hall.
- **F3** Often a **lock-up/cell** + an **armoury/muniment** (secure).
- **F4** A **bell** to summon the town.
- **F5** The market arcade open below.

## 11. Fixtures & assets needed (→ backlog)
Arcade columns, council/court seating, secure chests, weapon rack, bell. →
[`WantedAssetsBacklog.md`](../WantedAssetsBacklog.md).

## 12. Grounding ledger
| Claim | Provenance |
|---|---|
| hall over open market arcade; multi-purpose (court, armoury, jail, school) | CITED — [Guildhall (Wikipedia)](https://en.wikipedia.org/wiki/Guildhall); [Heritage Calling](https://heritagecalling.com/2024/03/14/the-timeless-charm-of-english-market-towns-and-halls/) |
| cell/armoury | REUSE-CANON — `gaol` |
| **sizes** | `to_ground` |

## 13. Open questions / unknowns
- When a town merits a `town_hall` vs sharing the `guildhall` — a settlement-tier call.

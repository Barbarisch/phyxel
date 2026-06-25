# Bank / Counting House — Archetype Data Sheet

> Status: **DRAFT** (the worked exemplar). Schema: [`README.md`](README.md).

## 1. Identity
- **id:** `counting_house` / `bank`
- **function:** safekeeping, lending, and exchange of money/valuables; trustworthy record-keeping
- **aka:** counting house, *banco*, money-lender's house, treasury
- **group:** Finance & institutions (Part 6)
- **extends:** `townhouse` for the urban shell; the vault reuses the keep / retaining-wall canon
- **genre/period:** the **counting house is real** (originated in Italy in the high Middle Ages); the *bank-as-public-institution* with a built masonry **vault** is late-medieval → early-modern. The poorer/earlier form is a **strongbox/coffer in a locked room**, not a built vault. GENRE-FLAG for a strict-early-medieval brief (CC7).

## 2. Essence
Not "a building with a vault" — a **security gradient with access control, plus trustworthy record-keeping**.
Two things must never fail: the **money** (theft) and the **ledgers** (fire/forgery). Every feature derives from
protecting one of those.

## 3. Threat model / failure modes
- **Theft** (primary) — burglary, armed robbery, the inside job, **tunnelling from below** (ties to Part 8).
- **Fire destroying records** — the ledgers *are* the bank; losing them is ruin.
- **Fraud / forgery** — controlled ledger access + witnessed counting.
- **Run / riot** — a crowd demanding deposits; the public hall must be closable/defensible.

## 4. Access tiers / zoning (the core)
- **T0 Public** — *banking hall*: customers reach only here; a **counter + grille** is the public/staff boundary; **one** controlled street entrance; barrable against a run.
- **T1 Staff** — *counting room* (the counting board, historically used **openly**), clerks' desks, manager's closet.
- **T2 Secure** — *strongroom/vault*: reached **only** through T1; thick masonry; **one** iron-bound door; **no exterior window**; core-of-plan or below grade. The tunnelling threat makes the **floor and ceiling** part of the secure envelope, not just the walls.
- **T3 Inner** *(wealth-scaled)* — bullion / deposit-box room and/or the **fireproof ledger archive**.
- A **guard post** sits at the T0/T1 boundary with sightline on both the entrance and the vault approach.

## 5. Required spaces (program)
| Space | Tier | Purpose | Required fixtures | Size |
|---|---|---|---|---|
| banking_hall | T0 | receive + transact | counter + **grille**, bench, counter scale, barrable shutters | `to_ground` |
| counting_room | T1 | reckon + record | counting board, ledger desks, **coin scales/balance**, coin coffers, a strongbox | `to_ground` |
| vault / strongroom | T2 | secure storage | **iron vault door**, deposit boxes/strongboxes, shelving, *(no window)* | walls: REUSE keep/retaining canon (2–4 m fortified; ≥0.667 m stone floor); room size `to_ground` |
| ledger_archive | T2/T3 | protect records | ledger shelving, stone enclosure / fire door | `to_ground`; **fire-separated** |
| manager_office | T1 | negotiate, hold keys | desk, private strongbox, seating | `to_ground` |
| guard_post | T0/T1 | control access | weapon rack, seat, sightlines | `to_ground` |
| bullion_room | T3 | highest value *(scaling)* | reinforced boxes | `to_ground` |

## 6. Adjacency & circulation rules (→ validator checks)
1. **Exactly one** public entrance to the banking hall.
2. **No public→vault path** — the vault is reachable *only* through staff (T1) space.
3. The counter+grille fully separates T0 from T1 — **no human-passable gap**.
4. The vault has **one** door, **no** exterior window; walls **and floor and ceiling** all meet the vault spec (tunnelling).
5. A guard post has line-of-sight to **both** the street entrance and the vault door.
6. The ledger archive is **fire-separated** (stone / fire door) from all timber + hearth areas.
7. The banking hall is **closable/barrable** (run/riot).

## 7. Construction & materials
- **Shell: masonry** (stone/brick), *not* timber — solidity is the signature *and* the theft answer.
- **Vault:** thick masonry per the keep/retaining canon (cited); **iron vault door** (WANTED); no windows.
- **Windows:** few, small, high, **barred** at ground level.
- **Archive:** stone enclosure / fire door.
- **Roof:** tile/slate (urban) — WANTED material.
- *WANTED materials:* dressed/ornamental stone (grand façade), clay tile/slate, iron (door/bars).

## 8. Signature / legibility
Imposing solid stone façade; barred ground-floor windows; a heavy **iron-bound door**; a **coin/scales pictorial
sign** (WANTED decal); the vault reads as a windowless masonry mass; often taller/grander than neighbours to
signal trust.

## 9. Status / period / setting scaling
- **Low:** a money-lender — a strongbox/coffer in a locked back room of a `townhouse`. No vault, no hall.
- **Mid:** a merchant counting house — counting room + a small strongroom + a public counter.
- **High:** a banking palazzo — grand hall, vault, archive, guards, ornate façade.
- **Fantasy (BG3):** a **warded** vault (arcane glyphs → world bible), construct/golem guards, a portal-sealed strongroom. Overlay per Part 9; the *structure* stays grounded.

## 10. Function testers (the deliverable)
- **F1** Exactly one public entrance exists.
- **F2** No path from the public hall to the vault avoids controlled staff space.
- **F3** The vault is fully enclosed to spec — every wall, the floor, and the ceiling; no window; one door.
- **F4** A counter/grille separates public from staff with no human-passable gap.
- **F5** A guard post sees the entrance **and** the vault door.
- **F6** The ledger archive is fire-separated from all timber/hearth areas.
- **F7** The banking hall can be barred against a crowd.
- **F8** Higher tiers add access *layers*, never remove them.

## 11. Fixtures & assets needed (→ backlog)
Iron vault door, deposit boxes/strongboxes, coin scales/balance, counting board, teller grille/counter, ledgers,
coin coffers, guard's weapon rack, coin/scales sign decal. → tracked in
[`WantedAssetsBacklog.md`](../../WantedAssetsBacklog.md) §2/§3.

## 12. Grounding ledger
| Claim | Provenance |
|---|---|
| counting house origin (Italy, high Middle Ages) + open counting board | CITED — [Counting house (Wikipedia)](https://en.wikipedia.org/wiki/Counting_house) |
| strongbox/coffer as the security furniture (oak + iron straps) | CITED — [Chests of the Middle Ages (larsdatter)](http://www.larsdatter.com/chests.htm) |
| vault wall thickness | REUSE-CANON — keep/retaining (2–4 m fortified, ≥0.667 m stone), itself grounded |
| all room **sizes** | `to_ground` — no clean medieval bank room dims found |
| bank-as-institution + built masonry vault | GENRE-FLAG — late-medieval/early-modern |
| fantasy overlay (wards/golems) | BIBLE-SOURCED (Part 9 / CC) |

## 13. Open questions / unknowns
- Real medieval/Renaissance banking-house **room sizes** (e.g. the Medici bank buildings) — unverified; needs a specialised architectural-history source.
- Did medieval counting houses have **built masonry vaults**, or only strongrooms + coffers? Evidence leans coffers/strongrooms early, built vaults later — confirm before encoding a vault as default.
- Does the vault **floor** require the Part 8 subterranean tier (tunnelling defence) — interaction to resolve.

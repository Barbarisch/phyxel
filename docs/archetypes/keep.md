# Keep / Great Tower — Archetype Data Sheet

> Status: **DRAFT**. Schema: [`README.md`](README.md). Dimensions reuse Part 6 (auditor-corrected). The core of [`castle`](castle.md).

## 1. Identity
- **id:** `keep` / `great_tower`
- **function:** the castle's strongest residence + last refuge
- **aka:** donjon, great tower
- **group:** Power / fortified (Part 6)
- **extends:** a massive fortified tower (the grandest `tower_house`)
- **genre/period:** real.

## 2. Essence
The castle's **strongest building** — a massive tower housing the lord's hall + chambers + chapel + **well**,
over a vaulted basement, entered at first-floor through a **forebuilding**, able to stand alone as the **last
refuge**. Defining quality: maximum wall thickness + a self-contained defensible residence + the last-stand role.

## 3. Threat model / failure modes
- **Siege** (the keep is the last refuge) → max walls (to **6.4 m** at Dover), a first-floor forebuilding
  entrance, a **well inside** (siege water), thick floors, mural chambers/stairs, a battlemented top.

## 4. Access tiers / zoning
- **T0** the forebuilding + first-floor entrance (defended).
- **Basement** — vaulted store + the **well** + a dungeon below (Part 8).
- **T1** the great hall (main floor).
- **T2** the lord's great chamber + solar + chapel (upper, private).
- **T3** battlements + corner turrets (defence).

## 5. Required spaces (program)
| Space | Tier | Purpose | Required fixtures | Size |
|---|---|---|---|---|
| vaulted basement | — | store + water | a **well**, stores, (a dungeon below — Part 8) | ground |
| great hall | T1 | lordship | hearth, high table | entrance floor |
| great chamber + solar | T2 | the lord | bed, fireplace | upper |
| chapel | T2 | worship | altar (often a corner turret) | upper |
| garderobes + mural stairs | — | sanitation + access | in the wall thickness | — |
| battlements + turrets | T3 | defence | crenellations, arrow loops | top |
| forebuilding | T0 | the defended entrance stair | a stair to the first floor | entrance |

## 6. Adjacency & circulation rules
1. The **entrance is at first-floor via a forebuilding** (the most defended point).
2. The **well is inside** (siege water).
3. The **great hall on the entrance floor**; **private apartments + chapel above**; the **dungeon below** (Part 8).
4. **Garderobes + stairs in the wall thickness.**
5. **Battlements + corner turrets** on top.
6. Stands within the castle's **inner bailey**.

## 7. Construction & materials
- Stone, **massive walls** (grounded: Dover to **6.4 m**; Pembroke round **16 m dia × 24 m**; shell-keep **3–3.5 m thick × 4.5–9 m**; general keep walls **2–4 m** — *auditor-corrected*); a vaulted basement; mural passages; battlements. Reuse Part 6.

## 8. Signature / legibility
A **massive square or round stone tower** with battlements + corner turrets + a forebuilding, dominating the
castle; few, small, high openings; the tallest, strongest thing.

## 9. Status / period / setting scaling
- **Down:** a `tower_house`.
- **Up:** a hall-keep / tower-keep → a great royal keep (Dover/Rochester); a shell keep (a wall around a motte top).
- **Fantasy:** a dark lord's keep (bible).

## 10. Function testers
- **F1** A **first-floor entrance via a forebuilding** (the defended approach).
- **F2** A **well inside** (siege water).
- **F3** **Massive walls** (reuse the grounded 2–4 m+, up to 6.4 m) + few small high openings.
- **F4** A self-contained residence (hall + chambers + chapel) able to be the last refuge.
- **F5** Garderobes + stairs in the wall thickness.
- **F6** Battlements + corner turrets on top.
- **F7** A vaulted basement (store + optional dungeon below, Part 8).
- **F8** Stands in the inner bailey (part of a `castle`, #31).

## 11. Fixtures & assets needed (→ backlog)
Well, great-hall + chamber furnishings, chapel altar, garderobes, mural stair, battlements, forebuilding stair. →
[`WantedAssetsBacklog.md`](../WantedAssetsBacklog.md).

## 12. Grounding ledger
| Claim | Provenance |
|---|---|
| Dover 29.5 m sq / 25.3 m tall / walls to 6.4 m; Pembroke round 16 m / 24 m; shell-keep 3–3.5 m / 4.5–9 m; keep walls 2–4 m | REUSE-CANON — Part 6 (cited + **auditor-corrected**: 1.5 m → 2 m lower bound) |
| first-floor forebuilding entrance + well + battlements | keep norm |
| floor area per story | `to_ground` |

## 13. Open questions / unknowns
- Square vs round vs shell keep by period (Norman square → later round) — encode as variants.
- Floor area per story — `to_ground`.

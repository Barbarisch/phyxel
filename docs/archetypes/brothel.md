# Brothel / Stews — Archetype Data Sheet

> Status: **DRAFT**. Schema: [`README.md`](README.md).

## 1. Identity
- **id:** `brothel`
- **function:** commercial sex (and, in origin, bathing)
- **aka:** stews, stewhouse, bawdy house
- **group:** Entertainment & vice (Part 6)
- **extends:** a house with multiple let chambers (often a former bathhouse)
- **genre/period:** real (medieval "stews", a **regulated** trade — Southwark/Bankside).

## 2. Essence
A regulated **"stews"** — a discreet reception/common room + **let chambers** ("cameren") where the women
boarded and worked, frequently grown from a bathhouse. Defining quality: a discreet reception + private
chambers + the **stewholder's control**, in a **tolerated zone**.

## 3. Threat model / failure modes
- **Discretion / reputation** — sited in a tolerated district (Bankside / extramural / riverside).
- **Disease**, **violence** (a watchman), **fire**.
- **Regulation** — episcopal landlords; rules posted; chamber rent capped (14 d/week).

## 4. Access tiers / zoning
- **T0** parlour / common room — reception (victual-selling was restricted).
- **T1** private chambers ("cameren") — let to the women.
- Stewholder's lodging (controls access); (bath origin) bathing rooms.

## 5. Required spaces (program)
| Space | Tier | Purpose | Required fixtures | Size |
|---|---|---|---|---|
| parlour / common | T0 | reception | hearth, seating | `to_ground` |
| chamber ×N | T1 | private | a bed, a chest, a candle (one-room "cameren") | `to_ground` |
| stewholder_quarters | — | control access | dwelling fixtures | `to_ground` |
| bathing room *(bath-stews)* | — | bathe | tubs, a furnace, water | `to_ground` |
| privy | — | sanitation | — | — |

## 6. Adjacency & circulation rules
1. The reception fronts a **discreet entrance**.
2. Chambers are **private**, off a stair/passage.
3. The **stewholder's quarters control access**.
4. *(Bath-stews)* bathing rooms need **hot water + drainage**.
5. Sited in a **tolerated district** (Bankside / extramural / riverside — Z-tier).

## 7. Construction & materials
- A house shell (`townhouse`/`hall_house`) with multiple chambers; *(bath)* a furnace + tubs + water.
- WANTED: beds, tubs, a painted sign (Bankside stews bore painted signs on the river frontage).

## 8. Signature / legibility
A row of chambers; (Bankside) a **painted sign facing the river**; a discreet-but-known house in a tolerated zone.

## 9. Status / period / setting scaling
- **Down:** a small bawdy-house (a few chambers).
- **Up:** a stews (many chambers + bathing) → a regulated Bankside row.
- **Fantasy:** a lavish pleasure-house (BG3 Sharess's Caress) — overlay.

## 10. Function testers
- **F1** A reception/common room + multiple private **let chambers**.
- **F2** Chambers private (off a controlled passage/stair), each a bed + minimal furnishing.
- **F3** A stewholder's quarters controlling access.
- **F4** Sited in a tolerated/peripheral zone (extramural / riverside / a vice district).
- **F5** *(bath-stews)* bathing rooms with hot water + drainage.
- **F6** A privy.

## 11. Fixtures & assets needed (→ backlog)
Beds, chests, candle holders, parlour hearth + seating, (bath) tubs + furnace, painted sign. →
[`WantedAssetsBacklog.md`](../WantedAssetsBacklog.md).

## 12. Grounding ledger
| Claim | Provenance |
|---|---|
| stews = bathhouse origin; multi-room "houses" + one-room "cameren"; regulation (Henry II 1161, episcopal landlord, 14 d/week chamber, victual restrictions) | CITED — [Medieval & Tudor Brothels of Southwark](https://www.lovebritishhistory.co.uk/2025/05/the-medieval-and-tudor-brothels-of.html); Coomans, *The Medieval Bathhouse* (thesis) |
| tolerated-zone siting (Bankside / extramural) | CITED — same |
| chamber **sizes** | `to_ground` |

## 13. Open questions / unknowns
- Chamber size — `to_ground`.
- How long the bathing function persisted vs a pure brothel by period.

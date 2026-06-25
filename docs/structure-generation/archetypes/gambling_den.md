# Gambling Den — Archetype Data Sheet

> Status: **DRAFT**. Schema: [`README.md`](README.md).

## 1. Identity
- **id:** `gambling_den`
- **function:** gaming for money (dice, cards, etc.)
- **aka:** gaming house, dicing house
- **group:** Entertainment & vice (Part 6)
- **extends:** `tavern` / `townhouse` shell
- **genre/period:** gaming is ancient/real; a **dedicated illicit "den"** is a flavour archetype — mild flag. Often **illegal** (hidden).

## 2. Essence
A **gaming floor wrapped with drink and discreet/secure access** — often hidden behind a legitimate front.
Defining quality: the gaming floor + a bar + a discreet entrance + a **bolt-hole for a raid**.

## 3. Threat model / failure modes
- **A raid** (when illegal) — a hidden entrance + a **separate back exit / bolt-hole** + a lookout.
- **Cheating / violence** — a watchman/enforcer.
- **Theft** — the house bank/coin.

## 4. Access tiers / zoning
- **T0** front — a legitimate face (a tavern/shop) or a discreet door.
- **T1** gaming floor — tables, the house bank, a bar.
- Back room (high-stakes/private); a **hidden exit** (raid escape → secret passages, Part 8); a lookout point.

## 5. Required spaces (program)
| Space | Tier | Purpose | Required fixtures | Size |
|---|---|---|---|---|
| gaming_floor | T1 | play | gaming **tables** + stools, a **bar**, the house **coffer/bank**, lamps | reuse `tavern` common room |
| back room | — | high-stakes/private | a private table | `to_ground` |
| discreet entrance + hidden exit | — | get in / escape a raid | a concealed door (→ Part 8) | — |
| lookout | — | watch the approach | a vantage | — |
| cover front | T0 | hide the den | a tavern/shop face | — |

## 6. Adjacency & circulation rules
1. The gaming floor sits **inside/behind a cover** (a tavern back room, a cellar, an upper room).
2. A **discreet entrance + a separate hidden exit** (bolt-hole) for a raid.
3. A **lookout** covers the approach.
4. The **bar serves** the floor; the **bank/coffer is secured**.

## 7. Construction & materials
- A `tavern`/`townhouse` shell + a **concealed room/entrance** (→ secret passages, Part 8); tables + lamps.
- WANTED: gaming tables, dice/cards props, a concealed door.

## 8. Signature / legibility
From outside — **nothing** (hidden); inside — a smoky, lamp-lit room of tables + a bar; a lookout at the door.

## 9. Status / period / setting scaling
- **Down:** a back-room dice game.
- **Up:** a gaming house → a grand casino/festhall (fantasy).
- **Fantasy:** a magical/rigged gaming house.

## 10. Function testers
- **F1** A gaming floor (tables + seating + a bar).
- **F2** A secured house **bank/coffer**.
- **F3** *(if illicit)* a discreet entrance + a **separate hidden exit / bolt-hole** + a lookout covering the approach.
- **F4** *(if illicit)* a cover/legitimate front (tavern/shop).
- **F5** A private/high-stakes back room.

## 11. Fixtures & assets needed (→ backlog)
Gaming tables, stools, bar, coin coffer/bank, lamps, dice/cards, concealed door. →
[`WantedAssetsBacklog.md`](../../WantedAssetsBacklog.md).

## 12. Grounding ledger
| Claim | Provenance |
|---|---|
| gaming for money | ancient/real (general) |
| dedicated illicit den + hidden bolt-hole | reasoned from illegality — FLAG (flavour archetype) |
| shell + secret exit | REUSE — `tavern` + secret passages (Part 8) |
| **sizes** | `to_ground` |

## 13. Open questions / unknowns
- Legality varied (some gaming was licensed) — affects whether the "hidden" features are required vs an open gaming house.

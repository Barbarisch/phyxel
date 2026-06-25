# Civic Palace / Seat of State — Archetype Data Sheet

> Status: **DRAFT**. Schema: [`README.md`](README.md). *(BG3: the High Hall / Ducal Palace.)*

## 1. Identity
- **id:** `civic_palace` / `seat_of_state`
- **function:** the seat of a ruler/government — audience, council, administration, residence
- **aka:** ducal palace, doge's palace, town palace, High Hall
- **group:** Civic & institutions (Part 6)
- **extends:** `manor_hall` / `keep` for the great hall; `manor` for the apartments
- **genre/period:** real (Italian/Flemish town palaces; great medieval halls).

## 2. Essence
A **monumental audience hall + governance rooms + the ruler's apartments** — the architectural seat of power,
bigger and grander than a `town_hall`'s moot hall. Defining quality: a graded public→governance→private→secure
sequence performing authority.

## 3. Threat model / failure modes
- **Status / authority display** (the primary driver).
- **Security** — guards + controlled access (a target).
- **Records + treasury** — secure archive + a treasury/strongroom.

## 4. Access tiers / zoning
- **Public:** gate → court → **audience/throne hall**.
- **Governance:** council chamber, offices, courts of law.
- **Private:** the ruler's apartments (reuse `manor`).
- **Secure:** treasury + archive; a guard force throughout.

## 5. Required spaces (program)
| Space | Tier | Purpose | Required fixtures | Size |
|---|---|---|---|---|
| audience_hall | public | receive, rule | a dais/throne, the great hall | REUSE `manor_hall` (monumental) |
| council_chamber | governance | govern | a council table/seats | `to_ground` |
| offices / courts | governance | administer/judge | desks, a court seat | `to_ground` |
| ruler's apartments | private | reside | (reuse `manor` great chamber/solar) | `to_ground` |
| treasury / archive | secure | money + records | a vault (reuse `bank`/keep) | secure |
| guard post(s) | — | control | weapon rack, sightlines | — |

## 6. Adjacency & circulation rules
1. The **gate controls** the court; the **audience hall opens off the court**.
2. **Governance rooms** sit between public and private.
3. The **ruler's apartments are private**, separated from the public hall.
4. The **treasury/archive is secure** (reuse the `bank` vault rules).
5. **Guards** cover the entrance + the private/secure approaches.

## 7. Construction & materials
- Stone, monumental; a great hall; ornate fenestration; a gatehouse/court.
- WANTED: marble, ornamental dressed stone, glazing, banners/heraldry (decal).

## 8. Signature / legibility
A **monumental façade + a grand hall + a gated court**; guarded; the most imposing secular building in the city.

## 9. Status / period / setting scaling
- **Down:** a grand `town_hall`.
- **Up:** a doge's/ducal palace (audience hall + courts + treasury + apartments + a court of guards).
- **Fantasy:** a ruler's palace with warded treasury/throne (bible overlay).

## 10. Function testers
- **F1** A monumental audience/throne hall.
- **F2** Council/governance chambers.
- **F3** The ruler's private apartments, separated from the public hall.
- **F4** A secure treasury/archive (reuse the `bank` vault testers).
- **F5** Controlled access + a guard force.
- **F6** *(defensible)* a gatehouse/court.

## 11. Fixtures & assets needed (→ backlog)
Throne/dais, council table, court seating, vault door, banners/heraldry (decal), marble, ornamental stone. →
[`WantedAssetsBacklog.md`](../../WantedAssetsBacklog.md).

## 12. Grounding ledger
| Claim | Provenance |
|---|---|
| great/audience hall + apartments | REUSE-CANON — `manor_hall` + `manor` |
| treasury/vault security | REUSE-CANON — `bank` / keep |
| town-palace as a real type (Italian/Flemish) | general medieval/Renaissance civic |
| palace **sizes** | `to_ground` |

## 13. Open questions / unknowns
- A surveyed medieval town-palace plan for proportions — `to_ground`.
- How much it overlaps `castle` when fortified — boundary to define.

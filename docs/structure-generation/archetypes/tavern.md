# Tavern / Inn — Archetype Data Sheet

> Status: **DRAFT**. Schema: [`README.md`](README.md).

## 1. Identity
- **id:** `tavern` / `inn`
- **function:** food, drink, and (inn) lodging + stabling for travellers and locals
- **aka:** alehouse (drink only), tavern (wine), inn (drink + lodging + stabling) — a **real period distinction**
- **group:** Hospitality (Part 6)
- **extends:** `hall_house` shell for the common room + a stable/yard
- **genre/period:** real. *(The galleried-courtyard coaching inn is more Tudor/early-modern — minor flag.)*

## 2. Essence
A **public common room wrapped around a hearth** — conviviality and revenue — fed by service (kitchen/cellar)
and, for an inn, backed by **lodging + stabling**. Defining quality: public sociability under the innkeeper's
control of drink, food, and beds.

## 3. Threat model / failure modes
- **Theft / drunken violence** — the common room must be watchable from the bar.
- **Fire** — hearth + drink + straw beds.
- **Spoilage** — drink/food need a cool cellar.
- **A traveller's security** — (semi-)private rooms + safe stabling for valuable horses.

## 4. Access tiers / zoning
- **T0 Public** — the common room (entrance from street/yard).
- **T1 Service** — kitchen, cellar (staff).
- **T2 Private** — guest chambers, innkeeper's quarters.
- **Yard/stable** — semi-public.
- The **bar/serving counter** is the public/service boundary (controls the drink).

## 5. Required spaces (program)
| Space | Tier | Purpose | Required fixtures | Size |
|---|---|---|---|---|
| common_room | T0 | drink, eat, gather | **serving bar**, hearth, boards (tables) + forms (benches) + stools, a high-backed **settle** by the hearth, casks | REUSE `hall_house` hall; `to_ground` |
| kitchen | T1 | cook | cook-hearth, work table, shelving | `to_ground` |
| cellar | T1 | cool storage | casks/barrels, bins | `to_ground`; cool/below |
| guest_chamber ×N | T2 | lodging *(inn)* | beds (straw, 1–3/room), candle holder, wall hooks | ≥ ~3 m any dim *(FLAG: RPG planning figure)* |
| innkeeper_quarters | T2 | owner's dwelling | dwelling fixtures | `to_ground` |
| stable + yard | semi | patrons' horses | stalls, hayrack, troughs, muck-out | `to_ground` (reuse Part 3 stable) |
| privy | — | sanitation | — | away from kitchen |

*(Inns held anywhere from ~5 to 17 chambers.)*

## 6. Adjacency & circulation rules
1. The common room fronts the entrance/yard; it is the largest room.
2. The **bar sits between common room and service**, controlling drink flow.
3. The cellar is below/adjacent the kitchen, kept cool.
4. Guest chambers are above/behind, reached by a stair off the common room — not through the kitchen.
5. The stable is on the yard; muck away from the well/kitchen.
6. The privy is separated from the kitchen.

## 7. Construction & materials
- Timber-framed `hall_house` shell; a large hearth + chimney; a cool cellar; a hung sign at the street.
- WANTED: **hanging inn-sign decal**, casks, settle, long boards/forms, beds.

## 8. Signature / legibility
A **hanging painted sign**; a large smoky common room seen through the door; (inn) a **stable yard**; a chimney.

## 9. Status / period / setting scaling
- **Low:** an alehouse — one room + casks, drink only.
- **Mid:** a tavern — common room + cellar + kitchen.
- **High:** a coaching inn — many chambers + a stable yard + a galleried courtyard *(Tudor flag)*.
- **Fantasy:** the BG3 Elfsong-type tavern — same program + a setting-bible overlay (a resident ghost, a magical sign).

## 10. Function testers
- **F1** A common room with a serving bar **and** a hearth **and** seating.
- **F2** Drink storage (cellar/casks) reachable by staff, not the public.
- **F3** *(inn)* Guest chambers with beds, off a controlled route (not via the kitchen).
- **F4** *(inn)* Stabling on a yard.
- **F5** Kitchen vented and adjacent the common room.
- **F6** The bar forms the public/service boundary.
- **F7** Privy separated from the kitchen.

## 11. Fixtures & assets needed (→ backlog)
Serving bar/counter, casks/barrels, long boards + forms + stools + **settle**, beds, candle holders, stable
fittings, **inn-sign decal**. → [`WantedAssetsBacklog.md`](../../WantedAssetsBacklog.md).

## 12. Grounding ledger
| Claim | Provenance |
|---|---|
| room program (hall/kitchen/cellar/5–17 chambers/innkeeper/stable/privy) | CITED — [Ye Ol' Bed & Breakfast (Medievalists.net)](https://www.medievalists.net/2015/02/ye-ol-bed-breakfast-look-medieval-inn/) |
| common-room furniture (boards/forms/stools/settle) | CITED — same |
| alehouse / tavern / inn distinction | CITED — same |
| guest-room ≥ ~3 m | FLAG — RPG planning figure (onestopforwriters lineage), not a historical standard |
| common-room size | REUSE-CANON — `hall_house` |
| galleried coaching inn | GENRE-FLAG — Tudor/early-modern |

## 13. Open questions / unknowns
- Typical common-room + chamber sizes — `to_ground`.
- Did locks exist on guest doors? (Sources: usually not.) — affects the security tester.

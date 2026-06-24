# Bathhouse / Stews — Archetype Data Sheet

> Status: **DRAFT**. Schema: [`README.md`](README.md). Overlaps [`brothel`](brothel.md) (the "stews").

## 1. Identity
- **id:** `bathhouse`
- **function:** communal hot bathing
- **aka:** stews, stewhouse, hothouse
- **group:** Civic & institutions (Part 6)
- **extends:** a heated hall + a furnace/water plant
- **genre/period:** real (medieval bathhouses; Southwark had 18) — the **bath↔brothel overlap** is period-true.

## 2. Essence
A **heated communal bathing hall** — hot water from a furnace, tubs/sweat rooms, changing. Defining quality: the
heat source + water supply/drainage + the bathing hall. (Often blurred with the `brothel`.)

## 3. Threat model / failure modes
- **Fire** — the furnace.
- **Water** — supply in + drainage out.
- **Morality / regulation** — bathhouses doubled as brothels; regulated (the "stews").

## 4. Access tiers / zoning
- Changing room → **bathing hall** (tubs/sweat) → furnace + water plant; (let) private chambers (→ `brothel`).

## 5. Required spaces (program)
| Space | Purpose | Required fixtures | Size |
|---|---|---|---|
| bathing_hall | bathe | **tubs/vats** (hot + cold), benches | `to_ground` |
| furnace / water plant | heat + supply | a **furnace**, water in, **drainage** out | `to_ground` |
| changing room | undress | benches, hooks | `to_ground` |
| sweat room *(optional)* | sweat | a heated chamber | `to_ground` |
| private chambers *(stews)* | privacy | a bed *(→ `brothel`)* | `to_ground` |

## 6. Adjacency & circulation rules
1. The **furnace + water supply + drainage** serve the bathing hall.
2. A **changing room** at the entrance.
3. **Sited with water access** (a conduit/well/stream).
4. *(Stews)* private chambers off the hall (the brothel overlap).

## 7. Construction & materials
- A heated hall; a **furnace** (the medieval norm — *not* a Roman hypocaust); tubs; drainage.
- WANTED: tubs/vats, furnace, water/drainage props.

## 8. Signature / legibility
Steam + smoke from the furnace; tubs; (Bankside) a painted "stews" sign; near water.

## 9. Status / period / setting scaling
- **Down:** a single hothouse with a few tubs.
- **Up:** a stews (bathing + chambers — `brothel` overlap) → a grand bathhouse.
- **Roman/fantasy:** a hypocaust-heated bath complex (anachronistic for medieval — flag).

## 10. Function testers
- **F1** A bathing hall with tubs/vats.
- **F2** A heat source (furnace) + water supply + drainage.
- **F3** A changing room.
- **F4** *(period)* note the stews↔brothel overlap (regulation).
- **F5** Sited with water access.

## 11. Fixtures & assets needed (→ backlog)
Tubs/vats, furnace, water/drainage fittings, benches, painted sign. →
[`WantedAssetsBacklog.md`](../WantedAssetsBacklog.md).

## 12. Grounding ledger
| Claim | Provenance |
|---|---|
| medieval bathhouses ("stews"); Southwark's 18; bath↔brothel overlap, regulated | CITED — Coomans, *The Medieval Bathhouse* (thesis); [Southwark stews](https://www.lovebritishhistory.co.uk/2025/05/the-medieval-and-tudor-brothels-of.html) (reused from `brothel`) |
| furnace-heated tubs (not a hypocaust) | medieval norm; hypocaust = Roman GENRE-FLAG |
| **sizes** | `to_ground` |

## 13. Open questions / unknowns
- Bathhouse hall size + tub count — `to_ground`.
- How to model the bath/brothel overlap — shared shell with `brothel`?

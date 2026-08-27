# Tenement (Purpose-Built Rental Row Unit) — Archetype Data Sheet

> Status: **GROUNDED (scale) / DESIGN-DECISIONS DISCLOSED (orientation, stair)**. Schema:
> [`README.md`](README.md). **First typology wired for CityForgePlan M4** (user ask:
> "apartment buildings"). Wealth-gradient neighbours: [`townhouse.md`](townhouse.md)
> (middling, mixed-use) above it, [`slum_tenement.md`](slum_tenement.md) (squalor overlay,
> designed-to-fail) below it. This sheet is the HONEST middle: purpose-built, minimal,
> respectable rental housing — the medieval "apartment".

## 1. Identity
- **id:** `tenement`
- **function:** stacked one-room-per-floor rental dwelling — a unit of a "rents" row
- **aka:** rents, row house, chantry row, cottage row
- **group:** Dwelling (Part 6) — humble tier, urban
- **extends:** the burgage plot; rows form by CONTIGUOUS PACKING of units (side_gap 0), not
  by a special multi-unit building — exactly how the engine's burgage allocator already works
- **genre/period:** real (the 14th-century urban rental row).

## 2. Essence
**Maximum respectable dwellings per street-length**: a narrow two-storey unit, one all-purpose
room below (hearth, table, storage) and one chamber above, repeated wall-to-wall along the
street. Defining quality: REPETITION — a row of identical narrow units reads instantly as
rental housing, and as *city*.

## 3. Threat model / failure modes
- **Fire** — party-wall condition, shared risk down the row (medieval reality; no fire breaks).
- **Theft** — one stout door; ground-floor shutters.
- **Damp/cold** — hearth in the ground room is the only heat.

## 4. Access tiers / zoning
- **T1** ground room — the household's everything-room (cook + eat + work).
- **T2** upper chamber — sleep; reached by the unit's own stair. No public tier at all
  (that's what separates it from `townhouse`'s T0 shopfront).

## 5. Required spaces (program)
| Space | Tier | Purpose | Required fixtures | Size |
|---|---|---|---|---|
| ground room | T1 | `living` (croft recipe REUSED — conformant today) | hearth, table, seating, chest | one unit footprint |
| upper chamber | T2 | `bedchamber` (tavern-upper recipe REUSED — conformant today) | bed, chest | one unit footprint |
| stair | — | vertical circulation | generated (forge StairPlanner) | inside the unit |

**REFUSE-ON-ANY-GAP note:** both purposes ship conformant (croft `living`, inn `bedchamber`)
— the typology is a pure DATA commit, no new assets.

## 6. Adjacency & circulation rules
1. Gable (narrow end) to the street; the unit runs BACK from the frontage.
2. Units pack contiguously (side_gap 0) — the ROW is the settlement allocator's doing.
3. One entrance on the street gable; the stair serves the single upper chamber.
4. Never fenced (city core rule — `shouldFencePlot` already handles this).

## 7. Construction & materials
Timber frame on the shared bay module; the engine's standard style profiles apply
(timber_cottage default). Jettied upper floors are the documented form (Our Lady's Row is
"the earliest surviving jettied building in England") — **jetty is NOT modelled** (no
overhang mechanism in the realizer; logged as a wanted feature, not faked).

## 8. Signature / legibility
A repeated narrow gabled front, door + one window per unit, wall-to-wall down the street.
The repetition IS the signature.

## 9. Status / period / setting scaling
Humble tier. Scaling up → `townhouse` (adds the shopfront + more storeys); scaling down →
`slum_tenement` (subdivision + decay overlay). Fantasy: guild-built worker rows, dockside
rents — same form, setting overlay.

## 10. Function testers
- **F1** Every unit's upper chamber is REACHABLE (forge traversal gate — enforced at build).
- **F2** Ground room furnishes as a working household (living recipe places, honest unfit counts).
- **F3** A city-core row shows ≥3 contiguous tenement units at density ≥1.25 (the row read).
- **F4** The unit never exceeds its plot (allocator invariant, already gated).

## 11. Fixtures & assets needed (→ backlog)
None for v1 (reuses living + bedchamber vocabularies). Wanted later: jetty overhang
(realizer feature), shutters-down variant, condition/decay overlay shared with slum_tenement.

## 12. Grounding ledger
| Claim | Provenance |
|---|---|
| Purpose-built rental ROW of stacked one-room dwellings, 2 storeys, one room per floor | CITED — Our Lady's Row, Goodramgate, York (built 1316 to endow the Holy Trinity chantry; earliest surviving row houses / jettied building in England) — Wikipedia "Our Lady's Row"; RCHME York vol. 5 |
| Per-unit scale ≈ 5.5 m frontage × 4.5 m depth (≈18×15 ft) | NEEDS-VERIFY — commonly quoted for Our Lady's Row; verify against RCHME York before citing as measured (engine dims below stay inside the plausible envelope either way) |
| "Rents" as a purpose-built investment form (chantry/monastic endowment rows) | CITED — Sarah Rees Jones on York rents; Tewkesbury Abbey Lawn cottages (15th c. monastic rental row) as the second exemplar |
| Engine unit = gable-to-street, 2 bays (8 m) deep × 4–5 m frontage | **DESIGN DECISION ×2, disclosed:** (a) Our Lady's Row runs its FRONTAGE along the street (eaves-on); the engine unit turns gable-on because burgage packing + the city core's narrow-frontage economics want it, and the allocator's row packing recreates the row read either way. (b) The documented 4.5 m depth cannot host the forge's generated stair + landing (traversal gate would refuse); 2 bays gives the stair an honest run. Medieval ladder-stairs are not modelled — a reachability-gated engine demands a real stair. |
| One room per floor | CITED — Our Lady's Row unit plan (single room up, single room down) |

## 13. Open questions / unknowns
- Verify the per-unit dimensions against RCHME York (flagged above).
- Jetty mechanism (realizer) — wanted, not faked.
- Party-wall sharing (true zero-gap shared walls) — units currently ABUT with their own
  walls; a shared-wall optimization is a realizer feature, not data.

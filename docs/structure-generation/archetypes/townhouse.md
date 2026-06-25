# Townhouse (Urban Burgage House) — Archetype Data Sheet

> Status: **DRAFT**. Schema: [`README.md`](README.md).

## 1. Identity
- **id:** `townhouse`
- **function:** urban dwelling, usually **mixed-use** (trade/work below, dwelling above)
- **aka:** burgage house, merchant's house
- **group:** Dwelling (Part 6)
- **extends:** the **burgage plot** (Part 7); the upper floors are jettied timber frame
- **genre/period:** real (the medieval urban house type).

## 2. Essence
A **narrow, deep, tall** house squeezing every use out of scarce street frontage on a burgage plot — gable (or
eaves) to the street, shop/workshop at ground, dwelling stacked above, **jettied** out over the street to gain
floor area. Defining: narrow frontage + verticality + mixed-use + the **party-wall** urban condition.

## 3. Threat model / failure modes
- **Fire** — dense timber + shared walls = the city-fire risk (party walls ideally masonry).
- **Street security** — shutters + a stout door at ground.
- **Space scarcity** — build **up** and **jetty out**.
- **Privacy gradient** — public shop ↔ private upper floors.

## 4. Access tiers / zoning
- **T0** shopfront / ground — public: shop or workshop opening to the street.
- **T1** hall / living — semi: the household's main room (often the first floor).
- **T2** chambers — private: upper floors.
- **Service** — kitchen + yard at the rear.
- **Cellar** — storage, below (sometimes a vaulted undercroft let separately).

## 5. Required spaces (program)
| Space | Tier | Purpose | Required fixtures | Size |
|---|---|---|---|---|
| shopfront / workshop | T0 | trade/work | counter + **shutters**, display | `to_ground` |
| hall / living | T1 | main room | hearth, table, seating | REUSE Part 3 hall/living |
| chamber ×N | T2 | sleep | bed, chest | REUSE Part 3 bedchamber |
| kitchen | service | cook | cook-hearth | rear (fire separation) |
| cellar / undercroft | — | storage | barrels, bins | below |
| rear yard | — | privy, well, garden strip | privy, well | behind the street range |
| stair | — | vertical circulation | — | serves stacked floors |

## 6. Adjacency & circulation rules
1. The shop **fronts the street**; the hall is above/behind it.
2. Chambers **stack** on the upper floors.
3. The **kitchen is at the rear**, fire-separated from the street range.
4. The plan is **deep** — the long axis runs back from the street.
5. **Party walls** are shared with neighbours in the dense core (not freestanding).
6. The **jetty projects the upper floors over the street**.
7. A rear **yard + privy + well** sit behind the street range.

## 7. Construction & materials
- Timber-framed, **jettied**, on a masonry ground floor / cellar.
- **Party walls** ideally masonry (fire).
- **Plaster/render** infill (WANTED), **tile** roof (WANTED), shutters.
- Jetty overhang **~0.4 m typical, up to 1.2 m** (cite).

## 8. Signature / legibility
A **narrow gabled (or eaves) street front**; **jettied** upper floors stepping over the street; a shutter-down
shopfront at ground; close-packed with neighbours (party walls).

## 9. Status / period / setting scaling
- **Low:** a subdivided tenement → see `slum_tenement`.
- **Mid:** a craftsman's house — shop + 2 floors.
- **High:** a wealthy merchant's house — wider, 3–4 floors, ornate, a courtyard, a vaulted undercroft/warehouse.
- **Fantasy:** a mage's townhouse — the ground-floor shop is an `arcane_emporium` (overlap).

## 10. Function testers
- **F1** Narrow street frontage on a burgage plot (frontage ≪ depth).
- **F2** Mixed-use where period-correct (commercial/work ground + dwelling above).
- **F3** Party walls shared with neighbours in the dense core (not freestanding).
- **F4** Kitchen/fire at the rear, separated from the street range.
- **F5** Jetty overhang within **0.4–1.2 m**.
- **F6** A rear yard with privy/well behind the street range.
- **F7** A stair serving the stacked floors.

## 11. Fixtures & assets needed (→ backlog)
Shop counter + shutters, hearth(s), beds/chests, kitchen fittings, stair, **jetty brackets**; materials:
**plaster/render, clay tile**. → [`WantedAssetsBacklog.md`](../../WantedAssetsBacklog.md).

## 12. Grounding ledger
| Claim | Provenance |
|---|---|
| burgage frontage/depth (narrow × deep) | REUSE-CANON — Part 7 (plot 16–18 × 60 m, frontage ~5–10 m) |
| jetty overhang ~0.4 m typical, up to 1.2 m | CITED — [Jettying (Wikipedia)](https://en.wikipedia.org/wiki/Jettying); [Jetty (Designing Buildings)](https://www.designingbuildings.co.uk/wiki/Jetty) |
| narrow-deep mixed-use urban form | CITED — burgage sources (Part 7) |
| party walls (urban norm) | standard practice |
| story clear height | REUSE-CANON — Part 5 (≥ 2.134 m) |
| plaster/render + tile | WANTED materials |

## 13. Open questions / unknowns
- Typical **story height + number of floors** by wealth — `to_ground` beyond the clearance minimum.
- Jetty on the first floor only vs every street-facing floor — to define (varies).

# Bakery / Bakehouse — Archetype Data Sheet

> Status: **DRAFT**. Schema: [`README.md`](README.md).

## 1. Identity
- **id:** `bakery`
- **function:** bake and sell bread
- **aka:** baker's, bakehouse, (shared) communal/seigniorial bakehouse
- **group:** Commerce (Part 6)
- **extends:** `townhouse` shell + a workshop
- **genre/period:** real.

## 2. Essence
A workshop built around a **masonry dome oven**, with a kneading area, dry flour storage, and a shopfront.
Defining quality: the **oven** (the whole trade) + **fire safety** + flour kept dry.

## 3. Threat model / failure modes
- **Fire** (primary) — the oven made bakeries a major town-fire source; often required **detached/segregated**.
- **Flour dust** — combustible.
- **Spoilage** — flour must stay dry; **heat** in the bakehouse.

## 4. Access tiers / zoning
- **T0** shopfront — sell bread.
- **T1** bakehouse floor — oven, kneading table, proving, peel.
- Flour store (dry) + fuel store; dwelling above.

## 5. Required spaces (program)
| Space | Tier | Purpose | Required fixtures | Size |
|---|---|---|---|---|
| shopfront | T0 | sell | counter, bread display | `to_ground` |
| bakehouse | T1 | bake | **masonry dome oven + chimney**, kneading table, proving area, peel rack | oven: dome, door ≈ **63% of interior height**; oven size `to_ground` (domestic vs communal) |
| flour_store | — | keep flour dry | dry bins | dry |
| fuel_store | — | feed the oven | fuel bin | near oven |
| dwelling | — | home | dwelling fixtures | above |

## 6. Adjacency & circulation rules
1. The **oven sits on a back/exterior wall, chimney-vented**.
2. The **kneading table is near the oven**; the flour store dry + near the kneading.
3. The shopfront opens to the street; fuel near the oven.
4. The dwelling is **fire-separated** from the oven.

## 7. Construction & materials
- **Non-combustible oven** — a masonry dome + chimney.
- The bakehouse is often **segregated/detached** (fire law).
- Flour store kept dry. WANTED: dome oven, kneading table, peel, bread racks.

## 8. Signature / legibility
A **chimney + the smell of bread**; a bread display at the counter; a wheatsheaf/pretzel sign; the masonry oven
mass.

## 9. Status / period / setting scaling
- **Down:** a domestic oven.
- **Mid:** a baker's shop.
- **Up:** a **communal/seigniorial bakehouse** — one oven for a whole village (drawn by lots).

## 10. Function testers
- **F1** A masonry oven on an exterior/back wall, chimney-vented.
- **F2** A kneading table near the oven.
- **F3** Dry flour storage.
- **F4** Fuel storage.
- **F5** A fire-safe envelope (non-combustible oven; ideally segregated/detached).
- **F6** A customer-facing bread counter.
- **F7** *(if dwelling)* fire-separated.

## 11. Fixtures & assets needed (→ backlog)
Dome oven, kneading table, peel, proving baskets, bread racks/display, flour bins, sign. →
[`WantedAssetsBacklog.md`](../../WantedAssetsBacklog.md).

## 12. Grounding ledger
| Claim | Provenance |
|---|---|
| masonry dome oven; door ≈ 63% of interior height (airflow) | CITED — [Masonry oven (Wikipedia)](https://en.wikipedia.org/wiki/Masonry_oven) |
| communal/seigniorial vs domestic scale (village oven, drawn by lots) | CITED — [Communal bread ovens (oldandinteresting)](http://www.oldandinteresting.com/communal-bread-ovens.aspx) |
| oven overall size | `to_ground` |
| fire-segregation of bakehouses | period fire-law logic (reuse the smithy fire-safety pattern) |

## 13. Open questions / unknowns
- Medieval oven **hearth diameter** by type — `to_ground`.
- Was the shop combined with the bakehouse, or bread sold separately/at market? — varies.

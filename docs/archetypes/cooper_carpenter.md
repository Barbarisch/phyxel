# Cooper / Carpenter — Archetype Data Sheet

> Status: **DRAFT**. Schema: [`README.md`](README.md).

## 1. Identity
- **id:** `cooper` / `carpenter`
- **function:** make barrels (cooper) / timber goods, furniture, building frames (carpenter/joiner/wright)
- **aka:** joiner, wright, turner
- **group:** Commerce (Part 6)
- **extends:** `townhouse` shell + a yard
- **genre/period:** real.

## 2. Essence
A **workshop around a workbench, a seasoned-timber store, and a yard** for raw stock and assembly. Defining
quality: the workbench (+ shaving horse / cooper's fire) + **dry-seasoned timber** + working space.

## 3. Threat model / failure modes
- **Fire** — wood everywhere + (cooper) a fire to steam/bend staves.
- **Seasoning** — timber must season **dry** or the work fails.
- **Space** — long timber + assembly need a yard.

## 4. Access tiers / zoning
- **T0** storefront / yard — sell, take orders, display.
- **T1** workshop — workbench, shaving horse, tool rack; (cooper) a fire/cresset + windlass.
- Timber store (seasoning, dry) + assembly yard; dwelling above.

## 5. Required spaces (program)
| Space | Tier | Purpose | Required fixtures | Size |
|---|---|---|---|---|
| workshop | T1 | make | **workbench + vice**, shaving horse, tool rack; *(cooper)* a **fire/cresset** to toast/bend staves + a windlass | `to_ground` |
| timber_store | — | season stock | racked long stock, dry + ventilated | dry |
| yard | — | assembly / raw timber | space for long stock + assembly | open |
| storefront | T0 | sell/order | display, order point | `to_ground` |
| dwelling | — | home | dwelling fixtures | above |

## 6. Adjacency & circulation rules
1. The **timber store is dry + ventilated (seasoning)** and near the workshop.
2. The **yard** holds long stock + assembly space.
3. *(Cooper)* the **fire/cresset is vented + fire-safe**.
4. The storefront opens to the street.

## 7. Construction & materials
- `townhouse`/workshop shell; a **long, well-lit** workshop; *(cooper)* a fire — fire-safe.
- WANTED: workbench, shaving horse, tool rack, barrels, timber racks.

## 8. Signature / legibility
**Stacked timber + barrels** in the yard; a barrel/plane sign; shavings; the sound of work.

## 9. Status / period / setting scaling
- **Down:** a jobbing carpenter — one bench.
- **Mid:** a workshop + yard.
- **Up:** a **building-yard** (framing whole houses) / a cooperage.

## 10. Function testers
- **F1** A workbench + tool storage as the work station.
- **F2** A dry, racked timber store (seasoning).
- **F3** A yard/space for long stock + assembly.
- **F4** *(cooper)* a fire/cresset for staves — fire-safe + vented.
- **F5** A storefront / order point.
- **F6** A trade sign.

## 11. Fixtures & assets needed (→ backlog)
Workbench + vice, shaving horse, tool rack, cooper's fire + windlass, timber racks, barrels, sign. →
[`WantedAssetsBacklog.md`](../WantedAssetsBacklog.md).

## 12. Grounding ledger
| Claim | Provenance |
|---|---|
| cooper bends staves over a fire; carpenter workbench + timber seasoning | general craft practice (standard) |
| shop/workshop + yard pattern | REUSE — the shop-family / `townhouse` |
| storefront = the urban shop-unit (~2–2.5 m frontage); workshop + timber yard = the burgage back-plot depth | CITED — Chester selds ([British History Online](https://www.british-history.ac.uk/vch/ches/vol5/pt2/pp225-239)); REUSE Part 7 burgage |
| workshop floor for long timber | `to_ground` |

## 13. Open questions / unknowns
- Workshop + yard footprint for working long timber — `to_ground`.
- Cooper vs carpenter vs joiner: separate sheets or one with variants? — a design call.

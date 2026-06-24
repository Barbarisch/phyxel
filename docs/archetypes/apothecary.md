# Apothecary — Archetype Data Sheet

> Status: **DRAFT**. Schema: [`README.md`](README.md).

## 1. Identity
- **id:** `apothecary`
- **function:** prepare and sell medicines, herbs, compounds, spices
- **aka:** spicer, herbalist, druggist
- **group:** Commerce (Part 6)
- **extends:** `townhouse` shell (shop ground + dwelling above)
- **genre/period:** real (medieval apothecaries/spicers; a regulated trade).

## 2. Essence
A **retail counter backed by ordered storage of many small ingredients**, plus a preparation workshop. Defining
quality: the **jar/drawer wall** (hundreds of labelled simples) + a prep bench + the counter — *order is safety*.

## 3. Threat model / failure modes
- **Theft + misuse** — dangerous substances (poisons) need a **locked/controlled** store, not open shelving.
- **Fire** — a distilling still + drying.
- **Spoilage / damp** — dry, airy storage.
- **Mislabelling** — ordered, labelled storage is a safety requirement.

## 4. Access tiers / zoning
- **T0** shop floor — counter + the jar/drawer display.
- **T1** prep workshop — mortar, balance, still, bench.
- **Secure store** — poisons + costly spices, **locked**.
- Drying loft (airy) + cool store; dwelling above.

## 5. Required spaces (program)
| Space | Tier | Purpose | Required fixtures | Size |
|---|---|---|---|---|
| shop_floor | T0 | sell | counter, **jar/drawer shelving**, scales | `to_ground` |
| prep_workshop | T1 | compound | mortar & pestle, **balance**, distilling still, bench, herb racks | `to_ground` |
| secure store | — | poisons/costly | **locked** cabinet/room | `to_ground` |
| drying loft / cool store | — | keep ingredients | racks; cool bins | airy / cool |
| dwelling | — | home | dwelling fixtures | above |

## 6. Adjacency & circulation rules
1. The **counter separates** customer from stock; the **jar/drawer wall** is behind it.
2. The prep workshop sits behind/beside the shop.
3. The **poison/costly store is locked** and access-controlled.
4. The drying loft is high + airy; the cool store cool/below.
5. Deliveries via the rear.

## 7. Construction & materials
- `townhouse` shell; a still needs **fire safety + venting**; **dry, airy** storage.
- WANTED: jar/drawer shelving, still, balance, trade-sign decal.

## 8. Signature / legibility
A **wall of labelled jars/drawers**; a hanging sign (mortar & pestle); ordered and aromatic.

## 9. Status / period / setting scaling
- **Down:** an herb stall → a village apothecary.
- **Up:** a town apothecary with a still + a secure poison store.
- **Fantasy:** an alchemist / potion-shop (overlaps `arcane_emporium`; bible).

## 10. Function testers
- **F1** A counter separating customer from the ingredient store.
- **F2** Ordered, labelled storage (the jar/drawer wall).
- **F3** A prep workshop (mortar / balance / bench).
- **F4** A **secure/locked** store for poisons + costly goods.
- **F5** Dry + airy storage (drying loft / cool store).
- **F6** *(if a still)* fire-safe + vented.
- **F7** A trade sign.

## 11. Fixtures & assets needed (→ backlog)
Counter, jar/drawer shelving, scales/balance, mortar & pestle, still, herb racks, locked cabinet, sign. →
[`WantedAssetsBacklog.md`](../WantedAssetsBacklog.md).

## 12. Grounding ledger
| Claim | Provenance |
|---|---|
| apothecary/spicer as a real regulated trade | general medieval (regulated guild trade) |
| shop pattern (counter + ingredient wall + prep workshop) | REUSE — the shop-family / `townhouse` pattern |
| locked poison store | reasoned from the regulated/dangerous nature (period apothecaries were licensed) |
| shop-floor footprint = the urban shop-unit (~2–2.5 m frontage; small lock-up ~2.3 × 3.4 m) | CITED — Chester selds ([British History Online](https://www.british-history.ac.uk/vch/ches/vol5/pt2/pp225-239)) |
| prep-workshop / drying-loft / store **sizes** | `to_ground` |

## 13. Open questions / unknowns
- Prep-workshop + drying-loft sizes — `to_ground` (the shop floor is now grounded to the shop-unit).
- Distilling prevalence by period/region (affects the still + fire tester).

# General Store / Trading Post — Archetype Data Sheet

> Status: **DRAFT**. Schema: [`README.md`](README.md).

## 1. Identity
- **id:** `general_store` / `trading_post`
- **function:** retail of varied everyday goods
- **aka:** dry-goods shop, chandlery, mercer, curiosity shop
- **group:** Commerce (Part 6)
- **extends:** the `townhouse` shell (shop at ground + dwelling above)
- **genre/period:** the **broad "general store" is more early-modern**; medieval retail was **specialised** (mercer, chandler, spicer). GENRE-FLAG (mild) — for a strict-medieval brief, prefer a single-trade shop.

## 2. Essence
A **customer-facing counter backed by dense, varied stock**, in a townhouse shell. Defining: the **counter** (the
public/stock boundary) + broad storage + a display to the street.

## 3. Threat model / failure modes
- **Theft** — the counter separates customer from stock; **lockable shutters** at night.
- **Spoilage** — some goods need a cool store.
- **Fire** — a dwelling above.

## 4. Access tiers / zoning
- **T0** shop floor — customers, display, the counter.
- **T1** back store / stockroom — staff.
- **Cellar** — bulk / cool storage.
- **Dwelling** — above (private, mixed-use).

## 5. Required spaces (program)
| Space | Tier | Purpose | Required fixtures | Size |
|---|---|---|---|---|
| shop_floor | T0 | sell | **counter**, shelving/display to the street, **scales/measures**, shutters | `to_ground` |
| back_store | T1 | stock | shelving, crates/barrels/sacks | `to_ground` |
| cellar | — | bulk/cool | barrels, bins | below |
| dwelling | — | owner's home | dwelling fixtures | above (mixed-use) |

## 6. Adjacency & circulation rules
1. The shop **fronts the street** (shutter-down counter / display opening).
2. The **counter** separates customers from the stock.
3. The back store sits **behind** the shop; the cellar below.
4. The dwelling is **above** (mixed-use), separated.
5. Deliveries come via the **rear/yard**.

## 7. Construction & materials
- `townhouse` shell (timber-framed, jettied, plaster, tile).
- A **shutter/counter opening** to the street; shelving-dense interior.
- WANTED: shutters, shelving, scales, **trade-sign decal**.

## 8. Signature / legibility
A **shutter-down counter** open to the street; a goods-laden display; a **pictorial trade sign** naming the goods
(a candle for a chandler, etc.); mixed-use above.

## 9. Status / period / setting scaling
- **Stall:** a temporary market stall — the settlement-tier version (`dress_street_life`).
- **Single-trade shop:** chandler / mercer / spicer (the medieval norm).
- **General store / emporium:** broad stock (early-modern flag).
- **Fantasy:** a curiosity shop, or the mundane half of an `arcane_emporium`.

## 10. Function testers
- **F1** A customer counter separating the public shop floor from the stock.
- **F2** A street-facing display / shuttered opening.
- **F3** Back/cellar storage reachable by staff, not the public.
- **F4** Lockable at night (shutters/door).
- **F5** *(if dwelling)* mixed-use above, separated.
- **F6** A pictorial trade sign identifying the goods.

## 11. Fixtures & assets needed (→ backlog)
Counter, shelving/display, shutters, scales/measures, crates/barrels/sacks, **trade-sign decal**. →
[`WantedAssetsBacklog.md`](../WantedAssetsBacklog.md).

## 12. Grounding ledger
| Claim | Provenance |
|---|---|
| shopfront + counter + shutter-down stall form | REUSE-CANON — the shop-family / `townhouse` |
| mixed-use (shop ground + dwelling above) | CITED — burgage sources (Part 7) |
| "general store" breadth | GENRE-FLAG (mild) — medieval used specialised shops/mercers |
| shopfront width + counter dims | `to_ground` |
| curiosity / arcane overlay | BIBLE-SOURCED (Part 9) |

## 13. Open questions / unknowns
- Shopfront width + counter dims — `to_ground`.
- Which specific medieval trades to model as **distinct** shops vs one generic `general_store` — a design call.

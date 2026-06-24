# Butcher / Shambles — Archetype Data Sheet

> Status: **DRAFT**. Schema: [`README.md`](README.md).

## 1. Identity
- **id:** `butcher`
- **function:** slaughter and sell meat
- **aka:** flesher; a clustered butchers' street = **the Shambles**
- **group:** Commerce (Part 6) — a **noxious** trade
- **extends:** `townhouse` shell + a rear yard
- **genre/period:** real (the Shambles).

## 2. Essence
A **meat counter/display backed by a slaughter + hanging area**, **noxious** (blood/offal/smell) so sited and
drained accordingly. Defining quality: the meat counter + hooks/block + the rear slaughter yard + **waste
handling**.

## 3. Threat model / failure modes
- **Spoilage** — meat needs a cool, fly-managed store (a cellar / north room).
- **Noxious waste** — blood/offal → **drainage**, a midden, **downwind + downstream**.
- **Disease** — the foul by-products.

## 4. Access tiers / zoning
- **T0** shopfront — counter, hanging hooks, chopping block (meat display to the street).
- **T1** cutting room; cool/cold store (cellar).
- Rear **slaughter yard** — holding pen, slaughter point, offal/blood drain, midden; dwelling above.

## 5. Required spaces (program)
| Space | Tier | Purpose | Required fixtures | Size |
|---|---|---|---|---|
| shopfront | T0 | sell | counter, **hanging hooks**, **chopping block**, scales | `to_ground` |
| cutting room | T1 | butcher | block, knives, hooks | `to_ground` |
| cool store | — | keep meat | cool cellar/north room | cool/below |
| slaughter yard | — | slaughter | holding pen, slaughter point, **blood/offal drain**, midden | rear |
| dwelling | — | home | dwelling fixtures | above |

## 6. Adjacency & circulation rules
1. Meat **hangs at the shopfront** (display to the street).
2. The **slaughter yard is at the rear** (out of the street's sight/smell).
3. **Blood/offal drains to a midden, away from the street and DOWNSTREAM + DOWNWIND** of dwellings + water (Z3).
4. The cool store is cool/below.
5. Butchers **cluster** (a Shambles street), ideally near running water (washing).

## 7. Construction & materials
- `townhouse` shell + a yard; **drainage channels** (blood/offal); a cool cellar.
- WANTED: meat hooks, chopping block, counter.

## 8. Signature / legibility
Meat hung at an **open counter**; a bull's-head/cleaver sign; a yard behind; flies + smell; **clustered** as a
Shambles.

## 9. Status / period / setting scaling
- **Down:** a market meat-stall.
- **Mid:** a butcher's shop.
- **Up:** a **Shambles** row (clustered) + a shared slaughterhouse.

## 10. Function testers
- **F1** A meat counter with hanging hooks + a chopping block at the front.
- **F2** A slaughter yard at the rear (pen + slaughter point + drainage).
- **F3** Blood/offal drainage to a midden, away from the street and **downstream + downwind** of dwellings + water.
- **F4** A cool store.
- **F5** Sited per the noxious-trade rule (Z3 — edge/downstream, near water, clustered as a Shambles).

## 11. Fixtures & assets needed (→ backlog)
Counter, meat hooks, chopping block, scales, holding pen, slaughter fittings, drain channel, midden. →
[`WantedAssetsBacklog.md`](../WantedAssetsBacklog.md).

## 12. Grounding ledger
| Claim | Provenance |
|---|---|
| the Shambles (clustered butchers' street); noxious siting downstream/downwind | REUSE-CANON — checklist Z3 + general medieval urban practice |
| spoilage → cool store; blood/offal drainage | reasoned from the trade |
| layout **sizes** | `to_ground` |

## 13. Open questions / unknowns
- Did slaughter happen on-site or at a shared shambles/abattoir? — varies by town size (affects whether the yard is required per shop).

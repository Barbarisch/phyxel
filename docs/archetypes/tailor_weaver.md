# Tailor / Weaver / Draper — Archetype Data Sheet

> Status: **DRAFT**. Schema: [`README.md`](README.md).

## 1. Identity
- **id:** `tailor` / `weaver`
- **function:** make cloth (weaver) / make garments (tailor) / sell cloth (draper)
- **aka:** clothier, draper, webster/webber
- **group:** Commerce (Part 6)
- **extends:** `townhouse` shell
- **genre/period:** real.

## 2. Essence
A **light-filled workshop around the loom (weaver) or cutting table (tailor)**, with cloth storage + a counter.
Defining quality: the work station + **strong natural light** (big windows) + a dry cloth store.

## 3. Threat model / failure modes
- **Light** — fine work needs big windows (the defining environmental requirement).
- **Damp / moth** — cloth store dry.
- **Theft** — cloth is valuable (secure store).

## 4. Access tiers / zoning
- **T0** shopfront — counter + cloth display.
- **T1** workshop — loom **or** cutting table, **at the windows**.
- Cloth store (dry, secure); dwelling above.

## 5. Required spaces (program)
| Space | Tier | Purpose | Required fixtures | Size |
|---|---|---|---|---|
| shopfront | T0 | sell | counter, cloth-bolt display | `to_ground` |
| workshop | T1 | weave / tailor | **loom** *(1.5–2.0 m tall, width ≤ weaver's arm span ~0.6–1.5 m)* + warp space; **or** a cutting table + tailor's bench; at a **window wall** | loom dims cited; room `to_ground` |
| cloth store | — | keep cloth | dry, secure shelving | dry |
| dwelling | — | home | dwelling fixtures | above |

## 6. Adjacency & circulation rules
1. The **workshop is on the best-lit side** (big / north windows).
2. *(Loom)* the loom needs **warp clearance** — the warp beam rolls ~20 yd of cloth, so a workshop long enough to work it.
3. The cloth store is **dry + secure**.
4. The shopfront opens to the street.

## 7. Construction & materials
- `townhouse` shell + **large windows** (more glazing than a typical house) for light; a dry store.
- WANTED: loom, cutting table, glazing, cloth bolts.

## 8. Signature / legibility
A **big-windowed workshop** (light); cloth bolts at the counter; a scissors/distaff/shuttle sign.

## 9. Status / period / setting scaling
- **Down:** a home weaver — one loom.
- **Mid:** a weaver's/tailor's shop.
- **Up:** a draper (cloth retail) / a clothier's workshop (multiple looms).

## 10. Function testers
- **F1** A loom (weaver) **or** a cutting table + bench (tailor) as the work station.
- **F2** **Strong natural light** at the work station (a window wall).
- **F3** *(loom)* warp clearance — a workshop long enough for the loom + working it.
- **F4** A dry, secure cloth store.
- **F5** A customer counter with cloth display.
- **F6** A trade sign.

## 11. Fixtures & assets needed (→ backlog)
Loom, cutting table, tailor's bench, cloth bolts/shelving, counter, large windows, sign. →
[`WantedAssetsBacklog.md`](../WantedAssetsBacklog.md).

## 12. Grounding ledger
| Claim | Provenance |
|---|---|
| horizontal treadle loom 1.5–2.0 m tall; width ≤ weaver's arm span (~0.6–1.5 m / 24–60"); warp rolls ~20 yd | CITED — [Model of a horizontal treadle loom (EXARC)](https://exarc.net/sites/default/files/exarc-eurorea_2_2005-model_of_a_horizontal_treadle_loom.pdf); [Cloth Widths in the Middle Ages](http://www.geocities.ws/ladymairghread/clothwidth.htm) |
| strong-light requirement for fine work | reasoned from the craft |
| room **sizes** | `to_ground` |

## 13. Open questions / unknowns
- Workshop length needed to work a ~20 yd warp — `to_ground` (loom + beating + sitting clearance).
- Vertical (warp-weighted) vs horizontal treadle loom by period/region — affects footprint.

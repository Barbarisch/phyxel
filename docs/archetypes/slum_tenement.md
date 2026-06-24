# Slum Tenement — Archetype Data Sheet

> Status: **DRAFT**. Schema: [`README.md`](README.md). **Intentional low tier — designed to fail quality checks.**

## 1. Identity
- **id:** `slum_tenement`
- **function:** maximal cheap shelter for the urban poor
- **aka:** tenement, "rents", back-plot hovel, cellar/garret dwelling
- **group:** Dwelling (Part 6) — the bottom of the wealth gradient
- **extends:** a subdivided `townhouse`/burgage, or improvised infill
- **genre/period:** real (medieval urban overcrowding), and a deliberate **squalor tier**.

## 2. Essence
The **opposite of the `manor`**: improvised, subdivided, overcrowded shelter where a whole household crams work
and sleep into a **single room**. Defining quality: maximal occupancy in minimal, decaying space — it should
**read as poverty**, and it deliberately **fails** the quality and many believability checks (that failure is
the point, CC7).

## 3. Threat model / failure modes *(mostly unmet — by design)*
- **Fire** — dense, no fire breaks, leaky thatch (the city-fire risk embodied).
- **Disease / sanitation** — shared or absent privies; foul (gaol-fever/plague conditions).
- **Collapse** — improvised, unmaintained, over-jettied.
These are modelled as **present hazards**, not fixed.

## 4. Access tiers / zoning
- **None.** Single rooms let separately; a **shared (over-used) yard, privy, and well**; cellars and garrets occupied.

## 5. Required spaces (program)
| Space | Purpose | Fixtures | Size |
|---|---|---|---|
| single multi-use room (per household) | cook + sleep + work | a hearth/brazier (or none — a smoke hole), pallets, a chest | cramped; `to_ground` (FLAG: occupancy/density) |
| shared / absent sanitation | — | an over-used privy + well, or none | shared |
| back-plot hovels / lean-tos | extra shelter | improvised | encroaching |

## 6. Adjacency & circulation rules
1. **Subdivision** — multiple households per plot/building (split burgages, let cellars + garrets).
2. **Encroachment** — lean-tos crowd the street/yard; no setbacks.
3. **Shared, over-used** privy/well; sanitation inadequate or absent.
4. No fire breaks between dwellings.

## 7. Construction & materials
- **Scavenged / patched** materials; **no proper foundation**; leaning, over-jettied beyond safe; failing
  wattle-&-daub; leaky thatch; often **no chimney** (smoke holes).

## 8. Signature / legibility
**Squalor** — patched leaning walls, encroaching lean-tos, refuse/middens, smoke holes, visible overcrowding.

## 9. Status / period / setting scaling
- **Subdivided burgage room** → **back-plot hovel/lean-to** → **cellar/garret let** → a **"rents" row** of one-room dwellings.
- **Fantasy:** a refugee warren, a "lower city" slum (BG3) — same squalor + a setting overlay.

## 10. Function testers *(note: this archetype should FAIL the quality tier — that is correct)*
- **F1** A single multi-use room per household (no room program).
- **F2** High occupancy / subdivision — multiple households per plot or building.
- **F3** Shared or absent sanitation — flagged as a **designed deficiency**, not a bug.
- **F4** Improvised/decayed construction + street encroachment.
- **F5** It **fails** the category-C quality tier and many believability checks **by design** — a slum that *passes* "quality" is wrong (CC7 honesty: this is a deliberate low tier, not a broken build).

## 11. Fixtures & assets needed (→ backlog)
Brazier/smoke-hole hearth, pallets, chest, lean-to framing, refuse/midden props; decay/condition styling. →
[`WantedAssetsBacklog.md`](../WantedAssetsBacklog.md).

## 12. Grounding ledger
| Claim | Provenance |
|---|---|
| subdivided burgage → multi-family tenement; cheap rental subdivision | CITED — [Tenement (Wikipedia)](https://en.wikipedia.org/wiki/Tenement); [Burgage (Wikipedia)](https://en.wikipedia.org/wiki/Burgage) |
| poor housing = single multi-use room (vs rich many-roomed) | CITED — [Housing in Medieval England (historymedieval.com)](https://historymedieval.com/housing-in-medieval-england-rich-and-poor-homes/) |
| occupancy / density figures | `to_ground` / FLAG — no clean source |
| "designed to fail quality" | DELIBERATE low tier (Part 6 note + category C/J + CC7) |

## 13. Open questions / unknowns
- Realistic occupancy density (households per plot) — `to_ground`.
- How the **condition/decay overlay** (J) interacts with this archetype — needs the style-overlay system (backlog §4).

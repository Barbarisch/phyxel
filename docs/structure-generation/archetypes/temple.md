# Temple / Church — Archetype Data Sheet

> Status: **DRAFT**. Schema: [`README.md`](README.md).

## 1. Identity
- **id:** `church` / `chapel` / `temple`
- **function:** communal worship
- **aka:** parish church, chapel, minster; **temple** (a pantheon faith)
- **group:** Faith (Part 6)
- **extends:** a long oriented hall (masonry for a major church, timber for a chapel)
- **genre/period:** the **church is real**; a polytheist **temple** is a fantasy/historical overlay grounded to the **world bible** (Part 9).

## 2. Essence
An **oriented processional space**: the laity progress down the **nave** toward the holy focus — the **altar in
the chancel** at the (east) end — with the sacred zone reserved for clergy. Defining: orientation + the
nave→chancel hierarchy + verticality and light drawing the eye to the altar.

## 3. Threat model / failure modes
- Not security — **reverence + legibility of the sacred**, acoustics, and **light** are the design drivers.
- **Fire** — candles + a timber roof.
- Secondarily a **refuge** in danger (sturdy walls, a tower).

## 4. Access tiers / zoning
- **T0** nave — laity, public.
- **T1** chancel / sanctuary — clergy only, beyond the **rood screen**.
- **T2** sacristy / vestry — secure (vessels, vestments).
- **Tower / belfry** — the bell.
- The **rood screen** is the public/sacred boundary.

## 5. Required spaces (program)
| Space | Tier | Purpose | Required fixtures | Size |
|---|---|---|---|---|
| nave | T0 | congregation | **font** near the entrance, pews/standing, aisles (arcade) | nave ≈ **2 bays wide**, bay-divided; proportions `to_ground` |
| chancel / sanctuary | T1 | the rite | **altar** (east end, raised, lit), lectern, sedilia, piscina | `to_ground` |
| sacristy / vestry | T2 | secure store | aumbry / chest | `to_ground` |
| tower / belfry | — | the bell | a bell | `to_ground` (reuse `tower_house` for structure) |
| porch | T0 | entrance | — | `to_ground` |

## 6. Adjacency & circulation rules
1. The long axis runs **east**; the **altar is at the east end**, raised and emphasized (a window/light above).
2. The main entrance is at the **west or south porch** — never behind the altar.
3. The **rood screen** separates nave (laity) from chancel (clergy).
4. The altar is the visual terminus of the axis.
5. The sacristy opens **off the chancel** and is secure.
6. The tower stands at the west end or over the crossing (cruciform).

## 7. Construction & materials
- Masonry (stone) for a major church; timber for a chapel.
- A **high nave + lower aisles** (arcade); a tower; large windows.
- WANTED: **stained-glass decals**, **marble** (altar), a bell, carved stone.

## 8. Signature / legibility
An **oriented long body with a tower/spire**, a big **east window**, and a porch; reads as sacred from the
silhouette; a bell.

## 9. Status / period / setting scaling
- **Low:** a wayside shrine — an altar + an icon.
- **Mid:** a chapel — nave + a small chancel.
- **High:** a parish church — nave + aisles + chancel + tower + porch.
- **Grand:** a cathedral *(compound)* — + transepts + crossing tower + crypt + cloister.
- **Fantasy temple:** the deity's iconography (symbols / sacred colours / orientation / cult statue) per the **world bible** (Part 9 / CC5).

## 10. Function testers
- **F1** A clear long axis with the altar/holy focus at one (east) end.
- **F2** The altar is raised and emphasized (light/window) at the axis end.
- **F3** A nave (laity) separated from the chancel (clergy) by a screen/step.
- **F4** A secure sacristy off the chancel.
- **F5** The entrance is at the opposite/side end (porch), not behind the altar.
- **F6** *(fantasy temple)* iconography matches the deity per the world bible.

## 11. Fixtures & assets needed (→ backlog)
Altar, font, rood screen, pews/benches, lectern, candles, bell, aumbry/chest, cult statue (temple),
**stained-glass windows (decal)**. → [`WantedAssetsBacklog.md`](../../WantedAssetsBacklog.md).

## 12. Grounding ledger
| Claim | Provenance |
|---|---|
| nave (laity) / chancel (clergy) / rood screen / crossing tower / nave ≈ 2 bays wide / bay division | CITED — [Nave (Wikipedia)](https://en.wikipedia.org/wiki/Nave); A. Hamilton Thompson, *The Ground Plan of the English Parish Church* (Gutenberg) |
| east orientation | REUSE-CANON — existing project canon (Part 3 / R) |
| high nave + low aisles + arcade (cruciform) | CITED — Nave (Wikipedia) |
| specific nave:chancel:tower proportions/dims | `to_ground` — strong regional variation |
| pantheon temple iconography | BIBLE-SOURCED (Part 9 / CC5) |

## 13. Open questions / unknowns
- Nave:chancel:tower proportional ratios — vary regionally; `to_ground` against a surveyed set.
- Aisled vs aisleless decision driver (status/span) — to define.

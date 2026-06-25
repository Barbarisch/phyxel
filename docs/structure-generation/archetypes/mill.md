# Mill (Water / Wind) — Archetype Data Sheet

> Status: **DRAFT**. Schema: [`README.md`](README.md).

## 1. Identity
- **id:** `mill`
- **function:** grind grain to meal (also fulling, sawing — same power principle)
- **aka:** gristmill, watermill, windmill, post-mill
- **group:** Civic & institutions (Part 6) *(often a lord's monopoly)*
- **extends:** a building wrapped around the grinding mechanism, sited at its power source
- **genre/period:** real (watermills ancient; post-mill windmills from ~12th c.).

## 2. Essence
A building **built around the grinding mechanism** — a water wheel or sails → gearing → **millstones** — sited
at its power source (a millrace or a windy rise). Defining quality: the power source + the gear train + the
stones + the grain→meal gravity flow.

## 3. Threat model / failure modes
- **Fire** — flour dust + friction (mills burned).
- **The power source** — water rights / a millrace, or wind on an open rise.
- **Wear** — the stones need periodic dressing.

## 4. Access / zoning — a **machine**, not a privacy gradient
Grain in at the top → **stone floor** (millstones) → **meal bins** below; the **wheel/gear pit** below the
stones; a **sack hoist** above. The miller's dwelling adjoins.

## 5. Required spaces (program)
| Space | Purpose | Required fixtures | Size |
|---|---|---|---|
| stone_floor | grind | **millstones** (fixed bed + driven runner ~120 rpm), a **hopper** feeding grain, meal bins below | `to_ground` |
| wheel/gear pit | power | *(water)* a water wheel ~10 rpm + **pit wheel → wallower → vertical main shaft**; *(wind)* the post-mill body's gearing | `to_ground` |
| sack hoist | lift grain | a hoist (lucam) to the top | upper |
| grain + meal stores | hold | bins/sacks | dry |
| power source | drive | *(water)* a **millrace/leat**; *(wind)* an open windy rise + the post | sited |

## 6. Adjacency & circulation rules
1. *(Water)* the mill sits **on the millrace/leat** — the wheel in the water.
2. *(Wind)* a **post-mill body rotates on its post** to face the wind, on an open rise.
3. The **gear train runs vertically** (wheel/pit below → stones → hoist above).
4. **Grain in at top, meal out below** (gravity flow).

## 7. Construction & materials
- Timber (post-mill) or stone/timber (watermill); a wheel pit; multi-level.
- WANTED: water wheel, sails, millstones, gearing, hopper.

## 8. Signature / legibility
*(Water)* a wheel on a leat beside a low building; *(wind)* a **post-mill** — a boxy body on a great post with
four sails, turning to the wind.

## 9. Status / period / setting scaling
- **Down:** a hand-quern (no building) → a small watermill.
- **Up:** a great lord's mill (a monopoly), a tide mill, a fulling/saw mill (same power).
- **Fantasy:** an arcane-powered mill (bible).

## 10. Function testers
- **F1** A power source correctly sited (a **millrace/leat** for water; an **open windy rise** for wind).
- **F2** A gear train from the wheel/sails to the millstones.
- **F3** Millstones (a fixed bed + a driven runner) with a hopper feed + meal bins.
- **F4** A sack hoist (multi-level grain→meal gravity flow).
- **F5** *(post-mill)* the body can rotate to face the wind.
- **F6** Fire awareness (flour dust).

## 11. Fixtures & assets needed (→ backlog)
Water wheel, sails, millstones, pit wheel/wallower/shaft, hopper, meal bins, sack hoist. →
[`WantedAssetsBacklog.md`](../../WantedAssetsBacklog.md).

## 12. Grounding ledger
| Claim | Provenance |
|---|---|
| watermill: vertical wheel → pit wheel → wallower → vertical shaft → stones; ~10 rpm wheel / ~120 rpm stones; fixed bed + driven runner | CITED — [Watermill (Wikipedia)](https://en.wikipedia.org/wiki/Watermill); [Gristmill (Wikipedia)](https://en.wikipedia.org/wiki/Gristmills) |
| post-mill body rotates on a post to face the wind | CITED — [Medieval windmill technology (Brewminate)](https://brewminate.com/medieval-and-early-modern-windmill-architecture-and-technology/); [Historic England: Mills](https://historicengland.org.uk/images-books/publications/iha-mills/heag212-mills/) |
| building **sizes** | `to_ground` |

## 13. Open questions / unknowns
- Mill building footprint + wheel diameter by type — `to_ground`.
- Tower-mill (rotating cap) vs post-mill (rotating body) by period — affects the structure.

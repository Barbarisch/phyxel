# Blacksmith / Smithy — Archetype Data Sheet

> Status: **PARTIALLY GROUNDED** (program + footprint SCALE cited to the excavated Jamestown smithy;
> work height grounded; anvil/bellows dims are disclosed functional/anachronism proxies; **open
> NEEDS-RESEARCH: period bellows dims**). Schema: [`README.md`](README.md). First non-residential
> workshop typology wired to the engine (after `tavern`) — see `ValidationLedger.md`.
>
> **Grounding-auditor correction (2026-06-28):** an earlier draft mis-cited the Anderson Blacksmith
> Shop (Colonial Williamsburg) for the footprint — the "under 30 ft / 16×20 ft kitchen" text was **not
> on the cited page** (a WebSearch misattribution). Corrected to the **excavated Jamestown smithy
> (16×20 ft = 4.9×6.1 m)**. Anvil length corrected 0.55→0.60 m (right product). Bellows dims downgraded
> to INFERRED (open source gives 4 ft, not 5 ft).

## 1. Identity
- **id:** `blacksmith` / `smithy` / `forge`
- **function:** working iron/steel — tools, fittings, arms, horseshoeing
- **aka:** smithy, forge, farrier (shoeing)
- **group:** Commerce (Part 6)
- **extends:** a workshop + a customer-facing storefront/yard; dwelling above/behind (mixed-use)
- **genre/period:** real. *(A water-powered trip-hammer / scaled foundry is later — see the `foundry` archetype.)*

## 2. Essence
A **fire-managed workshop built around the forge → anvil → quench work triangle**, vented and fire-safe, with a
customer edge. Defining quality: the forge relationship *and* fire safety — not "a shop with an anvil."

## 3. Threat model / failure modes
- **Fire** (primary) — open forge + fuel + flying sparks; the defining hazard.
- **Heat / smoke** — needs venting (a hood/chimney).
- **Theft** — tools and stock iron.
- The forge must **never** sit under thatch or against combustibles.

## 4. Access tiers / zoning
- **T0** storefront / yard — customers, finished goods, the shoeing area.
- **T1** the forge floor — smith only: forge, anvil, bellows, quench, workbench.
- **Storage** — fuel (charcoal/coal), stock iron, tools.
- **Dwelling** — above/behind (mixed-use), **fire-separated**.

## 5. Required spaces (program)
| Space | Tier | Purpose | Required fixtures | Size |
|---|---|---|---|---|
| forge_floor | T1 | forging | **forge/hearth on the back wall** + chimney/hood, **anvil** (~0.80 m work height) within a step, **quench trough**, bellows, tool rack, workbench | ~1 timber bay (~4 m × ~5.5 m); forge workspace at the excavated Jamestown smithy scale (16×20 ft = 4.9×6.1 m) |
| fuel + stock store | — | feed the fire / stock | fuel bin, stock-iron rack | `to_ground` |
| storefront / yard | T0 | sell, shoe | finished-goods rack, shoeing area | `to_ground` |
| dwelling | — | smith's home | dwelling fixtures | `to_ground` (mixed-use) |

## 6. Adjacency & circulation rules
1. The **forge sits on a back/exterior wall**, vented by a chimney/hood.
2. The **anvil is within a step of the forge**; the **quench trough is beside the anvil** (the work triangle).
3. Combustibles are kept **above the fire level or out of the building**.
4. The storefront/yard opens to the street.
5. The dwelling is **fire-separated** from the forge.

## 7. Construction & materials
- **Non-combustible around the forge** — a stone hearth + chimney; **not** timber/thatch over the fire.
- Floor of beaten earth or stone; the rest of the shell may be timber.
- Often **detached or on a corner** (fire risk).
- WANTED: forge, anvil, bellows, quench trough, tool rack assets.

## 8. Signature / legibility
A **chimney with smoke**; the **forge glow** through an open front; an anvil; a **horseshoe/hammer pictorial
sign**; usually set slightly apart from neighbours.

## 9. Status / period / setting scaling
- **Low:** a village smithy — one forge floor + a lean-to.
- **Mid:** a town smithy — forge floor + storefront + dwelling above.
- **High:** an armourer's / large workshop — multiple hearths, more benches.
- **Fantasy:** an enchanting forge — a rune-anvil, an everburning forge (→ world bible / `glow`).

## 10. Function testers
- **F1** A forge/hearth on an exterior/back wall, vented by a chimney/hood.
- **F2** An anvil within a step of the forge (~0.80 m working height).
- **F3** A quench source adjacent to the anvil.
- **F4** **No combustible (thatch/timber) directly over or against the forge fire** — a fire-safe envelope.
- **F5** A customer-facing storefront/yard.
- **F6** Fuel + stock storage present.
- **F7** *(if dwelling)* the living space is fire-separated from the forge.

## 11. Fixtures & assets needed (→ backlog)
Forge/hearth, anvil, bellows, quench trough, tool rack, workbench, fuel bin, stock-iron rack, finished-goods
rack, trade sign decal. → [`WantedAssetsBacklog.md`](../../WantedAssetsBacklog.md).

## 12. Grounding ledger
| Claim | Provenance |
|---|---|
| anvil/hearth working height ~0.80 m (31.5") — knuckle height | CITED — [Planning the forge (Beautiful Iron)](https://beautifuliron.com/forge_planning.htm) *(a modern craft guide; the height is anthropometric/invariant, not a period survey)* |
| forge on the back wall, vented; combustibles above/out; non-combustible structure (firebrick/fireclay hearth + tuyere) | CITED — same; [Forge (Wikipedia)](https://en.wikipedia.org/wiki/Forge) |
| smithy **workspace footprint ~4.9 × 6.1 m (16 × 20 ft)** — an EXCAVATED smithy | CITED — [Blacksmith Shop/Bakery, Historic Jamestowne](https://historicjamestowne.org/archaeology/map-of-discoveries/blacksmith-shopbakery/) ("rectangular, 16 feet by 20 feet"; James Fort metalworking structure c.1608, later a bakery). *Grounds the forge-WORKSPACE scale; it was a single forge cellar, not a 2-room shop.* |
| ~~Anderson Blacksmith Shop "under 30 ft" / 16×20 ft kitchen~~ | **RETRACTED** — grounding-auditor confirmed the text is NOT on the cited beautifuliron page; RR1690 is a tinsmithing report. Do not cite. (CW Archaeological Reports RR1227/RR1230 for Building 22 would be the right docs but are access-restricted.) |
| **DESIGN DECISION: 2-bay open-fronted village smithy (~8 m × ~5.5 m), forge floor + storefront/yard** | bay 1 (forge floor) ≈ the Jamestown workspace scale; bay 2 (storefront) ADDED by analogy to the grounded ~4 m timber-frame bay (croft/longhouse/hall_house, Brunskill 1985 caveat). Same honest-analogy basis as the `tavern` bay frame; storefront is the disclosed addition. |
| anvil overall **~0.60 m (23.5–23.75") long × ~0.25 m (10") base, ~0.10 m (4") face**, 100–120 lb working anvil; mounted to ~0.80 m | CITED — [Centaur Emerson 100 lb](https://www.centaurforge.com/100-lbs-Emerson-Traditional-Anvil-w_-Turning-Cams/productinfo/EMERSON100/) (23.75") / [Centaur JHM Legend 120 lb](https://www.centaurforge.com/120-lbs-JHM-Legend-Anvil/productinfo/AB120LEGEND/) (23.5", 10" base). **ANACHRONISM (disclosed):** London-pattern anvil is ~16–17th c; the medieval anvil was a simpler block — modern dims used as a form proxy. |
| forge **firepot opening ~0.23 × 0.33 m (9 × 13", two axes of one oblong pot) × ~0.11 m (4.5") deep**; shop hearth pan 0.61 × 0.76 m (24 × 30") | CITED — [Shady Grove firepot](https://blksmth.com/mild-steel-firepot/) / [Centaur 24" coal forge](https://www.centaurforge.com/24-Wide-Shop-Coal-Forge-Dumping-Ashgate/productinfo/SVD/). *(forge_hearth canon width 1.0 m = pan + hood framing, DESIGN — framing extent not separately sourced.)* |
| great double-lung **bellows ~1.5 m long** | **INFERRED, not cited** — open source ([Persimmon Forge](http://persimmonforge.blogspot.com/2013/01/making-great-double-lung-blacksmith.html)) gives the author's own at **4 ft (1.22 m)**, "up to ~6 ft"; 1.5 m is the mid of that 4–6 ft range. Width/loft are functional estimates. **NEEDS-RESEARCH:** Smith, *The Blacksmith's Craft* (1956); Horne, *The Artist-Blacksmith* (2002). |
| **quench / slack tub = a water-filled barrel** | CITED — [Forge (Wikipedia)](https://en.wikipedia.org/wiki/Forge) ("usually a large container", whiskey barrel) → **REUSE the grounded `barrel` canon** (no new dim) |
| forge as a function fixture | also in Part 3 (smithy program) |
| water-powered trip-hammer | GENRE-FLAG — later/industrial (see `foundry`) |

## 13. Open questions / unknowns
- Smithy **floor area** — grounded to the EXCAVATED Jamestown smithy (16×20 ft = 4.9×6.1 m) for the
  forge workspace; the 2-bay (forge + storefront) frame is the disclosed design decision. A *directly
  surveyed MEDIEVAL English* smithy footprint (Jamestown is early-colonial 1608; Wharram Percy /
  Portmahomack left only superficial footprints) would sharpen the period fit — soft refinement, not a
  blocker.
- **Bellows dimensions** — NEEDS-RESEARCH: open craft sources give 4 ft (1.22 m); the shipped 1.5 m is
  INFERRED within the attested 4–6 ft range. Consult Smith, *The Blacksmith's Craft* (1956) / Horne,
  *The Artist-Blacksmith* (2002).
- **Tool-rack** exact size — `to_ground`; built as a wall-mounted rack at ~1.4–1.7 m mount height
  (analogy to the cited sconce/wall-mount precedent).
- **Workbench** — REUSE the existing `counter`/`bench` canon at ~0.9 m working height (no new asset).
- Charcoal vs coal vs coke by period/region — affects fuel store + the genre flag (charcoal = the
  conservative medieval default; coal/coke flagged later/industrial).

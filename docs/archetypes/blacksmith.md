# Blacksmith / Smithy — Archetype Data Sheet

> Status: **DRAFT**. Schema: [`README.md`](README.md).

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
| forge_floor | T1 | forging | **forge/hearth on the back wall** + chimney/hood, **anvil** (~0.80 m work height) within a step, **quench trough**, bellows, tool rack, workbench | `to_ground` (small hobby ref ~3.7 m sq — FLAG modern) |
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
rack, trade sign decal. → [`WantedAssetsBacklog.md`](../WantedAssetsBacklog.md).

## 12. Grounding ledger
| Claim | Provenance |
|---|---|
| anvil/hearth working height ~0.80 m (31.5") — knuckle height | CITED — [Planning the forge (Beautiful Iron)](https://beautifuliron.com/forge_planning.htm) |
| forge on the back wall, vented; combustibles above/out; non-combustible structure | CITED — same; [Forge (Wikipedia)](https://en.wikipedia.org/wiki/Forge) |
| smithy floor ~3.7 m sq (12 ft) | FLAG — modern hobby-shop reference, not medieval |
| medieval smithy floor area | `to_ground` |
| forge as a function fixture | also in Part 3 (smithy program) |
| water-powered trip-hammer | GENRE-FLAG — later/industrial (see `foundry`) |

## 13. Open questions / unknowns
- Medieval smithy **floor area** — `to_ground` (find a surveyed example).
- Charcoal vs coal vs coke by period/region — affects fuel store + the genre flag.

# Foundry / Manufactory — Archetype Data Sheet

> Status: **DRAFT**. Schema: [`README.md`](README.md). **GENRE-FLAG: early-modern/industrial (+ overtly fantastical in BG3).**

## 1. Identity
- **id:** `foundry` / `manufactory`
- **function:** smelt + cast metal at scale (fantasy: manufacture constructs)
- **aka:** ironworks, casting house, manufactory
- **group:** Industry (Part 6)
- **extends:** a `blacksmith` scaled up to an industrial furnace hall
- **genre/period:** **early-modern / industrial** (the blast furnace is ~15th c.+); the BG3 Steel Watch Foundry is overtly fantastical. Strong genre flag (F0).

## 2. Essence
A **smithy scaled to an industrial process** — a furnace/blast furnace + a casting floor + bellows/water-power +
material yards + a great chimney. Loud, hot, smoky, fire-managed. Defining quality: the furnace at scale + the
casting floor + material flow + heavy fire/venting.

## 3. Threat model / failure modes
- **Fire + extreme heat** — molten metal (the dominant hazard).
- **Smoke/fumes** — venting via a tall chimney.
- **The power source** — great bellows / a water wheel for the blast.
- **Material handling** — ore, fuel, sand.

## 4. Access tiers / zoning
- **T0** yards — ore, fuel, finished castings (carts).
- **T1** the furnace/casting hall — furnace, casting floor, molds, ladles, the blast.
- The pattern/mold shop; (water) the wheel/blast plant.

## 5. Required spaces (program)
| Space | Purpose | Required fixtures | Size |
|---|---|---|---|
| furnace_hall | smelt + cast | a **furnace/blast furnace**, a **casting floor**, sand molds, ladles, a **great chimney/flue** | `to_ground` |
| blast plant | drive the furnace | great **bellows**, or a **water wheel** | `to_ground` |
| material yards | feed it | ore, charcoal/coke, sand bins | cart access |
| pattern/mold shop | make molds | benches, patterns | `to_ground` |
| finished-goods yard | ship castings | cart access | — |

## 6. Adjacency & circulation rules
1. The **furnace is vented by a tall chimney**; the **casting floor beside it** (tapping).
2. The **blast** (bellows / water wheel) feeds the furnace.
3. **Material yards adjacent** (cart access).
4. **Non-combustible throughout** (molten metal).
5. Sited at the **edge** (noise/smoke/fire); (water-blast) on a **stream**.

## 7. Construction & materials
- **Non-combustible** (stone/brick furnace + hall); a **tall chimney/flue**; a **strong floor** (heavy castings); (water) a wheel + leat.
- WANTED: furnace, casting molds, ladles, great bellows, chimney.

## 8. Signature / legibility
A **great smoking chimney + furnace glow + an industrial mass**; clamour; (fantasy) glowing arcane furnaces,
half-built constructs.

## 9. Status / period / setting scaling
- **Down:** a `blacksmith` (one forge).
- **Up:** a foundry/casting house → an industrial ironworks.
- **Fantasy:** a magical construct-foundry (BG3 Steel Watch — arcane furnaces, automaton assembly; bible overlay).

## 10. Function testers
- **F0 (genre):** early-modern/industrial (+ fantastical in BG3) — not medieval.
- **F1** A furnace/blast furnace vented by a tall chimney.
- **F2** A casting floor beside the furnace (molds/ladles).
- **F3** A blast source (great bellows or a water wheel) feeding the furnace.
- **F4** A **non-combustible** envelope + a strong floor (molten metal + heavy castings).
- **F5** Material yards (ore/fuel/sand) with cart access.
- **F6** Sited at the edge (noise/smoke/fire); water-blast on a stream.
- **F7** *(fantasy)* the construct-assembly/arcane overlay is bible-sourced (CC); impossibilities flagged (CC8).

## 11. Fixtures & assets needed (→ backlog)
Furnace, casting molds, ladles, great bellows, water wheel (blast), chimney, material bins. →
[`WantedAssetsBacklog.md`](../WantedAssetsBacklog.md).

## 12. Grounding ledger
| Claim | Provenance |
|---|---|
| foundry/blast-furnace at scale | GENRE-FLAG — early-modern/industrial (blast furnace ~15th c.+) |
| furnace→casting→blast workflow; non-combustible + venting | REUSE-CANON — `blacksmith` fire-safety (cited), scaled |
| water-blast wheel | REUSE-CANON — `mill` |
| fantasy construct-foundry | BIBLE-SOURCED (Part 9) |
| hall **sizes** | `to_ground` / NEEDS-RESEARCH |

## 13. Open questions / unknowns
- Bloomery vs blast furnace by period (the blast furnace dates the building) — affects the genre flag.
- Foundry hall size — NEEDS-RESEARCH.

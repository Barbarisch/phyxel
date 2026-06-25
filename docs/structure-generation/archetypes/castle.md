# Castle — Archetype Data Sheet

> Status: **DRAFT**. Schema: [`README.md`](README.md). **Compound** — composes curtain + towers + gatehouse + [`keep`](keep.md) + bailey buildings (`compose_compound`, #46). Reuses fortification placer #31 + checklist Q.

## 1. Identity
- **id:** `castle`
- **function:** a fortified stronghold + lordly/royal residence + administrative centre
- **aka:** stronghold, fortress
- **group:** Power / fortified (Part 6) — **compound**
- **extends:** an enclosure of curtain + towers + gatehouse + keep + a bailey of buildings
- **genre/period:** real.

## 2. Essence
A **fortified enclosure** (curtain wall + towers + gatehouse) containing one or more **baileys** of buildings
(great hall, chapel, lodgings, stables, kitchen, smithy) and a **keep** as the last refuge, often with a
moat/ditch and a dungeon below. Defining quality: **layered defence** + a self-contained lordly settlement within.

## 3. Threat model / failure modes
- **Siege + assault** (the whole point) → a moat/ditch, a curtain wall (**2–6 m**, grounded) with flanking
  towers, a gatehouse/barbican choke point, murder holes / portcullis / drawbridge, the keep as the last
  refuge, arrow loops + battlements; + a garrison self-sufficient for a siege (**well + stores**).

## 4. Access tiers / zoning (= layered defence)
outside the moat → the **barbican** → the **gatehouse** (drawbridge, portcullis, murder holes) → the **outer
bailey** (stables, workshops, garrison) → the **inner bailey** (great hall, chapel, lord's lodging, kitchen) →
the **keep** (last refuge) → the **dungeon** below (Part 8). Each layer a controlled gate.

## 5. Required spaces (program — a compound of archetypes)
| Element | Composes | Notes |
|---|---|---|
| curtain wall + mural towers | placer #31 | the enceinte; towers flank/cover it |
| gatehouse + barbican | #31 | the single controlled entrance (drawbridge/portcullis/murder holes) |
| moat / ditch | — | outermost (moat = water, partly engine-blocked) |
| outer bailey | `stable`, `blacksmith`, barracks, workshops, **well** | service/garrison |
| inner bailey | `manor_hall` (great hall), chapel, lord's lodging (`manor`), kitchen, brew/bakehouse | the lord's seat |
| keep | [`keep`](keep.md) | the last refuge |
| dungeon / oubliette | `gaol` + Part 8 | below the keep/gatehouse |

## 6. Adjacency & circulation rules (layered)
1. **Moat/ditch outermost** → **barbican** → **gatehouse** (the single controlled entrance).
2. → **outer bailey** (service/garrison) → a further gate → **inner bailey** (hall + chapel + lodging) → the **keep** (innermost, highest).
3. A **well inside** (siege water).
4. **Towers flank the curtain at intervals** (covering fire).
5. The **dungeon under the keep/gatehouse** (Part 8).
6. Sited for defence (a spur / motte / cliff / water).

## 7. Construction & materials
- Stone **curtain (2–6 m, grounded)** + towers + a gatehouse + a keep; a **moat** (water — partly engine-blocked) or a dry ditch; battlements/crenellations + arrow loops throughout.
- The bailey buildings are the relevant archetypes. Reuse #31 + `keep` + `manor_hall` + `gaol`.

## 8. Signature / legibility
A **walled enclosure of towers + a gatehouse** crowning a defensible site, a **keep rising within**, a moat/ditch
around; battlements everywhere; banners.

## 9. Status / period / setting scaling
- **Down:** a **motte-and-bailey** (a wooden tower on a mound + a palisaded bailey).
- **Mid:** a stone **enclosure castle** (curtain + keep).
- **Up:** a **concentric castle** (two rings of walls) → a great royal fortress.
- **Fantasy:** a dark fortress (bible — wards, a monstrous garrison).

## 10. Function testers
- **F1** A continuous defensible **enceinte** (curtain 2–6 m + flanking towers covering it).
- **F2** A **single controlled entrance** — a gatehouse (+ barbican, drawbridge, portcullis, murder holes).
- **F3** A **layered approach** (moat → gate → outer bailey → inner bailey → keep), each a controlled gate.
- **F4** A **keep / strong point** as the last refuge (the `keep` testers).
- **F5** A **well + stores** inside (withstand a siege).
- **F6** The bailey contains a **functioning lordly settlement** (hall, chapel, lodging, kitchen, stable, smithy — the relevant archetypes).
- **F7** Battlements + arrow loops throughout.
- **F8** Sited for defence.
- **F9** *(compound)* it **composes** its sub-buildings + walls (`compose_compound`, #46) — not one mega-room.

## 11. Fixtures & assets needed (→ backlog)
Portcullis, drawbridge, murder holes, arrow loops, battlements, the bailey buildings' fixtures, well, banners. →
[`WantedAssetsBacklog.md`](../../WantedAssetsBacklog.md).

## 12. Grounding ledger
| Claim | Provenance |
|---|---|
| curtain wall 2–6 m | REUSE-CANON — Part 6 / CLAUDE.md (cited) |
| keep | REUSE-CANON — [`keep`](keep.md) (Dover/Pembroke, cited) |
| gatehouse / barbican / moat / concentric; motte-and-bailey → stone → concentric | fortification norm (placer #31, checklist Q) |
| bailey **footprint** | `to_ground` |
| moat (water) | partly **ENGINE-BLOCKED** (water feature) |

## 13. Open questions / unknowns
- Motte-and-bailey vs enclosure vs concentric by period — encode as variants.
- Bailey footprint — `to_ground`.
- How much the moat needs the water engine feature vs a dry ditch.

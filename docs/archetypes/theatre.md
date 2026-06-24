# Theatre / Playhouse — Archetype Data Sheet

> Status: **DRAFT**. Schema: [`README.md`](README.md). **GENRE-FLAG: early-modern, not medieval.**

## 1. Identity
- **id:** `theatre`
- **function:** stage plays for a paying audience
- **aka:** playhouse, amphitheatre
- **group:** Entertainment & vice (Part 6)
- **extends:** a purpose-built polygonal timber frame
- **genre/period:** **early-modern (Elizabethan, 1576+)** — **no permanent medieval playhouses**; medieval performance used inn-yards, guild pageant wagons, and churches. Strong genre flag (F0).

## 2. Essence
An **open-air polygonal amphitheatre** — a thrust stage backed by a tiring house, ringed by a standing **yard**
and tiered **galleries**. Defining quality: the stage→yard→galleries audience geometry + the tiring house +
crowd flow for thousands.

## 3. Threat model / failure modes
- **Crowd management** — up to ~3,000: multiple entries/exits, the yard.
- **Sightlines** — a 3-sided thrust stage.
- **Weather** — open over the yard; roofs only over stage + galleries.
- **Fire** — thatched galleries (the first Globe burned, 1613).

## 4. Access tiers / zoning
- **T0** yard — groundlings (cheap, standing).
- **T1** galleries — 3 tiers of paid seating.
- **T2** lords' rooms — premium, above/behind the stage.
- Backstage **tiring house** (actors only); **"the heavens"** (effects loft).

## 5. Required spaces (program)
| Space | Tier | Purpose | Required fixtures | Size |
|---|---|---|---|---|
| stage | — | perform | a raised thrust stage, the "heavens" canopy | **~13 × 8 m (43 × 27 ft), raised ~1.5 m** |
| yard | T0 | standing audience | open ground around 3 sides | ~30 m (100 ft) polygon, ~20 sides |
| galleries | T1 | seated audience | 3 tiers of seating ringing the yard | 3 stories |
| lords' rooms | T2 | premium | seating above the stage | — |
| tiring house | — | backstage | dressing, props, actors' entrances | behind the stage |
| heavens | — | effects | an effects loft over the stage | over stage |

## 6. Adjacency & circulation rules
1. The **stage thrusts into the yard**; the yard wraps **3 sides**.
2. **Galleries ring the yard** (sightlines to the stage).
3. The **tiring house is behind the stage** (actors' entrances + dressing); lords' rooms above it; the heavens over the stage.
4. **Multiple paid entrances** feed the yard + galleries (crowd flow for thousands).
5. **Open to the sky over the yard**.

## 7. Construction & materials
- A timber polygonal frame, 3 stories; galleries roofed (thatch originally — **fire**); the stage roofed ("the heavens"); the yard open.
- WANTED: stage/gallery structures, bench seating.

## 8. Signature / legibility
A tall **polygonal open-roofed drum**; a flag flown on performance days; a thrust stage under a painted
"heavens"; tiered galleries.

## 9. Status / period / setting scaling
- **Medieval alternative:** an inn-yard performance / a pageant wagon (no building) — use this for a strict-medieval brief.
- **Up:** a purpose-built playhouse (Globe/Fortune) → an indoor hall playhouse (Blackfriars).
- **Fantasy:** a bardic amphitheatre.

## 10. Function testers
- **F0 (genre):** this is an **early-modern** building — for a medieval brief, substitute inn-yard / pageant-wagon performance (flag).
- **F1** A raised thrust stage (~13 × 8 m, ~1.5 m high) backed by a tiring house.
- **F2** A standing yard around 3 sides of the stage.
- **F3** Tiered galleries ringing the yard (sightlines).
- **F4** A backstage tiring house with actors' entrances.
- **F5** Multiple paid entrances sized for crowd flow (thousands).
- **F6** Weather/fire honesty — open yard + roofed stage/galleries (thatch = a flagged fire risk).

## 11. Fixtures & assets needed (→ backlog)
Stage, tiring-house structure, gallery bench seating, the heavens canopy, flag. →
[`WantedAssetsBacklog.md`](../WantedAssetsBacklog.md).

## 12. Grounding ledger
| Claim | Provenance |
|---|---|
| Globe ~100 ft wide, ~20 sides, 3 stories; stage ~43 × 27 ft (13 × 8 m) raised ~5 ft (1.5 m); yard + 3 galleries; tiring house + lords' rooms + heavens; ~3,000 capacity | CITED — [Globe Theatre design (Britannica)](https://www.britannica.com/topic/Globe-Theatre/The-design-of-the-Globe); [Globe Theatre (Wikipedia)](https://en.wikipedia.org/wiki/Globe_Theatre) |
| no permanent medieval playhouses (inn-yards/pageants instead) | GENRE-FLAG — early-modern |

## 13. Open questions / unknowns
- Model the **medieval inn-yard / pageant-wagon** performance as its own mini-archetype? (recommended for a medieval brief).

# Monastery — Archetype Data Sheet

> Status: **DRAFT**. Schema: [`README.md`](README.md). **Compound** — organized by the cloister.

## 1. Identity
- **id:** `monastery`
- **function:** a religious community's self-contained home — worship, work, study
- **aka:** abbey, priory, friary, convent
- **group:** Faith (Part 6) — **compound**
- **extends:** the **claustral plan** (St Gall)
- **genre/period:** real (the Plan of St Gall, c.820, is the canonical model).

## 2. Essence
A self-sufficient community organized around the **cloister** — the church on one side, the canonical ranges
(chapter house + dorter, refectory, cellarer's range) on the other three — plus an **outer court** of workshops
and gardens. Defining quality: the claustral square as the hub, with the monks' day moving around it.

## 3. Threat model / failure modes
- **Enclosure / contemplation** — separation from the world (a precinct wall + a gatehouse).
- **Self-sufficiency** — food, water, work all on-site.
- **The Rule** — the daily round of offices drives the plan (e.g. the night stair).
- Some **defensibility**.

## 4. Access tiers / zoning
- **T0** outer court / gatehouse — where the world meets the monastery (almonry, guesthouse, stables, workshops).
- **T1** church + cloister — the enclosed community.
- **T2** inner claustral ranges — dorter, refectory, chapter house (monks only).
- Infirmary, abbot's lodging, cemetery.

## 5. Required spaces (program — St Gall)
| Space | Side of cloister | Purpose | Notes |
|---|---|---|---|
| church | **north** | worship | E–W axis |
| cloister | centre | the hub | a square arcaded walk |
| chapter house | **east range** | daily chapter | dorter above |
| dorter (dormitory) | **east, upper** | sleep | a **night stair** to the church |
| refectory | **south range** | dine | opposite the church; kitchen adjoining |
| warming room (calefactory) | east | the one heated room | — |
| cellarer's range | **west range** | wine/beer cellar + larder | deals with the outside |
| infirmary | east, set apart | the sick | quiet |
| outer court | beyond | guesthouse, almonry, stables, workshops, barns | the world |
| gardens / orchards | outermost | food | self-sufficiency |

## 6. Adjacency & circulation rules
1. **Church on the north of the cloister** (E–W axis); the **square cloister to the south**.
2. **East range:** chapter house + (above) the **dorter, with a night stair down to the church**.
3. **South range:** the **refectory (opposite the church)** + kitchen + warming room.
4. **West range:** the **cellarer's** cellar + larder (the outward-facing officer).
5. **Infirmary east**, quiet and set apart.
6. **Outer court** (gatehouse + guesthouse + almonry + workshops + stables) separates the world from the enclosure.
7. A **precinct wall encloses all**.

## 7. Construction & materials
- Stone church + 2-story claustral ranges; a vaulted/arcaded **cloister walk**; a **precinct wall + gatehouse**.
- Self-sufficient infrastructure (mill, brewhouse, bakehouse, fishpond, gardens).
- WANTED: cloister arcade, choir stalls, refectory tables, etc.

## 8. Signature / legibility
A great church beside a **square cloister ringed by 2-story ranges**, within a **walled precinct with a
gatehouse**; bells; gardens.

## 9. Status / period / setting scaling
- **Down:** a small priory/friary (church + a modest cloister).
- **Up:** an abbey (full claustral + outer court + granges).
- **Variants:** Benedictine (self-contained, St Gall) · Cistercian (remote, plain, lay-brother ranges) · friary (urban, preaching).
- **Fantasy:** a monastic order of a deity (pantheon — bible).

## 10. Function testers
- **F1** A church on one side of a **square cloister**, the canonical ranges on the other three (chapter house/dorter east, refectory south, cellarer west).
- **F2** A **night stair** from the dorter to the church.
- **F3** The **refectory opposite the church**, with a kitchen.
- **F4** A **chapter house** off the cloister (east).
- **F5** An **infirmary**, quiet and set apart.
- **F6** An **outer court** (gatehouse + guesthouse + almonry + workshops) separating the world from the enclosure.
- **F7** A **precinct wall** enclosing all.
- **F8** Self-sufficiency (water + food production — mill/bakehouse/garden).

## 11. Fixtures & assets needed (→ backlog)
Cloister arcade, choir stalls, chapter-house seating, refectory tables + reading pulpit, dorter beds, kitchen,
gatehouse. → [`WantedAssetsBacklog.md`](../../WantedAssetsBacklog.md).

## 12. Grounding ledger
| Claim | Provenance |
|---|---|
| claustral layout (church N + cloister S; chapter house + dorter E w/ night stair; refectory + warming room S; cellar/larder W; outer court workshops/stables; gardens/orchards; precinct wall); 40+ buildings | CITED — [Plan of Saint Gall (Wikipedia)](https://en.wikipedia.org/wiki/Plan_of_Saint_Gall); [St Gall Plan & medieval monasteries (Medievalists)](https://www.medievalists.net/2022/01/st-gall-plan-medieval-monasteries/) |
| cloister square + range **sizes** | `to_ground` |

## 13. Open questions / unknowns
- Benedictine vs Cistercian vs friary plan differences — encode as variants.
- Cloister square dimension — `to_ground`.

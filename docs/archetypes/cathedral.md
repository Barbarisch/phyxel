# Cathedral — Archetype Data Sheet

> Status: **DRAFT**. Schema: [`README.md`](README.md). **Compound.** Extends [`temple`](temple.md); pulls in [`monastery`](monastery.md) claustral adjuncts + Part 8 crypt.

## 1. Identity
- **id:** `cathedral`
- **function:** the seat of a bishop — a great church + diocesan worship/administration
- **aka:** minster, duomo, great church
- **group:** Faith (Part 6) — **compound**
- **extends:** `temple` (a great church) + monastic-style adjuncts (chapter house, cloister)
- **genre/period:** real.

## 2. Essence
A **monumental cruciform church** — the `temple` sheet at maximum scale — with a **crypt**, a **chapter house**,
and a **cloister**, the grandest expression of the faith. Defining quality: the oriented nave→chancel hierarchy
at cathedral scale + the bishop's seat (**cathedra**) + the claustral adjuncts.

## 3. Threat model / failure modes
- **Reverence + awe** (verticality, light, acoustics) — the design driver.
- **Civic/diocesan pride.**
- **Fire** — the great timber roofs burned.
- A **refuge/sanctuary** in danger.

## 4. Access tiers / zoning
- **T0** nave — laity (vast).
- **T1** choir/chancel — clergy, beyond the screen + the **cathedra**.
- **T2** sacristy/treasury — secure (vessels, relics).
- Crypt (below, relics/tombs); chapter house + cloister (clergy); tower/spire (bells).

## 5. Required spaces (program)
| Space | Tier | Purpose | Required fixtures | Size |
|---|---|---|---|---|
| nave | T0 | laity | aisles, clerestory, font | very long; `to_ground` |
| transepts | — | the cross-arms | (crossing tower over) | at the crossing |
| choir / chancel | T1 | clergy + bishop | **high altar (east)**, choir stalls, **cathedra** | east |
| ambulatory + chapels | — | processions/relics | radiating chapels | wraps the choir |
| crypt | — | relics/tombs | (reuse Part 8) | below the choir |
| sacristy / treasury | T2 | secure relics/plate | aumbry, secure | secure |
| chapter house | — | clergy chapter | seating | off the cloister |
| cloister + range | — | the canons | arcade walk | (south) side |

## 6. Adjacency & circulation rules
1. Long **E–W axis**; **altar + cathedra east**; **transepts at the crossing** (crossing tower/spire).
2. **Nave (laity) west of the screen, choir (clergy) east.**
3. An **ambulatory wraps the choir** with radiating chapels.
4. The **crypt is below the choir** (relics).
5. A **chapter house + cloister** off the (south) side.
6. A **west front** = the grand entrance + towers.

## 7. Construction & materials
- Stone, vaulted (or a timber roof); flying buttresses (Gothic); **huge stained-glass** (clerestory + rose);
  towers/spire.
- WANTED: marble, **stained glass (a major driver)**, carved stone, bells.

## 8. Signature / legibility
A towering cruciform stone mass — a **crossing tower/spire**, a **west front with towers + a rose window**,
flying buttresses; it dominates the skyline.

## 9. Status / period / setting scaling
- **Down:** a parish church (`temple`) → a minster.
- **Up:** a full cathedral (transepts + crypt + chapter house + cloister + towers).
- **Fantasy:** a grand temple to a major deity — pantheon iconography at scale (bible, CC5).

## 10. Function testers
- **F1** All `temple` testers (oriented axis, altar east, nave/choir screen separation, secure sacristy).
- **F2** Transepts forming a cross with a crossing tower/spire.
- **F3** A crypt below (relics/tombs, Part 8).
- **F4** A chapter house + cloister for the clergy.
- **F5** The bishop's **cathedra** in the choir.
- **F6** A monumental west front/entrance with towers.

## 11. Fixtures & assets needed (→ backlog)
High altar, cathedra, choir stalls, rood screen, **stained glass (decal)**, bells, tombs/effigies, font. →
[`WantedAssetsBacklog.md`](../WantedAssetsBacklog.md).

## 12. Grounding ledger
| Claim | Provenance |
|---|---|
| nave/chancel/screen, cruciform, crossing tower | REUSE-CANON — `temple` (Nave wiki, Hamilton Thompson, cited there) |
| crypt | REUSE-CANON — Part 8 |
| chapter house + cloister | REUSE-CANON — `monastery` / St Gall |
| cathedral **scale/proportions** | `to_ground` — enormous regional variation |
| stained glass | WANTED material |

## 13. Open questions / unknowns
- Typical nave length/height band — `to_ground` (varies enormously; the "highest naves" list is extremes).
- Secular-canon vs monastic cathedral (affects whether a full cloister).

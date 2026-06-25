# Gaol / Prison — Archetype Data Sheet

> Status: **DRAFT**. Schema: [`README.md`](README.md).

## 1. Identity
- **id:** `gaol` (jail) / `prison`
- **function:** confine the accused / convicted / debtors securely
- **aka:** gaol, lock-up, bridewell / house of correction; the **dungeon** when under a castle (Part 8)
- **group:** Civic (Part 6); under a castle it is the subterranean dungeon
- **extends:** a secure masonry shell + cells; walls reuse the keep / retaining-wall canon
- **genre/period:** **real** and well-documented (medieval gaols, castle dungeons, oubliettes). Run by a **keeper for profit** — prisoners paid for food/comfort (a period truth worth modelling).

## 2. Essence
**Secure confinement against escape**, graded by prisoner type (debtor vs felon vs condemned). The defining
quality is *no easy egress* + total movement control through a single keeper-held choke point.

## 3. Threat model / failure modes
- **Escape** (primary) — over/through a wall, **through the floor** (tunnelling → Part 8), through the door, by overpowering the keeper, by bribe.
- **Rescue / riot** — a mob springing prisoners; the gate must hold.
- **Death by neglect/disease** — period gaols were lethal ("gaol fever"); ventilation/sanitation mattered and were often **absent** (model honestly; do not retrofit modern hygiene).

## 4. Access tiers / zoning
- **T0** keeper's lodging + gate/guardroom — the **only** entrance; controls all movement.
- **T1** common ward / debtors' area — laxer (debtors walked the leads/roof, used a common kitchen).
- **T2** felons' cells — secure, manacled, historically **6 to a room**.
- **T3** the worst — **oubliette** / "little ease" / condemned cell: solitary, often subterranean, deliberately inhumane.
- Only the keeper/guard passes freely; T0→T1→T2→T3 is increasingly controlled.

## 5. Required spaces (program) — with grounded cell dims
| Space | Tier | Purpose | Required fixtures | Size |
|---|---|---|---|---|
| gate/guardroom | T0 | sole entry, control | **barred gate**, weapon rack, keeper's seat, key-board | `to_ground` |
| keeper's lodging | T0 | gaoler lives on-site | dwelling fixtures | `to_ground` |
| common ward (debtors) | T1 | hold debtors | benches, common hearth/kitchen, latrine | `to_ground` |
| felons' cell | T2 | secure hold | locked/**barred door**, wall ring/manacles, straw, slop bucket; small high aperture | **GROUNDED**: ~3.0–6.4 m × 2.0–3.4 m (10–21 ft × 6.5–11 ft); a single improved cell **3.05 × 2.03 m** (10 × 6'8"); aperture ~**0.6 × 0.3 m** (2 × 1 ft) |
| oubliette | T3 | indefinite solitary | **top hatch**, no light | a narrow vertical pit; sometimes **too narrow to lie down** (< 2 m) |
| exercise yard *(optional)* | — | air | high walls | `to_ground` |

## 6. Adjacency & circulation rules (→ validator checks)
1. **One** controlled entrance, through the guardroom; the keeper's lodging covers it.
2. Cells open **only** onto controlled internal space — never a door/large window to the exterior.
3. Cell apertures are **body-impassable** (≤ ~0.6 × 0.3 m) and high.
4. The oubliette is entered **only from above** (a hatch) — no side door.
5. Felons (T2/T3) are **zoned apart** from debtors (T1).
6. Walls **and floor** meet the secure spec (tunnelling — especially above a sewer/undercroft).

## 7. Construction & materials
- **Masonry, thick walls** (reuse keep/retaining canon, cited); **iron-barred** doors/gates + manacle rings (WANTED: iron bars/gate asset).
- Apertures tiny, high, barred; minimal/no glazing.
- Often built **into** a castle (the dungeon, Part 8) or a town **gatehouse**.
- Sanitation: a latrine/pit at best — period gaols were unhealthy (model honestly).

## 8. Signature / legibility
Forbidding **blank masonry**; tiny barred high windows; a single heavy **iron-barred gate**; usually within or
under a fortified structure or gatehouse; grim.

## 9. Status / period / setting scaling
- **Low:** a village lock-up — one barred room.
- **Mid:** a town gaol — guardroom + debtors' ward + felons' cells.
- **High:** a castle dungeon — multiple cells + oubliette + a torture room (Part 8 subterranean).
- **Fantasy:** anti-magic warded cells, a magically-sealed oubliette, monster pens — world-bible overlay (Part 9).

## 10. Function testers (the deliverable)
- **F1** Exactly one controlled entrance, covered by the guardroom/keeper.
- **F2** Every cell opens only onto controlled interior space — no exterior door/large window.
- **F3** Cell apertures are body-impassable (≤ ~0.6 × 0.3 m) and high.
- **F4** The oubliette is **top-entry only**.
- **F5** Debtors and felons are zoned apart.
- **F6** Secure walls **and floor** meet spec (no tunnel/sewer breach below).
- **F7** The exterior presents no easily climbable/breachable face (barred, blank).

## 11. Fixtures & assets needed (→ backlog)
Iron-barred gate + cell doors, manacles/wall rings, slop bucket, straw, key-board, weapon rack, oubliette hatch,
torture implements (high tier). → [`WantedAssetsBacklog.md`](../../WantedAssetsBacklog.md).

## 12. Grounding ledger
| Claim | Provenance |
|---|---|
| cell dims (Lancaster 21×9 ft & 20'2"×11'2"; Southampton 18'9"×8.5 ft; Newcastle ~14 ft sq; improved 10×6'8", aperture 2×1 ft) | CITED — [theprison.org.uk](https://www.theprison.org.uk/) (Lancaster, Southampton entries) |
| debtor vs felon regime (6/room, manacled; debtors' kitchen + leads) | CITED — same |
| oubliette / "little ease" (top entry, too narrow to lie) | CITED — [Little Ease (Wikipedia)](https://en.wikipedia.org/wiki/Little_Ease); [Oubliette (history.co.uk)](https://www.history.co.uk/articles/oubliettes-in-the-uk) |
| wall thickness | REUSE-CANON — keep/retaining |
| gaol fever / poor sanitation | HISTORICAL — note honestly; do not retrofit hygiene |
| fantasy overlay | BIBLE-SOURCED (Part 9 / CC) |

## 13. Open questions / unknowns
- Typical **guardroom/ward** sizes — `to_ground`.
- Ventilation: meaningful standards didn't exist; "fixing" it is anachronistic — flag rather than invent.
- Where the gaol = a castle dungeon, the **whole sheet folds into Part 8** (subterranean) — resolve the boundary.

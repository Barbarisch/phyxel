# Manor / Ornate House — Archetype Data Sheet

> Status: **DRAFT**. Schema: [`README.md`](README.md).

## 1. Identity
- **id:** `manor` / `ornate_house`
- **function:** the lord's full residence — the spatial expression of lordship
- **aka:** manor house, gentry seat, (urban) great townhouse
- **group:** Dwelling (Part 6)
- **extends:** `manor_hall` (the great hall is its core)
- **genre/period:** real. *(The long gallery is a later/Tudor addition — mild flag.)*

## 2. Essence
The great hall (`manor_hall`) **grown into a full status hierarchy** of rooms — great chamber, solar, parlour,
chapel, service, lodgings — often around a **courtyard** behind a **gatehouse**, set in gardens. Defining
quality: a graded public→private sequence that performs lordship at every step.

## 3. Threat model / failure modes
- **Status display + privacy gradient** (the primary drivers).
- **Some defensibility** — a gatehouse controls the court.
- **Fire** — a large kitchen (detached/screened).

## 4. Access tiers / zoning
- **Public:** gatehouse → courtyard → great hall.
- **Semi-private:** great chamber, parlour.
- **Private:** solar, the lord's bedchamber, the chapel.
- **Service:** kitchen, buttery, pantry, servant lodgings.

## 5. Required spaces (program)
| Space | Tier | Purpose | Required fixtures | Size |
|---|---|---|---|---|
| gatehouse | public | control the court | a gate, a lodge | `to_ground` |
| great_hall | public | lordship | (see `manor_hall`) | `manor_hall` |
| great_chamber | semi | lord & lady's apartment | bed, seating, fireplace | `to_ground` |
| solar | private | family withdrawing room | seating, fireplace, window | `to_ground` |
| parlour | semi | small private sitting/dining *(later-medieval)* | table, seating | `to_ground` |
| chapel | private | worship | altar | `to_ground` |
| service | service | cook/store | kitchen, buttery, pantry | fire-separated |
| lodgings | service | servants/guests | beds | separate from the family |

## 6. Adjacency & circulation rules
1. The **gatehouse controls** entry to the courtyard; the **great hall opens off the court**.
2. The **great chamber / solar lie beyond the dais** (private, often upper).
3. The **chapel sits by the private apartments**.
4. **Service (kitchen/buttery/pantry) is screened** from the hall (low end); the kitchen is fire-separated.
5. **Servant lodgings are separate** from the family apartments.
6. Gardens lie within/beside the walls.

## 7. Construction & materials
- Stone or high-status timber; many rooms; a gatehouse; a chapel.
- WANTED: ornamental dressed stone, glazing (heraldic/stained), wall hangings/tapestries (decal), tile/lead roof.

## 8. Signature / legibility
A **gatehouse + courtyard + hall range + cross-wings + chapel + gardens**; ornate fenestration and dressed
detail; reads as wealth/lordship.

## 9. Status / period / setting scaling
- **Down:** a modest manor — hall + solar + service only.
- **Up:** a courtyard manor — gatehouse, ranges, chapel, gardens → fortified manor (→ `castle` adjacency).
- **Fantasy / gothic:** the Strahd/Cazador overlay — `manor` + a crypt (Part 8) + a gothic-horror **style overlay** (Part 9 / CC).

## 10. Function testers
- **F1** All `manor_hall` testers hold (the hall is the core).
- **F2** A great chamber **and** a solar — private apartments distinct from the hall.
- **F3** A parlour (small private room) where period-appropriate.
- **F4** A chapel.
- **F5** A service zone (kitchen/buttery/pantry) screened from the hall, kitchen fire-separated.
- **F6** *(courtyard manor)* a gatehouse controlling access.
- **F7** Servant lodgings separate from the family apartments.

## 11. Fixtures & assets needed (→ backlog)
Gate, great-chamber + solar furnishings, parlour set, chapel altar, wall hangings/tapestries (decal),
heraldic glazing (decal), ornamental stone. → [`WantedAssetsBacklog.md`](../../WantedAssetsBacklog.md).

## 12. Grounding ledger
| Claim | Provenance |
|---|---|
| room set (great hall, solar, great chamber, parlour, buttery, pantry, chapel) | CITED — [Manor house](https://en.wikipedia.org/wiki/Manor_house); [Solar](https://en.wikipedia.org/wiki/Solar_(room)); Great chamber |
| hall proportions/dims | REUSE-CANON — `manor_hall` |
| parlour | GENRE-FLAG (mild) — later-medieval |
| gothic/horror variant | STYLE-OVERLAY (Part 9 / CC) |
| room **sizes** beyond the hall | `to_ground` |

## 13. Open questions / unknowns
- Great chamber vs solar distinction by period (the terms shift) — to pin down for the program.
- Courtyard vs linear plan trigger (status threshold) — to define.

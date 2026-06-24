# Archetype Data Sheets

Deep, per-archetype specifications. [`StructureGenerationPlacers.md`](../StructureGenerationPlacers.md) Part 6 is
the **index** (a one-line program + signature + function-test per archetype); the files in this directory are the
**depth** — enough that generation is deterministic, not guesswork. **No archetype is built in 3D until its data
sheet exists.** These are design-of-record toward a future `structure_archetypes.json`.

## Why the depth is mandatory

A thin entry ("bank = counting room + vault + door") renders a labelled box. It cannot tell whether the result
is *actually* a bank or trivially robbable. The defining quality of many archetypes — especially
security/function-defined ones (bank, gaol, vault, armory, temple sanctuary, smithy) — is an **access-controlled
gradient answering a threat**, which a room list doesn't capture. The data sheet makes that explicit and
**testable**.

## The data-sheet schema

Every sheet has these sections:

1. **Identity** — id, function, aka, Part 6 group, `extends` (base shell/room-program), **genre/period flag** (real / anachronistic / fantasy, per checklist CC7).
2. **Essence** — the single defining quality (what makes it this, not a generic box).
3. **Threat model / failure modes** — the adversary/failure the design must answer. *Every feature should derive from this.*
4. **Access tiers / zoning** — the security/privacy gradient (public → staff → secure …) and the control between tiers.
5. **Required spaces (program)** — per space: purpose, required fixtures, size (cite a canon, reuse one, or `to_ground`).
6. **Adjacency & circulation rules** — hard rules ("X reachable only via Y", sightlines, separations). These become validator checks.
7. **Construction & materials** — structural specifics (wall material/thickness — cite/reuse/`to_ground`), openings logic, fire separation; names the WANTED materials it needs.
8. **Signature / legibility** — how it reads as this archetype, inside and out.
9. **Status / period / setting scaling** — low→high tier variants; the real→fantasy overlay (Part 9).
10. **Function testers** — concrete pass/fail gates (the building-level function test, decomposed). **The deliverable.**
11. **Fixtures & assets needed** — explicit list, cross-referenced to [`WantedAssetsBacklog.md`](../WantedAssetsBacklog.md).
12. **Grounding ledger** — per-value provenance: CITED (source) / REUSE-CANON / `to_ground` / GENRE-FLAG. The honesty record.
13. **Open questions / unknowns** — what we still don't know. Honest gaps, not silent guesses.

**Grounding rule applies in full:** no dimension stands by vibe. Cite it, reuse a grounded canon, or flag it
`to_ground` / NEEDS-RESEARCH. Genre-flag any post-medieval/fantasy element.

## Index

**Progress: 24 DRAFT / ~50 total.** One row per archetype (no bundling). STUB = in Part 6, no deep sheet yet.
`BLOCKED` = waiting on an engine feature. Scaling *tiers* of a done sheet (e.g. shrine/chapel under `temple`)
are noted, not separate files, unless they diverge enough to need their own.

### Dwellings (7) — ✅ complete
| Archetype | Status |
|---|---|
| [`croft`](croft.md) | **DRAFT** |
| [`longhouse`](longhouse.md) | **DRAFT** |
| [`hall_house`](hall_house.md) | **DRAFT** |
| [`manor_hall`](manor_hall.md) | **DRAFT** |
| [`townhouse`](townhouse.md) | **DRAFT** |
| [`manor`](manor.md) / ornate_house | **DRAFT** |
| [`slum_tenement`](slum_tenement.md) | **DRAFT** |

### Hospitality (1)
| [`tavern`](tavern.md) / inn | **DRAFT** |

### Commerce — shops (9) — ✅ complete
| Archetype | Status |
|---|---|
| [`blacksmith`](blacksmith.md) | **DRAFT** |
| [`general_store`](general_store.md) / trading_post | **DRAFT** |
| [`apothecary`](apothecary.md) | **DRAFT** |
| [`bakery`](bakery.md) | **DRAFT** |
| [`butcher`](butcher.md) | **DRAFT** |
| [`tailor_weaver`](tailor_weaver.md) | **DRAFT** |
| [`cooper_carpenter`](cooper_carpenter.md) | **DRAFT** |
| [`tanner`](tanner.md) *(noxious)* | **DRAFT** |
| [`arcane_emporium`](arcane_emporium.md) *(fantasy)* | **DRAFT** |

### Finance (1)
| [`bank`](bank.md) / counting_house | **DRAFT** (exemplar) |

### Entertainment & vice (4) — ✅ complete
| Archetype | Status |
|---|---|
| [`brothel`](brothel.md) / stews | **DRAFT** |
| [`theatre`](theatre.md) / playhouse *(early-modern)* | **DRAFT** |
| [`costumier`](costumier.md) | **DRAFT** |
| [`gambling_den`](gambling_den.md) | **DRAFT** |

### Civic & institutions (10)
| Archetype | Status |
|---|---|
| [`gaol`](gaol.md) / prison | **DRAFT** |
| guildhall | STUB |
| town_hall / moot_hall | STUB |
| civic_palace / seat_of_state | STUB |
| warehouse | STUB |
| mill *(water/wind)* | STUB |
| printing_house *(early-modern)* | STUB |
| bathhouse / stews | STUB |
| hospital / hospice | STUB |
| mortuary / mausoleum | STUB |

### Faith (3)
| Archetype | Status |
|---|---|
| [`temple`](temple.md) / church *(covers shrine→chapel→church tiers)* | **DRAFT** |
| cathedral *(compound)* | STUB |
| monastery *(compound)* | STUB |

### Power / fortified (3)
| Archetype | Status |
|---|---|
| tower_house / wizard_tower | STUB |
| keep / great_tower | STUB |
| castle *(compound)* | STUB |

### Industry (1)
| foundry / manufactory *(early-modern/fantasy)* | STUB |

### Agriculture & outbuildings (6)
| Archetype | Status |
|---|---|
| byre / cowshed | STUB |
| stable | STUB |
| barn | STUB |
| granary | STUB |
| dovecote | STUB |
| pigsty / coop | STUB |

### Maritime (5) — `BLOCKED` on the water/shoreline engine feature
| Archetype | Status |
|---|---|
| wharf / pier / quay | BLOCKED |
| harbormaster / customs_house | BLOCKED |
| fish_market | BLOCKED |
| lighthouse / beacon_tower | BLOCKED |
| boathouse / shipyard | BLOCKED |

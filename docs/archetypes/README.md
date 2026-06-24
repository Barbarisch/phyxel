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

| Archetype | Group | Sheet status |
|---|---|---|
| [`bank`](bank.md) — counting_house | Finance | **DRAFT** (worked exemplar) |
| [`gaol`](gaol.md) — prison | Civic | **DRAFT** |
| tavern / inn | Hospitality | STUB |
| blacksmith | Commerce | STUB |
| arcane_emporium | Commerce | STUB |
| temple / church | Faith | STUB |
| tower_house / wizard_tower | Power | STUB |
| keep / castle | Power | STUB |
| townhouse | Dwelling | STUB |
| manor / ornate_house | Dwelling | STUB |
| slum_tenement | Dwelling | STUB |
| foundry | Industry | STUB |
| civic_palace | Civic | STUB |
| guildhall / town_hall | Civic | STUB |
| warehouse / mill | Civic | STUB |
| brothel / theatre / gambling_den | Entertainment | STUB |
| hospital / bathhouse / mortuary | Civic | STUB |
| harbor complex (wharf, lighthouse, …) | Maritime | STUB *(engine-blocked on water)* |

*(STUB = listed in Part 6, no deep sheet yet. The list mirrors Part 6; add a row when an archetype is added there.)*

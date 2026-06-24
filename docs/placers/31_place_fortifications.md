# 31 · place_fortifications

> Tier: Conditional (defensibility > none). Part-1 status: **M**. Schema: [`README.md`](README.md).

## Job
Build the defensive works — **curtain wall + flanking towers + gatehouse/barbican + crenellations + arrow loops
+ moat/ditch**. Runs with the structural placers when the brief sets a threat.

## Reads
- Brief `defensibility > none`; the enceinte line (castle/town); checklist Q; the keep/castle archetype sheets.

## Emits
- A battlemented **curtain wall** (2–6 m), **mural towers** at intervals (covering fire), one **gatehouse** (+ barbican, portcullis, murder-holes, drawbridge), a **moat/ditch**, arrow loops + crenellations + a wall-walk.

## Algorithm
1. Trace the enceinte.
2. Build the curtain to the grounded thickness/height with a wall-walk + crenellations.
3. Place flanking towers at intervals so they **cover the curtain by enfilade** (no dead ground).
4. **One** gatehouse = the controlled entry (barbican / portcullis / murder-holes / drawbridge).
5. A **moat** (water) or a **dry ditch** outside; arrow loops through the curtain.

## Satisfies (checks)
Q (defense — wall / towers / gate / moat / loops), the castle F1–F9 testers.

## Engine capability needed
- Wall/tower/gate voxels — ✅; portcullis/drawbridge kinematic — ⚠️; **moat (water)** — ❌ (water gap → fall back to a **dry ditch**).

## Failure modes
- Towers too far apart → dead ground (no covering fire).
- More than one gate (Q).
- An undefended curtain; a water moat without the water feature (use a dry ditch + flag).

## Function testers
- **F1** A continuous curtain (2–6 m) + flanking towers covering it.
- **F2** Exactly **one** gatehouse (+ barbican / portcullis / murder-holes).
- **F3** A moat or dry ditch outside.
- **F4** Crenellations + arrow loops + a wall-walk.
- **F5** Sited for defense.

## Grounding
- Curtain **2–6 m**, gatehouse (Byczyna 6.8 × 9.5 m, single-source) — REUSE Part 6/7 + Q (cited).
- Tower spacing (covering fire) — `to_ground`; **moat = water = BLOCKED** (dry-ditch fallback).

## Open questions
- Motte-and-bailey vs concentric (the castle variants); palisade (timber) vs masonry by period.

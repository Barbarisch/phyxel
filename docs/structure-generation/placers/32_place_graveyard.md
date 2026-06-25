# 32 · place_graveyard

> Tier: Conditional (religious + burial). Part-1 status: **M**. Schema: [`README.md`](README.md).

## Job
Lay a **consecrated burial plot** — oriented graves, markers by status, a lych-gate, a charnel/ossuary or crypt —
beside a church/cathedral/monastery.

## Reads
- A church/cathedral/monastery (the graveyard adjoins consecrated ground); brief faith/burial; checklist R; the `mortuary_mausoleum` + crypt (Part 8) sheets.

## Emits
- A consecrated plot beside the church; **graves oriented** per the rite (Christian: W–E, feet east); **markers scaled by status** (wooden cross → headstone → table-tomb → mausoleum); a **lych-gate**; a **charnel/ossuary** or a crypt below; a boundary.

## Algorithm
1. Lay the plot beside the church (consecrated ground).
2. Grave rows **oriented per the rite** (W–E).
3. Assign markers by status; the wealthiest get a table-tomb / `mortuary_mausoleum`.
4. A **lych-gate** at the entrance; a **charnel/ossuary** (or crypt, Part 8) for disinterred bones; a boundary wall (#23).

## Satisfies (checks)
R (religious/burial — consecration, orientation, markers, lych-gate, charnel), + the `mortuary_mausoleum` links.

## Engine capability needed
- Grave/marker props — ⚠️ (headstone/tomb templates → backlog §3); a **crypt below** — ❌ (Part 8 excavation).

## Failure modes
- Graves mis-oriented (rite).
- Markers ignoring status.
- A graveyard not adjoining consecrated ground.

## Function testers
- **F1** A consecrated plot beside the church.
- **F2** Graves oriented per the rite (W–E).
- **F3** Markers scaled to status.
- **F4** A lych-gate.
- **F5** A charnel/ossuary or crypt; a boundary.

## Grounding
- Orientation (east) — REUSE R / `temple` (cited); crypt dims — Part 8 (cited).
- Marker types by status — `to_ground`.

## Open questions
- Non-Christian / fantasy-pantheon burial rites (cremation, barrow, sky-burial) — bible-driven (Part 9).

# 19 · place_clutter

> Tier: Interior. Part-1 status: **M**. Schema: [`README.md`](README.md).

## Job
Scatter **grabbable surface props** — tableware, books, bottles, tools, food — on tables, shelves, mantels, and
counters, so a room reads as *lived-in*, appropriate to its use.

## Reads
- Furniture + fixtures (#16/#17) and their **surfaces** (tables, shelves, mantels, counters).
- Room use (kitchen → pots/produce; study → books/quill; tavern → tankards; smithy → tools).

## Emits
- Small **grabbable** (physics-enabled) props on the surfaces, use-appropriate, at a lived-in density.

## Algorithm
1. Enumerate the room's prop surfaces.
2. For each, draw use-appropriate clutter (kitchen counter → pots, a board, produce; study desk → books, a quill; tavern table → tankards; workbench → tools).
3. Density by lived-in-ness (occupied vs abandoned); place without clipping; mark grabbable.

## Satisfies (checks)
M (believability / lived-in), K (clutter), occupancy realism (ties to who uses the room).

## Engine capability needed
- Small-prop spawn (grabbable) — ✅.
- **The prop templates** — ⚠️ many MISSING (tableware, books, bottles, tools) → backlog §3.

## Failure modes
- Empty, sterile surfaces (reads as a showroom, not a home).
- Over-cluttered (noise) or clipping props.
- Anachronistic props (A).

## Function testers
- **F1** Prop surfaces carry **use-appropriate** clutter.
- **F2** Density reads as lived-in, not a junk pile and not empty.
- **F3** Props are grabbable (physics), not clipping.
- **F4** Period-correct items only.

## Grounding
- Qualitative; a prop set per room use; period-correctness via A.

## Open questions
- Tie clutter density to the **condition/age overlay** (a derelict room vs a kept one) — the overlay system (backlog §4).

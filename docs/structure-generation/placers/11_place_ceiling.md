# 11 · place_ceiling / place_intermediate_floor

> Tier: Closure & roof. Part-1 status: **P** (fused; single story only). Schema: [`README.md`](README.md).

## Job
Cap each story: a **ceiling slab** on the top story, or a **structural intermediate floor** between stacked
stories (which *is* the ceiling of the story below + the deck of the story above).

## Reads
- Walls + load-bearing partitions (#6/#7); story height; `stories[]` (is there a story above?); stack_stories (#36) base-Y.
- Style: `ceiling` / `floor` thickness.

## Emits
- **Top story** → a ceiling slab at the wall top.
- **Multi-story** → an intermediate floor: joists bearing on the walls/partitions below, a finish deck above (= the upper story's floor).
- A **reserved stairwell/hatch hole** (cut by place_stairs #12, not filled here).

## Algorithm
1. If this is the top story → paint a ceiling slab at the wall top.
2. Else → span joists across the bearing walls/partitions (shorter span), deck above them; the deck's top = the next story's floor datum.
3. Reserve the stairwell/hatch footprint (mark, don't fill).
4. Record the bearing lines (the floor must land on walls/posts, not mid-span).

## Satisfies (checks)
E (ceiling/floor thickness), V3 (an intermediate floor IS the ceiling below; stairwell holes pierce both), D / V10 (joists bear on walls/posts, not mid-span).

## Engine capability needed
- Slab/joist paint — ✅.
- **Multi-story base-Y stacking** — ⚠️ depends on stack_stories (#36), the missing loop (realizer hard-codes `stories[0]`).

## Failure modes
- A ceiling with no floor above on a multi-story building (V3).
- Joists spanning with no bearing wall under them (D).
- Filling the stairwell that #12 must reopen.

## Function testers
- **F1** The top story has a ceiling slab.
- **F2** Each intermediate floor is the ceiling-below + the deck-above (one element).
- **F3** Joists bear on walls/posts below, not mid-span.
- **F4** The stairwell/hatch footprint is reserved (not solid).

## Grounding
- Ceiling/floor thickness — REUSE `structure_styles.json`.

## Open questions
- Exposed-beam ceiling vs plastered vs vaulted by status/material (a J/finish refinement).

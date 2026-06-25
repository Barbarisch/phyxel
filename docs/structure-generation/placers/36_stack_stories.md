# 36 · stack_stories

> Tier: Vertical. Part-1 status: **M** (the realizer hard-codes `stories[0]`, line 71). Schema: [`README.md`](README.md). See Part 5.
> **The multi-story keystone** — the prerequisite for upper floors, basements, and attics.

## Job
Run the per-story placers at **each** story's base-Y (stacking them), then realize each `ProgStair` connecting
them — the loop the realizer currently lacks.

## Reads
- `program.stories[]` (**all** of them, not just `[0]`); per-story height; `ProgStair`.

## Emits
- Drives the per-story placers (floor/walls/openings/ceiling #4–11) for **each** story at the right base-Y; then calls **place_stairs (#12)** to connect them.

## Algorithm
1. Compute each story's **base-Y** (running sum of heights + floor thicknesses; basement at negative Y).
2. For each story (basement → ground → upper → attic), run the per-story placers at that base-Y.
3. Ensure walls/posts **stack** — upper walls bear on the walls/posts below (the vertical load path).
4. Realize each `ProgStair` (fromStory → toStory) via #12.

## Satisfies (checks)
V3 (floors stack, no gap/overlap), V10 (vertical load stacks), V1 (stairs connect, via #12), and the **"realizer builds only `stories[0]`"** gap.

## Engine capability needed
- **The multi-story loop** — ❌ MISSING (realizer hard-codes `stories[0]` at line 71 — the headline gap this placer closes).

## Failure modes
- Only the ground floor built (current).
- Upper walls **not over** lower walls → load mid-span (V10).
- A gap/overlap between stacked stories.

## Function testers
- **F1** Every story in `stories[]` built at the correct base-Y.
- **F2** Floors stack with no gap/overlap.
- **F3** Upper walls/posts bear on those below.
- **F4** `ProgStair`s connect the stack (via #12).
- **F5** A basement is included at level −1.

## Grounding
- Story height — REUSE Part 5 ceiling clearances (cited); the load-stack — D / V10.

## Open questions
- Per-story footprint changes (a jettied upper floor, a set-back attic) — the base-Y loop must allow differing rects.

# 08 · cut_openings

> Tier: Site & shell. Part-1 status: **P** (gaps only — no sills/reveals/lintels). Schema: [`README.md`](README.md).

## Job
Carve **door / window / arch** openings through the walls — with **sills, reveals, and lintels** — at the portal
positions. (The leaf/glazing themselves are place_doors #9 / place_windows #10.)

## Reads
- Walls from #6/#7; the **portal graph** + portal specs (pos, width, height, kind) from #5.
- Brief/archetype: window/opening logic (period + status + defensibility → how many, how big, glazed/shuttered/barred).

## Emits
- A carved void per portal (the current `fillMicroBox(..., "")` clears the wall band).
- A **lintel** over each opening (a beam/arch carrying the wall above), **reveals** (the thickness returns), and a **sill** for windows (the current ~1-cube sill).
- `AssemblyPlan.openings` entries (kind, pos, size, state).

## Algorithm
1. For each portal, map its position to the wall cell(s) + orientation (the current `alongZ` logic).
2. Carve the opening void: full width × (sill→head) height through the wall thickness.
3. Add a **lintel/arch** above (so the wall above is carried — not floating) and **reveals** at the jambs.
4. Windows: set a **sill** at the grounded height; doors: down to the floor; arches: full height.
5. Tag each opening's intended treatment (door leaf / glazing / shutter / board / bar) for #9/#10.

## Satisfies (checks)
H (openings — doors/windows: count, size, placement, sills, lintels), D (the wall above an opening is carried by a lintel, not floating), A/C (opening logic by period + status), G (door sizes pass clearance).

## Engine capability needed
- Void carve through a wall band — ✅ (`fillMicroBox` with empty material).
- Lintel/arch emit — ⚠️ (logic missing; the current code carves the gap but adds no lintel → the "wall above the opening floats" risk).

## Failure modes
- A carved gap with **no lintel** → the wall above is unsupported (a floating-voxel/structural lie) — the current gap-only behavior.
- Windows with no sill / doors not reaching the floor.
- Openings clipping a corner or another opening → overlap check.
- Too many/large openings on a defensible wall → A/C/Q-violation.

## Function testers
- **F1** Each portal is a clean void of its width × height through the wall.
- **F2** A **lintel/arch** carries the wall above every opening (nothing floats).
- **F3** Windows have a sill at the grounded height; doors reach the floor.
- **F4** Reveals (jamb returns) present in the wall thickness.
- **F5** No opening clips a corner/another opening.
- **F6** Opening count/size respects period + status + defensibility.

## Grounding
- Door clear height / window sill height — REUSE the grounded canon (Part 5: door clear 2.03 m; window sill ~1 cube) + Part 2 H.
- Opening count/size by period/status — `to_ground` per archetype (e.g. a keep's "few small high openings").

## Open questions
- Lintel vs arch (timber lintel ↔ stone arch) by material — pick from the style.
- Relieving arch over wide openings in masonry — a D refinement.

# 10 · place_windows

> Tier: Closure & roof. Part-1 status: **M**. Schema: [`README.md`](README.md).

## Job
Fill window openings per **period + status + climate + defensibility** — glazing, shutters, boards, bars, or
open.

## Reads
- Window openings + sills from cut_openings (#8).
- Brief: period, status, region/climate, defensibility.

## Emits
- Per window: **glazing** (`Glass`), **shutters**, **boards**, **bars**, or an **open** void; (stained/heraldic → a decal, currently blocked).

## Algorithm
1. Choose the treatment: glass = high status / later period; shutters or boards = common; bars = defensible or ground-floor; open = warm climate / poor.
2. Place the chosen fill in the opening on its sill.
3. Defensible walls → narrow + barred at ground; light-hungry workshops (weaver, scriptorium) → larger + glazed/shuttered.
4. Tag stained/heraldic glass for the decal system (flag — not faked).

## Satisfies (checks)
H (windows), A2 (glazing available in the period — no float glass early), C (glazing scales with wealth), B (climate response), Q (defensible windows barred), J (finish).

## Engine capability needed
- `Glass` material — ✅.
- Shutters/boards/bars as voxel fills — ⚠️ (static/kinematic; minor).
- **Stained / heraldic glass decal** — ❌ MISSING (backlog §2).

## Failure modes
- Float glass before its era (A2); glazing a peasant croft (C); a window with no sill (H).
- A big undefended window on a keep (Q).

## Function testers
- **F1** Every window treated per period + status + climate.
- **F2** Ground-floor / defensible windows barred or narrow.
- **F3** Glazing only where the period + status allow it.
- **F4** Stained glass flagged for the decal system, never faked with a flat texture.

## Grounding
- Glazing availability by period — Part 2 A2; sill height — Part 5.
- Window size/count by status + defensibility — `to_ground` per archetype.

## Open questions
- Leaded-light vs single-pane vs horn/oiled-cloth by period/status.

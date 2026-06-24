# 33 · apply_seasonal_state

> Tier: Conditional (final dressing pass). Part-1 status: **M**. Schema: [`README.md`](README.md).

## Job
Skin the finished build for **season + time-of-day** — snow, foliage, crop state, chimney smoke, shutters, ice —
as a **dressing pass over a season-agnostic base** (the structure is NOT rebuilt per season).

## Reads
- The finished build; brief **season + time-of-day**; climate.

## Emits
- Seasonal dressing: **snow** on roofs/ground (winter), **foliage** state (bare/leaf/autumn), **crop** state in the fields (#26), **chimney smoke** (occupied + cold), **shutters** closed (night/storm), **ice** (frozen water), mud (wet).

## Algorithm
1. Run **last**, as an overlay (don't rebuild the structure).
2. Apply the season + time skin: snow accumulation; leaf/crop state; smoke from **occupied + cold** hearths; shutter/door state by time/weather; puddles/ice.

## Satisfies (checks)
S (season/temporal state), S15 (a **dressing pass**, not a rebuild).

## Engine capability needed
- Overlay material/prop toggles (snow layer, foliage swap, smoke particles, shutter state) — ⚠️ (needs a season-agnostic base + an overlay system; ties to the style/condition-overlay backlog §4).

## Failure modes
- **Rebuilding** the structure per season (S15 — it must be a skin).
- Summer snow; smoke from an empty/cold house; leaves on a winter tree.

## Function testers
- **F1** Dressing applied as an **overlay** (the base build is unchanged).
- **F2** Snow / foliage / crop / smoke / shutter / ice match the season + time.
- **F3** Smoke only from **occupied + cold** hearths.

## Grounding
- Qualitative (a season skin); snow depth + foliage timing by climate — `to_ground`.

## Open questions
- Shared mechanism with the condition/age overlay (decay) — both are "skins over a base build" (backlog §4).

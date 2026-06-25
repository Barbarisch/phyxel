# 13 · place_roof

> Tier: Closure & roof. Part-1 status: **P** (gable on a bounding rect, blocky, floats; gable-end thin-wall fix in). Schema: [`README.md`](README.md).

## Job
Roof the building over its **real outline** — gable / hip / valley — with **eaves overhang + fascia/soffit +
ridge**, leaving a **hollow void** beneath for the attic (place_attic #37).

## Reads
- Wall top + the **real footprint outline** (not just a bounding rect); the story's ceiling (#11).
- Style: roof material, `pitch_deg`, `roofStyle` (gable/hip/…).

## Emits
- Roof planes at the grounded pitch (`pitch = max(1, round(3·tan(deg)))`), as a **hollow shell** (~pitch+1 subcubes thick — the current shell logic), following the outline.
- **Eaves overhang** beyond the wall + fascia/soffit; a **ridge**; **valleys** where wings meet.
- **Thin gable-end walls** (exterior-wall thickness, the existing `gableBand` fix — *not* full-cube).
- A hollow attic void beneath (handed to #37).

## Algorithm
1. From the real outline (not a bounding box), build the roof surface at the style pitch.
2. Keep it a **hollow shell** (don't fill the wedge solid) → leaves the attic void.
3. Extend the eaves beyond the wall face (overhang) + add fascia/soffit.
4. Hip ends / valleys where the plan turns or wings meet; gable ends get a **thin** triangular wall, not a full cross-section.
5. No floating voxels at gable/partition edges (the known bug).

## Satisfies (checks)
I (roof form, pitch grounded, weatherproof, eaves), V7/V9 (a usable attic void, not a solid wedge), M (no floating/blocky roof), and the "blocky-floating-gable / full-cube gable wall" fixes.

## Engine capability needed
- Subcube roof paint — ✅ (`addSubcube`).
- **Hip/valley over a non-rectangular outline** — ⚠️ (current does gable-on-rect; flat cap otherwise).
- **Eaves overhang** beyond the footprint — ⚠️.

## Failure modes
- Roofing a **bounding rect** instead of the real outline (current) → wrong shape on L-plans.
- A **solid wedge** (no attic void).
- **Floating** gable voxels; **full-cube** gable-end walls (the caught bug).
- Pitch ignoring the style → wrong-angle thatch/tile.

## Function testers
- **F1** The roof follows the **real outline** (hips/valleys on non-rect plans).
- **F2** Pitch = the style's `pitch_deg` (grounded).
- **F3** Eaves overhang the wall + a fascia/soffit.
- **F4** A **hollow void** beneath (attic-ready), not a solid wedge.
- **F5** Gable ends are thin walls; no floating voxels.

## Grounding
- Pitch — REUSE `structure_styles.json` `pitch_deg` (cited: thatch 50°, tile 40°).
- Eaves overhang — `to_ground` (function of material + climate; thatch oversails more).

## Open questions
- Hip vs gable vs gambrel by region/period; dormers (handed to place_attic #37).

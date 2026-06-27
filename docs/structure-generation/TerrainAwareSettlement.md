# Terrain-Aware Settlement — roadmap + validation plan

The settlement deployer (`engine/core/SettlementLayout` + `build_settlement`) currently places
buildings on a **flat-plane grid** and is **terrain-blind**. The goal: deploy settlements on varied
terrain — smartly pick flat spaces / hilltops for buildings, route walkable paths between them, accept
some flattening but generally preserve the terrain, and **degrade gracefully** where terrain is
unbuildable (a sheer mountain → few/no buildings, with a surfaced reason, not a broken result).

## What the engine already has (building level — terrain-adapted)

`PlacedObjectManager::seatStructure` / `SeatPlan` adapts a **single** building to terrain: samples the
ground under the footprint column-by-column (`ChunkManager::hasVoxelAt`), takes the **median** grade
(robust to stray boulders/pits), seats the floor **flush** with the surrounding walkable surface,
**excavates** the occupied terrain, and **builds steps** in front of ground-level doors. The v2 build
path uses it, so each `build_settlement` house already cut/fills to seat itself.

**Missing = the settlement-level smarts:** `SettlementLayout` does NOT analyze terrain, place plots on
good ground, route paths over terrain, cap cut/fill, or limit on steep terrain.

## Validation throughline (applies to every phase)

- **Buildability is MEASURED against real terrain fixtures (L2).** `WorldGenerator` Perlin (rolling
  hills) / Mountains (steep) / Flat give deterministic terrain to assert against — slope/grade/water
  classification must match the fixture, not vibe.
- **Paths + seating must be physically WALKABLE on real terrain (L3).** A `TraversalProbe` walks the
  actual terrain+path occupancy, every step ≤ the character's step-up (reuse the stair/door L3
  machinery). "Drew a path" is not "the path is walkable."

## Phases

| phase | scope | validation (required layer) |
|-------|-------|------------------------------|
| **0** | **Stress the FLAT deployer at scale** — N≈25, mixed typologies. Proves composition scales; surfaces the frame-stall + larger-grid layout/variety bugs N=4 hid. (No terrain.) | every building seats, 0 bbox overlaps at N=25; mixed typologies produce distinct buildings; assert invariant at scale |
| **1** | **`analyze_site` (#01) — buildability map.** Sample terrain over the region → per-cell {height, slope, water?} → classify flat / slope-ok / too-steep / water. The keystone everything else consumes. | **L2**: on a Perlin/Mountains fixture, classification matches the terrain (assert slope/grade/water at known cells) |
| **2** | **Terrain-aware plot placement (B+C).** Pick flat areas + hilltops for plots; size to fit; seat with a **cut/fill budget**; reject plots that would over-excavate. | **L2** plots on buildable terrain (slope < threshold), no overlap, fit; cut/fill ≤ budget; **L3** a building actually seats there |
| **3** | **Smart path routing over terrain (D).** Route walkable paths between buildings following contours; switchbacks/steps on grade; flatten only where needed. | **L3** a TraversalProbe walks each path end-to-end over real terrain+path, every step ≤ character step-up |
| **4** | **Stress on REAL terrain (E) + graceful degradation.** Rolling hills with water/cliffs: all buildings seat within budget, none in water/on a cliff, every building reachable by a walkable path; steep mountain → few/no buildings + a surfaced reason. | **L3** whole-settlement walkability over terrain; **L2** degradation: steep fixture → few/no plots + signal; hills → full settlement |

**Sequencing:** Phase 0 (flat stress) → Phase 1 (`analyze_site`, the keystone) → 2 → 3 → 4.

## Status

- ✅ Settlement composition on flat ground: `subdivide_plots` (L2), `populate_plots` (L2), inter-building
  walkability (L3), `build_settlement` (engine-side) — see [`ValidationLedger.md`](ValidationLedger.md)
  settlement tier.
- ✅ **Phase 0** — DONE. `ScaleSixBySixAllInvariantsHold` (36 plots/buildings, no overlap at scale);
  `build_settlement` mixed typologies (`typologies` array). Runtime: 25-building mixed village built,
  0 overlapping bboxes. **Findings:** (1) frame-stall is MILD (builds ~3-4s, API stayed responsive —
  spreading over frames is low priority); (2) **mixed typologies on a UNIFORM grid trip per-typology
  footprint gates** (manor wants elongated → `footprint_too_square`; croft wants narrow →
  `footprint_too_wide`) — warn-but-allow builds them, but a real follow-up is **vary plot size per
  typology** (or assign typologies that fit the plot); (3) exterior variety is interior-only today
  (all timber_cottage gable) — a separate refinement.
- ✅ **Phase 1** (`analyze_site`) — DONE + validated against REAL terrain. `SiteAnalysis` classifies
  Flat/SlopeOk/TooSteep/Water from the height **RELIEF over a building-footprint window** (= cut/fill
  needed). **Key finding (real-terrain validation drove a redesign):** the original point 1-cube slope
  was WRONG — real Perlin/Mountains terrain is smooth at 1-cube spacing, so both read 100% buildable;
  the footprint-relief metric discriminates them. Encoded against `WorldGenerator::sampleSurface`:
  **Perlin hills = 98% buildable, Mountains = 60%**. Synthetic fixtures (cliff/plateau-skirt-cliff/
  water) + real-terrain test, red-confirmed teeth.
  **Remaining glue (with Phase 2):** a runtime `ChunkManager` column-scan sampler so the live deployer
  analyses the actual world (the encoded validation already uses the generator's height fn).
- ✅ **Phase 2** — DONE + LIVE-VERIFIED (auditor PASS on substance). `selectBuildablePlots(site,
  plotSize, spacing, maxPlots)` picks non-overlapping plots whose ENTIRE footprint is buildable (no
  TooSteep/Water), spaced, flattest-first; plots land on flat valleys + hilltop plateaus, never
  cliffs/water; **graceful degradation** — a sheer mountain yields 0 plots. Red-confirmed (stub on
  cliff/mountain).
  **Glue is now LIVE:** `build_settlement` "terrain" mode (`editor/src/Application.cpp`) wires a
  runtime `groundTopAt` `ChunkManager` column scan into `analyzeSite` → `selectBuildablePlots` →
  per-building seating (`by = groundTopAt(footprint centre)`). Verified at runtime (L4) on a generated
  Perlin rolling-hills world (StructGenHills, seed 7, heightScale 18):
  - **Discrimination (live log):** FLAT world `buildable=1.0`; HILLS world `buildable=0.926509` —
    same code path, not unit tests (`scripts/seating_evidence/buildable_flat_vs_hills.txt`).
  - **Seating invariant (recorded red-before-green):** for every building, `|seatY − median(terrain
    ringing the footprint)| ≤ 2`. RED (`seat_flat` toggle → terrain-blind `by=oy`, same plots) buries
    buildings, **max dev 3.0 → FAIL**; GREEN (shipped seating) tracks the ground, **max dev 1.0 →
    PASS**. Threshold 2 sits strictly between (justified by data separation, not chosen to pass).
    Proof: `scripts/verify_terrain_seating.py` (+ `--seat-flat`), driver
    `scripts/run_seating_redgreen.sh`, evidence in `scripts/seating_evidence/`.
  - **Known race (not a logic bug):** if `build_settlement` is called while async `generate_world` is
    still populating, `groundTopAt` reads incomplete columns and seats low — buildings buried (≈7 in
    the rough-terrain repro). In normal use the world is pre-generated (loaded from DB) so no race; the
    driver waits until terrain is complete (`voxel_top==terrain_height`) before building.
- ☐ Phases 3–4 — planned (this doc): walkable paths over terrain; terrain stress + degradation.
  **Phase 2 follow-up (carried):** the v2 build path seats the floor at the footprint-centre ground
  but does NOT yet cut/fill flush across the footprint, so on a slope corners can clip/float by the
  local grade — Phase 3 (cut/fill budget + steps) closes this.

### Follow-ups surfaced (not yet scheduled)
- **Per-typology plot sizing** — the uniform grid mismatches croft (narrow) / manor (elongated); plots
  should be sized to the assigned typology's grounded width/length range.
- **Exterior variety** — vary style/roof/size across buildings so a settlement isn't visually uniform.

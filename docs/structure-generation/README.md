# Structure Generation Pipeline — START HERE

The canonical home for the **structure generator**: the system that lets the *engine* generate
buildings/settlements from a high-level intent, deterministically and grounded — not Claude
hand-placing voxels. New session, or picking this up on another machine? Read this page first, then
follow the reading order below. **The approach is uniform and enforced — see "Standing rules".**

> Scope: buildings & settlements. The *terrain/world* generator (biomes, flora, world recipe) is a
> sibling pipeline and lives at `docs/TerrainGenerationBiomes.md` / `docs/WorldRecipeAndFlora.md`,
> not here. The cross-session game-dev **feedback** inbox (`docs/feedback/`) is unrelated.

## The pipeline, one line

`StructureBrief` (grounded intake) → `BuildingProgram` (LLM-authorable semantics) → derived physical
plan → **realize** into a `MicroCanvas` (cube/subcube/microcube) → place into the world. Each level
has a validator/gate; nothing physical is invented by the LLM.

## Reading order

1. **[StructureGenerationV2.md](StructureGenerationV2.md)** — the live design of record (settlements,
   parcels, ground-up buildings, the level stack). Start here for *what & why*.
2. **[StructureBrief.md](StructureBrief.md)** — the mandatory grounded intake (the ~43-field brief);
   engine-resident schema. The gate before anything builds.
3. **[StructureGenerationPlacers.md](StructureGenerationPlacers.md)** — the placers (the HOW) + the
   quality checklist. Part index; the depth lives in the subdirs below.
4. **[StructurePipelineGaps.md](StructurePipelineGaps.md)** — engine/capability gaps (what's missing,
   logged honestly instead of faked).
5. **[GroundingGaps.md](GroundingGaps.md)** — dimensions still needing a real-world source.
6. **[BuildKnownIssues.md](BuildKnownIssues.md)** — honest tracker of issues found during runtime
   verification (KI-0…KI-4 history; KI-4 = the stairs-walkability saga).
7. **[StructureGenerationPipeline.md](StructureGenerationPipeline.md)** — *superseded* by V2; kept for
   history.

### Depth sheets (subdirectories)
- **[`placers/`](placers/)** — per-placer specs (one file per pipeline step, the HOW).
- **[`rooms/`](rooms/)** — per-room data sheets (the interior layer).
- **[`archetypes/`](archetypes/)** — per-building-type data sheets (the building layer). *No archetype
  is built in 3D until its sheet exists.*

## Standing rules — the uniform approach (NON-NEGOTIABLE)

Every placer and every bit of logic in this pipeline is held to these. They exist because each was
learned the hard way:

1. **Ground every dimension.** No invented sizes — each value cites a real source or is flagged a gap
   (`GroundingGaps.md`). The `grounding-auditor` agent enforces it.
2. **Validation is a layered, tracked deliverable** — not an afterthought. Each placer must reach the
   validation depth its *use* demands (see "Validation layers" below). Track it; don't ship L0/L1 for
   something a character must traverse.
3. **Stress-test, don't N=1.** Push the scaling axis to the extreme (10-story tower, full chunk) and
   assert the invariant at *every* step. The agent designs the adversarial test, not the user.
4. **Physical-usability invariants.** "Reachable" must mean *walkable*. Geometry-exists and
   topology-connected are NOT "works". Prove use with an algorithm + a measured invariant + a
   traversal check.
5. **Red-before-green; no fabricated verification.** A success claim needs a falsifiable measurement
   on the real output, shown failing first. The `solution-auditor` agent + the Stop-hook gate enforce
   this; "compiled / unit-passed / looks right" are proxies, not proof.

## Validation layers (how to size & track validation)

Pick the depth by what the output is *used for*; validate usability-critical + silent-failure +
scaling things first and deepest:

| Layer | Proves | Tool |
|---|---|---|
| L1 artifact exists / right shape | the thing is present | realizer + `MicroCanvas::occupiedMicro` |
| L2 structural invariant on real output | a measured property holds (clearance, no overlap, riser ≤ step) | `BuildingProgramValidator`, canvas scans |
| L3 functional agent simulation | a character can *use* it | **`TraversalProbe`** (kinematic character-box) |
| L4 live-engine runtime | it behaves in the real loop | MCP build + behaviour capture |

Cross-cutting: **scale** (rule 3) and **adversarial audit** (rule 5) at every layer.

## Engine components (where the logic lives)

`engine/include/core/` + `engine/src/core/`: `StructureBrief*` (intake + schema + validator),
`BuildingProgram` + `BuildingProgramValidator` (semantic spec + pre-build gate), `DimensionCanon` /
`StyleProfile` (grounded dims), `AssemblyPlan`, `MicroCanvas` (realization grid), `StructureRealizer`
(shell), `StairPlanner` (climbable stairs + `stackedEmergenceClearance`), `TraversalProbe` (walkability
sim), `FurniturePlacer`, `StructureGenerator` (place into the world). Canon data: `resources/*.json`
(`structure_styles`, `structure_brief_schema`, `room_program`, `object_dimensions`).
LLM-facing intake is the `structure` skill (`.claude/skills/structure/`).

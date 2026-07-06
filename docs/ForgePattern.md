# The Forge Pattern — one design for all procedural generators

_Established 2026-07-05 (user decision) after tree_forge proved the shape. This is the standing
architecture for every procedural generation layer: settlements, structures, finish/trim,
furniture, flora, texture masks. New generators follow it from birth._

## What a "forge" is

A forge is a **deterministic, grounded generator** with four stages:

```
GROUNDED PARAMS  →  PLAN / SKELETON  →  RASTERIZE (multi-res canvas)  →  EMIT (hierarchical compress)
 (real-world       (space colonization,  (draw in micro space at the    (solid interiors collapse
  numbers, cited    room layout, parcel   resolution the feature         to cubes; surfaces keep
  or gap-flagged)   plan, frame layout)   needs — never coarser)         sub/micro detail)
```

## The contract (all six, every forge)

1. **Grounded inputs.** Every dimension traces to a real-world source (silvics for trees, period
   building sources for structures, `object_dimensions.json` for furniture) or is explicitly
   flagged in a gaps doc. The grounding-auditor enforces. A generator with invented magic numbers
   is not a forge. This is load-bearing: the pipeline's credibility comes from the large corpus
   of real-world numbers already collected (DimensionCanon, StructureBrief's ~43 fields,
   StyleProfile sources maps) — forges consume that corpus, never bypass it.
2. **Determinism.** Same params + seed → byte-identical output. Libraries regenerate
   reproducibly; a pinned test proves it (`test_determinism`, `test_substrate_extraction_is_pure`).
3. **Per-voxel resolution; detail by default.** Coarse mass where it's invisible (emit compresses
   interiors to greedy-merged cubes), sub/micro on every visible surface, edge, and feature.
   NEVER behind a quality flag or tier — tree_forge deleted its `round_trunk` flag rather than
   default it on; the finish pass runs unconditionally.
4. **One algorithm, many presets.** Archetypes/styles/species are parameter presets of a single
   algorithm per domain — never a pile of bespoke per-type functions (the gen_tree trap).
5. **Red-before-green invariants on real output.** Each capability ships with a falsifiable
   structural test measured on the actual canvas/output, shown failing first, at the validation
   depth its use demands (L1-L4; ValidationLedger).
6. **Measured perf.** `ResolutionReport` / primitive counts / `get_render_stats` faces against a
   recorded datum (tavern 412k→49FPS; hero oak +2.4% for full surface shells). Budgets are
   numbers, not vibes.

## Substrates (the shared canvases)

| Substrate | Where | Used by |
|---|---|---|
| `tools/forge_core.py` | Python, offline baking | tree_forge, future asset forges |
| `MicroCanvas` (C++) | engine, runtime realization | structure pipeline |

These are conceptual twins (micro canvas + hierarchical C/S/M emit + detail primitives). When a
primitive earns its keep in one (capsule/sphere rasterizers, chamfer, frame/course/quoin
detailers), consider it for the other.

## The forge map (naming convention)

| Forge | Today's implementation | Status |
|---|---|---|
| `tree_forge` | `tools/tree_forge.py` | SHIPPED — the pattern's proof |
| `leaf_forge` | `tools/leaf_forge.py` | SHIPPED — texture-space forge (cutout masks) |
| `furniture_forge` | `tools/regen_furniture.py` | rename target (already grounded + micro-detailed) |
| `structure_forge` | `BuildingProgram → StructureRealizer → place` chain | shipped under legacy names |
| `finish_forge` | NEW — openings/trim detail layer (docs/structure-generation/FinishDetailPlan.md) | P1 in progress |
| `settlement_forge` | settlement/parcel layer of StructureGenerationV2 | future naming |

**Naming rule:** new components take forge names at birth (`FinishForge`). Existing classes
(`StructureRealizer`, `FurniturePlacer`, …) keep their names until a dedicated mechanical rename
commit — incremental refactoring, no big-bang renames; this table is the mapping of record.

## BlockSmith — deprecation intent (user, 2026-07-05)

The LLM-driven BlockSmith path (`external/blocksmith`, `tools/blocksmith_generate.py`,
`generate_template`/`build_building` MCP tools, the `--building` mode) **will be removed at some
point**. It predates the forges and violates the contract (non-deterministic, ungrounded, and its
buildings are the worst full-cube offenders). Do NOT build new features on it. Replacement path:
forges + the grounded template library. Removal is its own future task (touches CLAUDE.md, MCP
tools, the skills plugin, template_catalog provenance) — until then it merely coexists.

# Mixed-Resolution Voxel Composition — Design

> **Status:** design / not yet implemented. Motivated by the chimney/fireplace wall-gap defect, but the
> underlying issue is general and will recur across the engine. Owner: structure-generation workstream.

## Problem

Phyxel voxels exist at three resolutions in the same grid: **cube** (1×), **subcube** (1/3, 27 per cube),
**microcube** (1/9, 729 per cube). Structures (walls/floors/roofs) are built mostly as cubes/subcubes;
fixtures (furniture) are microcubes. When these **overlap in the same cell**, the composition is lossy:

1. **Coarse-deleted-by-fine (gaps).** `ChunkVoxelManager::addMicrocube`: adding the *first* microcube into
   a `(cube, subcube)` cell that a **subcube** already fills **removes the whole subcube** (27 micro-cells)
   to insert **one** micro-cell → the other 26 become air. If the fine element doesn't refill them, you get
   a **micro-level see-through gap**. (This is the fireplace breast eating the exterior wall: 92 wall
   subcubes removed per settlement build; where the breast's own micros don't re-seal, the wall leaks.)
2. **Fine-rejected-by-fine (blocked overwrite).** `addMicrocube` returns false if a microcube already
   exists at that cell — so you cannot *overwrite* detail, only add to empty cells. Any "refine the coarse
   material, then stamp the detail over it" strategy is blocked by this.
3. **Order-dependent, silent.** The outcome depends on placement order (structure-then-fixture vs
   interleaved), and the only signal was a misleading `LOG_WARN "possible database corruption"` (now
   downgraded to DEBUG — it was never corruption; it's this composition gap).

**Why it's general (not a chimney bug):** the same lossy composition breaks *any* finer-over-coarser case:
- built-in / wall-flush furniture (bookshelves, benches, counters against a subcube wall),
- wall-mounted or recessed fixtures (sconces, niches, hearths, ovens, forges),
- doorways/windows cut into subcube walls,
- structures embedded in terrain (cube terrain vs sub/micro foundations),
- future micro-detailed trim/molding over coarse walls.
Cube-level validators can't even *see* these (the hole is sub-cube), so they slip past `/api/world/validate`.

## Root cause

There is **no `refine` operation** and **no `overwrite`/priority semantics** in the mixed-resolution model.
"Place a finer voxel where a coarser one is" is implemented as *delete-coarse + add-one-fine*, which is
neither watertight (loses the other 26/27) nor composable (can't then overwrite).

## Design goals

1. **Watertight:** overlapping a coarse voxel with a finer one preserves the coarse **material** in every
   sub-cell the finer element does not explicitly claim. No gaps, ever, from refinement alone.
2. **Composable/overwrite:** a finer voxel placed at an occupied finer cell **replaces** it by a defined
   rule (priority or last-writer), so "refine coarse → stamp detail" works.
3. **Order-independent & deterministic:** same inputs → same voxels regardless of placement order.
4. **Density-bounded:** refinement inflates voxel counts; provide **re-coarsening** (merge uniform fine
   regions back to subcube/cube) so composition doesn't explode face counts (ties to render greedy-mesh #40).
5. **Validatable at the right resolution:** a **micro-solidity** check (is this structure surface watertight
   at micro level?) — the cube validator can't do this; the fix needs its own falsifiable test.

## Proposed architecture

### A. Refinement primitive (the core fix)
`ChunkVoxelManager::refineToMicro(cube, subPos)` — replace an existing **cube/subcube** with its constituent
**microcubes of the same material** (a cube → 729, a subcube → 27), leaving the grid visually identical but
now edit-at-micro-granularity. `addMicrocube` (and the bulk template-spawn path) calls this *instead of
deleting* when it finds a coarser voxel in the target cell: refine → then write the new micro. Result: the
26 untouched cells keep the wall material; the 1 touched cell gets the fixture → **watertight**.

### B. Overwrite semantics
`addMicrocube` gains a **replace mode** (or a per-voxel priority: `structural` < `fixture` < `override`).
When a micro exists at the target, replace it per priority (so a fixture micro wins over refined-wall
material, and structure never silently erases fixtures). The bulk template spawn uses replace, not reject.
This unblocks "refine coarse → stamp detail over it" and makes composition order-independent.

### C. Composition layering (who wins)
Define an explicit precedence so results are deterministic:
`terrain (cube) → structure (cube/subcube) → structural-element (hearth/stair, sub/micro) → fixture (micro)
→ clutter (micro)`. Higher layers refine + overwrite lower ones; lower layers never delete higher. A small
`layer`/`priority` tag on placed voxels (or inferred from the placement path) drives B.

### D. Re-coarsening (density control)
After a composition pass, a cleanup merges any `(cube, subcube)` whose 27 micros are uniform back into a
subcube, and any cube whose 27 subcubes are uniform back into a cube. Keeps the watertight fill from
exploding voxel/face counts. Complements — and is partly subsumed by — greedy meshing (RenderOptimization
#40): even without re-coarsening, a greedy mesher merges the interior fill into few faces.

### E. Validation (micro-solidity)
A realized-world check operating at **micro** resolution: for a structure's exterior surface (wall/roof
plane), assert **no air micro-cells** in the plane except intended openings (doors/windows). This is the
red-before-green test for the gap fix — the cube validator structurally cannot express it. Likely needs a
`scan_micro`-with-per-cell-occupancy API (current `scan_micro` returns per-cube counts, not per-micro
occupancy) or an in-engine watertightness scan exposed via a route.

## Complementary: structural elements in the realizer
Some "fixtures" are really **structure** — hearths, chimneys, staircases, built-in shelving. These should be
generated by the **structure realizer as first-class structural elements** (a hearth = a masonry
wall-segment + firebox + flue, built *with* the wall), not stamped as furniture *over* a finished wall. That
removes the overlap entirely for structural things (and fixes chimney #5/#7 directly), while the composition
model (A–E) handles the genuinely-decorative overlaps that remain. The two are complementary: realizer-level
integration for structure; composition primitive for detail-over-structure.

## Phasing
- **P1 — Composition primitive (A+B+E):** `refineToMicro` + overwrite/priority + micro-solidity validation.
  Fixes gaps for *all* finer-over-coarser cases at the source. Red test = micro-solidity on a wall the
  fireplace overlaps.
- **P2 — Structural elements (realizer):** build hearth/chimney (then stairs, built-ins) as structural
  masonry in the realizer, composing via P1. Fixes chimney #5/#7 and future built-ins.
- **P3 — Re-coarsening / density (D):** merge uniform fills; coordinate with greedy-mesh #40.

## Open questions
- Priority model: an explicit per-voxel `layer` tag, or inferred from the placement API path? (Simpler:
  bulk-template-spawn = fixture priority; realizer = structure priority.)
- Persistence: refined micros are heavier in the SQLite store; does P3 re-coarsening run before save?
- Does `spawnTemplateMicro` need to sort its own voxels (subcubes before microcubes) or does A make order
  irrelevant? (Goal: A makes it irrelevant.)
- Micro-occupancy API: extend `scan_micro`/add an in-engine watertightness check for E.

# Feature Design Keys

**Read this before designing or implementing any new engine feature.** It is the standing gate:
the questions below must be answered *in the plan*, not discovered during implementation.

The "Design Keys" and "Before you continue?" sections are the author's own words and are
authoritative. Everything after them is accumulated evidence for *why* each one matters, with the
specific failures that produced them.

---

## Design Keys

Game engine is voxel based and therefore new features must strive to match the voxel aesthetic.

The game engine is trying to capture considerably higher visual quality than minecraft in detail
(smaller voxels), better lighting, and better physics.

Engine voxels are primarily either static or dynamic voxels (with some exceptions).

Static voxels are stored and rendered based on "chunks" which is key to several optimizations to
allow for expansive worlds.

Static voxels come in 3 sizes: cubes, subcubes (1/3 cubes), and microcubes (1/9 cubes).

Dynamic voxels are dynamic physics bodies and designed to behave in physically realistic ways.

## Before you continue?

Does this feature fit into an procedural generation pipelines?
If so, adjust accordingly and make sure it doesnt negatively impact other features in other stages
of the pipeline.

Does this feature get exposed over an API?
If it does, is the interface robust and thought out?

Does the plan for the new feature also include a plan for how to visually test the new feature?

Does the test plan include a appropiatly simplified and small test world to streamline testing?

---

# Why each key matters

## Chunks are an optimization. They must not be visible.

> Static voxels are stored and rendered based on "chunks" which is key to several optimizations.

Chunks exist for **storage and rendering**. They are not a unit of appearance or behaviour. The
operative rule:

> **Appearance must be a pure function of world position and persistent world state. Per-chunk
> quantities may only bound COST — never how something looks.**

**The model to copy** — `GrassRenderPipeline::bladesForDistance`. It is per-chunk, but only as a
*conservative upper bound on vertex count*, computed from the chunk's **nearest** corner so it can
never clip something the shader wanted to draw. Cost bounded, appearance untouched.

**How this rule gets broken, with real examples:**

- **Deciding a visual property per chunk.** Grass density was once picked per chunk from the chunk
  centre; two adjacent chunks landing in different bands drew different densities and the boundary
  showed as a hard line through open field. Fixed by making density a continuous per-blade function
  of the blade's own world distance.
- **Deriving appearance by scanning a chunk's contents.** The grass edge taper counts grassy
  neighbours, and material is not queryable across a chunk border — so out-of-chunk is *assumed*
  grassy. Bounded at one voxel it is an acceptable trade; it is also exactly why the taper could not
  simply be made wider.
- **Binary visual decisions on chunk identity.** `RenderCoordinator.cpp` skips grass and foliage
  entirely on any chunk at LOD ≠ 0. This is why distance-driven chunk LOD stays default-OFF: its
  working window is the grass band, so enabling it would make meadows disappear in chunk-shaped
  patches. **An optimisation is blocked by an appearance-couples-to-chunk violation.**

**Two ways to satisfy the rule:**

1. **Derive it from world position** — continuous noise, hashes seeded on the absolute world cell,
   lattices aligned to the world rather than to the chunk. Seam-free by construction.
2. **Derive it in the generator** — a pure, order-independent function of world position, evaluable
   for any column without consulting a neighbouring chunk. `WorldGenerator::floraCellLayer` is the
   pattern: a per-cell predicate that reads only hashes of neighbouring *cell coordinates*, never
   any other cell's *result*, so a single chunk and a whole-region pass agree bit-for-bit.

Reaching for cross-chunk lookups to patch a chunk-local dependency is usually the wrong move — it
adds a stale-neighbour failure mode instead of removing the dependence.

**Pin it with a test.** The invariant has an established shape here:
`FloraMarginTest.WideCanopyNotClippedAtChunkSeams` asserts the union of per-chunk output equals a
whole-region pass; `FaunaPlanTest.DeterministicAcrossGeneratorsOfSameSeed` asserts a sub-window's
results appear in the full-region plan. Both are "chunking must not change the answer." Apply the
same shape to any new world-derived attribute — otherwise the seam is rediscovered visually, late.

## Match the voxel aesthetic — and use sub-voxel detail

Detail assets exploit subcubes (1/3) and microcubes (1/9). Full cubes are for coarse mass only. Any
small or detailed object — especially a handtool held in the fist — must be authored in microcubes.
Generators make detail **unconditional**, never behind a flag or a quality tier.

## Procedural generation pipelines

If a feature belongs in generation, put it there rather than bolting it onto rendering. Ask:

- **Which stage?** Terrain → hydrology → biome/material → decoration (flora/fauna) → structures.
  Order matters: a later stage may depend on an earlier one's output.
- **Does it disturb a later stage?** Changing surface material affects flora gating; changing height
  affects water and structure grounding.
- **Is it order-independent?** It must produce identical output whether evaluated for one chunk or a
  whole region, in any order. If evaluation order can change the result, it will seam.
- **Does it persist?** Per-world tuning belongs in the world recipe (`world.db world_meta`), which is
  the source of truth once a world exists — editing a global JSON must not silently change existing
  worlds.

## API exposure

If it is tunable, expose it deliberately:

- **Name the units.** World units, voxels, fraction-of-radius — say which, in the field comment.
- **Say what "unchanged" is.** Omitted/negative-means-unchanged is the convention in
  `/api/debug/*`; follow it rather than inventing per-endpoint semantics.
- **Echo the resulting state back** in the response so a caller can assert it took effect. A knob
  that silently does nothing against a stale binary has cost real debugging time.
- **Clamp at the boundary, and say why in the code.** Values that break an invariant must be
  clamped where they enter, with the failure they prevent written down — e.g. blades per voxel is
  capped at the lattice size because a wrapped index lands one blade exactly on top of another.
- **Defaults are a pinned contract.** `LodCharacterizationTest` exists because defaults drifted
  silently. Change one deliberately and update the pin in the same commit, with the reason.

## Visual test plan — and a small world to run it in

A feature is not planned until its test is planned. The plan must name:

1. **What "works" means**, as a measurable statement.
2. **The validation depth** — artifact exists (L1) · structural invariant measured on real output
   (L2) · functional simulation (L3) · live engine (L4).
3. **The red test** that proves it, shown failing first.

### Test-rig discipline

Hard-won; each line below is a specific failure that cost hours.

- **Small and isolated.** A 5×5 slab, not a 1.15M-voxel world. Big rigs are slow, and every extra
  object is a confound.
- **Keep the rig inside ONE chunk.** A slab spanning x/z −2..2 straddles four chunks; only one is
  resident in a fresh flat world, so 21 of 25 fills drop **silently** and the rig looks built but
  is not.
- **Verify the world, not the API response.** `/api/world/fill` over HTTP is asynchronous — it
  returns `{"status":"accepted"}`, with no placed count. Query the voxels back. `place_voxel`
  refuses to overwrite and returns `success:false`; a discarded response once produced a full table
  of zeros from an empty world that looked like a real measurement.
- **One variable, a stated prediction, and a control.** Write the prediction down *before* running,
  so the run can falsify it. The control is not optional: a grass-coverage metric reported "all
  blades distinct" at every density — the exact opposite of the truth — because it was measuring the
  grass voxel's own green top face. Only the control caught it.
- **The rig is not the game.** State the deltas between rig settings and shipped defaults, and what
  they do to the result. A single-blade rig sits at the density-compensation cap, making its blades
  ~2.6× wider than a blade in a real field, so every threshold it measures is optimistic.
- **Never trust a test run started before your edits.** It is validating a binary that no longer
  exists. Kill it and re-run.
- **Measure; do not infer from screenshots.** Eyeballing produced five consecutive wrong
  single-cause diagnoses for one bug that turned out to be two bugs masking each other. A/B the
  exact same frame with the feature on and off and diff the pixels.

### A note on instrumentation

Counters written into `lastFrameStats` from the shadow pass read back as zero, because `drawFrame()`
clears it *after* that pass. That produced a confident "0 casters submitted" when 189 were real.
When a measurement says something is impossible, suspect the measurement first.

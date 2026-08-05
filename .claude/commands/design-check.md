Run the Feature Design Keys gate against a proposed feature, BEFORE implementing it.

The feature under review is: $ARGUMENTS

Read [`docs/FeatureDesignKeys.md`](../../docs/FeatureDesignKeys.md) in full first. Then answer every
question below **about this specific feature**, with evidence from the codebase — not in the
abstract, and not by restating the question.

If the feature is already partly built, review what exists against the same gate and say plainly
where it fails.

## 1. Voxel aesthetic

- Does it match the voxel look, or does it import a smooth/organic idiom that will read as foreign?
- If it produces detail assets: are they authored in **subcubes/microcubes**? A detailed object built
  from full cubes is a defect, not a simplification.
- Is the detail **unconditional**, or hidden behind a flag/quality tier? It should be unconditional.

## 2. Chunk independence — the one most often failed

- List every quantity the feature uses that is **derived from a chunk** (its contents, its identity,
  its LOD level, its distance).
- For each: does it affect **appearance/behaviour**, or only **cost**? Anything affecting appearance
  is a defect — say so and propose the world-position or generator-derived alternative.
- Are you reaching for a **cross-chunk lookup**? If so, justify why the dependence cannot simply be
  removed instead. Cross-chunk plumbing adds a stale-neighbour failure mode.
- Name the **chunked-vs-whole-region equality test** you will write (`FloraMarginTest` /
  `FaunaPlanTest` shape). If you cannot state it, the feature is not ready to build.

## 3. Procedural generation

- Which pipeline stage does this belong to — terrain, hydrology, biome/material, decoration,
  structures? Or none?
- What does it consume from earlier stages, and what might it break in later ones?
- Is it **order-independent** — identical output whether evaluated per-chunk or whole-region, in any
  order? Show why, don't assert it.
- Does any per-world tuning need to persist in the **world recipe** so editing global JSON doesn't
  silently change existing worlds?

## 4. API surface

- What is exposed, in what units, and what does "unchanged" mean for each field?
- Does the response **echo back the resulting state** so a caller can assert it took effect?
- What values would **break an invariant**, and where are they clamped? Is the prevented failure
  written down at the clamp site?
- Are any **defaults** changing? If so, which pinned test must be updated in the same commit?

## 5. Visual test plan

- What does "works" mean, as a **measurable** statement?
- What **validation depth** is required — L1 artifact / L2 structural invariant on real output /
  L3 functional simulation / L4 live engine?
- What is the **red test**, and what exactly will it report when it fails?
- Describe the **test world**: small, inside ONE chunk where possible, with the *one* variable that
  moves, the **prediction written down in advance**, and the **control** that proves the metric is
  measuring the intended thing.
- How does the rig differ from **shipped defaults**, and what does that do to the numbers?

## Output

A short report, one heading per section above, ending with a clear verdict:

- **READY** — every question answered, test plan concrete.
- **NEEDS WORK** — list precisely what is unresolved.
- **REDESIGN** — a design key is violated in a way tuning cannot fix (say which).

Do not begin implementing. This command is a gate, not a build step.

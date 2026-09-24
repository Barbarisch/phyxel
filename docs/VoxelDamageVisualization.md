# Voxel Damage & Cracks — current-state reference

**What this is:** the reference for how a damaged-but-unbroken voxel shows its damage. It describes
the system **as shipped on `main`**. It is not a plan. Read §8 before changing anything here.

**History:** the full design plan, five design-check passes, build log (P0–P5) and measurements
that produced this system are preserved in git. See **`git show f109fd64:docs/VoxelDamageVisualization.md`**
(the last revision of the plan-era document). §10 summarises the decisions that still matter.

**Related:** [`DestructionSystemV2.md`](DestructionSystemV2.md) (what happens when a voxel BREAKS),
[`FractureModes.md`](FractureModes.md) (damage-source size, sub-voxel carving),
[`GlassTransparency.md`](GlassTransparency.md) (cracks on glass render in the transparent pass, §5),
[`LightingPipeline.md`](LightingPipeline.md) (`voxel.frag` is a lighting receiver).

---

## 0. In one paragraph

Every sub-threshold hit adds energy to the voxel's accumulated damage. The mesher divides it by
**the material's own break toughness**, quantizes the ratio to one of **7 visible stages**, and packs
the stage into instance bits 11–14. `voxel.frag` turns the stage into a **Voronoi fracture network
seeded from absolute world position**, so cracks run unbroken across voxel and chunk boundaries.
The network **widens** as damage grows; it never swaps pattern. Its density comes from the
material (`crackStyle`, from `brittleS1`), so glass cracks fine and steel cracks wide, which is how
the surface predicts how the voxel will break. A hit that moves a voxel across a stage boundary
re-meshes its chunk; a hit that doesn't costs nothing.

## 1. Data path

| step | where | what |
|---|---|---|
| accumulate | `DamageSystem::applyDamage` (`engine/src/core/DamageSystem.cpp`, graze branch) → `Cube::addDamage` | sub-threshold hits add `apply_damage` energy to `Cube::accumulatedDamage`. **Cube only** (§5) |
| normalize | `DamageSystem::responseFor(material).toughness` | damage ratio = energy / the material's break toughness, so a stage means the same fraction of the way to failure on Glass as on Steel. Materials without a `break` block fall back to `bondStrength × 120` |
| quantize | `Core::damageStage()` in `engine/include/core/DamageStage.h` | **the single source of truth**: the mesher and the graze re-mesh rule (§3) both call it, so they can't disagree about what's visible |
| pack | `ChunkRenderManager::rebuildCubeFaces` | `Core::packDamageStage()` spreads the stage over the 4-bit field (bits 11–14). Damage is part of the greedy-merge key, so a damaged voxel never merges with a pristine one |
| draw | `shaders/voxel.frag` → `crackField()` in `shaders/crack.glsl` | §4 |

**Instance `reserved` word is full:** bit 0 emissive, 1 transparent, 2–9 alpha, 10 mirror,
**11–14 damage**, 15 `varied`. The stage is clamped to 15 because a larger value would overflow
into bit 15 and hash-rotate the texture (pinned by `StageIsClampedBelowTheVariedFlagBit`).

**Not persisted.** Damage lives only on materialized `Cube` objects. It is lost when a chunk is
evicted or the world reloads.

## 2. Stage count: 7, set by measurement

`kDamageStagesVisible = 7` (`DamageStage.h`, which carries the full measurement table). It was 3
on cost grounds, and the legibility half of the P4 A/B overturned that:

| stages | damage-induced faces (24×10 wall) | low-damage legibility @ 12 u | blind below damage ratio |
|---|---|---|---|
| 15 | +196 | 5.14% | 0.033 |
| **7** | **+109** | **5.30%** | **0.071** |
| 3 | +51 | 2.67%, **on the 2.27% noise floor** | 0.167 |

- **The blind band is `0.5 / n` of toughness.** `damageStage()` rounds, so below that a voxel
  renders pristine however the shader is tuned. At 3 stages a voxel spent its first ~17% of life
  looking untouched. Asserted in `StageQuantizationAtEveryBoundary`.
- 7 matches 15's legibility within noise at 12/16/48/96 units for 56% of its face cost, so 15 buys
  nothing a viewer can see.
- **Runtime knob for A/Bs:** `POST /api/debug/damage_stages {"stages": n}`, clamped to
  `[1, kDamageStageMax]`, re-meshes resident chunks. The shipped value is the constant. The mesher
  reads the knob **once per rebuild** into a local, because it is an atomic read on the meshing thread.
- **Before changing the count:** re-measure with `tools/damage_stage_legibility.py`, and keep its
  noise-floor row. Without it, a floor reading looks like faint cracking.

## 3. Graze re-mesh rule

A hit that damages but does not break re-meshes the voxel's chunk **only when it moves the voxel
across a visible stage boundary** (`DamageSystem.cpp`, graze branch, same `damageStage()` and
denominator as the mesher). One blast can graze thousands of voxels, and dirtying them all would
be pure cost. The number of rebuilds is therefore bounded by visible changes: at most 7 per voxel
over its life.

It uses **`markChunkForRemesh`**, not `markChunkDirty`: damage has no DB field, and a DB-dirty mark
makes the streaming evictor re-save the chunk. Mass evictions of DB-dirty chunks are what produced
the multi-hundred-ms save stalls. Pinned by `VoxelGrazeRemeshIntegrationTest`.

## 4. The crack model (`shaders/crack.glsl`)

`crackField(worldPosAbs, faceNormal, stage01, style)` returns a crack mask in [0, 1]. **It is the
only implementation**, and the CPU mirror in `VoxelCrackSeamTest` is pinned to it.

- **Hard rule: world position only.** The inputs are absolute world position, face normal, stage
  and style. It must never read `texCoord`, `sizeU`/`sizeV` or anything else chunk-derived. UV
  space is tied to the greedy-merge rectangle, which ends at chunk borders; a UV-seeded crack
  shows up as **chunk-periodic repetition and scale changes between differently sized merge
  runs**. It does *not* show as a visible break at the seam: the cell size divides the merge
  lattice, so the junction lands on a crack line. `voxel.frag` reconstructs the position exactly
  as `vChunkBaseAbs + (inWorldPos − vChunkBaseRel)` (shared helper `phxWorldPosAbs`,
  `shaders/voxel_world.glsl`), the same camera-independent seed the `varied` tile hash uses.
- **Field:** Voronoi edge distance `F2 − F1` over a jittered 3×3 cell search, which traces a
  connected network of cell walls rather than blobs.
- **Cell size `kCrackCell = 1/3`** (the subcube lattice) for the primary network. It was measured,
  not chosen: at 1/9 the network was sub-pixel at 16 units and less legible than the flat
  darkening it replaced. A **second octave at ×3** (the microcube lattice) is admitted from stage
  ≈ 0.45, so fine branching lands where future spall chips would fall.
- **Stage widens the same network:** half-width `mix(0.012, 0.075, stage01)`. A voxel advancing
  through the stages shows the same cracks growing. The stage is per voxel, so crack WIDTH steps
  at a voxel boundary where stages differ while the geometry flows through. That is intended: it's
  how a player reads which voxel is closest to failing.
- **Darkening split (`voxel.frag`):** crack pixels ×0.18; a whole-face wear term ×0.78 at full
  damage, which keeps damage legible once the crack goes sub-pixel at distance; roughness →
  `max(crack, dmg·0.25)`. The old flat `×0.55` is what made damage read as grime.
- **Cost gate:** `if (dmg > 0.0)`. It bounds cost only; wherever there is damage, the detail is
  unconditional.
- **`crackStyle` per material:** `style = 0.75 + (clamp(s1, 1.3, 4.5) − 1.3) / 3.2 × 0.85`
  (`Core::crackStyleFor`), from `brittleS1`, so larger = sparser. It reaches the shader through the
  per-material props array at **stride 2**: `props[gi*2+1].x` (`AtlasManager.cpp`; read by
  `phxCrackStyleOf` in `voxel_world.glsl`). ⚠️ **The 0.75 floor is load-bearing:** below it the
  network falls back toward the microcube lattice, which is the sub-pixel failure above.

| material | `brittleS1` | style | cells per 1 m face |
|---|---|---|---|
| Glass | 1.3 | 0.75 | ~4.0, dense and fine |
| Stone | 1.8 | 0.88 | ~3.4 |
| Wood | 3.5 | 1.33 | ~2.3 |
| Steel | 4.5 | 1.60 | ~1.9, sparse and wide |

## 5. Where cracks render, and where they cannot

| surface | cracks? | why |
|---|---|---|
| full-cube static chunk faces (terrain, cube fills) | **yes** | the path above |
| **glass** (transparent materials) | **yes, frosted** | drawn by `transparent_voxel.frag` from the same `crackField` and exact seed, as bright frosting that raises local alpha. See [`GlassTransparency.md`](GlassTransparency.md) §4 |
| **subcube / microcube faces (every generated building wall)** | **no** | sub-voxel faces carry no damage bits, and damage state is cube-only. The V1 scope boundary; §9 has the costed V2 fix |
| kinematic voxels (doors, furniture, coherent fragments), GPU debris | **no, structurally** | both vertex paths hardcode `flags = 0` and zero the chunk-base seed. Extending them means widening two instance formats |

**Do not describe this feature as "cracks on voxels" without the sub-voxel qualifier, and do not
demo it on a generated building.**

## 6. API and debug views

- **`GET /api/world/voxel?x=&y=&z=`** returns, for a full cube: `material`, `damage_tracked`,
  `damage_energy` (apply_damage units), `toughness`, `damage01` (fraction of toughness),
  `damage_stage`, `damage_stages_max`, `damage_stage_bits`. For a subdivided cell: `exists` and
  `damage_tracked: false`.
- **`POST /api/damage/apply`** (MCP `apply_damage`): `{x, y, z, radius, energy, collapse}`.
- **`POST /api/debug/damage_stages {"stages": n}`**: the §2 runtime knob.
- **Debug view mode 19, the raw crack field:** `POST /api/debug/shadow {"mode": 19}`. Greyscale
  `crackField` at full strength on EVERY voxel (pristine included), with albedo, lighting and wear
  stripped. This is the only reliable way to judge the pattern: shaded frames hide it, because a
  pattern restart doesn't change brightness and stone albedo swamps structure. It was mode 11
  before the merge with main, where 11 is main's flat-grey probe; the editor clamp is 0–19.

## 7. Tests and tools

| | what it pins |
|---|---|
| `VoxelDamageStateTest` (`tests/core`) | accumulation, stage quantization at every boundary, clamp below the `varied` bit, packing, the stage-change predicate, toughness normalization, the echoed denominator |
| `VoxelCrackSeamTest` / `VoxelCrackStyleTest` (`tests/core`) | CPU mirror of `crackField`: independent of chunk partition, reads only its declared inputs, stage widens the same network, pinned sample table, `crack.glsl` tracked by the shader manifest; the style mapping is ordered and bounded and measurably changes density |
| `VoxelGrazeRemeshIntegrationTest` (`tests/integration`) | a graze re-meshes only on a stage crossing |
| `DamageSystemBreakProfileTest` | per-material break profiles (toughness, s1, s2) |
| `tools/damage_ladder_rig.py` | the manual review rig: a damage ladder, both looks (shipped AgX, and curve 0) |
| `tools/damage_stage_legibility.py`, `tools/damage_stage_ab.py` | stage-count legibility and cost A/B |
| `tools/crack_seam_test.py` | runtime seam rig. **Not a gate:** no shaded-frame statistic can detect a UV-seeded crack (§9) |

## 8. Rules for changing this system

1. **Any change to `voxel.frag`, `crack.glsl` or `voxel_world.glsl`:** run `.\build_shaders.bat`
   and **commit the regenerated `.spv`** (glslc ignores `#include` dependencies). Keep
   `tools/shader_manifest.py --check` green. `voxel.frag` is a lighting receiver, so update
   [`LightingPipeline.md`](LightingPipeline.md) §0 and §9 when its lighting changes.
2. **`crack.glsl` stays the single implementation.** Change it and the CPU mirror together, and
   regenerate the pinned table (`VoxelCrackSeamTest.DISABLED_RegenerateSampleTable`) only when the
   change to the pattern is intended.
3. **Never seed the field from anything chunk-derived** (§4). Judge pattern changes in debug
   mode 19, not in a shaded frame.
4. **Re-measure before changing the stage count or the style range** (§2, §4). Both were set by
   measurement, and the cheapest option always wins on the cost axis alone.
5. **Stage changes must still re-mesh through `markChunkForRemesh`** (§3), never `markChunkDirty`.

## 9. Known limits and open work

| item | state |
|---|---|
| **Sub-voxel damage (V2)**: cracks on generated buildings | **Open.** Costed: sub-voxels are stored sparsely (only existing cells allocate), so per-cell damage costs +8 B per existing sub-voxel (`Subcube` 120 → 128 B, `Microcube` 128 → 136 B; the float plus padding). **Recommendation: a per-parent-cube aggregate** (+8 B per subdivided parent cube). It matches V1's granularity, keeps one damage model, and doesn't fragment sub-voxel merge runs. On subdivision, **inherit the parent's damage** rather than resetting (today subdivision silently loses it). V2 is plumbing: write the damage bits into the two sub-voxel instance paths in `ChunkRenderManager` and give the aggregate a home (parent `Cube` vs a per-chunk side table, undecided). No crack-model change is needed, because the field is world-seeded. Also wanted by `FractureModes.md` |
| **Geometric spall (V1.5)** | Open; depends on V2. Chips would sit on the microcube lattice, where the second octave already cracks |
| **Automated seam guard** | **Not solved.** The chunk-seam invariant is settled visually (mode 19 shows one continuous network across x = 31/32) and pinned on the CPU side by `CrackFieldIndependentOfChunkPartition`. No pixel statistic on a shaded frame detects a UV-seeded crack: seven were tried. A correct guard must detect **repetition** (same-size merge rects rendering the same field region), not a seam edge; a first attempt was inconclusive (93.3 vs a 96.9 control) |
| **Legibility at the style extremes** | Open. The stage-count ladder ran on Stone only (style 0.88). Glass (0.75, at the clamp floor) and Steel (1.60) are unmeasured at 48/96 units. Cheap: parameterise `damage_stage_legibility.py` by material |
| **Persistence** | Damage is lost on chunk eviction and reload (§1) |

## 10. Decisions that outlive the plan (why it is this way)

- **World-position seeding** is what makes cracks continuous across chunk borders, and what lets V2
  reuse the same field on 1/3-scale faces without per-scale cases.
- **Normalize by the material's toughness,** not a global constant, so stage N means the same
  fraction of the way to breaking on every material.
- **7 stages:** the cost axis alone would have confirmed 3 (cheapest by 3.84×). The legibility axis
  found the blind band. When one axis always favours the cheapest option, it can't decide by itself.
- **`crackStyle` lives in the per-material props array** (stride 2), not in the instance word: the
  word is full, and the only reclaimable bits are the alpha precision glass depends on.
- **Cell size 1/3, not 1/9,** because 1/9 was invisible at play distance.

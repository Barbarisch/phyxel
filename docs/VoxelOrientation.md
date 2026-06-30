# Voxel Texture Orientation & Tiling Variation

> **Status:** design (2026-06-30). Phase A prototyped (see §6). Born out of the
> observation that **per-material/per-voxel tiling reads as a repetitive grid**,
> while the Phase-3 projected surfaces (one image across many voxels) look clean.
> Goal: make stuck-together voxels *blend* (no obvious cube grid), and optionally
> support *intentional* per-voxel orientation. Companion to
> [`docs/VoxelAppearanceModel.md`](VoxelAppearanceModel.md).

## 1. The problem (and what it actually is)

Place a wall of the same material and it reads as a grid of identical tiles. It's
tempting to call this a "seam" or "UV discontinuity" problem — it isn't. The real
cause is **1-unit repetition**: every cube shows the *same* full texture tile, so
the eye picks out the repeat even when the texture is perfectly seamless.

This matters because it's the difference between the two fixes:

- A *continuity* fix (align UVs across voxels) is mostly already done — and doesn't
  help, because the tiles still repeat.
- A *variation* fix (make each tile look a little different) is what actually breaks
  the grid.

## 2. Ground truth — what the renderer does today

Verified in `static_voxel.vert`, `voxel.frag`, `VulkanDevice.cpp`,
`ChunkRenderManager.cpp`:

- **The albedo/normal samplers are `VK_SAMPLER_ADDRESS_MODE_REPEAT`** (`VulkanDevice.cpp`
  ~2509). `voxel.frag::sampleVoxelPBR` samples `texture(arr, vec3(uv, L))` with no
  manual `fract()` — hardware REPEAT tiles each layer, with correct mips.
- **Greedy-merged cube faces already tile continuously.** A merged `sizeU × sizeV`
  quad gets `uv = baseUV * vec2(sizeU, sizeV)` (`static_voxel.vert` ~243); REPEAT
  wraps it. Because merged quads are integer-aligned, the tile *phase* lines up
  between neighbouring quads too → **no visible UV discontinuity between same-material
  cubes.** The grid look is repetition, not seams.
- **Sub/microcubes** (`scaleLevel` 1/2) use per-parent-cube sub-tile UV slices (1/3,
  1/9). Continuous within a parent cube, repeating per parent.
- **Bit budget is tight.** `InstanceData.reserved` is a `uint16` — bit0 emissive,
  bit1 transparent, bits2-9 alpha, bit10 mirror, bits11-14 damage — **only bit 15 is
  free.** The static path has no room to grow without enlarging the struct (and the
  dual-`InstanceData` footgun, see memory `reference-dual-instancedata-struct`).
- **Greedy meshing keys on appearance.** `ChunkRenderManager` `faceKey = (tex<<16) |
  (reserved | dmgBits)`. Anything that varies per-voxel and lands in this key **splits
  merges** → more faces → the engine's #1 perf issue (microcube density).
- **The three voxel paths share `voxel.frag`.** `kinematic_voxel.vert` and the dynamic
  path output `flags = 0`, so a `flags`-gated feature on the static path automatically
  spares props, rugs (Phase 3), debris, etc.

## 3. Three features (don't conflate them)

| Feature | What it does | Cost driver |
|---|---|---|
| **Continuity** | adjacent voxels form one continuous pattern, no edge | mostly already true for merged cubes; cheap to extend |
| **Variation** | break the repeat so a wall doesn't read as a grid | the real win; cheap if done in the fragment shader |
| **Intentional orientation** | author a per-voxel rotation/flip (planks, arrows, mosaics, laid floors) | needs a stored per-voxel field; fights greedy meshing |

## 4. Two delivery mechanisms

**(a) Stored per-voxel orientation field** (2–3 bits: 4 rotations ± mirror = up to 8
dihedral). Conceptually clean, but on the static path it is the *expensive* option:
no free bits (struct grow + dual-struct edit), **and** putting orientation in the
greedy key makes randomly-oriented voxels stop merging → face explosion. Justified
only for **intentional** orientation (bounded, authored regions) — and it is *cheap*
on the kinematic/prop path (48 B `KinematicFaceData`, no greedy meshing; the Phase-3
`uvScale` work is the precedent).

**(b) Procedural hash-rotation in the fragment shader.** Rotate each tile by a hash of
its **world cell coordinate**, computed in `voxel.frag`. **No per-voxel data, no struct
change, and it never touches the merge key** — a merged quad still rotates each 1-unit
cell independently at shading time. This is "stochastic texture rotation" / texture
bombing. It is the right tool for **variation** and is essentially free.

> **Rule of thumb:** variation → procedural (mechanism b). Intentional → stored field
> (mechanism a), kinematic path first.

## 5. Phased plan (cheapest-highest-value first)

- **Phase A — procedural variation (frag shader, per-material opt-in).** Hash-rotate
  tiles for materials flagged `"varied"` in `materials.json`. Natural surfaces
  (Dirt/Stone/Sand/Gravel/Grass/Sandstone/Ice) on; directional ones
  (Wood/Log/Bricks/StoneBricks/paving) off. Zero memory cost, merging preserved.
  **Prototyped — see §6.**
- **Phase B — continuity polish.** Derive unmerged/sub/microcube UV from world position
  so phase aligns *everywhere*, not just inside merged quads. Shader UV math; no struct
  change. (Largely cosmetic given §2 — do only if a real discontinuity is found.)
- **Phase C — authored per-voxel orientation (stored field).** 3 dihedral bits.
  *Kinematic path first* (cheap): add to `KinematicVoxel`, parse a `.voxel` token
  (`rot=90` / `flip=x`), expose in the editor. *Static path later* (grow `reserved`
  u16→u32, both `InstanceData` structs + `attributeDescriptions`, red-before-green);
  accept the merge cost only for deliberate, bounded patterns.
- **Phase D — connected textures (CTM), optional/future.** Neighbour-aware border tiles
  so glass panes / bookshelves read as one surface. Heaviest (context-dependent tile
  selection); only for that specific look.

## 6. Phase A spec (the prototype)

**Gate.** `MaterialDef.varied` (bool, from `materials.json` `"varied": true`). The static
mesher (`ChunkRenderManager`, cube path) ORs **`reserved` bit 15** when the material is
varied. It is *constant per material*, so it does **not** split greedy merges. Sub/microcube
paths and kinematic/dynamic (`flags = 0`) never set it → props, rugs, debris untouched.

**Rotation (`voxel.frag::sampleVoxelPBR`, when bit 15 set):**
1. Project `inWorldPos` onto the face plane (axes chosen by `inNormal`) → continuous
   in-plane coords `p`.
2. `tile = floor(p)`, `local = fract(p)`; `hash(tile)` → a 90° rotation step (0–3) and a
   flip. 8 dihedral variants per cell.
3. Rotate `local` (and the corresponding gradients) about the tile centre; sample albedo
   **and** normal with `textureGrad` using the rotated gradients (avoids the mip seam that
   per-tile `fract` rotation otherwise causes).
4. Rotate the sampled tangent-space normal's `xy` by the same step so lighting stays
   consistent with the rotated detail.

Cubes only for now (sub/microcube sub-tile UV rotation is Phase C-adjacent).

**Tradeoff (inherent).** Differently-oriented neighbouring cells don't share an edge, so a
faint **inter-tile seam** replaces the repeat. For non-directional natural textures this is
far less noticeable than the grid; for structured/directional textures it looks wrong —
hence the per-material opt-in.

## 7. Risks / open questions

- **Mip seams** at tile boundaries from rotated UV → mitigated by `textureGrad` with
  rotated-but-continuous gradients. Verify at distance.
- **Directional materials must stay off** `varied` (planks/bricks/paving). Default is off.
- **Normal-vector rotation** must match the UV rotation or relief lights wrong; cheap for
  90° steps + flip.
- **Seam vs repeat** is a judgment call per material — tune which materials opt in by eye.
- **Sub/microcubes** deferred; they're mostly decorative props where Phase-3 projected
  surfaces already give continuity.

## 8. Related
- `docs/VoxelAppearanceModel.md` — substance/tint/state/surface model; Phase 3 projected
  surfaces (the "one image across many voxels" path).
- `docs/RenderOptimization.md` — face-count / greedy-meshing perf (why mechanism b matters).
- `reference-render-pipeline-internals`, `reference-dual-instancedata-struct` (memory).

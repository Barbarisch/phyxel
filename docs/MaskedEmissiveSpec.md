# Masked-Emissive Rendering — Spec (enchanted-log glowing cracks)

> Goal: a material can be **lit normally AND emit light from part of its texture** — e.g. an
> "enchanted log": ordinary bark that is normally sun/shadow-lit, with **glowing cracks** that emit a
> colored light *and cast that colored light into the surrounding world* (magical forest ambiance).
> This replaces the rejected subcube-geometry glow veins (which looked bad and cost primitives).

## 1. Why the current emissive path can't do this

`voxel.frag:296-304`: if a material's `emissive` flag is set, the fragment outputs
`albedo × emissiveMultiplier × blockColorHue` and **`return`s — skipping sun, shadow, ambient, and
PBR entirely**. So flagging a bark+cracks texture `emissive` makes the *whole face* flat self-lit: the
cracks glow but the bark loses all normal shading. We need **normal lighting PLUS an additive emission
term gated to the crack pixels.**

## 2. Two load-bearing constraints (must be resolved by this spec)

1. **The per-face flags word is 100% full.** The 16-bit `InstanceData.reserved` (`Types.h:85`) is fully
   assigned: bit0 emissive · bit1 transparent · bits2-9 alpha(8) · bit10 mirror · bits11-14 damage ·
   bit15 varied. No enum; hand-packed literals in three CPU packers (`ChunkRenderManager.cpp:226`,
   `:854`, `:1008`). **There is no spare bit** for a "masked-emissive" flag without freeing one.
2. **Block-light seeding is all-or-nothing on `material.emissive`, colored only by `colorTint`.** The
   colored block-light bake (`ChunkRenderManager.cpp:349-353` cubes, `:360-379` sub/micro) only seeds
   light for voxels whose material has `emissive=true`, and picks the light color from
   `physics.colorTint` (already used as an albedo multiplier). A *partially* emissive log with
   `emissive=false` would **cast no light** under today's code. This must change for the cracks to
   glow the forest.

Everything else mirrors the existing **normal+roughness (NR) parallel array** added in Phase 2
(`docs/TextureSystemOverhaul.md`), which is the ideal template to clone.

## 3. Key design move: NO new flag bit — drive it from the material + a mask that defaults to "off"

We avoid the full-flags-word problem entirely by **not using a per-face flag**. Masked emission is a
**material property**, and the emission source (mask/luminance) is zero for ordinary materials, so the
shader can *always* add the emission term with no per-face branch:

- The `emissive` bit (bit0) keeps its current meaning (full self-lit glow blocks — unchanged).
- An enchanted material sets `emissive = FALSE` (so it takes the normal lit path) and instead declares
  masked-emission via new material fields (§5). Ordinary materials contribute zero emission.
- Block-light seeding is gated on the **material property** (CPU-side, `md->…`), not a per-face flag —
  so no flag bit is needed there either.

This defers the (invasive, 3-packer) flags surgery unless per-fragment cost later proves it necessary
(§7 perf note).

## 4. Two implementation options for the emission source

Both share the same shader integration (§6) and block-light change (§7); they differ only in *where the
"which pixels glow" signal comes from*.

### Option A — luminance-gated (no mask texture) — RECOMMENDED FIRST
The emission is derived from the **albedo's own brightness** above a per-material threshold:
```
emission = smoothstep(thr, 1.0, luminance(albedo)) * albedo * emissiveColor * emissiveMultiplier
```
The enchanted-log **albedo texture** is authored so the cracks are the brightest pixels (bark kept
below `thr`). Emission tints toward `emissiveColor` (e.g. green).
- **Pros:** zero new texture array, zero descriptor/VRAM changes, zero AtlasManager/VulkanDevice work.
  Shader + material fields + block-light bake only. Fastest path to seeing it in-world.
- **Cons:** less precise — any bright albedo pixel glows; the artist must keep only cracks bright.
- **Touch:** `voxel.frag`, `MaterialRegistry` (new fields), `ChunkRenderManager` (bake), materials.json.

### Option B — dedicated emissive-mask array (clone the NR array) — UPGRADE
A parallel `sampler2DArray` (bindings **8/9**, same layer index as albedo) holds an artist-painted mask
(mostly black; crack pixels = emissive color/strength). `_em.png` sidecar convention next to each
albedo PNG (exactly like `_nr.png`), single-channel → **BC4** (~0.5 B/texel, ~36 MB for the whole set;
mostly-black data).
```
emission = texture(emMask, vec3(uv, layer)).r * albedo * emissiveColor * emissiveMultiplier
```
- **Pros:** precise, artist-controlled; independent of albedo brightness.
- **Cons:** clones the full NR machinery — AtlasManager build/upload/cache, VulkanDevice image +
  descriptor bindings 8/9 + pool + writes, +VRAM, one extra texture fetch per fragment.
- **Touch:** everything in A **plus** `AtlasManager.{cpp,h}`, `VulkanDevice.{cpp,h}`, descriptor layout.

**Recommendation:** ship **A** first (validate the look + block-light glow with ~3 files), then upgrade
to **B** only if the luminance gate proves too coarse for the final art.

## 5. Material schema additions (`resources/materials.json` + `MaterialRegistry.cpp`)

Add to the scalar parse block (`MaterialRegistry.cpp:100-125`) + `saveToJson` (`:139`):
- `emissiveColor: [r,g,b]` — the glow hue (distinct from `colorTint`, which stays an albedo mult).
- `emissiveStrength: float` (default 0) — **> 0 enables masked emission**; scales both the shader term
  and the block-light seed. (0 everywhere = no behavior change for existing materials.)
- Option A: `emissiveThreshold: float` (default ~0.75) — luminance gate.
- Option B: `_em.png` sidecar convention (no schema field) OR explicit `emissiveMask` per-face in
  `parseTextures` (`:55`).

New material `enchanted_log` (bark albedo like `Log` + bright cracks), `emissive:false`,
`emissiveStrength:1.5`, `emissiveColor:[0.3,1.0,0.4]`.

## 6. Shader change (`shaders/voxel.frag`) — the core

- **Do NOT take the `:296` early-out** for masked materials (they have `emissive=false`, so they
  already fall through to the normal lit path `:306-328` — no change needed to reach it).
- After the lit `color` is accumulated (before `outColor`, ~`:365`), add:
  ```glsl
  // masked emission: glow from crack pixels, bloom-catching, tinted by emissiveColor
  float e = (option A) smoothstep(uThr, 1.0, luminance(albedo))
          : (option B) texture(emMask, vec3(suv, layer)).r;
  color += e * albedo * uEmissiveColor * ubo.emissiveMultiplier;
  ```
- `uThr` / `uEmissiveColor` reach the frag via the per-material path. Cleanest: fold `emissiveStrength`
  and `emissiveColor` into the existing **per-face** data the frag already has — but the flags word is
  full and `textureIndex` is a bare layer. Simplest wiring: a small **per-material SSBO** (indexed by
  material id, or by texture layer) holding `{emissiveColor.rgb, emissiveStrength, threshold}`. The
  frag already reads an SSBO at set0/binding4 (`AtlasUVBuffer`); add a parallel material-params SSBO,
  or extend that one. (The layer→material mapping is deterministic: 6 layers per material,
  `MaterialRegistry::assignAtlasIndices:176-194`.)
- `luminance(albedo)` where `albedo` is the already-sampled base color in `sampleVoxelPBR`
  (`voxel.frag:91-136`).
- Recompile: `.\build_shaders.bat` (manually recompile `voxel.frag` — glslc doesn't track `#include`).

## 7. Block-light: make the cracks glow the forest (`ChunkRenderManager.cpp`)

This is what casts the colored light — the magical part. Today (`:229-239`) the per-material emissive
light color is computed `if (em && md)` from `colorTint`. Change:
- Compute the seed for materials with `emissiveStrength > 0` too (not just `emissive==true`), using
  **`emissiveColor`** and scaling intensity by `emissiveStrength` (a thin-crack log should seed *less*
  light than a full glow block — tune the seed level down for masked materials since only a fraction of
  the surface emits).
- Seed at the existing sites: cubes `:349-353`, sub/micro `seedVoxelLight` `:360-379` — extend the
  `emissive` gate to `emissive || emissiveStrength>0`.
- Output already flows to `vBlockColor` → per-corner light → `voxel.frag:328`. No shader change for the
  *received* light; only the seeding gate/color.

**Perf note:** Option A adds a `smoothstep+luminance` per fragment (cheap, no fetch). Option B adds one
texture fetch per fragment for *all* voxels (the mask array, black for most). If dense-scene profiling
shows the fetch hurts, *then* free a flag bit (narrow alpha bits2-9 from 8→4) to gate the fetch — but
not before it's measured.

## 8. How the trees use it (`tree_forge.py`)

tree_forge already parameterizes the trunk material: `PRESETS["enchanted_oak"]` (or an override) sets
`log_mat="enchanted_log"`. Enchanted-forest giants generate with that log material; the stamped voxels
render via the **static chunk path** (placed templates become chunk voxels — confirmed: a spawned tree
voxel queries as a normal chunk cube), so **only the static packers/bake (§6-7) are needed for trees.**
Kinematic/dynamic paths (glowing furniture/debris) are optional future work.

## 9. Validation plan (red-before-green)

- **Shader unit-ish:** a materials.json fixture with `enchanted_log` (green, strength 1.5); render a
  single stamped enchanted log; `get_visual_diagnostic` — assert the bark is *shaded* (has a
  light/dark gradient from the sun, i.e. NOT flat) AND crack pixels are brighter than bark. A flat
  (fully-emissive) result fails the "bark is lit" half.
- **Block-light cast:** place an enchanted log in a dark enclosed cell; assert neighboring non-emissive
  voxels receive green `vBlockColor` (query render/lighting or sample a screenshot pixel) — proving the
  cracks cast light, not just glow themselves. Compare against the same cell with plain `Log` (no cast).
- **No-regression:** existing `glow`/`glow_green`/`burning_wood` still render as before (full emissive
  untouched); ordinary materials with `emissiveStrength=0` are byte-identical in output.
- Solution-auditor + grounding pass on any authored dimensions/strengths.

## 10. Touch list (recommended = Option A)
1. `resources/materials.json` — `enchanted_log` + `emissiveColor`/`emissiveStrength`/`emissiveThreshold`.
2. `engine/src/core/MaterialRegistry.cpp` — parse/serialize the new fields (`:100-125`, `:139`).
3. per-material params SSBO — `MaterialRegistry` (fill) + `VulkanDevice`/`RenderCoordinator` (upload) +
   `voxel.frag` (read). *(Or, minimal: reuse `colorTint`+a strength packed into an existing buffer.)*
4. `shaders/voxel.frag` — additive masked-emission term (§6); `build_shaders.bat`.
5. `engine/src/graphics/ChunkRenderManager.cpp` — extend emissive block-light seed to
   `emissiveStrength>0` with `emissiveColor` (`:229-239`, `:349-353`, `:360-379`).
6. `tools/tree_forge.py` — `enchanted_log` log-material preset for enchanted-forest giants.
- Option B additionally: `AtlasManager.{cpp,h}` (em array), `VulkanDevice.{cpp,h}` (bindings 8/9),
  `_em.png` sidecars.

## 11. Risks / open questions
- **Per-material params to the frag:** the flags word is full and `textureIndex` is a bare layer, so
  `emissiveColor`/`strength` must ride a **material-indexed SSBO** (cleanest) — confirm the layer→
  material map is stable and add the buffer. This is the main new plumbing in Option A.
- **Block-light intensity for thin cracks:** a masked log emits from a small surface fraction; seeding
  it at full glow-block strength would over-light. Needs a tuned `emissiveStrength`→seed curve.
- **Descriptor binding budget** (Option B only): bindings 8/9 add two samplers — check device limits
  (already flagged in `TextureSystemOverhaul.md:214`).
- Hot-reload (`MaterialRegistry`/`AtlasManager` reload paths) must carry the new fields.

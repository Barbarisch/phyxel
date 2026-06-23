# Texture System Overhaul — High-Res PBR + Per-Object Texturing

> Status: **PLAN / not started.** Decisions locked (see §1). Goal: kill the 64px
> "Minecraft" aesthetic and support detailed, immersive, per-object texturing —
> without bloating the performance-critical static voxel instance path.

## 0. Where we are today (ground truth, verified 2026-06-22)

- **Resolution:** `AtlasManager`: `TEXTURE_SIZE = 64`, `PADDING = 1`, `TEXTURES_PER_ROW = 6`,
  `MAX_ATLAS_SIZE = 2048`. 64×64 px per texture is the entire root of the blocky look.
- **Container:** single 2D atlas — `VulkanDevice` creates `textureAtlasImage` as
  `VK_IMAGE_TYPE_2D`, `VK_IMAGE_VIEW_TYPE_2D`, `VK_FORMAT_R8G8B8A8_SRGB`,
  **`mipLevels = 1` (no mips → distance aliasing), `arrayLayers = 1`.**
- **Indexing:** `InstanceData` (8 B): `packedData` (u32) + `textureIndex` (**u16**) + `reserved` (u16).
  The u16 face texture handle already addresses 65,535 textures. **No spare room to grow this struct.**
  - `DynamicSubcubeInstanceData` (64 B) and `KinematicVoxelManager::KinematicFaceData` (40 B)
    also carry `textureIndex` and **have headroom** for extra per-face UV data.
- **Sampling:** `voxel.frag` (shared by static/dynamic/kinematic; sibling variants
  `mirror_voxel.frag`, `transparent_voxel.frag`) reads atlas UV bounds from an SSBO
  (set 0, binding 4), does `fract()` tiling + half-texel inset to avoid bleed.
  Subcube/microcube UV math in `static_voxel.vert` computes a 0..1 coord *within* the tile.
- **Shading:** Blinn-Phong. `metallic`/`roughness` exist per-material in `materials.json`
  but the shader **ignores them** — only `albedo × light`.

## 1. Locked decisions

| Decision | Choice |
|----------|--------|
| Lighting model | **Full PBR** — albedo + normal + ORM (occlusion/roughness/metallic), Cook-Torrance |
| Resolution | **Mixed**: 512² terrain/materials, 1024² objects/items |
| First texture source | **CC0 PBR libraries** (ambientCG / Poly Haven), LLM gen second |

## 2. The core architectural move: atlas → texture array

Replace the packed 2D atlas with **`sampler2DArray`** (one layer per texture). This is the linchpin:

- `textureIndex` (u16) → array layer **directly**. No growth to the 8-byte static struct.
- Deletes atlas packing: no UV-bounds SSBO, no `fract()` wrap, no half-texel inset, no seam bleed.
  Sampling becomes `texture(arr, vec3(uv, layer))` — simpler and slightly faster than today.
- Per-tile mipmapping + REPEAT wrap work correctly (impossible with a packed atlas — that's
  why mips are off now). Fixes distance aliasing for free.
- Scales to thousands of textures instead of fighting a 2048² atlas budget.

### Mixed-resolution implication
Array layers must be uniform size, so "512 terrain / 1024 objects" = **two array sets**:
- `albedo512[] / normal512[] / orm512[]` and `albedo1024[] / normal1024[] / orm1024[]`.
- Selector = **top bit of `textureIndex`**: bit15 = res class (0 = 512, 1 = 1024),
  bits 0–14 = layer (32,768 per class). Frag branches on the bit. 8-byte struct unchanged.

### VRAM budget (why compression is mandatory)
- 512² RGBA8 × 256 layers ≈ 256 MB (+~33% mips). ×3 for PBR maps → ~1 GB. Untenable raw.
- **Offline BC7 (albedo) + BC5 (normal) + BC4/BC7 (ORM) via KTX2.** BC7 ≈ 1 B/texel:
  512² × 256 ≈ 64 MB; PBR set ≈ ~130 MB. 1024² object set is smaller (far fewer layers).
- Vulkan: enable `textureCompressionBC` device feature; load KTX2 (libktx or hand-rolled).

## 3. Phasing

### Phase 1 — Container swap + high-res base materials (the immediate visual win)

> **SPIKE STATUS (branch `feature/texture-array-pbr`, 2026-06-23): container swap DONE +
> verified in-engine. Uncommitted.** The atlas→`sampler2DArray` migration is complete at the
> existing 64px res with trilinear mips. Verified: fresh Perlin terrain renders grass/dirt/
> stone/bricks correctly, distant brickwork is smooth (mips working), all 3 voxel paths +
> glass render fine, log confirms `Texture array uploaded (162 layers @ 64x64, 7 mips)`,
> 6/6 AtlasManager unit tests pass, no Vulkan validation errors.
> What changed: `AtlasManager` is now layer-major (one TEXTURE_SIZE² layer per textureIndex,
> uvBounds kept only as SSBO count/fallback metadata); `VulkanDevice::uploadTextureArray()`
> creates a 2D_ARRAY image with a full blit-generated mip chain (falls back to no-mips if the
> format lacks linear-blit support); sampler switched to LINEAR + trilinear (anisotropy left
> off — device feature not enabled); `voxel.frag`/`mirror_voxel.frag`/`transparent_voxel.frag`
> now declare `sampler2DArray` and sample `texture(arr, vec3(uv, layer))`.
> **UPDATE (commit 2, 2026-06-23): 512px CC0 re-source DONE + verified.** `TEXTURE_SIZE`
> bumped 64→512; `loadPNG` now bilinear-resamples any source size to the layer size (so kept
> low-res sources coexist with native 512 tiles). New `tools/fetch_cc0_textures.py` pulls CC0
> material bundles from ambientCG (1K PNG → downscaled to 512), writes per-face albedo into
> `resources/textures/source/`, and caches Normal/Roughness/AO maps under
> `resources/textures/pbr/` (gitignored) for Phase 2. 12 core materials re-sourced
> (Dirt/Grass/Stone/Cobblestone/StoneBricks/Sand/Gravel/Wood/Bricks/Sandstone/Metal/Ice +
> grass biome variants; Grass uses grass-on-top / dirt-on-sides). Provenance in
> `resources/textures/source/CC0_SOURCES.json` (all CC0). Verified in-engine: grass field +
> burgomaster building stonework render at high res (log `162 layers @ 512x512, 10 mips`,
> 551 FPS, no validation errors); 4/4 AtlasManager tests pass.
>
> **UPDATE (commit 3, 2026-06-23): BC7 compression DONE + verified.** Vendored `bc7enc`
> (MIT/public-domain, `external/bc7enc/`) into phyxel_core; enabled the `textureCompressionBC`
> device feature (RGBA fallback if absent). `AtlasManager::encodeBC7()` CPU-generates the mip
> chain per layer and BC7-encodes it (multithreaded across layers, ~6.2s for 162×512²);
> `VulkanDevice::uploadTextureArrayBC7()` uploads a `VK_FORMAT_BC7_SRGB_BLOCK` 2D array (one
> copy region per precomputed mip, no GPU blit). Result: **~54 MB VRAM vs ~226 MB raw (4×)**,
> renders identically (verified close-up: stone blocks, wood grain, no artifacts), 441 FPS.
> A **disk cache** (`cache/textures/voxel_bc7.bin`, gitignored, keyed by a hash of source-file
> metadata + materials.json + format version) makes only the first launch pay the encode:
> subsequent launches load in ~55ms. `uploadToGPU` now does load-cache → else encode+write →
> upload BC7, falling back to RGBA if the device lacks BC.
>
> **STILL TODO in Phase 1:** the two-array mixed-res split (512 terrain / 1024 objects via the
> textureIndex top-bit selector), and re-sourcing the remaining materials (Log/Leaf variants,
> Gold, Sandstone tuning; Glass/glow/Mirror stay special-cased).
>
> ### ambientCG asset map (material → asset ID, all CC0)
> Dirt=Ground003 · Grass=Grass004(top)/Ground003(sides) · Stone=Rock030 ·
> Cobblestone=PavingStones128 · StoneBricks=PavingStones070 · Sand=Ground027 ·
> Gravel=Gravel022 · Wood=WoodFloor007 · Bricks=Bricks075A · Sandstone=Rock035 ·
> Metal=MetalPlates006 · Ice=Ice001. Edit `ASSETS` in `tools/fetch_cc0_textures.py` to retune.
- Migrate `AtlasManager` → a layered "TextureArrayManager" (keep name or rename): build
  per-layer images instead of blitting into one atlas; drop UV-bounds SSBO.
- `VulkanDevice`: create `VK_IMAGE_VIEW_TYPE_2D_ARRAY` images (512 set + 1024 set),
  `mipLevels = full`, generate mips on upload, BC7/BC5 sampling.
- Update `static_voxel.vert` (UV stays 0..1; pass layer), `voxel.frag` +
  `mirror_voxel.frag` + `transparent_voxel.frag` (sample array, branch on res bit),
  `dynamic_voxel.vert`, `kinematic_voxel.vert`.
- Re-source the 19 materials at 512 from CC0 libs, seamless. Add mips.
- **Outcome: kills the Minecraft aesthetic on its own.**

### Phase 2 — PBR shading
- Parallel `normal` + packed `ORM` arrays (same layer index).
- `voxel.frag`: Blinn-Phong → Cook-Torrance, using existing sun + point/spot lights.
  Normal maps make a flat voxel face read as carved stone / woodgrain — most of the
  "immersive" payoff. Gate behind a quality toggle. Scalar `metallic`/`roughness` from
  `materials.json` become per-material fallbacks when a map is absent.

### Phase 3 — Per-object / per-template textures
- Rich UV-mapped images (painting, sign, book cover, unique furniture skin) live in the
  **kinematic/dynamic** path (40 B / 64 B structs with room for `uvOffset`/`uvScale`/layer),
  **not** the tight 8-byte static path → chunk rendering untouched.
- Templates reference a custom texture set → just more array layers (1024 class).
- `materials.json` schema gains optional `normal`/`orm`/`resolution` fields per texture.

### Phase 4 — Generation pipeline (feeds the same KTX2 arrays + registry)
- **CC0 importer (first):** pull ambientCG/Poly Haven PBR sets (ambientCG has an API) →
  make seamless → BC-compress to KTX2 → register material/texture entry.
- **LLM image-gen (second):** text→albedo via an external image model (Claude does **not**
  generate images — needs SD/Gemini/fal endpoint), then derive normal/roughness/AO from
  albedo and make tileable.
- Likely a `tools/texture_pipeline.py` evolving the existing `tools/texture_atlas_builder.py`.

## 4. Touch list (files)
- `engine/src/core/AtlasManager.{cpp,h}` — core rewrite (layered build).
- `engine/src/vulkan/VulkanDevice.cpp` (~L940–2210 atlas image/view/upload/descriptor),
  `engine/include/vulkan/VulkanDevice.h`.
- `shaders/voxel.frag`, `mirror_voxel.frag`, `transparent_voxel.frag`,
  `static_voxel.vert`, `dynamic_voxel.vert`, `kinematic_voxel.vert` → `.\build_shaders.bat`.
- `engine/src/core/MaterialRegistry.cpp` (schema: normal/orm/resolution).
- `resources/materials.json`, `resources/textures/source/` (new high-res + map PNGs).
- `tools/texture_atlas_builder.py` → texture pipeline.
- `engine/include/core/Types.h` — `textureIndex` res-class bit convention (doc only; no size change).

## 5. Open questions / risks
- Descriptor set binding budget when adding 6 array samplers (512/1024 × albedo/normal/orm) —
  confirm against device limits; consider combining ORM res classes.
- Fragment overdraw cost of 3 texture fetches × PBR math on dense voxel scenes — measure with
  the engine-perf skill; keep the quality toggle.
- KTX2 loader choice: libktx dependency vs. minimal in-house BC reader.
- Hot-reload parity (`AtlasManager::hotReload`/`reloadMaterial`) must survive the rewrite.

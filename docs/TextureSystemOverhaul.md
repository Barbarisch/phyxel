# Texture System Overhaul — High-Res PBR + Per-Object Texturing

> Status: **PHASES 1–2 DONE + MERGED TO MAIN** (8 commits, 2026-06-23). High-res CC0 PBR
> textures, normal-mapped relief, per-material metallic, mixed-res 512/1024 two-array split,
> BC7 compression + cache, and a damage→roughness prototype are all live and verified. The
> 64px "Minecraft" aesthetic is gone. Detail/progress is in the per-phase STATUS callouts below.

## Remaining / saved for later (2026-06-23)
Picked-up order is up to priority; all are independent. None block anything.
- **Environment reflection / IBL for metals** — the only remaining item with real *visual* upside.
  Metal/Gold work (per-material metallic landed) but look dark except in direct light because
  there's no environment term. A cheap fake-env or a real IBL probe would make metals read richer.
- **AO maps** — cached per-asset under `resources/textures/pbr/` (gitignored); minor for flat-lit
  voxel faces (SSAO likely already covers contact). Would need a channel/binding (or pack via BC5).
- **BC5 for normals** — ~36 MB VRAM saving, *no visual change*; requires splitting the current
  normal+roughness (BC7 RGBA) packing since BC5 is 2-channel.
- **Refine per-face tangent** — the TBN in `voxel.frag` is approximate (stable per face but fine
  normal-feature orientation may differ); match `static_voxel.vert`'s per-face UV axes for exactness.
- **Damage-roughness prototype → product** — normalize damage by each material's break *toughness*
  (lives in `DamageSystem`, not the mesh build) instead of the `kDamageRef=30` placeholder; and
  extend it to **placed-object templates** (they render via a different mesh path than chunk terrain).
- **Re-source stragglers** — ~~leaves (5 variants) still 64px~~ **RESOLVED (verified 2026-07-21):**
  all 5 leaf variants (`leaf_top.png`, `leaf_autumn_top.png`, `leaf_birch_top.png`,
  `leaf_jungle_top.png`, `leaf_spruce_top.png`, and their side/bottom faces) are now **512×512**,
  generated procedurally via `tools/gen_nature_textures.py`/`tools/leaf_forge.py` (not opaque CC0
  photos) — the tiling concern this bullet raised no longer applies. Materials remain class-0/512px
  (no `"resolution":1024"` in `materials.json`), just no longer low-res. Birch/spruce logs using
  generic bark and Gold's albedo are still open (unverified against current textures in this pass).
- **Phase 3 (original plan, not started)** — unique per-object/template 1024 textures (paintings,
  signs, hero props): the whole reason the 1024 class exists. Lives in the kinematic/dynamic path
  (40 B/64 B structs have room for uvOffset/scale), NOT the tight 8 B static path.

> ORIGINAL PLAN BELOW (decisions locked in §1; kept for reference/context).

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
> **UPDATE (commit 4, 2026-06-23): mixed-res two-array split DONE + verified.** Phase 1 complete.
> `textureIndex` (u16) now encodes the resolution class in bit 15 (`RES_CLASS_BIT`) and the
> within-class layer in bits 0..14 (`LAYER_MASK`); `MaterialDef.resolution` (512 default / 1024)
> drives per-class index assignment in `MaterialRegistry::assignAtlasIndices`. `AtlasManager`
> builds/encodes/caches TWO arrays independently (`atlas_[2]`; caches `voxel_bc7_512.bin` +
> `voxel_bc7_1024.bin`, cache version → 2). `VulkanDevice` adds a second 1024 image at
> **descriptor binding 5** (layout/pool updated; `uploadTextureArray*` take a `target` class;
> `updateAtlasUVBuffer` carries per-class counts). `voxel.frag` samples binding 1 or binding 5
> on the class bit with per-class bounds + placeholder fallback (mirror/transparent unaffected —
> they don't sample the array). `materials.json`: StoneBricks/Wood/Bricks → `"resolution":1024`,
> re-fetched at native 1024 by the fetch tool (per-material resolution).
> Verified in-engine (CharacterTestbed): class 0 = 144 layers @ 512 (48 MB BC7), class 1 =
> 18 layers @ 1024 (24 MB BC7); SSBO `count512=144, count1024=18`; building (StoneBricks/Wood/
> Bricks) renders from the 1024 array, terrain from 512; no magenta, no validation errors,
> 500 FPS. Tests updated (registry counts + class-aware index range).
>
> **STALE COUNTS (flagged 2026-07-21):** this 144/18 split is a dated (2026-06-23) snapshot, not
> current. `resources/materials.json` now has **102 total material entries** (up from the ~19 in
> the original Phase-1 push), of which **30 carry `"resolution": 1024`** (class 1) — roughly double
> the 18 this section reports — including LogPine/LogJungle/LogPalm/LogRedwood/WoodPlanks/Thatch/
> ClayTile/WoodShingle/Slate/StoneSlab/etc. added since. The re-baked leaf materials from the recent
> merge (Leaf/LeafBirch/LeafSpruce/LeafJungle/LeafAutumn) remain unresolved-field (class 0/512px) —
> the leaf re-bake did not move them to the 1024 class. `AtlasManager.h`'s `TEXTURE_SIZE=512` /
> `TEXTURE_SIZE_HI=1024` / two-class split (§2 "Mixed-resolution implication") is still exactly how
> the code works — only the specific 144/18 layer-count snapshot is out of date. Nobody has
> re-verified the live layer counts in-engine since; treat 144/18 as historical, not current.
>
> **PHASE 1 COMPLETE.** Net VRAM ≈ 72 MB BC7 (48 + 24) for the whole voxel texture set.
>
> **REMAINING (optional polish):** re-source the rest of the materials (Log/Leaf variants, Gold,
> Sandstone tuning; Glass/glow/Mirror stay special-cased). Then **Phase 2 (PBR shading)** using
> the normal/roughness/AO maps already cached under `resources/textures/pbr/`.
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

> **SPIKE STATUS (commit 5, 2026-06-23): normal mapping + roughness + Cook-Torrance DONE + verified.**
> The fetch tool emits per-face `_nr.png` sidecars (RGB = tangent-space normal, A = roughness) at
> each material's resolution. `AtlasManager` builds a parallel normal+roughness array per class
> (flat-normal default where no sidecar) and the BC7 encoder/cache is generalized over map type
> (`voxel_bc7_albedo_*` / `voxel_bc7_nr_*`, cache version → 3). `VulkanDevice` adds the NR arrays at
> **descriptor bindings 6 (512) / 7 (1024)** in `VK_FORMAT_BC7_UNORM_BLOCK` (linear — normals must
> not be sRGB-decoded); the upload `target` selector now spans 4 images. `voxel.frag` samples
> normal+roughness, builds a per-face TBN (axis-aligned faces → stable tangent), perturbs the
> normal, and replaces Blinn-Phong with a **Cook-Torrance GGX BRDF** (dielectric F0, energy-
> conserving) for sun + point + spot lights. Diffuse 1/π is intentionally omitted for brightness
> parity with the prior model.
> Verified in-engine: all 4 BC7 maps upload (albedo+NR × 2 classes), ~144 MB VRAM total, renders
> clean (no magenta, no validation errors), 400 FPS, brightness comparable.
> **NOT YET / FOLLOW-ONS:** AO + per-material metallic (Metal/Gold look dielectric for now); the
> per-face tangent is approximate (fine normal-feature orientation may differ per face) — refine to
> match `static_voxel.vert`'s per-face UV axes; consider BC5 for normals to cut the +72 MB NR cost;
> re-source remaining materials' PBR maps. **Idea (user):** drive roughness from per-voxel damage
> state — needs `DamageSystem`'s "accumulate damage" TODO implemented first (voxels are binary
> intact/broken today); then modulate `rough` in `voxel.frag` from a few damage bits in the
> instance `flags`.

#### Original Phase 2 plan
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

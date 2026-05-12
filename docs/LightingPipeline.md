# Lighting Pipeline — Implementation State

*Last updated: May 11, 2026*

This document describes the multi-phase rendering upgrade that added shadows, SSAO, planar mirrors, and transparent (glass) voxels. See `STATUS.md` for the project-level summary.

---

## Render Pipeline Overview

All scene rendering targets an offscreen `R16G16B16A16_SFLOAT` image. A fullscreen post-process quad composites the result to the swapchain each frame.

**Frame order in `RenderCoordinator::drawFrame()`:**

1. Shadow pass (`ShadowMap`) — depth-only, light-space orthographic
2. GPU compute (particle solver)
3. Reflection pass — renders reflected scene into `reflectionImage` (only when mirror voxels are present)
4. Scene pass (offscreen HDR) — opaque geometry, entities, kinematic objects, mirror surfaces
5. SSAO pass — reads scene depth, outputs occlusion into `ssaoBlurImage`
6. OIT pass — re-renders chunks through `transparent_voxel.frag` (currently all fragments discarded — see below)
7. Post-process pass (swapchain) — composites scene + OIT + SSAO + bloom, tone-maps, gamma-corrects

---

## Phase 1 — Shadow Mapping (PCF)

**Shadow map:** 4096×4096 `D32_SFLOAT` image. Orthographic light-space matrix from scene AABB.

**PCF:** 16-sample Poisson-disk in `voxel.frag`. Bias = 0.005.

**Shadow pipelines:**
| Pipeline | Vertex shader | For |
|---|---|---|
| Static voxels | `shadow.vert` | Chunk voxels |
| Characters | `character_shadow.vert` | `AnimatedVoxelCharacter` |
| Kinematic | `kinematic_shadow.vert` | Doors, furniture, fragments |
| Dynamic | `dynamic_shadow.vert` | GPU-particle debris |

---

## Phase 2 — SSAO

**Inputs:** Scene depth buffer (sampled as `SAMPLED_BIT` texture), random hemisphere kernel (64 samples), 4×4 Halton noise tile.

**Passes:**
1. SSAO → `ssaoImage` (R8_UNORM)
2. Blur → `ssaoBlurImage` (R8_UNORM)

**Integration:** `post_process.frag` binding 2 samples `ssaoBlurImage`. `color *= ao` applied after OIT composite, before tone-mapping.

---

## Phase 3 — OIT Infrastructure (Weighted Blended)

### Resources
| Image | Format | Clear | Usage |
|---|---|---|---|
| `oitAccumImage` | `R16G16B16A16_SFLOAT` | (0,0,0,0) | Additive weighted color accumulation |
| `oitRevealImage` | `R8_UNORM` | 1.0 | Product of `(1 - alpha)` across layers |

### OIT render pass
- `initialLayout = VK_IMAGE_LAYOUT_UNDEFINED` (CLEAR discards prior content)
- `finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL`
- Depth attachment: `loadOp = LOAD`, `initialLayout = DEPTH_STENCIL_READ_ONLY_OPTIMAL` — reuses scene depth without writing

### OIT pipeline (`transparent_voxel.frag`)
- Depth test: ON, depth write: OFF, compare: `LESS_OR_EQUAL`
- Blend attachment 0 (accum): `ONE / ONE` additive
- Blend attachment 1 (reveal): `ZERO / SRC_COLOR` multiplicative
- `independentBlend = VK_TRUE` required

### Post-process composite (`post_process.frag`)
```glsl
vec4 accum  = texture(oitAccum, inUV);   // binding 3
float reveal = texture(oitReveal, inUV).r; // binding 4
if (accum.a > 1e-5) {
    vec3 transparentColor = accum.rgb / accum.a;
    color = mix(transparentColor, color, reveal);
}
```

### Current state — OIT disabled
`blurImages[0/1]` (bloom ping-pong images) are created in `VK_IMAGE_LAYOUT_UNDEFINED` and never transitioned because `renderBloom()` is never called from `drawFrame()`. The post-process descriptor at binding 1 points to `blurImages[0]` in UNDEFINED layout, which causes a validation error on every frame and corrupts the draw call output — making OIT composite invisible.

**To re-enable OIT:**
1. Wire bloom: call `postProcessor->renderBloom(cmd)` before `beginPostProcessRenderPass`, or at minimum add a one-time image barrier transitioning `blurImages[0/1]` from UNDEFINED → SHADER_READ_ONLY_OPTIMAL before the first post-process draw.
2. In `voxel.frag`, restore: `if ((flags & 2u) != 0u) discard;`
3. In `transparent_voxel.frag`, remove the `discard;` at the top of `main()`.

---

## Phase 4a — Transparent (Glass) Voxels

### Flag encoding
`InstanceData.reserved` (uint16):
- Bit 0: unused (reserved for emissive in `packedData`)
- Bit 1: `isTransparent` — set when `matDef->alpha < 0.99f`
- Bits 2–9: quantised alpha = `uint16_t(alpha * 255)`
- Bit 10: `isMirror`

Glass material: `alpha=0.5` → `reserved = (0 | 2 | (127 << 2)) = 510`

### Current rendering path (OIT disabled)
Glass renders through `voxel.frag` alongside opaque voxels. The `if ((flags & 2u) != 0u) discard` line is absent — transparent instances are not culled from the opaque pass. Texture alpha cutout (`if (textureColor.a < 0.1) discard`) still discards fully invisible texels.

This matches exactly how kinematic and dynamic glass have always rendered.

### Face culling
`ChunkRenderManager::rebuildCubeFaces()` calls `neighborCube->isVisible()` to decide whether to generate a face. `Cube::isVisible()` returns the `visible` flag which is `true` for all placed cubes regardless of material alpha. This correctly suppresses interior faces between a glass cube and an adjacent solid cube — matching Minecraft-style glass rendering.

### Known issue — Glass_side.png missing
`resources/textures/source/Glass_side.png` does not exist. All 6 glass faces fall back to the atlas fallback texture. To fix: create or copy a glass texture PNG to that path and run `build_shaders.bat` to rebuild the atlas.

---

## Phase 4b — Planar Mirror Voxels

- **Mirror material flag:** `InstanceData.reserved` bit 10.
- **Reflection render pass:** Renders full scene into `reflectionImage` (RGBA16F) from a reflected camera. Runs only when `scanForMirrorVoxels()` returns true.
- **Mirror pipeline:** `mirror_voxel.frag` samples `reflectionImage` and composites the reflected view onto mirror-material voxel faces.
- **Culling:** Both `voxel.frag` and `transparent_voxel.frag` discard mirror-flagged fragments so they don't appear in the main scene pass.

---

## Known Issues

| Issue | Cause | Fix |
|---|---|---|
| `VK_IMAGE_LAYOUT_UNDEFINED` validation error (image `0xa900000000a9`) every frame | `blurImages[0]` created but never transitioned (bloom not wired) | Call `renderBloom()` from `drawFrame()` or add image barrier |
| OIT glass invisible | Downstream of bloom UNDEFINED — disables OIT until bloom fixed | See "To re-enable OIT" above |
| Glass uses fallback texture | `Glass_side.png` missing from source textures | Add PNG and rebuild atlas |
| No CSM | Single shadow cascade | Phase 5 work |

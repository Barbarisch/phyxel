# Voxel LOD — TODO / Design Notes (parked 2026-06-15)

Distance-based voxel level-of-detail for large worlds: render distant chunks at a
coarser resolution so view distance can grow without paying full-res geometry cost
everywhere. **Parked after a failed first attempt** — pick up from here.

---

## Status

| Piece | State |
|-------|-------|
| **Phase A — variable-size faces** (a static cube-face instance can span W×H voxels) | ✅ DONE, on `main` (commit `e916d1e`) |
| **Phase B — greedy meshing** (merge coplanar same-material faces) | ✅ DONE, on `main` (`e916d1e`), on by default, verified (~6× fewer faces, pixel-identical) |
| **Phase C — distance LOD** (downsample distant chunks) | ❌ Attempted, **broken**, **reverted** (`1966a2e` reverted by `917dd09`) |
| **LOD skirts** (hide cross-LOD cracks) | ❌ Attempted, reverted (`6c9d74a` reverted by `acea8ef`) |
| **`render_distance` debug API** | ✅ Kept on `main` (`19a5bd8`) — needed to test LOD at all (see gotcha #1) |

The reverted Phase C / skirt code is recoverable from commits `1966a2e` and `6c9d74a`
if we want a starting point — but **the coarse-mesh generation in it is wrong** (see below),
so a clean re-implementation of the downsampling is probably better than resurrecting it.

The **variable-size face format (Phase A/B) is sound and is the right foundation** — a coarse
LOD face is just a sized face in full-res coords (`packCubeFaceDataSized(pos*step, …, extent*step)`),
texture tiles per world unit. Reuse it.

---

## Why the first attempt failed (read before retrying)

Two compounding traps made it *look* like it worked when it didn't:

1. **⚠️ RENDER-DISTANCE GOTCHA.** `Application::maxChunkRenderDistance` is **~96 at runtime**
   (set low somewhere — NOT the `1000` header default). The camera far plane frustum-culls
   everything past ~96 units, so **every "far LOD terrain" view was the 96-unit cutoff, not LOD.**
   Raise it first: `POST /api/debug/render_distance {"distance":700}` (kept on `main`).
2. **Boundary shells masked a broken mesh.** Coarse chunks (lodStep>1) skipped cross-chunk
   culling and drew **full boundary walls** ("boundary shell") to be hole-free. Those walls
   sealed each coarse chunk in a box, hiding that the downsampled mesh underneath is **holey /
   fragmented**. With render distance raised AND walls culled, up-close the coarse mesh is
   **sparse, disconnected floating faces** (confirmed in wireframe). So the downsampling +
   coarse cross-chunk culling produce non-watertight geometry.

**Testing lesson (mandatory next time):** validate LOD
- at a **confirmed render distance** (raise it via the API; don't trust the default),
- **up close** — force tiny LOD distances (`lod1≈12, lod2≈28`) so a coarse chunk is right in
  front of the camera, not a speck at the horizon, and
- in **wireframe** (`POST /api/debug/overlay {"enabled":true,"mode":0}`) to see the actual geometry.
A render cutoff or filler walls can fake "it works" from afar.

---

## How the (broken) approach worked, for reference

- `rebuildCubeFaces(…, lodStep)`: build the full-res 32³ solid+material grid, then downsample
  to M=32/lodStep (LOD cell = **majority** solid + majority material of its lodStep³ block),
  greedy-mesh the coarse grid, emit faces in full-res coords (`pos*lodStep`, `extent*lodStep`).
- `Chunk::m_currentLod`; `ChunkManager::updateChunkLODs(camPos, budget)` picks LOD per chunk by
  distance (`m_lod1Dist`/`m_lod2Dist`) with hysteresis, re-meshing ≤budget chunks/frame
  (1 step at a time), called per frame from `Application`. `setVoxelLodEnabled` re-meshed all.
- LOD distance→level mapping **did work** and was confirmed with a histogram diagnostic
  (full-res 45–168u, LOD2 175–367u, LOD4 377–488u at lod1=150/lod2=350). The selection logic
  is fine; **the coarse mesh geometry is the broken part.**

---

## Open design questions for the retry

1. **Watertight coarse mesh (the core bug).** Why is the downsampled mesh holey? Suspects:
   - The majority-rule downsample may drop the thin surface layer / create internal gaps.
   - The coarse **cross-chunk culling** (sampling the neighbor's lodStep³ block as majority)
     likely over-culls — at lodStep=4, M=8, so most cells are boundary-adjacent, so over-culling
     fragments nearly the whole chunk. Verify/fix this first.
   - Decide the downsample rule deliberately: "solid if ANY sub-cell solid" (over-fills, never
     holes — maybe safer) vs "majority" (cleaner silhouette, risks holes).
2. **Cross-LOD seams.** Adjacent chunks at different LOD levels have mismatched edge heights.
   Options: skirts (curtains hanging from boundary surface columns — the reverted approach,
   but it was costly and built on the broken mesh), or constrain neighbor LODs to differ by ≤1
   + stitch. Solve only AFTER the coarse mesh is watertight.
3. **Skirt cost.** The reverted skirts were per-boundary-column × both face directions (static
   voxels use `VK_CULL_MODE_FRONT_BIT`, so one-sided faces need both). Greedy-merge skirt
   columns laterally to cut cost.
4. **Async / budgeted meshing.** Re-meshing on LOD change is synchronous (budgeted to N
   chunks/frame). For smoothness at scale, move meshing off the main thread.
5. **Mesh far chunks at their LOD on first load** (not full-res then coarsen over frames).
6. **`game.json` wiring** + sensible default LOD distances relative to render distance.
7. **Texture aliasing** on tiled coarse faces (LOD2 tiles 2×, LOD4 4×) — may need mip-bias or
   sampling tweaks once geometry is correct (was masked by the geometry bug this time).

## Available tooling
- `POST /api/debug/render_distance {distance}` — raise the far plane (on `main`).
- `POST /api/debug/overlay {enabled, mode:0}` — wireframe.
- (The `lod_status` histogram + `voxel_lod` toggle from the attempt were reverted; re-add them
  alongside the retry — they were useful for confirming the distance→LOD bands.)

## Reference commits
- `e916d1e` greedy meshing + variable-size faces (KEEP — foundation)
- `1966a2e` Phase C distance LOD (reverted `917dd09`) — broken coarse mesh
- `6c9d74a` LOD skirts (reverted `acea8ef`)
- `19a5bd8` render-distance debug control (kept)

# Camera-Relative Rendering — the continental-coordinate precision fix

> Motivation (established 2026-07-17 on the Middle-earth 1:1 world, player at x≈60,400):
> float ULP at 60k ≈ 4 mm. World→clip transforms cancel catastrophically, so contested
> edge pixels re-resolve per frame. Measured: **12.08% of character-region pixels change
> between two consecutive frames with a FIXED camera and an idle character** (grass/sky
> reference regions ≈ 1%). Visible as: per-voxel "not glued together" character speckle,
> dashed bright lines along greedy-merge/chunk borders (T-junction contests), grass
> shimmer. All confirmed absent/mild in near-origin worlds. Full diagnostic trail:
> RenderOptimization.md "Known issue" sections.

## Design

- **View matrix with the camera at the origin**: rotation-only view (eye=0). The CPU
  computes every translation handed to the GPU as `(worldPos - cameraPos)` in **doubles**,
  truncating to float only after subtraction. Result: all GPU-side positions are bounded
  by render distance (≤ a few thousand), where float precision is sub-micron.
- **Per-pass offsets**: chunks already push `chunkBaseOffset` — becomes
  `dvec3(chunkOrigin) - dvec3(cameraPos)` per frame. Kinematic/dynamic objects: subtract
  camera from the model matrix translation on CPU. Far-terrain tiles, grass/foliage
  (per-chunk), water plane: same treatment at their push/instance sites.
- **Shadow / light space**: build the light view around the camera-relative frame too
  (keep the world-anchored texel snapping semantics by anchoring in DOUBLE world space,
  then subtracting cameraPos — the snap must stay world-anchored or shadow crawl returns;
  see the far-terrain shadow arc notes).
- **Fragment worldPos consumers**: `varied` tile-rotation hash must NOT become
  camera-dependent — feed it a stable per-voxel key (chunk coord seed + local cell)
  instead of raw worldPos. Audit every other `inWorldPos` use (specular/view dir is
  camera-relative by definition — actually IMPROVES).
- Camera position storage should move to double (or double-precision accumulation) in
  the Camera class if it is float today (survey confirms).

## Increments (each independently shippable, visual A/B at the recorded poses)

1. **Core + static terrain**: UBO/view construction + static_voxel path (+ its shadow
   variant). Verify: terrain at 60k renders identically to pre-change from the repro
   poses (screenshot diff vs baseline should show ONLY stability improvements); the
   dashed-line poses; near-origin world unchanged.
2. **Kinematic + dynamic** (characters/furniture/debris): the character gate — frame-diff
   12% → ~1–2% at the fixed-camera idle pose (60398,27,50798 yaw45 pitch−15, sun 9.5).
3. **Grass + foliage**: blade shimmer at the same poses.
4. **Far terrain + water plane + sky**: far-field correctness (far tiles under-lap rule).
5. **OIT / mirror / reflection + VFX/debug lines**: the reflection pass uses a reflected
   eye — reflect the RELATIVE frame carefully.

## Verification protocol (per increment)

- Same-state temporal baseline first (must be ~1% noise floor), vegetation off where
  applicable, day paused — per the clean-diff paradigm in the plan doc.
- Repro poses (Middle-earth 1:1, sun 9.5 paused): character close-up (60398,27,50798
  yaw45 pitch−15); dash poses (60396,28,50796 yaw45 pitch−25 and 60401,26.2,50800.5
  yaw45 pitch−30); plan view (should stay dash-free).
- Frame-diff metric: two consecutive screenshots, character region change % (script in
  session notes — PIL diff, threshold 12/255).
- Near-origin regression guard: CharacterTestbed / LodTest visual parity.
- Winding footgun: multi-angle screenshots after ANY pass's matrix change
  (docs/AgentContext.md render-pipeline rules).

## Surveyed edit map (2026-07-17 — full detail in the survey session)

- **Root change point:** `Camera` stores FLOAT `position` (Camera.h:96) and builds
  `lookAt(position, …)` (Camera.cpp:20-22). Add double-precision position accumulation +
  `getRelativeViewMatrix()` (eye at origin, rotation-only).
- **UBO** (`VulkanDevice.h:104-121`, filled at VulkanDevice.cpp:1485-1524 from
  RenderCoordinator:1407-1543): `viewProj` consumers = static/dynamic/kinematic/grass/
  foliage verts; `view/proj` = far_terrain, debug_line, debug_voxel, transparent depth
  weight; `biasedLightSpace` = static/dynamic/kinematic/foliage; `cameraPosition` = frag
  view-dirs + grass/foliage distance fades. Set `cameraPosition = 0` post-conversion
  (V = −relPos stays correct with NO frag edits) and APPEND `vec3 cameraWorld` at the
  UBO END (std140 prefix-truncated blocks stay valid) for absolute-hash reconstruction.
- **The UBO cohort converts in ONE step** (they share the UBO view): static (+OIT+mirror
  geometry pushes at RC:399/655/800/859), grass, foliage, debug_voxel (chunkBaseOffset
  pushes → relative, CPU double subtract), kinematic (modelMatrix translation − cam,
  RC + KinematicVoxelManager:45/68), far_terrain (tileOrigin − cam.xz + Y handling),
  debug_line (CPU-built verts − cam), **dynamic** (inWorldPosition lives in the GPU
  particle buffer → add a `vec3 cameraOffset` push constant to dynamic_voxel.vert +
  dynamic_shadow.vert instead of rewriting the buffer).
- **Shadow build** (RC:1453-1528): keep the world-anchored texel snap in DOUBLE world
  space, then subtract cameraPos; all shadow-pass push sites (RC:1122/1200/1223/1246)
  feed relative offsets; foliage_shadow's ubo path follows the UBO cohort.
- **Own-view pipelines** (read Camera directly, convert same pass): vfx
  (VfxRenderPipeline:386), debris (DebrisRenderPipeline:409), character/
  character_instanced (RC:2052-2109 + AnimatedVoxelCharacter model builds
  3628/4045/4604/4741/4787), water plane (WaterRenderPipeline:319 — world =
  camPos + inPos: already camera-anchored, near-trivial), water_cell
  (WaterCellRenderPipeline:257 — per-cell world instance data − cam).
- **Absolute-hash preservation** (MUST NOT go camera-relative): voxel.frag:107 varied
  worldFaceUV, far_terrain.frag:64, grass.vert:87 / foliage.vert:80 /
  foliage_shadow.vert:67 mod-2048 wind/tint hashes → reconstruct
  `abs = rel + ubo.cameraWorld` in-shader (4 mm error ≪ the 1-voxel floor: stable).
- **Light SSBO** (point/spot positions, voxel.frag:340/358): subtract cam at
  LightManager GPU-data build.
- **Reflection/mirror** (RC:692-804): reflect the RELATIVE frame; mirror plane point
  (world voxel coords) − cam before building reflMat.

## Status (2026-07-17 — increments 1-3 SHIPPED and GATED)

- [x] Survey complete (transform plumbing map above)
- [x] **Core + static** — Camera::getRelativeViewMatrix + relativeTo (double subtract); UBO
      cameraPosition→0 + trailing cameraWorld; all 5 static chunk pushes relative; light-space
      built relative with the texel snap RE-ANCHORED through absolute doubles (crawl-safe);
      shadow culling stays absolute. CPU chunk frustum culling untouched (builds its own
      absolute view — verified).
- [x] **Kinematic + character** — KinematicVoxelPipeline setCameraWorld (model[3] − cam,
      baked-light sampling stays absolute); character main + shadow batch models relative.
- [x] **Grass + foliage** — pipelines push relative chunkBaseOffset PLUS exact ABSOLUTE
      origin scalars for hash/phase seeds (relative seeds would re-roll blades on camera
      motion); shaders hash the absolute domain, position in the relative one.
- [x] **Dynamic (debris)** — shader-side `inWorldPosition − ubo.cameraWorld` (GPU-buffer
      positions stay absolute; ~4 mm quantization imperceptible on debris); dynamic shadow
      push extended with cameraWorld (+layout size).
- [x] **varied tile-rotation RESTORED** (2026-07-17, after inc. 1-3) — push constants grew to
      32 B (`vec3 rel + uint debugMode + vec3 chunkBaseAbs`, VulkanDevice::getPushConstantRange
      + mirror layout); static_voxel.vert forwards BOTH origins as flat varyings (loc 10/11);
      voxel.frag reconstructs `worldPosAbs = vChunkBaseAbs + (inWorldPos - vChunkBaseRel)` —
      abs is an exact integer float, the parenthesized local is small-magnitude, so the hash
      key is camera-independent. dynamic/kinematic verts write zeroed varyings (their flags
      never set the varied bit). GATES: same-pose diff after a camera round-trip 0.676% ≈ the
      0.585% consecutive-frame noise floor (no re-roll); per-tile texel sampling on a fresh
      12-cube Dirt patch shows within-row divergence 3-4x the lighting gradient (varied
      ACTIVE); chunk-family unit suite 69/69.
- [x] **far_terrain CONVERTED + LIVE on the 1:1 world** (2026-07-17) — FarTerrainPush grew to
      16 B (`vec2 tileOriginRel + vec2 tileOriginAbs`); rel is double-subtracted on CPU
      (setCameraWorld per frame), the vert subtracts `ubo.cameraWorld.y` from the baked
      absolute mesh Y for clip space and hands the frag the EXACT absolute frame for the
      per-world-unit texture projection. Enable at runtime: `/api/debug/render_distance`
      (far plane) + `/api/debug/far_terrain {enabled, maxDistance}` — NOT persisted in
      game.json yet (config-phase item). GATES: tiles project coherently at x≈47.5k
      (Fangorn) and x≈61.9k (Gondor: Mindolluin range front + far ridge vista, 2 km);
      far-field round-trip diff 0.479% ≈ noise floor; clean near/far depth boundary.
      Near-origin (LodTest) untested live — same code path, rel==abs−cam exact; low risk.
- [ ] **Increment 5 (remaining)** — debug_line/debug_voxel overlays (F-key gated, render
      displaced), mirror/reflection pass (reflectedViewProj absolute vs relative inWorldPos:
      mirrors misproject), generic-entity placeholder draw. Water/VFX/debris-pipeline are
      SELF-CONSISTENT absolute (own view pushes) — correct placement, old precision.

**Gate results (Middle-earth 1:1, x≈60,400):** pose-frozen character frame-diff
**12.08% → 0.00%** (mean delta 0.000 — bitwise stable); ground/sky reference regions
~1% → 0.00%; character speckle GONE (user-confirmed visually); grass/foliage restored and
seed-stable; shadows coherent at 9:30 sun; **dotted merge-border lines GONE at the recorded
dash poses**; chunk-suite regression 118/118.

**Found during verification (pre-existing, logged):** GPU-particle occupancy does not cover
continental coordinates — debris falls through the world at 60k (physics-side far-origin
gap, blocked visual verification of the dynamic render path there; verify debris on a
near-origin world). Camera float-position ACCUMULATION granularity (~4 mm steps at 60 km)
left as a follow-up (storage stays float; only motion smoothness affected).

# Render Optimization for Microcube-Dense Worlds

> Goal (user): lean into microcubes for **"better than Minecraft"** visual fidelity. Open question:
> does the engine hold up at very high microcube counts? This doc records the **measured** answer and
> the optimization plan. Ground every "holds up / too slow" claim in numbers (no vibes).

## Measured baseline (2026-06-27, DEBUG build, RTX 4090 target rig)

A terrain-aware settlement on Perlin hills (StructGenHills, seed 7), camera over the settlement:

| scene | visible chunks | **visible face instances** | frame (render) | FPS |
|-------|:--:|:--:|:--:|:--:|
| terrain only | 11 | **7,892** | — | — |
| + 20 subcube buildings + 21,745 path microcubes | 11 | **3,424,612** | 66 ms (59 ms render) | ~15 (STUTTER) |

The settlement adds **~3.4 MILLION face instances** over bare terrain's 7.9k. Placement is cheap
(~60 µs/microcube); **RENDER is the wall** (≈59 ms/frame). Instrumented via
`RenderCoordinator::RenderStats.totalVisibleFaces` → `GET /api/render/stats` `total_visible_faces`
(from `ChunkManager::ChunkStats.totalVisibleFaces`).

### Single-building datapoint (2026-06-28, DEBUG, post main-merge)

Even ONE furnished subcube/microcube building tanks FPS — measured on the flat StructGenTest world,
same camera:

| scene | visible faces | FPS |
|-------|:--:|:--:|
| empty flat world (1 chunk) | **80** | **357** |
| + one v2 `tavern` (16×7, 2-story, furnished) | **412,298** | **49** |

The tavern's ~65k subcube/microcube voxels become **412k visible faces** in a single chunk (sub/micro
faces are NOT greedy-merged). Confirms this is NOT a lighting-merge or build-perf-fix regression (empty
world is 357 FPS) — it is purely the un-merged sub/micro face count. **This caps build density until #40
(greedy-mesh sub/micro) lands.**

## ✅ Phase 1 shipped (2026-06-28) — sub/microcube hidden-face culling

Before greedy-merge, a more basic gap: subcubes & microcubes had **no face culling at all**
(`rebuildSubcubeFaces`/`rebuildMicrocubeFaces` hardcoded `faceVisible[6] = {true,…}`), so every
sub/microcube emitted all 6 faces even when fully buried (412k baseline = ~65k voxels × 6.3 faces).

**Fix** (`ChunkRenderManager`, uncommitted): a transient leaf-occupancy lookup (`m_subOcc`/`m_microOcc`
`unordered_set`s) built once per `rebuildAllFaces` straight from the voxel hierarchy, reusing the
already-built `m_solidVis` as the cube oracle. The sub/micro builders cull a face when the neighbour
cell at its own resolution (96³ subcube / 288³ microcube) is provably fully solid (coarser fills roll
up). Chunk-boundary neighbours treated as exposed (conservative). No shader/format/UV/winding change —
only buried faces omitted, so zero winding risk.

**Measured (DEBUG, fresh `RenderCullTest` world, remove-and-remeasure) — same v2 `tavern` (16×7,
2-story, furnished, 19 fixtures):** isolated face contribution **412,298 → 55,068 (7.5× fewer)**;
solid walls verified inside + out, textures intact. NOTE: verify on a FRESH world — a test DB with
repeated tavern rebuilds at the same spot produced misleading holey walls (stale/overlapping chunk
state), not a culling bug.

**Phase 2 (greedy-merge) — attempted, reverted, PARKED.** Wider `InstanceData.mergeData` +
`static_voxel.vert` scaleLevel==3 branch + `rebuildFineFacesMerged` greedy mesher. Merged quads
rendered the magenta fallback texture (per-face path was correct; index delivery & geometry verified
fine; forcing a valid index in C++ AND in-shader still magenta → frag-side, scaleLevel==3-specific,
likely the world-space UV × texture-array sample). Backed out to keep the tree green; redo from the
design below in a focused session.

## Baseline 2026-07-03 — shader-math + greedy-mesh campaigns (Increment 0)

Fresh capture for [`ShaderMathRedundancyPlan.md`](ShaderMathRedundancyPlan.md) +
[`BinaryGreedyMeshingPlan.md`](BinaryGreedyMeshingPlan.md). **DEBUG build @ commit `6e9f55e`
(main) + Phase 1 culling**, StructGenTest flat world (pristine reset), one v2 tavern built by the
engine generator: `POST /api/structure/build {"schema":"v2","typology":"tavern","function":"tavern",
"style":"medieval","footprint":[16,7],"stories":[{},{}],"position":{"x":8,"y":16,"z":8}}` —
log-verified: `auto-filled 2 story(ies) -> 6 rooms [typology tavern]`, validation OK, 20 fixtures
placed 0 skipped, sign hung. Structure AABB (8,17,8)–(23,27,14).

| scene | `total_visible_faces` | notes |
|-------|--:|-------|
| empty flat world | 14 | 138.7 FPS at initial camera |
| + v2 tavern | **51,258** | close to the documented post-Phase-1 55,068 (different seed/pos) |

**Fixed comparison poses** (MCP `set_camera` mode:"free" — see methodology warning below):

| pose | position | yaw/pitch | reference screenshot |
|------|----------|-----------|----------------------|
| A exterior NE | (34, 32, 29) | −135 / −25 | `screenshots/screenshot_20260703_084841_802.png` |
| B exterior W (sign + character visible) | (−6, 28, 28) | −37 / −16 | `screenshots/screenshot_20260703_084913_364.png` |
| C interior (furniture + micro walls) | (18.5, 20, 11.5) | −165 / −18 | `screenshots/screenshot_20260703_085015_966.png` |

**⚠ FPS methodology (hard-won, 2026-07-03):**
1. **`POST /api/camera` does NOT reliably hold a free-camera pose** — the camera can revert to
   player-follow (~(22.6,19.2,22.6)) within seconds, silently measuring the wrong view. The MCP
   `set_camera` tool holds indefinitely. Always **verify `GET /api/camera` position in the same
   breath as reading `GET /api/debug/engine_timing`**, or the number is meaningless.
2. **Restart-to-restart FPS variance is ±20% on the target rig** (same shaders, same DB world,
   same verified pose: 140.4 vs 113.6 FPS across two launches). Only **within-run** comparisons
   at verified poses are meaningful at the few-ms scale.
3. World state must be **loaded from the saved DB** (`POST /api/world/save` after building —
   chunk voxels are NOT auto-persisted by the structure build; a force-killed engine loses them.
   Rebuilding the same payload at the same position is deterministic: 51,258 faces every time).

**Shader-math A/B result (2026-07-03, `ShaderMathRedundancyPlan.md` executed):** old vs new
shaders (same binary, same DB world, verified poses): A 142.4→140.4 FPS, C 75.7→72.8 FPS —
**no measurable difference** (within noise). The NVIDIA driver on the RTX 4090 was already
hoisting the per-vertex uniform mat4×mat4 products (the plan's documented caveat). The changes
ship anyway: correct-by-construction, removes reliance on driver heroics, fixes the misdeclared
`static_voxel.vert` UBO block, and eliminates a real per-fragment matrix product in
`character.frag`. Visual identity verified at poses A/B/C by manual comparison (not a pixel-diff
tool) — after-screenshots: A = `screenshot_20260703_090250_111.png`, B = `_091328_493.png`,
C = `_090307_105.png`.

## Baseline 2026-07-06 — greedy-mesh build kickoff (Increment 0, this campaign)

Fresh capture opening the `BinaryGreedyMeshingPlan.md` implementation. **DEBUG build (main + Phase 1
culling), StructGenTest flat world (pristine reset — `worlds/default.db` deleted, world regenerated),
one v2 tavern built by the engine generator** (provenance-verified in `phyxel.log`):
`StructureV2 generate_room_layout: auto-filled 2 story(ies) -> 6 rooms total [typology tavern]`,
`program validation: OK`, `StructureGenerator Placing 48492 voxels`,
`FurniturePlacer: engine placed 20 fixtures (0 skipped)`, sign hung. Structure AABB (7,17,8)–(23,27,14).
Payload: `POST /api/structure/build {"schema":"v2","typology":"tavern","function":"tavern",
"style":"medieval","footprint":[16,7],"stories":[{},{}],"position":{"x":8,"y":16,"z":8}}`.
World saved (`save_world all`) so it reloads deterministically.

| scene | `total_visible_faces` | FPS | notes |
|-------|--:|--:|-------|
| empty flat world | 14 | 148.3 | initial camera |
| + v2 tavern | **68,126** | — | higher than the 2026-07-03 51,258 (fuller fixture depth this build; same payload, deterministic per DB) |

**Fixed comparison poses** (MCP `set_camera` mode:"free", camera position verified held in the same
breath as the timing read, per the methodology warning above):

| pose | position | yaw/pitch | FPS | cpuFrame | screenshot |
|------|----------|-----------|----:|---------:|------------|
| A exterior NE | (34, 32, 29) | −135 / −25 | 107.1 | 9.34 ms | `screenshots/screenshot_20260706_165136_835.png` |
| C interior (furniture + micro walls + light) | (18.5, 20, 11.5) | −165 / −18 | 74.4 | 13.44 ms | `screenshots/screenshot_20260706_165201_478.png` |

**Red unit test (red-before-green, `tests/graphics/FineFaceMergeTest.cpp`):** a 4×4 slab of
microcube-packed cubes (729 micro/cube, same material). The green characterization guard
(`MicrocubeSlab_UnmergedTopFaceCountIs…`) passes: the +Y direction emits exactly **1,296 = 81·N²**
faces, every one an unmerged microcube (scaleLevel 2). Two DISABLED_ targets shown FAILING on the
current per-face path (via `--gtest_also_run_disabled_tests`): `…CollapsesPerCube` wants ≤ N²=16
(actual 1296 → Increment 2 greens it); `…CollapsesAcrossSlab` wants ≤ 4 (actual 1296 → Increment 4).

## ✅ Increment 1 shipped (2026-07-06) — encoding spike, the Phase 2 mystery is dead

Branch `render-fine-greedy-mesh`. Proved the fine-merge **encoding renders end-to-end with NO
magenta** — the exact frag-side failure that sank the reverted Phase 2 (`:55-60`). Approach per
`BinaryGreedyMeshingPlan.md` §4.1 **Option A**: merged sub/microcube faces store their rectangle
extents `(sizeU-1, sizeV-1)` in the **light word bits 16-31** (provably written-0 / read-never today),
so the `InstanceData` struct is **not** widened — the dual-struct attribute-offset failure class is
structurally impossible, and unmerged data decodes byte-identical.

Changes: `Types.h::packFineExtentsIntoLight()`; `static_voxel.vert` + `shadow.vert` decode the
extents, scale the sub/micro quad along the per-face u/v axes, and extend the UV by the extents
(`baseUV * extent`, within-tile). `debug_voxel.vert` deliberately untouched (it already ignores cube
extents — a consistent dev-view limitation, not a regression). A toggle `s_fineGreedyMerge`
(`ChunkRenderManager`, default OFF) + `POST /api/debug/fine_merge {"enabled":bool}` gate a live A/B;
the toggle-on path emits one hand-forged 2×1 merged subcube +Z brick quad at world (10,22,20)
(temporary probe, removed when Increment 3's real subcube mesher lands).

**Evidence (DEBUG, StructGenTest + saved tavern):**
- Toggle **OFF** = **byte-identical**: 68,126 faces (= Inc 0 baseline), interior pose C pixel-identical
  (`screenshots/screenshot_20260706_190339_556.png` vs Inc 0 `_165201_478.png`). §4.1 invariant proven.
- Toggle **ON**: 68,126 → **68,127** (exactly +1 — one merged instance, not two). The quad renders as a
  correct multi-course **brick texture, NOT magenta**, as a wide 2:1 rectangle (extent scaling applied;
  UV spans 2× the tile fraction, not a stretched single tile).
  `screenshots/screenshot_20260706_190407_872.png`.
- Shadow: `shadow.vert` extent math mirrors the proven `static_voxel.vert` path exactly and the shadow
  pipeline feeds attribute location 4 (`Types.cpp:95-97`) — verified by construction.
- Unit suite green: 68/68 (`FineFaceMerge` guard, `InstanceData*`, `Types*`, `GridEncoding*`); the two
  DISABLED_ merge targets remain red pending Increments 2/4.

## ✅ Increment 2 shipped (2026-07-06) — within-cube microcube merging

Branch `render-fine-greedy-mesh`. `rebuildMicrocubeFacesMerged` (selected by `s_fineGreedyMerge`):
per parent cube, per face direction, per depth slice, builds a 9×9 appearance-keyed mask and
greedy-merges same-appearance runs into maximal rectangles, one `InstanceData` per rectangle with
extents in the light word. Visibility is decided per-cell with the same `microCellSolid` oracle as
the per-face path, so a rectangle only ever covers individually-visible cells. Light is constant per
(cube, face) by construction → never splits a merge. Cross-cube runs stop at the parent-cube border
(Increment 4). Shader gained a UV-flip correction (`static_voxel.vert`): merged runs on inverted
texture axes (U on faces +X/+Y, V on all faces) slide the UV origin by −(extent−1) — a no-op when
unmerged. The Increment 1 spike hook was removed (superseded).

**Runtime (DEBUG, StructGenTest + saved tavern, `/api/debug/fine_merge` A/B):**

| pose | metric | toggle OFF (per-face) | toggle ON (micro-merged) |
|------|--------|--:|--:|
| — | `total_visible_faces` | 68,126 | **16,384 (4.2× fewer)** |
| A exterior | FPS / cpuFrame | 107.1 / 9.34 ms | **150.4 / 6.65 ms (+40%)** |
| C interior | FPS | 74.4 | 73.5 (pose not face-bound — small room in frustum) |

**Visual fidelity — MEASURED, not eyeballed (a first claim of "pixel-identical" was wrong and
retracted).** Clean same-session OFF-vs-ON pixel diff at one interior pose, grass+foliage disabled so
the scene is fully static (`tools`-free `PIL.ImageChops`, viewport crop (243,44)-(1197,672), OFF
`_210020_862.png` vs ON `_210043_900.png`):

| per-channel threshold | differing pixels | note |
|---|--:|---|
| > 0/255 (any) | 1.111% (max 37/255), spread **uniformly** across all textured cells | mip-LOD filtering on larger merged quads |
| > 2/255 | 0.003% (15 px) | |
| > 8/255 (perceptible) | **0.001% (8 px)** | effectively none |

Conclusion: the merge is **perceptually lossless (>99.997% of pixels within 2/255), NOT
framebuffer-identical** — the residual is sub-perceptual mip-LOD selection over larger merged
primitives (the same tradeoff the shipped cube greedy-mesh already makes), uniform and structure-free.
A UV/flip bug would be localized and high-magnitude; it is not present. The remaining 16,384 faces are
dominated by the **subcube walls** (still per-face — Increment 3) + cubes.

**Exterior clean diff** (same methodology, pose A, grass+foliage off, OFF `_212450_937.png` vs ON
`_212509_759.png`): 0.274% at >0/255 (vs the original 3.44% — that figure was overwhelmingly animated
grass), **0.030% at >8/255**, but the perceptible pixels are localized to the **bottom-centre** cell
(max diff 191) = the idle-animating **player character** (a separate, merge-independent render path,
not paused between the ~19s-apart captures); the building surfaces themselves show 0.0% at >8 —
filtering-only, same as the interior. Exterior FPS re-confirmed 118→142 (grass/foliage off).

**Unit tests (green, red-before-green; hardened after a solution-auditor FAIL):**
`MicrocubeMerge_TopFaceCollapsesPerCube` 1296→N² (was red);
`MicrocubeMerge_CoverageMatchesPerFacePathEveryDirection` and
`…WithHolesAndMixedMaterials` — merged Σ(extentU·extentV) == per-face micro count in ALL 6 directions,
on a uniform slab AND on a holed (checkerboard-occluded) mixed-material cube (the real
window/door/footprint complexity class); `MicrocubeMerge_SplitsOnAppearanceBoundaryWithinOneCube` —
two tints inside ONE 9×9 mask must not fuse (exercises the `Key`-equality boundary the per-cube
grouping otherwise hides); `MicrocubeMerge_UVReplicaMatchesPerFaceEveryFace` — a CPU replica of the
shader UV math asserts a merged 9×9 run samples each cell's exact per-face texel window on all 6
faces incl. both flip axes (proves the `fineUVOriginShift` flip correction; this caught a bug in an
earlier version of the *test's* own (s,t) mapping, confirming it is falsifiable). Toggle-off guard
still 1296. The `DISABLED_` across-slab target stays red for Increment 4.

**Known gaps (honest):** toggle-off "byte-identical" is proven by source (unmerged path writes light
bits 16-31 as zero; the decode is a no-op) + a face-count match, NOT a `memcmp` regression test.
Shadow correctness for Increment 2's real merged geometry is covered only indirectly by the full-frame
OFF-vs-ON diff above (which includes shadow receivers); a dedicated shadow-caster isolation is
deferred. Cross-cube merging is Increment 4.

## ✅ Increment 3 shipped (2026-07-06) — within-cube subcube merging (≥10× bar crossed)

Branch `render-fine-greedy-mesh`. `rebuildSubcubeFacesMerged` — the Increment 2 algorithm at the 3×3
subcube grid (3×3 masks, 3 depth slices), same appearance-key + per-cell `subCellSolid` visibility +
per-(cube,face) light + billboarded-leaf foliage handling. No shader change (scaleLevel-1 extents +
flip already shipped in Inc 1/2). Subcube walls dominate a v2 structure, so this is the biggest single
cut.

**Runtime (DEBUG, StructGenTest + saved tavern, BOTH micro + subcube merge on):**

| metric | OFF | ON (both merges) |
|--------|--:|--:|
| `total_visible_faces` | 68,126 | **6,466 (10.5× fewer)** |
| exterior FPS / cpuFrame | 113.9 / 8.78 ms | **189.4 / 5.28 ms (+66%)** |

**≥10× face-count drop = the shipped-validation bar (`RenderOptimization.md` plan) is crossed** (micro
+ sub together; micro-only was 16,384). Clean same-session OFF-vs-ON pixel diff (grass/foliage off):
interior subcube-wall close-up (`_213442_496` vs `_213459_193`) = **0.001% at >8/255, max 37/255**
(uniform mip-LOD filtering, heat grid all-zero — no structural/UV error); exterior (`_213332_895` vs
`_213354_292`) 0.057% at >8 but localized to the animated player character (max 190, bottom-centre),
building surfaces 0.0%. Perceptually lossless.

Face-count A/B raw API capture on disk: `docs/evidence/inc3_facecount_ab.txt` (68,126 → 6,466).

**Unit tests (green):** `SubcubeMerge_TopFaceCollapsesPerCube` (9·N²→N²);
`SubcubeMerge_CoverageMatchesPerFacePathWithHolesAndMixed` (per-face oracle, all 6 dirs, checkerboard
holes + mixed tint); `SubcubeMerge_UVReplicaMatchesPerFaceEveryFace` (a UV-*formula* self-consistency
check — it does NOT read real mesher output; see the geometry test below for the actual origin guard).

**Geometry-truth guard (added after a 2nd solution-auditor FAIL that this arc earned).** The auditor
proved the coverage + UV-replica tests could not catch a wrong ORIGIN: it injected a min-cell→max-cell
origin bug into the shipped merger and all prior tests still passed (coverage sums extentU·extentV,
invariant under an origin shift; the UV replica compares a hand oracle against itself with an *assumed*
origin, never decoding real output). Fix: `Sub/MicrocubeMerge_EmittedGeometryMatchesPerFaceEvery
Direction` decode the ACTUAL emitted `packedData` grid-origin bits + light-word extents, enumerate the
world cells each merged quad covers (offset-solid-block configs → multi-cell runs at NON-corner
origins), and assert the cell-centre multiset equals the per-face oracle's, per direction, plus an
in-bounds guard. **Proven falsifiable by re-injecting the exact min→max origin bug into both
`rebuildSubcubeFacesMerged` and `rebuildMicrocubeFacesMerged`, rebuilding, and observing ONLY these two
tests go red** (via both the cell-centre mismatch and the cube-border in-bounds guard), then reverting
to green. This guard retroactively covers Increment 2 (microcube) as well. All 11 `FineFaceMerge.*`
pass; full suite green. Cross-cube merging (the `DISABLED_` across-slab target, still red) is
Increment 4.

**Lesson recorded:** "coverage/UV-formula tests pass" is NOT "the emitted geometry is correct" — a
placer/mesher test must decode and check the real output's *positions*, not just counts or a formula's
internal consistency. The UV-replica tests are kept as a cheap formula guard but are explicitly not
the origin guard.

## ✅ Increment 4a shipped (2026-07-06) — cross-cube SUBCUBE merging (UV-wrap at seams)

Branch `render-fine-greedy-mesh`. `rebuildSubcubeFacesMerged` rewritten from per-cube grouping to
**chunk-wide** merging: per face direction, buckets subcubes by depth slice, fills a reused 96×96
plane mask, and greedy-merges coplanar same-key runs **across parent-cube boundaries**. The merge key
now includes **baked light** (per cube-face), so runs split exactly at light gradients (matching the
cube greedy path). A lone cube reduces to the Increment-3 within-cube result (no neighbours). **No
shader change** — a run's extent simply spans multiple cubes and the REPEAT-wrap sampler restarts the
tile at each cube (§4.2). Extents ≤ 96 < 256, no split. Cross-*chunk* merging still out of scope.
**Microcube cross-cube (288³) deferred to Increment 4b** (perf-risk; micro detail already well-merged
within-cube).

**Runtime (StructGenTest tavern; raw JSON `docs/evidence/inc4a_facecount_ab.txt`):** 68,126 →
**6,024** faces (11.3× from baseline; 6,466 → 6,024 vs within-cube — modest here because the tavern's
walls are broken by windows/doors/corners/light; the cross-cube win scales with large uniform surfaces
— long walls, paths, the settlement scene). Close pose FPS 86 → 128.

**UV wrap at cube seams (the plan's highest visual risk) — MEASURED CLEAN.** Close-up of the long
front wall spanning ~16 cubes merged into cross-cube runs, clean same-session OFF-vs-ON diff
(grass/foliage off, `_223053_029` vs `_223121_936`): **2 pixels differ at >8/255, max channel diff 12**
— even more subtle than the within-cube filtering. No seam artifacts, no texture discontinuity at cube
boundaries. The REPEAT-wrap restart per tile is correct.

**Tests (14 `FineFaceMerge.*` green):** `SubcubeMerge_TopFaceCollapsesAcrossSlab` (a uniform N×N slab
top → ONE cross-cube rectangle, was N² within-cube; coverage exact); `SubcubeMerge_CrossCube
CollapsesAlongRow`; `SubcubeMerge_CrossCubeGeometryMatchesPerFace` (decode REAL emitted origin+extents,
cross-cube cells == per-face oracle — proven falsifiable by re-injecting the min→max origin bug into
the cross-cube merger and watching it go red); `SubcubeMerge_CrossCubeSplitsOnTintBoundaryBetweenCubes`
(runs split at a tint boundary between cubes); `SubcubeMerge_CrossCubeSplitsOnLightBoundaryBetween
Cubes` (added after a solution-auditor FAIL flagged the "splits at light gradients" claim as
UNFALSIFIED — every prior test ran with constant light) — two same-appearance subcube cubes with a
solid blocker cube shading one column's +Y neighbour (BFS skylight ~14 vs open 15) must NOT fuse;
proven falsifiable by dropping light from the merge key and watching the +Y plane wrongly collapse to
1 rect. The `coveredCellCentres` geometry helper now uses absolute chunk-cell bounds so it validates
cross-cube runs. The `DISABLED_` MICROcube across-slab target stays red (Increment 4b).

## Increment 5 — stress, scale, Release (2026-07-06, PARTIAL — see disclosure)

Branch `render-fine-greedy-mesh`. Validates increments 0–4a; microcube cross-cube (4b) deferred, so
this covers the SHIPPED state (within-cube micro + cross-cube sub). **This closeout is PARTIAL: two of
`BinaryGreedyMeshingPlan.md` §5's mandatory items were NOT run — see "Not done" below.** (A first draft
of this section overclaimed — presented a small flat-world settlement as "the settlement test" without
disclosing it is not the documented 3.4M-face hills scene, and put Debug tavern face counts under a
"Release" header. Corrected here after a solution-auditor FAIL.)

**Stress unit tests (18 `FineFaceMerge.*` green; the honest bounds — auditor-verified falsifiable):**
- `Sub/MicrocubeMerge_CheckerboardDegradesToPerFaceExactly` — "binary meshing buys nothing here":
  an alternating-TINT checkerboard (every orthogonal neighbour differs) degrades EXACTLY to the per-face
  instance count per direction — no wrong merges, no dropped cells. Proven falsifiable: dropping `tint`
  from the merge key makes it (and the tint-boundary tests) go red. (Must alternate TINT not material —
  `MaterialRegistry` is unloaded in the unit env so "Stone"/"Wood" share one fallback texture; the
  first micro draft used material and correctly went red.)
- `SubcubeMerge_LargeUniformSlabTopCollapsesToOne` — a 10×10-cube uniform slab still collapses its top
  to ONE cross-cube rectangle (no size degradation), `count==1` AND `coverage==9·N²` (rules out a
  face-drop faking the collapse). Extent 30 < the 256 cap.

**The deliverable is the DETERMINISTIC face-count reduction; FPS at these scene scales is
variance-dominated NOISE and is NOT claimed as a validated win** (raw
`docs/evidence/inc5_real_validations_release.txt`). Build provenance was hash-verified per the
auditor's demand: `launch_engine config:Release` → process image MD5 `2e5c9116` == the Release binary
(≠ Debug `9184a021`), confirmed via PowerShell `Get-Process`; `config:Debug` → `9184a021` == Debug.
(The root `./phyxel.exe` is a *Debug* copy, but `config:Release` runs `build/editor/Release/phyxel.exe`,
not root — the source of an earlier Debug/Release confusion.)

- **Face counts (deterministic, build-independent — the real result):** empty world OFF **14** → ON
  **14** (merge is a no-op on cube-only content = **no regression**); tavern OFF 68,126 → ON 6,024
  (11.3×); settlement (4 buildings / 2 chunks) OFF **171,944** → ON **16,789** (10.2×); straddle tavern
  OFF 73,058 → ON 6,300 (11.6×). Identical in Debug and Release (it's the CPU mesher, not the compiler).
- **REAL chunk-boundary straddle** (geometry, not a camera stat): one tavern built at x=26 → `phyxel.log`
  structure bbox **(25,17,8)–(41,27,14)** crosses the x=32 seam and logs `Created new chunk at (32,0,0)
  for placement` — its own voxels forced a 2nd chunk. Renders correctly across the seam, no
  crash/artifact (`screenshot_20260707_115336_404.png`); cross-*chunk* runs conservatively stop there.
- **Settlement — SCALE PROXY, explicitly NOT the documented 3.4M hills scene:** flat-world engine
  `build_settlement` seed 7, 4 structures / 2 chunks. A real multi-building scale test, ~20× smaller
  than the plan's hills settlement.
- **FPS is NOISE at these scales — no FPS win claimed.** Same tavern, same held+verified pose A,
  within-run OFF→ON: **Release 86.1 → 121.3 (+41%)** but **Debug 124.6 → 85.9 (−31%, opposite sign)**.
  Two builds disagreeing on the *sign* of the OFF→ON change on the identical scene proves the tavern
  (6k–68k faces) is not face-count-bound on the RTX 4090; OFF/ON FPS is dominated by the documented ±20%
  restart/transient variance. (An earlier lucky Release run read 120.8→197.8; not reproducible, not
  relied upon.) FPS would only be a trustworthy signal on a genuinely face-bound scene (the 3.4M hills
  settlement — not run).

**Not done (honest disclosure of skipped §5 items):**
- The **exact documented 3.4M-face Perlin-hills settlement** (20 buildings + 21,745 path microcubes)
  re-run in Release — NOT run (needs that specific world recipe; the 4-building flat settlement above is
  a smaller proxy). Remaining bar for a full closeout.
- **Worst-case all-micro 32³ chunk runtime test** — NOT run: a true all-micro chunk is 288³ = 24M
  microcubes (infeasible to place); micro is WITHIN-cube merged in the shipped state, already unit-tested
  (`buildMicrocubeSlab`). Becomes a meaningful runtime test only after Increment 4b (micro cross-cube).

**Remaining work:** Increment 4b (microcube cross-cube, 288³ — the last `DISABLED_` micro across-slab
red test) + the two §5 items above. The cross-cube subcube win already scales (settlement walls).

## Heavy-scene FPS validation (2026-07-07) — mechanism recovers FPS when face-bound

The Increment-5 closeout could NOT show an FPS win because a single tavern (6k–68k faces) is not
face-count-bound on an RTX 4090 (OFF/ON FPS was noise — Release +41% vs Debug −31%). The honest
question — *does the face-count reduction recover FPS on a genuinely dense scene?* — is answered here
on **flat, pathless, tavern-only grids** (engine-generated v2 taverns; a face-bound PROXY, explicitly
NOT the documented 3.4M-face Perlin-hills settlement — which remains not run). Release binary
hash-verified `2e5c9116` via `Get-Process`; camera held+verified via MCP `set_camera`; OFF→ON toggled
live within one process (no restart); 3 reads/state; raw
`docs/evidence/inc5_heavy_scene_fps_release.txt`:

| scene | faces OFF → ON | FPS OFF → ON (avg of 3) | recovery (all 3 reads / excl. 1 flagged read) | cpuFrame |
|-------|--:|--:|--:|--:|
| 9 taverns (6 chunks) | 639,585 → 53,219 (12.0×) | 41.5 → 206.5 | **5.0×** / 5.5× (excl. a 161 settling read) | 23 → 4.4 ms |
| 16 taverns (9 chunks) | 1,126,856 → 92,438 (12.2×) | 26.4 → 180.4 | **6.8×** / 7.7× (excl. a 134 transient dip) | 38 → 5 ms |

Both ratios are shown (the flagged reads — a first-read settle after the toggle re-mesh, and one dip —
are self-consistent with their cpuFrame, so they are real captures, not noise; excluded only to bound
the steady-state). Either way the recovery is **~5–8×**, far outside the tavern's ±20% variance band,
with a *consistent* OFF→ON sign (the tavern flipped sign = noise). cpuFrame tracks it (23→4.4 ms,
38→5 ms), and the faces↔cpuFrame relation (68k~10 ms, 640k~23 ms, 1.1M~38 ms) confirms these scenes
ARE face-bound while the tavern was not. Renders correctly merged at scale
(`screenshots/screenshot_20260707_130014_053.png` [9], `_130255_370.png` [16]).

**Conclusion (scoped honestly):** the greedy-mesh *mechanism* is proven to recover FPS (~5–8×) on
face-bound sub/microcube scenes *when enabled*. **It is NOT yet fielded:** `s_fineGreedyMerge` defaults
`false` and is reachable only via the debug route `POST /api/debug/fine_merge` (`grep -rn
"setFineGreedyMerge(true)"` → nothing). So a normal/default game session gets no benefit yet — the
original 49-FPS build-density cap is *resolvable* by this work but not *resolved* for players until the
toggle is wired on by default (or auto-enabled past a face-count/mesher-cost threshold) and
re-validated (re-mesh cost at scale; the empty-world 14→14 already shows no regression on merge-free
scenes). That default-on wiring is the remaining step to actually ship the win.

## ✅ Shipped ON by default (2026-07-07) — re-mesh cost measured acceptable

`s_fineGreedyMerge` default flipped `false`→`true` (`ChunkRenderManager.cpp:25`), so every session gets
the merge without the debug toggle. The per-face path stays reachable via `POST /api/debug/fine_merge
{"enabled":false}` for A/B. Validated before flipping:

- **Default-ON works out of the box** (Release binary `bd1f7bbee097`, hash-verified — differs from the
  old default-off `2e5c9116`): a 9-tavern scene renders at **53,219 faces / ~206 FPS with NO manual
  toggle** (would be 639,585 / ~43 FPS if the default were still off). The win is live for players.
- **Re-mesh cost — the one untested regression risk — MEASURED + persisted**
  (`docs/evidence/inc5_remesh_cost_release.txt`). Timing the full re-mesh of all 6 chunks of the
  9-tavern scene (the `/api/debug/fine_merge` toggle blocks until the re-mesh completes):
  **per-face ~535 ms vs merged ~594 ms = +59 ms (~11%, ≈10 ms/chunk)** (3 cycles, consistent). Honest
  framing (corrected after audit): this re-mesh runs on the **game loop**, i.e. it is a **main-thread
  rebuild STUTTER, not a free background cost** — the cleared log shows each toggle as a
  `STUTTER DETECTED` frame of ~230–315 ms (the full 6-chunk rebuild in one frame; this is why an
  auditor grep for "Face rebuilding complete" found no trace — the toggle logs the stutter, not a
  completion line). BUT the stutter is **not introduced by this change**: the per-face path *already*
  stutters on any chunk rebuild (~40–50 ms/chunk incl. the shared lighting bake); enabling merge-by-
  default makes that existing rebuild ~11 % worse (~10 ms/chunk), in exchange for the 5–8× **per-frame**
  render win afterward. Worst case is a full all-chunk re-mesh (the toggle); normal gameplay re-meshes
  ONE chunk per edit (~40–50 ms + ~10 ms), world-load meshes many chunks (one-time). Net: a clear win.
- **No test regression:** `FineFaceMerge.*` 18/18. The full suite shows 7 failures, all git-proven
  PRE-EXISTING and causally unrelated (the merge flag cannot reach material/nav/skeleton/inventory
  code): `MaterialRegistryTest.{HasCorrectMaterialCount,HasCorrectTextureCount,GetAllMaterialNames_HasAll}`
  hardcode counts (e.g. `getMaterialCount()==27`) but committed `resources/materials.json` has **91**
  (last changed 2026-07-04, two days before this work); `MaterialRegistryTest.SaveAndReload_Roundtrip`
  is a **distinct, separate pre-existing bug** (mass 1.5→1 serialization round-trip, ~50 materials),
  not the count mismatch; `InventoryTest.SelectSlot`, `NavGridTest.StepUpOneBlock`,
  `CharacterSkeletonTest.KneeElbowLimitsCorrect` are similar unrelated expectation drift. `FineMergeScope`
  (test RAII) fixed to restore the PRIOR flag value, not a hardcoded false, so the new default doesn't
  leak between tests.

## Root cause — greedy meshing covers cubes but NOT subcubes/microcubes

The static chunk renderer (`ChunkRenderManager`) emits **one `InstanceData` (8 B) per visible face**,
drawn as a quad (`static_voxel.vert`, vertexID 0–3). Face culling removes occluded faces, but coplanar
same-material faces are merged **only for CUBE faces**:

- **Cube faces (scaleLevel 0):** the 8 B instance's bits 20–31 carry the **greedy-merged rectangle
  extents** (`sizeU`, `sizeV` as size-1), so a flat run of cubes is ONE big quad. (That is why bare
  terrain is only 7.9k faces.)
- **Subcube (scaleLevel 1) / microcube (scaleLevel 2) faces:** bits 20–31 instead carry the **grid
  position** (parent subcube + microcube, encoded 3×3×3) needed for the sub-tile UV. There are **no
  free bits for merge extents**, so every subcube/microcube face is its own instance — **no merging**.

So microcube-dense surfaces (path ribbons, subcube building walls) explode: the 3.4M faces are almost
entirely sub/micro faces (terrain cubes merged to 7.9k).

## The encoding constraint (why this is non-trivial)

`InstanceData` is 8 B: position (15 bits) + faceID (3) + scaleLevel (2) + bits 20–31 (12) which are
**already doubly-booked** — merge extents for cubes, grid position for sub/micro. To greedy-merge
sub/micro faces you must encode BOTH grid position AND merge extent → does not fit 8 B. Options:

1. **Wider instance format for merged sub/micro runs** (e.g., a second 4–8 B field or a separate
   instance stream) carrying extent; shader tiles the sub-tile UV across the merged quad.
2. **"Full-tile" UV mode for same-material micro runs** (e.g., paths): a flag that makes each microcube
   show a FULL texture tile (not 1/9), so a contiguous same-material run merges into one quad with a
   normal tiled UV — cheap and ideal for paving/large detail surfaces, at the cost of the sub-tile
   parent-texture look for those runs.
3. **Distance LOD:** collapse microcubes→subcubes→cubes beyond N metres (most micro detail is
   sub-pixel at distance anyway).

## Plan (ranked by leverage / risk)

1. **(highest leverage, medium risk) Greedy-merge subcube + microcube faces** of the same material per
   chunk per face-direction. Needs option 1 or 2 above (encoding). Expected: the 3.4M → tens of
   thousands for this settlement (walls/paths are mostly flat same-material runs).
2. **(cheap, low risk) Full-tile UV merge for same-material micro runs** (paths/large surfaces) — a
   subset of #1 that sidesteps the sub-tile UV problem; big win for paved paths + flat walls.
3. **(medium) Distance LOD** for far chunks.

## Validation (required before claiming "it holds up")

Red-before-green on the SAME settlement scene: record `total_visible_faces` + frame/render ms
**before** (3.4M / 59 ms — the red baseline above), implement, then **after** — assert face count drops
≥10× and render ms drops materially, with the village still visually correct (screenshot diff). Also
take a **Release-build** measurement (Debug Vulkan is much slower than Release; the 15 FPS figure is
Debug). Encode a unit/bench assertion on the meshing pass (a flat NxN same-material micro region →
O(1) merged quads, not N² faces) shown failing on the current per-face path first.

## Known issue — T-junction cracks at greedy-merge borders ("dotted lines", found 2026-07-17)

**Symptom:** thin dashed/stippled bright lines on flat terrain, world-axis-aligned, "all over the
place but only visible from certain angles." First noticed on the Middle-earth 1:1 plains (vast
uniform grass = the ideal display case); present in same-day PRE-4.4 captures too — NOT a
regression from the large-world work.

**Root cause (proven by elimination + relocation, 2026-07-17):** greedy meshing emits adjacent
coplanar rectangles with different extents, so one quad's corner vertex lies mid-edge of its
neighbour (a T-junction). The long edge's interpolated depth doesn't exactly pass through that
vertex, leaving sub-pixel gaps that leak the bright background (sky/clear colour) when the eye
ray grazes along the shared edge. Diagnostics that pinned it: grass layer OFF → lines persist;
face_dir_cull OFF → persist; plan view (straight down) → invisible (so not drawn geometry);
**smooth_lighting toggle (changes the merge layout) → the lines MOVED to new positions** —
conclusive. Repro: MiddleEarth1to1, camera ~(60401, 26, 50800) yaw 45 pitch −30, oblique angles.

**Fix options (design decision — do NOT rush):**
1. **Merge-constraint** (forbid T-junctions by aligning merge spans across rows/neighbours) —
   simplest correctness; costs some of the 10–12× face reduction, measure before accepting.
2. **Matched underlay** (ground-coloured backdrop / clear colour so leaks stop being bright) —
   hides rather than fixes; near-zero cost; probably the right stopgap.
3. **Edge skirts** (tiny overlap on merged quads) — the standard voxel-engine remedy; touches the
   FRAGILE winding/packing path, needs the full visual A/B discipline.

## Known issue — character "speckle" (NOT shadow acne — theory falsified 2026-07-17)

Character models show speckled per-face noise ("static" look), worst under a high sun and
shimmering under animation. **Initial diagnosis (shadow-map acne) was FALSIFIED by experiment
2026-07-17:** the normal-offset port to `kinematic_voxel.vert`/`dynamic_voxel.vert` (0.15,
matching `static_voxel.vert:241`) did not change the speckle, and a DIAGNOSTIC exaggeration to
1.0 — which makes self-shadow acne physically impossible — left the speckle intact. The 0.15
port is KEPT as hygiene (matches the static path, no visible harm), but it is NOT the fix.

**Current best theory (unverified):** per-voxel TEXTURE content/sampling on sub-voxel character
models — each tiny voxel face maps a different sub-tile patch of a noisy skin/cloth texture, so
the model reads as per-face mottling that shimmers as animation resamples it (fits the "streaky"
head voxels seen up close, the per-face granularity, and the sun-angle contrast dependence).
Next steps: inspect the character .anim → material/texture mapping; A/B a big-voxel kinematic
object (furniture) beside the character — if furniture is clean, it's the character asset
texturing, not the kinematic pipeline; check mip/filter settings for the kinematic draw.
Observed while verifying (separate oddity, unchased): spawn_entity'd animated characters did
not RENDER in CharacterTestbed (entities exist logically, no visuals; the game.json player in
MiddleEarth renders fine — possibly a spawned-entity vs player init difference).

Related fact from the same session: at a 9:30 sun the user confirmed GRASS jitter visibly
improves (vs the noon default), and the character speckle improves only modestly — consistent
with the texture theory (contrast-dependent) and the grass aliasing mechanism above.

**Related (user-observed 2026-07-17): grass-blade "jitter" looks like the same speckle but is a
DIFFERENT mechanism** — grass.vert does NOT sample the shadow map (the logged "blades receive no
shadows" gap), so its shimmer is thin sub-pixel quads swaying in the wind with no AA, plus any
sun-direction lighting term flipping as blade normals sway. Aggravated by this engine's DEFAULT
sun being NOON (DayNightCycle boots at 12.0 — vertical light is worst-case for both artifacts on
vertical/thin geometry). Consider: blade-width floor in screen space or distance fade tune, and
revisit when blades get shadow reception. A softer default sun angle (e.g. 10:00) would visibly
calm BOTH artifacts for free — worth considering as a default.

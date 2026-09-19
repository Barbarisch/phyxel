# Ravenmere black wall — diagnosis (2026-09-17)

Defect: house_1's south wall (StoneBricks, world x −35..−33, z = 19, y 19–21) renders black at the
town spawn (user's window capture `rv_user_window_capture_3.png`). Reproduces in the probe build and
in the editor (`build/editor/Release/phyxel.exe --project PhyxelProjects/Ravenmere`, town scene,
probe field OFF, free camera (−36, 19.6, 13) yaw 90 pitch 5). Control: a fresh StoneBricks cube
placed at (−38, 19, 17) via `POST /api/world/voxel` (in-memory only, never saved).

## Method trap that cost most of the session

Editor and game screenshots are read AFTER `post_process.frag`, which applies exposure ×8 and the
AgX curve (crosstalk matrix + toe + shoulder) to everything, including debug-view output. Every
debug-view channel value read before the last step was therefore compressed, clamped or
cross-contaminated (a shader output of (1, 1, 0) reads back as (248, 247, 204)). Probe with
`POST /api/debug/tonemap {"curve":0,"exposure":1}` and one grey quantity per capture.

## Raw-linear measurements (curve 0, exposure 1; swapchain sRGB-encoded 8-bit)

Editor process `editor_rays17.log`, captures `rv_wall_editor_rays17_mode{0,3,4,7}.png`.

| quantity (debug view) | wall box | fresh cube box |
|---|---|---|
| mode 0 normal render | 1.0 / 1.1 / 1.2 | 8.0 / 8.4 / 8.3 |
| mode 3 traced sky visibility `skyVis` | 166.7 (= 0.385 linear) | 255 (= 1.0) |
| per-ray probe: normal ray escaped | 0 (blocked) | 255 (escaped → early exit 1.0) |
| per-ray probe: 30° up ray escaped | 254 | 255 |
| per-ray probe: 30° down ray escaped | 0 (ground) | 0 (ground) |
| per-ray probe: 30° west ray escaped | 254 | 234 |
| per-ray probe: 30° east ray escaped | 0.5 (blocked) | 0 (blocked) |

Predicted `skyVis` from the escape pattern: (0.866 + 0.866) / (1 + 4·0.866) = 0.39. Measured 0.385.

Same pose with a 1-wide StoneBricks column placed 10 u in front of the fresh cube (x = −38, z = 7,
y 18–22; removed afterwards): cube `skyVis` 1.0 → 174.2 (0.42 linear), normal render 8.0 → 5.2 /
4.7 / 4.2. Removing the column restores 8.0 / 8.4 / 8.3 (`rv_wall_editor_rays17blk_*.png`,
`rv_wall_editor_blocker_*.png`, `rv_wall_editor_unblocked_*.png`).

## Mechanism

`phxSkyVisibility` (occupancy.glsl) returns 1.0 as soon as the surface-normal ray (reach 16 u)
escapes. When that ray is blocked it averages five rays cosine-weighted around the NORMAL: the
normal, four at 30° (up, down, left, right). For any vertical exterior wall: the down ray always
hits the ground, and the normal + side rays hug the horizon, so a neighbouring building 13 u away
(the Bricks structure at z ≈ 6) blocks them. Nothing samples the upper sky, which is where a wall's
sky light actually comes from. The ambient then uses `skyVis²` (`phxAmbientAtmos`):
0.65·0.39² + 0.02 = 0.12 vs 0.67 for an unblocked surface → 5.6× darker, and the AgX toe turns
that into black. The result is also discontinuous: the same wall with nothing within 16 u reads
1.0.

Excluded along the way (all measured): shader edits of this session (pre-today shaders identical),
fine merge, BC7 cache, tint/state words, sub-voxels in the cells, mesh instance words (identical to
a fresh cube), normal map / tangent frame (`dot(N, Ng)` = 1 on both), albedo (equal), probe-field
branch (painted magenta, never taken), uniform-buffer races (same `elapsedTime` before/after the
loop), post-shader modification (a constant output reads equal on both).

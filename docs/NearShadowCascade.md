# Shadow cascades (near + mid + far)

**Status: ALL THREE CASCADES SHIPPED + VERIFIED 2026-08-06.** This doc began as the
near-cascade plan; it now records the full 3-cascade system.

| Cascade | Fit | Res | Texel | Casters | Notes |
|---|---|---|---|---|---|
| Near | 40 u | 4096² | 0.0195 u | chunks (48 u margin) + characters + kinematic + dynamic + **grass (only here)** | blade shadows resolve; receivers min-compose with mid |
| Mid | 420 u | 8192² | 0.1125 u | chunks (GPU-driven multidraw, default ON) + characters + kinematic + dynamic + foliage | the original map; D1 stats live here |
| Far | 1600 u | 4096² | ~0.9 u | chunks (multidraw) + **far terrain tiles + far-tree/structure LOD meshes** (depth-only variants, cached last-frame draw lists) | recorded every `s_farShadowCadence`=4 frames (skip = no clear = map persists); far_terrain.frag + far_tree_mesh.frag receive |

**Far cascade verification (2026-08-06):** shadow-only view = every LOD tree to the horizon
casts a directional shadow; lit view = grounded shadows across the whole band, 56 FPS at the
elevated dense-forest pose. `docs/evidence/far_cascade_{shadowonly,lit}_green.png`.

⚠️ **Lessons the far cascade cost a debugging round to learn:**
- **`build_shaders.bat` is an EXPLICIT list** — every new shader must be added to it, and a
  missing .spv fails pipeline init LOUDLY in phyxel.log ("Shadow variant init failed:
  cannot open shader"). Grep the log before debugging shader logic.
- A probe that modulates only the sun term is invisible through aerial haze at range —
  debug probes must output flat colors.
- Shadow-caster pipelines bake a STATIC viewport: create them against the map they will
  render into (this is now the third time this rule earned its keep).

Knobs: `POST /api/debug/shadow {"near_enabled","near_distance","far_enabled",
"far_distance","far_cadence","distance","mode"}`. Debug counters:
`lod_report.far_shadow` (chunks/tile/tree caster counts; -1 = far pass never recorded).

---

*(Original near-cascade record follows.)*

**Near cascade: SHIPPED + VERIFIED 2026-08-06 (red→green on the single-blade rig).** Save
point before this work: `ed00bbb6`.

**The green measurement** (tools/grass_shadow_width_sweep.py, same rig + method as the red):
- RED (pre-cascade, on record below): blade casters changed 40.1% of the view as unstructured
  blobs, mean 77/255 — "strictly worse than casting nothing".
- GREEN (post-cascade): the single blade casts a real, structured shadow at EVERY width
  (area grows with the width multiplier, peak saturates at 199/255 — the tool's own signature
  for a genuine shadow), and the numbers are **byte-identical at shadowDistance 80 and 420**:
  blade shadows now live in the near map and are fully decoupled from the mid distance.
  Control (grass removed): area 0, PASS. Shadow-only view: individual blade shadows read as
  ALIGNED DIRECTIONAL STREAKS (`docs/evidence/near_cascade_blade_streaks_shadowonly.png`).
- **Bug found by the first (all-NO-SHADOW) sweep run:** the grass shadow pipeline bakes a
  STATIC viewport and was built against the 8192 mid map — drawn into the 4096 near
  framebuffer, blade depth landed at 2× the intended UVs. It is now built against the near
  map's render pass/extent. ⚠️ Any pipeline recorded into a shadow map MUST be created
  against THAT map's extent (or use dynamic viewport).

Implementation notes vs the original plan below:

- **Both maps, min-composed.** Receivers (voxel/grass/foliage frags) take
  `min(nearFactor, midFactor)` instead of a hard cascade select + blend band. min() is the
  union of shadows, so a caster recorded in only ONE map still shades every receiver — which
  is what lets grass cast ONLY into the near map (its proxy is sub-texel in the mid map =
  noise) and foliage stay mid-only (its shadow vert projects with the mid matrix) without
  either vanishing anywhere. The near map's existing 12% border fade IS the split blend.
- **Plumbing:** UBO gains `biasedLightSpaceNear` + `shadowCascadeNear` (x=rangeEnd 0=off,
  y=nearDepthRange, z=blend halfwidth) + raw `lightSpaceMatrixNear` (grass caster
  projection), appended after `grassDisplacerMeta` per the trailing-field rule. Descriptor
  set-0 gains binding 9 (near map sampler, appended at the END), with a fallback to the mid
  map so the binding is always valid; the near-cascade-off state makes the fallback inert.
  The long-undersized reflection descriptor pool was fixed to cover the full layout.
- **Fit:** `RenderCoordinator::fitShadowVolume()` — the mid map's exact fit (sphere fit,
  caster margin, world-anchored texel snap, NaN guard) extracted verbatim and called twice.
  Near: 4096² fitted to `s_nearShadowDistance` 40 u ⇒ 0.0195 u texel.
- **Caster pass:** `renderShadowPass(map, matrix, cull, nearPass)` records twice. Near pass =
  chunks (tight cull) + characters + kinematic + dynamic + GRASS (moved here from mid);
  GPU-driven multidraw + pipeline-stats stay mid-only. Grass's blade-width shadow clamp now
  uses the NEAR texel.
- Knobs: `POST /api/debug/shadow {"near_enabled": bool, "near_distance": N}`.

## Why

One shadow map is fitted to the whole shadow distance (default 420 u), giving a **0.1125 u texel
everywhere** — including at your feet. A grass blade's shadow proxy is 0.040 u × `shadowWidthScale`
2.0 = **0.080 u, i.e. 0.71 of a texel.**

Sub-texel casters do not fail cleanly. They rasterize **sporadically** — each blade either catches a
texel centre or misses, essentially at random. Measured by A/B on 2026-08-05 (toggling
`castShadows` on the 20×20 lab plane): the grass casters change **40.1% of the view** with mean
darkening **77/255**, producing a random scatter of blobs with no directional structure. That is
strictly worse than casting nothing — it reads as dirt on the ground.

Texel size is set by how far the map is *fitted*, not by how far the grass is from the camera, so
**no per-blade tuning can fix it.** Widening the proxy until it clears a texel brings back the fat
mushy smudge that was rejected earlier the same day (a shadow ~5× wider than its caster).

## The fix

A second, tight shadow map over the near field. At a 40 u fit and 4096², the texel is
`2 × 40 / 4096 = 0.0195 u` — a 0.080 u proxy spans **4.1 texels** and resolves as an actual blade.

| | far map (existing) | near map (new) |
|---|---|---|
| fit | 420 u | **40 u** |
| resolution | 8192² | **4096²** |
| texel | 0.1125 u | **0.0195 u** |
| blade proxy | 0.71 texel — noise | **4.1 texels — resolves** |

## Work

1. **Second `ShadowMap` instance** in `RenderCoordinator` (`shadowMapNear`), fitted to
   `kNearCascadeRange`. `ShadowMap` is already parameterised on size and range, so this is
   construction plus a second `getLightSpaceMatrix` call — no changes to the class.
2. **Descriptor layout**: a new binding for the near depth sampler. ⚠️ This is the riskiest step —
   the set-0 layout is shared by every scene shader. Add the binding at the END and leave existing
   indices untouched.
3. **UBO**: a second biased light-space matrix (`biasedLightSpaceNear`). Append after
   `grassDisplacerMeta` per the trailing-field rule, so only the shaders that declare that far need
   updating.
4. **`lighting.glsl`**: select the cascade by view distance and sample the right map. One function,
   so every consumer (voxel/grass/foliage/far terrain/far tree) gets it at once. **Blend across the
   split** or the boundary shows as a hard ring.
5. **Record shadow draws twice** — near map gets only chunks within its range. This is the cost:
   near-field geometry is drawn into both maps. Budget it against the ~131 shadow draws already
   measured; the near set should be much smaller.
6. **Cull sub-texel casters** in whichever cascade cannot resolve them, so far grass stops emitting
   noise instead of emitting it more cheaply.

## Verification

- **Red first**: the A/B above is the red measurement — grass casters change 40.1% of the view as
  unstructured blobs. After the cascade, blade shadows must be *directional streaks*, and the same
  A/B should show structure rather than scatter.
- Shadow-only view (`/api/debug/shadow {"mode":1}`) on the 20×20 lab plane, sun at 15:30.
- ⚠️ The lab plane sits near the hash-domain wrap and is only 20 u — fine for shadow work (unlike
  wind, which needed a field bigger than one gust front).
- Watch for a visible **ring at the cascade split** and for **peter-panning** at the boundary.
- Perf: shadow pass was already 24–26 ms of a 34.8 ms frame (`docs/RenderDensityPlan.md` §2d).
  Adding draws to it needs measuring, not assuming.

## Risks

- **Descriptor layout change touches every scene shader.** Highest-risk step; do it alone and
  verify nothing renders black before moving on.
- **The shadow pass is already the frame's dominant cost.** If the near map's draw list is not
  genuinely small, this makes the worst pass worse.
- `lighting.glsl` is shared by five shaders. A mistake here turned every static voxel black earlier
  in this session — change it in one step, with a screenshot check immediately after.

## Known issues

- **The dark circle (2026-08-07, OPEN — grass casting default OFF until fixed).** Because
  blades cast ONLY into the ~40 u near cascade, the cascade's camera-following coverage
  reads from any elevated camera as a giant dark **circle** gliding along with the view:
  inside it thousands of blade micro-shadows darken the ground a notch, outside it the
  ground gets no blade shadows at all, and `phxShadowBorderFade` rounds the seam into a
  clean disc. A/B-verified live (toggling `/api/debug/grass {"castShadows":...}` adds/
  removes the circle at a fixed camera; screenshots 20260807_123553 vs _123733).
  **Mitigation shipped:** `GrassRenderPipeline::s_castShadows` now defaults **false**
  (runtime knob unchanged). **The real fix when we come back:** fade blade shadow
  STRENGTH radially toward the near-cascade edge (blades fully shadow at your feet,
  taper to zero approaching the fit distance) so there is no step to see — or
  compensate outside the disc with a matching ambient/AO term. Re-enable the default
  and re-run the elevated-camera A/B when done.

# Near shadow cascade

**Status: planned, not started.** Save point before this work: `ed00bbb6`.

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

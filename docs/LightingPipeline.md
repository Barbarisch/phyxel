# Lighting Pipeline

**Last updated: 2026-09-17.** THE current-state reference for how this engine lights a frame.
Every change to a lighting or shadow shader, to the cascade fit, to the occupancy traces, or to
the probe field **updates this document in the same commit** (see §0.4 — the check in
`tools/lighting_doc_check.py` fails the build otherwise). Plans and their history live in
[`UnifiedLightingPlan.md`](UnifiedLightingPlan.md) and [`NearShadowCascade.md`](NearShadowCascade.md);
those are narratives with superseded sections. **This file states only what is true now.**

> **Why this rewrite (2026-09-17).** The previous version (2026-08-11) still described the deleted
> flood-fill skylight and block light, said the sun was "sky-gated", and called the character shader
> "not on the shared model". Reading it, an agent concluded the wrong things three times in one day
> and shipped a wrong shadow fix (Ravenmere G-135). The per-receiver matrix in §0 is derived from the
> shader source, function by function, and is the thing to keep true.

---

## 0. The model — what each term answers, who computes it, who consumes it

### 0.1 The questions a lit pixel asks, and the ONE answer to each

| # | Question | The answer (function, file) | Resolution / range | Who may NOT answer it |
|---|---|---|---|---|
| A | What colour is the sun / sky fill / haze / moon right now? | Atmosphere model (§1), `Atmosphere.cpp` ↔ `atmosphere.glsl`, into `ubo.sunColor / ambientColor / haze* / moonColor` | per frame | any hand-tuned ramp |
| B | **Is the straight line to the sun blocked?** (direct sun) | The **shadow map**: `phxShadowPCSS` / `phxShadowFast` in `lighting.glsl`, near ∪ mid cascades (`min`), far cascade for far LOD meshes | near 0.02 u, mid 0.11 u, far ~0.9 u texels; 40 / 420 / 1600 u | any sky/enclosure scalar. Inside shadow coverage `phxSunGate` returns 1 — see rule R1; beyond it the probe field's enclosure gate (`phxSkyAccessOf`) stands in |
| C | Where does the sun fall once nothing blocks it? | `pbrBRDF` (ground, Cook-Torrance) or Lambert/Blinn-Phong (vegetation, characters, glass) × `ubo.sunColor` | per fragment | — |
| D | **How much indirect light (sky + bounces) arrives here, per direction?** | The **probe field**: `gi_probe.comp` traces 18 directions per probe through the **micro-resolution occupancy** (1/9 u cells, reach 16 u) and stores an **ambient cube** (six cosine lobes ±X ±Y ±Z); `phxGiIrradiance` in `gi_field.glsl` samples it trilinearly (buried and back-facing probes weighted out) for the receiver's normal | 55,296 probes, 48×24×48 at 2 u around the viewer, 1/8 refreshed per frame; outside the grid = open sky | **no receiver traces its own sky** (rule R9). `phxSkyVisibility` is deleted from GLSL; the probe pass bounces off the field itself |
| E | What is the ambient fill? | `phxAmbient(worldPos, N, occBox, grid, ubo.ambientColor)` in `gi_field.glsl`: the probe field's answer (D) + the floor `kAmbientFloorAtmos`, faded into the analytic open-sky hemisphere `phxAmbientAtmos(N, 1.0, …)` over the grid's outer 4 probes and wherever the field is off | per fragment (ground, glass, foliage, characters), per blade vertex (grass) | a second ambient formula anywhere; `phxAmbientAtmos` with a sky scalar ≠ 1.0 (rule R9) |
| F | Does a point/spot light reach here? | `phxLightVisibility` (`occupancy.glsl`): one DDA from the surface to the emitter, emitter run-length excluded | per fragment, 32 point + 16 spot, forward loop | — |
| G | Moonlight | same directional path as C, **unshadowed**, scaled by the probe field's enclosure gate `phxSkyAccessOf` (no moon shadow map) | — | — |
| H | Emission | material emissive tint (`isEmissive` path in `voxel.frag`); block light **no longer exists** (U7 stage 2) | — | — |
| I | Distance haze | `phxAerialPerspective` | per fragment | — |
| J | Tone map / exposure | **once**, `phxTonemap` in `post_process.frag` (AgX, exposure 8.0) | per frame | scene shaders (none call it any more) |


### 0.2 Receiver matrix — every shader that lights a surface (from the source, 2026-09-17)

| Shader (pipeline) | Sky access (D) | Ambient (E) | Direct sun (B × C) | Shadow filter / cascades | Moon | Point/spot (F) | Haze | Notes |
|---|---|---|---|---|---|---|---|---|
| `voxel.frag` — static chunks, kinematic voxels (doors, furniture), GPU debris | probe field, shading normal (`phxSkyAccessOf` for the gates) | `phxAmbient` | `pbrBRDF × shadow × phxSunGate` | PCSS, mid ∪ near | yes, × enclosure gate | yes, with visibility trace | yes | `vSkyLight` varying is a dead constant 1.0; the kinematic `setLightSampler` feed is not read here. **Surface modifier (P4 damage cracks, `crack.glsl`):** on damaged STATIC CHUNK faces only, albedo and roughness are modified BEFORE lighting — crack pixels darken ×0.18 and go fully rough. It consumes no lighting input and feeds none; it is a material-state treatment in the same class as `vState` charred/wet. Reachable only on `static_voxel.vert` — the kinematic and dynamic paths hardcode `flags = 0u`, so damage bits and the world-position seed are both unavailable there |
| `transparent_voxel.frag` — glass | probe field, face normal | `phxAmbient` | Blinn-Phong × shadow × `phxSunGate` | PCSS, mid ∪ near | no | yes, with visibility trace | — | `vSkyLight` varying is a dead constant 1.0 |
| `grass.frag` (+`grass.vert`) — blades | probe field per **blade vertex** (up normal): `vAmbient`, gate `vSky` | `vAmbient` (= **`phxAmbientUp`**, the up-facing fast path — see §2) | `0.85 × shadow × phxSunGate` | **Fast 4-tap**, mid ∪ near | no | yes, with visibility trace | no | wind sheen also × `vSky`; `grass_shadow.vert` computes neither (caster only) |
| `foliage.frag` — leaf cards | probe field per fragment (up) | `phxAmbient` (up) | `0.7 × shadow × phxSunGate` + backlit translucency × (0.25+0.75·phxSunGate) | **Fast 4-tap**, mid ∪ near | no | yes, with visibility trace | no | — |
| `character.frag` — animated characters | probe field per fragment, vertex normal | `phxAmbient` (N) | Blinn-Phong × shadow × `phxSunGate` | PCSS, mid ∪ near | yes, × enclosure gate | yes, with visibility trace | no | block-light term from the bake is 0 |
| CPU debris (`DebrisRenderPipeline` light sampler) | **per-cell bake** at the body (the last per-cell consumer — §8) | CPU: `ambient + sun × 0.5 × sky²` | **no shadow map** | — | no | no | — | flat per-body light; the only place a sky gate still scales sun, because there is no map lookup and the CPU cannot read the probe field |
| `far_terrain.frag`, `far_tree_mesh.frag` — far LOD | open sky (outside the probe grid by definition) | `phxAmbientAtmos(N, 1.0, …)` = the field's own fallback | `ndl × shadow` | Fast 4-tap, **far cascade only** | no | no | yes | — |
| `water.frag`, `water_cell.frag`, `water_underwater.frag` | none | own constants | unshadowed | none | — | — | — | own model |
| `sky.frag` | — | — | — | — | — | — | — | emits the atmosphere |

Shared code: `lighting.glsl` (analytic hemisphere, shadow filters, `phxSunGate`, haze, tone map),
`occupancy.glsl` (occupancy query, DDA, light visibility; sky visibility for the probe pass only) and
`gi_field.glsl` (the probe field: `phxAmbient`, `phxGiIrradiance`, `phxSkyAccessOf`). **Never
re-inline any of it into a single shader** — five hand-synced copies is how grass went its whole life
with no shadow lookup, and `voxel.frag`'s private copy of the probe sampler is how the field stayed
a ground-only experiment for two weeks.

### 0.3 Rules — each one was a shipped defect

- **R1. Direct sun is the shadow map's answer wherever the map covers the fragment.** `phxSunGate`
  returns 1 inside the fitted volume (blending through the map's 12 % border fade) and the sky
  gate only outside it. Multiplying the sun by `skyVis²` on top of the map stamped a canopy's
  five-ray vertical footprint onto the ground as hard 1-m blocks beside the correct shadow —
  Ravenmere G-135, 2026-09-17. Beyond the maps the probe field's enclosure gate stands in.
- **R2. One ambient owner.** Every receiver calls `phxAmbient` (`gi_field.glsl`); the analytic
  hemisphere `phxAmbientAtmos` is called with sky = 1.0 only, as the field's fallback (enforced, R9).
  A receiver with its own ambient maths is a second lighting model.
- **R3. Near and mid cascades are min-composed, never selected.** `min(near, mid)` is the union of
  shadows, so a caster recorded in only one map still shades. The near map's border fade is the blend.
- **R4. Grass casts into the near cascade only, and `GrassRenderPipeline::s_castShadows` is
  `false` by default** (a camera-following dark disc from above).
- **R5. Shadow-caster pipelines bake a static viewport: create them against the map they render
  into; `VK_COMPARE_OP_LESS`, never the scene's reverse-Z compare.**
- **R6. Occupancy flags gate every trace.** `ubo.occupancyBox.w`: bit0 occupancy readable, bit1
  light tracing (`VulkanDevice::setLightTracingEnabled`, default ON), bit2 sky tracing for the
  probe pass's bounce estimate (`setSkyTracingEnabled`, default ON), bit3 probe field readable
  (`POST /api/debug/gi`, **default ON** since G-141 — the kill switch, not a feature flag). With a
  bit clear the corresponding function returns 1.0 / false and the fallback buffer must not be read.
- **R7. Measure, never eyeball — and never through the tone map.** Shadow-only view (debug mode 1)
  and per-term views (3 enclosure gate, 5 forward lights, 6 direct, 7 ambient) exist so a term can
  be isolated; `tools/lighting_stats.py` and `tools/ambient_model_check.py` for numbers. Every
  screenshot passes through exposure ×8 + AgX (`post_process.frag`), debug views included, which
  crushes, clamps and cross-contaminates channel values: read a debug view only with
  `POST /api/debug/tonemap {"curve":0,"exposure":…}` and one grey quantity per capture (that
  omission cost most of a day on G-141). A frame that does not contain the defect proves nothing.
- **R8. Occlusion is a property of matter, not of the voxel size that stores it.** Every light
  query — shadow casters, the point-light trace, the probe traces — is answered against the
  **micro-resolution** occupancy (1/9 u); a 1-micro roof seals a room exactly as a cube roof does
  (`ambient_model_check.py` A3). No lighting path may introduce a per-cube approximation; the one
  survivor (CPU debris reading the per-cell bake) is logged in §8, not tolerated as a pattern.
- **R9. No receiver traces its own sky.** `phxSkyVisibility` (five rays fanned around the surface
  NORMAL, so they hug the horizon on any wall) is deleted from GLSL. Used as a receiver term it made an exterior wall facing a neighbour 13 u away read
  0.39 sky, squared into a 5.6× darker ambient — black — while the same wall with nothing within
  16 u read 1.0 (Ravenmere G-141, 2026-09-17). `tools/lighting_doc_check.py` fails the build on
  any `.frag`/`.vert` that calls it or `phxSkyGate`.

### 0.4 Change discipline (enforced)

1. Any change under `shaders/lighting.glsl`, `shaders/occupancy.glsl`, `shaders/gi_field.glsl`,
   the receiver shaders in §0.2, `RenderCoordinator::fitShadowVolume` / `renderShadowPass`,
   `gi_probe.comp` / `GiProbeField`, or the occupancy upload **updates §0 (matrix + rules) and
   appends a line to §9 in the same commit.**
2. Then run `python tools/lighting_doc_check.py --update`, which stamps the fingerprint of the three
   shared includes into §9. `build_and_test.ps1` runs `--check`: a shared-include change without a
   doc update fails the build, and so does any direct-sun term that multiplies a sky gate without
   going through `phxSunGate` (R1), any receiver calling `phxSkyVisibility` / `phxSkyGate` (R9), any
   receiver calling `phxAmbientAtmos` with a sky scalar other than 1.0 (R2/R9), and any
   lighting-model shader missing from the matrix.
4. An ambient-model change runs `python tools/ambient_model_check.py --check <tag>` on the Lighting
   Lab (editor on StructGenTest) and quotes its five numbers: continuity (A1), sealed (A2),
   micro-roof resolution (A3), opening (A4).
3. A visual claim about lighting needs the defect **in frame** in both the before and the after
   capture, at the same pose, with the pose stated.

---

## 1. The atmosphere is the source of truth for light

`engine/include/graphics/Atmosphere.h` + `engine/src/graphics/Atmosphere.cpp` (CPU) and
`shaders/atmosphere.glsl` (GPU). Rayleigh + Mie + ozone single scattering, ray-marched through a
spherical shell at Earth scale, with quadratic step spacing.

It answers four questions that used to be four independently hand-tuned constant ramps in
`DayNightCycle` and `lighting.glsl`:

| Question | Function | Feeds |
|---|---|---|
| What colour is the sun? | `sunlightColor(toSun)` | `ubo.sunColor` |
| What colour is the sky fill? | `skyIrradiance(toSun)` | `ubo.ambientColor` |
| What colour is the distance haze? | `hazeHorizon` / `hazeZenith` | `ubo.hazeHorizonColor` / `hazeZenithColor` |
| What colour is moonlight? | `moonlightColor(toMoon, phase)` | `ubo.moonColor` |

Because they share one transmittance, a warm sun always arrives with a warm horizon and cool
shadows. **There is no sunset colour ramp and there should never be one again** — a horizon sun
measures R/B > 10 purely because the long slant path scatters blue away.

⚠️ **Direction convention.** `Atmosphere::` and `atmosphere.glsl` take `toSun` / `toMoon` pointing
**at** the body. `ubo.sunDirection` / `ubo.moonDirection` are the opposite — the direction light
*travels*, downward at noon. Passing one unflipped renders a permanent midnight. Pinned by
`AtmosphereTest.DirectionConventionIsTowardTheBody`.

⚠️ **Two implementations, one set of constants.** `AtmosphereTest.ShaderConstantsMatchTheCppModel`
parses `atmosphere.glsl` and asserts all 17 shared constants equal the C++ ones. Keep the GLSL
declarations in the plain `const float kName = <number>;` form the regex reads.

### The sky pass
`shaders/sky.{vert,frag}`, built by `RenderPipeline::createSkyPipeline`, drawn first in the scene
pass by `RenderCoordinator::drawSky`. Push constants only — no descriptor sets, no vertex buffer,
three vertices from `gl_VertexIndex` — with **depth test and write OFF**, so it fills the frame and
geometry draws over it. This replaced the flat clear colour; `PostProcessor::setSkyColor` is now
only a fallback for when the sky pipeline fails to build.

The view ray comes from **camera basis vectors** scaled by the projection's focal terms (signed, so
the Vulkan Y-flip rides inside `camUp`), deliberately not from `inverse(viewProj)`: the scene uses
reverse-Z with an infinite far plane, and un-projecting a clip point is three chances to get a
convention wrong.

### Apparent size of the sun and moon — a stylized choice
Both bodies are drawn at **5× life size** (`kSunSizeScale`), i.e. ~2.7° across instead of ~0.5°. At
true size each is a ten-pixel dot and the moon's phase is invisible; oversizing is the near-universal
game convention for exactly that reason.

⚠️ **Drawn size must not affect light timing.** The horizon fade — how fast direct sunlight dies as
the sun dips — uses `kSunPhysicalAngularRadius`, not the stylized radius, because it models the real
disc crossing the real horizon. Deriving it from the drawn size would leave the sun lighting the
world ~2.7° below the horizon (shadows at dusk). Pinned by
`AtmosphereTest.StylizedDiscSizeDoesNotAffectLightTiming` and `NoDirectSunlightBelowTheHorizon`.

⚠️ Disc edge softening (`kDiscEdgeAngle`) is an **absolute angle**, not a fraction of the radius. As
a fraction it scaled with the disc, so at 5× the antialiasing band was 5× wider and the sun read as a
soft blob.

### Configuring the sky — multiple suns and moons
Celestial bodies are **data**, not two hardcoded cases (`graphics/CelestialBody.h`). A body is a
disc with a size, an orbit, and a way of getting its light: it either **emits** (a star) or
**reflects** another body's light (a moon, which therefore has phases).

Author it in `game.json`:

```json
"sky": { "bodies": [
  { "name": "sun",  "angularDiameterDeg": 2.7, "emissive": true,  "periodDays": 1.0 },
  { "name": "luna", "angularDiameterDeg": 7.0, "emissive": false, "litBy": 0,
    "albedo": 0.12, "lightScale": 0.25,
    "periodDays": 1.037, "phaseOffset": 0.5,  "tint": [0.62, 0.78, 1.0] },
  { "name": "rust", "angularDiameterDeg": 4.5, "emissive": false, "litBy": 0,
    "albedo": 0.18, "lightScale": 0.25,
    "periodDays": 0.7,  "phaseOffset": 0.62, "planeTiltDeg": 28.0,
    "tint": [1.0, 0.45, 0.30] }
]}
```

| Field | Meaning |
|---|---|
| `angularDiameterDeg` | Drawn size. Real bodies are ~0.5°; the default 2.68 is 5× life, deliberately. |
| `discBrightness` | Disc brightness. Defaults to 24 for a star, **2.2 for a reflective body** — a star's value on a moon clips the disc to white and destroys its tint. |
| `tint` | Colour of both the disc and the light it gives. |
| `emissive` / `litBy` | A star, or lit by body index `litBy` (`-1` = the first star). |
| `albedo`, `lightScale` | Reflectance, and the honest cheat knob for how much light it delivers. |
| `castsLight` | `false` = drawn but contributes no light at all. |
| `periodDays` | Days per circuit. 1.0 = once per in-game day. |
| `phaseOffset` | Where in the circuit it starts, in turns. At `periodDays: 1`, this **is** the phase. |
| `planeTiltDeg` | Tilt out of the sun's plane, so a body traces a visibly different arc. |

Live tuning, no rebuild — `POST /api/debug/sky`:
`{"reset": true}` · `{"sizeScale": 2.0}` · `{"bodies": [...]}`. Always responds with the resulting
list, so it also serves as a query.

⚠️ **Only ONE body can cast shadows.** The cascades are fitted to a single direction, so the
brightest light-contributing body currently *above the horizon* owns them and every other body adds
**unshadowed** light. On a moonless night there is no caster and the cascades are left alone rather
than fitted to a light below the ground.

⚠️ **The sky's scattering follows the primary STAR, never the dominant light.** "What lights the
ground right now" becomes the moon at night; "what illuminates the atmosphere" is always the sun.
Conflating them renders a full daylight sky at midnight.

⚠️ Missing, empty or malformed `sky` falls back to the default sun + moon. A world with no sun is
never what was meant.

### The moon
`DayNightCycle` places the moon by lagging the sun's hour angle by `2*pi*phase`, with the phase from
WorldClock's 28-day cycle — so a **full moon rises at sunset because the geometry says so**. The
disc's terminator is not a parameter either: `phxMoonDisc` reconstructs the sphere normal per pixel
and tests it against the sun, so the drawn phase always agrees with the orbit.

⚠️ `setDayNumber` **must** call `recalculate()`. The day number drives the phase, hence the moon's
position and light. While it was an inert setter, the API (which sets `timeOfDay` first, and
`setTimeOfDay` does recalculate) rendered every moon with the *previous* day's phase.

---

## 2. Ambient — the probe field (and what it replaced)

The flood-filled skylight and the RGB block light of the original engine are **gone** (U7 stage 2
deleted block light; `static_voxel.vert` emits `vSkyLight = 1.0` as a placeholder). Since
2026-09-19 (G-141) there is **one** ambient source for every receiver in §0.2:

- **The probe field** (`gi_probe.comp` → SSBO binding 13, `GiProbeField.cpp`, `gi_field.glsl`).
  48×24×48 probes at 2 u spacing snapped to a lattice around the viewer, 1/8 of them refreshed per
  frame. Each probe traces 18 fixed directions (6 axes + 12 edge midpoints) through the sub-voxel
  occupancy (DDA in 1/9 u cells, reach 16 u): an escaping ray contributes the sky radiance
  `ubo.ambientColor`; a hit contributes the light **leaving the hit surface**,
  `0.30 × (indirect + sun × N·L × sunVisible / π)`, where `indirect` is **this field's own value at
  the hit point** (last refresh, sampled with `phxGiIrradiance`) and `sunVisible` is one DDA from
  the hit point toward the sun. Light therefore hops probe to probe: a doorway's light reaches the
  far wall after a few refreshes (2 u per hop, 8 frames per hop) and interreflection converges
  geometrically (gain 0.30 per bounce). The 18-direction set is **rotated by a per-probe,
  per-refresh random rotation** and the new estimate is **blended into the stored value**
  (`kBlend` 0.30 — the DDGI recipe), so the field integrates hundreds of directions over a second
  and a 1-wide doorway on an odd coordinate is found even though probes sit on the even lattice.
  Each direction's radiance is deposited into the six **ambient-cube lobes** it faces (weight =
  cosine to the axis), so a probe stores irradiance per hemisphere, not one scalar. A probe buried
  in solid is marked invalid; a freshly valid probe takes the estimate outright.
- **World-stable (scrolling) addressing.** The grid follows the viewer by re-snapping its origin
  to the lattice, but a probe is addressed by its world lattice coordinate wrapped into the slot
  array (`phxWrapSlot`, floor-division — **never GLSL `%`, whose result is undefined for negative
  operands and was measurably wrong here**), and each slot carries its lattice coordinate as a tag
  (lobes 2–4 `.a`). A probe therefore keeps its slot and its blended history while it stays inside
  the grid; a slot that has just scrolled in is recognised by its stale tag, read as invalid until
  the probe pass rewrites it, and starts fresh. Without this (the first G-141 build, one day) every
  2 u of camera travel shifted the whole buffer under every surface and the temporal blend spent
  ~3 s fading the wrong light out: Lighting Lab door-room wall after a camera trip +33 % then
  −56 % over 3 s; with scrolling the same trips leave it within the field's own noise.
- **Receivers** call `phxAmbient(worldPos, N, occBox, grid, sky)`: trilinear over the 8 surrounding
  probes, evaluated as `Σ N_axis² × lobe(sign N_axis)`; a neighbour counts only if it is in air,
  in front of the surface plane, and **visible from the surface** (one short DDA ≤ 2√3 u through
  the micro occupancy — the rule-R8 guard that stops light leaking through a wall). The result plus
  the floor `kAmbientFloorAtmos × sky` is faded into the analytic open-sky hemisphere over the
  grid's outer four probes; outside the grid, or with bit 3 clear, the analytic open-sky term is
  the answer; inside the grid with no reachable probe (a pocket narrower than the 2 u lattice) the
  floor alone. Grass evaluates it per blade vertex (`grass.vert` → `vAmbient`); everything else per
  fragment. Debug view 4 shows the validity outcome per pixel.
- **The up-facing fast path** `phxAmbientUp(worldPos, occBox, grid, sky)`, used by `grass.vert`
  only. Identical field, lattice and fallbacks, specialised for N = (0,1,0) and with no
  per-neighbour visibility trace. Grass evaluates ambient 24 times per blade, so its cost follows
  blade COUNT rather than screen coverage: the general `phxAmbient` there measured +20 to +22 ms
  per frame in Ravenmere town at every pose (§7, G-146). Dropping the X and Z lobes is EXACT for an
  up normal, since their ambient-cube weights are zero; dropping the visibility trace is the one
  approximation, and it measured at 0.1 % on the indoor-grass band of all three enclosed lab rooms.
- **The enclosure gate** `phxSkyAccessOf(ambient, N, sky)` (ambient luminance over the open-sky
  answer for the same normal, 0..1, unsquared) is the only "how enclosed is this point" scalar left.
  Two consumers: unshadowed moonlight, and direct sun beyond the shadow cascades' coverage.

**What it replaced, and why (the G-141 record).** `phxSkyVisibility` is **deleted from GLSL**
(the CPU mirror survives for the debris bake). From M3 (2026-08) to G-141 every receiver traced
its own sky: five rays fanned around the surface *normal* (the normal, then four at 30° off it),
cosine-weighted, early-out 1.0 when the normal ray escaped. That set was chosen to make a *sealed
room* read 0 — which it did — but for a vertical exterior wall it is the wrong estimator: the
downward ray always hits the ground and the normal and side rays hug the horizon, so a building
13 u away blocked them and nothing sampled the upper sky where a wall's light comes from. Measured
raw (tone map off, `docs/evidence/ravenmere/rv_black_wall_diagnosis_2026-09-17.md`): sky 0.385 on
the wall, 1.0 on a test cube two cells away; `skyVis²` then made the ambient 5.6× darker and the
AgX toe made it black. It was also discontinuous (a wall with nothing within 16 u read 1.0) and
per-fragment expensive, and the same scalar had gated direct sun until G-135. The lab check that
now pins the model (`tools/ambient_model_check.py`) failed on it 4/5.

**The per-cell bake** (`ChunkManager::sampleBakedLight`, one value per cube cell, traced at bake
time) is still read by CPU debris only (§8); characters and kinematic voxels still upload it but no
shader reads the value.

The occupancy the probe pass reads is the same sub-voxel occupancy the mesher builds (subcube and
microcube leaf-accurate, `m_subOcc` / `m_microOcc`), uploaded as two SSBOs (bindings 11/12) and
kept current with edits; `POST /api/debug/light_occupancy` reports per-micro counts. Bindings
11–13 are visible to the vertex, fragment and compute stages.

---

## 3. Shadows — three cascades, one rule

Created in `RenderCoordinator` (`fitShadowVolume`, `renderShadowPass`); design record in
[`NearShadowCascade.md`](NearShadowCascade.md).

| Cascade | Resolution | Fit distance | Texel | Casters | Receivers | Update |
|---|---|---|---|---|---|---|
| Near | 4096² | 40 u (`s_nearShadowDistance`) | 0.0195 u (fit sphere + 48 u caster margin ⇒ coarser in practice) | chunks, characters, kinematic, dynamic, grass (off by default) | everything within 40 u of the camera, min-composed with mid | every frame |
| Mid | 8192² | 420 u (`s_shadowDistance`) | 0.1125 u | chunks (GPU-driven multidraw), characters, kinematic, dynamic, foliage | ground, glass, grass, foliage, characters | every frame |
| Far | 4096² | 1600 u (`s_farShadowDistance`) | ~0.9 u | far terrain tiles + far tree/structure LOD meshes **only** | `far_terrain.frag`, `far_tree_mesh.frag` only | every `s_farShadowCadence` = 4 frames; the sampling matrix is latched to the render |

**Fit:** the view-frustum slice's bounding sphere (rotation-invariant, no shimmer when turning),
`+48 u` caster margin on the radius, `kCasterBack = 120 u` pulled toward the light so casters
between the sun and the volume register, texel-snapped in the absolute light frame,
`glm::orthoRH_ZO` with the Vulkan Y flip. Chunk casters are culled by the fitted sphere and then
by the light frustum (`s_shadowFrustumCull`).

**Filters:** contact-hardening **PCSS** (8-tap blocker search, 16-tap Poisson filter whose radius
scales with occluder distance, per-pixel dither rotation) on ground, glass and characters;
**Fast 4-tap** on grass, foliage and the far LOD. Bias is authored in world units and divided by the
volume's depth span so it means the same distance at every fit. Both filters fade to lit over the
outer 12 % of the map (`phxShadowBorderFade`).

**Composition:** `shadow = min(mid, near)` inside 40 u (R3). Then **direct sun =
BRDF × shadow × phxSunGate(skyGate, midCoord)** — and `phxSunGate` is 1 wherever the mid coord is
inside the volume (R1). The far cascade is not composed with the others: it serves receivers the
near/mid maps never see.

⚠️ Shadow multiplies **only** the sun term. Ambient, point/spot lights, emission and moonlight are
unshadowed (moon: gap §8).

**Known:** grass blades cast only into the near cascade, whose camera-following coverage reads from
an elevated camera as a dark disc gliding with the view — `GrassRenderPipeline::s_castShadows`
defaults **false**.

---

## 4. Dynamic point and spot lights

`Light.h` / `LightManager`: **32 point, 16 spot**, an SSBO, a forward loop per receiver.
Since M2 every consumer (ground, glass, grass, foliage, characters) runs `phxLightVisibility`
first: one DDA from the surface toward the emitter, with the emitter's own run-length excluded so a
lamp inside its own fixture still lights out. A lantern sealed in a stone room no longer lights the
character outside it or shines through glass. Attenuation is `(1 - d/r)²` on vegetation and a
`1/(1+ld+qd²)` with hard cutoff on the ground — still not photometric. Emissive chunk voxels are
registered as real lights (U3.2, "emissive voxel lights registered" at scene load).

Open: no shadow maps for point lights (occlusion is the binary trace), intensities authored before
the Lambert `1/π` was restored (2026-08-10) read π× dim, lights are not world-persisted.

---

## 5. Composition and exposure

Order in `voxel.frag`: ambient (probe field or analytic) → direct sun (`pbrBRDF`, shadowed, sun-gated
per R1) → moon (× skyVis²) → forward point/spot lights (visibility-traced) → masked emission →
aerial perspective. `post_process.frag` composites scene + OIT and applies **the frame's single
tone map** (`phxTonemap`, AgX, exposure 8.0 — `POST /api/debug/tonemap`). No scene shader tone-maps
any more; the editor viewport samples the same grade image as a packaged game.

A physical atmosphere returns radiance (a lit diffuse surface ~0.1), so exposure is required, not
polish. AgX rather than ACES because ACES bleaches the warm sun the atmosphere produces.
`phxTonemap` returns linear; the `B8G8R8A8_SRGB` swapchain applies the sRGB encode — never add a
manual `pow(1/2.2)`.

**Debug views** (`ubo.debugShadowMode`, `POST /api/debug/shadow {"mode": N}`, Ctrl+F4 cycles):
1 shadow-only · 2 grass wind ramp · 3 traced sky visibility · 4 (retired, black) · 5 forward
point/spot · 6 direct sun + moon · 7 ambient · 8 occupancy cells · 9 occupancy classes · 10 wind
field map (grass hidden).

---

## 6. Post-processing

**The grade pass (shipped 2026-08-15, `94927b07`).** `shaders/post_process.frag` composites scene
colour + OIT transparency **and applies the frame's single tone map**, rendering into an offscreen
*grade image* rather than straight to the swapchain. The swapchain pass is then a plain blit
(`shaders/blit.frag`), and the **editor viewport samples the same grade image**
(`PostProcessor::getGradeImageView()`), so the editor and a packaged game show identical composited
pixels. That permanently retires the class of bug where a post-process defect shipped invisible to
the editor — which is why bloom, SSAO and the tone map sat disabled for so long.

The swapchain is `B8G8R8A8_SRGB`, so hardware applies the linear→sRGB encode — **do not add a manual
`pow(1/2.2)`** anywhere in this pass. Double gamma was a real shipped bug.

⚠️ **The editor does NOT call `PostProcessor::draw()`.** `RenderCoordinator` inlines the sequence so
it can slot ImGui into the swapchain pass, so the composite must be driven via
`compositeToGrade()` + `drawBlit()`. This trap bit twice: first leaving the grade image unwritten
(blank viewport), then leaving `renderBloom` uncalled (bloom a silent no-op at any intensity).
`compositeToGrade()` therefore **owns** the bloom pass, so both call sites are correct by
construction. Do not move it back out.

### ⛔ Bloom is BROKEN — do not enable

Confirmed by the user 2026-08-15: at any visible intensity bloom produces **spots / blotches across
the frame** rather than a smooth glow. It ships **off** (`bloom = 0.0`) and must stay off. The knob
remains live purely so it can be debugged, and `POST /api/debug/tonemap` returns a `warning` field
whenever intensity is set above zero.

What *is* built and believed correct:
- a soft-knee **bright-pass** on the first blur iteration only (re-thresholding every pass erodes the
  highlight to nothing);
- **R16F** blur targets — they were `R8G8B8A8_UNORM`, which clamped every highlight to 1.0 at the
  seeding blit, so bloom could not tell the sun from a white wall;
- the threshold is authored in **post-exposure** units (1.0 = "this would clip") and divided by
  exposure before reaching the shader, because the scene target holds *physical radiance* where a lit
  noon surface is ~0.02–0.2;
- the blur chain runs at **half resolution** (`kBloomDownscale`), which took the cost from ~11% to
  ~6% of frame time.

Suspected cause of the spots, **not yet confirmed**: isolated very bright pixels survive the
bright-pass and each becomes a blob — classic **fireflies**. Candidate sources are the sky pass's
stars/airglow (per-pixel hash noise) and the known grass/character sub-pixel speckle
(`RenderOptimization.md:489,513`). The half-res blur doubles the width of every blob, which is why
they read as *spots* rather than fine sparkle. First things to try: clamp each bright-pass tap so one
pixel cannot dominate the kernel, and/or exclude the star/airglow term from what seeds bloom.

**Diagnose it by measuring, not by looking.** An earlier claim that the spots were "only in the sky"
came from eyeballing two screenshots and is unverified. Measure *where* the bloom-on vs bloom-off
difference lands, per region — and always against a control, because this scene animates (see §7).

### SSAO — still disabled
Depth-derivative normals degenerate at grazing angles and draw a dark band across screen centre.
`PostProcessor.h ssaoEnabled = false`, and nothing consumes its output. A forward renderer has no
normal buffer; fixing it properly means adding a normal attachment to the scene pass.

---

## 7. Measuring lighting changes

Use the rig; do not judge by eye.

- `tools/lighting_stats.py` — region-mean luminance, percentiles, and the **clipped-pixel fraction**.
  Measure the **viewport rect** (`docs/evidence/viewport_regions.json`), not the whole window: editor
  chrome pins the median otherwise.
- `tools/lighting_lab.py` — builds the LightingLab world (five one-variable rooms with written
  predictions and controls at both ends), drives fixed poses and a day/night sweep, and verifies by
  reading the world back.
- `POST /api/debug/shadow {"mode": 1}` — shadow-only view (white = lit, black = shadowed). Thin
  casters are unreadable against textured ground without it.

⚠️ Identical statistics across *different* scene states mean a **stale frame**, not a result. Settle
≥ 2.5 s and take two screenshots, keeping the second.

### What the probe-field ambient costs — measured in the REAL SCENE (2026-09-20)

The 2026-09-19 figures below were taken in the Lighting Lab, which is nearly empty. They are not
representative and should not be quoted as the cost. Measured in Ravenmere town instead, with
`tools/ambient_cost.py` reading the engine's own per-pass GPU timings
(`GET /api/debug/gpu_scopes`, which already reports a `GI Probes` scope), field ON vs OFF at three
fixed poses, noon, medians of 14-20 frames. Laptop RTX 1000 Ada.

| pose | frame ON | frame OFF | ambient costs | of which probe pass | of which Grass | of which Static Geometry |
|---|---|---|---|---|---|---|
| player spawn, down the street | 132.1 | 88.5 | **43.6** | 5.2 | 20.7 | 19.5 |
| nose to a wall | 87.7 | 35.3 | **52.4** | 5.3 | 20.1 | 7.7 |
| elevated, west end | 65.2 | 26.2 | **39.0** | 5.1 | 22.3 | 11.5 |

**After the grass fast path** (`phxAmbientUp`, same rig and poses, 2026-09-20). Grass ambient is
essentially gone; the whole frame drops 18 to 21 ms:

| pose | frame ON | ambient costs | of which probe pass | of which Grass | of which Static Geometry |
|---|---|---|---|---|---|
| player spawn | 113.7 (was 132.1) | **29.3** (was 43.6) | 5.6 | **0.9** (was 20.7) | 19.8 |
| nose to a wall | 68.7 (was 87.7) | **32.6** (was 52.4) | 5.6 | **-0.4** (was 20.1) | 8.1 |
| elevated, west end | 43.9 (was 65.2) | **18.2** (was 39.0) | 5.4 | **1.0** (was 22.3) | 11.6 |

Verified not to change the picture: outdoor grass luminance at two town poses moved -0.09 % to
+2.03 % (`docs/evidence/grass_look_{before,after}.json`, tone map off), the indoor-grass floor band
of all three enclosed lab rooms moved 0.1 %, and `ambient_model_check.py` stayed green on all five
invariants. **`Static Geometry` at +8 to +20 ms is now the largest remaining item.**

**The probe compute pass is 10-13% of it. The other 87-90% is per-fragment and per-vertex work in
the receivers**, i.e. the trilinear probe fetch (up to 8 neighbours x 4 SSBO reads) plus the
`phxSegmentBlocked` visibility test (up to 8 short traces), paid at every shading point.

**Grass alone is +20 to +22 ms in every pose, including the pose with almost no grass on screen.**
`grass.vert` calls `phxAmbient` per BLADE VERTEX (24 vertices per blade), so the cost tracks blade
count rather than screen coverage. That is the single largest item and it is a design error in how
G-141 wired grass up, not a tuning problem.

Implication for optimisation: skipping buried probes before tracing (the probe pass computes
`valid` *after* its 18 rays) can save at most about half of 5.2 ms out of ~45 ms. Not worth doing
on its own.

### What the probe-field ambient costs — Lighting Lab only, NOT representative (2026-09-19)

Lighting Lab, `in_door` pose (inside the door room looking at its far wall, grass and rooms in
frame), frame time via `/api/debug/engine_timing`, ON/OFF interleaved 4 rounds × 8 samples,
medians. The 4090 target has not been measured yet.

| Configuration | frame ms | Δ vs field off |
|---|---|---|
| field OFF (`/api/debug/gi` false: no probe pass, receivers on the open-sky fallback) | 6.5 | — |
| field ON, receivers with **no** visibility test (leaks through walls) | 13.9 | +7.4 |
| field ON, visibility test as 8 **micro** marches per fragment | 69.6 | +63 |
| field ON, visibility test as `phxSegmentBlocked` (cube-stepped, micro inside mixed cubes) — **shipped** | 17.0 | +10.5 |

The remaining +10.5 ms is the probe pass (18 micro-marched primary rays + a sun ray per hit, 1/8
of 55,296 probes per frame) plus 8 probe reads and ≤ 8 short segment tests per fragment. Next
levers, in order: primary rays through the two-level traversal (`phxDdaTrace` is still a micro
march), then 36 rays at 1/16 of the grid per frame.

### What the sky pass costs — measured, RELEASE
Toggle it with `POST /api/debug/sky {"enabled": false}`; that toggle exists **to make this
measurable**, since the pass otherwise always draws and there is nothing to subtract.

LightingLab, Release, 1600×900, median of 20 samples per state, sky ON minus sky OFF:

| Pose | Δ frame time |
|---|---|
| Looking up (most of the frame is raymarched sky) | **+0.10 ms** |
| Horizon (realistic gameplay mix) | **+0.15 ms** |
| Looking down (geometry covers nearly all sky pixels) | **+0.03 ms** |

**≈0.1 ms — negligible.** A full-screen 12-step view march with a 5-step inner sun march was the
obvious thing to suspect, and it is not worth optimising: a sky-view LUT would buy back a tenth of a
millisecond. If the LUT is ever built it should be for **accuracy** (multiple scattering, the blue
hour), not for speed.

⚠️ `/api/debug/engine_timing` reports identical `cpuFrameTime` and `gpuFrameTime`, so these are
frame times, **not** an isolated GPU measurement. Treat the split as unmeasured.

**Reference measurements** (LightingLab, exposure 8, AgX, viewport region): noon exterior mean 0.145
with 0.00 % clipped; golden hour 0.160; hearth interior 0.245 with 0.00 % clipped (was **29.72 %**
before the tone map); full moon 0.0094 > first quarter 0.0053 > new moon 0.0043.

---

## 8. Known gaps

| Gap | Detail |
|---|---|
| **Blue hour** | Single scattering cannot produce it — the twilight zenith measures B/R = 0.94. Needs a multiple-scattering LUT. Pinned as `DISABLED_TwilightZenithIsBlue_NeedsMultipleScattering`. |
| **Moon shadows** | Moonlight is unshadowed; the cascades are fitted to the sun. Fitting to the dominant body earns real moon shadows. |
| ~~No stars / airglow~~ | SHIPPED — stars + airglow render. Note they are a *suspect* in the bloom spots (§6). |
| **No real AO** | The per-corner skylight nibbles that gave implicit AO are gone (U7); nothing replaced them. SSAO is disabled (§6). |
| **Bloom produces spots** | ⛔ BROKEN, ships off. Spots/blotches instead of a glow; suspected fireflies from bright single pixels (sky star/airglow noise, grass speckle), widened by the half-res blur. See §6. |
| **No AA** | The grade pass now exists, so FXAA/TAA is unblocked but not built. |
| **Point lights** | See §4 — occluded by a binary trace but no shadow maps, intensities π× dim since the Lambert fix, not persisted. |
| **CPU debris reads the per-cell bake** | The last consumer of `sampleBakedLight` for lighting (one value per body, no shadow map) and the last per-cube approximation in the model (R8). It stays only because the CPU sampler cannot read the GPU probe field; retire it when debris lighting moves GPU-side. |
| **Geometry beyond the probe grid gets a WRONG answer, not a coarse one** | The field covers ±48 u (x/z) and ±24 u (y) around the viewer; outside it `phxAmbient` returns the open-sky hemisphere, which is wrong in BOTH directions. Measured 2026-09-20 with a stationary camera by toggling the field, which takes the identical fallback path (`tools/coverage_gap.py`, `docs/evidence/coverage_gap.json`): a sealed room 2.11× too bright, a room sealed by a 1-micro roof 2.09×, a wall lit only through a 1-wide door 5.75×, an exterior wall 0.23× (4.3× too DARK, because the fallback carries no sun bounce off the lit ground); an open-roof room's sunlit floor moves 0.94×, i.e. only fully-open surfaces are unaffected. How much of a real frame this touches (`tools/town_coverage.py`): Ravenmere town, player spawn looking down the street 97% of geometry inside the grid, an elevated vantage from the town's west end 80%. Direct sweep (`tools/ceiling_sweep.py`, `docs/evidence/ceiling_sweep.json`): a fixed enclosed ceiling with the camera flying up beneath it reads the fallback EXACTLY while uncovered (field on vs off identical to 4 dp at 24, 26 and 30 u) and departs once covered (×1.02 at 22 u → ×1.87 at 6 u). Coverage ends at viewer + 22 u vertically, as the grid arithmetic predicts. The boundary is **not** a hard seam: `PHX_GI_EDGE_FADE_PROBES` cross-fades over the outer 8 u, so the error ramps rather than jumping. Fix = a far cascade (sparse probes, many directions, per `EngineAdvancesResearch.md` §4) so the near field's edge lands inside a coarse field instead of on an assumption, converting the boundary from a brightness error into a resolution difference. |
| **Probe bounce albedo is a constant** | `gi_probe.comp` bounces with `kBounceAlbedo = 0.30` for every surface (the occupancy stores solidity, not material), so a white wall and a dark floor bounce alike. A material id in the occupancy pool is the fix. |
| **Probe angular resolution** | 18 fixed directions per probe: a 1-wide opening is seen only by the probes right at it; the rest of the room fills in by the probe-to-probe bounce, which is what `ambient_model_check.py` A4 measures. |
| **Small-opening flicker** | A surface lit only through a 1-wide doorway (≈1 % of a probe's sphere) still drifts over seconds even with the camera still: measured ±25–50 % around a value 2× the ambient floor (blend 0.05). Invisible under the AgX toe at game exposure so far, but the fix is more rays per probe (36 at 1/16 of the grid per frame costs the same as 18 at 1/8). |
| **Probe refresh latency** | 1/8 of the grid per frame and a 0.05 temporal blend: a world edit takes ~20 refreshes (~160 frames, ~2.5 s at 60 fps) to reach 2/3 of its final effect; moving fast re-snaps the grid and the fresh band starts from an unblended single estimate. A surface lit only through a 1-wide doorway (≈1% of a probe's sphere) still drifts over seconds — measured ±27% at blend 0.08; 0.05 is shipped and not yet re-measured. More rays per probe is the real fix. |
| **Grass under a roof at noon is sunlit** | Lighting Lab sealed room: blades on the floor read 0.041 linear (exposure 16) against 0.0018 on the wall, in both the old and the new ambient model — the sun term, not ambient. Ravenmere G-142. |
| **Glass pane reads black from inside a sealed room, before and after the sky-trace change** | Lighting Lab window room, pane at eye height in the -Z wall, camera 5 u inside looking at it: mean luminance 0.008 with the constant-1.0 sky AND with the traced sky. Either the glass's own lit term is negligible next to its alpha or the transparent pass does not composite there; the exterior should show through. Untested from outside. Open — measure the OIT composite of a lit pane before changing the glass model again. |
| **Far LOD sees full sky** | `far_terrain.frag` / `far_tree_mesh.frag` use the open-sky hemisphere — no interiors at that range, and it is exactly the field's own out-of-grid fallback. |
| **Metals** | No environment/IBL term, so they read dark except in direct light. |
| **T-junction cracks / character speckle** | Open render defects at greedy-merge borders; see `RenderOptimization.md`. |

## 9. Change log (append a line per lighting/shadow change; the fingerprint line is written by `tools/lighting_doc_check.py --update`)

- 2026-09-22 — **TWO DEFECTS IN ONE FUNCTION: `phxLightVisibility` is 90% of the frame AND
  leaks through walls (G-18 + G-157).** Drilling into the light loops: the MARCH alone is 237.5
  of 263.6 ms, and a probe that counts marches shows **31.3 of the 32-light cap running at 91.6%
  of screen pixels** — roughly 59 million marches a frame. Each march is cheap (~4–9 ns); there
  are simply far too many, because the radius and facing gates cull almost nothing once 32
  lights are uploaded near the camera. Separately, the run-length exclusion that stops an
  emissive voxel shadowing itself swallows any wall the emitter touches, so a sconce mounted
  flush on a wall lights the far side — reproduced at EVERY thickness from 1 micro to a 3-cube
  stone keep wall (`LightWallMatrixTraced.DISABLED_M4_...`), with the one-cell-off control
  correctly blocked at all of them. These are the same code, so the approach is what needs
  revisiting: patching the exclusion would entrench a 237 ms algorithm. Rules R8/R9 are
  untouched; no lighting behaviour changed in this commit.
- 2026-09-22 — **MEASURED: the forward point/spot light loops are 79-91% of the frame
  (Ravenmere G-18/G-52).** `voxel.frag` gained shader-cost bisect probes on `debugShadowMode`
  11-16 - each returns early with a flat or partial colour, adding no lighting behaviour and
  costing nothing at mode 0. Shipped Release, Ravenmere town, RTX 1000 Ada. Static Geometry
  at the worst pose is 260.7 ms and breaks down as: rasterise 0.04, textureGrad x2 0.21,
  shadow PCSS 0.55, ambient probe 23.49, sun+moon PBR 0.07, **point/spot lights 236.27
  (90.6%)**, fog/tonemap 0.04. On the street: 84.6 ms total, point/spot 66.63 (78.8%),
  ambient 17.52 (20.7%). The cause is `phxLightVisibility` - an occupancy ray march run PER
  LIGHT PER FRAGMENT, up to 32 point + 16 spot - and this town registers 143 emissive voxel
  lights. Two consequences for this document's assumptions: the shadow pass is a FLAT 1.6 ms
  at every pose and PCSS is 0.2-1.0% of the fragment cost, so "shadows are the cost" is dead;
  and the ambient probe field, while second, is under a quarter of the bill. No fix yet - the
  point of the exercise was to stop guessing. Rig: `tools/perf_town_profile.py`, evidence
  `docs/evidence/ravenmere/rv_perf*.json`.
- 2026-09-20 — **Grass gets an up-facing ambient fast path (`phxAmbientUp`, Ravenmere G-146).**
  `grass.vert` ran the general `phxAmbient` per blade vertex, 24 per blade, costing +20 to +22 ms
  per frame in the town at every pose, including one with almost no grass on screen. The fast path
  drops the zero-weighted X and Z lobes (exact) and the per-neighbour visibility trace (the one
  approximation): grass ambient falls to about 1 ms and the frame drops 18 to 21 ms. Gated on
  outdoor pixels (≤2 %), the indoor-grass band (0.1 %) and the five ambient invariants (green).
  Measured with `tools/ambient_cost.py` against the engine's own per-pass GPU timings.
- 2026-09-19 (later) — **World-stable probe addressing (Ravenmere G-143).** Slots are addressed by wrapped world lattice coordinate with a per-slot tag, so camera motion no longer shifts the field under surfaces; `phxWrapSlot` uses floor division after GLSL `%` on negative operands produced wrong slots (walls darker and jittery, a micro-roofed room leaking, all cured by the floor form alone). Lab door-room wall after camera trips: +33 % / −56 % → within noise; ambient check still green.
- 2026-09-19 — **The probe field is THE ambient source; the per-fragment sky trace is deleted (Ravenmere G-141).** Lab: `tools/ambient_model_check.py` RED on the trace (A1 0.37) → GREEN on the field (A1 1.00, A2 0.067, A3 0.99, A4 2.06). Cost table in §7. Also: `phxSegmentBlocked` two-level traversal (+ CPU mirror `packedPoolSegmentBlocked`, `OccupancyTraversalTest`) for every short visibility test; **`gi_probe.comp` gained the `build_shaders.bat` rule it never had (stale .spv since 2026-09-03).** `gi_field.glsl` (new, shared): ambient-cube probes (6 cosine lobes, `GiProbeField::kLobes`), `phxAmbient` / `phxSkyAccessOf`; `gi_probe.comp` deposits its 18 traced directions into the lobes; voxel/transparent_voxel/character/foliage/grass(.vert) call `phxAmbient`; the probe pass bounces off the field (multi-bounce by iteration) with a sun-visibility DDA, rotates its ray set per refresh and blends temporally (finds 1-wide doorways), and receivers keep only probes visible from the surface (no leak through walls); `phxSkyGate` deleted from `lighting.glsl`, `phxSkyVisibility` deleted from `occupancy.glsl`; `gi_probe.comp` gets the `build_shaders.bat` rule it never had (its `.spv` had been stale since 2026-09-03); `grass_shadow.vert` no longer traces (dead work); bindings 11–13 visible to vertex stages; `m_giEnabled` default ON (`/api/debug/gi` = kill switch). Rules R8 (micro-resolution occlusion) and R9 (no receiver traces) added and enforced by the doc check. New rig `tools/ambient_model_check.py` (lab rooms + exterior wall pair 13 u apart + a room sealed by a 1-micro roof, measured with the tone map off at noon). Numbers in the ledger row and in `docs/evidence/ambient_{red_trace,green_probes}.json`.
- 2026-09-22 — **damage cracks (P4, `shaders/crack.glsl`, new include).** `voxel.frag` gains a progressive fracture field on damaged static chunk faces: a Voronoi edge-distance network seeded from ABSOLUTE WORLD POSITION (never `texCoord`/`sizeU`/`sizeV`, which are tied to the greedy-merge rectangle and therefore terminate at chunk borders — `docs/VoxelDamageVisualization.md` §3.3). It is a pre-lighting surface modifier — albedo ×0.18 and roughness → 1.0 inside crack pixels, plus a small whole-face wear term — and consumes no lighting input, so the shared model is unchanged. Replaces the previous flat `mix(1.0, 0.55, dmg)` whole-face darkening, which read as grime rather than fracture. Gated on `dmg > 0` as a COST bound (pristine is ~100% of voxels), never as a quality tier. Measured at 16 units on the damage-ladder rig: stage steps 5.69 / 3.78 / 3.83 luminance against a within-stage noise floor of 2.91.
- 2026-09-17 — glass (`transparent_voxel.frag`) and characters (`character.frag`) trace sky visibility per fragment like the ground; the constant-1.0 glass sky and the one-value-per-body character bake are retired as lighting inputs. Lighting Lab A/B (editor on StructGenTest, `docs/evidence/lab_skysrc_{before,after}_*.png`): a character standing in the door room's doorway — torso seen from INSIDE the dark room mean luminance 0.208 → 0.001, from outside 0.192 → 0.084 (its outward face keeps its p90 0.28). The glass pane seen from inside the sealed window room read 0.008 both before and after — the change did not measurably alter that pose (see §8).
- 2026-09-17 — foliage translucency (transmitted sun) also goes through `phxSunGate`; caught by the new R1 check on its first run.
- 2026-09-17 — `phxSunGate`: direct sun is the shadow map's answer inside its coverage; the sky gate only outside it. Applied in voxel/grass/foliage/character/transparent_voxel (Ravenmere G-135). Doc rewritten to the current state; §0 matrix and rules added; check script added.
- 2026-09-02 — M5 one-bounce probe field working, default OFF (`/api/debug/gi`).
- 2026-09-01 — U7 stage 2: block light deleted from the engine; vegetation sky transport retired (grass/foliage trace their own sky).
- 2026-08-30 — M2 point/spot visibility trace on every consumer; M3-REDESIGN per-cell traced bake for non-chunk receivers.
- 2026-08-15 — single tone map moved to `post_process.frag` (grade pass).
- 2026-08-06 — near shadow cascade (40 u) shipped; receivers min-compose.

<!-- lighting-model-fingerprint: b762b433d082d4da -->

## Related

- [`NearShadowCascade.md`](NearShadowCascade.md) — the canonical cascade record.
- [`WorldRenderV2Plan.md`](WorldRenderV2Plan.md) — §3.3 and §7c designed much of the atmosphere work.
- [`EngineAdvancesResearch.md`](EngineAdvancesResearch.md) §4 — radiance cascades, the GI option.
- [`VoxelRenderPipelines.md`](VoxelRenderPipelines.md) — the three voxel vertex shaders and
  `InstanceData`, which carries the baked light words.

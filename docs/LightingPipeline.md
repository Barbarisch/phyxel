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
| B | **Is the straight line to the sun blocked?** (direct sun) | The **shadow map**: `phxShadowPCSS` / `phxShadowFast` in `lighting.glsl`, near ∪ mid cascades (`min`), far cascade for far LOD meshes | near 0.02 u, mid 0.11 u, far ~0.9 u texels; 40 / 420 / 1600 u | the sky trace (D). Inside shadow coverage `phxSunGate` returns 1 — see rule R1 |
| C | Where does the sun fall once nothing blocks it? | `pbrBRDF` (ground, Cook-Torrance) or Lambert/Blinn-Phong (vegetation, characters, glass) × `ubo.sunColor` | per fragment | — |
| D | **How much of the sky dome does this point see?** (ambient fill, interiors) | `phxSkyVisibility` in `occupancy.glsl`: 5 rays (up + four at 30°) DDA-marched through the **micro-resolution occupancy** (1/9 u cells), reach 16 u, weighted by cosine; ray 0 escaping short-circuits to 1.0 | per fragment (ground, glass, foliage, characters), per blade vertex (grass), per **cell** from the bake (CPU debris only) | direct sun (B) inside shadow coverage |
| E | What is the ambient fill? | `phxAmbientAtmos(N, sky, ubo.ambientColor)`; on the ground the **probe field** (`phxGiIrradiance`, M5, default OFF) replaces the sky scalar with a traced neighbourhood + one bounce when available, else the analytic term | probes 55,296 on a 48×24×48 grid around the viewer | a second ambient formula anywhere |
| F | Does a point/spot light reach here? | `phxLightVisibility` (`occupancy.glsl`): one DDA from the surface to the emitter, emitter run-length excluded | per fragment, 32 point + 16 spot, forward loop | — |
| G | Moonlight | same directional path as C, **unshadowed**, gated by `skyVis²` only (no moon shadow map) | — | — |
| H | Emission | material emissive tint (`isEmissive` path in `voxel.frag`); block light **no longer exists** (U7 stage 2) | — | — |
| I | Distance haze | `phxAerialPerspective` | per fragment | — |
| J | Tone map / exposure | **once**, `phxTonemap` in `post_process.frag` (AgX, exposure 8.0) | per frame | scene shaders (none call it any more) |


### 0.2 Receiver matrix — every shader that lights a surface (from the source, 2026-09-17)

| Shader (pipeline) | Sky access (D) | Ambient (E) | Direct sun (B × C) | Shadow filter / cascades | Moon | Point/spot (F) | Haze | Notes |
|---|---|---|---|---|---|---|---|---|
| `voxel.frag` — static chunks, kinematic voxels (doors, furniture), GPU debris | traced per fragment, geometric normal | probe field if on, else analytic | `pbrBRDF × shadow × phxSunGate` | PCSS, mid ∪ near | yes, × skyVis² | yes, with visibility trace | yes | `vSkyLight` varying is a dead constant 1.0; the kinematic `setLightSampler` feed is not read here |
| `transparent_voxel.frag` — glass | traced per fragment, face normal | analytic | Blinn-Phong × shadow × `phxSunGate` | PCSS, mid ∪ near | no | yes, with visibility trace | — | `vSkyLight` varying is a dead constant 1.0 |
| `grass.frag` (+`grass.vert`) — blades | traced per **blade vertex** (up normal), `vSky` | analytic (up) | `0.85 × shadow × phxSunGate` | **Fast 4-tap**, mid ∪ near | no | yes, with visibility trace | no | wind sheen also × skyGate |
| `foliage.frag` — leaf cards | traced per fragment (up) | analytic (up) | `0.7 × shadow × phxSunGate` + backlit translucency × (0.25+0.75·phxSunGate) | **Fast 4-tap**, mid ∪ near | no | yes, with visibility trace | no | — |
| `character.frag` — animated characters | traced per fragment, vertex normal | analytic (N) | Blinn-Phong × shadow × `phxSunGate` | PCSS, mid ∪ near | yes, × sky² | yes, with visibility trace | no | block-light term from the bake is 0 |
| CPU debris (`DebrisRenderPipeline` light sampler) | per-cell bake at the body | CPU: `ambient + sun × 0.5 × sky²` | **no shadow map** | — | no | no | — | flat per-body light; the only place the sky gate still scales sun, because there is no map lookup |
| `far_terrain.frag`, `far_tree_mesh.frag` — far LOD | **constant 1.0** | analytic | `ndl × shadow` | Fast 4-tap, **far cascade only** | no | no | yes | — |
| `water.frag`, `water_cell.frag`, `water_underwater.frag` | none | own constants | unshadowed | none | — | — | — | own model |
| `sky.frag` | — | — | — | — | — | — | — | emits the atmosphere |

Shared code: `lighting.glsl` (ambient, shadow filters, `phxSunGate`, haze, tone map) and
`occupancy.glsl` (occupancy query, DDA, light and sky visibility). **Never re-inline any of it into
a single shader** — five hand-synced copies is how grass went its whole life with no shadow lookup.

### 0.3 Rules — each one was a shipped defect

- **R1. Direct sun is the shadow map's answer wherever the map covers the fragment.** `phxSunGate`
  returns 1 inside the fitted volume (blending through the map's 12 % border fade) and the sky
  gate only outside it. Multiplying the sun by `skyVis²` on top of the map stamped a canopy's
  five-ray vertical footprint onto the ground as hard 1-m blocks beside the correct shadow —
  Ravenmere G-135, 2026-09-17. The sky trace owns **ambient**; that is how a sealed room stays dark.
- **R2. One ambient formula.** Every receiver calls `phxAmbientAtmos`; the probe field only changes
  where the sky scalar comes from. A receiver with its own ambient maths is a second lighting model.
- **R3. Near and mid cascades are min-composed, never selected.** `min(near, mid)` is the union of
  shadows, so a caster recorded in only one map still shades. The near map's border fade is the blend.
- **R4. Grass casts into the near cascade only, and `GrassRenderPipeline::s_castShadows` is
  `false` by default** (a camera-following dark disc from above).
- **R5. Shadow-caster pipelines bake a static viewport: create them against the map they render
  into; `VK_COMPARE_OP_LESS`, never the scene's reverse-Z compare.**
- **R6. Occupancy flags gate every trace.** `ubo.occupancyBox.w`: bit0 occupancy readable, bit1
  light tracing (`VulkanDevice::setLightTracingEnabled`, default ON), bit2 sky tracing
  (`setSkyTracingEnabled`, default ON), bit3 probe field (`POST /api/debug/gi`, default OFF).
  With a bit clear the corresponding function returns 1.0 / false and the fallback buffer must not
  be read.
- **R7. Measure, never eyeball.** Shadow-only view (debug mode 1) and per-term views (3 sky, 5
  forward lights, 6 direct, 7 ambient) exist so a term can be isolated; `tools/lighting_stats.py`
  for numbers. A frame that does not contain the defect proves nothing about it.

### 0.4 Change discipline (enforced)

1. Any change under `shaders/lighting.glsl`, `shaders/occupancy.glsl`, the receiver shaders in
   §0.2, `RenderCoordinator::fitShadowVolume` / `renderShadowPass`, `gi_probe.comp`, or the
   occupancy upload **updates §0 (matrix + rules) and appends a line to §9 in the same commit.**
2. Then run `python tools/lighting_doc_check.py --update`, which stamps the fingerprint of the two
   shared includes into §9. `build_and_test.ps1` runs `--check`: a shared-include change without a
   doc update fails the build, and so does any direct-sun term that multiplies a sky gate without
   going through `phxSunGate` (R1), and any lighting-model shader missing from the matrix.
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

## 2. Sky access — the traced visibility, the per-cell bake, and the probe field

The flood-filled skylight and the RGB block light of the original engine are **gone** (U7 stage 2
deleted block light; `static_voxel.vert` emits `vSkyLight = 1.0` as a placeholder). Three sources
of "how much sky does this point see" remain, and which one a receiver reads is in §0.2:

- **Traced per fragment / per blade vertex** — `phxSkyVisibility` (`occupancy.glsl`). Five rays
  from the surface (`+ normal × 2/9 u`): the normal, then four at 30° off it; each is a DDA through
  the sub-voxel occupancy (1/9 u cells, `kReach = 16 u`, `kCells = 288`). Weighted by the cosine
  to the normal. If the normal ray escapes the answer is 1.0 without tracing the rest (outdoors is
  the common case). Ground, foliage and grass use this; a sealed room reads 0, a doorway falls off
  with real geometry.
- **The per-cell bake** (`ChunkManager::sampleBakedLight` → `m_skyLight`, one value per cube cell,
  traced at bake time — M3-REDESIGN). Only CPU debris still reads it (one sample per body); the
  characters' `fragBakedLight.x` feed is uploaded but no longer read by `character.frag`.
- **The probe field** (M5, `gi_probe.comp`, SSBO binding 13, 48×24×48 probes around the viewer,
  spacing `ubo.giProbeGrid.w`). A probe stores irradiance: sky where a ray escapes, the light
  leaving the hit surface otherwise (albedo stands in as 0.30, 18 directions). `voxel.frag` uses it
  in place of the analytic sky scalar when bit3 is set and the sample is valid; buried and
  back-facing probes are weighted out, and any failure falls back to the analytic term. **Default
  OFF**; toggle with `POST /api/debug/gi`.

The occupancy the traces read is the same sub-voxel occupancy the mesher builds (subcube and
microcube leaf-accurate, `m_subOcc` / `m_microOcc`), uploaded as two SSBOs (bindings 11/12) and
kept current with edits; `POST /api/debug/light_occupancy` reports per-micro counts.

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
| **CPU debris reads the per-cell bake** | The last consumer of `sampleBakedLight` for lighting (one value per body, no shadow map). Characters and glass moved to the per-fragment trace 2026-09-17. |
| **Glass pane reads black from inside a sealed room, before and after the sky-trace change** | Lighting Lab window room, pane at eye height in the -Z wall, camera 5 u inside looking at it: mean luminance 0.008 with the constant-1.0 sky AND with the traced sky. Either the glass's own lit term is negligible next to its alpha or the transparent pass does not composite there; the exterior should show through. Untested from outside. Open — measure the OIT composite of a lit pane before changing the glass model again. |
| **Far LOD sees full sky** | `far_terrain.frag` / `far_tree_mesh.frag` pass `sky = 1.0` — no interiors at that range, so acceptable, but state it. |
| **Metals** | No environment/IBL term, so they read dark except in direct light. |
| **T-junction cracks / character speckle** | Open render defects at greedy-merge borders; see `RenderOptimization.md`. |

## 9. Change log (append a line per lighting/shadow change; the fingerprint line is written by `tools/lighting_doc_check.py --update`)

- 2026-09-17 — glass (`transparent_voxel.frag`) and characters (`character.frag`) trace sky visibility per fragment like the ground; the constant-1.0 glass sky and the one-value-per-body character bake are retired as lighting inputs. Lighting Lab A/B (editor on StructGenTest, `docs/evidence/lab_skysrc_{before,after}_*.png`): a character standing in the door room's doorway — torso seen from INSIDE the dark room mean luminance 0.208 → 0.001, from outside 0.192 → 0.084 (its outward face keeps its p90 0.28). The glass pane seen from inside the sealed window room read 0.008 both before and after — the change did not measurably alter that pose (see §8).
- 2026-09-17 — foliage translucency (transmitted sun) also goes through `phxSunGate`; caught by the new R1 check on its first run.
- 2026-09-17 — `phxSunGate`: direct sun is the shadow map's answer inside its coverage; the sky gate only outside it. Applied in voxel/grass/foliage/character/transparent_voxel (Ravenmere G-135). Doc rewritten to the current state; §0 matrix and rules added; check script added.
- 2026-09-02 — M5 one-bounce probe field working, default OFF (`/api/debug/gi`).
- 2026-09-01 — U7 stage 2: block light deleted from the engine; vegetation sky transport retired (grass/foliage trace their own sky).
- 2026-08-30 — M2 point/spot visibility trace on every consumer; M3-REDESIGN per-cell traced bake for non-chunk receivers.
- 2026-08-15 — single tone map moved to `post_process.frag` (grade pass).
- 2026-08-06 — near shadow cascade (40 u) shipped; receivers min-compose.

<!-- lighting-model-fingerprint: 10fea6f3d5c08d15 -->

## Related

- [`NearShadowCascade.md`](NearShadowCascade.md) — the canonical cascade record.
- [`WorldRenderV2Plan.md`](WorldRenderV2Plan.md) — §3.3 and §7c designed much of the atmosphere work.
- [`EngineAdvancesResearch.md`](EngineAdvancesResearch.md) §4 — radiance cascades, the GI option.
- [`VoxelRenderPipelines.md`](VoxelRenderPipelines.md) — the three voxel vertex shaders and
  `InstanceData`, which carries the baked light words.

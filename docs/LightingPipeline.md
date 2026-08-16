# Lighting Pipeline

**Last updated: 2026-08-11.** THE reference for how this engine lights a frame.

> **This document was rewritten on 2026-08-11 because the previous version had become actively
> misleading.** Dated 2026-05-11, it described a single 4096² shadow map fitted from the scene AABB,
> a 16-tap Poisson PCF, "No CSM — Phase 5 work", and a post-process pass that "composites SSAO +
> bloom, tone-maps, gamma-corrects". By August every one of those was false: three cascades had
> shipped, PCF had been replaced by contact-hardening PCSS, and bloom/SSAO/tone-map had all been
> deliberately DISABLED. It also never mentioned the baked per-voxel light field, despite
> `docs/README.md` advertising it as the doc that covered exactly that. The old text is not
> preserved here — git history has it.

## The one-paragraph version

A frame is lit by four things: a **physical atmosphere model** that supplies the sun's colour, the
sky's colour, the ambient fill and the distance haze; a **baked per-voxel light field** that carries
skylight and coloured block light through the world's geometry; **three sun shadow cascades**; and a
small number of **dynamic point/spot lights**. All of it is composed in `shaders/lighting.glsl`,
tone-mapped with AgX, and written to a linear HDR target.

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

## 2. The baked per-voxel light field

Lives entirely in the chunk mesher: `ChunkRenderManager::rebuildCubeFaces`. There is no
`LightingSystem` class.

- **Skylight**: 4 bits/cell. Columns open to the sky seed 15 losslessly downward, then a 6-connected
  BFS spreads at −1 per step. A sealed room stays 0.
- **Block light**: 4 bits × RGB, independent channels, same BFS, seeded from emissive materials
  (hue from `physics.colorTint`, peak 15, or `emissiveStrength × 4` for masked-emissive) and from
  emissive/flaming sub- and microcubes at their parent cube cell.
- **Cross-chunk bleed** via a boundary seed plus a border-change ripple that re-meshes the six
  neighbours; converges because light is monotone and capped.
- **Smooth lighting + implicit AO**: per quad corner, the light of the four cells touching that
  corner in the air cell's plane is averaged. Solid cells read 0, so concave corners darken. Only
  faces whose corners are uniform may greedy-merge (`s_smoothLighting`, `s_mergeTolerance`).

### What blocks light is NOT `m_solidVis`
`m_solidVis` answers "is there a visible cube here" and drives face culling and material lookup.
Light opacity is a separate array, `m_lightOpaque`, and it differs in both directions:

- **Sub-voxel geometry occludes.** Sub-voxel fill is accumulated per cell in micro-equivalents; a
  cell blocks light at `kLightOpaqueFill = 243` (= 729/3, one subcube-thick slab). Before this,
  a subcube-built roof was transparent to skylight and generated interiors leaked daylight.
- **Transparent materials do not occlude.** Glass is a window; it used to bake a glazed room black.
- **Leaf/billboarded sub-voxels are deliberately excluded** — counting them would flip every forest
  floor to near-black, which is a separate look decision.

⚠️ **An unresolvable sample must not mean "outdoors".** `skyLightAt` returns 15 for a cell it cannot
resolve, which is right for a face's own air cell (a chunk-edge exterior face must not go black
waiting for a stream-in) and **wrong** for the per-corner AO average. The corner average uses
`bakedLightResolvable()` and skips what it cannot resolve. Pinned by `LightBakeOcclusionTest`.

The bake is inseparable from a full chunk remesh (~40–50 ms/chunk in Debug); there is no light-only
update path.

---

## 3. Shadows — three cascades

Created in `RenderCoordinator`; design record in [`NearShadowCascade.md`](NearShadowCascade.md).

| Cascade | Resolution | Distance | Texel | Update |
|---|---|---|---|---|
| Near | 4096² | 40 u | 0.0195 u | every frame |
| Mid | 8192² | 420 u | 0.1125 u | every frame |
| Far | 4096² | 1600 u | ~0.9 u | on a cadence |

Fit is a view-frustum **bounding sphere** (rotation-invariant, so no shimmer when turning), texel-
snapped in the absolute world light frame, `glm::orthoRH_ZO` plus a Vulkan Y-flip.

Receivers **min-compose**: `min(mid, near)` is the union of shadows, so a caster recorded in only one
map still shades correctly, and the near map's 12 % border fade *is* the cascade blend.

Sampling is contact-hardening **PCSS** (8-tap blocker search, then a 16-tap filter whose radius
scales with occluder→receiver separation) for solid geometry, and a cheap 4-tap for vegetation.
Bias is authored in **world units** and divided by the light volume's depth span, so it means the
same physical distance at every shadow distance.

⚠️ Any shadow-pass pipeline must use `VK_COMPARE_OP_LESS`, never the scene's reverse-Z compare.
⚠️ Shadow-caster pipelines bake a static viewport — create them against the map they render into.
⚠️ Shadow multiplies **only** the sun term. Ambient, block light, point/spot lights and emission are
all unshadowed.

**Known issue:** grass blades cast only into the ~40 u near cascade, whose camera-following coverage
reads from an elevated camera as a dark disc gliding with the view. `GrassRenderPipeline::s_castShadows`
defaults **false** as mitigation.

---

## 4. Dynamic point and spot lights

`Light.h` / `LightManager`. **32 point, 16 spot**, uploaded as an SSBO and consumed in a forward loop
with a full Cook-Torrance evaluation each.

Open defects, all real:
- **They cast no shadows and do no occlusion test** — a chandelier lights through walls.
- **Positions are absolute world while fragment positions are camera-relative**, so they are only
  correct near the origin.
- Attenuation is a hand-rolled `1/(1+ld+qd²)` with a hard cutoff — no inverse-square, no photometric
  units.
- Structure-generation fixtures are **double-counted**: the same lamp is an emissive voxel seeding
  baked block light *and* a registered point light.
- `LightManager` lights are not world-persisted, so generated lighting does not survive save/load.

The planned resolution is to move static fixtures into the bake (where the flood fill already
respects walls) and reserve forward lights for dynamic sources.

⚠️ Point/spot intensities were authored against a diffuse term with the Lambert `1/pi` **omitted**.
That omission was removed on 2026-08-10 when the sun became physical, so every authored intensity is
now π× dimmer and needs retuning.

---

## 5. Composition, exposure and tone mapping

`shaders/lighting.glsl` is THE single source of the scene lighting model — ambient, shadow lookup,
aerial perspective and the tone curve. It is included by `voxel.frag`, `grass.frag`, `foliage.frag`,
`far_terrain.frag`, `far_tree_mesh.frag` and `sky.frag`. **Never re-inline a lighting constant or a
shadow loop into a single shader**; five hand-synced copies is how `grass.frag` went its entire life
with no shadow lookup at all.

Order in `voxel.frag`: hemispheric ambient (`phxAmbientAtmos`, driven by the sky colour) → sun
(Cook-Torrance, shadowed, sky-gated) → moonlight → baked block light → point lights → spot lights →
masked emission → aerial perspective → `phxTonemap`.

### Exposure is required, not polish
A physical atmosphere returns **radiance**: a noon sky is ~0.02 and a lit diffuse surface ~0.1,
whereas the flat clear colour it replaced was a display-referred 0.45–0.95. Rendering radiance
straight to an 8-bit display gives a nearly black frame — measured. `phxTonemap(color, exposure,
curve)` applies exposure and then **AgX**.

AgX rather than ACES: ACES desaturates bright colours toward white, which is the washed-out look
this work exists to remove and would bleach the warm sun the atmosphere works to produce. AgX does
its curve in inset primaries and rotates back out, which is where its hue preservation comes from.

⚠️ `phxTonemap` returns **LINEAR**. AgX's own output is display-referred, so it is converted back
with the 2.2 power. Dropping that step double-gammas the frame.

Live knob: `POST /api/debug/tonemap {"exposure": float, "curve": int}` — curve 0 = none (raw linear,
for A/B), 1 = AgX. Default exposure **8.0**, calibrated by sweep.

⚠️ **Staging note.** The tone map currently runs in the scene fragment shaders rather than in a
post-process pass, because **the editor viewport samples the raw offscreen HDR image and never sees
the swapchain post-process pass**. That is the documented reason bloom, SSAO and an earlier Reinhard
tone map were disabled — their bugs shipped in packaged games unseen. Until an editor-visible grade
pass exists, tone mapping in the scene shaders is the only form of it an author can actually see.
The function lives in one file so moving it later is a deletion, not a rewrite.

**Not yet on the shared model:** `character.frag` (its own `kSkyFill`, no ambient floor, no haze,
Blinn-Phong, mid cascade only) and the water shaders. They will read brighter than the world.

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
| **No real AO** | AO is implicit in the skylight nibbles, so it vanishes outdoors (sky = 15) and indoors (sky = 0). A dedicated per-corner AO channel fits in the 16 spare bits of `light2`/`light3`. |
| **Bloom produces spots** | ⛔ BROKEN, ships off. Spots/blotches instead of a glow; suspected fireflies from bright single pixels (sky star/airglow noise, grass speckle), widened by the half-res blur. See §6. |
| **No AA** | The grade pass now exists, so FXAA/TAA is unblocked but not built. |
| **Point lights** | See §4 — unshadowed, wrong coordinate space, double-counted, not persisted. |
| **Metals** | No environment/IBL term, so they read dark except in direct light. |
| **T-junction cracks / character speckle** | Open render defects at greedy-merge borders; see `RenderOptimization.md`. |

## Related

- [`NearShadowCascade.md`](NearShadowCascade.md) — the canonical cascade record.
- [`WorldRenderV2Plan.md`](WorldRenderV2Plan.md) — §3.3 and §7c designed much of the atmosphere work.
- [`EngineAdvancesResearch.md`](EngineAdvancesResearch.md) §4 — radiance cascades, the GI option.
- [`VoxelRenderPipelines.md`](VoxelRenderPipelines.md) — the three voxel vertex shaders and
  `InstanceData`, which carries the baked light words.

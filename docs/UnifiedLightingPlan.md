# Unified Lighting — one emitter model, real geometry, no flood fill

> **Status: M0 DONE on branch `lighting-rebuild` (2026-08-29). The flood is deleted.**
> Opened 2026-08-28 after a long debugging session on "interior lights shine through walls"
> turned up a deeper problem than the bug being chased. The approved milestone plan lives in
> `~/.claude/plans/floating-painting-bird.md`; this document holds the diagnosis and evidence.
>
> **Decisions taken 2026-08-29 (user):** quality first, optimise after · design for indirect
> bounce from the start · delete the flood first, then rebuild · delete the computation but keep
> the vertex-light transport for now (decide in M4) · work on a branch so `main` keeps usable
> lighting for CityForge.
>
> ### M0 record
> `ChunkRenderManager::rebuildCubeFaces` lost 356 lines: the per-cell opacity mask
> (`m_lightOpaque`/`m_subFill`) and both BFS floods. The light field now carries a **defined
> placeholder** — skylight uniform 15, block light 0 — *not* zero, because skylight also gates the
> sun and the ambient term, so zeroing it renders the world black and hides the replacement's
> behaviour rather than exposing it.
>
> Verified live: every probed cell now reads `sky=15, block=(0,0,0)`, no opacity mask. The cell
> that produced the interior black band, `(72,17,9)`, went from `sky=0` to `sky=15`. Measured at
> the same camera pose as the band evidence: upper wall **173.9 → 18.2**, band **1.9 → 24.5** —
> i.e. the band is gone and interiors are uniformly dim, matching the earlier `flood_bypass mask=3`
> measurement (18.2 / 24.5) exactly, which is the cross-check that the placeholder does what was
> predicted.
>
> **15 tests across `LightBakeOcclusionTest`, `LightBleedTest` and `LightWallMatrixTest` are now
> `DISABLED_`, not deleted.** Each states a requirement the replacement must meet (a sealed room
> admits no daylight; a wall holds an interior light in; a sub-voxel roof occludes). They are the
> acceptance gates for M2 and M3 and must pass unchanged, on the same geometry.
>
> ⚠️ `POST /api/debug/flood_bypass` is now **inert** — there is no flood to bypass. The endpoint and
> `s_floodBypass` are kept for M2/M3 to repurpose against the new systems.

## The directive

> *"if everything fell behind one lighting system, then 'dark rooms' would be solved the real
> world way, with actual small light sources. I dont see why we are worried about changing
> things, when what we have right now is a badly hacked system of multiple sources."*
>
> *"i hate having anything too similar to minecraft in this engine … i really hate that we have
> minecraft style lighting in this engine, and would prefer to get rid of it."*

## The target architecture

**Everything that emits light is a light source, and every light resolves visibility against the
real geometry.**

| emitter | form | visibility term |
|---|---|---|
| sun / moon | directional | shadow lookup |
| sky | dome | how much of the dome this surface can see |
| fire, torch, candle, lamp, forge, spell | point / area | is the path from surface to source clear |

One equation for all of them: **radiance × visibility**. Same geometry for every one of them —
cubes, subcubes, microcubes, and kinematic/item geometry alike.

Two consequences worth stating plainly, because they are the whole point:

* **There is no "ambient system".** What the engine currently calls ambient *is* the sky dome, and
  "how enclosed is this room" *is* that emitter's visibility term. It stops being a special case.
* **Dark rooms need no special handling.** A sealed windowless room is black because nothing emits
  into it. Cut a window and the sky lights it through the opening. Put a candle in and the candle
  lights it. The behaviour falls out of the model instead of being approximated by a stored field.

The one genuinely missing physical phenomenon after that is **indirect bounce** — a room lit
through a window has hard black corners until light bounces off the floor into them. That is real
physics with a real solution (GI), not a reason to keep an approximation.

---

## Current state — systems that disagree about what a wall is

> ⚠️ **THIS CENSUS WAS WRONG WHEN WRITTEN, AND THAT IS THE ROOT ERROR OF THIS PLAN.**
> It originally said "three systems" and enumerated sun/moon, the per-cell flood, and forward
> point/spot lights. A plan whose stated purpose is to UNIFY lighting is worthless if its inventory
> of what needs unifying is incomplete — and this one was. Two more were found later, by accident,
> in conversation rather than by audit:
>
> * **Emissive materials** (D12) — a whole emitter class that has radiance and NO transport. Missed
>   entirely, because it does not illuminate anything and so did not look like a "lighting system".
> * **`transparent_voxel.frag`** (D13) — a full duplicate of the voxel lighting model, for the same
>   voxel data, which has silently diverged from the original it was copied from.
>
> Superseded by the full audit below.

## THE ACTUAL MAP — every place lighting is decided (audited 2026-08-30)

Read in full: `shaders/lighting.glsl` (323 lines). Measured across all 16 scene fragment shaders,
plus the CPU light-creation paths, the emissive material path, the shadow-caster registrations and
the flood remnants. **Where a row says NO, that was verified by grep on the actual file, not
inferred.** Not yet read end-to-end: `ShadowMap.cpp` (823), `Atmosphere.cpp` (236),
`PostProcessor.cpp` (2255), the water shading bodies.

| shader | ambient | shadow filter | point lights | **M2 visibility** | emissive | tonemap |
|---|---|---|---|---|---|---|
| `voxel` | Atmos | PCSS | yes | **YES** | yes | – |
| `character` | Atmos | **own 16-tap PCF** | yes | **NO** | yes | – |
| `transparent_voxel` | **own flat** | **own 16-tap PCF** | yes | **NO** | yes | – |
| `mirror_voxel` | – | – | yes | **NO** | yes | – |
| `grass` | Atmos | Fast 4-tap | **no** | – | yes | – |
| `foliage` | Atmos | Fast 4-tap | **no** | – | yes | – |
| `far_terrain` | Atmos | Fast 4-tap | **no** | – | yes | – |
| `far_tree_mesh` | Atmos | Fast 4-tap | **no** | – | yes | – |
| `water`, `water_cell` | – | – | **no** | – | yes | **yes** |
| `water_underwater` | **own flat** | – | **no** | – | yes | **yes** |
| `post_process` | – | – | – | – | – | **yes** |

### What that map actually says

1. **⚠️ M2 — the fix for the reported bug — is in 1 of the 4 shaders that shade point lights.**
   `character`, `transparent_voxel` and `mirror_voxel` each carry their OWN point-light loop with no
   visibility term. **A lantern sealed inside a stone room still lights a character standing
   outside it, and still shines through glass.** M2 was reported as "the reported defect is fixed";
   it is fixed *for chunk voxels*. This is the single most misleading thing in this document's
   history and it is mine.
2. **M3 (traced sky) is in 1 of the 7 shaders that consume ambient.** Even when re-enabled, grass,
   foliage, far terrain, far trees, characters, glass and water would keep flat sky access.
3. **Three shadow implementations, not one.** PCSS (voxel), Fast 4-tap (vegetation/far), and a
   hand-rolled 16-tap PCF duplicated in `character` and `transparent_voxel` — the latter two sample
   the MID map only, so characters and glass get no near or far cascade.
4. **Three ambient implementations.** `phxAmbientAtmos` (6 shaders), a flat `vec3(ubo.ambientLight)`
   in `transparent_voxel` and `water_underwater`, and `phxAmbient` — the legacy hemispheric model,
   which is now **dead code with zero callers** and should simply be deleted.
5. **A torch does not light grass, foliage, far terrain, far trees, or water.** None of those
   shaders read the light SSBO at all. Vegetation is lit by sun + ambient only.
6. **Emissive is replicated across 12 shaders and emits nothing** (D12), with no CPU bridge —
   `MaterialRegistry` carries `emissive`/`emissiveStrength` purely as a render flag and nothing
   anywhere converts it into a light.
7. **Tone mapping runs in 4 shaders.** `post_process` grades the scene; the three water shaders
   ALSO call `phxTonemap` themselves. **Open question, must be measured not argued: is water
   double-tonemapped?** The comment in `water.frag` says its own call was added to fix a divergence,
   which implies the water pass is not covered by the post grade — verify before touching.
8. **Far cascade casters are far-terrain tiles and tree-LOD meshes only** — not chunks, characters,
   kinematics, particles or foliage. (D10 said "no chunk casters", which is right, but omitted that
   two pipelines DO register; corrected here.)
9. **Grass casts no shadows at all** — `GrassRenderPipeline::s_castShadows = false`, verified.
10. **The flood's storage is still allocated and served.** `m_skyLight` (=15), `m_blockR/G/B` (=0)
    still exist, `lightAt`/`bakedLightAt` accessors still answer, and `InstanceData` still carries
    the per-corner words. That is M4's deletion list, and until it happens every chunk still pays
    the memory.

**Standing rule for this section:** if anything is ever added to this map, this audit was incomplete
too — **re-run the measurement, do not reason about it.** The original "three systems" census was
written from inspection and was wrong; that single shortcut put a false premise under everything.

---

# ⚠️ THE GREP-DERIVED MAP BELOW WAS WRONG IN THREE PLACES. See "THE READ MAP" further down.

Reading the files (rather than grepping them) overturned three claims I had written into this plan
as fact. Recorded because the pattern matters more than the individual errors:

| claim (from grep) | truth (from reading) |
|---|---|
| `debris.frag` does no lighting | it has a **hardcoded fake sun**, `normalize(vec3(0.5,1.0,0.3))` — a false NEGATIVE, because it never mentions `sunDirection` |
| water self-tonemaps → possible double grade (**D17**) | the water shaders do **not** call `phxTonemap`; the grep matched a COMMENT saying *"Do not re-add a tone map here"* — a false POSITIVE. **D17 is deleted, it was never a defect.** |
| `mirror_voxel.frag` shades point lights | it declares the light SSBO for descriptor compatibility and **never reads it**; it is pure projective reflection — a false POSITIVE. Point-light shaders number **three, not four.** |

Two false positives and one false negative out of a dozen grep-derived claims. Everything below
this line comes from reading the file.

# THE READ MAP (2026-08-30)

**Read in full:** `lighting.glsl` (323) · `atmosphere.glsl` (377) · `Atmosphere.cpp` (236) ·
`DayNightCycle.cpp` (156) · `CelestialBody.cpp` (229) · `LightManager.cpp` (247) · `Light.h` ·
`character.frag` (215) · `grass.frag` (124) · `foliage.frag` body · `post_process.frag` (72) ·
`sky.frag` body · `mirror_voxel.frag` body · `debris.frag` · `vfx.frag` · `far_tree.frag` body ·
`ShadowMap.cpp` pipeline + caster sections · the atmosphere→GPU integration in `RenderCoordinator` ·
`static_voxel.vert` / `kinematic_voxel.vert` / `dynamic_voxel.vert` light paths · every
light-creation path.

**Deliberately NOT read, and why they cannot change the architecture:** `PostProcessor.cpp`'s
Vulkan resource plumbing (its lighting surface is the three grade push constants, which I read in
`post_process.frag`), `ssao.frag` (the SSAO composite is disabled — see below), the eight
`*_shadow.vert` caster transforms (they position casters, they make no lighting decision),
`ImGuiRenderer`'s light-editing UI, `RenderPipeline.cpp`'s pipeline construction.

## A. EMITTERS — what produces light

| # | emitter | mechanism | state |
|---|---|---|---|
| A1 | sun / moon / sky bodies | `DayNightCycle` + `CelestialBody` + `Atmosphere::skyIrradiance/hazeHorizon/hazeZenith`, packed into `AtmosphereUniforms` | **physical and coherent — the good part** |
| A2 | point lights | `LightManager`, **hard cap 32** | see A2-note |
| A3 | spot lights | `LightManager`, hard cap 16 | same |
| A4 | emissive materials | `MaterialRegistry` `emissive`/`emissiveStrength` → a per-shader `albedo * emissiveMultiplier` | **self-shading only. Emits NOTHING. No CPU bridge exists.** |
| A5 | VFX particles | `vfx.frag`, additive premultiplied | emissive-only, lights nothing (correct for particles) |

**A2-note — the cap is worse than "capped".** `LightManager::addPointLight` refuses at 32 and returns
−1. There is **no distance culling, no priority, no eviction**: the first 32 lights ever registered
win permanently, wherever they are in the world. A torch in the player's hand contributes nothing if
32 lights were registered anywhere first. `StructureForge` already logs
`"place_lights: light capacity reached"`. Contributors to those 32 slots: structure fixtures,
settlement build, VFX bursts, **NPC attached lights**, item effects, and manual/editor lights.

## B. RECEIVERS — nine different implementations of "how is this surface lit"

| # | shader | ambient | shadow | point lights | M2 visibility |
|---|---|---|---|---|---|
| B1 | `voxel.frag` (static + **kinematic** + **GPU dynamic** voxels) | Atmos | PCSS | yes | **yes** |
| B2 | `character.frag` | Atmos | **own 16-tap PCF, MID map only** | own loop | **no** |
| B3 | `transparent_voxel.frag` | **own flat** | **own 16-tap PCF, MID map only** | own loop | **no** |
| B4 | `mirror_voxel.frag` | **none — pure projective reflection, no lighting at all** | – | **no (declares the SSBO, never reads it)** | n/a |
| B5 | `grass` / `foliage` / `far_terrain` / `far_tree_mesh` | Atmos | Fast 4-tap | **none** | – |
| B6 | `water` / `water_cell` | own | none | none | – |
| B7 | `water_underwater` | **own flat** | none | none | – |
| B8 | `debris.frag` | **`max(dot(N, normalize(vec3(0.5,1.0,0.3))), 0.2)` — a HARDCODED FAKE SUN** | none | none | – |
| B9 | `far_tree.frag` | `vShade = 0.85 + 0.15*dot(up,-sunDir)` scalar only | none | none | – |

B8 is the worst: CPU debris is lit by an imaginary fixed sun at every hour of the day. It does not
read `sunDirection` at all, which is why a `sunDirection` grep gave a FALSE NEGATIVE and my first
matrix recorded it as "no lighting".

## B-EXTRA — what only reading revealed

* **`character.frag` uses a raw normalized-depth shadow bias**, `kShadowBias = 0.0009`, hardcoded.
  `lighting.glsl` documents that exact policy as the bug it fixed: a raw constant's PHYSICAL size
  scales with shadow distance (0.26 u at 40 u, 0.85 u at 420 u). Characters are still on the broken
  policy, and their own header comment admits the mid-cascade-only limitation as "deliberately
  deferred".
* **`character.frag` carries a dead `const float kSkyFill = 0.35;`** — a leftover from before it
  moved to `phxAmbientAtmos`. Unused, and exactly the kind of stale constant that gets "restored"
  by a later reader.
* **`grass.frag` and `foliage.frag` DO sample the near cascade** and min-compose it, and they DO
  honour debug modes 1 and 2. My D4 ("debug views exist only in `voxel.frag`") was **overstated** —
  vegetation is better integrated than characters are. What vegetation lacks is modes 3–9 and any
  point-light term.
* **BLOOM IS BROKEN AND SHIPPED OFF.** `post_process.frag`: *"⛔ BROKEN: bloom produces
  SPOTS/BLOTCHES, not a smooth glow (confirmed 2026-08-15). Ships with `grade.bloom == 0`."*
  **Emissive materials rely on bloom for their glow**, so the one visual payoff emissive currently
  has is disabled. SSAO is likewise disabled (grazing-angle band artifact).
* **The editor viewport never sees the tone map.** `post_process.frag` grades the swapchain; the
  editor displays the raw linear offscreen texture. So authoring happens on an ungraded image and
  standalone games on a graded one. (Screenshots go through the swapchain, so the measurements in
  this document ARE graded — which is why saturated red read as (255,198,188).)
* **Shadow cull modes differ by caster class, correctly and deliberately:** the main chunk pipeline
  BACK-culls; the character/kinematic/dynamic depth-only pipelines use `CULL_NONE` because
  mesh-time face culling means voxel geometry is not closed. Both use bias 1.25/1.75.
* **`DayNightCycle` computes a full parallel lighting model that is mostly dead.** Its `m_sunColor`
  (hand-authored horizon→noon ramp) is **overwritten** by the atmosphere's dominant-body colour
  whenever the cycle is enabled; its `m_skyColor` is now only a clear-colour fallback. But its
  `m_ambientStrength` (a 0.06→1.0 twilight ramp) is **NOT** overwritten — it still feeds
  `ubo.ambientLight`, which is what `transparent_voxel` and `water_underwater` use for their flat
  ambient. **Two CPU ambient models are live simultaneously**, one a 0..1 strength scalar and one a
  physical radiance, feeding different shaders — the precise trap `lighting.glsl` warns about in its
  own comments.
* **The sky (`atmosphere.glsl` + `Atmosphere.cpp`) is the healthiest part of the engine's lighting.**
  Single-scatter Rayleigh/Mie with an ozone layer, geometric moon phase, planet-shadow twilight,
  data-driven multi-body support, and a parity test that parses the GLSL and asserts every constant
  matches C++. `skyIrradiance()` is a cosine-weighted hemisphere average — i.e. **exactly the
  unobstructed-sky value that M3's visibility term is supposed to scale.** The architecture for
  "sky as an emitter" is already correct; only the visibility factor is missing and too slow.

## C. SKYLIGHT SUPPLY — SIX mechanisms, not three

Six different vertex-stage mechanisms supply "how much sky does this surface see":
* `static_voxel.vert` — per-corner 4-bit nibbles from `inLight` / `inLight2` / `inLight3` → `voxel.frag`
* `kinematic_voxel.vert` — a push constant, `pc.bakedLight.x`, sampled at the object's position → `voxel.frag`
* `dynamic_voxel.vert` — an instance field, `inDebrisLight` → `voxel.frag`
* `character_instanced.vert` — `fragBakedLight` (vec4: x = sky, yzw = block RGB) → `character.frag`
* `grass.vert` — `vSky` / `vBlock` → `grass.frag`
* `foliage.vert` — `vSky` / `vBlock` → `foliage.frag`
All currently deliver 1.0, because M0 pinned the flood to `sky = 15`. The storage
(`m_skyLight`, `m_blockR/G/B`, the three `InstanceData` words) is still allocated and still uploaded
per instance.

## D. SHADOWS
Three cascades (near 40 u / mid 420 u / far 1600 u), `VK_CULL_MODE_BACK_BIT`, depth bias 1.25/1.75.
**Casters:** chunks, characters and kinematics into near+mid; far terrain tiles and tree-LOD meshes
into far. **Non-casters:** grass (`s_castShadows = false`), far-tree impostor cards, CPU debris, VFX.
**Receiver filters: three** — PCSS, Fast 4-tap, and the duplicated 16-tap PCF in B2/B3.

## E. TONE / EXPOSURE
`phxTonemap` is called in `post_process.frag` **and** in all three water shaders. `m_exposure` = 8.0,
calibrated against the deleted flood and a since-corrected AgX curve, never re-derived. Bloom lives
in `PostProcessor`.

⚠️ **The grade HAD a receiver nobody counted: the game HUD** — `UISystem` drew
`resources/ui/default_hud.json` (health, hotbar) into the **offscreen scene image**, before
`compositeToGrade`, so it was bloomed and AgX-tonemapped along with the world. **FIXED 2026-08-30
(D18):** the HUD now draws in the post-process pass, after the grade, and is measured bit-identical
across a 5.3× exposure change and a bloom toggle. ImGui was never affected — it already drew after
the composite. **The grade boundary is now: everything before `compositeToGrade` is graded; UI drawn
after it is not.** Keep it that way — anything new that wants to be UI belongs after the composite.

---

# THE UNIFICATION PLAN (U1–U7) — implementation, in dependency order

The goal in one line: **one emitter registry, one receiver model, one visibility term, one skylight
supply, one grade.** Each step below names the files, the change, and the gate. Steps are ordered so
that no step depends on a later one.

### ✅ U1 — DONE 2026-08-30 (with one documented exception)

**Structural gate, verified by grep on the shipped shaders:**
* `phxAmbient` **deleted** — the legacy hemispheric model had zero callers, and a second ambient
  model living in the same file is precisely how passes desync.
* **No scene shader owns a shadow filter any more.** `voxel`, `character` and `transparent_voxel`
  all call `phxShadowPCSS`; `grass`, `foliage`, `far_terrain`, `far_tree_mesh` all call
  `phxShadowFast`. The only remaining mentions of a private `poissonDisk` are comments recording
  its removal.
* **`character.frag`**: its hardcoded `kShadowBias = 0.0009` — a raw normalized-depth constant,
  the exact policy `lighting.glsl` was created to replace because its physical size scales with
  shadow distance — is gone, replaced by `phxShadowBias` via `phxShadowPCSS`. It now also
  min-composes the **near cascade** (binding 9), which grass and foliage have had since
  2026-08-06 and characters never did. Its dead `kSkyFill = 0.35` is deleted.
* **`transparent_voxel.frag`**: flat `vec3(ubo.ambientLight)` → `phxAmbientAtmos`, own PCF →
  `phxShadowPCSS` + near cascade, and it now declares the `vSkyLight` varying `static_voxel.vert`
  was already emitting — so glass finally has sky gating. Glass and the stone beside it are now
  lit by one model.
* **`debris.frag`**: the fixed direction is no longer pretending to be light. `DebrisRenderPipeline`'s
  CPU sampler now returns a finished linear colour built from the REAL sun + atmosphere ambient
  (cached from the same values handed to the shaders, so it cannot drift), gated by sky access; the
  shader keeps a narrow 0.75..1.0 wrap purely as a FORM term, documented as such — a per-particle
  sun term would make tumbling chips flicker.

**Runtime health:** builds clean, **zero Vulkan validation errors**, and with a glass wall and a
stone wall side by side the frame responds to sun movement at 5.9M / 6.1M / 29.5M against a
**51,117 noise floor** (noise-mask method, 92.0% of pixels stable).

⚠️ **Documented exception — `ubo.ambientLight` survives, deliberately.** The plan said to delete it.
Two reasons not to:
1. It sits MID-STRUCT in the shared std140 UBO (between `numInstances` and `emissiveMultiplier`).
   Removing it shifts every following offset in every shader that declares a prefix — a very large
   blast radius for no behavioural gain.
2. Its one remaining reader, `water_underwater.frag`, uses it as a **fog brightness scalar**, not as
   a surface-ambient model. Converting that to physical radiance needs a calibration constant I
   would be guessing at, and guessing is what this plan exists to stop.
**The second ambient MODEL is gone** — no shader derives surface ambient from the scalar any more,
which was the actual defect. The scalar remains as a day/night brightness factor.
**Follow-up:** decide whether `DayNightCycle::m_ambientStrength` keeps feeding it, or the fog term
moves to a luminance of `ambientColor` with a measured constant. Logged, not silently dropped.

### U1 — original specification
*Files:* `transparent_voxel.frag`, `character.frag`, `debris.frag`, `far_tree.frag`,
`water_underwater.frag`, `lighting.glsl`, `DayNightCycle.{h,cpp}`, `VulkanDevice` (UBO).
1. **Delete `phxAmbient`** — the legacy hemispheric model, **zero callers**, verified by reading.
2. **Kill the second CPU ambient model.** `DayNightCycle::m_ambientStrength` still feeds
   `ubo.ambientLight`, which is the flat ambient `transparent_voxel` and `water_underwater` use.
   Move both to `phxAmbientAtmos(N, sky, ubo.ambientColor)` and then **delete `ubo.ambientLight`
   and the `m_ambientStrength` ramp**, or the two models will drift back apart. Also delete
   `m_sunColor`/`m_skyColor`'s dead ramps, or mark them explicitly as the cycle-disabled debug path.
3. **`character.frag`:** replace the private 16-tap PCF and its **raw `kShadowBias = 0.0009`** with
   `phxShadowPCSS` + `phxShadowBias` (world-unit policy), and wire the **near cascade** (binding 9)
   that grass and foliage already sample. Delete the dead `kSkyFill = 0.35`. Swap Blinn-Phong for
   the shared BRDF, or record why a character wants a different response.
4. **`transparent_voxel.frag`:** same treatment — shared ambient, `phxShadowPCSS`, all three cascades.
5. **`debris.frag` — SPECIFIED 2026-08-30, and my earlier framing was too harsh.** The fixed
   direction is documented in `DebrisRenderPipeline` as *"a small fixed directional term on top for
   form"* — a deliberate shape-giving term, not a forgotten sun. The real light already arrives via
   a **CPU sampler**: `setLightSampler` calls `chunkManager->sampleBakedLight(pos)` and modulates
   the particle's vertex colour. That sampler now reads the DEAD flood (sky = 15 → 1.0, block = 0),
   so debris is uniformly lit by a constant.
   *Fix:* extend the existing CPU sampler to deliver real sun + `ambientColor` (and, once U3.1
   lands, nearby point lights) instead of binding a UBO to the shader — the pipeline has
   `bindingCount = 0` and only push constants, so the CPU path is much the smaller change and is
   in keeping with how debris already works. Keep the form term, and label it.
6. **`far_tree.frag`:** the `vShade` scalar is a legitimate cheap tier for ≥900 u impostors — keep
   it, but derive it from the shared model and label it a deliberate LOD, not an accident.
*Gate:* no scene shader computes its own ambient or shadow filter; `phxAmbient` and `ubo.ambientLight`
gone; a fixed-pose A/B per affected surface class (glass, character, debris, far tree, underwater).

### ⚠️ MEASUREMENT RULE, learned the hard way 2026-08-30
**`GET /api/screenshot` captures the SWAPCHAIN, which in the editor INCLUDES the ImGui UI.**
Measured on a mode-5 capture: (69,69,69) 28%, (91,92,111) 15%, (122,123,134) 13%, (56,56,56) 4% —
**~65% of the frame is editor chrome, not the scene.**
Consequences, and they are not uniform:
* **ABSOLUTE pixel statistics are invalid** — "90% of the frame is lit" was mostly panel grey. Every
  absolute percentage taken this way understates the scene by roughly 3x.
* **DELTAS remain valid**, because the UI is identical in both captures and cancels: the M2
  blocked-light figures, the M3 interior 30.10 -> 18.33, the mode-8 red 3 -> 186,408 and U3.1's
  +21.97 are all differences and stand.
* **`GpuProfiler` scope timings are unaffected** — D1's numbers are timestamps, not pixels.
**Rule: measure lighting by DIFFERENCE between two captures, never by an absolute fraction of the
frame; and prefer the numeric probes over pixels wherever one exists.**

### U2 — ONE VISIBILITY TERM. M2 applies to every point-light consumer. **(closes D14)**
*Files:* new `shaders/occupancy.glsl`, `voxel.frag`, `character.frag`, `transparent_voxel.frag`.
**Three shaders shade point lights, not four** — `mirror_voxel` declares the SSBO and never reads it.
Move `phxOccupancySolid` / `phxDdaHitsSolid` / `phxLightVisibility` out of `voxel.frag` into a shared
include. `lighting.glsl`'s contract forbids implicit buffer reads, so the new file declares bindings
11/12 explicitly and is included only by shaders that bind them.
*Gate:* the sealed-box M2 rig repeated with a character inside/outside and with a glass wall — zero
exterior contribution in each, live positive controls.

**STATUS 2026-08-30 — IMPLEMENTED, GATE NOT YET MET.**
Done: `shaders/occupancy.glsl` created (bindings 11/12 + `phxOccupancySolid` + `phxDdaHitsSolid` +
`phxLightVisibility`, with `occBox` passed as a PARAMETER so it does not depend on any shader's UBO
prefix); `voxel.frag` now includes it instead of defining it; `character.frag` and
`transparent_voxel.frag` include it and gate their point/spot loops on it. Both needed a long
std140 prefix to reach the trailing `occupancyBox` field.
Also done, as D4's prerequisite: `character.frag` gained debug modes 1/2/5/8, and
`grass`/`foliage`/`sky` now render flat dark in modes >= 3 so an isolation view is not drowned by
passes that do not implement it.
Verified: all shaders compile; Release runs with **zero Vulkan validation errors**; and
**`occupancyBox` IS correctly aligned in `character.frag`** — its mode 8 reads red/green (occupancy
readable) with blue only 1.4% (sky), where a bad std140 prefix would have made every character
pixel blue. That was the one shader-specific risk, since the traversal itself is now literally the
same code voxel.frag runs and is unit-tested.
**NOT verified:** the end-to-end behavioural gate. Two reasons, both mine:
1. `POST /api/entity/spawn {"type":"animated","x":..,"y":..,"z":..}` did **not** create a character —
   the entity list afterwards held only `player` and `entity_1`. **There was no character in frame,
   so the capture measured terrain and UI.** Find the correct spawn call (or move the existing
   `player`) and re-run.
2. The screenshot/UI contamination above made the absolute readings meaningless.
## ✅ U2 GATE MET 2026-08-30 (voxel half), and the measurement method had to be fixed first

| | added light (masked) |
|---|---|
| noise floor — two identical captures | 30,662 |
| sealed light, trace **OFF** (old behaviour) | 1,252,485 |
| sealed light, trace **ON** | **41,811 — at the noise floor** |
| control: same light OUTSIDE | 55,792,910 (control ALIVE) |

**Tracing removes 96.7% of the leak and what remains is indistinguishable from noise.**

**The character half** is answered by the forced-occlusion diagnostic rather than by pixels: forcing
`phxLightVisibility` to `return 0.0` collapsed the control from 46,234,692 to 3,066,639 — a 15x
drop, far above the noise floor — proving the visibility term IS applied in the shaders that call
it. A separate sealed-box character capture was NOT taken, because an idle-animating character is
inherently noisy for this method and would be masked out by the fix below.

### ⚠️ THE MEASUREMENT WAS BROKEN, AND EVERY EARLIER PIXEL DELTA SAT ON A NOISE FLOOR
Before the mask, this same gate measured **52.7% removed** and I spent three rounds hunting a
"residual leak" that did not exist. The chain of errors, all mine:
1. Screenshots include the editor's ImGui UI (~65% of the frame).
2. Those widgets — frame-time graphs, counters — **repaint every frame**. A null-delta control
   (capture the unchanged scene twice) measured **1.3M–7.0M**, the same magnitude as the "leak".
3. So the delta method was reading its own noise, and attributing it to voxels and characters.

**THE RULE, now mandatory for every lighting capture:**
* **Take a NULL-DELTA CONTROL first** — capture the unchanged scene twice. If the delta is not
  small, nothing measured below it means anything.
* **Build a NOISE MASK** — capture the still scene ~5 times, mark every pixel that ever moves, and
  measure only over pixels that stayed put. Here that kept 94.9% of the frame and dropped the floor
  from ~5,000,000 to **30,662**, a ~160x improvement in signal-to-noise.
* Animated content (characters, grass, water) is masked out by construction; measure it by a
  different means, not by pixel delta.
Harness: `scratchpad/u2_gate5.py` is the worked example.

---

### (superseded) GATE RE-RUN before the noise mask — the 52.7% figure below was ARTIFACT

D2's world now exists: **`PhyxelProjects/LightingGates`** — Flat, non-streaming, flora density 0.
Built because the populated benchmark world produced three separate rig failures, the last of which
was decisive: `terrain_height` was read BEFORE streaming settled, so a whole sealed box was built
underground and the capture was a picture of dirt.

The gate script now CHECKS every precondition instead of assuming it, and all of them passed:
* terrain height **stable** across repeated polls (y=16) before anything is built;
* batch placement refused **0** of 322 voxels;
* the box is **visible to the tracer** — wall/roof/floor cells read 729/729, interior 0;
* the character **exists at the probe point** (verified from `/api/entities`);
* the numeric probe says the sealed light **is blocked**, by cube [16,18,40] — the wall.

Result, as deltas (editor UI cancels), debug mode 5:

| | added light | px brightened |
|---|---|---|
| trace OFF (old behaviour) | 6,767,528 | 73,867 |
| **trace ON (U2)** | **3,198,292** | **38,890** |
| control: light OUTSIDE | 46,234,692 | 276,592 (control ALIVE) |

**Tracing removes 52.7% of the leak. The gate demands zero.** Attribution, same scene, tracing on:
character present 6,328,800 vs character removed 3,377,993 — so **roughly half the residual is the
character and half is voxel geometry**.

That is the confusing part and it is unresolved: the CPU mirror says blocked, the occupancy contains
the box exactly, `occupancyBox` is provably aligned in `character.frag` (mode 8 reads red/green, not
blue), and `voxel.frag`'s own M2 gate passed earlier on the grounded wall rig at all five
thicknesses. Something in the GPU path still admits light the CPU path rejects.

**NEXT DIAGNOSTIC — do this first, it is decisive and cheap.** Temporarily force
`phxLightVisibility` to `return 0.0;` unconditionally and re-capture:
* if the residual **vanishes**, the leak flows through the visibility path and the fault is in the
  trace's inputs (surface/light world reconstruction, or the `occBox` value at that call site);
* if the residual **remains**, the light is reaching those pixels through some OTHER term entirely
  and the visibility function is innocent — in which case find that term before touching the trace.
Do not theorise further before running it; three hypotheses have already failed here.

### U3 — ONE EMITTER REGISTRY. Emissive voxels become real lights; the cap gets spatial management.
**(closes D12, D15, and the light cap)**
*Files:* `LightManager.{h,cpp}`, `MaterialRegistry`, `ChunkRenderManager` (emissive voxel discovery),
`RenderCoordinator` (per-frame selection), `grass/foliage/far_terrain/far_tree_mesh.frag`.
1. **✅ DONE 2026-08-30 — spatial selection instead of first-come.**
   `MAX_POINT_LIGHTS`/`MAX_SPOT_LIGHTS` are now an **upload budget**, not a storage limit:
   `addPointLight` accepts without limit (with a one-shot leak warning at 4096) and `getGPUData`
   `partial_sort`s by **relevance = distance from the viewer to the light's SPHERE**
   (`|pos − viewer| − radius`, negative when the viewer is inside it). Subtracting the radius is
   what stops a nearby candle displacing a hearth whose glow actually fills the room. Ties break on
   id so the selection is stable frame to frame and cannot flicker. `droppedPointLights()` reports
   how many enabled lights exceed the budget.
   *Tests (55/55 lighting suite green):* nearest-light-always-uploaded, uploaded-set-follows-viewer,
   sphere-relevance ordering, stability at equal relevance, and both former "capacity limit" tests
   **rewritten** to the new contract with the reason recorded in-file.
   *RED-BEFORE-GREEN, actually run:* restoring first-come selection turned exactly the two
   behavioural gates red with their intended messages ("the nearest light was not uploaded", "the
   uploaded set did not follow the viewer"); restoring the selector returned them green.
   *L4, Release, generated town:* 120 lights registered at runtime — **120 accepted, 0 refused**
   (the old code would have refused ~95). A **128th** light added beside the camera, after 127
   others, brightened the frame **+21.97 mean luminance across 384,262 pixels**; under first-come it
   would have been refused and changed nothing.
   ⚠️ *Honest limit:* the engine-generated town produced only **7** fixtures, so the "100-fixture
   city" case was exercised with API-placed lights, not generator-placed ones. The zero
   "capacity reached" warnings in that run prove nothing on their own — there were only 7 lights.
   ⚠️ *Approximation:* relevance is to the VIEWER, not to the view frustum. A light behind the
   camera can still take a slot from one just off-screen ahead. Frustum-aware selection is the
   refinement if this ever shows.
2. **✅ DONE 2026-09-01 — emissive voxels are real lights.** *(closes D12)*

   The walk the flood used is restored, but it emits **point lights** instead of BFS seeds, so a
   glow block now obeys the same rule as a torch or a spell: inverse-square, dependent on the
   receiving normal, and occluded by M2's traced visibility term — none of which a per-cell flood
   could do. Seeding rules recovered verbatim from the deleted code (commit `089ff2cb`): burning
   voxels take the per-voxel tint at scale 15/9; otherwise the material must be `emissive` or carry
   `emissiveStrength > 0`, hue from `physics.colorTint`, scale 15 or
   `clamp(strength * 4, 2, 10)` — the masked-emissive branch kept separate so the enchanted log
   keeps its dim crack-light.

   Chunks cache their emitters at bake time (`ChunkRenderManager::EmissiveLight`) and
   `RenderCoordinator` reconciles the union into `LightManager` **on change**, hashed over
   position/colour/radius. Not per frame: re-registering every frame would churn light IDs and
   defeat the id tie-break that keeps U3.1's selection stable.

   **Gate met.** 6 `glow` voxels placed through the WORLD api — no `/api/light` call anywhere —
   produced **6 registered lights**; removing them returned to **0**, with the log showing the
   reconcile stepping 3 → 2 → 1 → 0. No leak. Lighting suite 80/80 (the one red in the wider filter
   is the pre-existing D19).

   ⚠️ **The bug that cost the most here, worth keeping:** the first implementation walked the
   `cubes` vector and found **zero** emitters on a world containing six glow blocks. **Phase 4.2b
   flipped authority to `ChunkVoxelStore`** — a normally-built chunk has an EMPTY `cubes` vector and
   all its voxels in the palette store. The walk now mirrors `rebuildCubeFaces`' own scan so the two
   cannot disagree about what a cube is. Store voxels are chunk-LOCAL and need `worldOrigin` added;
   subcubes and microcubes already report world positions.

   ⚠️ **Also restored:** `m_flamingVoxels` had been cleared but never populated since M0 deleted the
   flood that filled it — so continuous fire VFX had been silently dead. Same walk, so it is fixed
   here rather than logged as someone else's problem.

   ⚠️ **Self-inflicted, recorded because it nearly shipped:** excising the first draft with
   index-based text surgery also deleted the **microcube occupancy loop** from
   `buildSubMicroOccupancy`, turning three `FineFaceMerge` microcube tests red. Caught by running
   the suite, restored. **Do not edit C++ by computed string offsets.**

   *(original specification kept below)*

2. **Emissive materials register radiance — SPECIFIED 2026-08-30, and smaller than it looked.**
   The enumeration ALREADY EXISTS in the flood M0 deleted, and is recoverable from the diff: it
   walked cubes and subcubes, seeded on `md->emissive || md->emissiveStrength > 0.0f`, took hue from
   `md->physics.colorTint` (glow) or the per-voxel tint (flaming), and scaled 15 for `emissive`,
   `clamp(emissiveStrength*4, 2, 10)` for masked-emissive (the "enchanted log" crack-light), 15/9
   for flaming/smoldering. **U3.2 restores that walk and emits LIGHTS instead of flood seeds.**
   Keep the per-face self-shading term for the fixture's own surface.
   ⚠️ Depends on U3.1: without spatial selection, a room of glow voxels would instantly exhaust
   the upload budget.
   ⚠️ Also depends on **U5's bloom fix** to be worth anything visually — bloom is what makes an
   emissive fixture read as glowing, and it currently ships off.
3. **✅ DONE 2026-09-01 — vegetation reads the light SSBO.** *(closes D15 for grass + foliage)*

   `grass.frag` and `foliage.frag` now declare the light SSBO (binding 3), extend their std140 UBO
   prefix to reach `occupancyBox`, include `occupancy.glsl`, and run a point-light loop gated by
   **the same `phxLightVisibility` stone uses** — so a torch inside a house does not light the lawn
   outside. `grass.vert` gained a `vWorldPos` varying; foliage already had one.

   A blade and a leaf card have no meaningful normal (both are camera-facing cutouts, and the sun
   term deliberately avoids per-blade N·L because it makes the field sparkle), so both light as
   upward-facing diffuse receivers: **attenuation and occlusion shape the pool, not card facing.**
   The dead `vBlock` term — a constant 0 since M0 — is removed in both.

   **Gate met, measured on a night meadow** (timeOfDay 23, one `glow` voxel placed via the world
   API, no `/api/light` call): viewport mean luminance **5.108 → 48.563**, with **322,652 pixels
   (56.3%)** brightening and **302,208 of them un-saturated** — i.e. grass being lit, not the block
   itself being visible. Falloff into darkness at the edges. 91/91 in the lighting+grass filter, no
   Vulkan validation errors.

   ### 🔴 The bug this exposed: AN EMISSIVE VOXEL OCCLUDED ITS OWN LIGHT, COMPLETELY.

   First attempt lit nothing — measured 20,831 px in a tight box around the block, and inspection
   showed those blades were **silhouettes against the bright block, not lit grass.**

   `phxLightVisibility` stopped its march `1/9` short of the light. That was fine while every light
   sat in air. **U3.2 made emissive voxels lights, and an emissive voxel is SOLID with its light at
   the cell CENTRE** — so the march ended 0.5 u *inside* the emitter and every ray hit it. Fixed by
   stopping **half a voxel** short (`kSelfSkip = 0.5`), exactly the emitter's own half-extent, in
   both `occupancy.glsl` and its CPU mirror (which must stay identical).
   *Control:* the sealed-box gates are what bound this constant — a light closer than 0.5 u to a
   wall could shine through it. **`LightWallMatrix` + `LightBleed` + occupancy: 60/60 green after
   the change**, so it does not leak.
   **This defect was invisible until U3.2 existed**, and it would have silently halved every future
   emissive fixture.

   ⚠️ **NOT tested at runtime: foliage.** It compiles and is wired identically, but `LightingGates`
   has `flora.density: 0` — there are no trees in it, so no leaf card was ever drawn. Grass is
   verified; foliage is verified only by construction.

   ⚠️ **`far_terrain` and `far_tree_mesh`: DECIDED sun-only, not wired.** They render beyond ~900 u,
   where a light of radius ≤ 15 u subtends nothing; wiring the loop would cost per-fragment work
   across a huge screen area for no visible result. Recorded here rather than left blank, per D15's
   own "decide per pass" wording. **Revisit trigger:** if emissive lights ever gain radii on the
   order of a settlement.
*Gate:* placing a `glow` voxel with no accompanying point light lights the room, with occlusion; a
camp fire lights surrounding grass; a 100-fixture city has no permanently-dark fixture; the nearest
light to the player is always among those uploaded.

### ✅ U4 — DONE 2026-08-30, but NOT by the work this step described. The premise was wrong.

I wrote U4 as "collapse the three (later six) competing skylight mechanisms onto one". Reading the
code shows they were never competing sources. They are **ONE SOURCE WITH SEVEN TRANSPORTS**:

| transport | how it gets sky |
|---|---|
| static cube faces; subcube/microcube faces | `skyLightAt()` → `m_skyLight` |
| grass; foliage | `skyLightAt(x, y+1, z)` → `m_skyLight` (the air cell above) |
| kinematic voxels (doors, furniture, item props) | `sampleBakedLight()` → `m_skyLight` |
| characters | `sampleBakedLight()` at torso height → `m_skyLight` |
| CPU debris | `sampleBakedLight()` → `m_skyLight` |
| GPU particle debris | `m_lightSampler` → `sampleBakedLight()` → `m_skyLight` |

Every one reads the same per-cell array. The differing *offsets* (grass and foliage sample the air
cell above the surface; characters sample at torso height) are deliberate and correct for what each
lights — they are not drift.

**So U4 was satisfied by M3-REDESIGN.** Once the bake filled `m_skyLight` with traced visibility
instead of the M0 constant, all seven transports inherited it in one change. Confirmed at a point:
with baking on, `/api/world/baked_light` — the same `sampleBakedLight` call kinematics, characters
and debris use — reads **0** inside the sealed box and **15** on open ground.

**The lesson is the same one this document keeps recording:** the "six mechanisms" line came from
counting `vSkyLight` writers in the vertex shaders without following them back to their source. A
transport is not a model. Had I acted on the premise, I would have spent the step rewriting six
call sites that already agreed.

### (superseded) U4 — original specification, based on that wrong premise
*Files:* `static_voxel.vert`, `kinematic_voxel.vert`, `dynamic_voxel.vert`, `ChunkRenderManager`.
Collapse the three mechanisms (per-corner nibbles / push constant / instance field) onto whatever
M3-REDESIGN chooses. **Sequenced after M3-REDESIGN** — collapsing them before the replacement exists
would just be churn.
*Gate:* one code path supplies sky access to `voxel.frag`; kinematic and dynamic voxels agree with
static ones at the same world position.

### ✅ U5 — BLOOM DEFECT FOUND AND FIXED 2026-08-31 (missing read-after-write barrier).

*(Header corrected 2026-09-01. The section below was written before the defect reproduced. The root
cause was an absent RAW dependency in the blur ping-pong -- not fireflies -- and the measured fix is
recorded further down. SSAO, editor-vs-standalone grading and the exposure re-derivation remain.)*

### (superseded) U5 — PARTIAL 2026-08-30. Safeguard added; **gate NOT met, because the defect would not reproduce.**

**What was done:** `blur.frag`'s bright-pass had **no upper bound** on a tap, which is the exact
mechanism `PostProcessor.h` names for the blotching (one very bright pixel clears the threshold,
becomes a blob, and the half-res blur doubles its width). A per-tap firefly clamp now bounds each
tap to 8x the threshold, making bloom a function of bright AREA rather than peak VALUE — the Karis
insight, applied at the tap. It deliberately does not weaken the sun or a hearth, which are many
adjacent bright pixels.

**What was measured**, using the gate `PostProcessor.h` itself specifies — *where* the on-vs-off
difference lands, per region, not eyeballed — against an emissive `glow` fixture:

| world | clamp | cells carrying bloom | share in top 3 cells |
|---|---|---|---|
| LightingGates (flat, no vegetation) | ON | 97 / 192 | 10.2% |
| LightingGates | OFF | 99 / 192 | 11.5% |
| DenseForestPerf (trees, grass, sky) | ON | 89 / 192 | 9.0% |
| DenseForestPerf | OFF | 90 / 192 | 10.2% |

**The clamp made no measurable difference, and blotching did not appear in either world** — with or
without it. A spread across ~90 of 192 cells with under 11% in the top three is the signature of a
smooth glow; blotching would concentrate.

**So this is NOT a verified fix.** I tested a change against scenes that never exhibited the bug —
the same class of mistake as the dead controls earlier in this document, in a new costume. The
clamp stays because it is correct in principle and bounded in effect, but **bloom remains
default-OFF** and U5 is not closed.

### ✅ THE BLOTCHING REPRODUCED 2026-08-30 — found incidentally while gating D18

`screenshots/screenshot_20260830_220938_709.png`. **LightingGates, flat, daytime, Release, bloom
intensity 1.0 / threshold 1.0 / knee 0.5 / exposure 8.0, camera (45,55,45).** The grass field is
covered in discrete soft blobs — unmistakably "spots, not a smooth glow". The bloom-OFF capture from
the same session and pose (`..._220351_039.png`) shows clean grass, and the paired A/B in the D18
gate measured the world moving 3.131 mean / 25.3% of pixels between them. **This is the defect
`PostProcessor.h` records, on demand, at a known pose.**

**Why earlier U5 measurement missed it, and the lesson:** U5 ran on this same world and reported "did
not reproduce". The metric was a 16×12 grid of ~100×75 px cells against a ~15 px blur radius — it
measured how bloom was distributed across the frame, which is smooth, and was structurally incapable
of seeing blob-scale structure. **The instrument was blind to the defect it was pointed at, and
reported a null.** A null from an instrument whose resolution was never checked against the feature
size is not a null. That is the same class of error as a dead control.

**Also note the firefly clamp is ON (`clampMul = 8`) in this capture and the blobs are still there**
— so the clamp is not sufficient, which is consistent with U5's own "safeguard, not a demonstrated
fix" wording. The seed is the grass sub-pixel speckle named in `PostProcessor.h`, and the blobs are
grass-clump sized, not single-pixel.

**Next for U5, now that it reproduces:** vary one thing at a time from this exact pose — clamp
on/off, `radiusScale` (both push constants already exist for this), threshold — and measure with a
metric sized to the blob, not to the frame. The mip-pyramid hypothesis in `blur.frag`'s comment is
directly testable here.

### 🔴 U5 ROOT-CAUSE CANDIDATE 2026-08-31 — BLOOM IS NON-DETERMINISTIC. Missing read-after-write sync.

**Attempting that sweep is what found this.** Four successive harnesses could not produce a
reproducible number; the reason is not the harness.

**MEASURED — bloom output varies frame to frame on a completely static scene.** Same pose, wind
speed 0, day-night paused, nothing changing, sampled every 10 s over 90 s (viewport-only luminance):

| | frame-mean range over 90 s | spread | per-pixel `mean｜f−f0｜` |
|---|---|---|---|
| **bloom OFF** | 146.866 – 146.951 | **0.085** | 1.6 – 2.1 |
| **bloom ON** | 149.235 – 153.218 | **3.983 — 47× larger** | 6.2 – 7.9 |

Bloom OFF is rock stable, so the scene is NOT drifting. **Bloom ON is not a deterministic function
of its input** — identical input, different output every frame, oscillating rather than settling.

**LOCATED — `PostProcessor::renderBlur`, `PostProcessor.cpp:1483-1541`.** The chain ping-pongs 10
passes: iteration *i* writes `blurImages[outputIndex]` as a **color attachment**; iteration *i+1*
**samples that same image** in the fragment shader. Between them there is **no barrier of any kind**
— the only `insertImageMemoryBarrier` is *before* the loop (`:1472`), covering the initial blit.
`blurRenderPass` (`:1216-1222`) declares exactly ONE subpass dependency:

```cpp
dependency.srcSubpass   = VK_SUBPASS_EXTERNAL;   dependency.dstSubpass = 0;
dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
dependency.srcAccessMask = 0;                                    // <-- nothing made available
dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT; // <-- write-vs-write only
```

That orders **writes against writes**. The hazard here is a **read-after-write**: the next pass's
`VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT` / `VK_ACCESS_SHADER_READ_BIT` never appears in any
dependency, so the sampled data is neither guaranteed available nor visible. Compare
`createSceneRenderPass` (`:229-245`), which declares the correct **pair** including
`COLOR_ATTACHMENT_WRITE → FRAGMENT_SHADER / SHADER_READ`. The blur pass has one, in the wrong
direction.

**Why this was never caught:** standard Vulkan validation does not check synchronization. U1's "zero
validation errors" is true and says nothing about this class of bug — **synchronization validation is
a separate layer feature that has to be turned on.**

**Why this is a better candidate than fireflies:** it explains the blotching (an incoherent,
partially-updated blur buffer reads as discrete blobs rather than a smooth glow), it explains the
per-frame shimmer, and it explains why the firefly clamp measured **no difference** — the clamp
addresses peak values, and the defect is not about values.

**Gate as stated before the fix:** add the missing `COLOR_ATTACHMENT_WRITE →
FRAGMENT_SHADER/SHADER_READ` dependency, then re-run the 90 s determinism measurement unchanged.
**Pass = the bloom-ON spread collapses toward the bloom-OFF spread.**

### ✅ U5 GATE MET 2026-08-31 — the fix landed and the defect is gone, quantitatively and visually

**Fix:** `createBlurRenderPass` now declares the correct **pair** of subpass dependencies
(`PostProcessor.cpp:1216+`) — a WAR (`FRAGMENT_SHADER/SHADER_READ → COLOR_ATTACHMENT_OUTPUT/WRITE`)
and, the one that was missing, a **RAW** (`COLOR_ATTACHMENT_OUTPUT/WRITE → FRAGMENT_SHADER/
SHADER_READ`), both `BY_REGION`. Mirrors `createSceneRenderPass`. No shader change, so no `.spv`.

**Determinism gate — same script, same settings, same scene:**

| | bloom OFF spread | bloom ON spread | bloom ON per-px `mean｜f−f0｜` |
|---|---|---|---|
| **before** | 0.085 | **3.983** (47× the control) | 6.2 – 7.9 |
| **after** | 0.107 | **0.122** | 0.32 – 1.79 |

**The bloom-ON spread fell 3.983 → 0.122 (33×) and is now indistinguishable from the bloom-OFF
control (0.107).** Per-pixel instability fell into the same band as bloom-off. Bloom is a
deterministic function of its input again.

*Scene identity is evidenced, not assumed:* bloom-OFF frame means match across the two builds to
0.01 (146.866–146.951 before, 146.852–146.958 after), so both runs measured the same scene.

**Visual gate, matched pose** (camera verified at 45,55,45 yaw −135 pitch −30 via `GET /api/camera`,
bloom 1.0 / threshold 1.0 / knee 0.5 / clamp 8 / radius 1 in both):
* **before —** `screenshots/screenshot_20260830_220938_709.png`: grass covered in discrete blobs
* **after  —** `screenshots/screenshot_20260831_082430_444.png`: **grass completely clean.** The only
  bloom left is a small halo on the one genuinely bright object in frame — i.e. bloom doing its job.

**So the blotching was never a firefly problem.** It was ten unsynchronised ping-pong passes per
frame sampling a buffer whose writes were never made visible. That is why the firefly clamp measured
nothing, and why `PostProcessor.h`'s stars/airglow/speckle hypothesis — reasonable on its face —
never reproduced under a controlled A/B.

⚠️ **NOT yet done, and required before bloom's default flips:**
1. **Enable Vulkan synchronization validation and get a clean run.** This bug class is invisible to
   standard validation, which is exactly how it survived a "zero validation errors" report. Until
   that runs, "no other sync bugs" is unevidenced — and the same ping-pong shape may exist elsewhere
   (SSAO blur is the obvious place to look).
2. **One scene is not a verdict.** Verified on flat LightingGates only. Re-check on a vegetated /
   night scene where stars and airglow are present, since those were the original suspects.
3. **`m_bloomIntensity` stays 0.0 (default OFF)** and the "BLOOM IS BROKEN" banners in
   `PostProcessor.h` / the API warning are **left in place deliberately** — flipping them is a
   separate decision that belongs with the U5 look pass (SSAO, editor-vs-standalone grading,
   exposure re-derivation), not with this sync fix.
4. The bloom contribution is now much smaller (frame mean 147.74 vs ~150.2 before) — the old
   brightness was partly garbage read from unsynchronised buffers. **Bloom will need re-tuning
   against correct data**, and any intensity/threshold values chosen before today are meaningless.

### ⚠️ Instrument notes from four failed harnesses — read before writing a fifth

1. **v1, unmasked:** four rows were the *same* shipped config and read peaks 159 / 81 / 260 / 383.
   Null floor 60.8.
2. **v2, "frozen" scene** (wind 0, day-night paused): floor got *worse*, 115.3. Freezing the
   declared animation sources did not still the frame.
3. **Diagnosis:** the instability is **not the grass** (mean std 0.506, 96.5% of pixels stable). It
   is the **sky/horizon** (std 2.1–3.2, p99 ≈ 55, max ≈ 96) — sparse, full-contrast, **bistable**
   pixels where the terrain silhouette flips against sky. **Frame averaging does not fix it**: the
   floor plateaued at ~25 instead of falling as 1/√N, because bistable flipping is not zero-mean.
4. **v3, noise-masked:** floor collapsed to **0.25** (from 60.8 / 115.3) — the mask works. But
   bloom-ON runs of one config still gave blobs 77/195/267, cover 74%/57%/55%. **Masking cannot fix
   this: bloom is NON-LOCAL**, so excluding a flickering pixel from the metric does not exclude its
   halo, which lands on the stable grass.
5. **v4, camera pitched down** to remove sky from the frame entirely: cover and meanD still moved
   **monotonically** across identical repeats — which is what pointed at the renderer rather than the
   scene, and led to the determinism measurement above.

**Standing lesson:** a gate on one metric is not a gate. v3's reproducibility check tested only
`peaks` (24%, just under the 25% bar) and passed an instrument whose `blobs` were 3.5× unstable.
Gate every metric a conclusion would rest on.

**To close U5, what was needed and is now available:** the scene, pose and settings that showed the
blotching — recorded above. Original wording kept:
`DenseForestPerf` was chosen because `PostProcessor.h` names stars, airglow and grass speckle as
the suspects and it has all three — that was a reasoned guess, and it was not enough. Candidates
still untried: a night pose (where stars dominate a dark frame and the bright-pass threshold is
relatively far lower), a very high bloom intensity, or a scene with strong speculars.

**Also in U5, unchanged and still open:** SSAO is disabled (grazing-angle band), the editor viewport
is ungraded while standalone is graded, and `m_exposure` (8.0) has never been re-derived.

**NEW 2026-08-30 — U5 also owns D18: the game HUD is inside the graded image.** `UISystem` draws
`default_hud.json` (health, hotbar) into the offscreen scene image *before* `compositeToGrade`, so
the HUD is bloomed and tonemapped as though it were world geometry. That is the user-reported "UI
elements get bloom around them", and it is the same defect class as the editor-viewport grading
mismatch already listed above — **where the grade boundary sits is not decided consistently**.
Full evidence, the proposed fix, the three unverified points and the gate are in **D18**. U5 cannot
close until the grade boundary has a single written answer, and **M4 must not re-derive
`m_exposure` before it does**, or the HUD restyles silently.

⚠️ **Self-inflicted, recorded so it is not repeated:** a PowerShell `-replace` rewrite of
`blur.frag` mangled its UTF-8 (em-dashes became `â€"`). Repaired via a Python rewrite with explicit
encoding. **Do not edit source files with PowerShell text replacement** — it does not round-trip
UTF-8 here.

### (superseded) U5 — original specification
**D17 was my error — water does not self-tonemap.** There is exactly one grade, in
`post_process.frag`, applied after compositing. What IS wrong here:
1. **Bloom is broken and shipped off** (`grade.bloom == 0`; spots/blotches, confirmed 2026-08-15).
   Emissive materials depend on bloom for their glow, so **fixing bloom is part of making emissive
   mean anything** — it belongs with U3.2, not filed as an unrelated post-process bug.
   *SPECIFIED 2026-08-30* — `PostProcessor.h` already records the hypothesis and the fix to try:
   isolated very bright pixels (the sky pass's per-pixel-hash stars and airglow, plus the known
   grass/character sub-pixel speckle) clear the bright-pass threshold and each becomes a blob;
   the half-res blur doubles every blob's width, so they read as spots — classic bloom fireflies.
   *Fix to try:* clamp each bright-pass tap so one pixel cannot dominate the kernel, and/or exclude
   the star/airglow term from what seeds bloom. `BlurPush { horizontal, threshold, knee }` already
   carries a threshold and knee, so the plumbing exists.
   *Gate (from that note, and it is the right one):* measure WHERE the on-vs-off difference lands,
   per region — do not eyeball screenshots.
2. **SSAO is disabled** (grazing-angle band).
3. **The editor viewport is ungraded** — it shows the raw offscreen texture, so authoring happens on
   a different response curve than a standalone game ships. Either grade the viewport or record the
   discrepancy where authors will see it.
4. Re-derive `m_exposure` (8.0, never re-derived) — M4/U7 owns this.
*Gate:* bloom on with a threshold and no blotching, verified against an emissive fixture; a stated
decision on editor-vs-standalone grading.

### ✅ U6 — DONE 2026-08-30. Every gap now has a written decision.

Caster coverage, established by reading `renderShadowPass`'s per-cascade guards (not by grepping
for pipelines, which is how I got this wrong twice):

| caster | near 40 u | mid 420 u | far 1600 u |
|---|---|---|---|
| chunk voxels | ✔ (legacy loop) | ✔ (GPU multidraw) | ✘ **deliberate** |
| characters / kinematic / dynamic | ✔ | ✔ | ✘ |
| foliage cards | ✘ | ✔ | ✘ |
| grass blades | ✘ (`s_castShadows=false`) | ✘ | ✘ |
| far terrain tiles | ✘ | ✘ | ✔ |
| tree-LOD meshes | ✘ | ✘ | ✔ |
| far-tree impostor cards | ✘ | ✘ | ✘ |
| CPU debris · VFX particles | ✘ | ✘ | ✘ |

**DECISIONS — no blank cells:**

| gap | decision | reason |
|---|---|---|
| grass casts nothing | **ACCEPT (off)** | Measured: toggling it changed 40.1% of the view into unstructured blobs. A blade is ~0.05–0.1 u against a 0.125 u mid texel — sub-texel, so the map captures clump-scale noise, not blades. Grass already RECEIVES near+mid and self-shadows via its `vGrad` AO gradient. **Revisit trigger:** near-cascade-only casting combined with sub-texel caster culling (already proposed as item 6 in `NearShadowCascade.md`). |
| far cascade takes no chunk casters | **ACCEPT** | Deliberate and measured: far-terrain tiles underlap the chunks, so tile depth already approximates their contribution, while drawing ~900 chunks every 4th frame spiked the pass to ~20 ms with visible judder. |
| characters/kinematics don't cast into far | **ACCEPT** | At >420 u a character is far below the far map's ~0.9 u texel. |
| far-tree impostor cards cast nothing | **ACCEPT** | They are billboards at ≥900 u; a card's shadow would be card-shaped. Tree-LOD *meshes* do cast into far and cover the same band with real geometry. |
| CPU debris casts nothing | **ACCEPT** | Transient, numerous, and small enough to be sub-texel in every cascade. |
| VFX particles cast nothing | **ACCEPT** | Additive emissive particles — they are light sources, and a light source should not occlude. |
| `NearShadowCascade.md` far row listed chunks | **CLOSED — doc corrected** | The row is now accurate and carries the measured reason for the exclusion. |

⚠️ **Process note, because I got this wrong twice in one sitting.** I first recorded "the far cascade
draws no chunk casters" (right), then "corrected" myself to the opposite after seeing
`m_farShadowChunksDrawn` being assigned (wrong — that write sits inside a mid-gated block), and only
settled it by reading the actual `cascade != kCascadeFar` guard and its comment. **A counter being
written is not proof that geometry is drawn.** Read the guard, not the telemetry.

### (superseded) U6 — original specification
Grass casts nothing; far-tree cards, CPU debris and VFX cast nothing; the far cascade takes only far
terrain and tree LOD. Each gets a written decision — close or accept, with the reason.
*Gate:* a table in this document with a decision per row and no blank cells.

### ⚠️ U7 — SKY HALF DONE 2026-09-01; BLOCK HALF STILL GATED ON U3.2.

*(Header corrected 2026-09-01: the 2026-08-30 revision below says the skylight field "must now be
KEPT" because the bake wrote to it. The bake is deleted and that field is deleted with it. What
remains of U7 is the BLOCK-light storage, which U3.2 still gates.)*

### (superseded) U7 — SCOPE REVISED 2026-08-30. M3-REDESIGN invalidated half of it.

U7 (and M4) said: delete `m_skyLight`, `m_blockR/G/B`, the `lightAt`/`bakedLightAt` accessors and
the three per-instance light words, because they were a constant nobody needed.

**`m_skyLight` must now be KEPT.** It is the destination of the M3-REDESIGN bake and the single
source all seven transports read (U4). Deleting it would delete traced sky visibility. The
per-corner nibbles in `InstanceData` must be kept for the same reason — they are how the baked value
reaches `voxel.frag`, and the per-corner interpolation is what smooths it across cells.

⚠️ **This is a DEFERRAL, not a reversal — see the contradiction note at the head of M3-REDESIGN
(raised 2026-09-01).** Stated flatly like this, the paragraph above reads as though the directive
changed its mind about deleting per-cell light storage. It did not. `m_skyLight` survives only
because the bake — an explicitly **temporary scaffold** — depends on it. **M4/U7 are blocked, not
cancelled**, and they unblock the moment the bake has an exit (M5 being the one that resolves the
contradiction rather than managing it). Do not read this as "the field is permanent now".

### ✅ U7 STAGE 1 DONE 2026-09-01 — InstanceData 24 → 16 bytes

U3.2 chose point lights, so block light is confirmed dead and this deletion is unblocked.
`InstanceData::light2` / `light3` are removed from **both** hand-synced struct copies (`Types.h` and
`vulkan/VulkanDevice.h`), from both vertex attribute tables (7 → 5 entries), and from every writer
(`ChunkRenderManager` ×8, `LodChunkMesh` ×2). Tint moves location 7 → 5.

`packedData 4 + textureIndex 2 + reserved 2 + light 4 + tint 4` = **16 bytes, was 24 — a third off
every chunk face instance.** That is M4's previously-unmet *"instance size shrinks measurably"*.

The location shift is contained: three shaders bind this buffer and only `static_voxel.vert` reads
past location 4. `shadow.vert` stops at 4 (it reads `inLight` purely for the fine-merge extents in
bits 16-31, which is exactly why that field could not be deleted) and `debug_voxel.vert` at 3.
`static_voxel.vert` also stops decoding the dead per-corner skylight nibbles — sky is traced per
fragment and `m_skyLight` is gone, so they were a uniform 15.

Removed `DISABLED_InteriorWallFaceCornersAtTheFloorAverageInSolidZeros`, which read those words to
measure the wall-base band — a property of the flood, which no longer exists. Replaced with a
comment rather than deleted silently.

**232/233** in the affected suites (the red is the pre-existing D19). Verified at runtime on the
engine's own generator: a `hall_house` (`placed: 13822`) renders with textures and tint intact —
which is what proves the tint relocation — dark interior through the doorway, 182 fps.

### ✅ U7 STAGE 2 DONE 2026-09-01 — block light is gone from the engine entirely

Removed: the `vBlockColor` varying from `static_voxel` / `dynamic_voxel` / `kinematic_voxel` and from
`voxel.frag`; the `vBlock` varying from `grass` / `grass_shadow` / `foliage` / `foliage_kinematic`
and both their fragment shaders; and the `m_blockR/G/B` CPU arrays with `blockLightAt` behind them.
Locations are left non-contiguous rather than renumbered — GLSL does not require contiguity, and
renumbering would have touched every consumer for no gain.

**Debug view 4 was the block-light view.** Kept as an explicit black rather than removed, so the
mode numbering and its clamp stay stable for existing tooling; emissive voxels are ordinary point
lights now, so **mode 5** is where they show.

**The cross-chunk border snapshot is now two constants.** It is retained because `ChunkManager`
re-meshes neighbours when it CHANGES; with constants it never reports a change, which is correct —
there is no cross-chunk baked light left to ripple.

⚠️ **One real thing was lost, and it is logged rather than buried.** `voxel.frag` used
`vBlockColor` to tint an emissive block's SELF-illumination by its own hue, so a `glow_blue` block
read blue. That varying has been a constant 0 since M0, so the `m > 0.05` test already failed and it
**already fell back to white** — the feature has been dead for the whole rebuild. It now reads
`vTint` instead: behaviour-identical for untinted voxels, strictly better for tinted ones.
**NOT restored:** material-level emissive hue. A `glow_blue` block with no per-voxel tint still
self-illuminates white, because the material's `colorTint` is not carried into the shader. U3.2
already reads exactly that CPU-side for the light colour, so carrying it through is the fix.

*Verified:* 235/236 in the affected suites (the red is D19), and the U3.3 night-meadow result is
unchanged — mean luminance 5.108 → 48.578 with 302,194 un-saturated lit pixels, against 48.563 /
302,208 before stage 2. A 14-pixel difference is noise.

⚠️ **(superseded) STAGE 2 STILL OPEN.** `vBlockColor` is still declared by `static_voxel`, `dynamic_voxel` and
`kinematic_voxel`, and `voxel.frag`'s debug view 4 still reads it; the `m_blockR/G/B` CPU arrays are
still allocated and uploaded. All carry constant zero. Split deliberately so the vertex-layout
change stays independently testable.

⚠️ **Process note, second occurrence in one session:** `git add -u engine editor shaders tests docs`
swept nine unrelated CityForge/test/doc files into this commit. Amended out, their working-tree
changes preserved. **Stage lighting files by explicit path; never `-u` a whole directory in a tree
that carries another workstream.**

**Still deletable, and this is what U7 now means:**
* `m_blockR` / `m_blockG` / `m_blockB` — pinned to 0 since M0, with no writer. Three N³ byte arrays
  per chunk paying memory and upload bandwidth for zeros.
* The `inLight2` / `inLight3` per-instance words that carry them, and their packing.
* `vBlockColor` and its consumers in `voxel.frag` / `character.frag` / `grass.frag` / `foliage.frag`.
⚠️ **Sequencing: do this AFTER U3.2 decides.** U3.2 makes emissive voxels real emitters. If it does
that with point lights (the plan's intent), block light stays dead and this deletion is right. If it
ever wants a baked block-light field instead, this storage is exactly what it would use — deleting
it first would mean rebuilding it. **Do not delete until U3.2 has chosen.**
*Gate (unchanged in spirit):* no reader remains for whatever is deleted; instance size shrinks
measurably; a fixed-pose A/B is identical.

### (superseded) U7 — original specification
*Files:* `ChunkRenderManager.{h,cpp}`, `static_voxel.vert`, `InstanceData`, `RenderCoordinator`.
Remove `m_skyLight` / `m_blockR/G/B`, the `lightAt`/`bakedLightAt` accessors, the three per-instance
light words and their packing. Every chunk currently pays that memory and upload bandwidth for data
pinned to a constant.
*Gate:* no reader remains; instance size shrinks measurably; a fixed-pose A/B is identical.

### Ordering and why
**U3.1 (spatial light selection) first** — it is self-contained, fixes a live user-visible bug, and
needs nothing else. Then **U2** (makes M2 true as reported), then **U1** (uniformity), then
**M3-REDESIGN**, then **U4**, **U5**, **U7**, with **U6** as documentation work that can happen any
time. U3.2/U3.3 land with or after M5, since a per-emitter forward path would recreate the cap.

All of the below verified in the source on 2026-08-28.

**1. Sun and moon — dynamic, per frame, geometry-accurate.** Three cascades (near ~40 u, mid
~420 u, far ~1600 u) rendered from the light's viewpoint. `RenderCoordinator::renderShadowPass`
(`RenderCoordinator.cpp:2322`) rasterizes the chunk's own instance buffer, which carries cube,
subcube and microcube faces — so a 1-micro ledge casts correctly. This is the part that already
works the way the whole engine should.

**2. Per-cell stored light — the Minecraft-style flood.** Computed inside the chunk mesher
(`ChunkRenderManager::rebuildCubeFaces`) on every re-mesh, not per frame. Two channels, both at
**cube resolution**, both stored as `uint8` per cell (`m_skyLight`, `m_blockR/G/B` — 4 bytes ×
32³ = **128 KB per chunk**):

* **skylight** — 4 bits, seeded 15 down open columns, 6-connected BFS at −1 per step.
* **block light** — 4 bits × RGB, same BFS, seeded from emissive materials.

The decay is *linear per cell step*. There is no inverse-square, no direction, no bounce, no
dependence on the receiving surface's normal. It is a distance field with a falloff, not light
transport. `EngineAdvancesResearch.md` §4 calls it what it is: "Minecraft-style flood".

**3. Forward point and spot lights — per frame, movable, no visibility test.** `voxel.frag`'s point
loop gates on distance and `N·L` and nothing else. `LightingPipeline.md:200`: *"Shadow multiplies
only the sun term. Ambient, block light, point/spot lights and emission are all unshadowed."*
Hard cap of **32 point / 16 spot** (`Light.h:10`); `StructureForge` reports fixtures as UNLIT once
a city exceeds it.

### The failure, stated precisely

The stored flood is doing two jobs with one data structure:

* **Job A — "how enclosed is this spot."** An indirect/ambient approximation. Coarse is defensible;
  real physically-based renderers also store indirect coarsely (lightmaps, probe grids).
* **Job B — "does light from this source reach this surface."** Direct visibility. Coarse is **not**
  acceptable, and this is what leaks.

Job B is the defect. Because the field is cube-resolution and walls are sub-voxel, a mixed cell
must be rounded to solid-or-air; `m_lightOpaque` is that rounding and
`kLightOpaqueFill = 243` of 729 is its threshold. The generator's default timber wall is **2 micro
= 162**, under the bar. Measured (`LightBleedTest`, 2026-08-28): **12 of 15** light levels crossed
a 2-micro wall.

Meanwhile the exact data already exists and is never consulted by lighting: `m_subOcc` /
`m_microOcc` are leaf-accurate subcube/microcube occupancy, rebuilt every mesh "straight from the
voxel hierarchy (the source of truth)" — and used **only for hidden-face culling**.

### What the flood is load-bearing for today

Removing it naively breaks these, so each needs a replacement before it goes:

1. Interiors being dark at all (skylight gates the ambient term and the sun).
2. Every emissive chunk voxel's glow — `glow`, fires, lamps built as chunk voxels.
3. The exposure calibration (`m_exposure = 8.0`) was derived with it in place.
4. Eleven shaders consume the shared `lighting.glsl` model.

---

## Design keys (`docs/FeatureDesignKeys.md` — answered up front)

**Procedural-generation pipeline?** No — this is a render-system change. It must not require the
generator to know anything about lighting. The generator's only obligation stays what it already
is: emit correct geometry and register emitters.

**Exposed over an API?** Yes, and it must be, or none of this is testable headlessly. Required:
per-light enable/disable and move (exists: `LightManager`), a visibility query
(`is point A lit by light L`) for deterministic tests, and per-stage cost counters in
`get_render_stats`. The debug surface (`POST /api/debug/tonemap`, `/api/debug/sky`) gains toggles
per lighting stage so A/B is one request, not a rebuild.

**Visually tested how?** Sealed-box rig (below) for correctness, plus fixed-pose A/B captures
measured with `tools/lighting_stats.py` for look. **Screenshots are never the evidence on their
own** — every claim is a number with a control.

**Small, simplified test world?** Yes: the sealed-box rig is one chunk, boxes of one variable each,
built through `MicroCanvas` so the voxel resolutions are the engine's own choice and not a
fixture's.

**⚠️ Chunks must not be visible.** Appearance must stay a pure function of world position and
persistent world state. Any per-chunk structure introduced here (occupancy tiles, probe pages) may
bound **cost** only, never looks. Every stage carries a chunked-vs-whole-region equality test in the
style of `FloraMarginTest`.

---

## The instrument, before any change

**M0 — the sealed-box rig.** Partly built already
(`tests/graphics/LightWallMatrixTest.cpp`, `LightBleedTest.cpp`).

Sealed boxes — floor, four walls, roof, **no openings** — so any light outside is a defect by
definition. Walls painted into a real `MicroCanvas` at thicknesses from
`StructureRealizer::thicknessMicro()` on real `structure_styles.json` values, with voxel
resolutions chosen by `MicroCanvas::exportVoxels()`. Verified engine behaviour:

| style thickness | micro | emitted as |
|---|---|---|
| stone_keep 3.000 | 9 | `Cube` |
| stone_manor 0.667 | 6 | `Subcube` |
| default 0.333 | 3 | `Subcube` |
| timber_cottage 0.222 | 2 | `Microcube` |
| thinnest 0.111 | 1 | `Microcube` |

**M0 must be extended to cover every light type**, which it does not yet:

*light type* (sun · sky · stored flood · forward point · item-prop light) × *wall resolution*
(cube · subcube · microcube) × *occluder kind* (chunk voxels · kinematic/item geometry) ×
*source position* (centre · against a wall · in a corner · outside as positive control).

One number per cell: did light escape. **Baseline the current engine first** — the table of what
leaks today is the contract every later stage is measured against.

---

## Stages

⚠️ **Numbering.** An earlier draft of this section numbered the stages M1 = light visibility,
M2 = sky, M3 = retire flood, M4 = bounce — i.e. with no M0 and no separate occupancy stage. Every
RESULT section below, all the code comments, and the commit history use the numbering that was
actually approved and built: **M0 delete · M1 occupancy · M2 light visibility · M3 sky · M4 retire
transport · M5 bounce.** That is the numbering used here. The old scheme is dead; if you find a
reference to "M3 — retire the flood", it means **M4**.

Each stage is independently shippable, leaves the engine runnable, and must pass its gate — *and its
cost measurement* — before the next begins.

### M0 — Delete the flood ✅ DONE
### M1 — Sub-voxel occupancy on the GPU ✅ DONE
### M2 — Direct light visibility ✅ DONE *(this is the originally reported bug)*
See the RESULT sections below for what each measured, and what each did NOT verify.

### M3 — Sky as an emitter ✅ DONE 2026-09-01, as originally specified
Traced per fragment against real geometry, **default ON**, no stored per-cell field. The 24.6 ms
that once retired it was measured at 9 rays / reach 24 / 512 cells; at the 5 rays / reach 16 the
bake itself shipped at, plus a normal-ray gate, the sky term costs **+2.68 ms** (Static Geometry
2.997 ms vs a 0.318 ms control). M3-REDESIGN's bake and its per-cell field are both deleted.

### M4 — Retire the transport ⚠️ SKY HALF DONE 2026-09-01; block half + exposure remain

**`m_skyLight` is DELETED.** With the bake gone it had no writer and was a constant 15, so removing
the N³ array is byte-identical by construction: `skyLightAt()` returns uniform open sky, the vertex
path packs the same nibbles it packed before, and `voxel.frag`'s `vSkyLight *` multiply — a
documented no-op against a constant 1.0 — is dropped. Enclosure now comes from **one** place, the
per-fragment trace.

*Verified:* engine-generated `hall_house` (`placed: 13822`), `sky_probe` along y=18 z=1 reads
`0.92 0.92 0.79 | 0.00 ×5 | 0.71 0.79 0.92` — **unchanged from before the deletion**, which is
exactly the "fixed-pose A/B is identical" this gate asks for. Frame renders at 298 fps with the
generated doorway visibly dark. Lighting suite 63/63; the only red in the wider filter is D19.

⚠️ **The gate said "instance size shrinks measurably". IT DID NOT, and here is why.** `inLight` is
**dual-purpose**: bits 0–15 are the sky nibbles, **bits 16–31 carry the greedy-merge extents**
(`fineSizeU/V`, `static_voxel.vert:102-103`). The field cannot be removed — only the light half of
it freed. The 8 bytes that *would* shrink `InstanceData` are `inLight2`/`inLight3`, which carry
**block** light, and U7 explicitly sequences those behind **U3.2** ("do not delete until U3.2 has
chosen"). So the storage win is real but still gated, and claiming M4 complete here would be false.

**Remaining in M4/U7:**
* `m_blockR/G/B` + `inLight2`/`inLight3` + `vBlockColor` — the 8-byte shrink. **Gated on U3.2.**
* The other five skylight transports (`kinematic_voxel` `pc.bakedLight.x`, `dynamic_voxel`
  `inDebrisLight`, `character/grass/foliage` `vSky`) still feed a `vSkyLight` no one reads for
  chunks. They are harmless constants now, but they are dead weight and belong in this deletion.
### ✅ M4 — `m_exposure` RE-DERIVED 2026-09-01 (measured; default NOT changed)

8.0 was calibrated against two things that no longer exist: the per-cell flood (deleted by M0) and a
transposed AgX curve (corrected in `20341333`). Re-derived on the engine's own generator —
`hall_house` on grass, noon, sun fixed, one pose giving sunlit ground, a lit roof, a shadowed wall
and sky in a single frame.

**First attempt could not decide it, and that is itself a result.** A clipped/crushed histogram over
the viewport showed **0.000% clipped at every exposure from 2 to 16** — AgX is holding the highlights,
so the tone curve is not the constraint — while "crushed" fell monotonically with exposure. That
metric only says *brighter is better*, which is not a calibration; most of those crushed pixels are
the shadowed wall and dark interior, which SHOULD be dark.

**Re-measured against reference surfaces**, which is what exposure actually converts:

| exposure | sunlit grass | sunlit roof | shadowed wall | sky |
|---|---|---|---|---|
| 3.0 | 102.1 | 17.1 | 6.8 | 88.5 |
| 4.0 | 114.9 | 23.2 | 10.6 | 101.2 |
| **5.0** | **124.9** | 28.7 | 14.5 | 111.3 |
| 6.0 | 133.1 | 33.6 | 18.1 | 119.6 |
| **8.0 (current)** | **146.0** | 42.2 | 24.6 | 132.8 |
| 11.0 | 160.0 | 52.7 | 32.9 | 147.3 |

Mid-grey on an 8-bit display is ~118. Grass has a real albedo of roughly 0.25, so a sunlit lawn
should sit **just above** mid-grey — which is **exposure ≈ 5.0** (124.9). **At the shipped 8.0 it
reads 146**, about a third of a stop hot: sunlit diffuse is being pushed into the upper mid-tones and
everything shadowed is lifted with it.

⚠️ **The default is NOT changed.** This is a whole-game look decision, not a correctness fix —
nothing clips at either value — and this plan's standing rule is to report measurements and let the
user judge. Before/after captures at the same pose were handed over for that call.
⚠️ **What this did NOT test:** one scene, one time of day, one biome. Night, dawn/dusk, snow and
desert are untested, and exposure interacts with everything today's work changed (traced sky
replacing the flood, emissive voxels as lights). A single noon lawn is a calibration reference, not
a proof across the day cycle.

### M4 — original specification
Delete the per-corner vertex light words and their `InstanceData` fields if M2/M3 do not need them.
`vSkyLight` is still multiplied into the traced sky term ON PURPOSE so this stage can A/B before
deleting. Re-derive `m_exposure` (still 8.0, calibrated against both the deleted flood *and* a
since-corrected AgX curve — never re-derived).
**Gate:** no remaining reader of `m_skyLight`/`m_blockR/G/B` or the vertex words; full-scene A/B
against the M3 captures at fixed poses with `lighting_stats.py --compare`; all eleven
`lighting.glsl` consumers re-verified.

### M5 — Indirect bounce — NOT STARTED
Radiance cascades on the M1 structure (`EngineAdvancesResearch.md` §4; parking condition **expired**
2026-08-11). Removes the 32-light cap as a side effect — a city already exceeds it and logs fixtures
as UNLIT.
**Gate:** to be defined once D1 below has real numbers. Deliberately not scoped further.

---

## ✅ D0 — FIXED-STEP MARCHING WAS THE WRONG STRUCTURE. Found AND FIXED 2026-08-30.

**RESOLVED by replacing both marches with an Amanatides & Woo DDA in micro space** (`ddaHitsSolid`
in `VoxelLightOccupancy.cpp`, `phxDdaHitsSolid` in `voxel.frag`). A DDA visits every micro cell the
segment crosses, in order, so correctness no longer depends on feature thickness at all — and cost
becomes cells-actually-crossed rather than a fixed sample count.

| wall | 9 micro | 6 micro | 3 micro | 2 micro | 1 micro |
|---|---|---|---|---|---|
| sealed room sees sky, fixed-step | 0 | 0 | 0 | 0 | **0.536 LEAK** |
| sealed room sees sky, **DDA** | 0 | 0 | 0 | 0 | **0** |

Doorway falloff preserved at every thickness (near-door 0.134 vs far 0.000). **28/28 unit tests**,
M2 gate still green at all five thicknesses including corners with controls firing. Live: 74/74
chunks, 16,832 mixed cubes, **zero Vulkan validation errors**, open ground still reads 1.0.

Two obsolete assertions of mine were updated, not weakened: `LightVisibility::stepLen` was deleted
(a DDA has no step length) and `cappedOut` now means only "ran out of cell budget", which the
distant-wall case no longer does because the DDA reaches the wall directly.

The original finding is kept below because the REASON matters more than the fix.

---

### The finding (kept for the reasoning)

Found by finally running the gates the plan specified, on the grounded `LightWallMatrix` rigs
(sealed boxes at real `structure_styles.json` thicknesses, resolutions chosen by
`MicroCanvas::exportVoxels`). Result at five wall thicknesses:

| wall | 9 micro | 6 micro | 3 micro | 2 micro | **1 micro** |
|---|---|---|---|---|---|
| sealed room sees sky | 0 | 0 | 0 | 0 | **0.536 — LEAKS** |

The occupancy is not at fault: probes confirm the 1-micro shell is present (16,856 microcubes), and
the escaping rays are exactly the four 30°-tilt ones (3.46/6.46 = 0.536). The roof sits ~5.4 u from
the probe — **beyond the 3 u fine band** — so the march has already coarsened to a 1/3 step, which
advances 0.289 u vertically per sample against a 0.111 u roof and steps straight over it.

**This cannot be fixed by tuning.** A fixed-step march is only safe when the step is smaller than
the thinnest feature; the thinnest feature is 1/9 u, so a safe step is 1/18 u, and covering the 24 u
sky reach then costs ~432 samples per ray — ×9 rays per fragment. The two-rate compromise that made
it affordable is precisely what makes it blind to the finest geometry the engine can represent, which
is the opposite of this rebuild's whole premise (`docs/FeatureDesignKeys.md`: detail is
unconditional, never behind a tier).

**The fix is a DDA over the occupancy** — visit every cell the ray crosses, so feature thickness
stops mattering, and use the layout's own solid/mixed bits to skip at cube granularity and descend
to micro only inside mixed cubes. That is both exact and cheaper than the current march for typical
rays, and it is what `EngineAdvancesResearch.md` §4 anticipated ("a voxel grid gives free
ray-marching structure"). Both `phxLightVisibility`/`phxSkyVisibility` and their CPU mirrors change.

**The failing gate is left RED on purpose.** The M0 parking note in `LightWallMatrixTest.cpp` says:
"If a replacement cannot satisfy one of these, that is a finding about the replacement, not a reason
to weaken the test." Weakening it to 2-micro would have hidden this entirely.

**Gate:** `M3_SealedBoxSeesNoSkyAndADoorwayAdmitsItWithFalloff` green at ALL five thicknesses
including 1 micro, with the M2 gate still green and the doorway falloff preserved. ✅ MET.

### Why this was missed until now — the process lesson

This bug existed in M2 *and* M3 and survived both "gates" because I gated them on rigs I built
ad hoc in a populated project instead of the rigs this plan named. My rigs used **full cubes**; the
engine's own rigs sweep **9/6/3/2/1 micro**, and only the 1-micro case exposes it. The instrument
was sitting in the repo, parked by me at M0 with a note saying to re-enable it at exactly these two
milestones. Following the plan was not bureaucracy here: it was the difference between shipping
sub-voxel lighting and shipping lighting that cannot see the finest geometry the engine supports.

---

## DEBT — carried out of M0–M3, and gated like any other work

These are not footnotes. Each was found or left open by measurement during M0–M3, each has a gate,
and **D1 blocks M5 being scoped at all**. Written here rather than buried in the result prose,
because a concern that only exists in a caveat paragraph does not get done.

**D1 — ✅ DONE 2026-08-30. And it invalidated M3 as implemented.**

Release build, `GpuProfiler` scopes, pipeline statistics **OFF**, fixed pose (70,60,40 yaw −120
pitch −18), on a town the **engine's own generator** built (`POST /api/settlement/build`, medieval
town, seed 7, density 1.4 — 14,356 mixed cubes, 1.93 MB pool). Four combinations, interleaved twice,
median of ~24 frames each. Draw calls (12) and vertex count (202,832) identical in every sample, so
the scene is provably drawn the same way in all four.

| scope (median ms) | off/off | trace only | **sky only** | both |
|---|---|---|---|---|
| Static Geometry | 0.142 | 0.141 | **24.604** | 24.620 |
| Scene Pass | 1.271 | 1.292 | **26.262** | 26.241 |
| Shadow Pass | 2.246 | 1.818 | 3.205 | 2.911 |
| **FPS** | **275** | **287** | **35** | **34** |

* **M2 (point/spot visibility) is effectively FREE**: −0.4 ms total, inside noise. The
  `dot(N, ldir) > 0` and `spotFactor > 0` gates mean almost no marches actually run, and those that
  do terminate early. The originally reported bug is fixed at no measurable cost.
* **M3 (sky) costs ~24.5 ms/frame** — **173× the entire baseline static-geometry pass**, and it
  takes the frame from 275 fps to 35. Nine rays × a DDA of up to ~216 micro cells, per fragment,
  every frame. It is CORRECT (all gates pass) and completely unshippable in this form.

**Action taken:** traced sky is now **DEFAULT OFF** (`VulkanDevice::m_skyTracing = false`). It stays
switchable at `?sky=1` for measurement. Shipping a 275→35 fps regression by default would be wrong,
and silently leaving it on while calling M3 "done" would be worse.

⚠️ **This is not a worst case.** The pose used has little chunk geometry on screen (Static Geometry
0.142 ms baseline). A pose filling the screen with chunk pixels will cost more, not less.

**Method note:** the FIRST run of this harness reported the scene pass *88× cheaper with tracing on*
— an impossible result caused by sampling frames where the scene was not drawn. It was caught by
adding draw-call/vertex evidence per sample and interleaving repeated measurements, and would
otherwise have been reported as a spectacular and entirely fictional win.

---

## ✅ M3-REDESIGN — DONE 2026-08-30. Option 1 (bake) implemented and gated.

> ### 🔴 UNRESOLVED CONTRADICTION WITH THE DIRECTIVE — raised by the user 2026-09-01
>
> **"I thought we were getting rid of the bake."** Correct, and this document had stopped saying so.
>
> The directive's objection to the flood was **architectural, not just numerical**: *"it is not an
> opacity bug, it is a **storage resolution** bug ... any scheme storing one light value per cube
> cell reproduces it."* M3 was specified as sky **traced** against real geometry; M4/U7 were to
> **delete** `m_skyLight` and the per-corner vertex words.
>
> M3-REDESIGN, on cost grounds alone (24.6 ms/frame, 275→35 fps), did the opposite:
> 1. it **reinstated the exact per-cell field M0 deleted** — this section says so in its own next
>    sentence, *"stored in the same per-cell field the deleted flood used"*;
> 2. it **cancelled the M4/U7 deletion** — U7 now reads *"`m_skyLight` must now be KEPT"*;
> 3. it reintroduced per-cube-cell quantisation of light, plus staleness coupling (the
>    occupancy-flush ordering rule) and a per-chunk streaming cost.
>
> **What is genuinely better than the flood**, and why this was not a silly decision: the *values*
> are traced against real sub-voxel geometry, so a sealed room reads 0 and a doorway produces real
> falloff instead of a linear 1-per-cell ramp; and the wall-base band is avoided because cells are
> traced from their centre rather than marked opaque. **What is not better:** the condemned storage
> property is back, and the plan never re-ratified it — it recorded the consequence in U7 as settled
> fact and moved on.
>
> ⚠️ **And the drift compounded.** The 2026-08-31 session treated *"turn the bake on by default"* as
> **the main goal**, and spent itself making the bake 4.8× faster (option 4) and teaching its gather
> to see sub-voxel walls (D21). Both are real fixes to real defects — and both **further entrench a
> component this plan intended to delete.** Optimising the scaffold is how the scaffold becomes the
> building.
>
> **Status: the bake is a TEMPORARY SCAFFOLD, not the destination.** It stays for now because it is
> the only thing that makes interiors dark at a shippable cost. It does not get treated as the
> answer, and "enable the bake" is **not** the main goal.
>
> **Exits, to be chosen deliberately rather than by default:**
> * **M5 — radiance cascades.** The design's actual endgame (parking condition *expired
>   2026-08-11*), which subsumes sky visibility as a byproduct **and** delivers the indirect bounce
>   committed to in the directive. If M5 lands, every hour spent on the per-cell sky cache is
>   deleted work. **This is the one that resolves the contradiction rather than managing it.**
> * **Make per-fragment tracing affordable.** The 24.6 ms was the naive shape — 9 rays × full DDA,
>   every fragment, every frame, no reuse. It is now 5 rays / 16 u, and M2's visibility term
>   measured *free* because its `dot(N,L) > 0` gate means almost no marches actually run. Half-rate
>   evaluation and temporal reuse have never been tried.
> * **Cache, but not per-cube-cell** — keep an intermediate while dropping the resolution property
>   the directive condemned.
>
> Until one is chosen, M4/U7 stay blocked, because they cannot delete a field the scaffold depends
> on.
>
> ### ✅ EXIT CHOSEN AND UNDER WAY 2026-09-01 — and the 24.6 ms was never real
>
> **The number that retired per-fragment tracing was measured at settings the bake itself does not
> use.** D1 ran the shader at **9 rays / reach 24 / 512 cells**. M3-REDESIGN then established, by
> measurement with a doorway control, that **5 rays / reach 16** seals a room at every wall
> thickness — and shipped the bake at those settings. Nobody re-ran the per-fragment path at them.
>
> Re-measured on Release, generated town, fixed pose (70,60,40 yaw −120 pitch −18), pipeline stats
> off, `GpuProfiler` scopes, sky OFF/ON interleaved twice, medians:
>
> | | Static Geometry | Scene Pass |
> |---|---|---|
> | **D1 — 9 rays / 24 u / 512** | **24.604 ms** | 26.262 ms |
> | **now — 5 rays / 16 u / 288** | **5.166 ms** | 6.257 ms |
> | sky OFF control | 0.299 ms | 1.439 ms |
>
> **The sky term costs +4.87 ms, not +24.46 ms — 5× less, for the quality the bake already
> ships.** The premise of M3-REDESIGN does not survive its own settings.
>
> ⚠️ **Scene is comparable, not identical.** `POST /api/settlement/build` returned no `placed`
> count, so this town is not verified to match D1's 14,356-mixed-cube scene; 16 visible chunks and
> 18,586 shadow instances. The OFF baselines line up closely (0.299 vs 0.142 ms Static Geometry,
> 1.439 vs 1.271 ms Scene Pass), so the scenes are the same order — but the honest claim is
> "same-order scene, 5× cheaper sky", not "identical scene".
>
> **This is the exit. M3 goes back to being traced per fragment, as specified.**
>
> ### ✅ THE BAKE IS DELETED 2026-09-01. The contradiction is resolved, not managed.
>
> 1. **Gated the trace** — the same idea that made M2's visibility term measure free (`dot(N,ldir)>0`
>    plus a radius test meant almost no marches ran); this trace had **no gate at all**. Ray 0 is the
>    surface normal and the cheapest probe: if it escapes, the fragment is outdoors and the other
>    four rays confirm a foregone conclusion. It can only ever return **more** sky for a surface
>    whose normal already sees sky, so it cannot brighten an interior — an interior fragment's normal
>    ray is blocked and takes the full path.
>    **Static Geometry 5.166 → 2.997 ms**, against a 0.318 ms sky-OFF control.
>    **The sky term costs +2.68 ms where D1 measured +24.46 ms — 9.1× less.**
> 2. **`m_skyTracing` now defaults ON.** Traced sky is the shipped mechanism.
> 3. **The bake block, `s_bakeSkyVisibility`, `s_lastSkyBakeMs/Cells`, the D21 pool gather
>    (`s_cubeOccupancy`, `s_gatherFromPool`), and the `bake_sky` / `gather_pool` API surface are all
>    DELETED.** Builds clean; lighting suite 63/63.
>
> **Verified at runtime with the bake gone**, on the engine's own generator — an engine-built
> `hall_house` (`placed: 13822`) at its reported position, `sky_probe` along y=18 z=1:
>
> | x | −3 | −2 | −1 | **0–4** | 5 | 6 | 7 |
> |---|---|---|---|---|---|---|---|
> | sky | 0.92 | 0.92 | 0.79 | **0.00** | 0.71 | 0.79 | 0.92 |
>
> **Interior exactly 0, exterior 0.79–0.92, real falloff at the boundary — with no stored per-cell
> light field anywhere in the path.** That is M3 as the directive specified it.
>
> ⚠️ **Honest caveats.** `sky_probe` queries the CPU mirror at its own default 9 rays / 24 u, so it
> proves the *geometry* seals, not that it seals at the shader's 5/16 — that rests on the
> doorway-controlled gate and the `M3REDESIGN` unit test, both at 5/16. The town used for the timing
> is *comparable to* D1's, not verified identical (`settlement/build` returned no `placed` count).
> And +2.68 ms/frame is not free: it is a real GPU cost paid every frame, traded against a bake that
> cost 5.59 ms of CPU per streamed chunk.
>
> **M4/U7 are now genuinely unblocked** — `m_skyLight` has no writer left. Deleting the field, the
> per-corner `InstanceData` words and the seven transports is the next step, and it is the deletion
> the directive asked for.

Sky visibility is now computed at CHUNK-BAKE time by tracing the occupancy, and stored in the same
per-cell field the deleted flood used — so the shader reads one interpolated value again instead of
marching 9 rays per fragment.

**Render cost — the whole point — measured with `GpuProfiler`, pipeline stats off, same pose:**

| | Scene Pass | Static Geometry |
|---|---|---|
| sky OFF (baseline) | 1.364 ms | 0.170 ms |
| **BAKED sky ON** | **1.373 ms** | **0.174 ms** |
| per-fragment tracing | 20.016 ms | 18.923 ms |

**The bake is free at render time** — 1.373 vs 1.364 ms is inside noise — against **20.0 ms** for
per-fragment. That is a **~14.6x** reduction and clears the gate's "< 2 ms" with the entire budget
to spare.

**Behaviour preserved**, read from the baked field as numbers (`/api/world/baked_light`), not pixels:
sealed interior **0**, open ground **15/15**.

**Gated at the settings that SHIP, not at probe quality.**
`M3REDESIGN_BakeSettingsStillSealARoomAtEveryWallThickness` re-runs the sealed-room requirement at
the bake's actual 5 rays / 16 u on all five real wall thicknesses (9/6/3/2/1 micro), **with a
doorway control** — because the cheaper ray set drops the 60-degree diagonals, and at full quality
the single ray that threaded a doorway WAS a diagonal. Without that control a passing "sealed = 0"
could mean the tracer had simply gone blind. Both hold. **56/56 lighting tests.**

**Cost knobs chosen by measurement, not taste.** At full probe quality (9 rays, 24 u) the bake cost
**39.89 ms for 1024 cells** on one chunk — against a **6 ms budget for ALL dirty-chunk work in a
frame**. Dropping to 5 rays / 16 u brought it to **14.38 ms** for the same 1024 cells while
sealed/open readings stayed 0 and 15. Only AIR cells adjacent to something solid are traced (1024 of
32768); solid rock and open air are skipped.

⚠️ **REMAINING ISSUE, measured not guessed: the bake is still ~14 ms per chunk, above the 6 ms
dirty-chunk budget.** One newly-meshed chunk can therefore overrun a frame. This did not exist
before — it is the cost this redesign moved, not removed. Options, in order of preference:
1. run the bake on the mesh worker rather than the main thread;
2. amortise it — bake a chunk's cells across several frames, starting from the full-sky default;
3. reduce further (3 rays, or shorter reach) — but reach is what decides the largest room that
   reads as sealed (D8), so this trades a real quality property.

### ✳️ OPTION 4 added 2026-08-31 — PARALLELISE THE BAKE. Preferred over 1–3, and here is why.

**This is the main goal's blocking item** (see the status note below: with the bake off,
`m_skyLight` is pinned to 15 and interiors are lit as if outdoors), so the cheapest sufficient fix
wins.

Established by reading, not assumed:
* **The bake loop is embarrassingly parallel.** Each cell writes only its own
  `m_skyLight[cellIdx(x,y,z)]` and reads only `m_solidVis` (already filled) plus the callback. No
  cell depends on another — unlike the BFS flood it replaced, which was inherently sequential.
* **The query is safe to call concurrently.** `VoxelLightOccupancyGpu::skyVisibility` is `const`
  (`VoxelLightOccupancyGpu.h:109`) and delegates to the free function `packedPoolSkyVisibility`
  over `m_packed` — pure reads of the *last flushed* pool. The M3-REDESIGN ordering rule already
  guarantees the pool is flushed before `updateDirtyChunks()` runs, so it is not mutating underneath.
* **Cost is per-cell and dominated by the query.** ~1024 traced cells at ~14 µs each; the 32768-cell
  scan that finds them is trivial by comparison. So wall time divides by core count.

**Why this beats the other three:**
* vs **1 (mesh worker)** — `DirtyChunkTracker::updateDirtyChunks` processes chunks *inline on the
  calling thread* under a budget with adaptive backoff (its own comment: *"a single chunk remesh
  (full mesh + light bake) can cost far more than the whole budget"*). Moving meshing off-thread
  means making chunk state and GPU buffer handoff thread-safe — a large change with a large blast
  radius, for a stage that is not the actual problem.
* vs **2 (amortise)** — leaves a chunk visibly wrong for several frames while it converges from the
  full-sky default, i.e. a visible pop on every newly-streamed chunk.
* vs **3 (fewer rays/reach)** — spends a measured quality property (D8: reach decides the largest
  room that can read as sealed) to buy time we can get for free.

Option 4 is **contained entirely within the bake block**, changes no threading contract elsewhere,
and keeps rays and reach exactly where measurement put them.

**Gate:** `s_lastSkyBakeMs` for a chunk of comparable traced-cell count falls **below the 6 ms
dirty-chunk budget**, with `s_lastSkyBakeCells` unchanged (same work, less wall time), and the
sealed/open readings still 0 and 15 with the doorway control alive — the same gate M3-REDESIGN
passed, because a faster bake that changes the answer is not a faster bake.

### ✅ OPTION 4 RESULT 2026-08-31 — 4.8× faster, under budget. Cost half of the gate MET.

Release, LightingGates, bake forced by toggling a voxel to dirty a chunk, read back through
`GET /api/debug/light_occupancy` (`last_sky_bake_ms` / `last_sky_bake_cells`), 6 runs:

| | cells traced | ms/chunk | µs/cell |
|---|---|---|---|
| **serial (recorded baseline)** | 1024 | **14.380** | 14.04 |
| **parallel — median of 6** | **1029** | **2.978** | **2.89** |

Range across the 6 runs 2.788–3.314 ms, so it is reproducible, not a lucky sample. **Cell count is
unchanged (1029 vs 1024 — same chunk shape, same work), so this is wall time divided, not work
skipped.** 14.380 → 2.978 ms is **4.8×**, and it clears the **6 ms dirty-chunk budget** that was the
sole reason sky visibility shipped disabled.

⚠️ **A CRASH, and the reason is worth keeping — it is a language trap, not a typo.** The gather
buffer was first written as `static thread_local std::vector<int> bakeCells`. That crashed the
engine on the first real bake. **Variables with static or thread storage duration are NOT captured
by a lambda** — the name resolves, inside the lambda body, to the *executing* thread's instance. So
every worker thread saw its own empty vector and indexed out of bounds. It is now a plain local with
a `reserve`; a per-call allocation is nothing against ~14 µs per traced cell. The comment in
`ChunkRenderManager.cpp` says so, because this reads like a harmless optimisation.

Also worth recording: **`const` was not what made the query safe.** `packedPoolSkyVisibility` was
*read* and confirmed genuinely pure — locals only, no `static`, no `mutable`, reading `packed` and a
const global. A `const` method can still touch shared mutable state, so the guarantee came from
reading the body, not from the signature.

**Threading shape:** `min(hardware_concurrency - 1, 16)` workers, one core left for the rest of the
frame because this runs *inside* the frame rather than on a worker; chunks with fewer than 64 cells
to trace stay serial, where thread hand-off would cost more than it saves.

**Correctness half of the gate — MET.** `LightWallMatrixTest.M3REDESIGN_BakeSettingsStillSealARoom
AtEveryWallThickness` **passes** (298 ms), so sealed rooms still read sealed at all five wall
thicknesses with the doorway control alive: the parallel bake produces the same answer, not a
faster wrong one. Whole lighting suite green — **63/63 in ~2.1 s** (LightWallMatrix 6,
VoxelLightOccupancy 26, LightManager 27, LightBakeOcclusion 3, LightBleed 1).

⚠️ **What "under budget" does and does not mean.** The 6 ms budget covers **all** dirty-chunk work in
a frame, not one chunk. At 2.978 ms that is ~2 chunks per frame; at 14.38 ms a **single** chunk blew
the whole budget 2.4× over. So the change does not make streaming free — it makes
`updateDirtyChunks(budgetMs)`'s budget-and-backoff mechanism work as designed instead of being
overrun by every individual chunk. Heavy streaming still queues chunks across frames, which is the
intended behaviour.

### DECISION 2026-08-31 — turn the sky bake ON by default (`s_bakeSkyVisibility = true`)

The bake shipped disabled for exactly one stated reason — the streaming cost — and that reason is
now measured away. Leaving it off would mean the engine keeps `m_skyLight` pinned to 15 and lights
interiors as if outdoors, which is the M0 hole this entire rebuild exists to climb out of. **This is
the step that makes "a dark room is dark because nothing emits into it" true.**

**L4 gate for the flip — measured in a live engine, not asserted:**
1. an interior reads substantially darker than the exterior at the same pose, with the exterior as
   the positive control (if the exterior also darkens, something global changed, not enclosure);
2. the ordering sealed < window < door < open matches the baseline this plan already recorded;
3. no streaming hitch: frame time while flying through fresh chunks stays comparable to bake-off.
**If any of these fail, the flip is reverted rather than explained away.**

⚠️ **Ordering dependency introduced:** `updateLightOccupancy()` now runs BEFORE
`updateDirtyChunks()`, because a chunk meshed before its own geometry reached the pool would bake as
if the world were empty and read fully sky-lit indoors. Occupancy comes from the physics grid, which
is populated at chunk LOAD independent of meshing, so the reverse dependency does not exist.
⚠️ Baking is **default OFF** (`?bake_sky=1` to enable) until the streaming cost above is resolved.

---

**(superseded) M3-REDESIGN — per-fragment sky tracing must be replaced.**
Options, to be chosen with these numbers in hand:
1. **Bake sky visibility per voxel** into the chunk data (the flood's storage slot, but computed by
   tracing rather than by BFS decay, and at sub-voxel resolution where it matters). Cost moves to
   chunk build time; the shader reads one value again.
2. **Far fewer rays + filtering** (temporal reprojection or a spatial blur). Cheaper but noisy, and
   D7 already flags that banding was never examined.
3. **Fold into M5's radiance cascades**, which compute exactly this kind of visibility once at
   several scales rather than per fragment per frame.
Option 1 is the closest to the existing architecture and the only one that clearly gets to a
shippable frame budget without new machinery. **Gate:** the same LightWallMatrix M3 gate green at
all five thicknesses, with sky ON costing < 2 ms at the D1 pose.

---

**D1-ORIGINAL — Measure GPU cost, in Release, on a furnished scene. (kept for the gate wording)**
No valid cost number exists for M1, M2 *or* M3. Debug is CPU-bound and `engine_timing`'s
`gpuFrameTime` mirrors `cpuFrameTime`, so the "no measurable difference" A/B taken during M2 proves
nothing. M3 is the worry: 9 rays × up to ~90 samples per fragment, versus one march per light.
**This may invalidate the per-fragment approach entirely** and force precomputed per-voxel sky
visibility or folding sky into M5's cascades — which is why it must land before M5 is scoped.
**Gate:** Release build, `GpuProfiler` scopes, pipeline-statistics queries OFF, A/B via `?trace=`
and `?sky=` at fixed poses on a furnished city; per-pass ms recorded for all four combinations.

**D2 — PARTLY DONE 2026-08-30.** The *deterministic* half is done and is what actually caught the
D0 bug: `LightWallMatrixTraced` now gates M2 and M3 on the engine's own grounded rigs (sealed and
doorway boxes at real `structure_styles.json` thicknesses, resolutions chosen by
`MicroCanvas::exportVoxels`), which is stronger than any hand-placed world rig.
The *runtime* half is written but NOT yet run: `tools/lighting_gate_rig.py` builds sealed/doorway
boxes over the API, queries every surface height instead of assuming it, and **aborts rather than
measures if any voxel is refused**. A `game.json` was added to the `LightingLab` project (flat,
vegetation-free, flora density 0) — but that project already contains a 1.6 MB world DB from
2026-08-17 which is not mine to delete, and an existing DB does not regenerate from `game.json`.
**Remaining:** point the script at a fresh project (or an explicitly regenerated world) and compare
its numbers with the deterministic gates.

**D2-ORIGINAL — Build a dedicated lighting test world. BLOCKS trustworthy gates.**
M2 and M3 were gated in `CharacterTestbed`, which is uncontrolled. See "On the test world" below —
this directly caused a dead control, wrong probe heights, and refused rig voxels.
**Gate:** a flat, vegetation-free, single-biome world with a scripted rig builder (sealed box,
one-window box, doorway box, open ground) that reports placed-vs-refused counts; M2 and M3 gates
re-run on it and the numbers compared against the CharacterTestbed ones.

**D4 — CORRECTED.** "Debug views exist only in `voxel.frag`" was overstated: `grass.frag` and
`foliage.frag` honour modes 1 and 2 and sample the near cascade. What they lack is modes 3–9 and any
point-light term. `character.frag` has no debug modes at all, which is the case that actually blocks
U2's gate.

**D14 — ✅ CODE CLOSED BY U2 2026-08-30; ⚠️ GATE ONLY PARTLY MET. No longer blocking.**
*(Status corrected 2026-08-31 — this entry had gone stale and still read "BLOCKING" after U2 closed
it. Re-verified against the shipped shaders, not against the prose above it.)*

**Verified by grep on `shaders/`:** `occupancy.glsl` is included and `phxLightVisibility` is called
in **all three** point-light consumers — `voxel.frag` (2 calls), `character.frag` (2),
`transparent_voxel.frag` (1). The shared-include fix described below was done. A lantern sealed in a
stone room no longer lights a character outside it, and no longer shines through glass.

⚠️ **What is still owed is the GATE, not the code.** U2's result is explicitly titled "(voxel half)".
The character half was answered by a **forced-occlusion diagnostic** — forcing `phxLightVisibility`
to `return 0.0` collapsed the control 46,234,692 → 3,066,639 (15×, far above the noise floor),
proving the term is applied — and U2 states plainly that *"a separate sealed-box character capture
was NOT taken"*. The glass-wall and mirror-surface scenarios in the gate below were never run at all.
**Proving a term is applied is weaker than proving the leak is gone.**
**Remaining gate:** the sealed-box rig with (a) a character inside/outside and (b) a glass wall —
zero exterior contribution, live positive controls, noise-mask method. (c) mirror is moot:
`mirror_voxel.frag` declares the light SSBO and never reads it, so it is not a point-light consumer.

*Original text kept below — the shared-include diagnosis is what U2 acted on.*

Raised 2026-08-30 by the full audit. `phxLightVisibility` exists only in `voxel.frag`, while
`character.frag` and `transparent_voxel.frag` each shade point lights with their own loop and no
visibility term. (`mirror_voxel.frag` was originally listed here and is NOT a point-light consumer —
it declares the SSBO and never reads it. Corrected by reading.) `voxel.frag` does, however, serve
static chunks **plus kinematic voxels plus GPU-dynamic voxels**, so doors, furniture, item props and
debris particles all get M2 correctly. **A lantern sealed in a stone room still lights a character
standing outside, and still shines through glass.** M2 was reported as fixing the reported defect;
it fixes it for chunk voxels only.
Cause: the occupancy buffers (bindings 11/12) and the DDA are declared in `voxel.frag` alone. The
fix is to move `phxOccupancySolid` / `phxDdaHitsSolid` / `phxLightVisibility` into a shared include
— they are already pure functions apart from the two SSBO reads, so the buffers must be declared in
each consumer (the `lighting.glsl` contract forbids implicit buffer reads, so this needs either a
second include or an explicit sampler-style parameterisation).
**Gate:** the sealed-box M2 rig repeated with (a) a character inside/outside, (b) a glass wall, (c) a
mirror surface — zero exterior contribution in each, with live positive controls. D4 (debug modes in
`character.frag`/`grass.frag`) is a prerequisite for seeing (a) at all.

**D15 — ✅ MOSTLY CLOSED BY U3.3 2026-09-01.** Grass and foliage read the light SSBO and use the
shared visibility term; measured on a night meadow, one glow voxel took viewport mean luminance
5.108 → 48.563 with 302k un-saturated pixels lit. `far_terrain` / `far_tree_mesh` are a written
ACCEPT (sun-only: beyond ~900 u a ≤15 u light subtends nothing). **Still open: WATER**, which was
listed in this item and has not been touched. Foliage is verified by construction only — the test
world has no trees.

**D15 — (original) A TORCH DOES NOT LIGHT GRASS, FOLIAGE, FAR TERRAIN, FAR TREES OR WATER.**
None of those shaders read the light SSBO. Vegetation is sun + ambient only, so a campfire in a
meadow lights the ground voxels and leaves every blade around it unlit. Previously the block-light
flood covered this; M0 deleted it, so this is a REGRESSION introduced by this rebuild, not a
pre-existing gap. **Gate:** decide per pass — wire the light loop in (with D14's shared include), or
write down that vegetation is deliberately sun-only and why. Not left to be rediscovered.

**D16 — ✅ CLOSED BY U1 2026-08-30, with one documented exception.**
*(Status corrected 2026-08-31 — stale entry, re-verified against the shipped shaders.)*
`phxAmbient` is **deleted** (the only surviving mention is a `lighting.glsl` comment recording the
removal). The hand-rolled 16-tap PCF is gone from both `character.frag` and `transparent_voxel.frag`
— the only `poissonDisk` hits in either file are comments saying so — and both now call
`phxShadowPCSS` **and** min-compose the near cascade, so characters and glass have the same cascade
coverage as chunks. `transparent_voxel.frag` uses `phxAmbientAtmos(normal, vSkyLight,
ubo.ambientColor)`; its one remaining `ubo.ambientLight` hit is a comment describing the old
behaviour. **Exception:** `water_underwater.frag` still reads the flat `ubo.ambientLight` as a fog
brightness scalar — deliberate, reasoned in U1, and tracked there as a follow-up.

*Original text kept below.*

**D16 — THREE SHADOW FILTERS AND THREE AMBIENT MODELS, WHERE ONE OF EACH IS INTENDED.**
Shadow: PCSS (`voxel`), Fast 4-tap (vegetation/far), and a hand-rolled 16-tap PCF duplicated in
`character` and `transparent_voxel` that samples the MID map only — so characters and glass get no
near or far cascade. Ambient: `phxAmbientAtmos` (6 shaders), flat `vec3(ubo.ambientLight)`
(`transparent_voxel`, `water_underwater`), and `phxAmbient` — the legacy model, now **dead code with
zero callers**.
**Gate:** `phxAmbient` deleted; the two flat-ambient users on `phxAmbientAtmos`; the duplicated PCF
replaced by `phxShadowPCSS` (or a documented reason a character needs its own filter); cascade
coverage equal for characters and glass.

**D17 — WITHDRAWN, MY ERROR.** Water does **not** self-tonemap. The `grep -l phxTonemap` that
produced this item matched a COMMENT reading *"Do not re-add a tone map here."* There is exactly one
grade, in `post_process.frag`, after compositing — which is correct. Replaced by U5, which addresses
what is actually broken behind the grade (bloom, SSAO, the ungraded editor viewport).
Original text kept below so the mistake is legible rather than quietly deleted.

**(void) D17-ORIGINAL —**
`post_process.frag` grades the scene and `water`/`water_cell`/`water_underwater` each call
`phxTonemap` themselves. `water.frag`'s comment says its own call was added to fix a divergence,
implying the water pass is outside the post grade — but that was not verified during this audit.
**Gate:** an A/B capture of a water surface with the water-side call removed; if the two agree, the
water calls are redundant and go.

**D13 — ⚠️ MOSTLY CLOSED BY U1/U2; ONE DIVERGENCE LEFT (the sun BRDF).**
*(Status corrected 2026-08-31, verified against the shipped shader.)* `transparent_voxel.frag` now
`#include`s **both** `lighting.glsl` (line 12) and `occupancy.glsl` (line 68). Converged: ambient
(`phxAmbientAtmos`), shadow filter (`phxShadowPCSS`), near-cascade min-compose, `vSkyLight` gating
via `phxSkyGate`, and the point-light visibility term. **Still diverged:** the sun term is
`diff * ubo.sunColor + sunSpec * ubo.sunColor` (line 170) — a plain diffuse+specular, not the shared
`pbrBRDF` that `voxel.frag` uses. So glass and the stone beside it agree on ambient, shadow and
occlusion but still respond differently to the sun.
**Remaining gate:** either move the sun term onto `pbrBRDF`, or record why a transmissive surface
warrants its own BRDF. Lower priority than the four divergences already fixed.

*Original text kept below.*

**D13 — `transparent_voxel.frag` IS A SECOND, DIVERGED COPY OF THE VOXEL LIGHTING MODEL.**
Found 2026-08-30. It does not include `lighting.glsl`. Its own comment says *"Lighting (same as
voxel.frag)"* — a copy-paste that is now false in four ways:
* Blinn-Phong sun term instead of the shared `pbrBRDF`;
* its own 16-tap PCF against the **mid shadow map only** — no near cascade, no far cascade;
* `ambient = vec3(ubo.ambientLight)`, a flat constant, instead of `phxAmbientAtmos` — so glass gets
  none of the physical atmosphere the rest of the world has had since 2026-08-10;
* its own point-light loop, so glass gets **no M2 visibility term** — a lantern shines through a
  glass wall exactly as it did before this rebuild.
A glass voxel is therefore lit by a different engine than the stone voxel beside it, from the same
chunk data. This is the clearest instance of the duplication this plan exists to remove, and it was
NOT in the original census.
**Gate:** `transparent_voxel.frag` includes `lighting.glsl` and uses the shared ambient/shadow/PBR
path plus `phxLightVisibility`; a lantern behind glass is occluded like a lantern behind stone; the
misleading "same as voxel.frag" comment is gone.

**D12 — ✅ CLOSED BY U3.2 2026-09-01.** Emissive voxels are registered as real point lights and go
through the same visibility term as every other emitter. Gate: 6 glow voxels placed via the world
API produced 6 lights with no `/api/light` call, and removing them released all 6.

**D12 — (original) EMISSIVE MATERIALS ARE NOT EMITTERS. The last system still outside the model.**
Raised 2026-08-30. `glow`/`glow_blue`/`glow_green` and the flaming state are **self-shading only**:
`voxel.frag` computes `albedo * emissiveMultiplier * tint` (plus bloom) and the voxel illuminates
NOTHING. A point/spot light is the mirror image — it illuminates other surfaces and has no visible
body. They are two halves of one physical object, authored and maintained separately, and a torch
needs both kept in sync by hand.

Consequences, all live today:
* a wall of `glow` voxels looks blazing and leaves the room dark;
* a point light with no emissive geometry lights a room from an invisible source;
* the two can drift — move the fixture, forget the light;
* **emissive is UNCAPPED while lighting is capped at 32 point / 16 spot**, so the *look* of a lit
  city scales indefinitely while the *lighting* silently stops at 32 fixtures — and since M0 deleted
  the block-light flood, fixtures past the cap now contribute nothing at all;
* emissive is the ONE emitter M2 did not touch, because there was nothing to occlude. It has
  radiance and no transport, so it sits entirely outside the radiance × visibility rule this whole
  rebuild exists to establish.

This directly contradicts the target-architecture table above, which names one rule for every
emitter. The right shape is that an emissive voxel **IS** the light source: the material declares
radiance and the lighting system reads it from voxel data, instead of a human hand-placing a
matching point light beside it. That also dissolves the 32-light cap as a special case, because
voxel emitters are geometry rather than SSBO slots — which is exactly what M5's radiance cascades
consume natively, and why the cap removal was parked there.
**Gate:** placing a `glow` voxel with NO accompanying point light lights the surrounding room, with
correct occlusion; and the emitter count is not bounded by MAX_POINT_LIGHTS.
**Sequencing:** decide with M5, not before — a per-emitter forward path would just re-create the cap.

**D3 — Kinematic geometry and characters do not occlude.**
The occupancy is built from per-chunk *static* grids, so doors, furniture, item props and characters
are not occluders. **A closed door does not block light.** For a feature whose headline case is
"a lantern inside a building", a door that does nothing is a real gap.
**Gate:** decide explicitly — either register kinematic occupancy into the pool, or write down that
it is accepted and why. Not left to be rediscovered.

**D4 — Debug views exist only in `voxel.frag`.**
Grass, foliage, water and characters render normally in every mode, which is exactly what produced
M2's dead control. Extending modes to `character.frag`/`grass.frag` is a stated prerequisite for
trusting any gate on characters.
**Gate:** modes 0–9 honoured in both shaders; re-run M2's mode-5 gate with a character in frame.

**D5 — Verification runs through the CPU mirror, not a GPU readback.**
The GLSL and C++ marches are line-for-line equivalent and the GPU path demonstrably changes the
image, but nothing confirms the GLSL march step for step.
**Gate:** a readback path (or a debug mode encoding step count/first-hit) compared against
`packedPoolLightVisibility` at the same points.

**D6 — Sealed-room and moving-light gates from this plan were never run.**
M2 used a wall rig, which exercises the same mechanism but is not the same test. The moving-light
gate ("shadow edges track the light frame to frame") is untested, and it is the claim that a carried
torch works. **Gate:** run both, on D2's world.

**D7 — Sky visibility is unfiltered and unexamined for banding.**
9 fixed directions, per fragment, no filtering. Banding/noise has not been looked at.
**Gate:** capture a graded interior; if banding is visible, decide between more directions,
interleaved sampling, or a spatial filter — with the cost from D1 in hand.

**D8 — Reach bounds the largest room that reads as sealed.**
24 u today. A larger hall reads as partly sky-lit even when closed.
**Gate:** measure the largest generated interior in the structure library; set reach from that or
document the limit.

**D9 — Gap 15: `remove_subcube` desyncs the occupancy grid** (`StructurePipelineGaps.md`).
Pre-existing, affects character collision too, and produces mode-8 green.
**Gate:** the grid rebuilds a cell's masks from surviving content after a removal.

**D10 — ✅ CLOSED BY U6 2026-08-30.** *(Status corrected 2026-08-31 — stale entry.)* U6 delivered
exactly what this asked for: a table with a written decision and a measured reason for every caster
gap, no blank cells, plus the `NearShadowCascade.md` doc correction. Original text below.

**D10 — Sun shadow coverage gaps, to DECIDE not discover.**
Grass casts nothing; the far cascade draws no chunk casters, characters, kinematics, particles or
foliage; far-tree cards never cast; CPU debris and VFX never cast; the `NearShadowCascade.md` row
claiming far includes chunk multidraw is stale. These get more visible now the sun is the only
direct path. **Gate:** a written decision per gap — close or accept.

**D11 — Roof striping.** `N·L` on stepped faces approximating a slope. Survives this entire rebuild
and is still untracked separately. **Gate:** own doc entry, or an explicit "accepted".

**D21 — ⚪ MOOT 2026-09-01: the bake it describes no longer exists.** The gather defect was real and
was fixed, then deleted along with the bake when M3 went back to per-fragment tracing. Kept for the
method lessons: verify the world *where the thing actually is*, and note that the structure build
API ignores the origin passed to it and reports its real position in its locations field.

**D21 — ⚠️ DIAGNOSIS RETRACTED 2026-08-31. THE EVIDENCE BELOW WAS MEASURED IN THE WRONG PLACE.**

> **Read this before the entry below.** Every "all cells read 15" measurement in D21 was taken
> around x≈36–48 and x≈54–71, because I passed `"origin": {...}` to
> `POST /api/structure/build` and assumed the building landed there. **It did not.** The build
> response's own `locations` field reports
> `{"id":"hall_house_-2_1","position":{"x":-1.5,"y":17.0,"z":1.5}}` — the generator **ignored the
> requested origin** and placed the structure near the world origin. `placed: 13822` was true; my
> probes were 60+ units away, sampling empty air, and `/api/world/voxel` at the cells I probed
> returns `exists: false` because *there is nothing there*, not because enclosure failed.
>
> **What survives:** the code observation, which was made by reading and is independent of the
> measurements — `m_solidVis` really is cube-resolution (`:350`, *"1 = a visible CUBE occupies the
> cell"*) and generated buildings really are sub-voxel, so the gather really is blind to a 2-micro
> wall. That is a latent defect.
>
> **What does NOT survive:** the claim that this is what produced the observed symptom, and
> therefore the claim that it is what blocks the main goal. `last_sky_bake_cells = 1024` is equally
> well explained by "the chunks I dirtied contain nothing but flat ground" — which, with the
> building at the world origin, is exactly what they contained. **A single-sample statistic from
> whichever chunk happened to mesh last was never evidence about a specific building.**
>
> **The gather fix (option 2, pool-based `packedPoolCubeOccupancy`) is implemented and builds, but
> it is UNPROVEN against any symptom.** It must not be described as fixing D21 until re-measured at
> the building's real location. `s_bakeSkyVisibility` stays `false` meanwhile.
>
> ### ✅ RE-MEASURED AT THE REAL LOCATION — diagnosis CONFIRMED, fix ATTRIBUTED (2026-08-31)
>
> Same engine, same session, same cells, **one variable** — `?gather_pool=0/1`, a live switch added
> for exactly this reason, so the fix could be attributed by measurement rather than asserted.
> Engine-generated `hall_house` (`placed: 13822`) at its reported position `(-1.5, 17.0, 1.5)`;
> line probe at `y=18, z=1`:
>
> | gather | bake cells | ms | sky at x = −4…7 | dark (<12) |
> |---|---|---|---|---|
> | **pool (the fix)** | **1290** | 5.59 | `15 15 15 15 · 0 0 0 0 0 · 12 · 15 15` | **5 / 12** |
> | cube-only (pre-fix) | **1024** | 2.56 | `15 15 15 15 15 15 15 15 15 15 15 15` | **0 / 12** |
>
> **Interior reads 0, the boundary cell reads 12, the exterior positive control stays 15.** The
> cube-only path reproduces the pinned **1024** exactly, and 1024 → 1290 is the sub-voxel walls
> becoming visible to the gather. **The original diagnosis was correct; only its evidence was
> gathered in the wrong place.** Both statements need to stand together — the retraction above was
> right at the time it was written.
>
> ⚠️ **The cost moved, and not in a comfortable direction: 2.56 ms → 5.59 ms.** The gather now runs
> a pool query per cell over all 32768, and traces 26% more cells. That is still inside the 6 ms
> dirty-chunk budget, **but only just, and that budget covers ALL dirty-chunk work in a frame, not
> one chunk.** The 4.8× won by parallelising the trace has been substantially spent by the gather.
> **`s_bakeSkyVisibility` therefore stays `false`**: L4 conditions 1 and 2 are met, condition 3 (no
> streaming hitch) is not evidenced, and a 5.59 ms single-chunk cost against a 6 ms whole-frame
> budget is not something to enable on the strength of a static-scene probe.
> **Next, and it is now a cost problem again rather than a correctness one:** parallelise the gather
> (it is the same embarrassingly-parallel shape as the trace), or hoist the per-cell pool lookup —
> the directory slot is constant for a whole chunk, so it is being recomputed 32768 times.
>
> **Method lesson, and it is the same one this document keeps relearning:** I verified the build
> *response* (`placed: 13822`, `failed: 0`) and then verified *the world* — but at coordinates I
> had assumed rather than at coordinates the engine reported. Verifying the world is only worth
> anything if you verify it **where the thing actually is**. The `locations` field was in the
> response the whole time.

*Original entry follows, retained so the reasoning and its error stay legible.*

**D21 — 🔴 THE SKY BAKE NEVER TRACES A GENERATED INTERIOR. Its cell gather is CUBE-LEVEL; buildings
are SUB-VOXEL. This, not cost, is what blocks the main goal.** *(Found 2026-08-31 by trying to turn
the bake on and measuring the result.)*

**The flip to `s_bakeSkyVisibility = true` was made and then REVERTED**, per the L4 gate written
above ("if any of these fail, the flip is reverted rather than explained away").

**What was measured, in a live Release engine on an ENGINE-GENERATED building**
(`POST /api/structure/build` schema v2, `hall_house`, footprint `[5,7]` — `placed: 13822`, chimney
built, generator's own report listing `dark_rooms: service/hall/solar`):

* A 17×17 horizontal slice of `/api/world/baked_light` at `y=18` read **15 in all 289 cells**.
  Min 15, max 15, zero cells below 8. **No enclosure anywhere.**
* `bake_sky: true`, and the bake was demonstrably running — `last_sky_bake_ms` 2.5–2.9 ms.
* The occupancy pool **did** receive the building: `mixed_cubes` 12 → **199**, `pool_words`
  51513 → 56001. So the walls are visible to the tracer.
* **The tell: `last_sky_bake_cells` was pinned at exactly 1024** — every bake, before and after the
  building was placed, including after forcing a re-bake of the building's own chunk. 1024 = 32×32 =
  one flat layer.

**Diagnosis.** The gather (`ChunkRenderManager.cpp`, phase 1 of the bake) selects cells with:
```cpp
solidAt(x,y,z) := m_solidVis[cellIdx(x,y,z)] != 0
```
and `m_solidVis` is **cube resolution only** — `:350` `m_solidVis.assign(N*N*N, 0); // 1 = a visible
CUBE occupies the cell`. Generated buildings obey the engine's own sub-voxel detail rule, so their
walls and floors are subcubes/microcubes and register **nothing** in `m_solidVis`. Air cells beside
them fail the `touches` test, are never gathered, are never traced, and keep the default 15. The
only geometry that qualifies in a flat test world is the ground — exactly 1024 cells, which is the
constant that was showing up.

**This is the same class of bug M0 was created to fix.** The wall-base band was a *storage*
resolution bug; this is a *gather* resolution bug. Both come from asking a cube-level structure
about sub-voxel geometry.

⚠️ **Why the unit gate did not catch it — a real hole in the validation ladder.**
`LightWallMatrixTest.M3REDESIGN_BakeSettingsStillSealARoomAtEveryWallThickness` **passes**, and it is
not a bad test: the *tracer* genuinely seals rooms at every wall thickness. What is untested is
**which cells the tracer is invoked on**. L2 covers the trace; nothing covers the gather. A test can
be green, honest, and still leave the feature inert in the live engine.

**Candidate fixes, none free — the ordering trap is the reason it looks like this:**
1. Gather from sub-voxel occupancy instead of `m_solidVis`. ⚠️ Blocked by the ordering trap this
   document already records: `buildSubMicroOccupancy` runs **after** `rebuildCubeFaces`, so leaf
   sub-voxel occupancy does not exist yet at bake time (the same inversion logged as D5).
2. Gather from the **GPU occupancy pool**, which does hold sub-voxel data and is already flushed
   before `updateDirtyChunks()` by the M3-REDESIGN ordering rule. Needs a cheap cube-level
   "solid-or-mixed" query so the 32768-cell scan stays trivial.
3. Reorder the mesher so sub-voxel occupancy precedes the bake — the cleanest semantically, the
   largest blast radius, and squarely on top of D5.

**Gate (unchanged in spirit, plus the hole that let this through):** the same live rig — an
engine-generated `hall_house`, the slice at `y=18` — must show a dark interior region inside a field
of 15, with the exterior staying at 15 as the positive control; `last_sky_bake_cells` must exceed
1024 on a chunk containing a building, proving cells beside sub-voxel walls were actually gathered.
**And add a test that pins the GATHER, not just the trace** — otherwise the next fix is equally
free to be green and inert.

**D20 — FIRST-EVER SYNCHRONIZATION-VALIDATION RUN. Blur pass is CLEAN; two more sync defects found,
plus four other VUID classes.** *(2026-08-31, required by U5's close-out list.)*

**Method — reproducible, and worth keeping:** Release compiles validation out
(`VulkanDevice.h:760-769`, `enableValidationLayers = false` under `NDEBUG`), so the layer was forced
on through the **loader** instead of the application — no rebuild needed:
```
$env:VK_INSTANCE_LAYERS      = "VK_LAYER_KHRONOS_validation"
$env:VK_LOADER_LAYERS_ENABLE = "VK_LAYER_KHRONOS_validation"
$env:VK_LAYER_SETTINGS_PATH  = "<dir>\vk_layer_settings.txt"   # validate_sync = true, log_filename = ...
```
Bloom was turned ON for the run so the blur chain actually executes. SDK 1.4.321.1.

**Result — 48 KB of output, never previously seen because sync validation had never been run here:**

| message | × | verdict |
|---|---|---|
| `SYNC-HAZARD-READ-AFTER-WRITE` | 10 | **real — water render pass** |
| `SYNC-HAZARD-WRITE-AFTER-WRITE` | 10 | **real — scene render pass (depth)** |
| `VUID-VkImageMemoryBarrier-oldLayout-01212` | 20 | untriaged |
| `VUID-vkCmdDraw-None-09600` | 20 | untriaged |
| `VUID-vkCmdBlitImage-srcImage-00219` | 20 | untriaged |
| `VUID-RuntimeSpirv-OpEntryPoint-08743` | 6 | untriaged |
| `VUID-vkQueueSubmit-pSignalSemaphores-00067` | 4 | untriaged |

✅ **The bloom blur pass appears in NONE of them.** Every RAW cites `loadOp VK_ATTACHMENT_LOAD_OP_LOAD`
plus a depth attachment; the blur pass is `LOAD_OP_CLEAR` with no depth, and all 10 share one render
pass handle. That is independent corroboration, from a different instrument, that the U5 fix holds.

**D20a — scene render pass: depth is entirely absent from its dependencies.**
`createSceneRenderPass` (`PostProcessor.cpp:386-402`) declares both dependencies with stage
`COLOR_ATTACHMENT_OUTPUT` and access `COLOR_ATTACHMENT_WRITE` only — but attachment 1 is **depth**,
cleared via `LOAD_OP_CLEAR`, which writes at `EARLY_FRAGMENT_TESTS` with
`DEPTH_STENCIL_ATTACHMENT_WRITE`. Unsynchronized against the layout transition.
> *"clears the depth aspect of attachment 1 … it must allow
> `VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT` accesses at
> `VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT`"*

**D20b — water render pass: `loadOp = LOAD` is a READ, and the dependency only permits writes.**
`createWaterRenderPass` (`:1976-1983`): `colorAtt.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD` (`:1945`,
"keep the rendered scene; water blends over"), but `deps[0].dstAccessMask` is
`COLOR_ATTACHMENT_WRITE | DEPTH_STENCIL_ATTACHMENT_READ` — no `COLOR_ATTACHMENT_READ`.
> *"it must allow `VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT` accesses at
> `VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT`"*

Both are **pre-existing** — neither pass was touched by the U5 work, which changed
`createBlurRenderPass` only.

**Gate:** both hazards absent from a re-run of the same validation configuration, **and** the U5
determinism measurement re-run to confirm no regression. The four other VUID classes are **logged,
not fixed** — they are outside the lighting scope and need their own triage; recorded here so they
are not silently inherited. **Do not claim "the engine is sync-clean" on the strength of fixing 20
messages out of 90.**

### D20 RESULT 2026-08-31 — one of the two fixed and verified; the other NOT. Gate PARTLY met.

Same validation configuration, before vs after:

| message | before | after |
|---|---|---|
| `SYNC-HAZARD-READ-AFTER-WRITE` | 10 | **0 ✅** |
| `SYNC-HAZARD-WRITE-AFTER-WRITE` | 10 | **10 ❌ unchanged** |
| `VUID-VkImageMemoryBarrier-oldLayout-01212` | 20 | 20 |
| `VUID-vkCmdDraw-None-09600` | 20 | 20 |
| `VUID-vkCmdBlitImage-srcImage-00219` | 20 | 20 |
| `VUID-RuntimeSpirv-OpEntryPoint-08743` | 6 | 6 |
| `VUID-vkQueueSubmit-pSignalSemaphores-00067` | 4 | 4 |

**✅ D20b (water pass `COLOR_ATTACHMENT_READ`) — FIXED, verified 10 → 0.**

**❌ D20a (scene pass depth) — NOT demonstrated. The count did not move.** The edit is in
(`createSceneRenderPass` now names `EARLY_/LATE_FRAGMENT_TESTS` and
`DEPTH_STENCIL_ATTACHMENT_WRITE` in both dependencies) and is correct in principle — a pass with a
depth attachment must name depth in its dependencies, exactly as `createWaterRenderPass` already
did. But **it is not proven to have fixed anything**, and the remaining 10 hazards are in **two
other render passes**:
1. a pass whose colour attachment uses **`LOAD_OP_DONT_CARE`** — which the spec counts as a
   **write** (`COLOR_ATTACHMENT_WRITE`) — while its dependency permits only `SHADER_READ` at
   `FRAGMENT_SHADER`. The validator says so explicitly: *"according to the specification
   VK_ATTACHMENT_LOAD_OP_DONT_CARE is a write access."*
2. a second colour+depth pass clearing depth with the same shape as D20a.
Render-pass handles differ between runs, so the two cannot be matched against the "before" log by
handle. **Identifying which passes these are (shadow map / OIT / grade / post-process are the
candidates) is the outstanding work.** Do not assume the scene-pass edit was one of them.

**Determinism re-verified, and the harness verified with it.** At a fully static pose (free camera
45,40,45 pitch −75 — verified via `GET /api/camera`, no animated content in frame), bloom OFF and
bloom ON both measured **0.0000 spread, bit-identical across 9 samples over 90 s.**

⚠️ That result was *checked before being believed*, because a perfect zero is exactly what a stalled
capture looks like: two screenshots 5 s apart differ in **96 pixels, all within y 883–892 / x
360–373 — the FPS counter** — while the viewport crop differs in **0** pixels. The engine is
rendering; the viewport is genuinely identical. Note this pose is *stricter* than the earlier
before/after pair (which contained an idle-animating character), so it is corroboration, not a
replacement: **the matched A/B (0.085 / 3.983 → 0.107 / 0.122) remains the primary evidence.**

**D19 — M0 LEFT A STALE LIGHTING TEST RED, AND IT HAS BEEN RED ON THIS BRANCH EVER SINCE.**
*(Found 2026-08-30 by finally running the full suite — the clean pass this plan has owed throughout.)*

Release suite: **3713 ran, 3699 passed, 8 skipped, 6 FAILED.** Attributed, not lumped together:

| failing test | ×  | cause | mine? |
|---|---|---|---|
| `MaterialRegistryTest.HasCorrectMaterialCount` / `HasCorrectTextureCount` / `MaterialIDs_MatchJSON` / `GetAllMaterialNames_HasAll` | 4 | `materials.json` now loads **108 materials / 648 texture slots**; the tests hardcode **102**. `resources/materials.json` is **unmodified in the working tree** — committed drift on `main`. (`CLAUDE.md` also still says 102.) | **no** |
| `ForgeGateTeeth.AllowInvalidSkipsProgramGateEnforcement` | 1 | Already recorded as a known pre-existing red awaiting bisect (CityForge). | **no** |
| `FineFaceMerge.SubcubeMerge_CrossCubeSplitsOnLightBoundaryBetweenCubes` | 1 | **M0 fallout — see below.** | **no** |

**The FineFaceMerge one is ours, and the test says so itself.** Its comment
(`FineFaceMergeTest.cpp:659-663`) states the rig: *"a solid blocker cube 2 above column 0 shades its
+Y neighbour air cell (**BFS skylight ~14**) while column 1 stays open (15), so the two cubes' +Y
faces carry DIFFERENT light and must not fuse."* **M0 deleted that BFS flood.** With no flood, both
columns carry identical light, the two +Y faces legitimately fuse, and the assertion
`topSubFaces() >= 2` gets 1.

**This is a dead stimulus, not a broken mechanism** — and the distinction matters, so it was checked
rather than assumed: the sibling `SubcubeMerge_CrossCubeSplitsOnTintBoundaryBetweenCubes`, which
exercises the *same* cross-cube split path via tint, **passes**. So the merge key still splits
correctly; the rig just no longer produces a light difference to split on. There is no light-bleed
rendering bug here.

⚠️ **M0's own gate said "the existing lighting tests are updated to describe the new reality rather
than deleted." This test was missed, and nothing caught it because the full suite was never run.**
That is the actual lesson: the owed clean pass was not bookkeeping — it was the thing that would have
caught this weeks ago.

**Gate:** re-express the rig against the M3-REDESIGN *baked* sky visibility (the blocker must produce
a real baked difference in whatever path the unit test drives), **or** state explicitly that no
light-boundary split is reachable from a bare `rebuildAllFaces` and move the coverage to a level that
runs the bake. Do not simply relax the assertion to `>= 1` — that would delete the coverage while
appearing to keep it. Separately: fix the 102→108 material-count drift and `CLAUDE.md`'s stale
count (not this plan's work, but logged so it is not lost).

**D18 — ✅ FIXED AND GATED 2026-08-30. The game HUD WAS inside the graded image — bloomed and
tonemapped as if it were world geometry.** *(User-reported 2026-08-30: "the UI elements get bloom
around them. The UI elements should not get bloom, ever.")* Result and caveats at the end of this
entry; the diagnosis below is kept because the mechanism explains the magnitude.

**Verified by reading, not grep:**
* The in-game HUD is **not ImGui**. `resources/ui/default_hud.json` → `UI::UISystem` → `UIRenderer`,
  which builds its pipeline from `ui.vert.spv` / `ui.frag.spv` (`engine/src/ui/UIRenderer.cpp:493`).
  ImGui remains, but only for *editor* chrome.
* `RenderCoordinator.cpp:924` — `m_uiSystem->initialize(postProcessor->getSceneRenderPass())`. The
  HUD pipeline is created against the **scene** render pass.
* `RenderCoordinator.cpp:4013` — drawn between `beginWaterRenderPass` (3939) and
  `endWaterRenderPass` (4016), i.e. **into the offscreen HDR scene image**. The existing comment
  says so: *"on top of all geometry AND water, into the offscreen image."*
* `RenderCoordinator.cpp:4041` — `compositeToGrade` then runs bloom + `phxTonemap` over that image.
  **The HUD is therefore in the bloom source**, and every bright HUD pixel clears the bright-pass and
  gets a halo exactly like a torch would.
* `RenderCoordinator.cpp:4057` — ImGui draws *after* the composite. Editor chrome genuinely cannot
  bloom, which is why this looked impossible at first glance.

**Bloom is the visible symptom; the tonemap is the worse half.** Being in the scene pass means HUD
colours are exposed and AgX-curved. A health bar dims when the player walks into a dark room, and
re-deriving `m_exposure` (M4 / open question 5) would restyle the entire HUD as a side effect. UI
should be composited in display space, after the grade.

**Proposed fix, and the reason it is not a one-line move:** re-create the `UISystem` pipeline against
`postProcessor->getPostProcessRenderPass()` and draw it after `drawBlit` (4046), where ImGui already
draws. But `RenderCoordinator.cpp:924`'s placement is deliberate — drawing into the offscreen image
is what makes the HUD appear **inside the editor's docked viewport**, which is an ImGui image of that
texture. Moved to the swapchain pass, it would render over the whole editor window instead. So it
likely needs to be host-dependent: swapchain pass for `GameShell`/standalone, and for the editor
either kept in-scene or drawn into a separate un-graded overlay image.

### ✅ D18 unknown #1 RESOLVED by reading — and the defect is bigger than "the HUD blooms"

`shaders/ui.frag` writes `inColor.rgb` **straight through** — the authored widget colour from
`default_hud.json`, with no linearization and no grade compensation. So the HUD writes
**display-referred 0..1 values into a buffer that holds PHYSICAL RADIANCE**. That mismatch is the
whole bug, and it makes both symptoms quantifiable:

`PostProcessor.cpp:1529-1531` converts the authored bloom threshold into radiance units:
```cpp
const float exposureRel = (m_gradeExposure > 0.0f) ? m_gradeExposure : 1.0f;   // 8.0
bp.threshold = (i == 0) ? (m_bloomThreshold / exposureRel) : 0.0f;             // 1.0 / 8.0 = 0.125
bp.knee      = m_bloomKnee / exposureRel;                                      // 0.5 / 8.0 = 0.0625
```
Defaults: `m_gradeExposure = 8.0`, `m_bloomThreshold = 1.0`, `m_bloomKnee = 0.5`
(`PostProcessor.h:177,209-210`). **Effective bright-pass threshold on the scene image = 0.125**, with
the knee band spanning `[0.0625, 0.1875]`.

The same file states the physical scale (`PostProcessor.cpp:1525-1526`): *"a lit noon surface is
~0.02–0.2 and only the sun disc exceeds 1.0."*

| what | value written to the scene image | vs bright-pass threshold 0.125 |
|---|---|---|
| lit noon world surface | 0.02 – 0.2 (radiance) | at/below the knee — barely blooms. Correct. |
| HUD mid-grey panel | **0.5** (display value) | **4× over** — fully bloomed |
| HUD white text / bar | **1.0** (display value) | **8× over** — fully bloomed |

**So the HUD is not merely included in bloom — in most frames it is the BRIGHTEST thing in the bloom
source, brighter in bright-pass terms than the sunlit world.** Every HUD pixel above 0.1875 clears
the pass completely. That is why the halo is obvious and uniform across the whole HUD rather than
appearing only on bright elements.

The same mismatch drives the tonemap half: an authored 0.5 becomes `AgX(8.0 × 0.5) = AgX(4.0)`,
which is near-white. **HUD colours are already being rendered substantially blown out**, not merely
"shifted". The `default_hud.json` palette is not what anyone is seeing.

**Consequence for the fix:** moving the HUD to the post-process pass is not a risk to its
appearance — it is the *correction*. The current state is the distorted one; drawing after the grade
would show authored colours for the first time. Unknown #1 resolved in favour of the move. **Expect
the HUD to look different — darker and more saturated — and that difference is the bug being fixed,
not a regression.** Capture a before/after so the change is not mistaken for one.

### ✅ D18 unknown #2 RESOLVED by reading — the rect plumbing already exists on both sides

* `UIRenderer::render` sets **dynamic** viewport and scissor per draw
  (`UIRenderer.cpp:404-407`) and derives NDC purely from a push constant,
  `pc.scale = {2/screenWidth_, 2/screenHeight_}`, `pc.translate = {-1,-1}` (`:417-418`).
  Rendering into an arbitrary rect therefore needs an **origin offset**, not a pipeline or layout
  change — no descriptor churn, no recreation.
* The editor already tracks exactly that rect: `m_viewportPosX/Y`, `m_viewportSizeW/H`
  (`editor/src/Application.cpp:3006, 3967-3968, 5351-5352`), already used to remap mouse input and
  project overlays into the docked viewport.

**⚠️ Ordering constraint found while resolving this — it decides the call site.** The editor's
viewport is an **ImGui image of the offscreen texture**, drawn by `imguiRenderer->render()` at
`RenderCoordinator.cpp:4057`. A HUD drawn between `drawBlit` (4046) and ImGui (4057) would be
painted *underneath* that image and vanish in the editor. **The HUD must draw AFTER ImGui**, i.e.
between 4057 and `endPostProcessRenderPass` (4061).

### DECISION — one path, not per-host

Draw the HUD in the **post-process pass, after ImGui**, for both hosts, parameterised by a rect:
* **standalone / `GameShell`** — rect = full swapchain extent (unchanged appearance modulo the grade
  correction).
* **editor** — rect = `m_viewportPos/Size`, so the HUD lands inside the docked viewport as it does
  today.

This is preferred over the host-dependent split originally proposed above, and it is only possible
*because* unknown #2 resolved as it did. It also fixes the pre-existing "editor viewport is ungraded
while standalone is graded" mismatch already listed under U5 **for the HUD specifically**: the HUD
becomes ungraded in both hosts, which is the correct answer for UI in both.

**Remaining unverified — this is the gate, and it is a RUNTIME measurement:**
3. **The halo itself has not been measured.** The mechanism is established by reading; the *defect*
   is user-reported. Note that bloom ships default-OFF (U5), so this is only visible when bloom is
   enabled — which it was, left on by the U5 A/B rigs. The before/after A/B below is what closes it.

**Gate:** a fixed-pose A/B with bloom ON, HUD visible, measured with the noise-mask method — the
delta must land on the world and **not** on the HUD rect; plus a HUD-rect colour readout that is
identical at two different `m_exposure` values, proving the HUD left the grade. State the
editor-vs-standalone decision explicitly rather than fixing one host and leaving the other.

**Sequencing:** belongs with **U5**, which owns the grade and bloom, and must be settled **before
M4** re-derives `m_exposure` — otherwise M4 changes the HUD's appearance and nobody will know why.

### ✅ D18 IMPLEMENTED AND GATED 2026-08-30 (Release, editor host)

**Change:** `RenderCoordinator::initUISystem` now builds the HUD pipeline against
`getPostProcessRenderPass()` instead of `getSceneRenderPass()`, and the draw moved out of the water/
scene pass to **after `imguiRenderer->render()`** in the post-process pass. `UIRenderer` gained
`setViewportRect()` (dynamic viewport/scissor — no pipeline variant); the editor feeds it
`m_viewportPos/Size` every frame where it already records them, so the HUD lands inside the docked
viewport instead of over the whole window. Standalone leaves the rect unset = fill the target.

**Gate 1 — is the HUD out of the grade?** Change ONLY `m_exposure`, 8.0 → 1.5 (5.3×):

| rect | mean delta | % px moved | null-control floor |
|---|---|---|---|
| **HUD hp-bar** | **0.000** | **0.0%** | 0.000 |
| **HUD hotbar icon interiors (all 9)** | — | **0.00%** | — |
| WORLD grass (**positive control**) | **69.937** | **100.0%** | 0.415 |
| ImGui panel (was always ungraded) | 0.000 | 0.0% | 0.000 |

The HUD is **bit-identical** across a 5.3× exposure change while 100% of world pixels moved. The
positive control is emphatically alive, so this is not a dead-rig null.

⚠️ The whole-hotbar rect *did* move 2.612 / 7.9%. Chased rather than waved off: a column profile
showed movement only in narrow spikes at the ~42 px slot pitch, and splitting icon interiors from
the gaps gave **icon interiors 0.00% moved (all 9 slots) vs gaps 71.43%**. That is the graded world
showing through between slots — correct behaviour for UI composited over the scene, not leakage.

**Gate 2 — does bloom still touch the HUD?** Toggle bloom OFF → ON at threshold 1.0:

| rect | mean delta | % px moved |
|---|---|---|
| **HUD hp-bar interior** | **0.000** | **0.0%** |
| **HUD hotbar icon interior** | **0.000** | **0.0%** |
| WORLD grass (control) | 3.131 | 25.3% |

**Zero. Bloom cannot reach HUD pixels**, which is guaranteed by construction — the blur chain reads
the offscreen scene image, and that image is finished before the HUD is drawn.

⚠️ **What this rig could NOT resolve, stated because a caveat-free result here would be false.** The
zones *just outside* the HUD moved 4.6–11.2, i.e. more than the mid-screen world reference (3.1).
Those are world pixels, so the control was to repeat the bloom A/B with **the HUD hidden**:

| zone | HUD visible | HUD hidden | difference |
|---|---|---|---|
| hp-bar halo above | 11.228 | 7.375 | +3.853 |
| hp-bar halo below | 11.525 | 11.558 | −0.032 |
| hp-bar halo left | 2.719 | 4.710 | −1.991 |
| hotbar halo above | 9.018 | 10.492 | −1.475 |
| WORLD grass (same rect, no HUD near it) | 2.442 | 3.428 | −0.986 |

**Mixed signs, not a consistent reduction** — if the HUD were seeding bloom, hiding it would lower
every zone. The scatter (±2–4) is the same order as the world-reference rect's own run-to-run
scatter (0.99) on a rect with no HUD anywhere near it. **This world has animated grass, and that
animation floor is too high for this rig to resolve a faint halo in the surrounding zones.** The
architectural argument is what actually settles it (the HUD is not in the blur input); the
surrounding-zone numbers neither confirm nor refute a small residual. A still, grass-free scene
would be needed to measure that, and it has not been run.

**⚠️ NOT TESTED:**
* **Standalone / `GameShell` only reasoned about, not run.** Only the editor host was measured. The
  standalone path leaves the rect unset and should fill the window, but that is untested.
* **Window resize.** The post-process render pass survives `recreateSwapChain` (it is passed *in*,
  not recreated), so the pipeline stays valid — verified by reading. But `UISystem::resize` has **no
  call site anywhere**, so `screenWidth_/screenHeight_` are stale after a resize. **Pre-existing, not
  introduced by D18**, and now more visible since the rect maps HUD-logical space onto the viewport.
* **Menu screens** (`pause_menu`, `mainmenu_screen`, `settings_screen`) also run through `UISystem`,
  so they moved out of the grade too. Correct by the same argument, not visually checked.
* The HUD's own colours now render as authored for the first time. **Expect the HUD to look
  different — darker and more saturated than before.** That is the correction, not a regression.

---

## Open questions — to be measured, not assumed

1. **Do kinematic items and characters cast sun shadows today?** Verified that the sun pass
   rasterizes the *chunk* instance buffer; **not** verified for kinematic/item/character geometry.
   If they don't, the inconsistency is wider than described and M1 must cover it.
2. **Per-fragment tracing cost.** The dominant unknown. Must be measured on a real furnished city
   scene early in M1, not assumed. `NearShadowCascade.md` records that shadow cost tracks caster
   volume, not draw count — expect the same shape here.
3. **Which structure carries sub-voxel occupancy to the GPU.** A full micro-resolution volume is
   unaffordable (729× the cells); a two-level scheme — coarse cube bits to skip empty space, with
   729-bit detail only for genuinely mixed cells — is the obvious candidate but is unmeasured.
4. **Does M2 subsume ambient occlusion**, or is a separate short-range AO term still wanted for
   contact darkening?
5. **Exposure re-derivation.** `m_exposure = 8.0` was calibrated against the flood *and* against a
   skewed AgX curve later corrected in `20341333`; it was never re-derived after that fix.

---

## In-flight changes and what happens to them

Two changes from the 2026-08-28 debugging session are in the working tree. Both live *inside* the
approximation this plan deletes, so neither is a long-term answer:

* **Per-axis coverage opacity** (`ChunkRenderManager`) — replaced the scalar fill-fraction test so a
  thin full-face wall blocks and a decorative speck does not. Measured red→green: a 2-micro wall
  went from leaking 12/15 to 0. Real bug, separate from the reported one. **Recommend keeping until
  M3 removes the flood entirely.**
* **Cube-resolution DDA occlusion for point lights** (`LightOcclusionVolume` + `voxel.frag`) —
  fixes the flat-wall case but inherits the cube rounding, and the user observed a lit corner with
  it in place. **Recommend reverting before M0 baselines**, so the baseline measures the engine as
  it actually shipped rather than a half-migrated state. Its `LightOcclusionVolume` scaffolding may
  be reusable in M1.

Decision on both is the user's, not mine.

---

## Discipline for this work

Set explicitly because this session went wrong repeatedly without it:

1. **No claim without a number and a control.** "It looks right" is not a result; neither is a
   screenshot on its own.
2. **Baseline before fixing.** Measure the broken behaviour first, or there is nothing to compare to.
3. **One variable per rig.** Wall thickness or light position or light type — never several.
4. **State which system a result is about.** Sun, flood and forward lights are three different
   systems; a result about one says nothing about the others. Conflating them cost most of a day.
5. **Never declare something fixed.** Report what was measured and let the user judge.
## Debug-view verification per milestone

Every gate is checked with the per-system views (`POST /api/debug/shadow {"mode":N}`), because they
are the only way to see one lighting system in isolation per fragment. Each step names the mode,
the pose, and the expected reading — measured with `tools/lighting_stats.py` or a rect mean, never
described from the image.

| mode | shows |
|---|---|
| 0 | final composition | 
| 1 | shadow term only (white = lit, black = shadowed) |
| 3 | sky access | 4 = block light | 5 = forward point/spot | 6 = direct sun+moon | 7 = sky fill |

**M0 (done).** Mode 3 must read uniform white everywhere (placeholder skylight 15); mode 4 uniform
black (no block light). Mode 0 shows interiors flat and dim. *Verified: band cell `sky=0 → 15`;
band region 1.9 → 24.5 vs wall 173.9 → 18.2, matching `flood_bypass mask=3`.*

**M1 — occupancy on the GPU.** Add **mode 8 = occupancy hit**: shade each fragment by whether the
GPU structure reports its own cell solid, and at what level (cube / subcube / microcube). A wall
must read solid at the resolution it was built at; the 2-micro timber wall and 3-micro floor must
appear as *partially* filled cells, not as fully solid ones — that distinction is the whole point of
M1 and is invisible in every other view. Compare against `GET /api/debug/occupancy_cell` for the
same cells so the visual and the numeric agree.
⚠️ Adding a mode **requires raising the clamp** in `set_debug_shadow_mode`, or the new mode silently
selects the previous one — a documented footgun that has already bitten once.

**M2 — point-light visibility.** Mode **5** is the gate, because it isolates exactly the system
being built.
1. Sealed windowless room, light inside: mode 5 must be **black on every exterior surface** and
   non-black on interior surfaces. Positive control: move the same light outside — the wall must
   light up. A zero with a dead control is not a result.
2. Doorway room: mode 5 shows light through the opening only, with the wall beside it dark.
3. Moving light (carry a torch, or move it via the API): mode 5's lit region must **track the
   light**, and the shadow edges must move with it. This is the "moving shadows" claim and it is
   checkable frame to frame.
4. Mode 0 must equal mode 5 + mode 6 + mode 7 within tolerance, or something is double-counting.

**M3 — sky as an emitter.** Mode **3** becomes the traced sky visibility rather than a stored field.
1. Sealed box: mode 3 reads **black** throughout the interior.
2. One-window box: mode 3 shows a gradient falling off from the opening — and critically, *not* the
   flood's linear 1-per-cell ramp. Record the falloff curve as the evidence.
3. Interior/exterior ordering must match the measured baseline: sealed < window < door < open-roof.
4. Mode 7 (sky fill) must go dark in a sealed room, where today it does not.

**M4 — transport retired.** Mode 0 A/B against the M3 captures at fixed poses, with
`lighting_stats.py --compare`. Any per-surface delta means something still read the vertex words.

**M5 — bounce.** Add a **bounce-only view** so indirect can be judged separately from direct; the
gate is that a window-lit room's corners fill in without direct light reaching them.

---

## M1 RESULT — sub-voxel occupancy is on the GPU and tracks live edits (2026-08-29)

**What was built.** `VoxelLightOccupancy.{h,cpp}` flattens `Physics::VoxelOccupancyGrid` into
`ChunkLightOccupancy` (solid bits / mixed bits / per-mixed-cube 729-bit micro masks) and packs many
chunks into `PackedOccupancyPool` (a 2048-entry directory + a word pool). `packedPoolSolidAt()` is
the **CPU mirror of the GLSL M2 will write**, so the addressing is unit-tested before any shader
exists. `VoxelLightOccupancyGpu` owns two host-coherent, persistently-mapped buffers and repacks on
dirty. `VoxelOccupancyGrid` gained a `revision()` counter, bumped by every mutator, so
`RenderCoordinator::updateLightOccupancy()` re-flattens **only changed chunks** (budgeted 24/frame)
and drops departed ones.

**Measured, live, on the running engine** (`GET /api/debug/light_occupancy?x&y&z`, which reports
per-micro agreement between the GPU pool and `Chunk::getOccupancyGrid()` over all 729 cells):

| step | target cell (5,20,5) cpu/gpu | control cell (7,20,5) | disagreements |
|---|---|---|---|
| baseline | 0 / 0 | 0 / 0 | 0 |
| + 1 microcube | **1 / 1**, `partial:true` | 0 / 0 | 0 |
| + 1 full subcube | **28 / 28**, `partial:true` | 0 / 0 | 0 |
| − that subcube | 0 / 0 | 0 / 0 | 0 |

Predictions were written before running; 1, 28 and the control's constant 0 were all as predicted.
The whole rig was **re-run after the box was made viewer-relative** and read identically (28/28,
control 0), so that change did not disturb the mirror.
`mixed_cubes` moved 2→3→2 and `pool_words` 2097→2121→2097 (+24 = 1 index word + 23 micro words),
i.e. the blob grew and shrank by exactly the documented layout. Repack cost 0.04–0.05 ms.
Unit tests: **15/15** (`VoxelLightOccupancyTest`), including
`PartiallyFilledCellsAreRepresentedNotRounded` — the 3-micro floor slab that caused the wall-base
band reads solid in its bottom third and **open above**, which is the whole point of M1.
Storage measured: 8192 solid cubes = 8,192 B/chunk; 256 two-micro wall cells = 32,768 B; a dense
micro bitfield would be 2,985,984 B.

### M1 close-out (same session) — seams, overflow, eviction, GPU plumbing, mode 8

**Seam-freedom (the gate I had skipped).** `ChunkedOccupancyEqualsTheWholeRegionAcrossEverySeam`
builds 2×2×2 chunks meeting at the world origin — straddling the x, y **and** z seams at once, half
of it at negative coordinates — from a **pure world-position predicate**, and asserts every micro
cell in the band matches that predicate. The reference is arithmetic on world coordinates, not a
second data structure, so it cannot share a bug with the thing it checks.
`TheSameShapeReadsTheSameWhereverTheChunkSeamsFall` adds the FloraMarginTest half: a 40-cube run of
2-micro wall + 3-micro floor, built chunk-aligned and again at offset (17,13,5), must read
identically relative to its own origin. Both carry not-empty / not-all-solid controls.
**RED-BEFORE-GREEN, actually run:** reverting `floorDiv` to truncating division turned the seam test
red (along with `PackedPoolAgreesWithTheGrid`, `DirectoryIndexingHandlesNegativeChunkOrigins` and
`BoxOnlyMovesInWholeChunkSteps`); restoring it returned all to green. The test has teeth.
Cost note: the seam test first ran in **146 s** because it authored eight full chunks at micro
resolution (~191M predicate evaluations); bounding authoring to the queried band cut it to ~2 s.

**Overflow and eviction now tested.** The overflow policy was extracted to a pure function
(`selectChunksThatFit` + `blobWords`) so it is testable without a device — and so the flush is one
pack instead of a repack-per-dropped-chunk loop. `PoolOverflowDropsWholeChunksAndNeverTruncatesOne`
proves surviving chunks still answer correctly after a drop, that zero capacity drops everything,
and — the control — that ample capacity drops nothing. `MovingTheBoxForgetsChunksItNoLongerCovers`
covers residency: out-of-box `setChunk` is refused rather than silently stored, moving the viewer
forgets what the box left behind, returning does not resurrect it, and `removeChunk` is idempotent.
**19/19 unit tests.**

**On the GPU, and read by a shader.** Bindings **11 (directory) and 12 (pool)** appended at the END
of the set-0 layout; both descriptor pools corrected — and while doing so, the *reflection* pool was
found still one sampler short from when binding 10 was added (it counted 1,2,5,6,7,9 but the layout
declares binding 10 too), the same class of mistake that doc warns about, now fixed. The occupancy
is created in the RenderCoordinator constructor *before* the descriptor write, so no in-flight
descriptor set is ever rewritten. Absent occupancy falls back to the light SSBO — a real, valid
buffer — guarded by `ubo.occupancyBox.w`, so it is inert rather than wrong. `phxOccupancySolid()` in
`voxel.frag` is the line-for-line mirror of the unit-tested `packedPoolSolidAt`.
**Zero Vulkan validation errors at runtime.**

**Measured at scale, on a loaded project (CharacterTestbed), which the earlier empty world could not
reach:** 73/73 chunks resident, **15,008 mixed cubes**, 509,769 pool words = **2.04 MB of the 64 MB
pool (3%)**, **0 dropped**, full repack **3.65 ms** when dirty. The per-frame budget was observed
doing its job (24 resident, then 73/73). Agreement sweep over 204 cells: **0 disagreements**.

**Debug mode 8 (occupancy hit) built**, and the clamp raised to 8 with a comment saying to raise it
again next time. Measured, not eyeballed — pixels classified by dominant channel:

| view | red-dominant | green-dominant | blue-dominant |
|---|---|---|---|
| mode 0 (control) | 3 (0.00%) | 3,450 (0.24%) | 99,719 (6.92%) |
| mode 8 | **186,408 (12.95%)** | 2,861 (0.20%) | 99,719 (6.92%) |

Blue is **identical** in both views, so it is sky/UI rather than mode-8's out-of-box colour: nothing
visible reads as uncovered. Green is unchanged from the control, i.e. effectively zero.
⚠️ Note the first "pure red" count returned 0 and was wrong — mode 8's output passes through AgX, so
saturated red arrives as (255,198,188). Classify debug views by dominant channel, never by exact RGB.

**This corrected my own description of mode 8.** I had documented green as "a partially filled
cell's empty micro". It is not: the probe steps half a micro *into* the surface, so partial cells
read red on their own faces. Green means a surface is drawn where the occupancy says its cell is
empty — a **mesh-vs-occupancy disagreement**, i.e. the defect colour (gap 15's `remove_subcube`
desync produces it). The shader comment was corrected rather than left to mislead later work.

---

## M2 RESULT — point/spot lights now have a visibility term (2026-08-30)

**What was built.** `phxLightVisibility()` in `voxel.frag` marches the M1 occupancy from the surface
to each point/spot light and drops the contribution when anything solid is between them. Both
forward loops call it, gated on `dot(N, ldir) > 0` and (for spots) `spotFactor > 0` so no march runs
where the contribution would be zero anyway. `packedPoolLightVisibility()` is its CPU mirror, unit
tested, and reachable live at `GET /api/debug/light_occupancy?x&y&z&lx&ly&lz&nx&ny&nz`.
A/B switch: `?trace=0|1`.

**The gate, answered as DATA rather than from an image.** Rig: an 11×15 stone floor with a stone
wall across z=21, a light at (21,20,18) on the near side, floor normal +Y. Every prediction written
before running:

| surface point | expected | measured | occluder named |
|---|---|---|---|
| z=19, same side, open | visible | **visible** (17 steps) | — |
| z=15, same side, distant | visible | **visible** (30 steps) | — |
| z=23, just beyond the wall | blocked | **blocked** | cube **[21,18,21]** |
| z=25, beyond the wall | blocked | **blocked** | cube **[21,18,21]** |
| x=16,z=26, corner beyond | blocked | **blocked** | cube **[18,19,21]** |

Every occluder reported sits at **z=21 — the wall**, so this is not a coincidental block.
Unit tests (23/23 total): a wall blocks and open air does not (with control); a flat floor does not
shadow itself at grazing angles; **a 2-micro wall occludes** — the case a cube-resolution test could
never see, and the reason M1 stores sub-voxel occupancy at all.

### Two defects found by measuring, that I would otherwise have shipped

1. **A fixed step with a fixed cap silently reported "visible".** Probes returned `steps=96` — the
   cap — meaning the march stopped short and defaulted to lit. With a radius-22 light, every
   surface beyond ~10.7 u was lit **straight through walls**: the exact defect M2 exists to remove,
   reintroduced at range. Fixed with an adaptive step (`stepLen = max(1/9, span/96)`) so the march
   ALWAYS reaches the light. Within 10.7 u it stays at micro resolution; beyond, it coarsens, so a
   very distant 2-micro wall can be stepped over — a deliberate, documented trade, because reaching
   the light coarsely beats not reaching it at all. Pinned by
   `ADistantLightStillGetsOccludedRatherThanRunningOutOfSteps`, which also asserts near lights keep
   full micro resolution so the coarsening cannot leak inward.
2. **The ray origin used the normal-mapped normal.** `N` in `voxel.frag` is normal-mapped; offsetting
   the ray start along it can slide the origin along the surface or back into it. Now offset along
   the geometric face normal `Ng`, while shading still uses `N`.
   ⚠️ **Honest correction:** I changed this expecting it to explain a measured 79/21 split, and
   **it did not** — the numbers were unchanged (78.9% vs 79.0%). The change is still correct, but
   my acne hypothesis was wrong and the screen-space split was simply the wrong instrument. What
   actually explains that split is legitimate occlusion of distant grazing rays by terrain relief
   and by the rig floor's own raised edge — confirmed by probing: a point at (10,17,10) is blocked
   by cube [15,18,14], the floor edge, exactly as a real step should shadow lower ground.

### Method notes worth keeping

* **A screen-space A/B could not settle this.** On a real world it mixes wall shadow with terrain
  relief and cannot separate them; the point-probe endpoint was built because of that, and it
  answered in seconds what pixel counting could not answer at all.
* **My first M2 A/B had a DEAD CONTROL.** Mode 5 with the light removed read 51.51 mean luminance
  versus 51.55/51.66 with it — i.e. the measurement was of grass and foliage, which render normally
  in every debug mode, and contained no light at all. Always capture the no-light control.

### M2 caveats

* **No valid GPU cost measurement.** Debug frame time was 10.87/11.10 ms trace-off vs 10.91/11.01 ms
  trace-on — indistinguishable, but that is **not** evidence the trace is cheap: Debug is CPU-bound
  and `gpuFrameTime` mirrors `cpuFrameTime` (the documented trap). A real number needs Release plus
  `GpuProfiler` scopes, on a furnished city, and has NOT been taken. This is the plan's own "measure
  M2 cost early on a real furnished scene" requirement, still outstanding.
* **Verified through the CPU mirror, not by reading back the GPU.** The GLSL and C++ marches are
  line-for-line equivalent and the GPU path demonstrably changes the image (47.7% of added light
  blocked on the rig), but no shader-side readback confirms the GLSL march step for step.
* **Characters, grass and foliage are unaffected** — they use their own shaders. A torch will not
  yet cast onto or be occluded by a character.
* The sealed-room and moving-light gates from the plan are **not** run; the wall rig covers the
  same mechanism but is not the same test.

---

## M3 RESULT — the sky is an emitter; enclosure is traced, not flooded (2026-08-30)

**What was built.** `phxSkyVisibility()` in `voxel.frag` traces 9 fixed cosine-weighted hemisphere
directions against the M1 occupancy and returns the fraction that escape. It feeds both
`phxSkyGate` and `phxAmbientAtmos`, and **mode 3 now shows the traced value** instead of the stored
field. `packedPoolSkyVisibility()` is its CPU mirror, unit tested and live at
`GET /api/debug/light_occupancy?sky_probe=1&x&y&z`. A/B switch: `?sky=0|1`.
The occupancy flag `occupancyBox.w` became a **bitfield** — 1 readable, 2 light trace, 4 sky trace —
so M2 and M3 can be toggled independently against one scene.

This is what replaces the deleted flood. The flood decayed 1 per cube cell from the nearest opening
(measured: `14,13,12,11,10,9,8,7` — 47% of full daylight eight cells from one doorway) and a sealed
room could still be bright. Now enclosure is a property of geometry.

**Live gate — one room, one variable, measured as data (not from an image):**

| probe | sealed | after cutting a 3×3 doorway |
|---|---|---|
| interior **near** the opening (25,19,19) | **0.0000** | **0.0774** |
| interior **far** from it (17.5,19,19) | **0.0000** | **0.0000** |
| open ground (5,17,40) | 1.0000 | 1.0000 |
| open ground (40,34,40) | 1.0000 | 1.0000 |

Ordering **sealed < opening < open** holds, and the falloff is geometric: cutting the doorway lights
the near interior and leaves the far interior untouched — not a linear per-cell ramp.

**GPU-side confirmation** (the probes above run the CPU mirror). Camera inside the room, mode 0,
sky tracing off vs on: interior mean luminance **30.10 → 18.33 (−39%)**, near-black pixels
**46.4% → 82.4%**. The shader is darkening real enclosures.

Unit tests (25/25): sealed = 0, open = 1, opening in between with falloff; and
`SkyVisibilityIsBlockedBySubVoxelCeilingsNotJustFullCubes` — a **2-micro ceiling** blocks the sky,
with a roof-removed control proving the zero came from the ceiling rather than the walls or the
reach limit. A cube-resolution test could not see that ceiling at all.

### What measuring changed — REACH, not resolution, decides "sealed"

The first version used a uniform 1/9 step with a 96-step budget, i.e. ~10.7 u of reach. A diagonal
ray inside a **9×7×9 sealed room** then ran out of budget without hitting anything and was counted
as sky: the far corner read **0.077 instead of 0**. Resolution was never the problem — range was.
Fixed with a two-rate march (micro resolution for the first 3 u where 2-micro walls and ledges live,
1/3 u beyond) reaching 24 u. The corner now reads 0.0000.
⚠️ The horizon is still real and is now the documented knob: **a room larger than the reach reads as
partly sky-lit even when sealed.** Raise `reach` if large halls look wrong.

Also corrected: my first window test asserted a floor probe could see a window set 3 u up the wall.
It cannot — the hemisphere tilts at most 60° off vertical. The test was wrong about the geometry,
not the code; it now uses a doorway that reaches the floor, which is also the plan's own ordering.

### On the test world — CharacterTestbed made the gates WORSE

M1's scale evidence needed it: an empty world had 2 mixed cubes and would not drive the camera,
while CharacterTestbed gave 74 resident chunks and 16,832 mixed cubes, which is what exercised the
per-frame budget, residency, and the partial-cell audit. For M1 it was the right call.

For the M2/M3 **gates** it was the wrong instrument, and it cost real errors — every one of these
traces to using an uncontrolled world instead of the small, single-variable rig `CLAUDE.md` requires:

* **A dead control.** Mode 5 read ~51.5 mean luminance *with the light removed* — the crop was
  grass and foliage, which render normally in every debug mode. The first M2 A/B measured nothing.
* **Wrong probe heights.** A sky probe at (40,17,40) read 0.0 and looked like a defect; the surface
  there is y=33, so the probe was buried 16 voxels inside solid ground.
* **Refused rig voxels.** 46 of 442 sealed-room cells and 5 of 209 wall-rig cells were refused
  because terrain already occupied them — so the rig was not the shape I specified, and the sealed
  room had to be re-verified rather than assumed.
* **Contamination across steps.** M3's interior capture still contained M2's test point lights,
  which is part of why the sealed interior did not read fully black.
* **Unknown provenance.** The 16,832 mixed cubes are whatever that project happens to contain; I
  never established what geometry the gates were actually measuring.

Hence **D2**. The scale work should keep using a populated world; the gates need a built-for-purpose
one.

### M3 caveats

* **Still no GPU cost number**, and M3 is far more expensive than M2: 9 rays × up to ~90 samples per
  fragment, versus one march per light. This is now the single most important outstanding
  measurement in the whole plan — it may well force a different structure (precomputed per-voxel
  sky visibility, or folding this into M5's cascades) rather than per-fragment tracing.
* **Sky visibility is per-fragment and unfiltered**, so banding or noise at the 9-direction
  resolution has not been examined at all.
* Interiors did not go fully black in the live capture (mean 18.3): the scene still held the M2 test
  point lights and the room now has a doorway. Not a clean "sealed room is black" capture.
* `vSkyLight` (the M0 placeholder, constant 1.0) is still multiplied in, deliberately, so M4 can
  A/B before deleting the vertex path.
* Characters, grass and foliage still use their own shaders and get no traced sky.

### Caveats from M1 — what was NOT verified, and one design limit that had to be resolved

1. **The directory box was world-fixed and far too small — FIXED the same session, box now follows
   the viewer.** As first built it covered 16×8×16 chunks = 512×256×512 world units centred on the
   origin; this repo's own `worlds/default.db` has placed objects at **x≈611–635**, already outside
   it, and a WorldForge world spans kilometres. Chunks outside the box occlude **nothing**, so a
   settlement past the edge would light as though it had no walls. Now: the box is **32×16×32
   chunks (1024×512×1024 u)** and `boxMinChunk` is recentred on the camera each frame, snapped to
   whole chunks so ordinary camera motion does not repack — the same player-following-region
   pattern the water sim uses. Every index function takes the box explicitly, so no caller can
   silently assume the old constants. Verified live at spawn: centre `(45,55,45)` → chunk (1,1,1)
   → `box_min_chunk (-15,-7,-15)`, correct. Covered deterministically by
   `RecentringTheBoxCoversGeometryFarFromTheWorldOrigin` (a chunk at x=608 reads *not solid* from
   the origin-centred box and *solid* from a box centred at x=620, **with the reverse control** —
   the origin chunk then drops out, proving the box trades regions rather than covering both) and
   `BoxOnlyMovesInWholeChunkSteps`.
   ⚠️ **What is still NOT verified: the box moving in response to camera motion at runtime.** The
   attempt failed for an unrelated reason worth recording — `POST /api/camera` writes the position
   to **`InputManager`, not `Camera`** (`set_camera` handler), and the render `Camera` only picks
   it up during the normal per-frame update, which neither the project-launcher modal nor a hung
   project boot ever reached. `GET /api/camera` reads back the InputManager value, so it reported
   x=2000 while the render camera sat at (45,55,45) — the two disagree, and reading the GET alone
   would have produced a false "the box didn't follow" conclusion. The box is fed
   `camera->getPosition()`, which is the **same source chunk culling, far terrain and the shadow
   centres all use**, so it is consistent with the rest of the renderer by construction.
2. ~~Verified against one chunk only~~ — **CLOSED**: 73/73 chunks, 15,008 mixed cubes, budget and
   residency observed live; overflow and eviction now have unit tests. See the close-out above.
3. ~~No shader reads any of this~~ — **CLOSED**: bindings 11/12 + `phxOccupancySolid()` + mode 8,
   zero validation errors. Still open: **no frame-cost measurement** of the occupancy path, because
   nothing yet traces with it — mode 8 is a single lookup per fragment, not a ray march. The repack
   cost IS measured (3.65 ms when dirty, at 73 chunks) and is the one number to watch: during heavy
   streaming that lands every frame, and it is the first thing to optimise if it hurts.
4. ~~Mode 8 not built~~ — **CLOSED**, with its semantics corrected (see close-out).
5. **The partial-cell case is proven on the CPU but NOT on the GPU.** The 204-cell live sweep found
   **0 partial cells** in the sampled region, so the agreement it demonstrated was over fully solid
   and fully empty cells only. Partial cells are covered by unit tests and by the earlier 1/729 and
   28/729 live readings, but no *shader-side* read of a partial cell has been confirmed. Mode 8
   cannot show it either (the probe steps into the surface). Closing this needs a sweep aimed at
   where the 15,008 mixed cubes actually are.
6. **No deliberate-disagreement control was run for mode 8.** Green read ~0, which is the healthy
   answer — but a zero with an unproven detector is weak. The clean control exists and is cheap:
   trigger gap 15 (`remove_subcube`) in view and confirm green appears. Not done.
7. **Found, pre-existing, not caused by this work:** `remove_subcube` leaves the physics occupancy
   grid and the chunk's real content **disagreeing**. After removing subcube (2,2,2),
   `occupancy_cell` reports `content.micro_slots: [{slot:[0,0,0], count:1}]` while `grid` reports
   `cube_filled:false` and no subcubes at all — `markSubdivided(false)` erases all 27 micro masks
   and nothing restores the surviving microcube. The mirror faithfully reported what the grid said
   at every step; the **grid** under-reports. This also affects character collision, so it is
   logged in `docs/StructurePipelineGaps.md` rather than worked around here.

⚠️ **Known limitation of the views themselves:** they exist only in `voxel.frag`. Grass, foliage,
water and characters render normally in every mode, so anything lit in a debug view that is *not* a
chunk voxel is showing its ordinary shading, not the isolated system. I misread this once already.
Extending the modes to `character.frag` and `grass.frag` is a prerequisite for trusting M2's gate on
characters.



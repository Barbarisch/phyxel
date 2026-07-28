# Water System v3 — Look & Flow

> Status: **Phases 1, 2 and 3 COMPLETE + L4-verified** (2026-07-27).
> This is the **fidelity** plan for water:
> making it *look* like water and *flow* like water. It is the successor to
> [`docs/WaterSystemV2.md`](WaterSystemV2.md), which was the **scale** plan (player-following
> region, baked hydrology, rivers/lakes/oceans) — v2 remains accurate for what shipped there and
> its open items still stand. v1 ([`docs/WaterSystem.md`](WaterSystem.md)) remains the design
> rationale for the CA rules and the implicit-ocean model.
>
> **v2 asked "where can water exist?" — v3 asks "does it read as water?"**
>
> Standing discipline applies: ground every dimension (grounding-auditor), red-before-green +
> solution-auditor on every "works/fixed" claim, a stress phase per feature, and a named validation
> layer per deliverable (**L1** artifact exists · **L2** structural invariant measured on real
> output · **L3** functional agent simulation · **L4** live-engine runtime). For rendering work the
> L4 evidence standard is **same-vantage before/after captures plus a pixel probe** — the precedent
> is the v2 ocean-slab fix (mean B−R over a fixed crop). A screenshot alone is never evidence.

> ### Progress
>
> **Testbed: the `WaterLab` project** (`Documents/PhyxelProjects/WaterLab`, generated from
> `samples/game_definitions/water_test.json`). Launch with
> `launch_engine args:["--project","<...>/WaterLab"]` — never bare. `seaLevel: 54` is MEASURED,
> not assumed: terrain-v2 puts this world's surface at y≈49-70, so the engine default
> `kSeaLevelY=16` sits ~35 voxels underground and shows no water at all.
>
> **SHIPPED 2026-07-27 (Phase 1 items 1-4):**
> - **Post-scene water pass** — `PostProcessor::{createWaterRenderPass,createWaterResources,
>   beginWaterRenderPass,endWaterRenderPass}`. Colour LOADs the finished scene; depth is bound
>   READ-ONLY, so it is depth-tested AND sampled in the same pass with no copy. Both water
>   pipelines moved out of the scene pass (`RenderCoordinator::drawFrame`).
> - **The HUD moved with it.** The custom UI used to draw last in the scene pass; water drawing
>   afterwards would have painted over it, so the HUD now draws at the END of the water pass.
>   (Pipelines built against the scene pass are render-pass-COMPATIBLE with the water pass —
>   identical attachment formats/counts — so nothing is rebuilt.)
> - **Half-res refraction snapshot** — `PostProcessor::captureRefraction` blits scene colour to a
>   half-res sampled image between the passes.
> - **`depthSampler` ownership moved** from the SSAO path (which is OFF by default, so it never
>   existed) to `createOffscreenResources`.
> - **Shared shading module** `shaders/water_common.glsl`, included by both `water.frag` and
>   `water_cell.frag`: depth-buffer thickness, Beer-Lambert absorption, screen-space refraction
>   with a depth-rejection test, sun-driven sky/specular, soft-shoreline alpha.
> - **Scene UBO at set 0** on both water pipelines (set 1 = refraction/depth/reflection), so water
>   reads the live sun direction/colour and ambient. NOTE: water still builds its own `viewProj`
>   from the camera because it is authored in ABSOLUTE world space — `ubo.viewProj` is the
>   camera-RELATIVE one. Only the UBO's sun/ambient and its (convention-independent) view rotation
>   + projection are consumed.
> - **Resize fix (pre-existing defect, found while wiring):** the water pipelines were never
>   recreated on swapchain resize, so their static viewport/scissor stayed baked at the old extent.
>   They are now recreated (and their set-1 descriptors re-pointed at the recreated images).
>
> **L4 evidence (WaterLab, fixed vantage, Debug):**
> - Refraction + absorption: the seabed is visible through the shallows with a depth gradient
>   (screenshot `20260727_171339_999`); the per-cell renderer shows the tub floor through a
>   1-cell-deep pool (`20260727_171559_777`) — both pipelines render with the new set layout, no
>   artifacts.
> - **Day/night MEASURED:** water-crop mean luminance 145.75 at noon vs 79.60 at midnight
>   (**1.83×**), crop (900,60)-(1150,200) of `20260727_171339_999` / `20260727_171350_625`.
> - No Vulkan or rendering errors in `phyxel.log` (only the pre-existing `ScriptingSystem`
>   "No module named 'phyxel'"). All 44 `Water*` unit tests pass.
>
> **UNDERWATER STATE SHIPPED (Phase 1 item 5), 2026-07-27:**
> - `shaders/water_underwater.frag` + a second pipeline in `WaterRenderPipeline` sharing the sea
>   plane's layout (fullscreen triangle from `post_process.vert`, depth test OFF so it also covers
>   the sky and the underside of the surface). Drawn at the end of the water pass, before the HUD.
> - `RenderCoordinator::cameraSubmergence()` decides submersion: **the SIM is authoritative while
>   the camera is inside its region** (it knows lakes at any altitude and is connectivity-gated, so
>   a sealed dry cavity below sea level correctly reads DRY), falling back to sea level outside it.
>   Fades over a 0.35-voxel band so breaking the surface doesn't pop.
> - `water_stats` now also reports `plane_enabled`, `render_sea_level`, `camera_submergence`,
>   `camera_depth_below`, `surface_cells` — the render-side state an L4 check needs.
> - **L4 measured (WaterLab, camera (-32,51,-32), 4 voxels under):** the far-field underwater colour
>   matches the shader's hand-computed prediction at BOTH times of day — noon measured
>   (72,141,172) vs predicted (69,138,163); midnight measured (7,24,32) vs predicted (7,25,33).
>   Those constants exist only in this shader, so this identifies the overlay specifically rather
>   than "the scene got darker". Probe reported `camera_submergence 1.0, depth_below 4.0`.
>   **Process note:** my first read of the underwater screenshot called it "not working" — the
>   fogged far field looks like a plausible sky. The numeric prediction is what settled it; do not
>   trust eyeballing for this class of change.
>
> **PERF A/B — measured, no resolvable regression (2026-07-27):**
> Protocol: **Release** builds (Debug lies — see [[project_character_pipeline_scaling]]'s measurement
> rules), same WaterLab scene and pinned noon lighting, 4 fixed vantages, 25 warm-up frames then 30
> samples of total frame time from `/api/debug/frame_profile`. Baseline = this work `git stash`ed
> (the tracked `.spv` files revert too, so the baseline runs its original shaders). Probe script:
> `scratchpad/water_perf_probe.py`.
>
> | vantage | before | after | delta |
> |---|---|---|---|
> | coast_above | 1.581 ms | 1.583 ms | +0.2% |
> | open_water | 1.270 ms | 1.242 ms | −2.3% |
> | submerged | 1.268 ms | 1.210 ms | −4.5% |
> | land_only | 1.181 ms | 1.236 ms | +4.6% |
>
> **Read this honestly: the deltas are NOISE, not a result.** Within-run spread (p90−p10) is
> 0.46–0.83 ms, i.e. 35–70% of the median, and the signs go both ways (two vantages got *faster*,
> which added work cannot do). The correct conclusion is an upper bound: **at this scene's cost
> (~1.2–1.6 ms/frame, 600–800 FPS, 16k faces) the water pass + half-res blit cost less than the
> ~±0.06 ms noise floor — no measurable regression.** `land_only` is the vantage where the fixed
> blit cost should show (the blit runs whenever water draws, visible or not) and it is the largest
> positive delta, consistent in sign with expectation but NOT resolved above noise. A real per-pass
> number needs GPU timestamps (the F7 panel's `GpuProfiler` results are not exposed via the API) or
> a far heavier scene.
>
> ### Phase 2 — GERSTNER SWELL, SHIPPED 2026-07-27
>
> - **The sea is no longer one quad.** `WaterRenderPipeline` now builds a camera-centred RADIAL grid
>   (96 rings × 128 sectors = 12,289 verts / 24,320 tris, static, indexed). Radial because the sheet
>   spans ~2× the render distance: a uniform grid would spend its vertices on the horizon where a
>   wave is sub-pixel and starve the water near the viewer where the shape reads. Ring radius grows
>   as (r/R)² so density follows the camera — the cheap approximation of a projected grid.
> - **Gerstner (trochoidal) waves** in `water.vert`: 3 components at spreading angles with halving
>   amplitude, steepness summing to 0.75 (< 1, or the surface self-intersects into a shimmering
>   knot at the crest). Deep-water phase speed `c = sqrt(g/k)`. Normals are **analytic** from the
>   wave derivatives, so they stay correct on the coarse outer rings.
> - ⚑GROUND: amplitude 0.45 voxel / wavelength 14 voxels ≈ a 0.9 m swell on a 14 m period —
>   Beaufort 4, "clearly alive, not stormy". Disc radius 0.75 × sheet size: the old quad was a
>   SQUARE whose diagonal reached ~1.41× its half-extent, so a disc at 0.5× would leave an arc of
>   missing water in the screen corners (far plane at 0.5×, diagonal half-FOV ~30° ⇒ need ≥0.58×).
> - **Displacement fades to zero at the rim**, so the sheet's edge sits exactly at sea level and
>   meets the far/near boundary flush — and horizon waves stop aliasing.
> - **New `water_waves` debug command** {amplitude, wavelength, wind}. Amplitude 0 restores the flat
>   pre-Phase-2 sheet; that is the A/B control below and the escape hatch if waves misbehave.
>
> **A SECOND DEFECT FOUND AND FIXED IN THE SAME PATTERN AS PHASE 3'S.** The first version passed
> `flowDir = 0` for the sea, so the whitecap pattern never advected — it froze into a static
> world-space pattern that read as painted-on diagonal corduroy. The sea has no per-CELL sim flow
> but it emphatically HAS a direction (the wind driving the swell), so the wind vector is now fed in
> as the flow direction at strength 0.35 and the foam/ripples travel with the waves. Whitecap
> coverage also changed from a linear ramp to a `smoothstep(0.10, 0.38)` with a dead zone — the
> linear version put foam on nearly every wave face, where a real Beaufort-4 sea breaks white on a
> minority of the steepest crests.
>
> **L4 evidence (WaterLab, open water at (−32,60,−32), noon, same crop):** vertical structure — the
> signature of relief — **row-to-row luminance change 0.405 (flat) → 2.112 (swell), a 5.2× increase**;
> overall sd 20.3 → 31.5. Shoreline checked at a shallow vantage: seabed reads through the swell with
> no gap, no z-fighting and no torn waterline (the Phase-2 L2 "watertight at the handoff" item).
>
> **Phase 2 perf — MEASURED (gap closed 2026-07-27, commit `dbae8e63`).** First the displacement
> *math*: toggling amplitude 0 ↔ 0.45 moved median frame time 2.696 → 2.348 ms, i.e. below the noise
> floor (the ON case measured FASTER, which added work cannot do). **But that toggle does not
> measure the mesh** — amplitude 0 still draws every triangle. Isolating the tessellation needs a
> COVERAGE-MATCHED control: 1 ring at the same sector count, so the silhouette (and therefore the
> fill) is identical and only the vertex count differs. A first attempt used 4 sectors and was
> discarded — fewer sectors shrink the disc's area, confounding vertex cost with fill. Release,
> sea-filling vantage, 60 samples each:
>
> | mesh | triangles | median frame | vs control |
> |---|---|---|---|
> | 1 ring × 128 (control) | 128 | 1.469 ms | — |
> | **48 × 96 (shipped)** | **4,608** | **1.679 ms** | **+0.21 ms** |
> | 96 × 128 (first cut) | 24,320 | 1.912 ms | +0.44 ms (**+30%** of frame time) |
>
> The dense mesh was NOT free. **48 × 96 keeps 95% of the wave structure** (row-to-row luminance
> change 2.010 vs 2.112 on an identical capture) **for less than half the cost**, so it ships as the
> default. The measured table lives in the comment above the constants so the next tuning pass
> starts from data rather than vibes.
>
> ### Phase 3 — FLOW VELOCITY, SHIPPED 2026-07-27
>
> - **Flow proxy derived free from the CA.** Every horizontal transfer `step()` already computes now
>   credits `f * direction` to BOTH endpoints; the result is EMA-smoothed (`FLOW_EMA = 0.25`, ~4-step
>   response). `WaterSimulation::flowAt(x,y,z)`. **Named honestly: it is NOT a velocity in m/s** —
>   the CA has no momentum — it is "net mass moved per step, and which way", the right input for
>   shading and the wrong one for physics. Vertical flow is deliberately not tracked (waterfalls are
>   already found by lip detection), keeping it to 8 bytes/cell.
> - **Carried to the shader** as a 4th `vec4` on `WaterSurfaceCell` (dir.xy, strength, foam), with
>   `FLOW_FULL = 0.15` mass/step mapping to full strength, and foam where flow meets shallow depth.
> - **Kinematic river flow.** `FlowField::flowDirAt()` (new; exposes the existing per-cell
>   `m_downstream`) + `WaterManager::setRiverFlowQuery`. A baked river is **pinned full end to end,
>   so it performs no transfers and the CA proxy reads exactly zero** — without this a river shades
>   as a long thin lake. Stated plainly in the API docs: this is a VISUAL flow over a hydrostatically
>   static field, not advection.
> - **L2 tests (4, red-verified):** flow points down a channel; is zero on settled water; decays
>   after flow stops; survives a region `shift()`. Mutation (zeroing the accumulation) turns 3 of the
>   4 red — the 4th asserts flow is *zero*, so a zeroing mutation cannot fail it; it is the
>   false-positive guard, not a presence test.
>
> **A DEFECT I SHIPPED AND THEN FIXED — read this before touching the flow shading.** The first
> working version tiled the water into a visible checkerboard. Cause: `flowDir`/`strength` are
> **per-instance**, so they are constant across a cell's quad and DISCONTINUOUS at its boundary, and
> the shader advected the wave field by `flowDir * t * speed`. Because that offset **grows with
> time**, any per-cell difference grows with it — after minutes of uptime neighbours were
> phase-shifted by tens of world units and the surface shattered. Smoothing the flow field across N4
> neighbours (done, and kept) did NOT fix it, because a tiny difference still diverges. The fix is
> the standard **flow-map cycle**: advect over a bounded 3 s window and crossfade two half-period
> samples, so the offset never exceeds `PERIOD * speed` and inter-cell discrepancy stays bounded
> forever. Also removed: the anisotropic squash (distortion grew with |p|, so it tiled even worse)
> and the hard still/flowing branch (cells popped as the CA jittered across the threshold).
> **Measured (same camera, before/after):** cell-scale roughness 7.51 → 4.88 (−35%) with overall
> contrast preserved. Two standing rules are now written into `water_common.glsl`: **translate the
> sample point only, never warp it anisotropically**, and **never let an offset grow with time.**
>
> **Phase 3 perf — MEASURED, +27% of the sim's ACTIVE step.** Via the `PHYXEL_WATER_FLOW_ENABLED`
> compile switch (kept in `WaterSimulation.h` for cheap re-measurement), Release,
> `--gtest_filter=Water*`, 64×32×64 worst-case active sweep, 3 runs each: **OFF ~175 µs/step, ON
> ~222 µs/step**. Costs **nothing** when the field is settled (0.002 µs/step either way), which is
> the common case in a live world — and at 20 Hz even the worst case is ~4.4 ms per wall-clock
> second. An earlier form cost ~+70%; gating the flow arrays on wet cells recovered most of that
> (the write set spans each column's FULL height and the box is overwhelmingly air, so streaming two
> extra per-cell arrays through cache dominated). If it ever needs to be cheaper, the next step is
> per-COLUMN flow (32× less memory; the renderer only reads the surface cell anyway).
>
> **NOT done — do not assume these:**
> - **Phase 3's river path is L2-covered but still NOT verified live.** Three
>   `WaterManagerTest.RiverFlowQuery*` / `ClearingRiverFlowQuery*` tests now pin the stamping
>   (direction applied on a pinned channel · still pools untouched · unbind clears it),
>   red-verified by mutation — and that mutation independently confirms the premise: with the stamp
>   disabled a pinned river's CA flow reads **exactly 0**. What is still missing is **L4**: the
>   kinematic river needs a streamed world with a baked hydrology order≥3 channel, and WaterLab is a
>   small authored world with no bake, so only the CA-derived path (the flume) was exercised at
>   runtime.
> - **The red was NOT run as a build.** The day/night failure is proven by SOURCE (pre-change
>   `water.frag` hardcoded `sunDir`/sky constants and bound no scene UBO, so it could not respond
>   to time of day) — not by measuring the old binary at midnight. The A/B baseline build existed
>   only for the perf run.
> - Water-vs-OIT ordering (glass seen through water) is unchanged and untested.
> - The underwater fog is a **single-alpha approximation** — true per-channel extinction would need
>   the scene colour as an input to the overlay (a second full-res copy). Stated in the shader.
> - Caustics, surface-from-below distortion, and muffled audio are Phase 5, not built.

---

## 1. Ground truth — what ships today (source read 2026-07-27)

| Layer | File | State |
|-------|------|-------|
| Sim core | `engine/src/core/WaterSimulation.cpp` | Mass-conserving CA: compression-aware gravity, 25%-toward-average horizontal leveling, upward pressure, evaporation, source/channel pins. Per-column active set + sleep. 23 unit tests. |
| Manager | `engine/src/core/WaterManager.cpp` | One 64×32×64 region (`Application.cpp:372`) following the camera in XZ **and** Y, stepped at 20 Hz. Baked water table (per-column lake/ocean levels), river channel pins, runtime shoreline snap. Builds per-cell sloped surface + skirts + waterfall lips. 21 unit tests. |
| Render (far) | `WaterRenderPipeline` + `shaders/water.{vert,frag}` | ONE camera-locked flat quad at `seaLevel`. |
| Render (near) | `WaterCellRenderPipeline` + `shaders/water_cell.{vert,frag}` | Instanced per-cell quads: sloped tops from a shared corner grid, 4 side skirts, waterfall curtains. |
| Mist | `Application.cpp:3291` | `VfxSystem` dome bursts at detected fall lips. |

The **scale architecture and the CA rules are sound**. Everything below is fidelity.

## 2. Diagnosis — why it reads "Minecraft-like"

Each item is evidenced in source. Ranked by cost to the look.

### Appearance
1. **The sea has no geometry.** `shaders/water.vert:26` emits one quad pinned to `y = seaLevel`.
   Zero displacement, zero waves. A perfectly flat plane cannot read as an ocean regardless of
   fragment shading.
2. **Water cannot see the scene.** Both pipelines draw *inside* the scene render pass
   (`RenderCoordinator.cpp:1893`, `:1908`), so they cannot sample the color/depth they are writing.
   This structurally blocks **refraction, depth-based absorption, soft shorelines and foam** — the
   four cues that make water read as a volume rather than a blue decal. The ingredients already
   exist: `PostProcessor` creates the offscreen color (`R16G16B16A16_SFLOAT`) *and* the depth image
   both with `VK_IMAGE_USAGE_SAMPLED_BIT` (`PostProcessor.cpp:227`, `:235`).
3. **Water ignores world lighting.** Both frags hardcode
   `sunDir = normalize(vec3(0.4, 0.85, 0.35))` plus fixed horizon/zenith colors
   (`water.frag:60-64`, `water_cell.frag:49-53`). Neither binds the scene UBO that carries
   `sunDirection` / `sunColor` / `ambientLight` / the shadow map (`shaders/voxel.frag:17-27`).
   **At midnight the sea still carries a midday sun glint**, and nothing shadows water.
4. **Normals are two sine waves** at fixed amplitude and direction with no distance LOD
   (`water.frag:23-36`) — a visible repeat up close, shimmer aliasing far away.
5. **No reflection.** `m_waterReflectionActive = false` is hardcoded
   (`RenderCoordinator.cpp:1716`); the shader branch is dormant.
6. **Hard shoreline.** No foam, no depth fade, no wet-terrain darkening. (The terrain side —
   a coastal beach material — is a known-missing v2 item.)
7. **No underwater state.** `grep -ri underwater` over `engine/ editor/ shaders/` returns one
   unrelated `WorldGenerator` comment.

### Flow
8. **There is no velocity anywhere in the system.** The CA is diffusion — mass redistribution with
   no momentum. `WaterSurfaceCell` (`WaterManager.h:21`) carries no direction or speed, so the
   ripple animation is identical on a still pond and in a rapid.
9. **Rivers are pinned, not flowing.** `setRiverQuery` pins every recessed channel bed cell as a
   full source (`WaterManager.h:104-118`) — a river is a lake shaped like a river. This was a
   deliberate, *measured* v2 decision (edge-only inflow died into puddles ~5 cells out) and it is
   hydrostatically correct; it is only visually static.
10. **Spills level instantly** instead of surging downhill — the momentum-free consequence of (8).
11. **Waterfall curtains are static walls** shaded identically to a flat lake — no streaking, no
    acceleration, no white water. Only the mist sells the fall.
12. **Full-voxel only** — subcube/microcube terrain collapses to solid/empty, so water sits wrong
    on the engine's own structure geometry.
13. **The GPU CA round-trips the whole field every step** (`WaterManager.cpp` `stepGpu`) — upload,
    submit, wait, read back. Off by default; not a win.

---

## 3. Phase plan

Ordered by look-per-cost, and so each phase unlocks the next. Phases 1–3 are shader/render and
O(active-cell) work; only Phase 4 touches the CA's per-step budget.

### Phase 1 — Water becomes a volume  ← IN PROGRESS
The structural unlock. Everything downstream depends on water being able to sample the scene.

**Deliverables**
1. **Post-scene water pass.** Move both water draws out of the scene pass into a new pass that
   runs after `endSceneRenderPass` and before the OIT pass. Color attachment = the existing
   offscreen image (`LOAD`/`STORE`); depth = the existing depth image bound **read-only**
   (`DEPTH_STENCIL_READ_ONLY_OPTIMAL`) — legal because water already runs
   `depthWriteEnable = VK_FALSE` (`WaterCellRenderPipeline.cpp:209`,
   `WaterRenderPipeline.cpp:264`), and it lets the same image be depth-tested *and* sampled in the
   fragment shader with no copy.
2. **Refraction texture.** Blit the scene color to a **half-resolution** sampled image before the
   water pass. ⚑GROUND: half-res is chosen because refraction is a blurred lookup — it cuts the
   per-frame copy from ~16.6 MB to ~4.2 MB at 1080p. Measure the real cost; full-res is the
   fallback if half-res artifacts show at the shoreline.
3. **Scene lighting.** Bind the shared scene UBO at set 0 (the established pattern —
   `FoliageRenderPipeline.cpp:172`, `VulkanDevice::getDescriptorSetLayout()`), water's own
   textures at set 1. Sun direction, sun color and ambient now drive the water's specular, sky
   tint and glint, so water tracks the day/night cycle.
4. **Depth-driven shading**, all from the sampled depth buffer:
   - **Refraction** — sample the refraction texture at a normal-perturbed screen UV, rejecting
     samples whose depth is in front of the surface (the standard halo fix).
   - **Absorption** — Beer-Lambert extinction over the *true* water thickness (seabed depth minus
     surface depth), replacing the current fake `depth/5.0` ramp and the flat plane's
     `ndv`-based tint. This is what gives shallows-to-deeps gradation everywhere, not just inside
     the sim region.
   - **Soft shoreline** — fade alpha to zero as thickness → 0, killing the hard waterline.
5. **Underwater state.** Detect camera submersion (sim mass / baked table level) → depth-tinted
   fog and a color shift; render the surface correctly from below. ✅ SHIPPED — see the progress
   block above.

**Validation**
- **L2:** an automated pixel probe over fixed crops — (a) shoreline gradient: alpha/color must vary
  monotonically across the waterline instead of stepping; (b) day/night: the same vantage at noon
  vs. midnight must differ in mean luminance (today it does not).
- **L4:** same-vantage before/after captures at a coast, a lake and a waterfall.
- **Perf:** `engine-perf` before/after. Budget the water pass + blit; report the real number.
- **Red-before-green:** the day/night probe and the shoreline-gradient probe both fail on `main`
  today — capture that failure first.

**Stress:** camera at the waterline (half-submerged), grazing angles across a long coast, water
against glass/transparent voxels (the OIT pass draws after water — confirm ordering is right),
and a fully-submerged camera at depth.

### Phase 2 — The surface gets a shape ✅ SHIPPED (see the progress block above; normal-map detail
### LOD and shoreline foam remain — the swell, whitecaps and per-cell ripple landed)
- **Gerstner-displaced sea.** Replace the single quad with a camera-centered, radially-graded grid
  (⚑GROUND the tessellation from a measured fill/vertex budget) summing 3–5 Gerstner waves with
  **analytic** normals in the vertex shader. No CPU work, no sim coupling.
- **Per-cell ripple.** A small vertical offset scaled by `(1 - clamp(thickness))` so ponds move
  while thin films stay attached to the terrain.
- **Normal detail LOD.** Replace the 2-sine hack with scrolling normal-map octaves whose amplitude
  falls off with distance — fine detail near, smooth specular far, no shimmer.
- **Shoreline foam** off the Phase-1 thickness term.
- **Validation L2:** the displaced sea must stay watertight against the per-cell field at the
  region boundary (no gap, no z-fight) — the v2 "ocean slab" defect class.

### Phase 3 — Flow velocity ✅ SHIPPED (see the progress block above)
- **Derive velocity from the CA's existing transfers.** `step()` already computes every transfer;
  accumulate the per-cell net `(dx, dy, dz)` into an EMA-smoothed velocity field. No new sim math,
  one extra buffer over the active set.
- **Carry it to the renderer** via a 4th `vec4` on `WaterSurfaceCell` (flow xz + speed + foam).
- **Shade with it:** advect normals *along* flow, stretch ripples by speed, generate foam where
  speed/shear is high, and streak waterfall curtains downward with white water at lip and plunge.
- **Kinematic river velocity.** Attach a flow direction sampled from the baked `FlowField`'s
  downhill direction to pinned river cells. **Stated honestly: this is a visual flow over a
  hydrostatically static field** — the ribbon still does not advect mass. It is the cheap, stable
  way to make rivers read as rivers; real advection is Phase 4.
- **Validation L2:** the derived velocity field must point downhill along a carved channel (compare
  against `FlowField`'s direction over N sampled cells) and must be ~zero on a settled lake.

### Phase 4 — Real motion in the CA
- **Momentum.** Per-cell horizontal velocity biasing the leveling rule (advect preferentially along
  existing velocity, damped) so a spill surges and rounds corners instead of spreading like paint.
  Two float arrays over the **active set only**.
- **Validation L2 (load-bearing):** mass conservation must hold exactly, and the field must still
  settle (no oscillation / "popcorn water") — both are existing test patterns in
  `WaterSimulationTest.cpp`. Red-verify by mutation.
- **Sub-voxel floor height:** one float per cell derived from sub-occupancy (no cell-count change),
  so water sits correctly on subcube/microcube structure geometry.

### Phase 5 — Polish
- **Screen-space reflections** marched against the depth buffer with a sky fallback — cheaper and
  better on a rippled surface than re-rendering the scene for planar reflection. (Planar remains
  the fallback if SSR's disocclusion artifacts prove worse than its gain.)
- Projected **caustics** on the seabed; underwater post pass + muffled audio; **buoyancy/swimming**.
- **GPU CA revisit** — resident field, dirty-page upload, no readback, render from the GPU buffer.
  Ranked last: the CPU active set already measures 0.002–0.004 ms/frame on a settled ocean, so
  this only pays once the region grows substantially.

---

## 4. Perf posture

Phases 1–3 are fill-rate and shader cost plus one half-res image blit per frame; Phase 4 is the
only one that touches the sim's per-step budget, and it stays O(active columns). The engine's
standing #1 issue is render density, and water adds **transparent overdraw** — so every phase
measures with the `engine-perf` skill before and after rather than trusting estimates. Numbers get
reported as measured, including regressions.

## 5. Reuse vs. build new

**Reuse:** the whole CA rule set and its test suite, the per-column active set/sleep, the baked
water table + river/shoreline machinery, the corner-smoothed surface + skirt + waterfall-lip
builder, the VFX mist emitter, `PostProcessor`'s offscreen color + depth images and its render-pass
plumbing, the shared scene UBO + its descriptor-set pattern, and the mirror/reflection pass as the
Phase-5 fallback.

**Build new:** the post-scene water pass + refraction target, depth-driven absorption/refraction/
shoreline shading, underwater state, the Gerstner sea surface, normal-map detail LOD, the velocity
field and its render channel, CA momentum, sub-voxel floor heights, SSR, caustics, buoyancy.

## 6. Open questions

- **Water vs. OIT ordering.** Water currently draws before the OIT pass would run. Glass seen
  through water, and water seen through glass, need one ordering; decide with a real test scene.
- **Half-res refraction at the shoreline.** Half-res is the perf choice; if the soft-shoreline
  gradient shows stepping, either go full-res or do the shoreline term at full res from depth only.
- **Region boundary under displaced waves (Phase 2).** The Gerstner sea and the per-cell field must
  agree in height where they meet, or the v2 "slab" artifact returns in a new form.
- **Whether Phase 4 momentum is worth its stability risk** — Phase 3 may deliver enough of the read
  of moving water that the CA can stay a pure leveling automaton. Decide after Phase 3 ships.

## References
[`docs/WaterSystemV2.md`](WaterSystemV2.md) (scale plan + open items),
[`docs/WaterSystem.md`](WaterSystem.md) (v1 CA/implicit-ocean rationale),
[`docs/TerrainGenerationV2.md`](TerrainGenerationV2.md) §P2 (hydrology bake that feeds water),
[`docs/RenderOptimization.md`](RenderOptimization.md) (render-density context).

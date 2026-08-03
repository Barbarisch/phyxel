# Water Appearance v4 — Per-Body Optics & Sea State

> Status: **W1 (profile pipe) BUILT + L4-VERIFIED, uncommitted** (2026-08-03). W2–W5 planned.
> Successor to [`docs/WaterSystemV3.md`](WaterSystemV3.md) (which made water read as a *volume*:
> refraction, absorption, swell, surf, flow) and a sibling of
> [`docs/WaterPhysicalFeelPlan.md`](WaterPhysicalFeelPlan.md) (which makes it *behave* against the
> world). v3 asked "does it read as water?" — **v4 asks "does it read as THIS water?"**
>
> **User goal, verbatim:** *"a big lake can be very still and glassy making it reflect really well.
> Big bodies of water are usually not very transparent … especially an ocean which is constantly
> moving making reflections less likely. Variable transparency, different levels of choppiness, and
> different levels of reflection."*
>
> Standing discipline applies: ⚑GROUND every dimension (grounding-auditor), red-before-green +
> solution-auditor on every "works/fixed" claim, a stress phase per feature, and a named validation
> layer per deliverable (**L1** artifact exists · **L2** structural invariant measured on real output ·
> **L3** functional simulation · **L4** live-engine runtime). For rendering work the L4 evidence
> standard is **same-vantage before/after captures plus a pixel probe** — a screenshot alone is never
> evidence.

---

## 0. Scope decisions (user, 2026-08-03)

| Decision | Choice |
|---|---|
| How a body's profile is decided | **Derived + authorable override** — the engine derives a profile from the bake (class/size/fetch/depth) so every procedural world gets sensible water for free; `game.json` / the world recipe can override a named body or set world defaults. |
| Reflections | **Full SSR now** — screen-space reflections with roughness-driven blur and sky fallback. This is the only path to a lake that genuinely mirrors terrain. |
| Coverage | **Oceans + lakes** (the sea clipmap's domain). Rivers/creeks/ponds keep today's look; because the shading is shared, they must be pinned to a **default profile that is pixel-identical to today** — this is a regression gate, not an afterthought. |

---

## 0b. W1 SHIPPED — the profile pipe (2026-08-03)

**What landed.** `Phyxel::WaterProfile{turbidity, waveEnergy, roughness}` +
`deriveWaterProfile` + `buildHydroUpload` (`engine/{include,src}/core/WaterProfile.*`); the hydrology
render texture widened **`R32G32_SFLOAT` → `R32G32B32A32_SFLOAT`** (R level · G energy · B turbidity ·
A roughness); the packing loop moved OUT of `RenderCoordinator::drawFrame` into that pure function so
it is unit-testable at all; `water.frag` fetches the profile **per pixel** (like the basin level
already was — NEAREST texture, and a varying would smear one body's profile across a divide);
`water_common.glsl` consumes both; `POST /api/debug/water_look` forces a profile as the positive
control. **Derivation is NEUTRAL for every body** — W1 is the pipe, not the look.

**Evidence.**
- **Unit: 6 `WaterProfileTest` cases, red-verified by MUTATION.** Mutating the energy normaliser
  (`/10.0f → /11.0f`) and swapping the B/A writes turns **4 of the 6 red** with the expected
  diagnostics (lake energy 0.370 → 0.336; pond falling to the 0.15 floor; turbidity/roughness
  transposed). The 2 that stay green are the ones those mutations cannot reach — correct, not
  missing coverage. Reverted → 6/6 green. ⚑The exe was deleted before the relink each time
  (MSVC will silently reuse a stale binary and fake a red/green cycle).
- **No suite damage: 150/150** across `Water*:Hydrology*:SeaMesh*:Terrain*:Ripple*`.
- **L4 POSITIVE CONTROL (WaterTableTest, Debug, cycle disabled at noon).** Measured as a **mean shift
  against a 5-frame control envelope** — water is animated, so a single control pair is a one-sample
  denominator, and the first pass's "27× red shift" was exactly that artifact (the real R envelope is
  ±5.3, not ±0.3). Two vantages, because **each channel is invisible at the other's**:
  | vantage | probe | result |
  |---|---|---|
  | look-down, shallow over sand (394,26,442 p−84) | turbidity 1.0 | **G 16.0× envelope, B 5.3×, hf 13.9×**, and the right spectral direction — G 165.6→145.5, B 149.4→136.5, **R rises** 126.1→133.6 (the turbid endpoint is flatter across the spectrum) |
  | grazing across water (500,21,600 y40 p−8) | roughness 20× | **hf 4.44 → 18.56 = 33.4× envelope**; RGB all ≈10× |
- **NO-REGRESSION — the guarantee is ALGEBRAIC; the capture only corroborates it.** State it in
  this order, because the reverse overstates the screenshots (solution-auditor, 2026-08-03):
  1. **Primary (structural, exact):** `mix(x, y, 0.0) === x` and `a1..a4 * 1.0 === a1..a4`. At the
     neutral profile the new code is the *same arithmetic* as the old, not merely close to it.
     `water_cell.frag` passes literal constants, so flowing water cannot vary at all.
  2. **Corroborating (empirical):** `git stash` → rebuild → capture 5 → unstash → rebuild →
     capture 5 at the identical verified camera (pre-change binary confirmed by `water_look`
     returning **404**). Every W1 frame lands **0.0–1.0× the baseline's own envelope** on R, G, B
     and hf.
  ⚑**Do not oversell (2) as "indistinguishable."** The envelope is session-dependent: the SAME crop
  measured **R ±2.8** in one run and **R ±6.8** in another (~2.4×), so a few-RGB-unit regression
  could hide inside a wide-envelope run. The screenshot A/B cannot carry a no-regression claim on
  its own — it is only meaningful because (1) already makes the change a no-op by construction.
  Contrast the override signal, which is 16–33× envelope and therefore immune to this concern.
- **⚑No pixel evidence exists for the per-cell renderer** (rivers/creeks/ponds). Its neutrality is
  supported by *source inspection only* — a hardcoded literal, so the risk is low, but no capture
  of `water_cell.frag` output was taken. Do not cite W1 as an L4 check on flowing water.

**Findings that change W2/W3 — read before starting them.**
1. **⚑The mechanism decides the vantage, and getting it wrong reads as "broken".** Turbidity is
   invisible at a grazing angle (Fresnel ≈ 1, so the sky reflection swamps the absorption term) and
   roughness is invisible looking straight down (Fresnel ≈ 0.02, and the refracted seabed there is
   flat uniform sand, so tilting the normal has nothing to act on). The first roughness probe
   measured **zero effect at 20× amplification** and looked like a dead channel; it was the vantage.
   Every v4 probe must state which vantage exercises the mechanism it claims to test.
2. **Zeroing the ripple amplitudes does NOT make a mirror.** roughness = 0 measured **0.2–1.5×
   envelope — inside the noise**. The shipped micro-detail sums to only ~0.048 of slope (deliberately
   tiny), so removing it changes almost nothing, while amplifying it is dramatic. **W3's "glassy
   lake" therefore cannot be delivered by scaling `a1..a4` alone** — it needs the swell amplitude
   (waveEnergy) down as well, and it needs W4's reflection to have something worth mirroring.
3. **Flat-sea worlds have no profile at all, by construction** — no bake ⇒ no bodies ⇒ the shader
   takes the early-out before reading B/A. `water_look` reports `hydrology_bound` so this reads as a
   fact rather than a failure. The first probe attempt wasted a cycle measuring flat-sea fallback
   water (`water_table_level` said `wet: false`) before this was understood.
4. **The turbid endpoint constants in `water_common.glsl` are an ungrounded PLACEHOLDER**, reachable
   only through `water_look`. W2 must replace them with the Jerlov/Secchi spectrum *before*
   derivation is allowed to emit non-zero turbidity.

**Operational gotchas hit (new).**
- **`POST /api/camera` applies yaw/pitch but NOT position.** It silently left the camera at its old
  spot twice, once producing a whole 5-frame baseline set at the wrong vantage. Use the MCP
  `set_camera` tool, and **always `get_camera` and assert the position before capturing**.
- In Debug this world's game loop needs ~1 min after boot before debug commands stop returning
  `"Request timed out waiting for game loop"`. `water_bake_info` answers earlier than the rest.
- The labs this plan's siblings reference (`WaterLab`, `RiverLab`, `CreekLab`) **do not exist on this
  machine** — they were created in another session. `WaterTableTest` (`streaming: true`) is the
  baked lab used here; its nearest baked-wet column to spawn is **(394, 442)**, level 16, with the
  wet region running NE to ~(1162, 1210).

**Not done in W1:** no perf measurement (the change is a one-off 1 MB upload and two fetched floats;
W4/SSR is where the budget question actually lands), and rivers/creeks/ponds are pinned to the
neutral profile as an explicit scope decision.

---

## 0c. W2 — variable transparency (2026-08-03)

**Grounding first, and it CHANGED THE DESIGN.** A grounding-auditor pass on the optical model
produced one correction that would have shipped a wrong-looking result:

- ⚑**The turbid endpoint's SPECTRAL ORDER was backwards.** The W1 placeholder was
  `vec3(1.20, 0.90, 0.75)` — R > G > B, i.e. blue transmitting best, on my assumption that turbidity
  merely "flattens" the spectrum toward grey. **Akkaynak & Treibitz (2017, CVPR)**, deriving RGB
  attenuation from the Jerlov dataset, find the familiar "red dies first" rule is an **oceanic-only**
  phenomenon: in very turbid coastal water **blue attenuates fastest**, because sediment and CDOM
  preferentially absorb blue-green, leaving a green-yellow (~520-570 nm) window. That is why muddy
  water reads **olive/brown, not grey**. The endpoint is now `vec3(0.95, 0.65, 1.20)` — B > R > G.
- **Magnitude is grounded by construction**, not read off a paywalled table: eutrophic Secchi
  0.9-2.3 m (**Carlson 1977** TSI) → mid 1.5 m; **Holmes (1970)** `Kd ≈ 1.4/Z_SD` for turbid water
  → broadband Kd ≈ 0.93 /m. The three channels average 0.933. ⚑The per-channel SPREAD about that
  mean is *not* measured — an artistic distribution satisfying the grounded magnitude + ordering.
  The real fix is Jerlov's Kd(λ) for a named coastal type (Solonenko & Mobley 2015 / Jerlov 1976
  Table XXXI); both paywalled and unobtainable this session.
- ⚑**The CLEAR endpoint's own comment was wrong and is corrected.** Real Pope & Fry (1997) values
  are **(0.340, 0.0565, 0.00922) /m**; the shipped `(0.42, 0.09, 0.045)` raises **all three**
  (1.24× / 1.59× / 4.88×), not "blue only" as the old comment claimed — and the old comment even
  misquoted the paper (it said green 0.05 while shipping 0.09). Values kept (deliberate exaggeration
  for metre-deep voxel water); the comment now states the real numbers and the per-channel factor.
- Also fixed: `Kd` vs beam attenuation `c` now cites **Gordon (1989)** and says plainly it
  *under*-attenuates turbid water; the doc's "1.44" was wrong — the turbid-water constant is
  **1.4 (Holmes 1970)**; `WATER_SCATTER` (clear) flagged as **unsourced, left as-is** since changing
  it is a visual decision; `WATER_SCATTER_TURBID` keeps ⚑PLACEHOLDER status — **Lobo et al. (2014)**
  grounds the *direction* (bb ≈ 0.018-0.030 × NTU) but not the RGB magnitude.

**Derivation.** Turbidity now comes from **mean depth** (`volumeEst / (areaCells·cellSize²)`),
ramped between `kTurbidDepth = 2 m` (turbidity 1) and `kClearDepth = 20 m` (turbidity 0). ⚑It is a
**named PROXY, not a measurement** — the bake knows nothing about sediment or algae; mean depth is
the strongest clarity-tracking quantity it carries, and the published Secchi ordering agrees
(Crater 44 m / Tahoe ~18 m deep-and-clear vs Carlson's eutrophic 0.9-2.3 m shallow-and-murky).
**Ocean short-circuits to CLEAR** — open ocean is Jerlov type I, the clearest natural water; it
reads opaque because it is deep and Fresnel-mirrored, not because it is dirty.

**Underwater overlay follows the surface.** `water_underwater.frag` hardcoded `VISIBILITY = 22.0`,
so a murky lake would have read clear from below and breaking the surface would pop. It now takes
the turbidity of the body the camera is in (via the one free push slot, `params2.w`, fed by the same
`waterProfileAt`). ⚑`VIS_TURBID = 2.0` is derived by a **grounded ratio**, not picked: clear coastal
Kd ≈ 1.7/20 = 0.085 vs eutrophic 1.4/1.5 = 0.93 → ~11× faster → 22.0/11. (An earlier 3.0 was a guess.)

**Evidence.**
- **Unit: 12/12** (`WaterProfileTest`), **red-before-green run as a build twice over**: the mapping
  was stubbed
  out first and `ShallowBodiesAreTurbidAndDeepOnesAreClear` failed with **"actual: 0 vs 0"**, then
  passed once implemented. ⚑The monotonicity test **passed on the stub** (0 ≤ 0 is trivially
  monotone) — it was a guard, not a falsifier, so a strict `first > last` assertion was added and is
  labelled as the part that does the work. `W1DerivationIsNeutralEverywhere` was **changed on
  purpose** (W2 breaks its turbidity half); what survives is roughness-stays-1 + dry-land-is-clear.
  A second mutation run covers the `isfinite(volumeEst)` guard, which the solution-auditor found had
  **no falsifier** — deleting it turns a non-finite volume into turbidity **1 (fully murky)** instead
  of clear, and `DegenerateBodiesDoNotProduceNaNTurbidity` now goes red on exactly that.
- **156/156** across `Water*:Hydrology*:SeaMesh*:Terrain*:Ripple*`.
- **L4 (WaterTableTest, lake body #47 at (4480,−14848)), 4-frame control envelope, open-water crop
  `300,430,1100,650`:** the derived frame sits **BETWEEN its own forced endpoints on all three
  channels** — R: clear 74.3 < **derived 108.8** < turbid 127.6 (shift **16.9× envelope** vs clear);
  G: 125.0 < **133.1** < 140.3; B: turbid 124.3 < **131.7** < clear 135.5. Per-channel fractional
  position 0.34–0.65 against a predicted 0.562 (G lands at 0.529), consistent with the nonlinear
  Beer-Lambert mapping rather than a linear one. **Blue DECREASING with turbidity is the corrected
  spectral order showing up in pixels** — under the old placeholder it would have increased.
- **The body's numbers are ARCHIVED, not asserted** (`docs/evidence/water-v4-w2-bodies-20260803.json`,
  `…-lake47-column-20260803.json` — raw HTTP responses). Body #47: `area_cells 8`,
  `volume_est 1296214.25`, cell size 128 ⇒ mean depth **9.889 m** ⇒ turbidity **0.5617**, which is
  where the 0.562 above comes from. The project has twice been caught citing numbers from
  un-archived sessions; this closes that loop rather than repeating it.
- ⚑**A SECOND CROP WAS INCONCLUSIVE AND IS REPORTED HERE, NOT JUST IN THE GOOD ONE.** Crop
  `350,100,700,350` (the near-shore mottled band) measured **0.2–0.7× envelope for every channel —
  no result**. Its envelope is inflated (R ±5.7 vs ±2.0 on the open-water crop) because that region
  carries animated foam and terrain showing through. Reporting only the decisive crop would have
  been cherry-picking; the honest statement is that the effect is measurable **in open water** and
  **not resolvable in the surf band at this vantage**.

**⚑NOT verified — do not claim these.**
1. **Per-body SPATIAL variation is not demonstrated at L4.** The override is global, so "derived
   lands between the forced endpoints" proves the derived *value* reaches pixels, not that two
   different bodies differ **in one frame** (this plan's stated gate). That remains unit-only
   (`buildHydroUpload` packs per-cell from per-body derivation). This world's non-ocean bodies are
   small and ~15 km apart, so framing two at once was not achieved.
2. **The underwater overlay change has NO runtime verification at all** — code + shader only.
3. A first L4 attempt at (3968,−15072) measured **nothing**, because that column is baked-DRY and
   the pale-blue frame was **sky**, not water (16 visible chunks of 592) — the documented
   "all-water wash" trap. `water_table_level` is the two-second disambiguator; use it *before*
   capturing, not after.

---

## 0d. W4 — screen-space reflection (2026-08-03)

**The problem this closes.** Water reflected a **procedural sky and nothing else** —
`waterSkyReflection` is a gradient plus a sun disc. A mirror-flat lake could not show the mountain
behind it. That is the single most obvious "not water" tell on calm water, and it is what the user's
"a big lake … reflect really well" asks for.

**Why SSR and not planar.** Planar reflection assumes a flat mirror plane — this sea is
Gerstner-**displaced**, so a plane is the wrong model — and the engine's shared mirror pass is
known-broken (wrong winding/projection, which is why `m_waterReflectionActive` had been hardcoded
`false` since v3). SSR also needs **no new resources**: the water pass already binds the half-res
scene-colour copy (captured before water draws, so it holds terrain + sky and no water) and the
scene depth buffer. It is a march over data already resident.

**Design notes worth keeping.**
- ⚑**Roughness coupling is AUTOMATIC and deliberate.** There is *no* explicit "blur by roughness"
  term. The reflected ray is built from `N`, and `N` already carries the ripple detail scaled by the
  per-body `roughness` — so a choppy surface scatters its own reflection rays, and when W3 derives a
  low roughness for a calm lake the same code returns a coherent mirror. One parameter, both
  behaviours; a second blur knob could only ever disagree with the first.
- ⚑**Projection frame matters.** The march is in ABSOLUTE world space and projects with the water's
  **own** `viewProj`, passed explicitly through `WaterSurfaceInput`. `ubo.viewProj` is
  camera-RELATIVE (a v3 Phase-1 property) and would march the ray in the wrong frame. Depth is
  compared as a linear distance along the camera forward axis, which is convention-independent.
- Budget: 24 steps over 120 world units, 5-iteration binary refine, 6-unit thickness test to reject
  the classic "ray passed far behind a foreground object" smear. Confidence fades at screen edges
  (where SSR data simply runs out) and with distance, so misses **fall back to the sky** rather than
  popping. `ssr = 0` is bit-identical to the pre-W4 look.
- The dead planar branch in `water.frag` is **deleted**; `params2.z` is reused as the SSR flag, and
  `POST /api/debug/water_ssr {enabled}` is the A/B control.
- **Cell water (rivers/creeks) keeps sky reflection.** Narrow, close-range, often overhung water is
  the worst case for a screen-space march — most rays leave the screen or hit the bank.

**L4 evidence — a PURPOSE-BUILT fixture, because the natural world could not falsify it.**
Three natural vantages in `WaterTableTest` produced **no measurable difference** between SSR on and
off, and that was *correct but useless*: looking across open water the reflected ray travels forward
and up into empty sky, so SSR rightly finds nothing. A null result there is equally consistent with
"SSR is broken", so it proves nothing either way. The three, named so the claim is checkable rather
than asserted: **(4270, 152, −14848) yaw 0** — measured A/B, 0.1–1.4× envelope, i.e. no result, over crops
`750,380,1180,660` and `300,330,1180,660` (stated so the number is recomputable, not just
assertable) (`screenshot_20260803_132150_219` + captures `…_132224_226`…`…_132255_472`);
**(4400, 151, −14700) yaw −90** (`…_132407_097`); **(4450, 151, −14750) yaw 90**
(`…_132426_616`). The last two are water-and-sky only — visibly nothing to reflect. Following the project's own rule — *pick the world that
maximises the defect, not the world where it was noticed* — a 100×35×4 **Bricks wall** was placed
across the water at (4400..4500, 145..180, −14702..−14698), giving the ray something high-contrast
to hit (red brick against blue-teal water and sky).

| crop | SSR ON | SSR OFF | shift |
|---|---|---|---|
| water beside the wall | (98.1, 111.6, 106.2) | (160.3, 181.9, 195.4) | **R 178× envelope**, G 76×, B 96× |
| water further out | (114.7, 130.7, 133.2) | (121.9, 139.7, 146.0) | R 4.0–6.5×, G 3.7–5.3×, B 3.7–5.1× |

With SSR on, water beside the wall darkens sharply; with it off, it returns to pale sky. Blue falls
hardest (89 levels), consistent with a blue sky reflection being replaced by a brown wall, and the
effect decays with distance from the wall.

⚑**"IT GOT DARKER" IS NOT PROOF OF REFLECTION — a bug that merely darkened water would look the
same.** The distinguishing test (run by the solution-auditor, not by me — my own prose said
"brick-tinted", which the raw numbers do not support: R is the *lowest* channel in the ON frame):
sample the wall's own on-screen colour (82.6, 64.9, 50.0 — identical in both frames, so only the
flag differs), then check whether the **(ON − OFF) colour vector is parallel to the
(wall − sky) vector**. Measured **cosine similarity 0.995**. The change points almost exactly at
"blend toward the wall", which generic darkening cannot produce by coincidence. That is the
falsifier for this claim; the envelope shift alone is not.

**PERF — MEASURED, COUNTERBALANCED, and it is not free (Release).**

⚑**The first protocol was biased and the solution-auditor caught it.** v1 measured a *block* of ON
then a *block* of OFF, always in that order. This project has documented monotonic drift (water-sim
refill, streaming settle, restart variance), so ON-always-first would bias ON slower in **every**
run — precisely the pattern v1 reported. v1's numbers are therefore not cited here. The table below
uses `scratchpad/ssr_perf2.ps1`: ON and OFF **interleaved sample by sample**, with the leading state
**alternating each pair**, 3 frames discarded after every state switch, 30 samples per arm. Raw
per-sample arrays are archived (they were prose-only before, a regression from the W1/W2 standard):

**THREE independent sessions per vantage** (a single session would assert stability rather than show
it — v1's repeats spanned ~0.6 ms, so one sample proves nothing about spread). Every raw array is
archived under `docs/evidence/water-v4-w4-ssr-perf-*.json`:

| vantage | run | SSR ON | SSR OFF | delta | arms overlap? |
|---|---|---|---|---|---|
| wall in view, rays **HIT** | 1 | 35.00 | 32.32 | **+2.68 ms (8.3%)** | no |
| | 2 | 34.68 | 32.05 | **+2.64 ms (8.2%)** | **yes** — one OFF spike lifted p90 to 35.10 |
| | 3 | 34.58 | 31.87 | **+2.72 ms (8.5%)** | no |
| open water, rays **MISS** | 1 | 34.79 | 30.79 | **+4.00 ms (13.0%)** | no |
| | 2 | 33.98 | 30.71 | **+3.27 ms (10.6%)** | no |
| | 3 | 34.36 | 31.02 | **+3.34 ms (10.8%)** | no |

**Hit case 2.64–2.72 ms (spread 0.08 ms); miss case 3.27–4.00 ms (spread 0.73 ms).** 5 of 6 runs
have non-overlapping arms; the exception is a single OFF outlier in wall run 2 whose median is
nevertheless within 0.04 ms of the other two. Call it **~2.7 ms when rays hit, ~3.3–4.0 ms when they
miss — 8–13% of frame time** in this scene.

**Why this is the effect and not drift** (the confound the auditor raised, answered from the data
rather than by assertion): within the original wall run, the ON−OFF gap goes 2.54 ms (first 5
samples) → 3.05 ms (last 5); open water 3.01 → 4.35 ms. A residual ordering/drift artifact predicts
the **opposite** signature — a gap that is large early and collapses toward zero once the states are
properly interleaved. The gap *widens*, so if anything the quoted medians are slightly conservative.
Combined with the cross-session stability above, the cost is earned rather than inferred.

⚑**MISSES COST MORE THAN HITS — the opposite of the intuition, and the named optimisation target.**
A hit returns as soon as it is found; a ray that sails into open sky marches all 24 steps and then
returns nothing. So the *common* case (water under empty sky) is the expensive one. The obvious next
move is an early-out for rays that are climbing and have seen only sky — deliberately NOT done here,
because it can silently drop the reflection of a tall object seen past a gap of sky, and that
trade-off deserves its own measured increment rather than being smuggled in.

⚑**A MEASUREMENT I GOT WRONG AND CORRECTED.** The first two Release runs were labelled "wall in
view / rays hit". They were not: the wall had been placed in a *Debug* session and never saved, so
the Release world had no wall and both runs were miss-dominated. Caught by `clear_region` returning
`removed: 0`. The table above is after re-placing the fixture in Release and re-running. If a
capture's fixture is not verified present *in the session being measured*, the label is a guess.

**⚑NOT verified — do not claim these.**
1. **Half-res reflection source is untested as a mirror.** `captureRefraction` is half-res *because
   refraction is a blurred lookup*; a mirror is not. Whether a glassy lake reads soft/aliased off a
   half-res source is an open §11 question and was not measured.
2. No natural-scene reflection capture — the only positive evidence uses the synthetic wall. Three
   natural vantages were tried first and all returned nothing, for the correct reason (open water
   reflects sky), which is exactly why the fixture was built.
3. SSR + the ⚑turbid crossover interact (a turbid body's reflection sits over a different body
   colour); not examined.
4. **Whether 8–13% of frame time is an acceptable price is a USER decision, not mine.** It ships
   default-ON because the user chose "full SSR now", and `POST /api/debug/water_ssr {enabled:false}`
   reverts to the pre-W4 look exactly. If the answer is "too expensive", the early-out above is the
   first lever and shortening the 120-unit march is the second.
5. **ZERO automated test coverage.** W1 and W2 each shipped with unit tests; W4 is entirely shader
   code and has none — every claim above rests on runtime capture. That is arguably unavoidable for
   GLSL in this codebase, but it is a real difference in evidence class and is stated rather than
   glossed. A future increment could pin the tracer's *math* (projection round-trip, thickness
   rejection) by extracting it to a CPU-testable form.
6. Frame times here are ~31–35 ms (≈29 FPS) in Release at a 15 km-from-origin streaming vantage —
   the scene is dominated by the known shadow-pass cost, so the *percentage* would differ in a
   cheaper scene even if the absolute millisecond cost held.

---

## 1. Ground truth — what ships today (source read 2026-08-03)

| Concern | Where | State |
|---|---|---|
| Absorption | `shaders/water_common.glsl:251` | `WATER_EXTINCTION = vec3(0.42, 0.09, 0.045)` — **one global const**, clear-water (Pope & Fry 1997) with blue nudged up. Beer-Lambert over true path length. |
| In-scattering | `water_common.glsl:254` | `WATER_SCATTER = vec3(0.04, 0.18, 0.24)` — **one global const**. No turbidity concept. |
| Wave amplitude | `water.vert:116`, `RenderCoordinator.cpp:1784` | Per-body **energy** from body size already exists (tangible-water F): `clamp(log2(areaCells+1)/10, 0.15, 1)`, ocean = 1. Scales **amplitude only**. |
| Micro-roughness | `water_common.glsl:129-168` | 4 ripple octaves at **fixed amplitudes** `a1..a4` (sum ≈ 0.048 slope) with per-octave screen-space LOD. Identical on a tarn and on the ocean. |
| Wind | `WaterRenderPipeline.h:144-146` | `m_waveAmplitude 0.45`, `m_waveLength 14.0`, `m_windDirection 0.6` — three globals, runtime-settable via `water_waves`. No wind *speed* concept. |
| Whitecaps | `water.vert:185` | `smoothstep(0.10, 0.38, 1-N.y)` on crest steepness. Not tied to wind speed. |
| Reflection | `water_common.glsl:219-240`, `RenderCoordinator.cpp:2230` | **Procedural sky gradient + sun disc only.** `m_waterReflectionActive = false` — the shared mirror pass is broken (wrong winding/projection). Fresnel itself is correct. |
| Profile transport (sea) | `WaterRenderPipeline.cpp:116` | 256² **`R32G32_SFLOAT`** hydrology texture: R = basin level, G = wave energy. NEAREST-sampled. |
| Body identity | `core/WaterBodyIndex.h` | CC-labeled bodies over the bake: `Class{Ocean,Lake,Pond}`, `areaCells`, `level`, `volumeEst`, `bboxMin/bboxMax`. **Already built and bound** (`WaterManager::setBodyQuery`). |
| Scene taps | `PostProcessor`, water pass | Half-res scene-colour copy + read-only scene depth **already bound at set 1** on both water pipelines. SSR needs no new plumbing. |

**Diagnosis in one line:** every optical and mechanical property of water in this engine is a
**global constant**, so all water is the same water. Body *identity* already exists; nothing consumes
it except wave amplitude.

### 1a. A correction to the premise, stated up front

The user's target look is right; one of its stated causes is not, and grounding the work on the wrong
cause would produce numbers that fail an audit.

**Open ocean is the CLEAREST natural water on Earth** — Jerlov type I, Secchi depth 30–50 m. It is not
sediment-laden. What makes it read opaque from above is that it is *kilometres deep* (no light
returns) plus grazing-angle Fresnel. Sediment/turbidity is a **coastal, river-mouth, and lake**
phenomenon (glacial flour, algal bloom, runoff) — a eutrophic lake at Secchi 1 m is far murkier than
any open ocean.

**Therefore the model uses two independent inputs — turbidity and depth — not "ocean = murky".** This
produces exactly the requested look (opaque ocean, variably clear lakes) and survives an audit:

- ocean → clear water, but so deep that Beer-Lambert kills everything → reads opaque;
- glacial/eutrophic lake → genuinely high turbidity → reads milky even when shallow;
- alpine tarn → clear *and* shallow → you see the bottom.

---

## 2. The unifying primitive: the WATER PROFILE

One struct, derived per body, consumed by every water shading path:

```
WaterProfile {
    float turbidity;    // optical: drives extinction magnitude, spectral flattening, in-scatter
    float waveEnergy;   // mechanical: fetch-limited significant wave height scale (0..1)
    float mss;          // mechanical: mean-square surface slope -> micro-roughness AND reflection blur
}
```

`mss` is the load-bearing one: **the same scalar drives choppiness and reflection sharpness**, which
is why "still lake mirrors, moving ocean doesn't" falls out of one physical parameter instead of two
art hacks that can disagree.

### 2a. Transport — constrained by push-constant space (verify before designing around it)

- **Sea clipmap push block is FULL:** `mat4 viewProj` (64 B) + 4× `vec4` = **exactly 128 B**, the
  Vulkan guaranteed minimum. No room. ⚑Verify the device's real `maxPushConstantsSize` (the dev
  target is a 4090, likely 256 B) before assuming; but design for 128.
- **So the profile rides the hydrology texture**, widened `R32G32_SFLOAT → R32G32B32A32_SFLOAT`:
  `R = level, G = waveEnergy, B = turbidity, A = mss`. A 256² RGBA32F texture is 1 MB, uploaded
  **once per world** on the memoized bake — effectively free. Stays NEAREST (basins are
  piecewise-constant; filtering across a divide tilts water — the P1 lesson).
- **Spectral extinction is reconstructed in-shader from the single `turbidity` scalar**, not shipped
  as a vec3 — there are not enough channels, and a 1-parameter family is what the science actually
  supports (see §3). Authored per-body full-colour tints are a v2 concern; the world-level override
  can carry a tint triple in the `water` block if needed.
- **Per-cell water (rivers/creeks/ponds) is out of scope** and passes a **DEFAULT profile chosen so
  the shading is bit-identical to today**. That equality is a test, not a hope.

### 2b. Derivation (engine) + override (authoring)

Derived from `WaterBodyIndex` + the hydrology bake:
- `turbidity` ← body class + estimated depth (`volumeEst / areaCells`) + a world default;
- `waveEnergy` ← **fetch** measured along the wind direction across `bboxMin..bboxMax` (§4), which
  replaces the current `log2(areaCells+1)/10`;
- `mss` ← wind speed via Cox–Munk (§4).

Override surface: a `water.profiles` block in `game.json` (world defaults + per-body by id or by a
world-space point that resolves to a body), persisted in the world recipe like `seaLevelY` — so a
hand-tuned lake survives a reload. **Stored value wins; loader WARNs on mismatch** (the established
`WorldRecipe.seaLevelY` pattern).

---

## 3. Phase W2 — Variable transparency

**Deliverable:** replace the two global consts with a turbidity-parameterised optical model.

- **Authoring knob is Secchi depth** (`Z_SD`, metres) — the one optical property of a real lake you
  can actually look up — converted to a diffuse attenuation coefficient via the **Poole & Atkins
  (1929) relation `Kd ≈ 1.7 / Z_SD`**.
- **Spectral shape anchored on Jerlov water types** (oceanic I / IA / IB / II / III; coastal
  1C…9C): clear water absorbs red ~10× faster than blue (the current constant's shape, Pope & Fry
  1997); as turbidity rises, attenuation **increases overall and flattens spectrally** (sediment
  scatters broadband), which is precisely why murky water goes grey-green rather than deep blue.
  The shader interpolates between a clear-water endpoint and a turbid endpoint in log space.
- **In-scattering rises with turbidity** — turbid water is *brighter* at shallow depth (light comes
  back off suspended particles) even as it becomes less transparent. Without this, murky water
  just goes black, which is the classic wrong-looking result.
- ⚑**HONESTY NOTE for the implementation comment:** `Kd` is the diffuse attenuation coefficient for
  downwelling irradiance, **not** the beam attenuation `c` this shader's Beer-Lambert term
  technically wants. For a shading model `Kd` is the correct *perceptual* scale (it is what Secchi
  measurements and Jerlov tables report). Say so in the shader; do not quietly conflate them.

**Validation**
- **L2 (red-before-green):** a pixel probe over a frame containing two bodies of different profile
  must show **different mean transmittance at equal water thickness**. On `main` today this is
  *identically zero difference by construction* — the const is global. Capture that red first.
- **L2:** default-profile per-cell water (rivers/ponds) must render **pixel-identical** to the
  pre-change build at a fixed vantage. This is the no-regression gate for the out-of-scope path.
- **L4:** same-vantage captures of ocean / clear lake / turbid lake, with measured mean B−R and
  luminance over fixed crops (the v2 ocean-slab precedent).

⚠️ **`water_underwater.frag` carries its own hand-tuned constants**, and its Phase-1 L4 proof was a
*numeric prediction* matched to measurement. Per-body extinction must reach the underwater overlay
too, or the fog will disagree with the surface and breaking the surface will pop. Treat the
underwater overlay as a first-class consumer of the profile, not a follow-up.

---

## 4. Phase W3 — Levels of choppiness

**Deliverable:** wind becomes a real parameter, and body geometry decides how much sea a body can build.

- **Wind speed + direction as world state** (today: a hardcoded direction and one global amplitude).
  Exposed via the existing `water_waves` debug command, extended with speed.
- **Fetch-limited wave growth (SMB / CERC shore-protection formulation)** replaces
  `log2(areaCells+1)/10` — significant wave height grows with wind speed *and* the distance the wind
  blows over open water. `WaterBodyIndex` already stores `bboxMin/bboxMax`, so fetch along the wind
  direction is a cheap projection. **A pond has metres of fetch and physically cannot build a
  swell**; a 6 km lake can. This also finally grounds a value whose own code comment admits it is
  keyed to "a ~1024-cell reference".
- **Micro-roughness varies — the missing piece that makes "glassy" possible.** Scale
  `waterRippleNormal`'s `a1..a4` by **Cox & Munk (1954) mean-square slope**, the canonical
  sun-glitter measurement relating surface slope variance to wind speed. Calm → σ² → ~0 → the
  surface stops scattering the specular lobe and becomes a mirror. Today a dead-calm tarn has
  exactly the same micro-chop as a gale.
- **Whitecap coverage grounded on Monahan & O'Muircheartaigh (1980)** — whitecap fraction rises as
  roughly the 3.4th power of wind speed and is **essentially zero below ~4 m/s**. That kills foam on
  a still lake for a *measured* reason instead of a dialled threshold, and replaces the current
  crest-steepness-only gate.

⚑**All coefficients above are auditor-gated.** The relations are named and real; the exact constants
must be verified against the cited sources by `grounding-auditor` before they land in code. Do not
ship a remembered coefficient.

**Validation**
- **L2 (red):** two bodies with different fetch/wind in one frame must differ in measured
  row-to-row luminance variation (the v3 Phase-2 "vertical structure" metric — flat 0.405 → swell
  2.112 is the existing precedent). Today energy already varies amplitude, so the red must target
  the **new** signal: **micro-roughness**, e.g. specular-highlight sharpness / high-frequency
  variance at a fixed vantage, which is invariant today by construction.
- **L2:** `mss → 0` must converge to a measurably *sharper* specular lobe, not merely a darker one.
- **Stress:** wind 0 (dead calm — must not divide by zero or produce NaN normals), storm-force wind,
  a body whose bbox is 1 cell, a long thin body with wind along vs. across it (fetch anisotropy is
  the whole point — assert the two differ).

---

## 5. Phase W4 — Reflection worth having (SSR)

**This is what "reflects really well" requires.** Today a mirror-flat lake reflects a procedural sky
gradient; the mountain behind it does not exist as far as the water is concerned.

**Deliverable:** screen-space reflections in `water_common.glsl`, marched against the **already-bound**
scene depth buffer, sampling the **already-captured** scene-colour copy, with the existing
`waterSkyReflection` as the fallback.

- **Ray march** the reflected view vector in screen space; on a depth hit, sample scene colour; on
  miss / off-screen / behind-camera, fall back to the procedural sky (which is already good and
  day/night-correct).
- **Roughness-driven lobe:** jitter/blur the reflection by the **same Cox–Munk σ²** as W3. Glassy →
  a sharp mirror of the treeline. Choppy ocean → the reflection breaks up and washes toward sky.
  **One parameter, both behaviours** — this is the design's payoff.
- **Fresnel is already correct** and stays; SSR only replaces *what* is being reflected.
- **Why SSR over planar:** it works on **displaced** Gerstner geometry (a planar mirror pass assumes
  a flat plane — the sea is not one), needs no second scene render, and leaves the known-broken
  mirror pass untouched. Planar remains the documented fallback for a hero glassy-lake case if SSR's
  disocclusion artifacts prove worse than its gain.

⚠️ **RISK — the reflection source is a HALF-RES copy.** `PostProcessor::captureRefraction` is half-res
*because refraction is a blurred lookup*. **A mirror is not a blurred lookup.** A glassy lake
reflecting terrain off a half-res source may read soft/aliased at exactly the moment the feature is
supposed to impress. Decide with a measurement, not a guess: A/B a full-res capture against half-res
at a glassy vantage and report both the visual delta and the frame-time delta. Full-res doubles the
per-frame blit (~16.6 MB vs ~4.2 MB at 1080p, per the v3 Phase-1 note).

⚠️ **Verify first:** confirm the sky is written *inside the scene pass* (so it is present in the
colour copy and available as an SSR hit). If the sky is drawn elsewhere, SSR misses become sky-less
and the fallback carries more weight than assumed.

**Validation**
- **L2 (red):** at a glassy vantage with distinct terrain behind the water, the water crop's
  correlation with the (vertically mirrored) terrain crop must rise from ~noise to strongly positive.
  On `main` it is noise by construction — there is no scene reflection at all.
- **L2:** reflection sharpness must **fall measurably as `mss` rises** (same vantage, wind swept
  low→high). This is the "ocean reflects less" claim, and it must be a measurement, not a look.
- **L4:** same-vantage before/after at (a) a calm lake with a mountain behind it, (b) a windy ocean.
- **Perf (mandatory, Release only):** SSR is a per-fragment march over a surface that can cover the
  whole screen — the single largest regression risk in this plan. Use the existing
  `scratchpad/water_perf_probe.py` protocol (4 fixed vantages, 25 warm-up + 30 samples, medians)
  and **report the number even if it is bad**. Budget the march length and step count explicitly.

---

## 6. Phase W5 — Weather coupling (optional, cheap once W3 exists)

Wind speed drifts over the day/night cycle → a glassy dawn that roughens into a choppy afternoon.
Everything already keys off wind speed by then, so this is a driver, not new shading. Deferred until
W2–W4 are measured; listed so it is not mistaken for an oversight.

---

## 7. Order of work

**W1** (profile plumbing — no visible change, and that is the test) → **W2** (transparency: cheapest,
directly answers "water is very transparent") → **W3** (glassy vs choppy) → **W4** (SSR: biggest
payoff, biggest cost/risk) → **W5** (optional).

W1 first is not ceremony: W2/W3/W4 all consume the same channel, and building it once means the two
renderers cannot drift apart — the reason `water_common.glsl` exists at all.

---

## 8. Testbed & stress phase

**Testbeds:** `WaterLab` (authored, flat-sea mode, fast) and `RiverLab` (real bake, 3,547 bodies,
perched lake at y≈325 — the case that killed the flat-plane attempt). **Neither currently frames an
ocean + a large lake + a pond together**, which is what a per-body A/B needs: building or choosing
that vantage set is a W1 deliverable, with cameras recorded in the lab's `game.json` and reference
baselines committed to `docs/water-refs/` (the established pattern).

**Stress axes** (the scaling dimension for this feature is *number and diversity of bodies in one
frame*, plus *extremes of each parameter*):
- **Count/diversity:** RiverLab's 3,547 bodies — assert the per-column texture upload stays one-shot
  and no per-body CPU work enters the frame loop.
- **Extremes:** turbidity 0 and max; wind 0 (dead calm) and storm; `mss` 0 (perfect mirror — check
  for NaN normals and specular blow-out).
- **Boundary crossing:** a body straddling the bake edge (beyond ±16 km the texture lookup falls back
  to flat sea at full energy — assert the profile falls back coherently, not to a hard seam).
- **Camera:** grazing angle across a long coast, directly overhead, half-submerged at the waterline,
  and **fully submerged** (the underwater-overlay agreement check from §3).
- **Time:** noon and midnight for every claim (the v3 Phase-1 day/night probe is the precedent).

---

## 9. Perf posture

The engine's standing #1 render issue is density, and water adds **transparent overdraw**. W1–W3 are
shader math plus a one-off 1 MB texture upload — expected to sit under the noise floor, but measured
anyway. **W4 (SSR) is the one item with a real, expected cost.** Every phase measures with the
existing water perf protocol in **Release** (Debug lies), and numbers are reported as measured,
including regressions.

---

## 10. Known traps (carried forward — these have each cost a session)

1. **`build_shaders.bat` does NOT track `#include` deps.** After every `water_common.glsl` edit,
   force-compile `water.frag` **and** `water_cell.frag`.
2. **`set_camera` right after `launch_engine` is silently overwritten** by the project's camera
   config. `get_camera` and confirm before trusting any frame.
3. **A restart empties the water sim**; it refills over ~1 min. Check `total_mass` is *rising* before
   concluding a change broke rendering.
4. **Debug is not a perf measurement.** Release only, with warm-up frames.
5. **Eyeballing a water screenshot is not evidence** — the v3 underwater state was first called "not
   working" off a screenshot and was actually correct; a numeric prediction settled it.
6. **Descriptor rewrites need an idle device** — widening the hydrology texture format touches a
   possibly in-flight set (`vkDeviceWaitIdle` first, as the existing upload path already does).

---

## 11. Open questions

- **Half-res vs full-res reflection source** (§5) — decide by measurement, not preference.
- **Does the authoring override key on body id or on a world point?** Body ids come from CC labeling
  over the bake and are **not stable across a regen** — a world-space point that resolves to a body
  is probably the durable handle. Decide before the override lands in a shipped `game.json`.
- **Should wind live in `game.json`?** (Carried over unresolved from `WaterPhysicalFeelPlan.md` §0.)
- **Water vs. OIT ordering** (glass seen through water) remains unchanged and untested from v3 —
  SSR does not fix it and may make it more visible.

## References

[`docs/WaterSystemV3.md`](WaterSystemV3.md) (the look plan this extends),
[`docs/WaterPhysicalFeelPlan.md`](WaterPhysicalFeelPlan.md) (physical behaviour; §0 current state),
[`docs/WaterSystemV2.md`](WaterSystemV2.md) (scale: bake, following region),
[`docs/RenderOptimization.md`](RenderOptimization.md) (render-density context).

**External sources to be verified by `grounding-auditor` before any constant ships:** Pope & Fry
(1997) pure-water absorption · Jerlov optical water types · Poole & Atkins (1929) Secchi–Kd relation ·
SMB/CERC fetch-limited wave growth · Cox & Munk (1954) sea-surface slope variance · Monahan &
O'Muircheartaigh (1980) whitecap coverage.

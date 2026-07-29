# Water — Physical Feel Plan

> Status: **PLAN, nothing built** (written 2026-07-28). Successor workstream to
> [`docs/WaterSystemV3.md`](WaterSystemV3.md), which made water *look* like a volume (refraction,
> absorption, swell, surf, flow shading). This plan is about making it *behave* like water against
> the world: breaking on land, splashing off voxels, moving grass, and running downhill in rivers
> you can actually go and look at.
>
> Standing discipline applies (⚑GROUND every dimension, red-before-green, named validation layer
> L1–L4 per deliverable) — **plus the look-first rule in §7, which exists because ignoring it cost a
> day of visual regressions on 2026-07-28.**

---

## 1. Honest starting position

**What water does today**
- Sea: a Gerstner swell on a camera-following radial mesh, with whitecaps, screen-space refraction,
  Beer-Lambert absorption, a soft shoreline, a depth-driven surf band, and underwater fog.
- Sim: a mass-conserving CA with momentum, a per-cell flow proxy, sub-voxel floors, and waterfall
  lips that already spawn `VfxSystem` mist.
- Rivers: the drainage network supplies a kinematic flow direction for baked channels.

**What it does NOT do — the four things this plan targets**
1. **Waves ignore land.** The swell travels in one global wind direction. It does not refract, so an
   island gets a foam ring that correctly wraps its contour while the crests march straight past it.
   Nothing steepens, nothing peaks, nothing breaks *on* anything.
2. **Nothing splashes.** Water meeting a voxel wall produces no spray. The only particles in the
   whole water system are waterfall mist.
3. **Water and vegetation ignore each other.** Grass has a displacer system, but it holds **16**
   spheres — enough for characters, useless for a coastline. Grass standing in the surf neither
   bends nor looks wet.
4. **Rivers have never been seen.** This is a real gap, not a small one: the flow proxy and the
   kinematic river direction were built and unit-tested, but WaterLab is an authored world with no
   hydrology bake, so **no river has ever been rendered**. Everything claimed about river flow rests
   on tests, not on looking at one.

---

## 2. The unifying primitive: the SHORE FIELD

Three of the four gaps need the same missing fact: *for this point on the water, where is land, how
far, and in which direction?* Building that once, well, is most of the work.

**What it is.** A CPU-baked 2D field that follows the player, per cell:

| channel | meaning |
|---|---|
| `depth` | water depth here (water surface Y − terrain top Y), negative on land |
| `shoreDist` | signed distance to the waterline (+ in water, − on land) |
| `shoreDir` | unit XZ direction pointing from land toward open water (the gradient of `shoreDist`) |
| `cliffiness` | local slope of the terrain — separates a gentle beach from a vertical sea wall |

**How it is built.** Terrain column heights come from the chunk system; the waterline is the
sea-level contour (or the baked hydrology level per column, when one exists). The distance transform
is a jump-flood over the grid — a handful of passes, trivially parallel, and it runs on the existing
`JobSystem` off the main thread. It recentres with the player exactly as the water sim region does.

⚑GROUND **cell size 2 m over a 512 m window = 256×256 cells**. 2 m is a quarter of the shortest
swell component (~8 m), which is the resolution refraction needs to bend smoothly; 512 m comfortably
covers the wave zone. Cost: 256² × 16 B = **1 MB**, rebuilt incrementally.

**Who consumes it**
- `water.vert` — refraction and shoaling (needs a **vertex-stage sampler**; the water descriptor set
  is currently `FRAGMENT_BIT` only, so its stage flags must widen).
- `water_common.glsl` — surf placement keyed to real distance-from-shore instead of view-ray depth.
- CPU — spray emitter placement, grass swash, and future gameplay (is this point in the surf zone?).

**Validation.** **L2** on the distance transform against analytic shapes: a circular island must give
radially symmetric `shoreDist` and `shoreDir` pointing outward at every bearing; a straight coast
must give a linear ramp and constant direction. **L4** a debug overlay that draws the field so it can
be eyeballed. Red-verify by feeding a known island and asserting the direction field.

**Risk.** This is the load-bearing dependency for §3–§5. If the bake is wrong, everything downstream
is subtly wrong in ways that look like shader bugs. Build and visualise it *first*, alone.

---

## 3. Phase A — waves that attack the coast

Depends on §2. This is the "a round island should have waves moving toward it" ask.

- **Refraction.** Each Gerstner component's direction rotates from the wind heading toward
  `shoreDir` as depth falls. Real waves turn to run parallel to the depth contours, which is why
  surf wraps an island and arrives head-on from every bearing.
  ⚑GROUND: Snell's law for water waves, `sin θ / c` constant, with shallow-water celerity
  `c = sqrt(g·d)`.
- **Shoaling.** Amplitude *grows* as the wave enters shallow water before it breaks.
  ⚑GROUND: **Green's law, H ∝ d^(−1/4)**.
- **Compression.** Wavelength shortens in shallow water (`L = c·T`, `c = sqrt(g·d)`), so crests
  bunch up as they approach — the visual signature of an incoming set.
- **Breaking.** Cap amplitude at the McCowan limit **H = 0.78·d** (already the surf criterion), and
  past that point sharpen the crest and hand off to foam and spray.

**Watch the two shader footguns already documented in `water_common.glsl`:** never let an offset
grow with time, and never warp the sample coordinate anisotropically. Refraction rotates the wave
*direction*, which is a different thing from warping the sample point — keep it that way.

**Validation.** **L2** on the refraction helper: a synthetic circular island must produce wave
directions converging on it from all bearings. **L4** build a literal round island in WaterLab and
photograph it from above — the crests should curve in. That screenshot is the acceptance test for
this phase, and it is exactly the picture that is impossible today.

---

## 4. Phase B — splash and spray against voxels

The wave function is deterministic and cheap, so the **CPU can evaluate the same Gerstner sum the
vertex shader does**. That means the CPU knows where crests are, and combined with the shore field it
knows where they break — so it can place particles without any GPU readback.

- **Breaker spray** along the break line: `VfxSystem::spawnBurst` with a `Cone` shape aimed up and
  shoreward, rate scaled by wave height.
- **Impact spray on vertical faces.** Where `cliffiness` says the shore is a wall rather than a
  beach, a crest hitting it throws a much taller, narrower plume — the sea-wall look, and the thing
  that makes water feel like it has mass.
- **Reuse the existing pattern.** Waterfall mist already does continuous `spawnBurst` from detected
  lips with a `MAX_WATERFALLS = 48` cap; splash should copy that structure, including the cap.
- **Plunge-pool foam** where a waterfall lands: a persistent foam disc, not just airborne mist.

⚑GROUND emitter budget: a hard cap in the same class as `MAX_WATERFALLS` (48), chosen against the
particle system's own budget, with the count per emitter scaled by wave energy.

**Validation.** **L2** emitters land on the break line and inside the cap under a stress case (a long
convoluted coast). **L4** the coast at a fixed vantage, before/after. **Perf** measured in Release —
particles are the classic way to quietly lose 5 ms.

---

## 5. Phase C — water moves the world (grass, wetness)

- **Swash.** Grass inside the surf band bends away from the incoming wave and springs back as it
  retreats. This must be a **field**, not the displacer array — 16 spheres cannot describe a
  coastline. Feed `grass.vert` the shore field plus the wave phase so each blade derives its own
  bend, exactly as it already derives its own wind response.
- **Submerged grass** should flatten and damp — grass under water does not wave in the wind.
- **Wetness.** Terrain within the swash band darkens, and the darkening *lags* the retreating wave.
  A wet-sand band that recedes after each wave is one of the strongest cues that water is real. This
  touches the terrain shader (`voxel.frag`) — a small, contained change, but outside the water pass,
  so it needs its own before/after.

**Validation.** **L2** the swash bend is zero outside the band and continuous across it (no popping
at the boundary — the same class of defect as the flow-tiling bug). **L4** stand in the surf and
watch. **Perf** grass is already a heavy pass; measure it, do not assume.

---

## 6. Phase D — rivers you can actually see

**This phase starts by fixing the reason rivers are invisible, before touching any shading.**

- **D0 — a river locator + testbed.** A `water_find_river` debug command that scans the baked
  `FlowField` for the nearest channel of order ≥ N and returns its world position (and can put the
  camera there). Rivers are ~5 voxels wide and can be kilometres inland; hunting for one by hand is
  why this has never been looked at. Then a **RiverLab** project: streamed, height-based, hydrology
  baked, with a saved camera on a known order-3+ channel. **Nothing else in this phase is
  meaningful until you can get to a river in one command.**
- **D1 — verify what already exists.** The flow proxy and kinematic river direction have L2 tests
  and zero L4. Confirm at runtime that a baked river reads as flowing, and report honestly if it
  does not.
- **D2 — river surface.** Bank foam where fast water meets the bank (shore field again), streaks
  advected along the flow (already built), and **obstacle wakes** — a bow wave upstream of a voxel
  standing in the current and a foam tail downstream. That is the river equivalent of "splashes
  against static voxels".
- **D3 — waterfall polish.** Spray density scaled by drop height, plus the plunge-pool foam from §4.
- **D4 — honest limit.** A baked river is a *pinned reservoir*: it does not advect mass, so nothing
  floats down it. Real transport needs the Phase 4A capacity work plus genuine inflow/outflow. Call
  that out rather than implying rivers "flow" in a physical sense when they are shaded to look like
  they do.

---

## 7. How this work gets judged (the process fix)

On 2026-07-28 a day of individually-justified changes made the water look worse, because each was
validated against a *metric* (perf, Nyquist sampling, physical grounding) and never against the
previous image. The rules now:

1. **Keep a reference screenshot** of the best-known state, at a fixed camera, committed alongside
   the work.
2. **Every visual change gets a same-vantage A/B** against that reference before it is committed.
3. **A metric improving is not evidence it looks better.** Perf wins, correct sampling and honest
   physical grounding are necessary, not sufficient. The question is always "is this prettier than
   the reference?" — and if it is not, the change does not land, however correct it is.
4. **Useful measured proxies:** fine-scale roughness at 4 px in an open-water crop catches ridging
   and corduroy (good look ≈ 1.7, over-detailed ≈ 5.0); row-to-row luminance change catches relief;
   frame-to-frame delta in a fixed crop proves motion is real rather than painted.
5. **Slope, not amplitude, is what the eye sees** for surface detail: an octave's visual weight is
   `amplitude × frequency`. Adding octaves without holding the slope sum roughly constant is what
   produced the corduroy.

---

## 8. Suggested order, and why

| # | Phase | Why here |
|---|---|---|
| 1 | §2 Shore field + §6 D0 river locator | Unblocks everything; and D0 is the difference between rivers being reviewable or not. Cheap, high leverage, no visual risk. |
| 2 | §3 Phase A — refraction, shoaling, breaking | The biggest single step toward "physical", and the direct answer to waves-should-move-toward-land. |
| 3 | §4 Phase B — spray | Turns a shading effect into something with apparent mass. Needs A's break line. |
| 4 | §6 D1–D3 — rivers | Makes an already-built system visible and finishes it. |
| 5 | §5 Phase C — grass and wetness | Highest polish-per-risk once the field exists, but it touches the grass and terrain passes, so it goes after the water is settled. |

---

## 9. Decisions needed before starting

- **Shore-field window size vs cost.** 512 m at 2 m/cell is the proposal. A bigger window means
  refraction that keeps working as view distance grows; it costs memory and bake time.
- **Does the sea get a wind direction per world?** Refraction needs a base heading; today it is one
  shader constant. It probably belongs in the `water` block of `game.json` alongside amplitude.
- **How physical should rivers get?** D2 shading is cheap and convincing. Real advection (things
  floating downstream) is Phase 4A-scale work and should be a deliberate decision, not a drift.
- **Wetness on terrain** means editing the voxel fragment shader, which every material in the game
  goes through. Worth confirming that is acceptable before starting §5.

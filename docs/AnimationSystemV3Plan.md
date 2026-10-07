# Animation System v3 — the plan for "the animation system of my dreams"

> **CLOSED AS A PLAN 2026-10-01 — this is now the BUILD LEDGER for A0–A5.** Forward work (A6,
> A7, T2, T3, §8, §9) moved to [`CharacterAnimationRoadmap.md`](CharacterAnimationRoadmap.md)
> (phases R1–R6; T2/T3 rejected there with reopen triggers). Read §4b for what was built.
>
> **Status (historical):** proposal, 2026-09-29. **Nothing here is built.** Every phase goes through
> `/design-check` before a line of code, and the owner picks the forks in §9.
> **Gate log:** 2026-09-29 slice A0+A1 — **A0 READY**, **A1 NEEDS WORK** (three items designed
> into A1's preamble); **A1 re-gated READY** the same day. Owner: "lets get going on A0 and
> A1" (2026-09-29). **A0 DONE (57/57 + 11/11) and A1 DONE (headless 23/23, live-verified on two
> patrolling NPCs) the same day — all uncommitted.** **A2 gated READY (owner forks A2-1=(a),
> A2-2=retire) and IN PROGRESS 2026-09-29** — red tests, rig retirement, two Quaternius plans and
> the runtime edits E1–E7 are in; §4b has the red→green ledger, the live-caught morphology-
> routing bug + fix, the render-probe falsification and the oracle window-length fix. **A2 DONE**
> (uncommitted). **A3 gated READY and BUILT 2026-09-30** (schema, plan table, transition graph,
> masks/layers, grip derivation, modifiers, sword/hammer pair, off-hand pin — ledger in §4b).
> **A4 (the chair) gated READY, BUILT and CLOSED 2026-09-30** (SeatFit engine-side, v2 sidecars,
> W1, one seated origin + per-frame seated solve, 102-cell matrix, live gauntlet 21/21; the
> approach walk is DEFERRED to its own gate; ledger in §4b). A5 gated 2026-09-30: NEEDS WORK (5 items) → folded → re-gated READY → BUILT → **CLOSED 2026-10-01** (owner: "closing A5 for now seems fine"; still-foot float 0.147 u on 1/3 u risers vs the flat + 0.03 bar is accepted as the residual; ledger in §4b).
> **Supersedes nothing, extends everything:** [`CharacterAnimationV2.md`](CharacterAnimationRoadmap.md)
> (2026-07-09) remains the paradigm analysis this builds on; its rules are restated in §2, not
> re-argued. Companion facts: [`LessonsLearned_ProceduralAnimation.md`](LessonsLearned_ProceduralAnimation.md),
> [`MotionBricksIntegrationPlan.md`](CharacterAnimationRoadmap.md),
> [`UniMateIntegrationPlan.md`](UniMateIntegrationPlan.md), [`CharacterLibraryPlan.md`](CharacterAnimationRoadmap.md),
> [`InteractionPipeline.md`](InteractionPipeline.md), [`FunctionalWiringBacklog.md`](FunctionalWiringBacklog.md)
> (the USER DIRECTION block on seating).
>
> Written from three surveys taken 2026-09-29: every design doc on animation, the runtime code
> (`AnimatedVoxelCharacter`, `scene/motion/*`, behaviours, `Application.cpp` sit/held/equip paths),
> and git (branches, stash, uncommitted). Citations are `file:line` at that snapshot.

## 0. The dream, stated honestly

Owner, 2026-09-29: *"in a perfect world the animations would be completely dynamic and AI driven
with little to no predefined animations… For instance with the sit animation, I can picture 100
different sitting positions that could arise just from the size of a chair."*

Three different technologies hide inside "completely dynamic", an order of magnitude apart in cost:

| Tier | What it is | What you get | What it costs | Where the engine is |
|---|---|---|---|---|
| **T1 — constraint-adapted priors** | A small set of motion *priors* (mocap or generated clips) bent to the world at runtime: contact IK, phase-aware foot locks, data-driven transitions | The chair example: ONE sit family + a solver that puts pelvis on *this* seat, feet on *this* floor, back on *this* backrest, hands on *these* armrests. Seat height/depth become inputs, not assets | Weeks per feature; ships in this codebase | Closest. 2-bone IK, foot lock, body spring, clip_meta, BodyPlan skeleton all exist (§1) |
| **T2 — learned kinematic motion** | A small neural net in-engine produces the pose from intent + geometry every frame (Starke's Neural State Machine = "sit on any chair, carry any box" learned from mocap; MotionBricks = the locomotion flavour) | Most of the "no predefined animations" feeling; adapts to unseen furniture | Months + an ML pipeline + training data; less authoring control | The seam exists and is WIRED: `IMotionSource` → `AnimatedVoxelCharacter::update` (§1.5). MotionBricks measured too slow for crowds (2.4 s first window / 10 agents) |
| **T3 — physics-based learned control** | Articulated actuated body + RL policy (DeepMimic / ProtoMotions) | True reactions, balance, pushes | Quarters; research not authoring; per body plan; external sim whose contacts won't match `VoxelDynamicsWorld`; needs an articulated solver + ML runtime the engine lacks | Nowhere. Hand-tuned version was failure #2 |

**Position of this plan:** build **T1 as the system**, with **T2 plugged in behind the seam that
already exists** (one named research spike, not a bet), and **T3 parked with written unpark
conditions**. Roughly 80 % of the pictured result comes from T1 done properly; the remaining 20 %
is exactly the expensive part. "AI-driven" in the sense of an LLM deciding *what* a character does
is cheap and orthogonal — the hard problem is motion synthesis, and this plan is about that.

## 1. Ground truth, 2026-09-29 (what the code actually does)

Only the facts the plan depends on. All from the runtime survey; AVC = `engine/src/scene/AnimatedVoxelCharacter.cpp`.

### 1.1 What exists and works
- **One production path:** `AnimatedVoxelCharacter` — 35-state FSM (AVC.h:43-79), kinematic capsule
  integrator with mantle/ladder/step-glide (AVC:82+), rigid voxel boxes parented to bones, 156 clips
  in `humanoid.anim`, data-driven melee/spell family mappers, `clip_meta` per-clip tuning.
- **BodyPlan abstraction exists (partial):** `m_bodyPlan` supplies `clipDefaults`, `legs`, `footIK`
  bones, the hip/root bone (AVC:2543, 4279-4297, 1847). It is the P0b foundation from V2 — begun,
  not finished (§1.3).
- **Foot IK exists, off by default:** 2-bone law-of-cosines + foot lock + body spring
  (AVC:4320-4988), stair mode via `contactFrame1/2`, terrain-follow mode with swing gate. Enabled
  only for the anim-editor character (`Application.cpp:20494…`); no gameplay character uses it.
- **Learned-motion seam exists and is wired:** `IMotionSource{submitIntent, sample, reset, status}`
  (`scene/motion/MotionSource.h:7-15`), `MotionIntent`/`LocalPoseFrame`/`MotionSourceStatus`
  (`MotionTypes.h`), attached to the **player only** (`Application.cpp:5780`), consumed after clip
  evaluation in locomotion states (AVC:3855-3920): slerps retargeted **rotations only**, 0.15 s ramp,
  fades on underrun; `rootTranslation` unused; capsule stays clip-authoritative. Gated by
  `PHYXEL_MOTION_PROVIDER=motionbricks`. Committed on main 2026-09-21 (`154d7244`).
- **MotionOracle + CombatTransitionMap exist — tests only** (`scene/motion/MotionOracle.h`,
  `CombatTransitionMap.h`, `tests/scene/motion/*`). Nothing in the runtime calls them. This is V2's
  P0a, half-shipped.
- **Offline quality gates exist:** `anim_lint.py` (unit quats, 120° jumps, loop gap, calibrated
  envelopes, and since 2026-09-29 the **stance-feet body-velocity / foot-slide metric**), `clip_metric`,
  the interaction detectors (`tools/interaction_pipeline/…/detectors.py:673-685`), the seat
  gauntlet (`validity_gauntlet.py`, 27/27 valid after the root-bind fix), creature-forge gates.
- **Generated clips flow in:** UniMate → `unimate_*` drafts → review ledger → promote
  (`UniMateIntegrationPlan.md`); 18 pending the owner's verdicts.

### 1.2 What is structurally missing
- **Blending:** a single two-clip whole-skeleton crossfade, one global 0.2 s (AVC:3835;
  `AnimationSystem.cpp:288-349`). No blend tree, blendspace, bone masks, additive layers, sync
  groups, or foot-phase matching between clips. Open owner item: "upper-body-only casts while
  moving (needs bone masking)" (`AgentContext.md:1591`).
- **Three clip-selection sites** that disagree: input-driven FSM (AVC:3672-3759), NPC
  external-velocity path (3473-3515, picks only Walk/Idle, never processes hit reactions), sit branch
  (3372-3410). `CharacterLibraryPlan.md:192-196` counted the same fork.
- **No contact/constraint layer.** Seating anchors ONE bone (Hips) by teleporting `worldPosition`
  (AVC:1926-1935, 3422-3442); nothing solves feet/back/hands against the furniture. Foot IK and the
  spring are *disabled* while sitting (4404-4411) because they fought the anchor.
- **No IK beyond 2-bone legs.** No hand IK, no FABRIK/CCD, no look-at, no spine solve.
- **Root motion:** root hardcoded as **bone 0** (AVC:3955, 4016) while the hips reference uses the
  plan root; extraction pauses during blends (4025-4029); no root-motion *extraction* tool for
  authoring (stair clips have no Y keys — MEM `project_stair_system`).
- **No runtime oracle:** `get_bone_positions` returns 12 segment boxes, not the skeleton; there is no
  `/api/animation/validate`. Every "does it look right" still ends at the owner's eyes.

### 1.3 Defects found by reading (cheap, deconfounding — Phase A0)
1. **`blendDuration` is zeroed forever after the first sit** (AVC:3406-3408, never restored). Every
   later transition on that character is a hard cut. Nobody noticed because sit is rare.
2. **Two sit offsets are dead:** `m_sitDownOffset`, `m_sittingIdleOffset`, `m_sitBlendDuration` are
   written (1869-1872, 1962-1965) and never read — all three seated states use
   `m_sitStandUpOffset` (1930, 3437). Per-state profile tuning has had **no effect** all along.
3. **Interaction-editor seek double-counts `heightOff`** (`Application.cpp:12264` + AVC:1863), and
   the per-frame snap then moves the character to the wrong Y. Part of why "no offset fixes it".
4. **Phase jitter is lost on every FSM clip change** (AVC:3734, 3511 set `animTime=0`); only explicit
   `playAnimation` honours it → lock-step crowds return whenever the FSM switches clips.
5. **NPC velocity path ignores clip `Speed`:** behaviours own the speed, playback rate is never
   matched → **behaviour-driven NPCs skate by construction** (AVC:3452-3515). The new stance-feet
   metric can now measure this.
6. **`clip_meta` drops unknown keys silently** — `releaseFrame`, `meleeFamily`, `weaponFamily` are
   parsed by nothing; `cast_1h_*`/`cast_2h_*` fall back to the 0.4 default hit frame (AVC:515-558;
   `Animation.h:93`).
7. **Hot reload returns stale keyframes** (parse cache has no invalidation; `reloadAnimations` only
   re-reads meta, AVC:1797-1824).
8. **`standUp()` uses the literal clip `"idle"`** (AVC:3164) and `m_pendingClipSnap` is consumed only
   on the input path (3754-3758) — NPCs standing up blend into the wrong reference.
9. **Debug spam in release paths:** `std::cout "DEBUG: Selected TargetAnim"` every 30 frames from a
   static counter shared by all characters (AVC:3667-3676); unconditional `LOG_INFO` inside the IK
   solver per call (4347) and per frame (4665, 4784).
10. **`m_strafeLean` is not reset by `sitAt`** while the seat snap rotates by `m_seatFacingYaw` → a
    possible yaw mismatch on sit-down.

### 1.4 Seating — the measured failure catalogue (do not re-derive)
From `tools/interaction_pipeline/engine_fix_queue.json` (sweeps 2026-05-22, `chair_wood`),
`FunctionalWiringBacklog.md:23-42`, MEM `project_seating_pipeline`:

| Symptom | Measurement | Mechanism (code) |
|---|---|---|
| INITIAL_TELEPORT | feet +0.17/+0.20 m above seat_y at `stand_to_sit` t=0 (tol −0.10) | `sitAt()` anchors the FIRST frame of SitDown to the END pose of `stand_to_sit` (AVC:1909-1911, 1928-1935) |
| POSE_FEET_DESYNC | asymmetry 0.154, drift 0.147–0.178 (tol 0.10) | foot-anchor lerp on a different alpha than the bone blend |
| SEATED_THIGHS_NOT_HORIZONTAL | knee−hips Y = −0.439 m (tol ±0.35) | `sitting_idle` is not a seated pose; no leg solve while seated |
| POSITION_SNAP_AT_CLIP_BOUNDARY / BLEND_DURATION_ASYNC | centroid jump >0.10; trailing travel >0.05 | hard cuts + per-state hips-reference re-snap (~0.5 m, AVC:1114-1121, 3342-3347) |
| Three references disagree | — | `worldPosition` re-snaps per transition, `getCameraTrackPosition()` hides it (1995-2013) |
| Unscaled root bind (fixed) | pelvis error 0.48–0.74 per race preset → 27/27 valid after fix | `CharacterLibraryPlan.md:235-245` |

Owner direction (2026-08-28): *"the seated POSE is not solved… Treat it as hard."* Standing rule:
*"accuracy over coverage — a character NEVER sits where it doesn't fit."*

## 2. Rules carried over unchanged (from V2, the lessons doc, and the owner)

1. **Automated oracle before eyes.** "It doesn't work" arrives as a failing metric with a number.
   No phase lands without its oracle; manual review is the last gate (the review panel from
   `UniMateIntegrationPlan.md` M1b *is* that last gate).
2. **Red-before-green, calibrated thresholds.** Every metric fails first on a seeded/historical
   defect; humanoid thresholds from vetted mocap, creature gait thresholds from biomechanics
   literature — never invented.
3. **Do not re-attempt procedural humanoid locomotion from scratch (#1) or PD-balance ragdoll (#2).**
4. **IK is a post-process on keyframe, feet first, root-motion-aware** (the lessons doc's retry
   recipe — already what the shipped foot IK is).
5. **Don't layer complexity on a working system:** the FSM keeps working at every step; new layers
   are additive and switchable, defaults change only with a pinned test.
6. **Deterministic geometric validation, not screenshots.** Never read facing from screenshots
   (model faces +Z). "The clip plays" is not "the interaction works" — sample bone AABBs through the
   whole transition (`CharacterLibraryPlan.md:228-234`).
7. **Hybrid mocap legs for big body moves** ("if the feet move the knees don't" — reuse mocap lower
   body over DSL upper body).
8. **Variants are content:** keep approved distinct variations so repeated actions don't look
   mechanical.
9. **Generated motion is animation data, not trusted control data.** Offline drafting sources
   (UniMate) and any learned runtime source sit behind gates and a permanent clip fallback.

## 3. Target architecture — one pose pipeline, five layers, one foundation

```
   FOUNDATION  BodyPlan (finish it): bones · root · hips · limb chains (leg/arm/spine/neck/tail) ·
               end-effectors (feet, hands, head) · size scale · gait class · clip-key map
                       │ consumed by every layer; no literal bone/clip names anywhere else
   ┌───────────────────┴─────────────────────────────────────────────────────────────┐
   │ L1 SOURCE      clips (mocap · UniMate drafts · forge-generated · procedural gait)   │
   │                + T2 learned source behind IMotionSource (rotations AND root)        │
   │ L2 TRANSITION  data-driven transition graph: per-edge blend time, foot-phase sync,  │
   │                bone masks (upper-body actions over locomotion), additive layers     │
   │ L3 CONSTRAINT  the new core — contact targets from AFFORDANCES (ground, seat,       │
   │                backrest, armrest, ledge, ladder rung, grip) solved by IK chains     │
   │                from the BodyPlan; ONE world anchor per interaction                 │
   │ L4 REACTIVITY  canned reaction clips + impulse nudge on the pose (no PD ragdoll)    │
   │ L5 LOD/PERF    anim update Hz by distance (exists: 30/15 Hz), instance buffer cap   │
   └────────────────────────────────┬────────────────────────────────────────────────┘
                                    ▼
                          MotionOracle (T2 headless) + /api/animation/validate (T3 live)
                          gate EVERY layer; review panel = last human gate
```

**Why L3 is the centre of gravity.** The owner's chair example is not a *blending* problem or a
*source* problem; it is a **constraint** problem. A sit prior + a solver that satisfies "pelvis on
seat surface, feet on floor, back on backrest, hands on armrests, head level" produces the 100
sitting positions from the chair's dimensions. The same layer, with different affordance types,
gives ledges, ladders, beds, mounts, workbenches, doors — everything in `FunctionalWiringBacklog`
that currently "plays a clip" instead of "works". It is also exactly where T2 (Neural State
Machine-style) slots in later: a learned source that *proposes* the pose, with L3 still enforcing
contacts, so the fallback path and the learned path share one validator.

**Affordance = data on the object, not on the clip.** A seat declares: surface plane (height, depth,
width), backrest plane + angle (optional), armrest planes (optional), approach point, facing. Items
already have `held.gripBone/gripOffset/gripEulerDeg`; furniture has sidecar interaction points
(sparse — `FunctionalWiringBacklog` W1: `placeTemplateMicro` writes none; `chair.voxel` has no seat
point; `regen_furniture.py` would delete them). Affordances are the missing generalisation of both.

## 3b. The factor model — composition over enumeration (the "tens of thousands" problem)

Owner, 2026-09-29: *"if a character is holding a sword or a hammer they might walk different. or if
the item is one handed vs two handed… I am worried that we will end up with 10s of thousands of
animations for all the character types (D&D creatures)… I am not totally opposed to a lot of
things pregenerated, but I want a system that is flexible."*

**The trap is the multiplication.** Naively a clip per combination:

| factor | levels (today's data or the obvious set) |
|---|---|
| locomotion state | ~12 (idle, walk, run, strafe ×4, back, turn ×2, crouch-walk, jump) |
| grip class | ~8 (empty, 1H, 1H+shield, 2H light, 2H heavy, bow, staff, torch/lantern) |
| load / carry | ~4 (none, light, heavy, bulky-two-hands) |
| condition | ~4 (fresh, tired, wounded/limp, encumbered) |
| mood / stance | ~4 (neutral, alert, hostile, sneaking) |
| size class | ~5 (Tiny … Huge) |
| body plan | ~8 (biped, quadruped, hexapod, serpentine, avian, amorphous, taur, winged biped) |

Multiplied: **12 × 8 × 4 × 4 × 4 × 5 × 8 ≈ 245,000 clips.** Impossible, and also *wrong*: a sword
does not change how the legs walk, weight changes lean and cadence not the gait, size is a scale.

**Factorized:** each factor becomes a *layer or modifier* applied to a shared base, and the pose is
the composition. Clip count becomes a **sum**:

| factor | mechanism | layer (§3) | authored data needed |
|---|---|---|---|
| locomotion state | base clip set **per gait class** (biped / quadruped / hexapod / serpentine / avian) | L1 | ~12 clips × 5 gait classes = **60** (mocap, UniMate, or procedural gait for non-bipeds) |
| grip class | **upper-body mask + additive carry pose** over the base (arms/hands/shoulders, torso lean); weapon transform via `held.gripBone/gripOffset` (exists) | L2 | 1 carry pose per grip class + action sets already exist as melee families (10) = **~8 poses** |
| load / carry weight | **procedural modifiers:** forward lean ∝ mass (posture lean exists, AVC:4087), cadence/stride via playback-rate + Speed scaling, arm swing damping; bulky = two-hand carry pose (a grip class) | L2/L3 | **0 clips**, a parameter table (mass → lean°, cadence factor) |
| condition | **modifiers + one additive layer each:** limp = asymmetric playback rate + hip drop; tired = slower cadence + slump additive; encumbered = load | L2 | **~3 additive layers** |
| mood / stance | **additive idle/posture layers** (alert: head up/scan; hostile: weapon raised — is a grip pose; sneak = crouch state exists) | L2 | **~3 additive layers** |
| size class | **BodyPlan size scale**: rig scale + stride/cadence law (stride ∝ leg length, cadence ∝ 1/√leg length), capsule/step-height scale (races already do this: `heightScale`, proportion presets) | foundation | **0 clips** |
| body plan | one base set per gait class + **retarget across plans within a class** (`retarget_quadruped.py` proves it: 12-rig mocap library → any quadruped by geometric role) | L1/A6 | shared |
| variation | phase jitter, per-character playback-rate ±5 %, per-character additive offsets, 2–3 approved *variants* per hub clip ("variants are content") | L1/L2 | a few variants per hub |

**Sum:** on the order of **60 base + 8 carry + ~6 additive + ~30 action clips per weapon-family
set + variants ≈ 150–300 authored clips for the whole bestiary**, against ~245,000 enumerated. The
humanoid already ships 156 clips, most of them *actions* — the factorized model needs *fewer*
locomotion clips than exist today, not more.

**What this requires of the engine (and where it lands in the phases):**
1. **Bone masks + additive layers in the transition layer** (A3). This is the single mechanism
   behind grip, load, condition and mood. It is also the standing open item "upper-body-only casts
   while moving". Today: none (§1.2).
2. **Clip metadata as factor coordinates.** Each clip declares its coordinates
   (`gait=biped state=walk grip=any load=any …` for a base; `grip=2h_heavy mask=upper additive=1`
   for a layer). Selection = nearest base in factor space + the set of layers whose coordinates
   match the character's current factors. `clip_meta` already carries per-clip fields; this is a
   schema, not a new format. Data-driven, so UniMate/forge/mocap all fill slots.
3. **Modifier parameters on the BodyPlan / item / condition**, never on clips: item mass and grip
   class on the item definition (`held` block exists), size on the plan, condition on the character.
4. **Held-object coupling** the other way too: the *weapon* follows the hand (exists, A0 fixed the
   NPC path); a **two-handed grip** needs the off-hand IK'd to the weapon's second grip point (L3,
   same 2-bone arm solver as the armrest case in A4). One-handed vs two-handed is then: grip class
   picks the carry layer; L3 pins the second hand.
5. **Creatures:** base sets per gait class, procedural gait synthesis for the plans with no mocap
   (hexapod/serpentine/amorphous — where procedural is the right tool), retarget within a class.
   D&D size categories are the size-class scale law, not new rigs.

**Where pregeneration is welcome (owner is "not opposed"):** filling the *base sets per gait class*
and the *carry/action layers* is exactly what UniMate (text-to-motion on our own skeletons,
including quadrupeds) and the forge are for. Generate many, review in the panel, promote few. The
factor schema tells the generator which slots are empty. What must **never** be pregenerated is
the product of factors — that is the runtime's job.

**T2 is the limit of this idea:** a learned source is a network *conditioned on the factor vector*
(gait, speed, grip, load, terrain, seat geometry) emitting the pose — full factorization with zero
clips. The factor schema built here is the conditioning vector that source would consume, which is
why the schema is designed before any learned work.

## 4. Phases

Each phase: **contract → required validation layer (L1 artifact · L2 structural · L3 functional ·
L4 live · W wired) → red test → stress axis → design-check gate**. Estimates are working days for
one agent+owner loop and are honest guesses.

### A0 — Deconfound: fix the ten known defects (2–3 days) — gate: `/design-check` (small)
Contract: none of §1.3 remains. Red tests: (1) unit test — after `sitAt()`+`standUp()`,
`getBlendDuration()==0.2`; (2) per-state sit offsets change the seated pelvis (measure, not assert
"used"); (3) `ie_seek` height equals `sit_character` height for the same seat; (4) two NPCs with
different phase seeds differ in `animTime` after an FSM-driven Walk→Idle→Walk; (5) **the stance-feet
metric on a behaviour-driven NPC**: feet velocity vs capsule velocity mismatch ≤ 25 % (today: fails
by construction); (6) `clip_meta` unknown key → one WARN log, known-but-unread keys (`releaseFrame`)
either consumed or renamed in the data; (7) `reload_entity_animation` returns new keyframes after a
file edit (cache invalidation API); (8) `standUp()` uses the mapped Idle; (9) no `std::cout` /
per-frame INFO in AVC (grep test). Stress: 100 NPCs sit/stand ×10 cycles, blend duration and
jitter invariants hold on every one.

> **Gate 2026-09-29 (slice A0+A1): A0 READY — may start. A1 NEEDS WORK on three items, now
> designed below; re-gate A1 before building it.**
>
> A1 design answers folded in:
> 1. **Live capture mechanism.** A per-character **ring buffer** of full-skeleton frames
>    (`OracleFrame`: local rotations, world joint positions, root velocity, capsule velocity,
>    planted flags) filled on the main thread inside `update()` when recording is armed
>    (`/api/animation/record {id, frames}`), drained by `/api/animation/validate {id}` which
>    evaluates whatever is in the buffer and returns metrics + thresholds + frames sampled. No
>    multi-frame wait inside a queued command (queued/async commands run ON the main thread —
>    standing footgun). Buffer capped at 600 frames (10 s @ 60 Hz), cap written at the clamp.
> 2. **Planted-joint definition.** Derived, not given: a foot joint is *planted* when its world Y
>    is within 0.03 u of the clip's/segment's lowest sample for that joint (the calibrated 3 cm
>    window from `anim_lint.foot_slide_metrics`, which recovers every shipped mocap Speed within
>    1–14 %). Ported to C++ next to `evaluateMotion`; red test = the same Speed×2 walk that fails
>    in Python must fail in C++ with the same numbers (cross-language pin).
> 3. **Terrain input.** Headless runs take a declared `float ground(x, z)` (flat, slope, step,
>    sinusoid); live runs use the character's own ground query (`findGroundY` path). The
>    float/penetration band is **calibrated first** on the shipped clips over flat ground (the sole
>    measured 12 cm high pre-G-99; log hover 0.3–0.4 accepted as "good enough") and stored in
>    `calibration.json`; the metric is a WARN until that calibration exists, ERROR after.

### A1 — MotionOracle → real oracle (T2 headless + T3 live) (4–6 days) — gate required
Contract: V2 §9.2's metric suite runs (a) headless on rig+clip or rig+procedural gait in a unit
test, (b) live on a driven character via `/api/animation/validate` with a **full-skeleton per-frame
dump** endpoint (today only 12 segment boxes). Start from `scene/motion/MotionOracle.h` (exists,
tests only). Metrics: foot skate (now in `anim_lint`, port to C++), penetration/float, chain-length
drift, knee inversion, smoothness envelopes, pose continuity across transitions, root-vs-capsule
velocity, self-intersection, balance proxy, gait invariants. Red: each metric fails on a seeded bad
clip (Speed×2 walk, stretched chain, inverted knee, popped transition) and passes the vetted set.
Deliverable also: `unimate_eval.py` and the review panel read the same numbers. **This is the
prerequisite for every phase after it — no exceptions.**

### A2 — Finish the BodyPlan (P0b) (5–8 days) — gate 2026-09-29: **NEEDS WORK (2 items)**

> **Gate findings.** More exists than this section assumed: a `BodyPlan` registry with **38 plan
> JSONs** (`resources/body_plans/`), `BodyPlanTest` resolving wolf/spider/dragon/Meshy rigs, and a
> written **NEUTRALITY CONTRACT** (`BodyPlan.h:18-24`): the humanoid plan keeps an EMPTY
> `clipDefaults` and stays on the legacy switch (sprint variants + candidate lists live there),
> pinned by `HumanoidClipDefaultsAreEmpty` + `CharacterGoldenPoseTest`. Step 1 below as first
> written CONTRADICTS that contract → **owner fork A2-1:** (a) keep the humanoid on the legacy
> table and make `selectClip` a unifying wrapper (three sites → one, table stays code; recommended
> for A2), or (b) migrate the table into humanoid.json with a schema for sprint variants and
> candidate lists and retire the pin deliberately (recommended as A3's first step, when the factor
> schema redefines the table anyway). **Item A2-2:** define the in-scope rig set (referenced by
> `monsters/visuals/bindings.json`, `biomes.json` fauna, race presets, bestiary hall) — the other
> `.anim` files are variants/legacy and are warnings, not gates. **Resolved by the gate:** the
> render-defect probe — the draw path is `RenderCoordinator::buildCharacterFrameData` (:4641),
> per-character `lodForDistanceSq` → `getCharacterBlob(ch, lod)` (:4808); suspect = the cached
> LOD blob for rigs with non-identity bind rotations. Probe = a new debug readback
> `GET /api/debug/character_draw {id}` (LOD + per-group bone translations as submitted to the GPU)
> compared with the A1 oracle capture of the same frame; equality test in the design-key shape:
> LOD 0 and LOD 1 bone transforms identical for the same animated frame (only the part list may
> differ); control = the humanoid matches at both. The test-build flag idea is dropped:
> `selectClip` takes its table explicitly, so the humanoid table is testable from the registry.
> Loader clamp, written at the site: a plan resolving to no root or no knee chain REFUSES the rig
> (a silent bone-0 fallback is the two-roots bug being removed).
>
> **Owner decisions 2026-09-29:** **A2-1 = (a)** — the humanoid stays on the legacy table under a
> unifying `selectClip`; the neutrality contract and its pins stand; migrating the table into plan
> data is A3's first step. **A2-2:** in-scope rigs = those referenced from gameplay data or code
> (`resources/`, `engine/`, `editor/`, `samples/`, `scripts/`) — measured 2026-09-29: **92 rig
> files, 83 referenced anywhere, of which 6 only by generator manifests under `tools/`
> (`character_spider3`, `humanoid_casts`, `monster_alien`, `monster_bunny`, `monster_cactoro`,
> `monster_ninja`) → WARN tier; 9 referenced by nothing** (`bear_fix`, `bear_proc`, `bear_smart`,
> `horse_fix`, `horse_smart`, `kotor_humanoid`, `monster_birb`, `monster_monkroose`,
> `quad_horse_g`). Owner: "no problem retiring old .anim files if we are going to make better
> ones" → A2 moves the 9 into `resources/animated_characters/legacy/` with a test that no shipped
> data references them (`kotor_humanoid` is used by `BodyPlanTest:112` as a rig-family probe —
> keep it under `legacy/` and point that test at the new path). Gate: **re-run `/design-check`
> on A2** with these two items closed.

**Contract.** Zero literal bone names and zero literal clip names in the character runtime outside
the BodyPlan loader and the plan JSON; one clip-selection function; root motion and hips anchoring
read the plan's root; every shipped rig loads onto a plan with declared chains. Behaviour on the
humanoid is unchanged, and the A1 oracle is the instrument that says so.

**Ground truth to remove (counted 2026-09-29, `AnimatedVoxelCharacter.cpp`):** 18 code lines still
carry a literal bone or clip name (`mixamorig*`, `"Hips"`, `"idle"`, `"sitting_idle"`, `"walk"`)
plus the `"mixamorig"` prefix special-case at :1575; root motion extracts and strips **bone 0**
(pre-A0 :3955/:4016) while `sitAt()` anchors the plan root — two roots; three clip-selection
sites (input FSM ~:3672, external-velocity NPC ~:3473, seated ~:3372) with different matching
rules (the NPC path only ever picks Walk/Idle and never processes `hitReact`); `m_bodyPlan.
clipDefaults` is EMPTY for the humanoid, so the legacy switch is the humanoid's real table
(`ClipSelectionTest` pins it verbatim); `computeCharScalarMetrics` samples the literal
`"sitting_idle"` (`Application.cpp` ~9509). Rig inventory: 92 `.anim` files under
`resources/animated_characters/` (46 in the bestiary hall + Meshy/legacy/variants).

**Design (the seams).**
1. `BodyPlan` gains: `root`, `hips`, `spine[]`, `neck/head`, per-limb chains with roles
   (`leg.hip/knee/ankle/toe`, `arm.shoulder/elbow/wrist/hand`), `forward` axis, `sizeScale`, and
   `clipDefaults` filled for the humanoid from the legacy switch (generated, then the switch
   becomes a fallback guarded by a test that it is never reached for a plan-complete rig).
2. **One** `selectClip(state, context)` used by all three sites; the NPC path gains the full state
   set (so `hitReact` and actions work for behaviour-driven NPCs) and keeps A0's rate scaling.
3. Root motion uses `m_bodyPlanResolved.rootBoneId` everywhere bone 0 was used; `sitAt`/oracle
   feet/knee chains read the plan chains instead of name suffixes (the suffix rule stays as the
   *loader's* default resolver for rigs without an explicit plan).
4. **Tabled render defect, timeboxed (1 day):** imported rigs with non-identity bind rotations
   render as a coherent unposed body beyond ~5–7 u (`StructurePipelineGaps.md:433-465`). Rule
   from that entry: do NOT re-run the asset probes; instrument the engine's character draw
   build in `RenderCoordinator` first (the exact function name in the gaps doc has drifted —
   locate by the per-character camera-distance branch). Red: a Meshy NPC's rendered hip joint
   world position at 8 u differs from the oracle's captured skeleton position (the A1 capture is
   the instrument — it reads the *animated* pose; the draw path shows the *bind* pose). Fix or
   record the falsification and move on; A2 does not block on it.

**Design keys.** Voxel aesthetic: no assets, rig data only. Chunk independence: none (character
space). Procedural: none; plan JSON is asset metadata. API: no route changes; `BodyPlan` JSON schema
gains fields with defaults (an old plan file loads unchanged); the render fix, if any, must not
change any per-chunk quantity into an appearance input. Defaults: none change; `ClipSelectionTest`
stays as the byte-identical pin of the humanoid table — the generated `clipDefaults` must
reproduce it exactly (that IS the red test for step 1).

**Validation.** L2: `ClipSelectionTest` unchanged and green with the legacy switch *disabled* in a
test build flag (proves the plan table carries the humanoid); a grep test (`test_no_literal_bone_
names.py`, same shape as `test_no_debug_spam.py`) asserting zero literal `mixamorig`/`"Hips"`/
`"idle"` in the runtime; `BodyPlanTest` extended to load all 92 rigs and assert every plan has a
root, ≥1 leg chain with knee/ankle, and feet that `findFeet` and the plan agree on. L4: the A1
oracle on the CharacterTestbed gauntlet before and after — identical metrics on the humanoid
walk/run (byte-identical behaviour pin), and the wolf/ibex NPC now selects a real clip on the
external-velocity path (today: `Idle` state, humanoid literals). Stress: all 92 rigs × plan
checks; 100 behaviour-driven NPCs of mixed rigs, every one on a non-empty clip after 1 s.

**Red tests, written first:** (a) grep test fails today (18 hits); (b) all-rigs plan check fails
today on rigs whose feet/knee cannot be resolved; (c) an NPC on the velocity path never enters
`HitReact` today after `hitReact()`; (d) the render-defect probe above.

### A3 — Transition + composition layer (8–12 days) — gate 2026-09-30: **NEEDS WORK (3 items) → folded in below**

> **Gate findings (2026-09-30, `/design-check`).** No voxel geometry, no chunk-derived input
> (the only distance gate is the anim-update Hz tier, cost-only), not a generation stage. Three
> items were unresolved; each gets its design here so the re-gate can pass on evidence.
>
> **Item 1 — `clip_meta` string coordinates.** Today `applyClipMetaFromFile`
> (`AnimatedVoxelCharacter.cpp:~537-560`) runs every value through `std::stof` and WARNs once per
> unknown key; string values are only tolerated via the tool-only skip list `kToolOnlyKeys`
> (`meleeFamily`, `source`, `ckpt`…). Design: the parser gains a **typed schema table**
> `kClipMetaSchema = {key → {Float | Bool | String | Enum{...}}}`; every key in the table parses to
> its type into `AnimationClip::factors` (a small `std::map<std::string,std::string>` for strings
> plus the existing floats); unknown keys still WARN once; a WRONG-typed value is an ERROR in
> `anim_lint` (red-before-green: a fixture clip with `grip=1.5` fails lint). Coordinates (all
> optional, absent = `any`): `gait` ∈ {biped, quadruped, hexapod, serpentine, avian, amorphous},
> `state` (FSM state name, lower-case), `grip` ∈ {empty, 1h, 1h_shield, 2h_light, 2h_heavy, bow,
> staff, torch}, `load` ∈ {none, light, heavy, bulky}, `condition` ∈ {fresh, tired, limp,
> encumbered}, `mood` ∈ {neutral, alert, hostile, sneak}, `role` ∈ {base, layer}, `mask` ∈
> {none, upper, arms, head, legs}, `additive` ∈ {0,1}. `kToolOnlyKeys` shrinks to the provenance
> keys only. Selection = nearest `role=base` in factor space (exact `gait`+`state` required, then
> most matching coordinates) + every `role=layer` whose coordinates all match or are `any`.
> Pinned by a new `ClipFactorSchemaTest` (parse, reject, select) and the lint fixture.
>
> **Item 2 — humanoid table migration schema.** `humanoid.json` `clipDefaults` is EMPTY by contract
> (`HumanoidClipDefaultsAreEmpty`, `ClipSelection.HumanoidMatchesLegacySwitchExactly`,
> `EmptyMemberFallbacksMatchLegacy`). The legacy switch carries three things the flat map cannot:
> **sprint variants** (`Run → run | fast_run`, strafes), **member-driven states** (Attack/Block/
> Cast/Dodge/HitReact/Death/Celebrate read `m_currentAttackClip`, `m_moveset.block`,
> `m_castSegments`, `m_currentDodgeClip`, `m_currentHitClip`, `m_deathClip`, `m_celebrateClip`) and
> **fallbacks** when those are empty (`body_block`, `roll_forward`, `idle`, `taunt`, `attack`).
> Schema: `clipDefaults` values become either a string or an object `{"clip": "run",
> "sprint": "fast_run"}`; member-driven states are NOT in plan data at all — they stay in
> `clipForState` as the one explicit "member state" branch, with their empty-member fallbacks
> moved to a second plan block `"clipFallbacks": {"Block": "body_block", "Dodge": "roll_forward",
> "HitReact": "idle", "Death": "idle", "Celebrate": "taunt", "Attack": "attack", "Cast": "idle"}`.
> Layer order stays mapping → plan → legacy; the legacy switch remains only as the guard for a
> plan that resolves nothing, and a new test asserts it is never reached on a plan-complete rig.
> **Pins that change, same commit:** `HumanoidClipDefaultsAreEmpty` is deleted with a comment
> naming this section; `HumanoidMatchesLegacySwitchExactly` keeps its table verbatim but reads it
> back through the plan (it becomes the byte-identical migration proof, the red test for step 1);
> `EmptyMemberFallbacksMatchLegacy` reads `clipFallbacks`. `CharacterGoldenPoseTest` is unaffected
> (poses, not names) and must stay green untouched — that is the behaviour pin.
>
> **Item 3 — where grip class and mass live.** Items carry `held.gripBone/gripOffset/gripEulerDeg/
> scale` only (`ItemDefinition.h:171`; 52 `held` blocks in `resources/items.json`, none declares
> hands or mass). Weapon families are already DERIVED, not authored: `MeleeAnimMapper::
> resolveFamily` evaluates `melee_anim_families.json` rules on the D&D `weapon.damageType` +
> `weapon.properties` (two-handed, heavy, versatile, light). Decision: **grip class is derived the
> same way**, in the same mapper (`resolveGripClass(item)`: two-handed+heavy → `2h_heavy`,
> two-handed → `2h_light`, shield in OffHand → `1h_shield`, bow/staff/torch by item class, else
> `1h`; no item → `empty`), and **mass** comes from the D&D weight already on `RpgItemDefinition`
> (lb → the modifier table's load bands: none <2, light 2–6, heavy 6–15, bulky >15 or two-hand
> carry). Nothing is hand-added to 52 entries. The one NEW authored field is `held.secondGrip`
> (offset + euler in the item's template frame, optional) for the off-hand pin; `gen_items.py`
> writes it for the 2H set (maul, warhammer, staffs, spear) from the grip manifest, and the
> two-handed lint (grip-distance check) refuses a `2h_*` item without it.
> **Also folded in:** `applyPostureLean` (`AnimatedVoxelCharacter.cpp:1061`) still finds the
> spine by the lower-case substring `spine`; A3 routes the lean through a plan **spine chain**
> (`segments` with a new `role: "spine"` tag, defaulting to the non-arm trunk set A2 built), and
> `tests/test_no_literal_bone_names.py` gains the lower-case pattern so this class of literal
> cannot return. **API surface named:** `GET /api/animation/transitions {id}` echoes the resolved
> edge set (from, to, blend seconds, phase-synced flag); `get_animation_state` gains `layers[]`
> ({clip, mask, weight, additive}) and `phase`; `set_blend_duration` becomes the graph's default
> edge and is echoed back per edge. Units: seconds, normalized phase 0–1, weights 0–1, clamped at
> the boundary with the reason written (a weight > 1 over-rotates the additive; a negative blend
> hard-cuts and must be spelled `0`).

Contract: a data-driven transition graph (`CombatTransitionMap` is the seed, tests only today):
per-edge blend duration, **foot-phase-synced** locomotion blends (walk↔run at matching stance
phase — the metric exists), root-motion extraction that does not pause during blends, and the
**factor composition** of §3b: **bone masks + additive layers** (grip carry poses, condition and
mood layers over any base), **procedural modifiers** (lean ∝ load, cadence/stride from size and
load, asymmetric playback for limp), and **clip factor coordinates** in `clip_meta` driving a single
selector (nearest base + matching layers). Motion-matching-lite is an *option* inside this layer,
not a requirement. First deliverable pair, chosen for the owner's example: **walk while holding a
1H sword vs a 2H hammer** — same base walk, different carry layer, hammer adds lean + slower
cadence, off-hand pinned to the hammer's second grip (L3).
Red: pose-continuity metric on every FSM edge with a seeded 0.2 s→0 s cut fails, passes with the
graph; a cast-while-walking clip shows legs still phase-continuous; the sword/hammer pair shows
identical leg trajectories (stance-feet metric) and different upper-body poses; a "2H carry" with
the second hand unpinned fails the grip-distance check. Stress: every pair of locomotion clips × 10
random switch times under the continuity threshold; every grip class × every base state composes
without a lint ERROR (8 × 12 = 96 compositions from 20 clips).

### A4 — Contact-driven interaction poses: **the chair** (10–15 days) — gate 2026-09-30: **NEEDS WORK (4 items) → folded in below** — **✅ CLOSED 2026-09-30** (owner: "close A4 then"; the approach walk is DEFERRED, see the close entry in §4b)

> **Gate findings (2026-09-30, `/design-check`).** No geometry, no chunk-derived input, decoration/
> structures stage (per-object pure function of template + placement → order-independent), nothing
> in the world recipe (affordances are asset data, like textures). More exists than this section
> assumed, and four items were unresolved; each is designed here from the evidence.
>
> **What already exists (evidence).** `asset_metrics.v1` sidecars (47 of 57 furniture templates,
> written by `tools/characterize_asset.py` → `tools/interaction_pipeline/asset_metrics.py`) carry
> per seat point `seat_top_y`, `seat_width_x`, `seat_depth_z`, `seat_center`, `front_edge_z`,
> `backrest_height`, `backrest_present` (derived from the contiguous seat slab and the voxels above
> it, `asset_metrics.py:249-310`). The fit rules `SEAT_TOO_NARROW/SHALLOW/TALL/LOW` +
> `BACKREST_BLOCKS_VIEW` compare those to character `hip_width` / `leg_length` measured from the
> segment boxes (`Application.cpp:9478-9541, 9653-9700`) and gate `sit_character` /
> `can_interact` / `find_fitting_seat` (deny-on-missing-sidecar). The detectors exist
> (`detectors.py`: `SEATED_HIPS_OFF_SEAT_XZ/_Y`, `SEATED_THIGHS_NOT_HORIZONTAL` knees_y_tol 0.35,
> `POSITION_SNAP_AT_CLIP_BOUNDARY`, blend async, feet), and so do the gates `seat_matrix.py`
> (9 presets × 7 real seats, `can_interact` parity) and `validity_gauntlet.py`. The 2-bone solver
> is generic (A3 reused it for the off-hand). **The sit clips carry NO `Speed` and NO `RootMotion`
> lines** (`stand_to_sit` 2.21 s, `sitting_idle` 6.38 s, `sit_to_stand` 2.25 s): they play in
> place, the hips travel is animated on the root bone inside the clip.
>
> **Item 1 — affordance schema extension (`asset_metrics.v2`, additive).** The characterizer gains
> per seat point: `backrest_angle_deg` (least-squares fit of the backrest voxels' front faces vs
> vertical, 0 = upright; column above the seat within `seat_depth_z` behind `seat_center`),
> `armrests[]` (0–2 planes: contiguous voxel tops in the band `seat_top_y + [0.15, 0.35]` u that
> sit laterally outside the seat slab within 0.15 u of its side faces — each with `top_y`,
> `inner_x`, `z_extent`), and `approach` (`front_edge_z` + 0.45 u along `facing`, on the floor,
> plus `facing_yaw` from the interaction point). Old sidecars stay valid (missing = none); a
> refusal reason `SEAT_UNCHARACTERIZED` stays for templates without a sidecar (10 today).
> `regen_furniture.py` re-runs the characterizer, so regenerating an asset keeps its affordances
> (closes the "regen would delete them" trap in W1). Red: `test_asset_metrics_v2.py` — `armchair`
> gets 2 armrests and a non-zero backrest angle, `bench_wood` gets none, `chair_wood` gets a
> backrest with angle ≈ 0 (it is a vertical slab).
>
> **Item 2 — pins that change, named.** (a) The per-state sit offsets (`sitDownOffset` /
> `sittingIdleOffset` / `sitStandUpOffset`, `InteractionPointDef`, `humanoid_normal.json`,
> `test_chair`) are RETIRED with the single anchor → `AnimationDeconfound.PerStateSitOffsetsChange
> TheSeatedPosition` (A0 #2) is deleted with a comment naming this section, and `BlendDuration
> SurvivesSitCycle` (A0 #1) stays and must stay green. (b) `getCameraTrackPosition()` stops
> compensating (`Application.cpp:4062, 5920` read it) → it returns the anchor-derived body
> position; no test pins the old compensation (grep 2026-09-30), the L4 check is the seat gauntlet
> screenshot with the defect in frame before/after. (c) Foot IK turns ON while seated only
> (`setFootIKEnabled` stays off for locomotion until A5) → `CharacterGoldenPoseTest` (standing
> poses) untouched; a new `SeatSolveTest` pins the seated pose. (d) The fit rules move from
> `Application.cpp` into `engine/src/core/SeatFit.{h,cpp}` (pure, testable, used by NPC
> behaviours and the solver's refusal) with the existing `seat_matrix.py` parity as the pin; the
> editor calls the engine function.
>
> **Item 3 — W1 scoped in.** `placeTemplateMicro` (`PlacedObjectManager.cpp`) computes
> `obj.interactionPoints` like its cube sibling but from `microAnchor`: world = `microAnchor / 9
> + rotateLocalOffset(def.localOffset, rotation)` (a new `computeInteractionPointsMicro`;
> `recomputeAllInteractionPoints` uses the same when `microAnchor` is set — W1 fault 2). `chair.voxel`
> (the placer's chair) gets its `# interaction_point:` line via `regen_furniture.py` (fault 3).
> Equality test (the "chunking must not change the answer" shape): anchor at placement ==
> anchor after `recomputeAllInteractionPoints` for a micro-placed chair, bit-for-bit; L2: a built
> tavern reports ≥ 1 fitting seat through `find_fitting_seat`.
>
> **Item 4 — approach transition, decided from the clip data.** Because the sit clips are
> in-place (no root motion), there is NO root-path warp: the character walks (A3 transition graph)
> to the seat's `approach` point facing `facing_yaw`, then `stand_to_sit` plays in place with
> the model origin fixed ONCE at `origin = anchor − R(facing) · hipsOffsetAtClipEnd` — derived
> from the clip's own last-frame hips (sampled with `sampleBonePosition`), so the hips land on
> the seat surface at the clip's end without any per-state re-snap. The preview-only time warp
> (`m_warpPreview*`) stays preview-only. Seated, the per-frame solve owns the pose: pelvis →
> seat plane (Y exact, XZ within the seat slab), feet → floor (2-bone leg IK, ON while seated),
> knees follow `seat_top_y` within the fit band (feet dangle when `seat_top_y > leg_length`,
> knees rise when `seat_top_y < 0.6·leg_length`; outside → refused, "accuracy over coverage"),
> torso lean → `backrest_angle_deg` over the A3 spine chain (capped 35°, clamp written at the
> site: a larger angle folds the spine through the seat back), hands → `armrests[]` when within
> the arm chain's reach (A3 off-hand solver, both arms), head level. `sit_to_stand` mirrors from
> the same origin.
>
> **API (units stated).** `sit_character` response echoes the solve: `anchor` (world, u),
> `pelvis_error_u`, `feet_floor_error_u`, `thigh_angle_deg`, `lean_deg`, `armrest_contacts`
> (0–2), or `refused` + rule id. New `GET /api/animation/seat {id}` returns the same each frame.
> `POST /api/debug/seat_solver {lean_cap_deg, knee_band}`: omitted = unchanged, echoed back,
> clamped (lean 0–35, band inside [0.4, 1.4]·leg_length) with the prevented failure written at
> the clamp. Defaults change only where item 2 says.
>
> **Validation depth.** L3 headless first (FloorWorld + synthetic seat planes; the six detectors
> ported to C++ with their calibrated thresholds; red today at the §1.4 numbers), the 100-seat ×
> 3-preset matrix as a deterministic stress test asserting pass-or-refused per cell, L2 for W1,
> L4 = `seat_matrix.py` + `validity_gauntlet.py` + a before/after seat screenshot at a stated
> pose. Control: a standing character on the same floor passes feet-on-floor and fails the seated
> detectors. Rig deltas: 60 Hz, flat floor, planes instead of voxel seats (front-edge collision
> absent → headless numbers slightly optimistic). Gate: **re-run `/design-check` on A4** with
> these four items closed.

The owner's worked example and the payoff phase. Contract, in the owner's words: *the character
sits correctly on any seat whose dimensions it fits, with one sit family* — 100 seats, 100 poses.
Design:
- **Affordances on furniture** (JSON sidecar, authored by `regen_furniture.py`, emitted by structure
  generation): seat surface (h, d, w), backrest plane/angle, armrests, approach point, facing.
  Fixes W1 (generated towns have zero seats) as a by-product.
- **One world anchor:** the pelvis contact on the seat surface. `worldPosition` is derived from it
  once; no per-state re-snap; camera compensation deleted. Kills the three-reference disagreement.
- **Constraint solve per frame** from the sit prior: pelvis → seat plane; feet → floor via the
  existing 2-bone IK (turned ON while seated — the reverse of today); knees follow seat height (feet
  dangle or knees rise within the fit limits, else refuse per "accuracy over coverage"); torso lean
  → backrest angle (spine chain, small); hands → armrests when within reach (new 2-bone arm IK,
  same solver); head level.
- **Transition:** approach → time-warped `stand_to_sit` whose root path ends on the anchor
  (warp exists in preview only, AVC:3821-3830 — make it gameplay); `sit_to_stand` mirrored.
- **Red tests (existing detectors, thresholds already calibrated):** SEATED_HIPS_OFF_SEAT,
  FEET_ON_FLOOR, THIGHS_HORIZONTAL (±0.35), POSITION_SNAP (>0.10), BLEND_ASYNC, plus the seat
  gauntlet 9 presets × 7 seats. Each must fail on today's code first (they do — §1.4).
- **Stress — literally the owner's sentence:** a generated seat matrix of 100 chairs (heights
  0.30–0.90 m, depths 0.30–0.60, with/without backrest/armrests, 3 body presets) → every pose
  passes the detectors or is *refused* with the right reason; zero silent bad sits.
- Same layer next: ledge/mantle hands, ladder rungs, bed lie-down, workbench, mount.
- **DEFERRED FROM A4 (owner decision 2026-09-30): the approach walk.** `sit_character` still snaps
  the character's origin to the seat from wherever it stands; the designed walk to the sidecar's
  `approach` point (pathing, arrival + facing test, timeout, refusal when unreachable) is NPC
  navigation work and gets its own `/design-check` alongside the character-library (Meshy) plan.

### A5 — Grounding always-on (P1) (3–5 days) — gate 2026-09-30: **NEEDS WORK (5 items) → folded in below → re-gated READY 2026-09-30**
Turn the existing foot IK + foot lock on by default on uneven terrain **once A1 proves the skate
invariant** (V2's honest caveat: the IK was tuned for stairs and dodges walk cycles by staying
off). Red: slope/rough gauntlet with IK off fails skate/float; on passes. Also fixes: ankle not
re-oriented to slope, pole target from a stable hint, logging.

> **Gate findings (2026-09-30, `/design-check`).** The A1 precondition is MET: skate is 3.5–4.1 %
> headless (`NpcPathSkateTest`, four speeds) and 4.6 % live at 2.0 u/s (§4b 2026-09-29). What
> exists: analytic 2-bone IK + knee hint (`applyTwoBoneIK`), a stair path with persistent foot
> locks, a step-up path driven by body-spring lag (positive-only, cap 4/9 + 0.05 u), and a
> bidirectional TERRAIN-FOLLOW branch with a swing gate and a half-microcube noise floor
> (`AnimatedVoxelCharacter.cpp:5350-5440`). Foot bones already resolve from the BodyPlan legs
> (`resolveFootBoneIds`, `.cpp:4801-4813`) — no name literals. The flag `m_footIKEnabled=false`
> (`.h:786`) is set by NOTHING in gameplay (only the anim editor), so today no shipped character
> ever runs any of it, stairs included. No API exists (zero hits for `foot_ik` in
> `Application.cpp` / `EngineAPIServer.cpp` / the MCP server). Five items, designed here:
>
> **Item 1 — foot-sized probes, in the IK and in the oracle.** The per-foot terrain probe uses the
> CAPSULE half-width 0.25 u (`.cpp:5364`, `m_originalHalfWidth`): a foot 0.2 u short of a step
> reads the upper surface and lifts early — the same defect class A4 hit live on the chair rail.
> Probe half-width becomes a foot-sized constant `kFootProbeHalfWidth = 0.05` (the stair path's
> value, `.cpp:5106`), searching 0.4 u above / 0.8 u below the foot as today. The live oracle's
> terrain hook (`animation_validate` → `groundYUnder`, `Application.cpp:10388`) is the same 0.25 u
> column and biases float/penetration near steps by up to a quarter unit — it gets a point probe
> (`groundYUnderPoint(x, z)`) so live and headless measure the same thing.
>
> **Item 2 — pelvis drop + ankle orientation in the terrain-follow branch.** Downhill, the stance
> foot must reach 1/3–4/9 u LOWER than the capsule floor; near full extension a leg cannot, and the
> branch has no pelvis lowering (the pelvis shift lives only in the step-up path, `.cpp:5306-5318`).
> Add: pelvis shift = the most negative stance correction (both feet weighted by their blends),
> clamped to `bodyRange = clamp(0.30 · leg_length, 0.10, 0.40)` (derived from the plan legs at
> load, not the per-clip 0.111 default; the clamp is written at the site — deeper folds the knee
> through the thigh). Ankle: after the 2-bone solve, rotate the foot bone so its sole normal
> matches the probed surface normal, estimated from four foot-sized probes at ±0.1 u in x/z
> (finite difference of the ground function — pure in world position, so seam-safe by
> construction); cap at 30° (a steeper voxel step is a riser, not a slope). The knee hint stays
> the animated knee direction (already the "stable hint"); log per-frame at TRACE only (A0 #9).
>
> **Item 3 — a failed probe is a skipped correction, never a drop.** A non-resident neighbour
> chunk makes `findGroundY` return −FLT_MAX; the lock path checks `surf <= -1e29` (`.cpp:~5414`)
> but the correction path must be shown to skip too. `grounding.probe_ok` in the readback says
> which foot probed. Pinned by the seam test in the visual plan below.
>
> **Item 4 — API.** `POST /api/debug/foot_ik {enabled, max_corr_u, body_range_u,
> probe_half_width_u}`: omitted = unchanged, every field echoed, clamps at the site with the
> prevented failure written: `max_corr_u ≤ maxStepHeight` (a larger reach plants a foot on a
> surface the capsule would not have climbed), `probe_half_width_u ∈ [0.02, 0.15]` (wider reads the
> neighbour cell — item 1), `body_range_u ≤ 0.4 · leg_length` (item 2). Readback
> `get_animation_state.grounding {enabled, blend, l_corr_u, r_corr_u, pelvis_shift_u, l_lock,
> r_lock, probe_ok[2], ankle_pitch_deg[2]}` and `GET /api/animation/grounding?id`, the A4 `seat`
> shape. Per-clip opt-out: `footIKEnabled` is promoted from `toolOnly` to a RUNTIME key in
> `clip_meta_schema.json` (schema test updated) so a clip that must not be corrected (dodge
> already is by state; hop/mantle later) says so in data, not in a switch.
>
> **Item 5 — the default flip is a pinned contract, and it is measured first.** `m_footIKEnabled`
> false → true is a default change. Same commit: `NpcPathSkate` ×4 stay < 10 %,
> `CharacterGoldenPose` standing goldens unchanged, `SeatSolve` ×5 (the seated path forces IK off,
> `.cpp:4921-4930`), `OffHandPin` ×4, `BlendDurationSurvivesSitCycle`. Cost: the header's "~360 µs
> per character per frame" (`.h:782`) is unmeasured — ONE Release measurement at n=100 walking
> NPCs (Character Pipeline Scaling rules: Release, n=100 not 1024, engine_timing) before the flip,
> with the flat-ground early-out (both probes within half a microcube of the capsule floor → skip
> the solve entirely) shown to make the common case near-free. Cost is bounded per CHARACTER by
> the existing update LOD (`LodTierLedger.md:52`), never per chunk.
>
> **Chunk independence.** Ground height is a world-position query over every grid under the
> column (`gatherGridsOverlapping`), so a foot straddling a seam reads both chunks. Equality test:
> `GroundingSeamTest.RampAcrossAChunkSeamMatchesTheSameRampInsideOneChunk` — the same subcube ramp
> built at x 12..20 of one chunk and again straddling x 28..36 across two resident FloorWorld
> chunks; walked oracle metrics (stance float, penetration, skate) equal to the millimetre. The
> oracle half already exists (`MotionOracleA1Test.TerrainSplitAtASeamDoesNotChangeTheMetrics`).
> **Procedural generation:** none — runtime pose correction; no recipe. **Voxel aesthetic:** no
> assets; feet land on the cube / 1/3 / 1/9 surfaces the world has; the correction is
> unconditional once the flag goes (the update-rate LOD bounds cost only).
>
> **Visual test plan.** *Works* = walking a 1/3-step ramp and a 1 u step at 1.5 u/s, the oracle's
> `maxStanceFloat ≤ 0.05 u` and `maxPenetration ≤ 0.02 u` on every stance, world skate < 10 %,
> and on flat ground the corrected pose equals the uncorrected one within 1 mm. Depth L3 headless
> (`GroundingTest`, FloorWorld, ONE chunk, floor y 16, a ramp of 1/3 u risers every 1 u along x
> (18°) built with `VoxelOccupancyGrid::setSubcube` plus one 1 u step; the `NpcPathSkateTest`
> external-velocity harness; the oracle ground = the DECLARED rig function of (x, z), not a
> probe), then L4 live (a subcube ramp placed on CharacterTestbed, `animation_record` +
> `animation_validate` with the point probe, before/after screenshots at a stated pose).
> **Red tests:** `WalkingDownASubcubeRampLeavesTheDownhillFootFloating` (IK off: downhill stance
> float ≈ one riser, 0.30–0.35 u, reported in units with the frame) and
> `PelvisDoesNotDropSoTheDownhillFootCannotReach` (IK on, item 2 absent: float stays > 0.15).
> **Prediction, written first:** IK off float 0.30–0.35 u; IK on float ≤ 0.05, penetration ≤ 0.02,
> skate unchanged within 1 %. **Control:** flat FloorWorld, IK on vs off, all metrics equal within
> 1 mm — proves the IK is a no-op on flat ground and does not fight the walk cycle (the V2 caveat).
> **Rig deltas:** 60 Hz fixed step, flat grey floor, no streaming spring lag, standard preset
> only — live slopes with foliage and scaled bodies read slightly worse; the A4 halfling
> standing-height observation applies to any scaled-body run. **Re-gate 2026-09-30: READY** — the
> five items answer every key; build order: item 1 probes + red tests → item 2 pelvis/ankle → item 3
> seam test → item 4 API + schema key → item 5 cost measurement, then the default flip in its own
> commit with the pinned tests.

### A6 — Source expansion (ongoing, parallel after A2)
UniMate drafts through the review ledger (in progress); the **retarget stage** (source bind → plan
bind, rotation-basis correction) so Mixamo/UniMate/video-mocap all feed the plan; **creatures via
procedural gaits on the plan** (V2's P3: spider/quadruped, FABRIK for >2-segment limbs, gait
invariants from literature) — the one place "algorithmic" is realistic and history does not
forbid it. UniMate M3 (creature text-to-motion) re-gates here.

**Meshy as a model + rig source (owner has a paid account, asked 2026-09-29).** Facts first:
Meshy is already a lane. Five `*_meshy` rigs ship (`wolf/bear/boar/elk/horse_meshy.anim`), made by
`tools/character_import.py --new-rig` (voxelize a rigged GLB into a standalone `.anim`), and
`CharacterLibraryPlan.md:154,174` already frames Meshy/Tripo as the 3D-generative lane. What we
learned about its **skeletons**: Smart-Rig bones are anonymous (`Bone_000…`), legs have six joints,
and every bone frame carries a baked ~90° rotation from the GLB import — so nothing may key on
Meshy bone names, and every delta must be conjugated through world axes (MEM `anim-retarget`
rules 2–3). That is solved: `retarget_quadruped.py` maps our 12-rig mocap library onto any Meshy
quadruped by geometric role, and all five carry mocap idle/walk/run/attack/death. **Meshy's own
animation library is NOT the call** (owner-settled 2026-08-25): retarget our packs onto its rigs.
**Blocker before Meshy rigs go wide:** the TABLED render defect — imported rigs with non-identity
bind rotations render as a coherent *unposed* body beyond ~5–7 u
(`StructurePipelineGaps.md:433-465`; ruled out clips, LOD, CPU sync; next suspect
`RenderCoordinator::buildCharacterDraws`). It burned a whole session once; it is timeboxed and
starts by instrumenting the draw path, and it sits in **A2** because the BodyPlan work touches the
same bind-pose plumbing. **Recommendation:** Meshy = *model and auto-rig* source for creatures and
props by role (organic shapes the forge's superellipse volumes do poorly); the forge = parametric
species where a spec is faster than prompting; both enter through `character_import.py --new-rig`
onto the BodyPlan, and both get their motion from the factorized base sets (§3b), never from
Meshy's animate feature. Voxel aesthetic: the importer voxelizes to the rig's box scale — keep the
subcube/microcube rule (`vox_import.py --scale fine` for props) and review silhouettes in the panel.

### A7 — Reactivity (3–5 days)
Blend-to-canned hit reactions (`hitReact` exists, but the NPC velocity path never processes it —
fixed in A2's single selection function) + an impulse nudge on the pose (L4), for both branches.
No PD ragdoll.

### T2 track — learned kinematic motion (research spike, timeboxed 5 days, after A3)
Generalise the seam: attach `IMotionSource` to NPCs (player-only today), consume
`rootTranslation`, make the provider drive intent from behaviours. Then ONE spike: a Neural
State Machine-style sit/carry source (Starke 2019, mocap-trained, small MLP, C++ inference via the
same GGML route MotionBricks used) proposing the pose into L3. Success = the spike's poses pass
the A4 detectors on the 100-chair matrix with no per-chair authoring. Failure is a written entry in
`EngineAdvancesResearch.md`, like MotionBricks. Honest priors: MotionBricks measured 2.43 s for the
first window at 10 agents and 18.5 s at 100 — runtime learned motion is single-agent or offline
until that changes.

### T3 — parked. Unpark conditions (all three): physical combat reactions become the headline
feature AND A7 measurably fails to deliver them AND the engine has an articulated actuated solver
on its own roadmap for another reason.

## 4b. Progress log

- **2026-09-29 — A0 red-then-fix (build pending).** `tests/scene/AnimationDeconfoundTest.cpp`
  (8 gtests, headless: physics world + flat floor grid, humanoid rig, plus a minimal 2-bone rig
  writer for meta/reload/stand-up cases) and `tests/test_no_debug_spam.py` (2). **All 10 failed
  on today's code for exactly the predicted reasons** (blendDuration 0 after sit; dz=0 between
  different sitDownOffsets; phase gap 0 cycles; walk advanced 0.50 s where 0.24 s was due at
  0.8 u/s vs Speed 1.68; releaseFrame dropped → 0.4; reload served 1.0 s not 2.5 s; standUp dz=0
  for mapped Idle; draw yaw off by 0.785 rad after sit; `std::cout` + per-frame INFO present).
  Fixes: seated switch cuts the transition instead of zeroing `blendDuration`; per-state offsets
  live in the snap, `sitAt()` first frame, and the editor's `ie_seek` (which also stops adding
  `heightOff` twice); `loopStartTime()` is the one definition of a clip's start (FSM, NPC and
  `playAnimation` paths); `m_externalRateScale = clamp(bodySpeed/clip.Speed, 0.5, 2)` on the
  external-velocity path only; `releaseFrame` aliases `hitFrameFraction`, tool-only keys are
  silent, any other unknown key WARNs once; `AnimationSystem::invalidateCache()` before reload;
  `standUp()` samples the mapped Idle and the NPC path consumes `m_pendingClipSnap`; all
  `std::cout` gone, IK logs TRACE; `sitAt()` zeroes `m_strafeLean`. Data: the shipped
  `test_chair` profile's dead per-state offsets (−0.6 / −0.1 / −0.1) normalised to the value that
  was actually in effect (−0.1 ×3) so behaviour is unchanged while tuning becomes real. NPC
  walk-speed *gameplay* is unchanged (behaviours still own speed; only playback rate follows).
- **2026-09-29 — A0 GREEN.** After the fixes: `AnimationDeconfound` 8/8 + `ClipSelection`,
  `BodyPlan`, `AnimPhaseJitter`, `CharacterDoorFunnel`, `CharacterCapsuleScaling`,
  `CharacterGoldenPose`, `CharacterFacing`, `NPCSystem`, `MotionOracle`,
  `CharacterMotionProvider` = **57/57**; Python `test_no_debug_spam` + foot-slide + rig integrity
  11/11. Two footguns hit on the way: a lambda local to `sitAt()` reused elsewhere (compile), and
  a test run against a STALE `phyxel_tests.exe` (identical failure digits to the red run — the
  known MSVC stale-binary signature; the wait must key on the exe mtime, not the build PID). One
  fix needed a second pass: `sitAt()` zeroing `m_strafeLean` was undone next frame by the
  smoothing toward a stale `m_strafeLeanTarget` → zero both. Still L4-open from A0: the
  `ie_seek` height fix and the NPC playback-rate fix are unit-tested but not yet watched live;
  A1's live oracle is the instrument for that, so they are verified together.

- **2026-09-29 — A1 built, headless GREEN (23/23), live check next.**
  `scene/motion/MotionOracle.{h,cpp}`: `derivePlantedJoints` (3 cm rule), `OracleOptions`
  (feet, terrain fn, knee chains, box adjacency, authored Speed), new metrics: stance body
  velocity/speed/residual/samples, `speedMismatch`, `maxPenetration`, `maxStanceFloat`,
  `maxKneeInversion`, `maxBoxOverlap`; old 3-arg `evaluateMotion` preserved (old tests untouched).
  `scene/motion/MotionOracleSampling.{h,cpp}`: headless `sampleClip` (AnimationSystem pose + FK),
  `findFeet` (same suffix rule as `anim_lint.find_feet`), `legChainsForFeet` (hip, knee, ankle —
  first version anchored at the toe and read every heel roll as an 8–11 mm "inversion"; fixed).
  Live: per-character ring buffer (`startOracleRecording` cap 600, `captureOracleFrame` at the
  frame's final world pose after IK/lean, `takeOracleFrames`), `groundYUnder(x,z)` via the
  character's own `findGroundY`, routes `POST /api/animation/record` + `/validate`
  (`animation_record`/`animation_validate` commands + MCP tools), verdict bands documented at
  the value (pose 120°, chain 0.02, knee 0.02, penetration 0.01/0.05, float WARN 0.15
  uncalibrated, world-skate 0.35/0.60 of capsule speed, residual 0.40).
  **Cross-language pin holds:** C++ stance estimate on `walk` = 1.667 u/s (±0.005), residual
  0.172 (±0.01) = the Python linter; Speed×2 → mismatch > 0.45 (red case); 8 shipped locomotion
  clips within 25 % / residual ≤ 0.40 / no knee inversion / no 120° pops; flat-floor penetration
  0; terrain-seam equality pinned (`TerrainSplitAtASeamDoesNotChangeTheMetrics`).

- **2026-09-29 — A1 LIVE (L4) on CharacterTestbed, Release, two patrolling NPCs
  (behaviour-driven external-velocity path — the A0 #5 fix under test), 240 frames each,
  armed over `POST /api/animation/record` and read with `/validate`.**
  First run exposed two capture artefacts, both fixed and re-verified: (1) the first frame had
  no previous position, so root-velocity error read exactly the capsule speed (1.5) — the first
  frame now seeds root velocity with the capsule; (2) the evaluator assumed a uniform frame time
  while HTTP polling stalled the main thread for seconds — frames now carry their own `dt`.
  After the fixes, `npc_oracle_probe` @ 1.5 u/s: root-velocity error 1.6e-4, **world skate 2.8 %
  (PASS)**, residual 0.22, penetration 0, knee inversion 0, chain drift 3e-6, pose delta 4.7°.
  Two genuine findings: (a) `npc_oracle_slow` @ 0.6 u/s read **50 % world skate (WARN)** — cause
  is A0 #5's own playback floor (0.5×; 0.6/1.68 needs 0.36×) → floor widened to 0.3.
  **Re-verified live: 0.6 u/s patrol world skate 0.497 → 0.079 (PASS), residual 0.088; the
  1.5 u/s control stayed PASS (0.113).** Prediction (< 0.35) written before the run held. A truly
  slow gait is an A3 selection problem, not a playback one. Note for the gauntlet: the control's
  second window caught a waypoint turnaround (planted-joint peak 57 u/s = a 0.38 u foot jump
  as the body pivots) — record windows must exclude turnarounds, or the oracle must segment
  on yaw rate; the mean-based `world_skate` verdict survived it, the peak did not. (b) planted-joint
  PEAK 6.4 u/s (≈ 4 cm in one frame) survives per-frame dt → a real **loop-seam foot pop in
  `walk`**; the mean is unaffected, so `world_skate` passes; loop closure is M2's contract and
  the peak is reported, not judged, until then. Also: segment-box overlap 0.004 u³ between
  non-adjacent boxes on every frame — WARN by design (voxel torso/arm boxes touch); calibrate
  before promoting to FAIL. The FSM `state` on the external-velocity path reads `Idle` while the
  clip is `walk` (known: that path bypasses the FSM — A2 collapses the selection sites).

- **2026-09-29 — regression sweep after A0+A1:** full `phyxel_tests.exe` 4113 tests, **4091
  passed, 2 failed, both outside animation and outside every changed file**
  (`AtlasManagerTest.BuildAtlasFromSourcePNGs`, texture atlas;
  `FineFaceMerge.SubcubeMerge_CrossCubeSplitsOnLightBoundaryBetweenCubes`, greedy-mesh
  lighting). Not re-run on the committed tree, so "unrelated", not "proven pre-existing"
  (`AgentContext.md:866, 981` record standing failures in those areas). MCP server restart needed
  before `animation_record`/`animation_validate` appear as tools; the HTTP routes work now.
- **2026-09-29 — A2 build (owner: "lets get to work").** Red first, all four confirmed failing
  on the pre-edit build: (a) `tests/test_no_literal_bone_names.py` — **18 lines** in
  `AnimatedVoxelCharacter.cpp` carrying a literal bone/clip name outside the allowed selector
  functions; (b) `BodyPlanScope.EveryGameplayRigResolvesRootAndACompleteLegOnSomePlan` — **10
  Quaternius animal rigs** (alpaca, bull, cow, deer, donkey, fox, quad_horse, quad_husky,
  quad_wolf, stag) resolved NO root on any of the 38 plans, and the Quaternius monsters
  (`monster_*`) fell onto `forge_ooze` (legless) because no plan knew `Hips/UpperLeg.L/Foot.L`;
  (c) `SelectClip.BehaviourDrivenNpcReactsToAHit` — `state=Idle, clip=walk` after `hitReact()`
  on the velocity path; (d) `tests/test_rig_scope.py` — 8 unreferenced top-level rigs.
  Edits: **A2-2 retirement done** — `bear_fix/bear_proc/bear_smart/horse_fix/horse_smart/
  monster_birb/monster_monkroose/quad_horse_g` `git mv`'d to `resources/animated_characters/
  legacy/`; `tools/anim_pipeline/rig_scope.py` writes `rig_scope.json` (77 gameplay, 6 tools-only,
  1 tests-only `kotor_humanoid`, 0 unreferenced, 11 retired) and `test_rig_scope.py` keeps it
  fresh (4/4). **Two new plans:** `quaternius_quadruped.json` (root `Back`, four
  Shoulder/UpperLeg/LowerLeg chains, clipDefaults Idle/Walk/Run→Gallop/Jump→Gallop_Jump/
  HitReact→Idle_HitReact1/Death) and `quaternius_biped.json` (morphology humanoid, root `Hips`,
  UpperLeg→LowerLeg→Foot .L/.R, Jump/Fall/Land/HitReact/Death/Attack→Punch/Celebrate→Wave);
  `detectMorphology` gains the Quaternius `Front*Leg`/`Back*Leg` marker so those animals route
  to a quadruped plan (they were `Unknown` before). `humanoid.json` still sorts first, so
  `planFor(Humanoid)` and the neutrality pins are untouched; `planForSkeleton` scoring picks
  `quaternius_biped` only where it resolves more than the Mixamo plan. **Runtime (E1–E7):**
  `loopStartTime` decides "loops" by `clipForState` of the FSM's cyclic states (no `idle/walk/
  run/boxing` substrings); `buildBodiesFromModel` classifies trunk bones by the plan
  (`isTrunkBone`: plan root + non-arm, non-leg segments) instead of `Spine/Head/Hips` name tests;
  `resolveBoneId` strips ANY `prefix:` namespace (the `mixamorig` literal is gone); the
  external-velocity NPC path now converts speed into `currentForwardInput` (−0.5 walk / −1.0 run
  at the walk/run clip-speed midpoint), zeroes strafe/turn and runs `updateStateMachine`, and the
  ONE shared selection block picks the clip for both paths (its `!usedExternalVelocity` guard is
  gone; the A0 #5 rate scale moved after selection); the rotation-offset fallback and the
  missing-clip warning key off `clipForState(currentState)`, not `"idle"`; every `bones[0]`
  (13 sites: root-motion strip, warp preview, oracle hips, character export, anchored playback)
  reads `m_skeletonRoot` (the bone with `parentId == -1`, cached in `adoptBodyPlan`) — the
  skeleton root and the plan root are now two NAMED things. Grep test green (0 hits, 20/20
  Python). **C++ red→green:** `BodyPlanScope.EveryGameplayRigResolvesRootAndACompleteLegOnSomePlan`
  went green after the two Quaternius plans plus a third, `quaternius_wyvern.json`, for the
  legless winged `monster_dragon` (Root/Torso/Neck/Head/Wing1-4, clips Flying_Idle/Fast_Flying/
  Headbutt/HitReact/Death — it had landed on the quadruped plan by segment score);
  `SelectClip.BehaviourDrivenNpcReactsToAHit` green; regression suites (ClipSelection, BodyPlan,
  GoldenPose, AnimationDeconfound, MotionOracle*, CharacterFacing, AnimPhaseJitter,
  CharacterCapsuleScaling) 60/60. **Full sweep 4117 tests: 4094 passed, 3 failed** — the two
  standing failures (`AtlasManagerTest.BuildAtlasFromSourcePNGs`, `FineFaceMerge.SubcubeMerge_
  CrossCubeSplitsOnLightBoundaryBetweenCubes`) plus `TransparentCullingCrossChunk.T16_Border
  SignatureCostsUnderFivePercentOfARebuild`, a timing test that ran while the engine was being
  launched on the same machine (re-run alone below).
- **2026-09-29 — A2 L4 (Release, CharacterTestbed) + a live-caught routing bug.** Patrolling
  NPCs on `deer.anim` (Quaternius quadruped) and `monster_yeti.anim` (Quaternius biped) report
  `state=Walk, clip=Walk`; a patrolling humanoid NPC hit with `damage_entity` reports
  `state=HitReact, clip=hit_head` (the red test's live twin — before A2 the velocity path
  never consumed the hit). **The deer, hit the same way, reported `state=HitReact, clip=Idle`**:
  it was on `humanoid.json`, because `CharacterVisualResolver::morphologyFromAnimFile` stamps
  **Humanoid from the FILE NAME** for anything not named wolf/spider/dragon/_meshy/forge_, which
  suppresses `detectMorphology(skeleton)` at load. The scope test never saw it (it scores plans
  directly, not through the morphology route), so a runtime-route test was added and shown red
  (`BodyPlanScope.NpcResolvedRigsAdoptTheirScopedPlanAtRuntime`: deer → "humanoid", HitReact →
  "idle"). Fix: the resolver's default is now **Unknown** (the skeleton decides; humanoids still
  detect from Hips, so the seeded palette is unchanged) and the detector gains a winged-and-
  legless → Dragon rule for the wyvern. `get_animation_state` now returns `plan`, `plan_root`
  and `skeleton_root` — the L4 instrument for "which plan did this rig adopt".
- **2026-09-29 — A2 render probe (1-day timebox): NOT REPRODUCED.** Twelve `bear_meshy` NPCs at
  11–23 u and one at ~40 u all render posed and horizontal (`dropped 0`, drawn 12). The draw path
  was read end to end: the only camera-distance branch is the 35/80 u part LOD, and both LOD
  levels share the same bone matrices — nothing there can make a 5–7 u threshold. The one
  candidate that could masquerade as distance is ORDER (candidates are distance-sorted before the
  buffers fill). Falsification and the order test to run if it recurs are logged in
  `StructurePipelineGaps.md` (2026-09-29 entry); A6 re-runs it under the 46-rig Hall.
- **2026-09-29 — A2 behaviour pin + an ORACLE INSTRUMENT BUG.** The A1 oracle re-run on the
  humanoid velocity path first read **31 % world skate at 1.5 u/s and 80 % at 2.0 u/s** (A1 had
  2.8 % at 1.5) while an input-driven humanoid read 2 % — a regression, apparently. Progress
  traces (fast loopback polling) showed the clip advancing at the right rate with no clip flips
  or resets, and a **headless twin** (`tests/scene/motion/NpcPathSkateTest.cpp`: FloorWorld, 60 Hz,
  the live route's exact oracle options; external-velocity at 1.5 / 1.68 / 2.0 u/s + an
  input-driven control) plants at every speed — 4.1 % / 3.5 % / 3.9 % skate, control 3.5 %. Cause: `animation_record` counts FRAMES, and at
  the quiet machine's 270 fps a 90-frame window is **0.33 s — shorter than one stride** — so the
  lowest-3 cm stance band takes touch-down/lift-off as stance and the "skate" is swing-foot
  velocity. A 600-frame (3.2 s) window on the same 2.0 u/s patrol read **4.6 % PASS**. A1's 2.8 %
  had been captured with the main thread stalling on `localhost` name resolution (every API
  call costs 2.0 s on this machine — `127.0.0.1` is 23 ms), which stretched its 120 frames over
  seconds by accident. Fix: the window is now a DURATION — `startOracleRecording(frames,
  minSeconds)` stops when both the frame count and the time floor are met (cap 600 → 1800),
  `animation_record` takes `seconds` (default 2.0), `animation_validate` reports
  `window_seconds` and returns **`SHORT_WINDOW`** for `world_skate`/`stance_residual` under
  1.5 s instead of a number. Verdict on A2 behaviour: velocity path 4.6 % at 2.0 u/s, headless
  pins green at all three speeds → **unchanged within the oracle's noise**. Two footguns for the
  ledger: use `127.0.0.1` for scripted polling; never read a skate figure without its window
  length.
- **2026-09-30 — A3 gated READY (re-gate after the three folded items) → owner "lets get to
  work". Item 1 BUILT:** `resources/anim/clip_meta_schema.json` is THE typed schema (float /
  bool / string / enum, `toolOnly`, `factor`, `runtime` field, `releaseFrame` alias), read by
  `graphics/ClipMetaSchema.{h,cpp}` (builtin numeric fallback if the file is missing) and by
  `tools/anim_pipeline/clip_meta_schema.py` (`anim_lint.py metacheck`, and `lint` now includes
  the schema findings). `applyClipMetaFromFile` delegates to it; factor coordinates land in
  `AnimationClip::factors`; `ClipMeta::selectComposition` = nearest `role=base` (exact
  gait+state, declared coordinates must match, more matches = nearer, first wins ties) + every
  `role=layer` whose declared coordinates match. Three keys the old parser WARNed on every load
  (`weaponRole`, `impactFrameFraction`, `gatheringRole`) are now typed tool-only keys. Red →
  green: `tests/test_clip_meta_schema.py` (ImportError → 95 passed incl. all 89 shipped rigs
  validating clean), `tests/graphics/ClipFactorSchemaTest.cpp` 7/7 (parse, reject wrong type /
  bad enum / unknown key, alias order, legacy numerics byte-for-byte, shipped headers clean,
  composition selection). **Item 2 BUILT:** `humanoid.json` carries the clip table
  (`clipDefaults` string-or-`{clip, sprint}`, `clipFallbacks` for Block/Dodge/HitReact/Death/
  Celebrate/Cast; Attack deliberately absent — its member returns verbatim); `BodyPlan` gains
  `clipSprint` + `clipFallbacks`; `clipForState` layer 2 is sprint-aware (the strafe run-band
  RULE stays in code, names in data) and layer 2b resolves member-driven states with plan
  fallbacks; the legacy switch is the guard only, counted by `legacyClipFallbackHits()`. Pins
  changed in the same commit: `HumanoidClipDefaultsAreEmpty` → `HumanoidPlanCarriesTheLegacy
  ClipTable`; `HumanoidMatchesLegacySwitchExactly` unchanged (now the byte-identical migration
  proof); new `LegacySwitchNeverReachedOnTheHumanoidPlan` (red: 63 fall-throughs → 0);
  `CharacterGoldenPoseTest` untouched and green; `BodyPlan.h` contract comment rewritten.
  49/49 across ClipSelection/BodyPlan/GoldenPose/ClipFactorSchema/Deconfound/SelectClip/
  NpcPathSkate/CharacterFacing. Next in A3: transition graph + stance-phase sync, then masks +
  additive layers, then grip derivation + `secondGrip`, then the sword/hammer pair.
- **2026-09-30 — A3 transition graph BUILT (increment 3).** Data: `humanoid.json` gains a
  `transitions` block (`defaultBlend` 0.2 s; edges Walk↔Run 0.25 s **phaseSync**, Idle→Walk 0.15,
  Walk→Idle 0.20, `*`→HitReact 0.08, `*`→Attack 0.10, `*`→Dodge 0.08, `*`→Death 0.15, `*`→Jump
  0.10; exact edge beats wildcard; negative blend clamped to 0 at parse with the reason);
  `BodyPlan::findTransition`, builtin mirror + `expectPlanEqual` cover it. **Stance markers:**
  `anim_lint.py stance [--write]` finds each foot's plant as the start of the LONGEST cyclic
  stance run (first-entry read the walk's feet 0.26 cycle apart because heel-toe roll lifts a
  foot out of the 3 cm band mid-stance; longest-run reads 0.50) and writes `stanceL`/`stanceR`
  (new typed schema keys → `AnimationClip::stanceL/R`) to the 18 humanoid locomotion clips,
  pruning them from the 34 combat/cast/draft clips that also carry Speed lines. **Runtime:**
  ONE switch entry `beginClipTransition(newClip, toState)` for `playAnimation()` and the FSM/NPC
  switch: plan edge (owner state → new state) gives the blend seconds (`m_activeBlendDuration`,
  the blend advance now divides by it, floor 1e-4) and phase sync — `phaseSyncedStartTime`
  measures gait phase from the left plant when both clips carry markers, else plain cycle
  fraction; non-synced edges keep A0 #4's own-phase start. Root motion no longer pauses for the
  crossfade: `AnimationSystem::sampleBonePosition` reads the CURRENT clip's own root while the
  skeleton holds the blended pose, and the switch primes `m_prevRootPos` from the new clip's
  start so frame 1 is a real delta. `adoptBodyPlan` seeds `blendDuration` from the plan default.
  **Red → green** (`tests/scene/motion/TransitionGraphTest.cpp`): no edges → Walk→Run edge found,
  wildcard hits, undeclared edge null; no markers → all 8 locomotion clips carry both, alternating
  gaits half a cycle apart (strafes exempt: side-step gait); **ten seeded walk→run switches
  entered the run at phase 0.000 with blend 0.20 → every seed lands within 0.000 of the walk's
  gait phase with blend 0.25**, and the synced start differs from the phase seed on all ten
  (control); root motion through a 0.2 s idle→step crossfade 0.0 u → ~0.2 u. Caveat, stated:
  the root-motion red run was confounded (the test drove the clip outside Preview, so the FSM
  re-selected idle); the assertion is discriminating by construction (the old `!isBlending`
  gate blocked the entire 12-frame window) but was not observed failing for that reason.
  Python: `tests/test_anim_stance_markers.py` 3/3 (ImportError/red → green), humanoid
  round-trip still byte-identical. C++: 89/89 across TransitionGraph/ClipSelection/BodyPlan/
  GoldenPose/Deconfound/SelectClip/NpcPathSkate/MotionOracle/ClipFactorSchema/Stair/Facing/
  PhaseJitter/Capsule. **API:** `GET /api/animation/transitions {id}` (edges with resolved blend
  seconds, default, character override, crossfade in flight) + MCP `get_animation_transitions`;
  `get_animation_state` adds `blending`, `blend_seconds`, `clip_owner_state`. Footgun for the
  ledger: `playAnimation()` is sticky only in Preview — a test that plays a clip in any other
  state watches the FSM re-select on the next update.
- **2026-09-30 — A3 bone masks + additive layers BUILT (increment 4, the §3b mechanism).**
  `AnimatedVoxelCharacter` composes `role=layer` clips over the evaluated base: layer selection =
  `ClipMeta::selectComposition` on the character's factors (`setCompositionFactors`: grip / load /
  condition / mood; `state` filled from the FSM, lower-case) — re-run on every clip switch and on
  every factor change; each layer carries its clip, a per-bone mask, `additive` (default 1) and a
  weight eased over 0.2 s in and out (a faded-out layer is dropped). **Additive = the delta from
  the layer clip's own frame 0** (authoring convention: frame 0 neutral) post-multiplied onto the
  base rotation; **override** = slerp to the layer pose; layer time clamps at the clip end (a
  pose holds). **Masks** resolve in `adoptBodyPlan` from plan ROLES unless the plan declares them
  (`BodyPlan::masks`, subtree roots): `legs` = leg-upper subtrees, `arms` = topmost isArm segment
  subtrees, `upper` = everything but the skeleton root and the legs, `head` = declared only; the
  skeleton root is never masked (root motion / anchoring own it). Applied right after the base
  pose (before the previous-time bookkeeping, before root-motion extraction and IK). **Red →
  green** (`tests/scene/motion/LayerCompositionTest.cpp`, 4-bone Mixamo-named rig with
  `role=base` walk, an additive `mask=upper grip=2h_heavy` carry layer and an override
  `mask=arms grip=1h_shield` guard): masks {} → legs {3} arms {2} upper {1,2}; no layer applied →
  additive arm exactly 45° off the base with the leg identical to the un-layered base at the
  same phase and the root untouched; override arm exactly the layer pose with the spine outside
  the mask untouched; factor change fades the old layer (weight < 1 after 3 frames) and drops it;
  the no-matching-layer control passed before and after. 94/94 across every animation suite.
  **API:** `POST /api/animation/factors {id, grip, load, condition, mood}` (omitted = unchanged;
  values validated against the schema enums, a typo is an error not a silent no-match; echoes
  factors + active layers) + MCP `set_animation_factors`; `get_animation_state` adds `factors`
  and `layers[]` ({clip, mask, additive, weight}). The humanoid ships no layer clips yet — the
  sword/hammer pair authors the first two.
- **2026-09-30 — A3 item 3 (grip + load DERIVED) BUILT.** `MeleeAnimMapper::resolveGripFactors
  (mainHand, offHand)` → `{grip, load}` from the D&D data: torch = held light or id contains
  "torch" (the torch item declares its light under `effects`); staff = id contains "staff";
  Ammunition → bow; Heavy+TwoHanded → 2h_heavy; TwoHanded → 2h_light; Heavy → 2h_heavy; else
  1h, and 1h_shield when the off-hand is `RpgArmorType::Shield`; load from `weightLbs` (<2 none,
  <6 light, <15 heavy, else bulky; no RPG entry → none). `maul` (10 lb, Heavy+TwoHanded) and
  `greataxe` (7 lb) were ADDED to `weapons_martial.json` (SRD) so the gameplay maul derives like
  everything else — nothing hand-added to the 52 `held` blocks. Hooked where the hand changes:
  the player's held-item → moveset path and `updateNpcHeldItems` (NPC off-hand read from
  `EquipSlot::OffHand`; the player has no off-hand slot yet, so 1h_shield is NPC-only).
  `tests/core/GripClassTest.cpp` 7/7 on real data (greatsword/maul 2h_heavy+heavy, longsword
  1h+light, dagger 1h+none, longsword+shield 1h_shield, longbow/bow bow, quarterstaff/staff_fire
  staff, torch + a held-light lantern torch, frying_pan 1h+none).
- **2026-09-30 — A3 procedural modifiers BUILT (lean ∝ load/condition, cadence).** `BodyPlan::
  spineChain` (humanoid.json declares Spine/Spine1/Spine2; undeclared plans use the A2 trunk set
  minus the root) resolves to `m_spineChain`; `applyPostureLean` distributes appearance lean +
  `factorLeanDeg()` over it — the lower-case `spine` substring is gone, pinned by a function-scoped
  grep test (`test_posture_lean_reads_the_plan_spine_chain_not_a_name_substring`, red first).
  Parameter table (code, documented as data-to-be): lean light 2° / heavy 6° / bulky 10°, tired
  +3° / limp +2° / encumbered +6°; cadence heavy 0.92 / bulky 0.85 × tired 0.90 / limp 0.85 /
  encumbered 0.90. Cadence scales the locomotion states ONLY (idle, attacks, casts, sits play at
  their authored rate) and scales playback AND the input-path move speed together so the feet
  stay planted; the NPC rate scale divides it out (a loaded NPC walks slower only if its
  behaviour asks). **Open item logged here, not fixed:** the appearance-PROPORTION heuristics
  (`getLimbScales`, belly shaping in `buildBodiesFromModel`, `buildSegmentBoxes` minimums) still
  classify bones by lower-case name substrings; they belong to the preset system and move to plan
  roles in a later phase — the global grep test stays capital-case for them on purpose.
  `FactorModifiersTest`: chain = the three spine bones; bulky walk capsule **1.427 u/s = 1.679 ×
  0.85, skate 4.2 %**; fresh control 1.679 / skate < 10 %; bulky idle spine lean sums to 10°
  with legs untouched (after gating cadence off idle — the first run leaked a 0.68° idle-phase
  drift into the lean measurement, which is exactly why cadence is gait-only).
- **2026-09-30 — A3 SWORD/HAMMER PAIR BUILT (the owner's example, L3).** `tools/anim_pipeline/
  make_carry_layer.py` authors an additive upper-body layer from an existing pose: frame 0 =
  `idle`@0 (reference), frame `duration` = the source pose, upper-body bones only (43 of 54
  carry a delta), `clip_meta role=layer grip=<g> mask=upper additive=1 type=layer source=<clip>`.
  Three layers written into `humanoid.anim`: `carry_2h_heavy` (from `slash2h_idle`, max delta
  102°), `carry_1h` (from `sword1h_light1`@0, 73°), `carry_bow` (from `bow_idle`, 77°); lint +
  metacheck clean, round-trip byte-identical. `SwordHammerPairTest` (FloorWorld, input walk,
  same phase seed): grip 1h vs 2h_heavy → **leg local rotations identical to 0.0000° on every
  frame, arm mean delta 23.9° (bare vs hammer 36.2°), skate 3.5 % / 3.5 % / 3.5 %, speed
  identical** (grip does not change speed; load does). Remaining A3 scope: `held.secondGrip` +
  the off-hand IK pin (grip-distance check), then the A3 re-gate/close.
- **2026-09-30 — A3 `held.secondGrip` + OFF-HAND PIN BUILT (L3, the grip-distance check).**
  `HeldItemInfo::secondGrip` (template-frame point, optional) is DERIVED, never typed:
  `tools/items_second_grip.py` reads `items_manifest.json` (grip_point_units + dims) for every
  template gen_items tags two-handed / staff / polearm and places the second hand one hand span
  (0.28 u) up the shaft, clamped 5 cm under the top; wrote 7 items (maul, battle_axe, spear, four
  staves) and prunes it from anything else. Runtime: `resolveOffHandChain()` builds the arm
  chains from the plan's isArm segments (topmost arm → arm child → first child) and picks the one
  NOT holding the grip bone; `applyOffHandPin()` runs after the base pose, layers, lean and
  global transforms with the existing 2-bone solver (`applyTwoBoneIK`, model matrix = the render
  frame) eased over 0.15 s, and records `offHandError()`. The item follow code (player and NPC)
  sets the target to `itemTransform * secondGrip` every frame when the grip class is 2h_* or
  staff, else clears it (one frame behind the main hand by construction).
  `tests/scene/motion/OffHandPinTest.cpp`: chain = LeftArm/LeftForeArm/LeftHand; **reachable
  target error 0.022 u (< 0.111 microcube)**; unreachable 2.0 u reported honestly; released hand
  returns to the base pose (control). Caveat: the first red run used a physics-less character,
  whose visual-Y spring never settles — the pin works in the render frame, so headless pin tests
  need the FloorWorld fixture. `tests/test_items_second_grip.py` 3/3 (red: no item carried one).
  `get_animation_state` adds `off_hand {active, error_units, chain_resolved}`.
- **2026-09-30 — A3 L4 (Release, CharacterTestbed) + two live-caught content fixes.** Patrolling
  humanoid NPC with `equip_item maul`: `get_animation_state` → factors `grip 2h_heavy, load heavy`,
  layer `carry_2h_heavy` weight 1.0, `off_hand.active` with `chain_resolved`; `iron_sword` NPC →
  `grip 1h, load none`, layer `carry_1h`, pin inactive. **Finding 1:** with the ADDITIVE 2H carry
  the pin error tracked the walk cycle, 0.00 u for half the stride and up to **0.19 u** for the
  rest — the base walk's arm swing still moved the main hand, carrying the maul's second grip out
  of the off-hand's reach. A two-handed carry must hold both arms, so `make_carry_layer.py` gained
  `--additive {0,1}` and `carry_2h_heavy` was re-authored as an **arms OVERRIDE** (`mask=arms
  additive=0`); the swing is gone and the error went flat — but flat at **0.12–0.15 u**, i.e. the
  second grip sat ~13 cm beyond full reach in that pose. **Finding 2:** the derived hand span
  (0.28 u, "a hand plus a gap") was the excess; `items_second_grip.py` now derives 0.16 u (hands
  touching) and re-wrote the 7 items. Result live: **pin error 0.036–0.059 u across the whole
  stride (< 0.111 microcube)**. Headless pins unchanged (pair test: legs 0.0000°, arms 25.8°;
  OffHandPin 0.022 u). Lesson for the ledger: an additive layer preserves the base's motion by
  design — right for a 1H carry offset, wrong when both hands must stay on an object; and a
  "derived" constant is still a number that has to be measured against the pose it serves.
- **A3 STATUS 2026-09-30: BUILT — every gated item delivered** (typed clip_meta schema · humanoid
  table in plan data · transition graph + stance-phase sync + root motion through blends · bone
  masks + additive layers · grip/load derived · lean + cadence modifiers · sword/hammer pair ·
  secondGrip + off-hand pin). **Full sweep on the A3 build: 4155 tests, 4133 passed, 2 failed — the two standing failures (AtlasManagerTest.BuildAtlasFromSourcePNGs, FineFaceMerge.SubcubeMerge_CrossCubeSplitsOnLightBoundaryBetweenCubes), nothing in animation.** Live L4 in the entry above. Still deliberately open
  from this phase: the appearance-proportion name heuristics (logged above); the player has no
  off-hand slot (1h_shield NPC-only); layers play at their own clock (a gait-phase-locked
  additive layer, e.g. arm swing, is A6 material); no motion-matching-lite (option, not
  requirement). **Next gate: A4 (the chair) needs its own `/design-check`.**
- **2026-09-30 — A4 gated READY (re-gate after the four folded items) → owner "lets get to
  work". Steps 1–3 BUILT.** **Step 1, SeatFit into the engine:** `engine/include/scene/SeatFit.{h,
  cpp}` — margins as the single source (`kHipClearance` 0.05, `kDepthClearance` 0.10,
  `kFootDropMax` 0.20, `kBackrestHeadMax` 0.10, `kKneeRiseMax` 0.35; `tests/test_seat_fit_margins.py`
  pins `sit.py` equal), `evaluate(metrics, seat)` + `refused()`, `SeatFeatures::fromJson` (v1 + v2
  fields), `measureCharacter()` from the PLAN (root = hips, leg chains, topmost arm segments,
  off-hand chain for reach, spine chain top for depth, new `BodyPlan::headBone` for eye/seated
  height, `clipForState(SittingIdle)` for the seated sample — no bone-name literals). The editor's
  `computeCharScalarMetrics` / `runSitCompatChecks` are thin wrappers now. `SeatFitTest` 5/5: each
  rule fires at its margin (1 mm either side), backrest is a warn not a refusal, chair_wood fits
  the standard humanoid. Footgun: `getBoneAABBs()` is empty until the first `update()` — a fresh
  character measured 0.44 u tall. **Step 2, `asset_metrics.v2`:** the characterizer adds
  `backrest_angle_deg` (least-squares of the frontmost face per height course, fitted 0.15 u above
  the seat so chair_wood's second slab layer stops reading as a 37° rake; backrest search allows
  0.2 u behind the rear edge for raked backs), `armrests[]` (tops in `seat_top + [0.15, 0.35]`
  within 0.15 u outside the slab, one per side) and `approach` (0.45 u ahead of the front edge on
  the floor). `characterize_asset.py --all` now walks the category taxonomy; all 58 furniture
  sidecars regenerated as v2 with v1 numbers byte-identical (diff = precision/order only);
  `regen_furniture.py` writes the sidecar FROM the characterizer (one source) and emits the
  `# interaction_point:` header line from its anchors. `tests/test_asset_metrics_v2.py` 4/4
  (synthetic raked armchair → 15° ± 2, two armrests, approach; bench → none; shipped seats v2 +
  parity). Two stale validator tests fixed for the 2026-08-07 library reorg paths. **Step 3, W1:**
  `PlacedObjectManager::computeInteractionPointsAt(defs, anchorWorld, rot)`; `placeTemplateMicro`
  writes points anchored at `microAnchor / 9`; `recomputeAllInteractionPoints` uses the same for
  `placedAtMicro` objects; `chair.voxel` regenerated with its seat line (`regen_furniture.py chair`
  — the script now takes asset names). `PlacedObjectMicroPointsTest` 2/2: off-grid micro anchor
  (13,0,22) puts the seat at micro/9 + local (0.44 u from the floored-cube answer, the control), and
  placement == reload bit-for-bit at 0/90/180/270. Caveat: written with the implementation — the
  red is the code read (the micro path wrote no points) and the floored-cube control, not a
  failing run.
- **2026-09-30 — A4 step 4 BUILT: one seated origin + the per-frame seated solve (red → green).**
  **Red first** (`tests/scene/motion/SeatSolveTest.cpp`, FloorWorld + synthetic seat plane, the
  six `detectors.py` thresholds ported): chair 0.45 → knee dY 0.088 (borderline), **feet-floor
  0.054 / 0.096 / 0.396 u on 0.45 / 0.60 / 0.90 seats** (> 0.05), **hips world step 0.103 u at
  the sit_to_stand → Idle release** (> 0.10). Root cause of the snap: the seated origin was
  `seat − hipsRef_sitDown`, i.e. it inherited the CLIP's baked seat height (0.55) — on a 0.45 seat
  the character's floor sat 0.103 below the real floor and the release ground-snapped it. **Green:**
  `AnimatedVoxelCharacter::applySeatSolve` (after the off-hand pin, before render), weight =
  smoothstep over stand_to_sit, 1 in sitting_idle, 1 − smoothstep over sit_to_stand: (1) pelvis —
  hips joint → seat anchor + `kHipsAboveSeat` 0.05 via a model-space root shift (worldPosition
  never moves); (2) torso lean = clamp(backrest, 0, 35)° spread over the plan `spineChain`; (3)
  feet — each plan leg's ankle → (animated XZ, floor + standing ankle height) with the shared 2-bone
  solver (floor from `groundYUnder`, else the floor captured at `sitAt`; a seat near leg length
  leaves the feet dangling by exactly the shortfall — no hover, no sink); (4) hands — each armrest
  world point takes the arm on its side (right vector of the seat facing) when within reach.
  **One origin:** the post-FSM snap uses `m_hipsRef_sitDown` XZ for the whole sit and **Y = the
  floor the character stood on**; the per-state offsets are ignored (A0 #2 test RETIRED with a
  comment naming this item) and `getCameraTrackPosition()` returns `worldPosition` (the seated
  XZ compensation existed only to hide the re-snap). `setSeatAffordances(backrestDeg,
  armrestWorldPts)` is pushed by `sit_character` from the v2 sidecar through the object's real
  origin (`microAnchor/9` for micro placements) and the same `rotateLocalOffset` convention as
  the interaction points; `get_animation_state.seat {sitting, weight, pelvis_error_u,
  feet_floor_error_u[2], lean_deg, armrest_contacts}` is the readback. **Numbers after:** chair
  0.45 → hipsY excess 0.000, hips XZ 0.000, knee dY 0.052, knee fwd 0.407, feet-floor 0.000,
  feet below hips 0.387; seats 0.60 / 0.90 → feet-floor 0.000 / 0.005; **worst hips world step
  0.017 u** (mid sit_to_stand travel, not a boundary); lean/armrest test: control lean 0 /
  contacts 0 / nearest joint 0.179 u from the armrest points, treatment lean 20.0 / contacts 2 /
  hands 0.000 u on the points / head 0.125 u further back. Two footguns found: (a)
  `skeletonFootOffset_` folds the +0.05 draw lift in, so "ankle above the sole" is
  `globalY − (footOffset − 0.05)` — measured 0.050 low first; (b) the test's FEET_ON_FLOOR must be
  judged against the STANDING foot-joint height on that floor (the joint is the ankle, not the
  sole). Rig deltas: 60 Hz, flat floor, synthetic seat plane (no voxel seat collision), one
  preset. Still open in A4: the 100-seat × 3-preset matrix stress, the live gauntlet + before/
  after screenshot, then the close.
- **2026-09-30 — A4 step 5 BUILT: matrix stress + live gauntlet, and the live run caught a real
  defect (red → green).** **Matrix** (`tests/scene/motion/SeatMatrixStressTest.cpp`): 34 seat
  heights 0.20–1.30 × halfling / standard / goliath = **102 cells, 38 FIT (every one sat and judged
  per cell), 64 REFUSED (never sat)**; refusals are height-only and land exactly at the SeatFit
  margins; the first refused-TALL seat per body is sat anyway as the CONTROL and leaves the feet
  hanging (halfling 0.219 u, standard 0.281 u above the floor). Rules the data forced: (1) level
  thighs are only demanded while the seat leaves room for a vertical shin (`seat + 0.05 −
  (shin + ankle) ≤ 0.35`); above that the legs hang and the knees must be below the hips (a bar
  stool is still sitting correctly); (2) feet are judged against the FLOOR (ankle band + the
  geometric shortfall from the SeatFit leg proxy), not the standing foot joint — the halfling
  preset stands with its foot joint 0.32 u up in this rig (observation logged in
  `StructurePipelineGaps.md` 2026-09-30, not chased). **Live gauntlet BEFORE** (`validity_
  gauntlet.py`, CharacterTestbed, Debug): **8 of 21 measured pairs INVALID, "floor breach"
  0.22–0.36 u** (standard × stool/bench_wood, elf/half_orc × chair_wood/bar_stool, dwarf × stool_low,
  ogre × bench_great). Reproduced by hand (elf on chair_wood): ankles 0.38 u above the floor,
  knees above the hips, readback feet error 0.000 — the per-ankle `groundYUnder` (a 0.25 u column
  from a unit above the capsule) returned the chair's own rail as the floor. **Red test first**
  (`SeatSolve.FeetLandOnTheApproachFloorNotOnFurnitureNextToTheAnkles`, a solid cube in the
  ankle cell of the FloorWorld): feet 1.000 u up against the pre-fix code; **fix:** the seated feet
  target the floor the character STOOD on at `sitAt` (`m_seatFloorY`), never a ground query;
  green 0.000. Fixture footgun: a second `VoxelOccupancyGrid` for the same chunk is deduped by
  `registerGrid` — add cells to the floor grid. **Live AFTER:** elf on chair_wood shin box
  17.04–17.53 (ankle on the 17.0 floor), thighs level; **gauntlet 21/21 measured pairs VALID**;
  `seat_matrix.py` (9 presets × 7 seats) coverage + expectations + gate parity PASS. Before/after
  screenshots at the stated pose camera (28.2, 18.4, 3.4) yaw −54 pitch −11:
  `screenshots/screenshot_20260930_132720_613.png` (knees high, feet drawn up) →
  `screenshots/screenshot_20260930_134556_332.png` (thighs level, feet on the grass floor).
  Tool fixes: `validity_gauntlet.py` still used pre-2026-08-07 flat template paths (now resolves
  by stem under the category folders); both gauntlets spawned "standard" WITHOUT a preset, which
  the engine RANDOMIZES (legLengthScale ~1.045 seen live → chair_wood refused SEAT_TOO_LOW), so
  the preset is now always pinned. `tests/test_seat_detector_parity.py` pins the C++ detector
  thresholds to `detectors.py`. **API:** `GET /api/animation/seat?id` (`get_animation_seat`) =
  the `seat` block (weight, pelvis_error_u, feet_floor_error_u[2], thigh_angle_deg, lean_deg,
  armrest_contacts); `sit_character` echoes `seat_affordances` (clamped backrest angle, armrest
  world points). Not built from the design's API list: `POST /api/debug/seat_solver` (the solve
  has no knee band to tune and the lean cap is the written clamp). **Still open in A4 before the
  close:** item 4's WALK to the `approach` point (today `sit_character` still snaps the origin to
  the seat from wherever the character stands — the origin derivation is the designed one, the
  approach walk is not built); `armchair` has no seat line so the armrest path is pinned headless
  only; the SeatFit leg proxy is pose-dependent (0.885 vs 0.893 on two live queries of the same
  body). Standing failures on this branch not touched by A4: `AtlasManagerTest`, `FineFaceMerge`,
  and `GpuTimingHistoryTest` ×3 (garbage values, perf-program code, no local edits).
- **2026-09-30 — A4 CLOSED (owner: "close A4 then").** Delivered and verified: SeatFit engine-side
  (5 tests + margin parity), `asset_metrics.v2` (58 sidecars), W1 micro-origin points (2 tests), one
  seated origin + the per-frame seated solve (pelvis / feet / lean / armrests; `SeatSolveTest` 5/5
  incl. the live-caught rail defect red→green), the 102-cell × 3-preset matrix (38 fit all judged,
  64 refused never sat), live gauntlet 21/21 valid after the fix, `seat_matrix.py` PASS, seat
  readback API + `sit_character` affordance echo, before/after screenshots at a stated pose.
  **Deferred, not built:** item 4's approach walk (see the A4 section's DEFERRED bullet).
  **Known gaps carried forward:** `armchair` has no seat line (armrests pinned headless only);
  the SeatFit leg proxy is pose-dependent (±1 cm between queries of the same body); the halfling
  preset's standing foot-joint height (0.32 u) is an unverified observation; the design's
  `POST /api/debug/seat_solver` was not built (nothing to tune). All A4 work is uncommitted on
  `perf/lights-microvoxel-program`. **A5 is next and needs its own `/design-check`.**
- **2026-09-30 — A5 IN PROGRESS (owner: "lets get to work"): measurement infrastructure + red
  baseline + items 1–4 written, green pending.** **Infrastructure first, because the oracle
  could not judge a ramp:** (a) planting was height-derived — on a descending ramp only the LOWEST
  stance counted as planted; `derivePlantedJointsOnTerrain` makes it terrain-relative when a
  ground function is given (pinned: `MotionOracleA1.PlantingIsTerrainRelativeWhenAGroundFunctionIsGiven`,
  height rule keeps 4 of 12 stance frames, terrain rule all 12); (b) the foot JOINT is the ankle,
  0.11 u above the sole in the bind pose — `OracleOptions::footClearanceRef` removes it so a planted
  foot reads 0; the walk clip's own lowest ankle clearance on the flat floor is **0.024 u**, not the
  bind pose's 0.112, so the tests self-calibrate from a flat walk; (c) float is judged on STILL feet
  (world XZ speed < 0.3 u/s, `stanceSpeedMax`) — with terrain that steps, height planting picks the
  penetrating frames; (d) the metrics now say WHICH frame was worst. Character side:
  `groundYUnderPoint` (0.05 u column) and `standingAnkleHeight()` (bind pose). **Rig** (`tests/scene/
  motion/GroundingTest.cpp`): FloorWorld, one chunk, a 12-riser 1/3 u ramp built from subcubes
  (footgun: the occupancy query only visits cells flagged as CUBES, so a subdivided cell needs
  `setCube` + `markSubdivided` + `setSubcube`; and the declared ramp function is pinned against the
  built voxels), the character walks DOWN it at 1.5 u/s under the external-velocity drive.
  **Red baseline (IK off, measured):** still-foot float **0.396 u**, penetration **0.152 u**
  (flat control: 0.062 / 0.000). My written prediction (float ≈ one riser, 0.30–0.35) was right in
  kind and low in size; the penetration was NOT predicted — the TRAILING foot sinks into the upper
  riser as the capsule steps down. **IK on with the OLD terrain-follow was identical to off**: three
  defects — the 0.25 u capsule-width probe, a fixed 0.8 u probe window that missed a surface sitting
  exactly at its bottom edge, and `corrRaw` clamped to ≤ 0 so the lift branch (and its lock) could
  never fire. **Rewrite (items 1–3):** foot-sized probes searched from 0.5 u above the foot down to
  `capsule − maxCorr − 0.15`; bidirectional correction = surface under the foot − capsule floor
  (down only in stance, up in any phase); the "ledge" skip now only for shared drops deeper than
  0.75 × step height (a ramp riser under both feet IS corrected); a failed probe skips the
  correction (`probe_ok`); pelvis drop bounded by the plan-derived body range; ankle pitch on
  gentle slopes only (≤ 1/9 u across the foot, cap 30°). **Dense trace (per frame, IK on) caught two
  more:** a toe-off foot flying forward and down read as "not swinging" (the rule was rising-only)
  and got LOCKED to the riser it was leaving (penetration 0.20); and a 0.25 u pelvis drop with a
  straight trailing leg sank that foot 4 cm. Fixes: swing = rising OR world-XZ speed > 0.8 u/s;
  pelvis drop = the full downward correction bounded by the held leg's remaining reach. **Item 4
  written:** `POST /api/debug/foot_ik` (omitted = unchanged, echo, clamps in
  `setGroundingKnobs`), `grounding` block on `get_animation_state`, `GET /api/animation/grounding`,
  MCP `set_foot_ik` / `get_animation_grounding`, `footIKEnabled` promoted from toolOnly to a
  RUNTIME clip key the solve honours; the live oracle route uses the point probe + ankle reference.
  **Measured state after the rewrite (the number that matters):** ramp IK on → penetration
  **0.003 u** (was 0.152), skate **0.012** (was 0.037), 141 still frames judged, still-foot float
  **0.170 u** (was 0.395) — flat control unchanged at 0.062 / 0.000 / 0.060, IK on == off on the
  flat floor to the millimetre. Seam test: the same ramp across the x=32 seam reads 0.163 / 0.003 /
  0.010 vs 0.170 / 0.003 / 0.012 inside one chunk (float noise at the 0.3 u/s still-threshold on
  two edge frames; tolerance 0.01 / 0.005 / ±3 frames). **The design's "flat + 0.03" float bar is
  NOT met.** The residual is a transient of a few frames per riser: while the capsule step-glides
  down (5 frames, 4 u/s) with the trailing leg straight, the landing foot cannot reach the lower
  cell — the pelvis drop is bounded by the trailing leg's reach at its animated target. The
  physical answer (heel-off: let the trailing ankle descend to flat-foot height while the foot
  pitches to keep the toe planted) was built and tried in **six rounds** (contact by ankle, by
  toe, hold-target relaxation, follow-room bound, release by toe height 0.10 → 0.25, release by
  toe XZ speed): each fixed the frames it targeted and created a 0.18–0.26 u sink or a 2 u/s
  skate elsewhere, because contact/release flipped one frame early somewhere in the stride. Every
  round is a dense per-frame trace in the session; the implementation was reverted to the stable
  form and the heel-off model is logged as **the next lever**, not a defect of the terrain probe.
  Two more measurement facts: the oracle's foot joint is the TOE (`findFeet` prefers ToeBase), 0.13 u
  ahead of the ankle the solver probes, so the oracle's sole extends BACKWARD from its joint (toe,
  mid, heel) while the solver's spans ±0.1 around the ankle; and the ground under a foot is the
  MEDIAN of its three sole samples (two of three on a level = supported), not the max — the max
  parked a toe 0.4 u in the air over the lower step with 4 cm of heel on the riser edge.
  Penetration is judged at the toe's own column. The IK-on pin now holds the honest state: no sink
  (≤ 0.03), no skate (< 0.10), float ≤ 0.20 and less than half the IK-off baseline. Item 5 (cost at
  n=100 Release, pinned tests) and the default-flip decision still ahead.
- **2026-09-30 — A5 item 5 + the default flip.** **Cost (Release, CharacterTestbed, 100 standard
  NPCs on wander, A-B-A, 31 samples per arm):** frame time OFF 7.15 ms median, ON 7.29, OFF again
  7.32 — **ON−OFF = +0.035 ms/frame for 100 characters = 0.4 µs/char/frame, inside the A-B-A
  noise.** The header's "~360 µs/char/frame" was never measured and is retired in the comment;
  the flat-ground noise floor (half a microcube) skips the solve in the common case. Caveat: the
  wander walk is on FLAT ground, so this is the common-case cost, not the ramp cost (six probes +
  up to two 2-bone solves per frame there). `tools/interaction_pipeline/grounding_cost.py` is the
  rig. **Default flipped:** `m_footIKEnabled = true` (`AnimatedVoxelCharacter.h`); with the flag
  on, the pinned set stays green — `NpcPathSkate` ×4 (3.5 / 3.5 / 4.1 / 3.9 % unchanged),
  `CharacterGoldenPose`, `SeatSolve` ×5, `SeatMatrixStress`, `OffHandPin` ×4,
  `AnimationDeconfound`, `MotionOracleA1` ×10, `CharacterCapsuleScaling`, `CharacterFacing`,
  `ClipSelection`, `BodyPlan*` — 74/74; the flat-floor control is IK on == off to the millimetre.
  **Live L4 (Release) — INCONCLUSIVE, logged honestly:** a 6-riser subcube ramp laid through
  `/api/world/subcubes/batch` (162 subcubes + 39 cubes placed), a standard NPC patrolling it with
  `set_foot_ik` on: the readback shows the solve firing on the risers (corrections −0.333 with
  pelvis −0.14…−0.16, `probe_ok` true) and nothing firing with it off; but the engine-oracle
  numbers on the same walk (max still-float 0.97 both ways, penetration 0.30/0.31, ON stance
  samples 1) are dominated by the patrol rig — waypoint pauses and pathing along a 3-cell-wide
  ramp put a foot over the ramp's side, and the oracle's live point column (0.05 u) reads the
  upper cell at a riser edge. The headless L3 numbers are the evidence; the live gauntlet needs a
  wider ramp, a straight external-velocity drive over the API (NPC behaviours only today) and a
  1 cm oracle probe — `tools/interaction_pipeline/grounding_live.py` is the starting point.
  Screenshot of the live ramp: `screenshots/screenshot_20260930_195230_411.png`.
  **A5 state:** items 1–5 built; the float bar is the one unmet design number (0.170 transient vs
  flat + 0.03); the heel-off model is the next lever. Full Debug sweep with the flipped default
  running at the time of writing (result appended below when it lands). ⚠️ The MCP server needs a
  restart to see `set_foot_ik` / `get_animation_grounding`.
- **2026-10-01 — A5 CORRECTION (owner: "i see almost no difference between the two characters").**
  The owner was right and the readback was lying. Two identical live characters standing astride a
  riser edge, foot IK off vs on, had bone positions identical to the millimetre while the readback
  showed `R −0.333, pelvis −0.267` for the ON one — it echoed the REQUESTED correction, not the
  achieved. Root cause: the round-2 pelvis bound ("the other leg's remaining reach") was INVERTED.
  Lowering the pelvis brings the hip CLOSER to a foot held on the upper level, so that leg bends
  and never runs out of reach; the bound therefore clamped the drop to ~3 cm exactly when both
  legs were straight, and the lower foot could not reach down. The six "heel-off" rounds above
  were partly chasing this symptom. **Fix:** the pelvis drops the full downward correction (body
  range only); the readback now carries ACHIEVED values (`achieved.hips_before_y/after_y`,
  `l_foot_dy/r_foot_dy`) and is cleared on every early return (an OFF character had reported stale
  corrections). **New pin** `Grounding.StandingAstrideARiserEdgeActuallyMovesTheHipsAndTheLowerFoot`
  (headless): hips 17.369 → 17.102, lower foot dY −0.283, feet at 16.317 / 16.093 on the 16.333 /
  16.0 levels. **Live (Debug, CharacterTestbed, same spot, same preset):** OFF hips 18.290, shins
  17.387 / 17.387; ON hips 18.026, shins 17.322 / 17.109 — pelvis 0.26 u lower, lower foot 0.28 u
  down with the other knee bent; screenshot `screenshots/screenshot_20260930_222649_930.png`.
  **Walking ramp re-measured:** IK on → still-foot float **0.147** (was 0.170), penetration 0.010,
  skate 0.097 (the height-planting skate figure is artefact-prone on terrain and sits at the
  limit — watch it); flat control unchanged. The float bar (flat + 0.03 ≈ 0.09) is still not met
  on the walk; the remaining transient is now a real double-support limit, not a bound. Pinned set
  45/45. Lesson for the ledger: **a readback must report what was achieved, and a live A/B with
  bone positions beats a per-frame request trace.**
- **Gate status:** A0 ✅ · A1 ✅ · **A2 ✅ BUILT 2026-09-29** (red→green on all five red tests,
  L4 routing readback, behaviour pin, render probe falsified, instrument bug fixed; uncommitted) ·
  **A3 ✅ BUILT 2026-09-30** (uncommitted) · **A4 ✅ CLOSED 2026-09-30** (SeatFit engine-side, asset_metrics.v2, W1, one seated origin + per-frame seated solve, 102-cell matrix, live gauntlet 21/21; approach walk DEFERRED; uncommitted) · **A5 BUILT 2026-09-30, items 1–5 + default flipped ON** (probes/solve/seam/API/cost; float bar 0.170 vs flat+0.03 NOT met — heel-off model is the next lever; live gauntlet inconclusive; uncommitted). **Next: owner decides A5 close vs another pass on the heel-off residual; A6/A7 gates.**

## 5. Ordering and why

A0 → A1 → A2 → (A3 ∥ A6) → A4 → A5 → A7, T2 spike after A3. A0 first because three of the ten
defects contaminate every measurement after them (zeroed blend, dead offsets, double-counted
height). A1 before anything visible because the owner's directive is that "looks wrong" must arrive
as a number. A2 before A3/A4 because both need chains and one selection site. A4 before A5 because
the chair is the owner's stated picture and a seat is a *harder* grounding problem than terrain
(more contacts); solving it exercises the whole L3 layer that A5 then reuses.

## 6. What this plan deliberately does NOT do
- Replace the FSM or the `.anim` format (V2 fork 4: keep text short-term; a binary/rig-metadata
  format is a later optimisation).
- Re-attempt procedural humanoid locomotion, PD ragdoll, or IK that fights baked feet without phase.
- Adopt any learned runtime as a required dependency.
- Redo the voxel character. Generalise the rig, keep the voxels.

## 7. Verification discipline (applies to every phase)
L1 artifact · L2 structural invariant on real output · L3 functional (a character-box *uses* it)
· L4 live engine · **W wired** (the owning manager knows about it — `FunctionalWiringBacklog`).
Red-before-green with the A1 oracle; stress axis named per phase; the review panel is the last
gate and the owner owns every verdict; every claim cites the command and its raw result.

## 8. Immediate next steps (if the owner says go)
1. `/design-check` on **A0** (small) and **A1** together — they are the gate for everything.
2. In parallel, no code: write the **affordance schema** for seats and run the 100-chair matrix
   generator against TODAY's code to record the baseline failure table (red state for A4).
3. Owner review session for the 18 pending UniMate clips continues independently; promoted walks
   become A3's first phase-sync test pair.

## 9. Owner forks (decide before A2)
1. **Endorse T1-as-system / T2-behind-seam / T3-parked**, or push T2 forward as the main line despite
   the crowd numbers?
2. **First creature body plan** for A6 procedural gaits: spider (needs a voxel model), wolf/quadruped
   (has models + mocap), or size variants of the humanoid (least new art)?
3. **Rig diet:** keep the 65-bone Mixamo humanoid (max clip compatibility) or move to a leaner plan
   rig with mandatory retarget-on-import?
4. **Seat refusal policy** stays "never sit where it doesn't fit", or allow a visible "perch/lean"
   fallback pose for oversized seats?
5. **Reactivity ambition:** is A7 (canned + nudge) enough for the game you want, or is physical
   reaction a headline that justifies revisiting T3 later?
6. **Factor set (§3b):** confirm the first-class factors — gait class, state, grip class, load,
   condition, mood, size — or add/remove (e.g. terrain wetness/depth, wielded light source, armour
   weight class). Each first-class factor is a layer slot the schema must carry from day one.
7. **Pregeneration appetite:** fill the per-gait-class base sets with UniMate/forge output reviewed
   in the panel (fast, variable quality), or source mocap packs for bipeds/quadrupeds first and
   generate only the gaps?
8. **Creature model lane:** Meshy (paid; organic, auto-rigged, anonymous bones, render bug to
   fix first) vs the parametric forge (free, deterministic, spec-driven) — by default *both by
   role* as in A6; say if you want one to be the primary.

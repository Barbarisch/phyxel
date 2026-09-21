# MotionBricks Integration Plan

> **Status:** implementation-ready plan, approved direction as of 2026-09-04.
> **Scope:** optional learned locomotion for humanoid characters. This does not replace authored
> action clips, creature animation, character physics, or terrain grounding.
>
> Upstream: [`localai-org/motion-bricks.cpp`](https://github.com/localai-org/motion-bricks.cpp)
> (C++23/GGML implementation, stable C ABI v1, CPU and Vulkan backends).

## 1. Outcome

Phyxel will be able to drive a humanoid's idle, walk, run, strafe, turn, and stop motion from
continuous movement intent instead of selecting a small set of clips and applying one whole-body
crossfade. MotionBricks is an optional motion source behind a Phyxel-owned interface. If its DLL,
model, style, compatible rig, or compute backend is unavailable, the character uses the existing
clip/FSM path with no behavioral or pose change.

The feature is successful only if it improves transition continuity and foot skate in a live voxel
character without causing frame spikes, changing capsule authority, or regressing authored actions.

## 2. Decisions (locked for the first implementation)

1. **Hybrid system, not replacement.** MotionBricks owns grounded humanoid locomotion states only:
   `Idle`, `StartWalking`, `Walk`, `Run`, `FastRun`, `BackwardWalk`, strafes, turns, `StopWalk`, and
   `StopRun`. Jump/fall/land, stairs, combat, spells, interactions, reactions, death, and emotes stay
   authored until separately proven.
2. **Optional dynamically loaded DLL.** Phyxel remains C++17. MotionBricks builds independently as
   C++23 and is accessed only through its C ABI. No upstream C++ headers or GGML types leak into
   `phyxel_core` public headers.
3. **No configure-time downloads.** Upstream is built with
   `MOTIONBRICKS_DOWNLOAD_MODELS=OFF`. Models/styles are installed explicitly, hash-verified, and
   never downloaded by an ordinary Phyxel configure/build/package operation.
4. **Provider boundary first.** A fake provider lands and passes tests before the real DLL adapter.
   This isolates pose routing, fallback, time ownership, and retargeting from ML/runtime failures.
5. **Retarget, do not rename.** MotionBricks' fixed 34-joint local-rotation output is converted to
   the loaded rig with an explicit source-joint -> `BodyPlan` semantic mapping and bind-pose
   correction. Unmapped target bones retain their existing/base pose.
6. **Buffered asynchronous planning.** `mb_agent_plan` never runs in render or character-update code.
   A scheduler produces immutable motion windows; the game thread samples already-published data.
7. **Gameplay owns the root.** The character controller/capsule remains authoritative for world
   translation. Generated root displacement is used for phase/speed matching and diagnostics, not
   to move through collision. Y root motion is ignored during ordinary locomotion; existing ground
   resolution remains authoritative.
8. **Post-process order:** source pose -> retarget -> constrained foot lock/IK -> posture/additive
   corrections -> global transforms -> voxel rendering. Existing foot IK remains default-off until
   the MotionOracle proves an enabled profile improves the terrain gauntlet.
9. **Deterministic fallback is permanent.** Saved games and packaged games must remain usable without
   model assets. Seeds are derived from stable character identity, never update order or chunk.
10. **Humanoids only in v1.** G1's 34-joint topology is not a general creature-motion solution.

## 3. Design-key answers

- **Voxel aesthetic:** output drives the existing segmented voxel body; it introduces no smooth
  mesh or foreign rendering style. Natural weight shifts and transitions should improve the voxel
  character rather than visually replace it. The A/B gate can reject styles whose high-frequency
  detail reads as noisy on coarse voxel limbs.
- **Procedural-generation pipeline:** not a world-generation stage. Stable per-character provider,
  style, and seed selections belong to persistent character definitions when persistence is added;
  results must not depend on chunk residence or evaluation order.
- **API:** no public gameplay API in the first slice. The later diagnostic API is deliberately
  read-only/configuration-oriented and reports requested plus effective state. Shipping defaults do
  not change during the prototype.
- **Visual test:** a one-character, one-chunk flat slab plus a one-variable terrain gauntlet is
  mandatory (section 8). Every live comparison uses the same character, route, camera, and seed.
- **Small world:** the rig is a 9x9 floor fully inside one resident chunk. The uneven variant changes
  only floor height along the route and verifies placed voxels before recording.

## 4. Runtime architecture

```text
Character controller intent (velocity, facing, desired speed, stable seed)
                              |
                       MotionSource
                  +-----------+------------+
                  |                        |
            ClipMotionSource        MotionBricksSource
             current FSM         async planner + motion window
                  |                        |
                  +-----------+------------+
                              |
                     LocalPoseFrame
                target-local rotations + root sample
                              |
               HumanoidRetargeter / bind corrections
                              |
                  foot lock + terrain IK (gated)
                              |
                  existing global bone transforms
                              |
                    existing voxel renderer
```

### Phyxel-owned types

- `MotionIntent`: world-space planar movement direction, model-facing direction, target speed in
  world units/second, optional target position/heading, style key, and stable `uint64_t` seed.
- `LocalPoseFrame`: timestamp, root translation, local XYZW rotations, source joint identifiers,
  validity/discontinuity flags, and provenance (`Clip`, `MotionBricks`, `Fallback`).
- `IMotionSource`: `configure`, `submitIntent`, `sample`, `reset`, `status`. It must be nonblocking
  from the caller's perspective.
- `MotionSourceStatus`: requested provider, effective provider, ready/degraded/error state, backend,
  queue age, buffered duration, last plan latency, and a bounded error string.
- `HumanoidRetargetMap`: source joint, target semantic/bone, source bind rotation, target bind
  rotation, correction, and scale policy. Validation rejects missing hips or incomplete leg chains.

`AnimatedVoxelCharacter` initially owns one source selection and consumes `LocalPoseFrame`; model
loading and scheduling live in a shared service so 100 characters do not load 100 model copies.

### Coordinate and quaternion contract

The adapter must establish these facts with fixtures, not assumptions:

- Phyxel is right-handed, Y-up, and character model-forward is +Z.
- MotionBricks returns row-major F32 root XYZ and **local XYZW** rotations.
- Quaternions are normalized and hemisphere-corrected against the previous frame before blending.
- Retarget correction is derived from source and target bind transforms and pinned by a neutral-pose
  test. Axis conversion occurs once at the adapter boundary.
- Sampling is time-based. MotionBricks frame cadence is read from the bundle/known format and must
  not be inferred from Phyxel frame rate.

## 5. Dependency, assets, and licensing

- Add upstream as a pinned source dependency only after the provider seam is green. Record the exact
  commit and SHA-256 hashes for DLL, model bundle, and every shipped `.mbstyle` file.
- Prefer a separately built shared library over `add_subdirectory`: it prevents upstream C++23 and
  GGML/Vulkan build settings from mutating Phyxel targets.
- Build CPU first. Vulkan is a separate acceptance milestone because GGML may create its own Vulkan
  objects, allocate substantial VRAM, and contend with rendering.
- The source is Apache-2.0. Model and style assets use the NVIDIA Open Model License. Before any
  redistribution, add the required license/agreement and attribution to `docs/ATTRIBUTION.md` and
  packaged notices, and complete a human license review. Development download is not authorization
  to ship the weights.
- The model is approximately 0.73 GB F32 / 183,148,382 parameters. It is optional content, not part
  of the base repository or default game package.
- `tools/package_game.py` includes the DLL/model/styles only when the project explicitly enables the
  provider and the license-notice bundle is present; otherwise packaging fails clearly or packages
  the clip fallback, never a half-working learned backend.

## 6. Implementation phases

### M0 - Contract and red tests

**Files introduced:**

- `engine/include/scene/motion/MotionSource.h`
- `engine/include/scene/motion/MotionTypes.h`
- `engine/src/scene/motion/ClipMotionSource.cpp`
- `tests/scene/motion/MotionSourceTest.cpp`
- `tests/scene/motion/HumanoidRetargeterTest.cpp`

Add the provider-neutral data model, fake source, and adapter seam without changing shipped behavior.
The clip path remains the only effective source.

Red tests first:

1. a neutral source pose without bind correction fails the target neutral-pose equality;
2. XYZW interpreted as WXYZ produces a known 90-degree fixture failure;
3. a discontinuous quaternion sign produces an angular spike;
4. an empty/late/corrupt provider frame must fall back to the exact existing clip pose;
5. unmapped fingers/toes must retain their base animation pose;
6. repeated stable intent/seed produces byte-equivalent provider commands.

**Exit:** all fixtures pass; `CharacterGoldenPoseTest` remains unchanged; no model or network is
required; default runtime images are pixel/pose identical.

### M1 - Retargeting and character integration with fake motion

- Implement `HumanoidRetargeter` using the existing `BodyPlan` rather than Mixamo literals.
- Extend the humanoid plan with optional motion-source semantic aliases only if the existing fields
  cannot express the mapping cleanly; preserve `builtinHumanoid()`/JSON field equality.
- Add a test-only deterministic source that produces neutral, stride, turn, and stop pose windows.
- Route only locomotion states through the provider seam; authored states force a controlled handoff
  to the FSM. Reset provider history after teleport, rig change, resurrection, or large time jump.
- Define transition alignment: hemisphere-correct local quaternion blend from current rendered pose;
  capsule position is unchanged; feet may not jump more than the calibrated good-clip envelope.

**Exit:** deterministic source plays in the animation editor/live character; forced provider failure
returns to clips without a pop above the calibrated threshold; all existing character tests pass.

### M2 - Native DLL loader and ABI smoke test

- Add `MotionBricksLibrary` with platform loading (`LoadLibraryW`/`GetProcAddress` on Windows) and a
  complete required-symbol table for ABI v1.
- Never include upstream headers in public engine headers; mirror only fixed-width ABI declarations
  in a private translation unit or generate them from the pinned header.
- Validate `mb_abi_version`, joint count/names/parents, model parameter count, styles, and status
  strings before creating agents.
- One shared immutable model; styles cached by key; one stateful `mb_agent` per actively generated
  character. Every create has its matching free on success, partial failure, shutdown, and reload.
- Add a standalone smoke/integration test using a tiny fake DLL first, then an opt-in real-bundle
  test excluded when assets are absent.

**Exit:** absent/wrong-version DLL and absent/corrupt model degrade cleanly; real CPU backend returns
a valid finite 34-joint motion; unload/reload x100 has no leaked handles or crashes.

### M3 - Async planner and CPU prototype

- Shared bounded job queue; no plan call on game/render threads.
- Per-agent double-buffered immutable motion windows published atomically.
- High/low water marks are expressed in seconds of motion. Initial values are measured from real
  plan latency and recorded in code comments; they are not guessed here.
- Coalesce superseded intents, prioritize player/near-visible characters, and rate-limit replans.
- If a window underruns, briefly continue the last valid sample within a measured grace interval,
  then blend to the clip fallback. Never block waiting for inference.
- Instrument plan p50/p95/p99, queue wait, underruns, generated frames/second, active agents, model
  RAM, and per-frame adapter/retarget time.

**Exit:** one CPU-backed character completes the scripted locomotion route with zero main-thread
plan calls, no invalid poses, and no visible fallback under steady input.

### M4 - MotionOracle, terrain post-process, and live A/B

Implement or complete the metrics already specified in `CharacterAnimationV2.md`:

- pose continuity (per-bone geodesic delta);
- angular velocity/acceleration envelope;
- root velocity versus capsule velocity;
- stance-foot skate and ground clearance;
- leg chain-length preservation and knee inversion;
- self-intersection proxy.

Calibrate thresholds from named known-good shipped Mixamo clips, check the calibration artifact into
the test data, and prove each metric red with a synthetic defect. Compare clips and MotionBricks on
the identical route/seed. Only then test a constrained foot-lock/IK profile on the uneven gauntlet.

**Exit (L4):** learned locomotion improves the predeclared continuity/skate metrics or is rejected;
zero ground penetrations/knee inversions; visual orbit capture shows no voxel-limb jitter or torso
self-intersection. Human review remains the final aesthetic gate.

### M5 - Scale, Vulkan evaluation, packaging decision

Stress 1, 10, and 100 characters across idle, coherent movement, and worst-case independently
changing intents. Record CPU/GPU frame time, plan latency distribution, queue age, underruns,
RAM/VRAM, and model startup time. Test Vulkan only after the CPU baseline, on the same scene.

Go/no-go:

- **Runtime default candidate:** 10 nearby active characters sustain the engine frame budget with no
  p99 hitch attributable to planning and no steady-state underruns.
- **Tiered runtime:** if 10 passes but 100 does not, learned motion is limited to a budgeted nearest/
  most-visible set; distant characters use clips. Assignment hysteresis must prevent pose thrashing.
- **Offline generator only:** if buffered runtime inference still spikes or consumes unacceptable
  memory, expose the adapter as an authoring tool that bakes generated windows into `.anim` clips.
- **Reject:** if the oracle or human A/B finds no material improvement after correct retargeting.

Packaging remains opt-in until license review, deterministic fallback, cold-start behavior, and a
Release build have all passed.

## 7. State ownership and failure policy

| Event | Required behavior |
|---|---|
| DLL/model/style missing | Report degraded status; use clips; log once per cause |
| Planner error/non-finite output | Discard entire window; preserve last valid pose; schedule fallback |
| Queue saturation | Coalesce newest intent; lower-priority agents remain on clips |
| Teleport/time discontinuity | Reset agent context and blend from current rendered pose |
| Enter authored state | Stop consuming learned frames and transition to the authored clip |
| Return to locomotion | Seed/reset context from at least four frames when supported, otherwise style reset |
| Rig incompatibility | Never partially retarget; use clips and expose missing semantics |
| Provider disable/reload | Drain jobs safely; no callback may retain a destroyed character |

Learned output is animation data, not trusted control data: reject non-finite values, normalize
quaternions, bound root deltas/rotation rates using calibrated envelopes, and never feed generated
root translation directly through collision.

## 8. Verification matrix

| Layer | Rig / test | Contract |
|---|---|---|
| L1 unit | synthetic 34-joint source + humanoid target | axes, XYZW, bind correction, mapping |
| L2 unit | generated motion windows | finite/normalized/continuous poses; deterministic commands |
| L2 oracle | known-good and seeded-bad clips | every metric passes good and fails its bad fixture |
| L3 simulation | 9x9 flat one-chunk slab | idle -> walk -> run -> strafe -> 180 turn -> stop |
| L3 simulation | same slab with one-height-variable gauntlet | contacts, clearance, no inversion/stretch |
| L4 live | animation editor and CharacterTestbed | rendered A/B, logs, frame/GPU profiles |
| Stress | 1/10/100 agents; x100 load/unload | bounded queue/resources, no hitch/leak/use-after-free |

The flat control prediction is: MotionBricks reduces transition angular discontinuity and stance-foot
horizontal velocity versus the current FSM without changing capsule travel. The uneven experiment's
prediction is: post-retarget foot locking reduces clearance error without worsening knee inversion
or angular-acceleration envelopes. Failure of either prediction blocks rollout.

## 9. Diagnostic surface (after M2)

Add a debug-only API/MCP surface only when real inference exists:

- requested/effective provider, CPU/Vulkan backend, model/style identity and hashes;
- readiness/degraded reason, active agents, queue depth/age, buffered milliseconds;
- last/p50/p95/p99 plan latency and underrun/fallback counters;
- per-character stable seed and current provenance;
- provider selection with omitted = unchanged and response echoing effective state.

Do not expose opaque MotionBricks handles or raw filesystem paths to gameplay scripts. Public
gameplay intent remains velocity/facing/style semantics independent of the provider.

## 10. First implementation slice

Start with **M0 only**. It is deliberately dependency-free and can be reviewed independently:

1. write the six red tests;
2. introduce `MotionIntent`, `LocalPoseFrame`, `MotionSourceStatus`, and `IMotionSource`;
3. implement the quaternion continuity/validation helpers and fake source;
4. introduce `HumanoidRetargeter` with a small explicit fixture map;
5. prove exact clip fallback and rerun `CharacterGoldenPoseTest` + `BodyPlanTest`;
6. update this document with measured test evidence.

Do not vendor MotionBricks, download weights, alter shipped defaults, or modify the main character
update path in M0. Those actions begin only after the provider contract and retarget fixtures pass.

## 11. Deferred questions (not blockers for M0)

- Which upstream commit and binary distribution mechanism to pin.
- Which of the 15 upstream styles best matches Phyxel's voxel humanoid.
- CPU thread budget and measured buffer water marks.
- Whether Vulkan inference can safely share the selected physical device without frame-time harm.
- Whether redistribution of the NVIDIA-licensed model is acceptable for Phyxel releases.
- Runtime versus offline-only final deployment, decided by M4/M5 evidence.

## 12. Implementation record (2026-09-04)

M0-M3 and the provider-neutral portions of M4/M5 are implemented. The ABI-v1 loader, real CPU
inference smoke, async per-agent window publication, explicit G1-to-Mixamo hinge-chain retarget,
150 ms provider handoff, metrics/status surface, bounded opt-in runtime gate, combat phase graph,
reproducible pinned build script, and deny-on-incomplete packaging path are present. Focused tests:
23/23 provider/retarget/combat/oracle tests and 45/45 including golden pose, body plan, and clip
selection. The real bundle test produced a finite 34-joint window in 0.71-0.93 seconds.

The decision is **single-agent experimental runtime / fallback clips**, not runtime-default.
Measured first-window CPU stress was 2.43 seconds for 10 agents and 18.54 seconds for 100 after
serializing shared-model inference; unconstrained 10-way inference exceeded three minutes. Vulkan
was attempted but the pinned GGML nested shader build failed to inherit Ninja on Windows. These
results reject broad runtime deployment. Terrain visual A/B and calibrated shipped-clip thresholds
have not demonstrated the M4 rollout criteria and therefore cannot enable broader deployment. See
`docs/MotionBricks.md` for setup, diagnostics, licensing constraints, and measured evidence.

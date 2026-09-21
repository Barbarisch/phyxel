# MotionBricks locomotion provider

Phyxel now has an optional MotionBricks ABI-v1 locomotion provider. It is off by default and the
existing `.anim` clips remain the deterministic fallback. Learned output supplies local joint
rotations only: the capsule, collision, attack hit frames, blocks, casts, reactions, and root
movement stay authoritative in Phyxel.

## Build the pinned runtime

The integration is pinned to `localai-org/motion-bricks.cpp` commit
`6fdb75e15ddb7f97dd1a4abb8017a57b936bc7a3` (GGML submodule
`8c63e70982c95ceb862e3a1073a2c1beef75d60a`). Nothing is downloaded by an ordinary configure,
build, or package operation.

```powershell
.\tools\motionbricks\build_runtime.ps1 -DownloadModels
```

The script builds CPU support, stages `motionbricks.dll` with its GGML DLLs, and downloads the
pinned model/style distribution with size and SHA-256 verification. Add `-EnableVulkan` only for an
explicit backend experiment.

## Enable a development run

```powershell
$env:PHYXEL_MOTION_PROVIDER = "motionbricks"
$env:PHYXEL_MOTIONBRICKS_LIBRARY = (Resolve-Path build_motionbricks_runtime\runtime\motionbricks.dll)
$env:PHYXEL_MOTIONBRICKS_ASSETS = (Resolve-Path build_motionbricks_assets)
$env:PHYXEL_MOTIONBRICKS_STYLE = "walk.mbstyle"
$env:PHYXEL_MOTIONBRICKS_MAX_AGENTS = "1"
$env:PATH = (Resolve-Path build_motionbricks_runtime\runtime).Path + ";" + $env:PATH
.\phyxel.exe
```

`PHYXEL_MOTIONBRICKS_DEVICE=vulkan` selects Vulkan; CPU is the default. The active-agent budget is
one by default because the current evidence covers one real CPU agent, not a 10/100-character
shipping tier. Characters beyond the budget use clips.

The runtime reports readiness, fallback/error text, buffer time, queue age, underruns, completed
plans, and p50/p95/p99 planning latency through `AnimatedVoxelCharacter::getMotionSourceStatus()`.

## Retargeting and combat

`g1ToMixamoRetargetMap()` collapses G1's serial pitch/roll/yaw hinges into Mixamo ball joints.
Unmapped head, finger, and toe-end bones preserve the authored clip pose. Provider entry and
underrun recovery use a 150 ms local-quaternion blend, and non-finite or incomplete windows are
discarded.

`CombatTransitionMap` maps authored attack/block clips by semantic phase (windup, active, recovery,
guard). It is intentionally provider-neutral: MotionBricks can eventually synthesize a visual
bridge between a swing and guard, but cannot move hit windows or decide whether a block succeeds.

## Packaging and licenses

Runtime packaging is opt-in and deny-on-incomplete:

```powershell
python tools\package_game.py MyGame `
  --motionbricks-runtime build_motionbricks_runtime\runtime `
  --motionbricks-assets build_motionbricks_assets `
  --motionbricks-notices path\to\reviewed-notices
```

All three arguments are required together. Source code is Apache-2.0; the published model/style
bundle declares the NVIDIA Open Model License. The repository does not assert that those model
terms are approved for a particular commercial release; provide a human-reviewed notice bundle
before packaging.

## Measured prototype evidence

On the development Windows CPU build, the pinned real backend loaded 183,148,382 inference
parameters, exposed all 34 `g1skel34` joints, and returned a finite 34-joint motion window in
0.71-0.93 seconds using two GGML threads. The call ran on the provider worker, never the game
thread. ABI probing 100 times is stable using the process-lifetime module registry. This registry
works around an upstream GGML abort observed during repeated DLL unload/reload; native model,
style, command, motion, and agent allocations are still paired with frees.

After adding a shared-model inference gate, 10 CPU agents produced their first valid windows in
2.43 seconds and 100 in 18.54 seconds. An unconstrained 10-way run previously exceeded three
minutes due to GGML oversubscription. This confirms the tiered architecture and rejects 100-agent
runtime use; it does not justify increasing the default live budget above one because a 2.4-second
cold queue is visible. A Vulkan build was evaluated but the pinned GGML nested shader-generator
configure failed to inherit Ninja on this Windows toolchain, so Vulkan remains unavailable and
automatically falls back rather than becoming a release dependency.

The automated quality gate currently covers pose continuity, angular velocity/acceleration, root
velocity disagreement, planted-joint skate, and chain-length drift, including synthetic red
fixtures. Terrain capture, aesthetic A/B review, and 10/100-agent performance evidence remain
rollout gates, so the provider is deliberately not a shipping default.

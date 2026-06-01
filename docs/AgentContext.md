# Agent Context — start here for a new Claude session

This file is the **portable** working context for an AI agent (Claude Code) on this
repo. The agent's richer per-machine memory lives outside the repo
(`~/.claude/projects/.../memory/`) and will NOT be present on a different computer —
**this committed file is the substitute.** Keep it current at the end of a work session.

Absolute paths below (e.g. `C:\Users\<you>\...`) are machine-specific — adjust them.

---

## How to work this repo (hard-won operational lessons)

- **Build/run via the `phyxel` MCP tools** (`build_project`, `launch_engine`,
  `stop_engine`, `engine_running`, `screenshot`, `get_engine_logs`, …) — see CLAUDE.md.
- **The MCP server can hang.** If an MCP tool stalls (and the user says you've been
  "stuck"), believe them — you have **no clock visibility between tool calls**. Don't
  argue about elapsed time. Switch to driving the engine **directly over HTTP at
  `localhost:8090`** (e.g. `curl http://localhost:8090/api/...`) and build with raw
  cmake.
- **Raw build** (CMake is not on PATH):
  ```
  $env:PATH += ";C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
  cmake --build build --config Debug --target phyxel
  ```
- **Always `stop_engine` / kill `phyxel.exe` before rebuilding** — the linker cannot
  overwrite a running exe.
- **Verify fixes by RUNNING the engine** (build → launch → trigger the scenario →
  capture `screenshot`/`get_visual_diagnostic`/`get_engine_logs`). "Compiled clean" is
  NOT verification.
- **Stale-binary trap:** if behavior doesn't change after a build, the binary may be
  stale. (MSVC incremental linking did this — fixed via `/INCREMENTAL:NO` on the editor
  target.) Detect by grepping the built exe for a string literal unique to your edit:
  `grep -c "my marker" build/editor/Debug/phyxel.exe`. Trace obj → lib → exe to find
  where it goes stale.
- **The HTTP command queue has a 5s game-loop budget.** Heavy commands (`open_project`,
  large destruction ops) **time out the 5s wait but still complete** — re-query state,
  don't assume failure. (Reducing this for `open_project`'s heavy DB load is an open item.)
- **Visual testing:** use the **CharacterTestbed** project — the bundled default world
  DB renders magenta/stale. It lives at `C:\Users\<you>\Documents\PhyxelProjects\CharacterTestbed`
  (machine-specific). `launch_engine` needs the **full** path; a bare name resolves to the
  repo root and the engine exits "Project directory does not exist". After launch it sits
  on the project selector — follow with `open_project <fullpath>`.
- **Profiling endpoints:** `/api/debug/frame_profile` (CPU phase tree),
  `/api/debug/gpu_scopes` (per-pass GPU), `/api/debug/engine_timing` (fps / commandRecordTime /
  visibleInstances). **Measure per-pass before optimizing — don't guess the bottleneck.**
- **Logger API:** `LOG_INFO("Tag", "msg {}", x)` (`{}` placeholders) OR
  `LOG_INFO_FMT("Tag", "msg" << x)` (stream). Printf `%s`/`%.2f` print as literal text.
  The `{}` path silently DROPS the line past ~5 args — use `_FMT` (ostringstream, no limit)
  for many args.
- **Never auto-commit.** Commit/push only on explicit request. End commit messages with the
  `Co-Authored-By:` trailer.

---

## Engine ground truth (supersedes older docs/comments)

- **Physics is HYBRID, and Bullet is removed** (the `external/bullet3` submodule is reference
  only; `getActiveBulletCount()` is hardcoded 0). Two live in-house backends:
  - **`GpuParticlePhysics`** (Vulkan compute, warm-started) — the stable,
    count-scalable path; **destruction (`DamageSystem`) always routes here** via
    `queueSpawn`. Any doc/comment calling GPU/AVBD "broken/experimental" is STALE.
    - **TWO pipelines exist; only ONE is live.** The default is the **AVBD constraint
      solver** (`solver_*.comp`), selected by `m_useNewPipeline` — hardcoded `true`,
      never toggled off. The older **XPBD pipeline** (`particle_integrate.comp` /
      `particle_collide.comp`, the `m_integratePass`/`m_collidePass` dispatch in the
      legacy branch of `recordComputeCommands`) is **dead code** kept for reference.
      `particle_expand.comp` and the grid-sort passes are **shared** (NOT legacy).
      **All particle physics changes go in the `solver_*.comp` shaders.** Trap that
      already bit once: the character-vs-debris push lived only in the legacy
      `particle_collide.comp`, so the player passed straight through GPU debris until
      it was ported to `solver_integrate.comp`. Character collision now works in the
      live solver (debris inherits character velocity on AABB overlap).
  - **`VoxelDynamicsWorld`** (custom CPU sequential-impulse rigid-body world) — furniture,
    the **static-terrain occupancy grids characters ground against**, and the **left-click
    break-debris path** (`breakCube` → `addGlobalDynamicCube` → `DynamicObjectManager`).
  - **Break routing** (`VoxelManipulationSystem`, see `docs/DynamicVoxelPhysics.md`): a
    left-click break PREFERS CPU `VoxelDynamicsWorld` and only falls back to GPU particles
    when smoothed FPS drops below a threshold. So "GPU is primary" is true for
    destruction/scale, NOT for every single break — it's genuinely hybrid. Don't overstate it.
  - Each GPU particle is one independent body; constraints are contacts only (no welds → no
    coherent rigid multi-voxel fragments yet — that's the destruction P5 idea).
- **Static collision = per-chunk `VoxelOccupancyGrid`** (sub-voxel: cube→subcube→microcube,
  bitset O(1) tests) registered into `VoxelDynamicsWorld::m_grids`. It is NOT redundant with
  `ChunkManager` cubes — don't "simplify" it away. See `docs/AgentContext.md` collision rule
  below.
- **Voxel mutation perf rule:** never re-mesh a chunk per-voxel inside a batch operation.
  `removeCubeFast` defers the re-mesh (marks the chunk dirty); the per-frame
  `ChunkManager::updateDirtyChunks()` re-meshes each touched chunk once. (This was a 48×
  destruction speedup: 5.4s → 113ms.)
- **Collision registration RULE (CPU grids):** any code path that loads chunks from the DB
  MUST follow `loadAllChunksFromDatabase()` with `ChunkManager::buildAllChunkPhysics()` —
  otherwise the occupancy grids are never registered and **characters fall through the
  world**. `AnimatedVoxelCharacter` logs a one-time ERROR ("No terrain occupancy grids
  registered…") if this is ever skipped again.
- **Collision registration RULE (GPU grid) — the analog:** the GPU particle solver has its
  OWN occupancy grid, separate from the CPU one. Every world-load/build path MUST also call
  `ChunkManager::rebuildOccupancyFromChunks()` — otherwise **debris particles have no floor
  and fall straight through the world** (while the character still stands, because that's the
  CPU grid). The init-time rebuild runs with 0 chunks, so it does NOT cover the
  `--project`/`autoLoadGameDefinition` or DB-load paths — those each call it explicitly.
  Symptom of a missing call: log shows only `Occupancy grid rebuilt from 0 chunks`.

---

## Current workstreams & roadmap (update me at session end)

- **Destruction system** (`docs/DestructionSystem.md`, `engine/core/DamageSystem`): P1 area
  damage, P2 damage accumulation + per-material toughness, P3 structural-collapse with
  "connected-to-main-mass" anchor, and the lag-spike fix are DONE + committed. Roadmap:
  **P4** visual cracks on damaged-but-unbroken voxels; **P5** GPU weld constraints for
  coherent breakable fragments; bedrock/anchor pin flags.
- **Character grounding robustness:** fall-through root cause fixed (DB-load paths now build
  + register physics) with fail-loud + auto-register invariant. Done + committed.
- **Character ↔ debris interaction:** the character now PUSHES GPU debris (one-way). The push
  lives in the live AVBD `solver_integrate.comp` (NOT the dead legacy `particle_collide.comp`).
  It uses the character's **12 per-limb segment boxes** (4 torso + 4 arm + 4 leg) uploaded each
  frame via `setCharacterColliders` (same boxes fed to the CPU `setKinematicObstacles` path),
  with a **broadphase union AABB** tested first for a cheap early-out. Also fixed debris falling
  through the floor (GPU occupancy grid not rebuilt on world-load paths — see GPU rule above).
  Done + committed.
  - **Trap (cost me a debug cycle):** `GpuParticlePhysics::MAX_CHAR_SEGMENTS` caps how many
    segment boxes upload. The character builds **12**; when the cap was 8 it silently dropped
    the trailing 4 (the LEGS), so short floor-resting debris (subcubes/microcubes) was never
    pushed while full-height cubes were. If a body region stops colliding, check this cap and
    `buildSegmentBoxes`' `kSegments` count FIRST.
  - **Known gap:** the lowest boxes are the shins (`mixamorig:LeftLeg`/`RightLeg`) — there is
    NO dedicated foot box, so debris directly under the foot tip can slip the shin box. Fix if
    it matters: also upload the controller capsule (reaches the floor) as an extra collider.
  - GPU debris does NOT push the character back (would need a GPU→CPU readback) — out of scope.
- **Spell VFX system:** 3-layer architecture (dumb archetypes → per-spell composition →
  gameplay modifiers) implemented. `VfxSystem`/`VfxDirector`/`SpellVfxMapper` +
  `VfxRenderPipeline`. Done + committed.
- **Render perf:** 18 → 235 FPS via removing two per-frame brute-force loops (mirror-voxel
  scan cache + `getPerformanceStats` O(1)). Open ideas: skip OIT pass when no transparent
  voxels, 36→6 index cube draw, backface cull (winding is fragile — see render docs).
- **Debris/particle solver perf:** the GPU particle solver (`GpuParticlePhysics`,
  `recordComputeCommandsNew`) dominated frame time under debris load. **Per-pass GPU timing
  is now built in** — `recordComputeCommands` takes an optional `GpuProfiler*` and emits
  phase scopes on the first physics tick: Setup / GridClear / GridBuild / SortScan /
  SortScatter / NarrowVoxel / ColoringCSR / Solve / Finalize, nested under "GPU Particles"
  in the `gpu_scopes` endpoint. **How to use:** break a wall (`apply_damage`, ~3000 debris),
  then `curl /api/debug/gpu_scopes` + `/api/debug/engine_timing`.
  - **FIXED:** the inter-particle broadphase prefix sum (`particle_sort_scan.comp`) was a
    SINGLE GPU thread serially scanning all 64³=262,144 grid cells every tick — ~24ms/tick,
    a FIXED cost regardless of particle count, ~72% of the solver. Replaced with a 3-pass
    work-efficient parallel scan (`particle_scan_block` / `_blocksums` / `_add`). Result
    (Debug, 3166 debris): SortScan 24ms→0.2ms, solver 130ms→10ms, ~7→50 FPS, and physics
    back to 1 tick/frame (the low-FPS→more-ticks spiral stops). Lesson: a `dispatch(cmd, 1)`
    over a large buffer is a serial-scan trap — check dispatch sizes.
  - **Next targets** (per the per-pass breakdown): NarrowVoxel (~5ms) and Solve (~3ms).
  - **Settling "popcorn" (PARKED — don't reopen without a plan):** debris stays too
    energetic, esp. in concave/bowl piles. Root cause: it's a pure position-based solver
    (NO restitution term anywhere — confirmed; so it's not "bounciness" to tune down) —
    resolving penetration moves position, and position deltas become velocity, so overlap
    injects energy. Two feeders: (1) spawn overlap — FIXED, shatter pieces now spawn at
    distinct non-overlapping sub-cell positions (`DamageSystem::applyDamage`), which removed
    the spawn-time burst but did NOT fully calm dense piles; (2) dense bowls — many
    simultaneous contacts under-converge in 8 solve iters + there's no sleep system, so
    residual jitter never halts (bodies are fully solved until the ~25s lifetime expires).
    Remaining real fix = a **contact-aware sleep/freeze with hysteresis** (also the biggest
    steady-state perf win). RISK: legacy XPBD sleep "oscillated in/out on stacks" — needs
    island-based waking + separate sleep/wake thresholds. Player-wake is easy (the per-limb
    char push in `solver_integrate.comp` just clears the sleep flag, as legacy did);
    debris-waking-debris (island propagation) is the hard part. User tabled this to avoid
    regressing working behavior — pick it up deliberately, not casually.
- **Open items:** `open_project` / heavy commands time out the 5s game-loop budget (one-time
  heavy load, cosmetic); no world DB versioning.

---

## User working preferences

- **Single source of truth** for state; **incremental** architecture simplification.
  **Avoid big rewrites and over-abstraction** — when investigation shows a refactor isn't
  warranted, say so and stop.
- **Measure before optimizing**; reproduce + instrument rather than guessing.
- **Verify before destructive "repair":** impossible-looking output usually means the
  harness/tooling is wrong (e.g. a stale binary / hung MCP), not the source — confirm with
  `git diff HEAD` / a grep before "fixing" working code.
- **Editor UI:** action buttons (Reset/Delete/…) always visible regardless of state;
  per-object properties on the object's panel; global action settings on their own panel.
- Wants thorough design/planning discussion before building large features.

---

*Last meaningful update: debris broadphase perf fix landed on `main` — parallel prefix sum
replaced the single-thread serial scan (SortScan 24ms→0.2ms, ~7→50 FPS at 3000 debris); added
permanent per-pass GPU timing scopes. Open thread: debris settling/"popcorn" + no sleep system
(see Debris/particle solver perf above). Earlier: per-limb character↔debris push, floor-collision
fix, legacy XPBD pipeline marked dead. Fresh session? Skim this, then `git log --oneline -15`.*

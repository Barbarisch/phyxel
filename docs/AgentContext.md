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
- **Character ↔ debris interaction:** the character now PUSHES GPU debris (one-way) — ported
  the AABB push into the live AVBD `solver_integrate.comp`; also fixed debris falling through
  the floor (GPU occupancy grid not rebuilt on world-load paths — see GPU rule above). Done +
  committed. **Next:** upgrade the single body-capsule AABB to the 8 per-limb segment boxes
  (already computed each frame for the CPU `setKinematicObstacles` path) so arms/legs push
  too, not just the torso. GPU debris does NOT push the character back (would need a readback)
  — deliberately out of scope.
- **Spell VFX system:** 3-layer architecture (dumb archetypes → per-spell composition →
  gameplay modifiers) implemented. `VfxSystem`/`VfxDirector`/`SpellVfxMapper` +
  `VfxRenderPipeline`. Done + committed.
- **Render perf:** 18 → 235 FPS via removing two per-frame brute-force loops (mirror-voxel
  scan cache + `getPerformanceStats` O(1)). Open ideas: skip OIT pass when no transparent
  voxels, 36→6 index cube draw, backface cull (winding is fragile — see render docs).
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

*Last meaningful update: character↔debris push landed on `main` (character collision ported
to the live AVBD solver; GPU occupancy floor-collision fix; legacy XPBD pipeline marked dead).
If you're a fresh session, skim this, then `git log --oneline -15` and
`docs/DestructionSystem.md`.*

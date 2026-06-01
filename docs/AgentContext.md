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

- **GPU AVBD physics (`GpuParticlePhysics`) is the LIVE, primary, stable physics path** for
  voxel debris (Vulkan compute XPBD, warm-started). Bullet was removed. Any doc/comment
  calling GPU/AVBD physics "broken/experimental" is STALE.
- **CPU `VoxelDynamicsWorld` still exists** for: furniture rigid bodies, and the **static
  terrain occupancy grids that kinematic characters ground against**. Each GPU particle is
  one independent body; constraints are contacts only (no welds → no coherent rigid
  multi-voxel fragments yet — that's the destruction P5 idea).
- **Static collision = per-chunk `VoxelOccupancyGrid`** (sub-voxel: cube→subcube→microcube,
  bitset O(1) tests) registered into `VoxelDynamicsWorld::m_grids`. It is NOT redundant with
  `ChunkManager` cubes — don't "simplify" it away. See `docs/AgentContext.md` collision rule
  below.
- **Voxel mutation perf rule:** never re-mesh a chunk per-voxel inside a batch operation.
  `removeCubeFast` defers the re-mesh (marks the chunk dirty); the per-frame
  `ChunkManager::updateDirtyChunks()` re-meshes each touched chunk once. (This was a 48×
  destruction speedup: 5.4s → 113ms.)
- **Collision registration RULE:** any code path that loads chunks from the DB MUST follow
  `loadAllChunksFromDatabase()` with `ChunkManager::buildAllChunkPhysics()` — otherwise the
  occupancy grids are never registered and **characters fall through the world**.
  `AnimatedVoxelCharacter` logs a one-time ERROR ("No terrain occupancy grids registered…")
  if this is ever skipped again.

---

## Current workstreams & roadmap (update me at session end)

- **Destruction system** (`docs/DestructionSystem.md`, `engine/core/DamageSystem`): P1 area
  damage, P2 damage accumulation + per-material toughness, P3 structural-collapse with
  "connected-to-main-mass" anchor, and the lag-spike fix are DONE + committed. Roadmap:
  **P4** visual cracks on damaged-but-unbroken voxels; **P5** GPU weld constraints for
  coherent breakable fragments; bedrock/anchor pin flags.
- **Character grounding robustness:** fall-through root cause fixed (DB-load paths now build
  + register physics) with fail-loud + auto-register invariant. Done + committed.
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

*Last meaningful update: destruction lag-spike fix + character-grounding robustness landed
on `main`. If you're a fresh session, skim this, then `git log --oneline -15` and
`docs/DestructionSystem.md`.*

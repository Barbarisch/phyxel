# Debris Interaction Plan — everything that moves can push GPU debris

**Status:** rev 4.13, 2026-10-07. **Phase 0 DONE** (main `ed924498`; results under Phase 0).
**Phase 1 DONE** (pushed to main through 1f):
- 1a build safety ✅ · 1b `shaders/solver_shared.h` ✅
- 1c one occupancy: steps 1–4 ✅ (tri-state query, edit-first repack, debris reads the shared
  pool, old bitfield deleted) · step 5 writer audit ✅ (7 of 8 gaps closed, red→green,
  `OccupancyCoverageTest`; bench `phase1c5` in band) · step 6 ✅ (`DebrisContactOccupancyTest`,
  `occupancy_diff`, asserted by every bench scenario)
- 1c blockers CLOSED: the session-state dependence was two solver leaks (slot order after
  `despawnAll`, warm-start hash never re-cleared); the blast "shift" was that leak — leak-free
  blast is 86.2 mm and a warm session now matches a fresh engine (details under 1c).
- 1d all break debris on the GPU: part 1 one spawn helper + `break_voxel` hook, part 2 texture
  parity (pixel red -0.183 -> green 0.969), part 3 CPU debris path deleted.
- 1e driven-input counters + analyzer window, 1f scripted kinematic box.
- **Phase 2 DONE**: movers are AVBD bodies, default flags 55, the D7 shove deleted (user accepted a
  few-cm transient overlap in packed piles; isolated bodies <= 2 cm).
- **Phase 3a DONE**: every animated character (player, entities, NPCs) pushes debris; far
  (update-LOD) characters are extrapolated.
- **Phase 3c DONE**: CPU rigid bodies (furniture, fragments, trees, item props) push debris,
  one-way, whole bodies within the 512-box budget.
- **Phase 3b DONE** (the `sword_swat` demo deprioritized by the user): doors, animated parts and held items push
  debris, and doors block CPU bodies. Open: angular velocity for GPU movers (door overlap median
  37 mm).
- **Phase 4 core DONE**: one impulse law for both worlds, `POST /api/physics/impulse`, and every
  blast or spell pushes existing debris and CPU bodies (the user's spell scenario passes live).
  Open: `CombatSystem` swing cones, `try_push`, angular kicks.
- **Phase 5 DONE** (2026-10-07): shared `DebrisRuntime` + shared debris API handlers; shipped
  games (scaffold, `minimal_game`) run GPU debris on by default, and scaffold spells blast.
  Verified in a PACKAGED Release game: bench parity with the editor, the blast's exact numbers,
  and the loud-off control. Next: Phase 6 (optional; separate design check).
- Still open, minor: 1c step 5 gap 8 (incremental add does not filter broken/invisible
  sub-voxels); drop_pile varies run to run (GPU nondeterminism, not session state).
- Fixed in Phase 4: the bench's blast-site restore kept damaged floor cubes, so back-to-back
  blast runs differed (now `replace: true`).
- Not started: Phase 6. Phase 3 budget orders by camera distance only (no host-side debris
  positions without a readback).
- Rev 2 rewrote the phases after a four-way code inventory (§Inventory).
- Rev 3 (user direction) puts simplification first: delete the old systems before new work.
- **Rev 4 folds in the second design check:**
  - debris adopts the existing lighting occupancy instead of a new grid (1c);
  - B-key-to-GPU (D1) moves after it;
  - texture parity for debris;
  - test hooks and the Phase 0 regression band.
**Predecessor:** [DebrisSettlingPlan.md §R](DebrisSettlingPlan.md). Debris now settles naturally;
this plan makes it *interactive*, *correct about the world*, and *shippable*.
**Gate:** [FeatureDesignKeys.md](FeatureDesignKeys.md). The design check ran 2026-10-04 with
verdict NEEDS WORK; §Gate maps each open item to the phase that resolves it.

## Goal

Every moving thing can push and knock around debris:
- characters (player, NPCs, monsters, fauna, residents), held weapons and doors;
- spells, explosions and melee;
- CPU rigid bodies (furniture, felled trunks, fragments, items).

Debris collides with the world as it actually is (sub-voxel detail, edits, streaming,
anywhere in a large world). It always re-settles naturally. It also works in shipped games,
not only the editor.

## Architecture decision (settled with the user, 2026-10-03)

**Keep the CPU/GPU split by role.**
- The CPU `VoxelDynamicsWorld` holds gameplay bodies (furniture, items, trunks, character
  grounding). It is authoritative, queryable in the same frame, and unit-testable.
- The GPU `GpuParticlePhysics` holds **all** debris (cosmetic, high count). It is **mandatory**,
  with no CPU debris fallback (Phase 0).

The connection is **one-way inputs into the GPU** (uploads, no readback). Readback is limited to
a few small aggregates, later (Phase 6).

**Rejected: everything on the GPU.** It loses same-frame queries (about 2 frames of latency),
server authority (vendor-dependent, non-deterministic floats), compound bodies (a 4+ phase
project) and headless tests.

---

## §Inventory (2026-10-04, four parallel code audits; file:line in the audit transcripts)

### A. Debris systems: there are FOUR, plus VFX
| System | Who uses it | Collision | Notes |
|---|---|---|---|
| **GPU `GpuParticlePhysics`** | spells, `apply_damage`/`cast_spell destroy`, axe-chop splinters, collapse scatter, small furniture shards, X-key derez, `spawn_gpu_*` | world occupancy grid + other debris + **the player only** | the live gameplay path |
| CPU `VoxelDynamicsWorld` single-box bodies (`DynamicObjectManager` global dynamic cubes) | **editor-only**: the B key, Python `break_hovered_*`, `spawn_bullet_cube`/`spawn_voxel_body` | full CPU world | plain left-click no longer breaks voxels; break impulses are always zero |
| CPU `DebrisSystem` (Verlet points) | only the derez fallback when the GPU failed to initialize | point test vs. `hasVoxelAt`, ignores size, no rotation | small bugs (static ring index, instance count over cap) |
| CPU coherent fragments (`CoherentFragmentService`) | tree felling, collapse, large furniture pieces | full CPU world | real rigid chunks; correctly CPU |
| `VfxSystem` bursts | chop chips, spell sparks | none | cosmetic, out of scope |

- The **"FPS-based CPU/GPU routing"** in `DynamicVoxelPhysics.md` does not exist; only unused
  fields remain (`ChunkManager.h:129-135`).
- **No GPU fallback:** without a GPU, `DamageSystem` removes voxels but spawns *nothing*, and
  small furniture shards are silently dropped.
- The only consumers of CPU debris bodies are `try_push`, three tests and
  `tools/perf_stress_test.py`.

### B. Runtimes: GPU debris is EDITOR-ONLY
`GpuParticlePhysics` is created only by `editor/src/Application.cpp`. None of these has GPU
debris or a voxel `DamageSystem`:
- `EngineRuntime`, `GameShell` and `GameApiService` (the shipped-game host);
- the `create_project.py` scaffold;
- `examples/minimal_game`;
- every test.

**Spell destruction, furniture shatter and derez debris do not exist in a shipped game.**

### C. The static collision grid (occupancy) is unreliable

> **There are THREE occupancy representations:**
> - the CPU `VoxelOccupancyGrid` (per chunk, three-level, the physics truth);
> - the GPU `VoxelLightOccupancy` (micro-resolution, chunk directory, recentring, built from
>   the CPU grid, unit-tested);
> - the GPU debris bitfield (described below).
>
> Rev 4 deletes the third and points debris at the second.
It is correct only for single-voxel edits through `ChunkManager`, `DamageSystem`, the editor's
fill/clear/generate commands, game-definition loads, scene transitions and settlement units.
- **Never updated** (ghosts or holes):
  - `spawn_template`, plus every `PlacedObjectManager` place, move, rotate and remove;
  - furniture activate and re-bake;
  - door registration;
  - `build_structure` (marks **one voxel per chunk**);
  - the removal half of `move_region`, `undo`/`redo` and `restore_snapshot`;
  - `StructureForge` sub-voxel clears;
  - latent: `removeVoxel` and a `VoxelForceApplicator` early return.
- **Never cleared:** chunk unload and evict, live WorldForge re-stream, project open with an
  empty DB, asset/anim/interaction editor scenes, `resetEditorScene`.
- **Sub-voxel cells:** the two ways the grid gets filled disagree. A full rebuild (`hasVoxelAt`)
  marks a sub-voxel-only cell **solid** (debris floats up to about 0.9 m high). Streaming and DB
  sync (`store.solid`) mark it **empty** (debris falls through sub-voxel walls, fences and trees).
  `removeSubcube` clears the whole cell (a hole); breaking the last subcube leaves it solid
  (a ghost).
- **Fixed window:** x and z in [−256, 255], y in [−64, 191] around the origin, never recentred.
  Outside it, `setOccupied` drops writes and the shader reads empty, so debris falls forever.

### D. Movers and forces
- **The player is the only mover that touches both worlds.** GPU colliders are fed for whatever
  `animatedCharacter` points at, and `createAnimatedCharacter` reassigns it, so the feed silently
  switches to the newest character. The boxes are one frame stale (fed before NPC updates).
- **NPCs, monsters, fauna, residents and spawned entities** are all `AnimatedVoxelCharacter`.
  They push CPU bodies (`setKinematicObstacles`, the only caller is `updateSegmentBoxes`), but
  **never GPU debris**. *(Fixed in 3a, 2026-10-06.)*
  - Up to 12 segment boxes per character, axis-aligned refits of rotated limbs (inflated up to
    about 1.4×), one whole-body velocity (no per-limb velocity), zero velocity while sitting or
    anchored.
  - Update-LOD skips ticks at 30, 60, 120 and 220 m, so obstacles keep a stale pose and
    velocity for up to 0.5 s.
  - Derez leaves a stale obstacle behind.
- **CPU bodies:** furniture (including grabbed and thrown), fragments, felled trees, item props
  and legacy cubes. They collide with each other and with characters, but **GPU debris passes
  through them**. *(Fixed one-way in 3c, 2026-10-06; debris → body coupling is Phase 4.)*
- **Touch NEITHER world:** doors and `KinematicAnimator` parts (`syncCollidersToPhysics()` is an
  empty stub), held weapons and items *(these three fixed in 3b, 2026-10-06)*, all VFX, CombatSystem melee and knockback, wind, triggers,
  the never-wired `RangedCasterBehavior` cast hook.
- **Force sources never push EXISTING bodies in either world:** `applyDamage`, chop, spells and
  melee only create new bodies or debris. Water pushes CPU bodies but not debris. There is no
  projectile physics.

### E. Shaders and duplicated layouts (must change together)
- **Live path:** sync_in → integrate → grid/scan/sort → narrowphase → voxel → CSR → colour →
  dual/primal → hardcontact → sync_out → warmstart_save → expand. Debris is drawn by
  `dynamic_voxel.vert` and `dynamic_shadow.vert` from the 64-byte face buffer.
- **Dead:**
  - the legacy XPBD pipelines `particle_integrate`, `particle_collide` and `particle_sort_scan`
    (created but never dispatched);
  - `particle_collide` also holds a **second copy of the occupancy constants and of the old
    character layout**;
  - the orphans `solver_jacobi` and `solver_graph_color` (still compiled) and `solver_apply`
    (no rule).
- **⚠ `build_shaders.bat`:** every `solver_*` and scan rule uses the broken
  `if %errorlevel%` check inside the `if defined USE_GLSLC (...)` block, so **a failing solver
  shader may not stop the build** and a stale `.spv` ships. (Other rules use
  `|| goto :shader_error`.) Also: a missing `frustum_cull.comp` is referenced, and
  `post_process.frag` has no error check.
- **Change-together sets:**
  1. Occupancy constants: `GpuParticlePhysics.h:257`, `voxel_contact.glsl:27`, dead
     `particle_collide.comp:74`.
  2. `GpuParticle`: `GpuParticlePhysics.h:40`, `particle_types.glsl:17`, plus the analyzer
     mirror `DebrisSettleAnalyzer.h:31` and flag literals.
  3. `SolverBody`, `GPUConstraint` and `WarmstartEntry`: GLSL only, with sizes hard-coded in C++.
  4. `CharacterCollider`: C++ plus a literal `charSeg[12]` in `solver_integrate.comp:26`.
  5. The 64-byte face stride (Types.h, `particle_expand`, `dynamic_voxel`, `dynamic_shadow`,
     ShadowMap's hard-coded 64).
  6. **Push-constant structs declared twice in C++** (`GpuParticlePhysics.cpp:644` and `:810`).
  7. `MAX_COLORS`, `HASH_*`, `WAKE_WORDS`, `GRID_SIZE`, `SCAN_BLOCK`, `SOLVER_FLAG_*`, `ALPHA`.

---

## §Gate (design check items → where they are resolved)
| Gate item | Resolution |
|---|---|
| Fixed window plus unreliable occupancy (behaviour coupled to edit path and residency) | **Phase 1c**: debris adopts `VoxelLightOccupancy` (micro-resolution, recentring, existing equality test `ChunkedOccupancyEqualsTheWholeRegionAcrossEverySeam`); the debris bitfield is deleted; the choke point is `VoxelOccupancyGrid` write-path coverage |
| D1 before the occupancy switch is a regression (2nd check) | **D1 moved to 1d**, after 1c |
| GPU debris shows the parent texture's centre slice (2nd check, aesthetic) | **1d texture parity**: 10-bit position inside the parent |
| No deterministic B-key or GPU-failure test hooks (2nd check) | **1d `break_voxel`**, **Phase 0 `PHYXEL_DISABLE_GPU_DEBRIS`** |
| Bench not bit-deterministic, so "unchanged" was undefined (2nd check) | **Phase 0 regression band** |
| Limb colliders were inflated AABBs | **Phase 3a**: rotated boxes (OBBs) with per-limb velocity |
| API units, clamps, echo, overflow | **§API** |
| Analyzer counts legitimate pushes as injection | **Phase 1d**: external-input window |

---

## Phase 0 — DEMOLITION, part A (safe now, no behaviour change)

**Why first:** every later phase edits the code this phase deletes.
- The duplicated layouts drop to one live copy each.
- "Which debris system handles this?" stops having four answers.
- The movers and impulse work only has to target one debris world.

Simplification is the point; this phase adds no features.

**End state after Phase 0 + D1 (Phase 1d):** exactly **two physics worlds** with clear jobs.
- **CPU `VoxelDynamicsWorld`**: gameplay bodies (furniture, items, coherent fragments and felled
  trees, character grounding).
- **GPU `GpuParticlePhysics`**: **all** debris. One spawn path, one renderer, one collision model,
  and, after Phase 1c, **one occupancy shared by CPU physics, lighting and debris**.

**Decision: GPU debris is mandatory; there is no CPU debris fallback.**
- If `GpuParticlePhysics::initialize` fails, that is a bug: log an ERROR once, disable debris,
  and count every refused spawn.
- Today's "fallback" mostly fails silently anyway: `DamageSystem` spawns nothing without a GPU.

| # | Delete (Phase 0, in this order) | Lines (approx) | What replaces it / migration |
|---|---|---|---|
| D4 | **Legacy GPU XPBD pipeline**: `particle_integrate/collide/sort_scan.comp`, the `m_useNewPipeline` branch in `recordComputeCommands`, its pipelines and buffers. Orphans: `solver_jacobi`, `solver_graph_color`, `solver_apply`. Legacy `CharacterCollider` fields and the `CHAR_PUSH_GRACE` flag byte | ~350 + 6 shaders | none (never dispatched). Frees GpuParticle flag bits 24–31 |
| D5 | **Dead entry points**: `breakCubeAtPosition` (two copies), `removeVoxel`/`removeHoveredCube`, `breakHoveredCubeWithForce`, the G-key "spawn dynamic subcube" stub, the FPS-routing leftovers in `ChunkManager.h` (`m_frameBreakCount`, `MAX_BULLET_BREAKS_PER_FRAME`, `GPU_FALLBACK_FPS_THRESHOLD`, `m_smoothedFps`, `updateSmoothedFps`, `resetFrameBreakCounter`) | ~150 | none |
| D3 | **`ForceSystem` + `VoxelForceApplicator`**, its ImGui debug panel (`renderForceSystemDebug`) and its toggle key, plus `Cube::Bond` if nothing else reads it (check at deletion) | ~450 | none (its break functions have no callers) |
| D2 | **CPU `DebrisSystem`**, `DebrisRenderPipeline`, `debris.vert/frag`, its render call in `RenderCoordinator`, `tests/core/DebrisSystemTest.cpp` | ~560 + 2 shaders | none. Derez uses the GPU only; with no GPU it logs and skips, like all other debris |
| D6 | **Debug endpoints for removed systems**: `try_push` (no external users), and the always-zero "bullet" fields in `dynamic_stats`, `engine_timing` and `clear_dynamics` — replaced by a REAL `cpu_dynamic` count (`DynamicObjectManager::getDynamicObjectCount`). **Revised at deletion (2026-10-04): `spawn_voxel_body` / `clear_voxel_bodies` STAY** — `VoxelDynamicsWorld` is live (furniture, coherent fragments) and they are its only headless benchmark harness (`perf_stress_test.py --mode voxel`; used for the `DestructionSystemV2.md` grid-count measurement) | ~200 | **`tools/perf_stress_test.py` is updated in the same commit** (`bullet` mode → `cpu`, reads `cpu_dynamic`, `--url`). `try_push` returns in Phase 4 as a cone impulse. `spawn_bullet_cube` goes with D1 |

**Deferred out of Phase 0 (each needs a prerequisite first):**
- **D1** (CPU single-box debris → GPU, B-key/Python breaks) becomes **Phase 1d**, *after* Phase 1c.
  Doing it first would move B-key debris from the CPU's sub-voxel-exact, unbounded
  `VoxelOccupancyGrid` onto the GPU's cube-resolution ±256 bitfield, which is a regression
  (design check 2026-10-04).
- **D7** (the player shove hack) goes in **Phase 2**, when real kinematic contacts replace it.

**Delete in small commits, in table order.** D4/D5 have zero behaviour impact. After each
commit: build, unit suite, `shader_manifest --check`, and the settle bench.

**Phase 0 is done when:**
1. Grep finds **zero** references to the deleted symbols, shaders and endpoints (outside git
   history and this plan).
2. A full build works, the unit suite passes at the pre-demolition count minus the deleted tests,
   and the shader manifest is OK.
3. **The settle bench stays inside the regression band** (written down because the solver is not
   bit-deterministic):
   - `drop_layer`, `packed`, `crater` and `crater_subcube` still SETTLE;
   - `drop_pile` has ≤ 20 forced sleeps and `blast` ≤ 6;
   - after the impact window: 0 rebounds, 0 injected energy, 0 hard-contact pushes, 0 tunnelled.
4. **New `gpu_init_failure_is_loud`**: launched with `PHYXEL_DISABLE_GPU_DEBRIS=1`, then
   `apply_damage`.
   - Pass: exactly one ERROR log, `gpu_physics` status echoes `disabled_reason`, and refused
     spawns are counted.
   - Today: silence.
   - Control: a normal launch.
   - **DONE 2026-10-04** — `tools/gpu_debris_disabled_check.py`: disabled run 8/8 PASS (one ERROR
     line, `disabled_reason` echoed, `debris_refused` 108 = refused-counter delta, 0 GPU bodies),
     control 6/6 PASS. Exercising it found a crash: the no-GPU derez fallback erased the character
     without unregistering it from the entity registry (fixed — one shared removal helper).
5. **Live (L4):** X-key derez, spell blast and `apply_damage` debris unchanged, watched by the
   user.

**Phase 0 RESULT (2026-10-04, all five criteria met; main `ed924498`).**
- Commits: plan, D4, D5, D3, D2, D6, the ghost-body fix, `gpu_init_failure_is_loud`.
- (1) zero live references (only "deleted" comments and historical docs); (2) full unit suite
  4140 run / 4115 pass / 20 skipped / 5 fail — all 5 pre-existing (AtlasManager, FineFaceMerge,
  3× GpuTimingHistory), `WaterOccupancyTest.StoredSpans…` still hangs (unrelated); manifest OK;
  (3) bench in band after every commit (drop_pile 6–17 forced, blast 0–3); (4) above; (5) user
  watched derez / fireball / `apply_damage` live.
- **Found while verifying (fixed):** every CPU dynamic-object drop path except "isDead" leaked
  its `VoxelRigidBody` (invisible colliders, incl. every scene transition) —
  `DynamicObjectBodyReleaseTest`; the no-GPU derez fallback crashed the engine.
- **Found, NOT fixed (open):** `MAX_DYNAMIC_OBJECTS` is an unenforced render budget (→ 1d);
  derez GPU debris renders pink/magenta (→ 1d texture parity); spells cannot hit or push debris
  (→ Phase 4, see the user report there).

## Phase 1 — Foundations

**1a. Shader build safety.**
- Convert every `solver_*` and scan rule in `build_shaders.bat` to `|| goto :shader_error`.
- Remove the `frustum_cull.comp` reference and add the missing `post_process` check.
- **Red test:** a syntax error injected into a copy of `solver_voxel.comp` must make the script
  exit non-zero.
- **DONE 2026-10-04** — `tests/test_build_shaders_fails_loud.py` (9 cases: a broken solver /
  scan / post_process shader × compiler found via VULKAN_SDK or via PATH, the two controls, and
  "no compiler"). Against the OLD script 7/9 FAIL (every broken shader exited 0; no-compiler
  started compiling); NEW 9/9 PASS; the real repo build exits 0 with no .spv change.
  - All 34 `if %errorlevel%` checks became `|| goto :shader_error`; `post_process.frag` got a
    check (it had none); the `frustum_cull.comp` rule went (no such file).
  - **Found and deleted: the glslc-only build mode** (~120 lines). It could never complete a
    build — every rule after its block used glslangValidator's `-V` — so it was a dead,
    duplicate shader list. The script now has ONE compiler; `where` detection no longer reads
    a parse-time `%errorlevel%`.
  - Footgun found: this machine sets `NoDefaultCurrentDirectoryInExePath=1`, so
    `cmd /c build_shaders.bat` is "not recognized" — call it by absolute path.

**1b. One source for shared constants and layouts.**
- After D4 there is one live copy of each layout. Move the constants into
  `shaders/solver_shared.h`, usable by both C++ and GLSL (macros):
  - `MAX_PARTICLES`, `MAX_COLORS`, `HASH_*`, `WAKE_WORDS`;
  - the kinematic and impulse caps, the flag bits, the particle flag bits;
  - the push-constant layouts.
- Collapse the duplicated push-constant structs in `GpuParticlePhysics.cpp` to one set.
- static_asserts on every C++ size.
- **DONE 2026-10-04** — `shaders/solver_shared.h` (valid C++ and GLSL): capacities, grid/scan
  sizes, solver-state layout + `SS_*` header slots, particle flag bits, `SOLVER_FLAG_*`,
  `SOLVER_ALPHA`, `PRIMAL_STORE_VELOCITY`, the occupancy window, the workgroup size, and all
  15 push-constant layouts as FIELD macros (GLSL `uniform PC { PHX_PC_X } pc;`, C++
  `struct XPC { PHX_PC_X };`). 14 size static_asserts + 8 invariant asserts (hash pow2/load,
  wake bits ≥ bodies, grid pow2, scan coverage, default flags).
  - Removed: THREE copies of the C++ PC structs (two had drifted — `pad` vs `flags`), the C++
    mirrors of every shared constant, `GRID_SIZE` ×3 in shaders, literal `256`/`12` in shaders
    and `(n + 255u) / 256u` / `hdr[4]` / `1u /*ACTIVE*/` literals in C++.
  - **Proof of no GPU change:** all 21 rebuilt compute `.spv` differ from HEAD only by unused
    `OpConstant`s (+ `OpSourceExtension`/an unused `float` type in the 4 shaders that gained the
    include) — zero instructions removed or changed (anonymised, sorted `spirv-dis` diff).
    Settle bench (clean lab, tag phase1b) in band: drop_pile 10/173 forced, blast 1/64, all
    post-window checks pass.
  - **Found: the DebrisLab floor was damaged** (39 holes under drop_layer from my own
    `apply_damage`/spell tests + 144 cells in chunk 4 from earlier sessions) and the bench's
    one-column `verify_lab` never saw it — drop_layer "tunnelled" 10–12 bodies through real
    holes. `verify_lab` now scans every non-blast chunk's whole slab top and refuses to run;
    `gpu_debris_disabled_check.py` now blasts the self-restoring blast chunk; lab rebuilt.
  - **Found (→ 1d):** `GpuParticlePhysics::SpawnParams::typeFlags` is never set by any caller,
    so every debris piece carries `PARTICLE_TYPE_CUBE` regardless of its scale.

**1c. ONE occupancy: debris adopts the lighting occupancy** (redesigned 2026-10-04).
- **It already exists.** `VoxelLightOccupancy` (`engine/include/graphics/VoxelLightOccupancy.h`,
  `docs/UnifiedLightingPlan.md`) is a GPU, **micro-resolution**, chunk-directory occupancy that
  **recentres on the camera**.
  - It is sourced from `Physics::VoxelOccupancyGrid`, the same per-chunk, three-level truth CPU
    physics uses, so CPU and GPU cannot drift apart.
  - Shaders query it through `occupancy.glsl` `phxOccupancySolid` (bindings 11/12); the C++
    mirror `packedPoolSolidAt` is unit-tested.
  - It already has the gate's equality test:
    `VoxelLightOccupancyTest.ChunkedOccupancyEqualsTheWholeRegionAcrossEverySeam`. It also has
    `RecentringTheBoxCoversGeometryFarFromTheWorldOrigin`,
    `DirectoryIndexingHandlesNegativeChunkOrigins` and sub-voxel exactness tests.
  - Its header explains why it rejected the debris bitfield: cube-only, and two fill paths that
    disagree.
- **Do:**
  - Bind the occupancy directory and pool buffers plus the `occBox` into the debris compute
    passes (`solver_voxel`, `solver_hardcontact`).
  - Rewrite `voxel_contact.glsl` on top of `phxOccupancySolid`:
    - **Cube level first:** `solid` and `mixed` bits give the existing 26-neighbourhood escape at
      unit resolution.
    - **Micro level in mixed cubes:** a sample in a mixed cube resolves against the 729-bit mask
      (escape search over micro cells, bounded to its cube and its neighbours).
    - Debris rests on 1/3 slabs and fences and falls through real gaps.
  - **Outside the covered box:** "not solid" is correct for light but wrong for debris (it would
    fall forever). Debris whose samples leave the box is **frozen and counted**
    (`frozen_out_of_box`). Spawns outside the box are refused and counted.
  - **Upload gating: verified 2026-10-04, not an issue.** `updateLightOccupancy()` runs every
    frame unconditionally from the render path (`RenderCoordinator.cpp:3343`), before meshing.
    Lighting toggles only gate the *traces* (`occBox.w` bits 1–2). It is absent only if
    `VoxelLightOccupancyGpu` fails to initialize.
  - **(3rd check) Three-state query.** `phxOccupancySolid` returns false outside the box, for
    not-yet-flattened chunks and for chunks dropped on pool overflow. "No occlusion" is correct
    for light; for debris it means falling through.
    - Add a sibling `phxOccupancyState` (solid / empty / **unknown**) to `occupancy.glsl`.
      Unknown = the directory slot is `PHX_OCC_NO_CHUNK` or the cell is out of the box.
    - Its C++ mirror is unit-tested like `packedPoolSolidAt`.
    - Debris with an unknown sample is **frozen and counted** (`frozen_unknown_occupancy`).
    - The lighting function is unchanged.
    - **DONE 2026-10-05 (1c step 1):** `phxOccupancyState` + C++ mirror `packedPoolOccupancyState`
      (`OccupancyStateIsSolidOrEmptyWhereKnownAndUnknownElsewhere`: known cells == `packedPoolSolidAt`
      across a mixed cube and a negative origin; unknown for a non-resident in-box chunk, all six
      out-of-box directions and an unpacked pool). The binding slots are now overridable macros
      (`PHX_OCC_SET/BINDING_DIR/BINDING_POOL`, default 0/11/12) — needed because the debris passes
      have their own descriptor layout (`ComputePipeline` numbers bindings 0..N−1). Every lighting
      `.spv` rebuilt byte-identical; LightingPipeline.md §9 logged.
  - **DONE 2026-10-05 (1c step 3): debris reads the shared pool.**
    - `voxel_contact.glsl` rewritten on `phxOccupancyState`/`phxCubeOccupancy`/`phxOccupancySolid`;
      CPU mirror `engine/include/core/DebrisContact.h`, `DebrisContactTest` (10 cases: full-cube
      floor, 1/3 slab, 2-micro fence, real gap, deep sub-voxel, cube fallback, unknown, negative
      origin, **bit-exact equality with the old cube search on random full-cube worlds**, and a
      control proving the old bitfield gets the sub-voxel cases wrong — sideways out of the slab).
    - **Design fix found while writing expected values:** a radius-1 micro search cannot see out
      of a body sunk 1.8 micro into a slab and fell back to a SIDEWAYS cube escape. Inside solid,
      each of the 26 directions now walks up to 4 micro cells (`kMicroEscapeSteps`).
    - `ComputePipeline` gained per-frame-slot descriptor sets; `solver_voxel`/`solver_hardcontact`
      bind the pool slot of the frame being recorded; `occBox` rides in their push constants.
    - Unknown occupancy → the body is HELD for the tick (pose pinned, invMass/invInertia 0) and
      counted (`SS_FROZEN_UNKNOWN` → analyzer `held_unknown_occupancy`, must be 0 in the lab).
    - **Bug found live:** the hard-contact "fully embedded" rescue judged the CUBE at the body's
      centre, so debris resting on a 1/3 slab (centre inside the mixed cell) was lifted a whole
      cube up (centres at 17.01). Now judged/lifted at micro resolution (identical on full cubes).
    - **Bug found live (pre-existing):** `/api/debug/particle_log` opened/closed the position-log
      stream on the HTTP thread while the main thread wrote it every frame → it hung the main
      thread. Now queued to the main thread; the auto-start on first spawn (an unasked-for
      per-frame CSV in the working directory during ordinary play) is gone.
    - Evidence: `tools/debris_subvoxel_rest_check.py` (prediction written first) — over the slab,
      median centre 16.5000 (pred 16.500), min ≥ 16.4990, 3/3 runs; control 16.1666 (pred 16.167).
      Settle bench as a fresh engine's first run: drop_layer hc max **11.2 mm = Phase 0 exactly**,
      drop_pile 8/167, blast 3/70, every post-window check and `held_unknown_occupancy` pass.
    - **Open (noted, not fixed):** bench numbers depend on session history — a second run in the
      same engine session gives different hard-contact depths (11.2 → 30–80 mm). Solver state
      survives `clear_dynamics` (likely the warm-start hash, initialised once). Bench gates are
      judged on a fresh engine's first run until this is fixed.
  - **DONE 2026-10-05 (1c step 4): the old debris cube bitfield is deleted** — buffer, `OCC_*`,
    `setOccupied`/`clearOccupancy`, `ChunkManager::rebuildOccupancyFromChunks` and its 17 call
    sites, the four `Application` region/chunk bit-clear loops, `ChunkManager::m_gpuParticles`.
    `updateOccupancyVoxel`/`syncChunkToOccupancy` survive for their WATER half only. Debris static
    collision is now ONLY the shared pool, so `VoxelOccupancyGrid` writer coverage (step 5) is
    what keeps debris right. 261 related unit tests pass; sub-voxel rest check PASS; bench in band
    on the last run (blast 2/67, drop_pile 11/162, all post-window checks, held 0).
  - **OPEN — must be explained before 1c is called done: blast shifted after step 3's first
    run.** Hard-contact max depth in `blast` was 88.1 mm in every Phase 0 run and in step 3's
    first bench, and is a constant 118.8 mm in every fresh run since; blast forced sleeps
    average ~4 (two runs at 7, band ≤ 6) against ~1.5 in Phase 0. Ruled out by A/B: the
    micro-level embedded lift (cube-level variant → 118.8), the position-log change (log on →
    118.8), accumulated blast-chunk damage (chunk wiped and rebuilt → 118.8). Not the contact
    geometry: full-cube contacts are bit-identical to the old search (`DebrisContactTest`) and
    no body is held. The same code gave 88.1 then and 118.8 now, so the difference is in state
    the bench does not control — next step: diff the blast scenario's spawn set (debris count,
    positions) between an 88.1 and a 118.8 run, and test the session-state leak (warm-start hash
    surviving `clear_dynamics`) noted under step 3.
  - **1c step 5 — writer inventory (2026-10-05, IN PROGRESS).** A read-only audit of every
    static-voxel writer against `VoxelOccupancyGrid` (the single full rebuild is
    `ChunkPhysicsManager::buildInitialCollisionShapes`; incremental `add/removeCollisionEntity`;
    remeshing does NOT rebuild the grid; `addCube`/`removeCube`/`removeCubesBatch`/`addSubcube`/
    `addMicrocube` SKIP the grid while the chunk is in physics "bulk mode"). Gaps, by impact:
    1. **Streamed chunks stay in bulk mode forever** — VERIFIED in code: the async worker builds
       chunks with `initializeForLoading()` (sets bulk), and neither the drain, the finalize
       lambda (`registerPrebuiltPhysics`, early return for air) nor `Chunk::initialize` clears
       it. So in every streamed chunk (and the deferred part of a DB boot) player place/break,
       `DamageSystem` cube breaks, `clear_region`/undo/redo/snapshot/`move_region` cube writes and
       `addSubcube`/`addMicrocube` leave the grid stale — wrong for debris, lighting AND CPU
       character collision. **FIXED 2026-10-05:** a full rebuild (`buildInitialCollisionShapes`)
       now ends bulk mode (the grid is authoritative from the store, which is what bulk mode
       defers to), and both finalize paths clear it before the air-chunk early return.
       Red→green: `OccupancyCoverage.EditsToAStreamedChunkReachTheGrid` (break/place cube,
       subcube, microcube in a worker-built chunk) failed every assertion before, passes after;
       control `ControlEditsToAnOrdinaryChunkReachTheGrid` passes both ways.
    2. Template spawn (`ObjectTemplateManager::spawnTemplate` / `spawnOrEraseMicro` add) adds in
       bulk mode, then rebuilds only if a stale `collisionNeedsUpdate` happens to be set — so
       placed objects, flora, StructureForge fixtures, placed-object move/rotate are missing from
       the grid (only `/api spawn_template` heals itself). **FIXED 2026-10-05:** entering bulk
       mode marks a rebuild OWED (`setInBulkOperation(true)` sets `collisionNeedsUpdate`), a full
       rebuild settles it. Red→green: `TemplateSpawnsReachTheGrid` (cube/subcube/micro via
       `spawnTemplate` and `spawnTemplateMicro`) failed all 5 assertions before.
    3. `DynamicFurnitureManager::deactivate` rebake: goes through `placeTemplate`/
       `spawnTemplate` — CLOSED by the fix for 2 (same code path, same test).
    4. `fillAllCubes`/`fillAllVoxels` never touch the grid: `/api generate_world` (sync + job)
       deep-uniform chunks are invisible to debris/lighting. **FIXED 2026-10-05:** outside bulk
       mode the uniform fill also fills the grid (`VoxelOccupancyGrid::fillSolid`); in bulk mode
       the owed rebuild covers it. Red→green: `UniformFillReachesTheGrid`.
    5. Main-thread flora fallback writes after the grid is built (only without a worker
       decorator). CLOSED by the fix for 1: the worker's rebuild now ends bulk mode, so the
       fallback's adds reach the grid incrementally (the lifecycle the GAP-1 test pins).
    6. Async fill/clear/generate jobs mutate grids off the main thread under the chunk WRITE
       lock, and NOTHING on the main thread takes the read lock. CONFIRMED. Fixed for the
       occupancy repack only: `updateLightOccupancy` try-locks shared and skips the frame while a
       job holds the write lock (revisions repack everything afterwards); counted as
       `occupancy_job_skips` on `/api/debug/light_occupancy`. No deterministic red test (a
       race); live L4 instead — a 11,760-voxel `fill_region` JOB on DebrisLab: 8 frames
       skipped, then 5/5 sampled cells 729/729 CPU = GPU, the clear 0/0. **Still open, outside
       1c:** meshing and CPU physics read the same grids/stores unlocked during these jobs.
    7. Light-pack cache keyed by (origin, revision): every grid counted from 0, so an evicted
       and re-created chunk could match the cached revision with different contents. CONFIRMED
       and FIXED: revisions come from one atomic source across all grids. Red→green:
       `RevisionsDoNotRepeatAcrossGridObjects` ("two different grid states share revision 1").
    8. Minor, OPEN: incremental add does not filter broken/invisible sub-voxels like the full
       rebuild; `spawn_template` heal region ignores rotation.
    Live L4 for 1 (2026-10-05, DebrisLab): break (170,15,16) via `/api/world/voxel/remove` →
    the probe read 729/729 before, 0/0 after, agrees. Settle bench, fresh engine first run
    (`docs/evidence/debris_settle/phase1c5`): drop_layer/packed/crater/crater_subcube settle,
    drop_pile 15 forced (≤ 20), blast 0 forced (≤ 6); post-window 0 rebounds / 0 injected /
    0 hc>1s / 0 tunnelled / 0 held_unknown everywhere. Blast hc max still 118.8 mm — the open
    shift is unchanged by step 5, so it is not a grid-coverage effect.
  - **1c step 6 — equality test + live instrument (2026-10-05, DONE).**
    - `DebrisContactOccupancyTest` (3 cases): a 2×2×2-chunk world around the origin (every
      axis crosses 0 and a chunk seam; one chunk ABSENT) with full cubes, 1/3 slabs, 2-micro
      fences and lattice cubes. Cell states and every micro of every mixed cube match through
      the pool and through the grids; 6,000 random points × 2 margins give the SAME contact
      (hit, unknown, micro, dirIdx, pen, normal — exact). The oracle answers from the grid's own
      physics query (`queryAABB`), never from the packing code. Control: a pool packed from a
      different world disagrees. Green on first run — an equality pin, not a bug fix; the
      control is what shows it can fail.
    - `POST /api/debug/occupancy_diff {x1..z2}` (world cubes, ≤ 64³; refuses larger with the
      count): store vs physics grid vs packed pool at micro resolution — `cell_mismatches`,
      `subcube_mismatches`, `grid_mismatches`, `cells_pool_unknown`, first 20 mismatches with
      the micro count of each copy. `agrees` requires 0 unknown (a loaded chunk not yet in the
      pool is not agreement — seen once at boot, gone within seconds).
    - The bench now asserts it per scenario over the scenario's chunk (column `occ diff`;
      non-zero → `FAILS(occupancy)`). `phase1c6`: 0 in all six scenarios, blast included
      (judged after the blast, before the site restore).
    - `phase1c6` otherwise in band EXCEPT drop_pile `hc>1s` = 1 (first non-zero across
      phase1b..1c5). Step 6 changed no solver or grid-writing code, so it was re-run as the
      FIRST scenario on two fresh engines: `phase1c6-droppile-fresh1/2` = 0 pushes, 0 rebounds,
      10 / 7 forced, occ 0. In the full bench drop_pile runs SECOND (after drop_layer): this is
      the open session-state dependence, now with a reproducible-ish trigger to chase.
  - **CLOSED 2026-10-05 — session-state dependence AND the blast shift: one root cause, two
    leaks in `GpuParticlePhysics`.**
    - Red (prediction written first: "the same scenario repeated in one session gives different
      numbers"): drop_layer ×3 in one fresh session → hard-contact max 11.2 / 81.4 / 47.1 mm
      (`slotorder-red`).
    - Leak 1, slot order: the free list was a stack initialised `N-1…0`; `despawnAll` pushed the
      freed slots back ASCENDING, so every later scenario got its bodies in REVERSED slots, and
      the solver's colouring/processing order follows slots. Now a min-heap: a spawn always
      takes the lowest free slot (occupancy, not history). Alone: 11.2 / 29.6 / 40.8
      (`slotorder-green`) — real but not all of it.
    - Leak 2, warm-start state: the warm-start hash table and the wake bits were cleared ONCE per
      engine (`m_hashInitialized`). New bodies inherited stale keys/lambdas and the
      open-addressed table filled across a session. Now re-cleared whenever the pool empties
      (`despawnAll`, or the last body expiring). Both: 11.2 / 11.2 / 11.2, identical t_all and
      forced (`slotorder-green2`).
    - Whole bench, warm session (9 scenarios already run) vs fresh engine
      (`sessionfix-warm` / `sessionfix-fresh`): drop_layer, packed, crater, crater_subcube
      bit-identical; blast 86.179 vs 86.178 mm, same forced (3) and t_all. drop_pile still
      varies (28.9 vs 26.2 mm, 9 vs 10 forced) — it varied between two FRESH first runs too
      (10 vs 7, above), so that is GPU nondeterminism in the 150-body pile (parallel constraint
      build), not session state. Bench gates no longer need a fresh engine.
    - **Blast explained:** leak-free blast is **86.2 mm**. Phase 0's constant 88.1 and step 3's
      constant 118.8 were both measured on leaked state (blast runs SIXTH, after five scenarios'
      stale warm-start keys); step 3 changed the contacts, so the same leak produced a different
      constant. Not a contact-geometry regression. 100 GPU-particle/debris unit tests pass.
  - Test-world footgun hit twice this session: `restore_blast_site` only refilled y 8..15, so a
    test structure on the blast chunk's SURFACE survived into later runs. It now clears above the
    slab too; `debris_subvoxel_rest_check.py` removes its slab when done.
  - **1c is split into small commits:** (1) tri-state query ✅ → (2) edit-first repack priority ✅ +
    `occupancy_edit_backlog` → (3) debris passes read the pool ✅ (per-frame-slot descriptor sets in
    `ComputePipeline`, `voxel_contact.glsl` rewritten, unknown/out-of-box frozen and counted) →
    (4) delete the debris bitfield ✅ → (5) `VoxelOccupancyGrid` writer audit + `OccupancyCoverageTest`
    → (6) `DebrisContactOccupancyTest` + `occupancy_diff`.
  - **(3rd check) Edits before residency.**
    - `updateLightOccupancy` repacks 24 chunks per frame in `chunkMap` order, with no priority.
      Under streaming load a just-blasted chunk can wait frames, so its debris spawns in cells
      the GPU still thinks are solid and gets shoved out.
    - Repack chunks **already in the pool whose revision changed** before first-time residency.
    - Expose the backlog as `occupancy_edit_backlog`. Cost-only change; lighting benefits too.
    - **DONE 2026-10-05 (1c step 2):** pure `chooseRepackOrder` (edits first, then residency,
      order kept, budget ≤ 0 packs nothing) — `RepackOrderTakesEditsBeforeFirstTimeResidency`;
      `RenderCoordinator::updateLightOccupancy` collects changed chunks then applies the plan;
      `GET /api/debug/light_occupancy` reports `occupancy_edit_backlog` /
      `occupancy_residency_backlog`. Live: blasted (176,15,16) → pool 0/729 = CPU 0/729 on the
      next query, intact neighbour 729/729, both backlogs 0.
  - **(3rd check) Per-frame-slot descriptors.**
    - The occupancy buffers exist once per frame in flight (`VoxelLightOccupancyGpu::kSlots`
      = 2), but debris compute pipelines bind buffers once at creation.
    - `solver_voxel` and `solver_hardcontact` get **one descriptor set per frame slot**, selected
      by the `frameIndex` that `recordComputeCommands` already receives.
    - Otherwise debris reads a buffer the CPU is rewriting for the other frame (the
      frames-in-flight bug class, memory `reference_frames_in_flight_buffers`).
- **Then DELETE the debris bitfield entirely:**
  - `m_occupancyBuffer`, the `OCC_*` constants, `setOccupied`/`clearOccupancy`;
  - `ChunkManager::rebuildOccupancyFromChunks`, the GPU half of `syncChunkToOccupancy`,
    `updateOccupancyVoxel`'s GPU write;
  - the ~20 scattered `setOccupied` call sites.

  The result is **one occupancy for CPU physics, lighting and debris.**
- **The choke point moves to `VoxelOccupancyGrid`.** Audit every static-voxel writer against
  *it* (not the deleted bitfield): place/remove at each level, template spawn and placed
  objects, structure build, undo/redo/snapshot, `move_region`, door register, furniture
  activate/rebake, chunk unload/evict, world/scene teardown. Fix the gaps once, and physics,
  lighting and debris are all right.
  - `OccupancyCoverageTest`: each writer applied to a small chunk, then `VoxelOccupancyGrid` ==
    chunk store.
- **Debris-level test:** `DebrisContactOccupancyTest`. `voxelPointContact` computed through the
  packed pool equals the same query through the source `VoxelOccupancyGrid`, for sample points
  across chunk seams, mixed cubes and negative coordinates. This is the debris twin of the
  lighting equality test.
- **Live instrument:** `POST /api/debug/occupancy_diff` compares the packed pool to the chunk
  store over a region. Every bench scenario asserts 0 mismatches.

**1d. D1: all break debris on the GPU** (moved here from Phase 0; needs 1c).
- **Delete CPU single-box debris:**
  - `DynamicObjectManager` (the whole class);
  - the `ChunkManager` global-dynamic wrappers;
  - the body creation in `VoxelManipulationSystem::breakCube/breakSubcube/breakMicrocube` and
    `ChunkVoxelBreaker`;
  - their CPU face-rendering path;
  - the `spawn_bullet_cube` endpoint.

  About 800 lines.
- **Why this is also a correctness fix (found 2026-10-04, Phase 0):** the CPU path's
  `MAX_DYNAMIC_OBJECTS = 300` is a render budget, never an enforced cap. The dynamic face buffer
  holds 1800 faces; beyond that `updateDynamicSubcubeBuffer` truncates the draw, so extra CPU
  debris is invisible yet still collides. (A second ghost — every clear/expiry path leaking its
  `VoxelRigidBody` — was fixed in Phase 0, `DynamicObjectBodyReleaseTest`.) Deleting the path
  retires the budget; until then do not rely on `cpu_dynamic_cap`.
- B-key and Python `break_hovered_*` spawn GPU debris through one `spawnBreakDebris` helper
  shared with `DamageSystem::spawnDebris`.
- **Texture parity** (design check, aesthetic key):
  - Today GPU debris always samples the *centre* slice of its parent voxel's texture
    (`particle_expand.comp:134` hard-codes position (1,1,1); `typeFlags` is never set), so a
    broken subcube visibly changes texture at the moment it breaks.
  - Each piece now carries its position inside the parent cube: a **10-bit microcube index**
    0–728, stored in **bits 16–25 of `GpuParticle::materialIndex`**.
    - There are about 102 materials, so 16 bits is ample (3rd check); no struct-size change.
    - Every reader masks `materialIndex & 0xFFFF`: `particle_expand` (texture),
      `solver_sync_in` and `solver_integrate` (material physics), and the C++ readbacks.
    - These sites join the change-together list.
  - Subcubes map to their 3×3×3 slice and microcubes to their 9×9×9 slice.
  - This also fixes blast and chop debris, which already show the wrong slice.
- **Test hook:** `POST /api/debug/break_voxel {x,y,z, level: cube|subcube|microcube, sub, micro}`
  drives the B-key path without cursor hover, and echoes the pieces spawned.
- `ChunkManagerIntegrationTest.cpp:287-289`'s CPU-subcube assertions are rewritten to assert GPU
  spawns.
- **1d is three commits:** (1) every break → GPU through one helper + test hook · (2) texture
  parity · (3) delete the CPU path.
- **DONE 2026-10-05 — 1d part 1: breaks spawn GPU debris through ONE helper.**
  - `DamageSystem::spawnBreakDebris(gpu, centre, vel, scale, material, angVel)` is the only
    debris spawn: `DamageSystem::spawnDebris` (blast/chop/collapse) and
    `VoxelManipulationSystem::breakCube/breakSubcube/breakMicrocube` (B key, Python
    `break_hovered_*`) all call it; no GPU solver (or not initialised) → refused + counted.
  - The manipulator takes a lazy `setGpuDebrisProvider` (the solver is created after it).
    breakSubcube no longer goes through `Chunk::breakSubcube`/`ChunkVoxelBreaker` (CPU body);
    those, `DynamicObjectManager` and its render path become dead code for part 3.
  - Test hook `POST /api/debug/break_voxel {x,y,z,level,sub,micro}` → `removed`, `gpu_pieces`,
    `refused`.
  - Live L4 (`tools/debris_break_voxel_check.py`, prediction written in the file): cube, subcube
    and microcube each removed=True, gpu_pieces=1, refused=0; after 6 s 3 pieces active, 0 awake,
    0 below the slab; cpu_dynamic=0; occupancy_diff agrees. With `--disable-gpu-debris`:
    removed=True, gpu_pieces=0, refused=1.
  - Test-world footgun found on the way: `occupancy_diff` over a chunk takes seconds in Debug;
    interleaving it with breaks let a 25 s piece EXPIRE before the settle check (measured
    lifetimes: break piece 27.9 s, blast 29.3 s — correct).
  - Unrelated, fixed separately: the integration suite crashed intermittently (access violation
    in `ChunkRenderBuffer::createBufferRaw` for ~19 destruction tests, segfault at exit). The
    region-arena singleton kept the first test's Vulkan device; fixtures destroyed devices
    without `ChunkArenaSystem::shutdown()`. After: 6/6 full runs clean. Remaining integration
    failure `SceneIntegrationTest.AddSceneThenTransitionToIt` is pre-existing (a runtime-added
    scene with no definition hands the loader null; code untouched since July).
- **DONE 2026-10-05 — 1d part 2: texture parity.**
  - `GpuParticle::materialIndex` bits 16–25 carry the piece's micro position inside its parent
    cube (`MATERIAL_MASK`/`SLICE_SHIFT`/`SLICE_MASK` in `solver_shared.h`). The slice is a pure
    function of the piece's world centre and edge (`DamageSystem::debrisSliceFor`), set in
    `spawnBreakDebris` — so blast, chop, collapse and B-key debris all get it with no caller
    change. `particle_expand` decodes it into the grid position `dynamic_voxel.vert` already
    reads (subcube 0..2; microcube packed sub+micro). Every material reader masks:
    `particle_expand`, `solver_sync_in`, `solver_integrate`, the C++ mass/position-log readers.
  - SPIR-V proof (1b rule): 21 `.spv` changed; stripped of debug info and dead constants, all
    but the three intended (`particle_expand`, `solver_integrate`, `solver_sync_in`) are
    byte-identical to HEAD.
  - `DebrisSliceTest` (4): every subcube at any cube incl. negative, every microcube, bit
    budget, full cubes → 0.
  - Visual red→green (`tools/debris_slice_pixel_check.py`, prediction in the file): 27 Bricks
    subcubes, head-on camera, solver FROZEN, corner + centre front subcubes broken in place;
    normalised correlation of each cell before/after. Red (expand shader with the old
    hard-coded centre slice): corner **−0.183**, centre control 0.985, unbroken ≥ 0.996. Green:
    corner **0.969**, centre 0.985, unbroken ≥ 0.996. Evidence: `docs/evidence/debris_slice/`.
    Rig lesson: the face mask must come from the SAME geometry in another (non-emissive)
    material — removing the cube moved its shadow into the mask; glow lit the ground.
  - Not pixel-verified: microcube pieces (a 1/9 cell is ~30 px here) — pinned by the unit
    test and the shared decode only.
  - Bench `phase1d2`: in band (drop_layer 11.2 mm bit-identical — the mask changed no physics;
    drop_pile 12 forced, blast 2; post-window 0 everywhere; occ diff 0).
- **DONE 2026-10-06 — 1d part 3: the CPU single-box debris path is deleted (~1,650 lines).**
  - Deleted: `DynamicObjectManager` (class + per-frame update/position-sync calls),
    `ChunkVoxelBreaker` (+ `Chunk::breakSubcube`), the `ChunkManager` global-dynamic lists and
    wrappers, `FaceUpdateCoordinator::rebuildGlobalDynamicFaces`, the CPU draw block in
    `RenderCoordinator::renderDynamicSubcubes`, `VulkanDevice`'s 1,800-face dynamic subcube
    buffer, `POST /api/debug/spawn_bullet_cube`, the `cpu_dynamic`/`cpu_dynamic_cap` stats
    fields and `clear_dynamics`'s `cpu_cleared`, `perf_stress_test.py`'s `cpu` and `mixed`
    modes, `DynamicObjectBodyReleaseTest` (it pinned a leak in the deleted class).
    `ObjectTemplateManager` lost its unused `DynamicObjectManager*` parameter (30 call sites).
  - `MAX_DYNAMIC_OBJECTS` (the unenforced 300 render budget, Phase 0 finding) is gone with it.
  - `ChunkManagerIntegrationTest.DynamicSubcubePhysics` (asserted a CPU subcube joined the
    deleted list) → `BreakingASubcubeRemovesItAndHandsThePieceToGpuDebris` (the B-key break
    through `VoxelManipulationSystem` removes the subcube and, with no GPU solver, counts the
    refused piece).
  - Verified: 748/752 related unit tests (4 skipped), integration 90/91 (the pre-existing scene
    failure), `debris_break_voxel_check.py` PASS on the new build, `spawn_bullet_cube` → 404,
    `dynamic_stats` = `{gpu_active, gpu_cap}`, bench `phase1d3` in band (drop_pile 6 forced,
    blast 2, post-window 0, occ 0).
  - Build footgun hit: `RenderCoordinator.h` used `Scene::RagdollCharacter` with no declaration —
    it arrived transitively through the deleted `DynamicObjectManager.h` → `Cube.h`. Now
    forward-declared.

**1e. Analyzer: external-input window.**
- Add `KINEMATIC_CONTACTS` and `IMPULSES_APPLIED` header counters.
- The judged window starts 0.5 s after the last external input; pushes are reported as "driven".

**1f. Scripted kinematic test box** (`POST /api/debug/gpu_kinematic_box`) isolates solver tests
from NPC AI.

**DONE 2026-10-06 — 1e + 1f.**
- **1e counters:** `HASH_BASE` 8 → 16 (room to grow); `SS_KINEMATIC_CONTACTS` (8, counted where
  the character-collider push moves a body) and `SS_IMPULSES_APPLIED` (9, 0 until Phase 4).
  `PROBE_HDR_UINTS` was a hand-synced `= 8 // == HASH_BASE` mirror — now `DebrisShared::HASH_BASE`.
  SPIR-V: only the five users of the hash base / wake bits / the counter changed (integrate,
  sync_in, narrowphase, voxel, warmstart_save).
- **1e analyzer:** the judged window starts at the latest of `settleWindow`, impact end + 0.5 s
  and **last external input + 0.5 s**; the all-asleep deadline also moves to last input + 3 s.
  `summary()["driven"]` reports `kinematic_contacts`, `impulses_applied`,
  `last_external_input_s`, `hardcontact_fires_while_driven`, `rebounds_while_driven`; the bench
  table shows `driven kin/hc`. Red→green: `ExternalInputDelaysTheJudgedWindowAndIsReportedAsDriven`
  (red: judged from 1.0 s, the in-pile push failed the run, no `driven`); control
  `ControlWithoutExternalInputThePushIsStillAFailure` (the same push with no input still fails).
- **1f box:** `GpuParticlePhysics::KinematicBox` (id, centre, half, velocity, ttl ≤ 10 s), moved
  and aged by SIMULATED time (stepped solver = deterministic). Until Phase 2 the boxes join the
  character-collider buffer: axis-aligned, one shared velocity, ≤ 12 boxes with the player's
  (rest counted as `overflow`); the endpoint echoes the box, `kinematic_boxes`, `overflow`,
  the backend and any ignored `rotation`/`angular_velocity`. `gpu_physics` status adds
  `kinematic_boxes` and `kinematic_overflow`.
- **Bench:** opt-in `box_through_pile` (`--only box_through_pile`, ≥ 10 s; not in the default
  band until Phase 2). `phase1f-box` on the shove path: 294 driven contacts, last input 5.87 s
  (box exit ≈ 5.75 s), 74 hard-contact fires + 204 rebounds reported as driven, judged from
  7.62 s, post-window clean, 12 forced sleeps — the shove's violence (bodies flung to 5.9 m/s),
  which is what Phase 2 replaces. Default bench `phase1e` in band, driven 0/0 everywhere,
  drop_layer still 11.2 mm.

## Phase 2 — Kinematic colliders as real AVBD contacts

- **Representation.** Kinematic boxes are appended to the solver body array (`invMass = 0`,
  `PARTICLE_KINEMATIC`). `initial` is the previous pose and `pos` the current one, so the existing
  body-pair contact path sees their motion via `J·Δq_B` (the §R tick-start convention). There is
  no new constraint type.
- At most 512 boxes. Each debris body tests all of them after an AABB reject (measure in Release).
- **Warm-start key:** (debris, owner id, part index, feature).
- **Wake:** contact closing speed above `WAKE_IMPACT_SPEED`.
- **Delete the player shove hack (D7)** once `box_through_pile` passes.
- **Flag:** `SOLVER_FLAG_KINEMATIC_CONTACTS = 32`. Defaults 23 → 55, pinned by the bench.

**Phase 2 design, concrete (2026-10-06, from reading the passes):**
- **Where they live:** kinematic bodies occupy FIXED body indices `[MAX_PARTICLES,
  MAX_PARTICLES + MAX_KINEMATIC)` (MAX_KINEMATIC = 512) in the SolverBody buffer — never inside
  the particle range, so `count`, grid, integrate, sync_in/out and the settle probe are untouched.
  The body buffer grows by 512 × 208 B.
- **Who writes them:** a host-mapped `KinematicBoxBuffer` (layout as field macros in
  `solver_shared.h` — no C++ mirror of SolverBody) holds, per box: frame-start centre, half
  extents, rotation, velocity. A new pass `solver_kinematic_sync.comp` runs at the start of EVERY
  tick and writes body `MAX_PARTICLES + k`: `initial = c0 + v·dt·tick`, `pos = initial + v·dt`,
  `quat = initialQuat = rot`, `cumAng = 0`, `invMass = 0`, flags `ACTIVE | PARTICLE_KINEMATIC`,
  `vel = v`, friction 0.6. Multi-tick frames therefore advance the box exactly. The CPU writes the
  buffer BEFORE it advances its own box centres for the frame.
- **Contacts:** `solver_narrowphase` gains a second loop: each awake (or sleeping) debris body
  tests every kinematic body after an AABB reject, at the box's TICK-START pose (`initial`), and
  emits through the same face/edge manifold code (`bodyB = MAX_PARTICLES + k`). Dual and primal
  already read `bodies[bodyB].pos - .initial` and `.cumAng` for a non-static B, so the box's
  motion this tick enters `C` as `J·Δq_B` — the push — with no new constraint type.
- **Passes that index per-body arrays through a constraint** (`csr_count`, `csr_scatter`,
  `body_color`) treat `bodyB >= MAX_PARTICLES` exactly like `SOLVER_STATIC` (never solved,
  never coloured). Wake: the existing impact-wake test wakes a sleeper when the box speed exceeds
  `WAKE_IMPACT_SPEED`.
- **Telemetry:** `SS_KINEMATIC_CONTACTS` counts kinematic constraints emitted;
  `SS_KINEMATIC_DEPTH_UM` (new) the deepest tick-start penetration into any box (the plan's
  "≤ 2 cm every tick" check). The analyzer reports `kinematic_max_depth_m`.
- **The player** joins the same path: its segment boxes become kinematic boxes (velocity =
  controller velocity). With flag 32 ON the integrate-pass shove (D7) and its union-AABB wake are
  skipped; flag OFF keeps the old shove — the control. D7's code is deleted after
  `box_through_pile` passes.
- **Measured displacement:** the analyzer exposes each body's start/end centre
  (`summary()["body_paths"]`, runs ≤ 2,000 bodies) so the bench computes "swept bodies displaced
  ≥ 0.2 m" from the box path.
- **Red first:** `box_through_pile` with flag 32 ON before the narrowphase loop exists → 0 %
  displaced (the shove is off and nothing replaces it). Controls: box path 3 m beside the pile
  (0 displaced) and flag 32 OFF (the old shove).

**Phase 2 STATUS (2026-10-06): mover contacts BUILT, behind flag 32 (default still 23 — not
flipped, D7 not deleted). Waiting on a user decision (below).**
- Red (`phase2-red`, flags 55, no narrowphase loop): 0/24 swept bodies displaced, 0 mm.
- Three bugs found on the way, each measured, each fixed:
  1. *Corrected design note above:* measuring the box at its TICK-START pose was wrong. `emitCon`
     converts the measured depth back to tick start by subtracting BOTH bodies' motion, so the
     box's motion was subtracted twice and every mover contact looked one box-step shallower.
     Movers are now measured at `pos` like every other body.
  2. **Frames-in-flight:** one host-mapped box buffer rewritten in `update()` was read by the
     previous frame's still-running ticks — the box jumped one frame (4 × 33 mm = 133 mm, the
     exact reading) ahead. Now one buffer per frame slot, written in `recordComputeCommands`
     after the slot's fence (the occupancy pool's pattern). The old shove's character buffer has
     the same race — pre-existing, logged, not fixed here.
  3. A sleeping body is static for the tick it is hit, and alpha = 0.99 recovers only 1 % of an
     overlap per tick. Mover pairs get a speculative margin of twice the box's step, so they are
     contacted — and woken — a tick early (`faceManifold` takes the margin too).
- Cold mover contacts start ×100 stiffer (`KINEMATIC_COLD_PENALTY_SCALE`): it did not change the
  depth, but driven hard-contact fires 3 → 0 and driven rebounds 16 → 6 (`phase2-kpen100`).
- **Result, one isolated body** (one-variable probe, the box through 1/3/6 cubes in a row, and a
  single cube centred / quarter-off / straddling the box edge): max penetration **0.0–4.2 mm**
  — the ≤ 2 cm criterion holds.
- **Result, the packed 6×3×6 pile** (`box_through_pile`): 24/24 swept bodies displaced (100 %),
  post-window clean, but **3–10 cm transient overlap** for the bodies wedged in the pile
  (geometric, from dumped positions; the SAT telemetry reads up to 133–157 mm on yawed bodies).
  Doubling `SOLVE_ITERATIONS` 8 → 16 halves it (2.5–4.7 cm, telemetry 64 mm): it is the solver's
  iteration budget propagating an unstoppable push through a zero-gap pile in one tick, not the
  contact. Reverted to 8.
- **Decision needed (user):** (a) accept a few-cm transient overlap when a mover plows a packed
  pile (criterion: isolated ≤ 2 cm, pile ≤ 10 cm) and flip the defaults to 55 + delete D7; or
  (b) buy iterations (16 doubles solver cost for ALL debris; measure in Release first); or
  (c) extra iterations only on ticks with mover contacts. Until then the shipped default is
  unchanged (flag 32 off = the old shove).
- **DECIDED by the user 2026-10-06: (a).** Before switching, the per-tick depth distribution was
  measured and reported: on the final code median 17 mm / p95 63 mm / max 71 mm, but an earlier
  run of the same configuration peaked at 157 mm (p90 121 mm) — the pile run is nondeterministic,
  so "never over 10 cm" does not hold reliably. The bench therefore pins what is true:
  `box_single_straddle` max ≤ 2 cm (measured 1.1 mm); `box_through_pile` ≥ 80 % displaced, per-tick
  median ≤ 5 cm, max ≤ 20 cm (measured 16.4 / 73.9 mm).
- **DONE 2026-10-06 — Phase 2 shipped.** `SOLVER_FLAGS_DEFAULT` 23 → 55 (static_assert updated).
  **D7 deleted:** the integrate-pass shove, its union-AABB wake, the character collider buffer
  (`CharacterCollider`, `CharSegmentGpu`, `MAX_CHAR_SEGMENTS`) and integrate's bindings 3/4.
  `setCharacterColliders` now only feeds the mover list (player boxes carry the controller
  velocity); overflow is counted against `MAX_KINEMATIC`. Both box scenarios are in the default
  bench.
  - Default bench `phase2-default55`: drop_layer / packed / crater / crater_subcube SETTLE and are
    bit-identical to `phase1e` (drop_layer 11.2 mm); drop_pile 9 forced (≤ 20); both box
    scenarios PASS; blast 10 forced — outside its ≤ 6 band once; three reruns gave 1 / 5 / 1 with
    0 mover contacts and hc max 76–86 mm, so blast is nondeterministic like drop_pile (logged).
  - **L4, a real character:** an `animated` entity spawned on the blast chunk and walked by
    injected W presses into a 4×2×4 pile → 1,689 mover contacts, 25/32 pieces moved ≥ 0.2 m,
    peak penetration 195 mm (packed-pile regime); control with no input: 0 contacts, 0 moved.
    Characters still walk THROUGH debris (debris pushing back is 6a).
  - Units 87/87 debris-related; integration 90/91 (the pre-existing scene failure).
- Next: **Phase 3a** — one feed for every `AnimatedVoxelCharacter` (NPCs, monsters, fauna),
  rotated limb boxes, per-limb velocity, fed after NPC updates.

## Phase 3 — Feed every mover

- **3a. Characters** — ✅ DONE 2026-10-06 (see below; LOD-deferred characters are extrapolated,
  not dropped): one feed for **all** `AnimatedVoxelCharacter`s (`NPCManager` plus entities
  plus the player), replacing the reassignable `animatedCharacter` pointer.
  - Rotated limb boxes (`SegmentBoxInfo.worldRotation`).
  - **Per-limb velocity** from the pose delta (the CPU obstacles get it too).
  - Fed **after** NPC and entity updates (fixes the one-frame staleness).
  - Characters skipped by update-LOD or derezzing are not fed.
  - Body plans with more than 12 segments are allowed.
- **3b. Held items and doors** — ✅ DONE 2026-10-06 (see below; `sword_swat` demo still open).
  Weapons as kinematic boxes from their bone attachment. Doors and
  `KinematicAnimator` parts as kinematic boxes, **and** implement the empty
  `KinematicVoxelManager::syncCollidersToPhysics()` stub so doors block CPU bodies too.
- **3c. CPU bodies** — ✅ DONE 2026-10-06 (see below; one-way, whole bodies only). Every `VoxelDynamicsWorld` body near awake debris (furniture including
  grabbed, fragments, felled trees, item props), with compound boxes and velocities. Sleeping
  ones are included as supports.
- **Budget:** at most 512 boxes per tick, by distance to awake debris and then to the camera.
  Overflow is **counted and logged once**. *(As built: priority scripted → limbs → bodies, each by
  camera distance; awake-debris distance needs a readback and is not done.)*

**DONE 2026-10-06 — Phase 3a: every animated character pushes debris.**
- One feed (`Application`, right after the entity update loop, so this frame's pose — the old
  player-only feed ran BEFORE NPC/entity updates, one frame stale): the player, every
  `AnimatedVoxelCharacter` entity and every NPC's character (monsters, fauna, residents),
  de-duplicated, nearest the camera first (the `MAX_KINEMATIC` overflow drops the farthest).
  Budgeting by distance to awake debris is not done yet (camera distance only).
- `AnimatedVoxelCharacter::collectMoverBoxes`: ORIENTED limb boxes (bind-pose half extents + the
  bone's world rotation, not the ~1.4× inflated AABB refit) with **per-limb velocity** from the
  pose delta across a full tick, clamped to 20 m/s (a teleport must not fling debris). The CPU
  kinematic obstacles get the per-limb velocity too (was the whole-body velocity).
  `GpuParticlePhysics::setMoverBoxes` takes the oriented boxes; scripted boxes use a frame-start
  snapshot so a late-frame feed cannot shift them a frame ahead.
- **Changed from the plan, with evidence:** the plan said characters skipped by update-LOD "are
  not fed". Live, a far NPC (camera ~115 m away, reduced tick rate) then fed only on its ticks:
  its boxes vanished between ticks and reappeared a whole banked interval further on — **547 mm**
  peak penetration vs **199 mm** for the same NPC with the camera beside it. A deferred
  character is now EXTRAPOLATED along each limb's velocity by the banked time (it really advances
  that far when the banked time folds in): far camera **183.5 mm**. Derezzing characters feed
  nothing.
- Unit `CharacterMoverFeedTest` (3): every frame feeds the same limbs when no time is banked;
  oriented bind-pose extents, finite velocity, a teleport clamped; a deferred tick is the last
  pose moved along limb velocity. A first version leaked update-LOD state (a viewer 5 km away)
  into 9 later character tests — the guard now restores the switch AND the viewer
  (`AnimatedVoxelCharacter::clearViewerPosition`, new). Character suites 363/365 (2 skipped).
- **L4 `npc_walk`:** an NPC patrolling (`/api/npc/spawn`, behavior patrol, 2 m/s) through a 4×2×4
  pile → 1,447–2,075 mover contacts, 22–25/32 pieces moved, peak 183–199 mm; control patrol
  4 m beside the pile → 0 contacts, 0 moved.
- Bench `phase3a` in band; `box_through_pile`'s own band written down: forced sleeps ≤ 12
  (observed 4–11; the push wakes 100+ bodies), box checks as above (median 27 / max 134 mm).

**DONE 2026-10-06 — Phase 3c: CPU rigid bodies push debris (one-way).**
- `DebrisMoverFeed::appendRigidBodies` (new, `engine/{include,src}/core/DebrisMoverFeed.*`): every
  live `VoxelDynamicsWorld` body (furniture incl. grabbed/thrown, fragments, felled trees, item
  props) as its compound boxes: the world centre, LOCAL half extents, the body's orientation, and
  the **point velocity** v + ω × r at each box. Sleeping bodies are fed with zero velocity (supports:
  debris rests on them; residual drift must not shove). Dead or non-finite bodies are not fed.
- Fed in `Application::update` right **after the CPU physics step** (post-step pose), through
  `GpuParticlePhysics::setBodyMoverBoxes`, staged after scripted boxes and character limbs.
  Nothing is fed while there is no GPU debris (the slots are freed).
- **Budget:** nearest the camera first, in the slots the limbs left (`bodyMoverBudget()`); a body
  is fed **whole or not at all**, because a body cut at the budget lets debris fall through its
  missing boxes. Skipped bodies are counted and logged once. Ordering by distance to *awake debris*
  is not done (the host has no debris positions without a readback); camera distance is the proxy.
- **One-way, on purpose:** debris does not push or slow the CPU body (that coupling is Phase 4).
- **Not a bench scenario:** the bench steps only the GPU solver while frozen, but CPU bodies move in
  wall time, so a stepped crate run would depend on timing. `crate_through_pile` runs live instead.
- Unit `DebrisBodyMoverFeedTest` (4), red first on a stub (3 failed): world pose and point
  velocity of a spinning yawed compound; sleepers fed at zero velocity; nearest first, and a body
  past the budget skipped whole; dead bodies not fed.
- **L4 `crate_through_pile`** (DebrisLab blast chunk, 4×2×4 pile; 1 m, 200 kg frictionless crate
  from `/api/debug/spawn_voxel_body` at 3 m/s):

  | Run | Mover contacts | Lane pieces moved |
  |---|---|---|
  | Red (flags 23, bit 32 off — what every CPU body got before 3c) | 0 | 0/16 |
  | Control (same crate, 3.5 m clear of the pile) | 0 | — |
  | Through the pile | 1,155 | **16/16** |

  Through the pile: peak overlap 54.7 mm (the user-accepted packed-pile transient), 0 rebounds
  after the settle window.
- **Stress (count past the budget):** 700 one-box bodies with debris live → exactly 512 boxes fed,
  kinematic overflow 0 (the feed trims whole bodies before staging), one WARN reading "188 CPU
  bodies past the 512-box budget". The slots are freed when the bodies or the debris clear.
- `/api/debug/gpu_physics` reports `character_mover_boxes` and `body_mover_boxes`.
- Full unit suite: 4,144 passed, 20 skipped, 5 failed. None of the failures is in a system 3c
  touches: `AtlasManagerTest.BuildAtlasFromSourcePNGs`,
  `FineFaceMerge.SubcubeMerge_CrossCubeSplitsOnLightBoundaryBetweenCubes` and 3
  `GpuTimingHistoryTest` cases (which read garbage values). Not yet checked against a clean
  baseline.
- Bench `phase3c` in band (no CPU bodies in the lab, so this is the no-regression check):
  - drop_pile: 6 forced sleeps;
  - blast: 6 forced;
  - box_through_pile: 9 forced; box 24/24 displaced, median 15.2 mm, max 71.8 mm;
  - straddle: 1.1 mm;
  - every other scenario: 0 forced sleeps;
  - `occ diff`: 0 everywhere;
  - blast hard-contact maximum: 86.2 mm, identical to `phase3a`.

**DONE 2026-10-06 — Phase 3b: doors, animated template parts and held items push debris; doors
block CPU bodies.** (`sword_swat` is NOT demonstrated yet — see the last bullet.)
- `KinematicVoxelObject.pushesDebris` (opt-in): set by `DoorManager` (doors),
  `ObjectTemplateManager` (animated template parts), and `Application` (the player's and NPCs'
  held items). OFF for the visuals of CPU bodies (furniture, item props, fragments): 3c already
  feeds the body, so feeding its visual too would double the push.
- `KinematicVoxelManager::syncCollidersToPhysics(dt)` — was an empty stub. Once per frame, after
  every owner set its transform and before the CPU step:
  - splits each flagged object's voxel AABB into oriented sub-boxes of at most `kMoverCell`
    (0.6 m) per axis, at most 4 per axis — so a door's free edge gets its own, faster box;
  - each sub-box's velocity comes from ITS centre's transform delta this frame (clamped to 20 m/s;
    the first frame is 0);
  - the same boxes go to the GPU as movers (`setObjectMoverBoxes`) and to the CPU world as
    kinematic obstacles (their AABBs), so **doors now block furniture**. A removed object leaves
    no ghost obstacle.
- **Budget:** staging order is scripted → limbs → objects → CPU bodies. Objects are sorted by
  camera distance and trimmed to the slots the limbs left (`objectMoverBudget()`); the overflow is
  logged once.
- Unit `KinematicMoverFeedTest` (4), red first on a no-op stub (3 failed; the 4th passed 0 = 0, so
  it now asserts boxes exist first):
  - only flagged objects are fed;
  - sub-boxes tile the object exactly and carry its rotation;
  - per-box swing velocity, free edge faster than the hinge edge, teleport clamped;
  - the same boxes become CPU obstacles, and a removed door leaves none.
- **L4 `door_swing`** (`door_wood` through the real `/api/world/template` + `/api/door/register`
  path, 120°/s; 3×2×3 piles of 1/3 cubes against both faces):

  | Run | Contacts | Swing side moved | Other side moved |
  |---|---|---|---|
  | Red (flags 23, bit 32 off — what doors got before 3b) | 0 | 0/18 | 0/18 |
  | With 3b | 419–425 | **18/18** | 0/18 (the control) |

  0 rebounds. **Overlap is worse than the scripted box:** per tick median 36.8 / p90 127 / max
  138 mm (box_through_pile: median 15–27). That is inside the accepted packed-pile band (median
  ≤ 50, max ≤ 200) but large against 1/3 m cubes. **Untested hypothesis:** GPU movers have no
  angular velocity, and their rotation steps once per frame (coarse at Debug frame rates).
  Angular velocity on kinematic bodies would help limbs (3a), doors and swings alike; open item.
- **L4 `door_blocks_crate`:** a 0.5 m crate sliding at 2 m/s at a closed door stops against it
  (z 15.30, door plane 15). The same crate 2 m to the side passes the plane (z 14.17). Before 3b
  the stub registered nothing (the unit red).
- **Stress (count past the budget):** 70 registered doors (560 boxes) plus one player →
  `object_mover_boxes` exactly 500 (512 − 12 limbs), limbs kept, `kinematic_overflow` 0, one WARN
  (first logged at 4 boxes over); after unregistering every door: 0.
- **Held items, live:** an `iron_sword` in the player's hand is 3 mover boxes. Unequipping drops
  them to 0 and re-equipping brings them back.
- Bench `phase3b`: drop_pile 7 forced, blast 4, straddle 1.1 mm, every other scenario 0 forced,
  `occ diff` 0, blast hard-contact maximum 86.2 mm (unchanged). **`box_through_pile` exceeded its
  written band:**
  - 13 forced sleeps against ≤ 12, with the box checks passing (24/24 displaced, median 14.4,
    max 69.5 mm);
  - three clean-engine reruns (`phase3b-btp-rep1..3`) gave **4 / 13 / 11** forced sleeps, box
    medians 14.7–23.2 mm and maxima 67.7–137.5 mm, all box checks passing;
  - so 13 recurs on a clean engine. The ≤ 12 band was set from six earlier runs (4–11), and the
    scenario is nondeterministic (255–281 bodies woken). 3b adds nothing to the GPU path when no
    object is flagged (it stages an empty list), but no A/B against the 3c binary was run;
  - **the band is NOT widened here.** Deciding the band (or making the scenario deterministic)
    is open for the user.
- **`sword_swat` is DEPRIORITIZED (user, 2026-10-06: "not as concerned with the sword swinging
  test case").** Not a gate for Phase 3; the notes below are kept for whoever picks it up. Swings sweep chest height, but the lab's debris lies on the
  floor; a weapon-height rig (debris on a pedestal at sword reach, beyond the bare arm's reach as
  the control) is not built. The held-item boxes use the same feed as the door, which is proven
  with contacts above.

**User report (2026-10-04, live): "I cast a spell at broken dynamic voxels and it didn't hit
them."** Pre-existing, not a Phase 0 regression. Two separate gaps, both confirmed in code:
- **Aiming:** `Application::castSpellAtHover` targets the hovered STATIC voxel; the hover ray
  only sees chunk voxels, so debris cannot be targeted — the bolt flies past to the ground.
- **Blast:** on impact `DamageSystem::applyDamage` breaks static voxels and spawns NEW debris;
  nothing pushes EXISTING debris (`GpuParticlePhysics` had no impulse API at all).
The radial impulse below fixes the blast half (aim at the ground beside a pile). Making a bolt
STOP on a debris piece mid-flight needs debris positions on the CPU — a small readback
(Phase 6) or a GPU-side projectile query; decide when Phase 4 starts. **Acceptance for Phase 4
includes this user scenario live:** spell at a settled pile → pieces in the radius move.

- **API:** `applyRadialImpulse(center, radius, impulse, upBias)` and
  `applyConeImpulse(origin, dir, halfAngle, range, impulse)`.
  - On the GPU: applied in integrate (Δv = J·w(d)/m, torque, wake).
  - **The same call pushes CPU bodies** in range.
- **Hooks:** `DamageSystem::applyDamage`, `carveChopKerf`, spell hits, `CombatSystem` swings
  (hit *or* miss). `try_push` returns as a cone impulse.
- **Clamps at entry, with the reason in code:** Δv ≤ 25 m/s (no continuous collision detection,
  tunnelling), radius ≤ 32 m, at most 64 impulses per tick (counted).

**DONE 2026-10-06 — Phase 4 (core): impulses on existing debris and CPU bodies; blasts and spells push.**
- **Mid-flight targeting decided:** NOT done. The impact-point radial push fixes the user's
  scenario without a readback. The hover ray sees only static voxels, so a spell aimed at a pile
  lands on the floor under or behind it, inside its footprint, and the push throws the pile.
  Stopping a bolt ON a piece mid-flight stays with Phase 6 (readback).
- **One law, both worlds** (`shaders/solver_shared.h`):
  - `phxImpulseWeight(d, r) = 1 − d/r` (0 at and beyond r), dv = J·w(d)/m along the radial
    blended toward +Y by `up_bias`, |dv| ≤ `IMPULSE_MAX_DV` (25 m/s);
  - `IMPULSE_MAX_RADIUS` 32 m, `MAX_IMPULSES` 64 per frame (extras counted);
  - `ImpulseGpu` record (48 B, static_asserted).
  - The GPU half is applied in `solver_sync_in` on the first tick that RUNS after queueing; a
    frozen or stepped solver keeps impulses queued until its next step.
  - The CPU half is `VoxelDynamicsWorld::applyImpulse`, applied immediately and calling the same
    `phxImpulseWeight`.
- **Fresh debris is not kicked twice:** bodies of spawn age 0 (never ticked: the debris this
  same blast just created, which carries its own launch velocity) are skipped.
- **Wake reach (found live):** an impulse also wakes sleepers out to `IMPULSE_WAKE_SCALE` (2) ×
  its radius. A sleeper is static during the tick it is hit (a contact only sets a wake bit for
  the next tick), so pushed pieces slammed into frozen walls. Before the fix, a 600-energy blast
  beside a settled pile gave the near pieces ~4.5 m/s yet moved the pile at most 15 cm.
- **API:** `POST /api/physics/impulse` (`x,y,z`, `radius`, `impulse`, `up_bias`, optional
  `direction` + `half_angle_deg` for a cone, `worlds` gpu|cpu|both). The response echoes the
  clamped values, `queued`, `cpu_bodies_pushed`, `gpu_pending` and `gpu_overflow`.
  `gpu_physics` reports `impulses_pending`, `impulses_submitted` and `impulse_overflow`.
- **Hook:** `DamageSystem::applyDamage` pushes on EVERY blast, including one that breaks nothing.
  That covers the real spell (`castSpellAtHover` → pending hit → `applyDamage`), API blasts and
  chops through it. `apply_damage` echoes `push{impulse, radius, gpu_queued, cpu_bodies}`;
  `push:false` is the test control (= every blast before Phase 4).
- **Blast strength, grounded:** J(E) = m_Stone · `BASE_SPEED` · √(E / toughness_Stone),
  i.e. 24·√(E/110) N·s. A loose Stone piece at the centre then leaves exactly as fast as a Stone
  voxel the same blast breaks there; loose debris has no bond to break, and momentum ~ √energy.
  The first guess, J = 0.05·E, gave 2.5 m/s against ≥ 4 m/s for broken pieces. Unit-pinned
  (`BlastPushMatchesTheBreakLaunchSpeedForStone`). The push reaches 1.5 × the blast radius.
- Unit `ImpulseLawTest` (4), red first on a stub (3 failed; the pure falloff passed): falloff,
  CPU dv = J·w/m along the radial and waking, up bias + 25 m/s clamp, cone exclusion.
- **L4 law (`impulse_law`)** — airborne Stone cubes at 1/3/5/7/9 m, J 40, r 8, solver frozen:

  | | 1 m | 3 m | 5 m | 7 m | 9 m |
  |---|---|---|---|---|---|
  | Predicted dv_x (m/s) | 5.833 | 4.167 | 2.500 | 0.833 | 0 |
  | Measured dv_x (m/s) | 5.798 | 4.142 | 2.485 | 0.829 | 0 |

  Within 0.6 % (one tick of damping). Control J 0 → 0 everywhere. The age-0 rule holds: the
  same impulse before the cubes ever ticked → 0. The first rig sat the cubes on the floor and
  read a constant ~0.15 m/s low, which is one tick of kinetic friction (μ·g·dt = 0.13 m/s) plus
  damping. That is why the rig is airborne.
- **L4 user scenario (`spell_on_pile`)** — a 300-energy `/api/damage/apply` (the spell's impact
  function) on the floor inside a settled 4×2×4 Stone pile's footprint:

  | Run | Pile pieces moved | Movement |
  |---|---|---|
  | With the push | **32/34** | 0.7–0.8 m, 0 rebounds after the window |
  | Control, `push:false` | 0/36 | largest 3.4 cm (from the blast's own new debris) |
- **Recorded, not a gate — a blast BESIDE the pile:** pushing a 192 kg packed Stone pile
  sideways on μ 0.8 moves it as one block by 9–17 cm (energy 300–600). That is correct
  momentum bookkeeping: the near pieces share their momentum with the whole pile, and floor
  friction stops a ~1 m/s block in ~8 cm.
- **L4 `blast_pushes_furniture`:** a 10 kg CPU crate 1.5 m from a 300-energy blast →
  `cpu_bodies` 1; it slid +0.175 m away from the blast, against a computed 0.19 m (dv 1.9 m/s,
  ~1.5 horizontal, slide v²/2μg). Control 8 m away: 0 bodies, 0.000 m. (The first threshold,
  0.2 m, was a guess set before the slide was computed; the script now checks ±30 % of the
  computed value.)
- **Stress, count past the cap:** 70 impulses queued on a frozen solver → pending stopped at 64,
  the extras were counted; one step submitted exactly 64 and emptied the queue.
  - Degenerate calls: a 1e30 centre was dropped and counted; radius 1e6 was clamped to 32 with
    J −5 clamped to 0 (not queued); `worlds:"nowhere"` → an error.
  - FOOTGUN: a minimized engine window stalls the game loop (API requests time out); the first
    churn run died to that.
- **Stress, churn:** 200 impulses over 200 stepped frames on a live 32-body pile, then 480 ticks
  of settling.
  - Every invariant holds: 0 NaN/lost bodies, 0 tunnelled, 0 rebounds after the window,
    0 injected energy after the window, all asleep by ~6 s.
  - The analyzer verdict still reads FAILS for two reasons, both run down:
    1. **`held_unknown_occupancy` 93–277 = a DebrisLab edge artifact, not Phase 4.** The lab is
       one row of chunks at z 0..31. With NO impulse at all, a cube resting at z 30.5 is held
       60 ticks (contact sampling reaches the missing chunk at z ≥ 32), while z 1.5 and z 14 give
       0. The churn threw pieces to z 30.3. Holding a body whose surroundings are unknown is the
       intended 1c behaviour. With the pile centred at z 10 (J 10, chunk 2), the pieces ended at
       z 2.1–23.5 and held = 0.
    2. **2 forced sleeps** (of 32). This is the packed-pile behaviour that every pile scenario
       carries a band for (drop_pile ≤ 20, box_through_pile ≤ 12). Recorded as this rig's
       observed value, not a pass at the analyzer's default limit of 0.
- **Bench `phase4`:**
  - drop_pile 10 forced; box_through_pile 5 forced (box 24/24 displaced, median 24.8 / max
    136 mm, pass); straddle 1.1 mm; packed/crater/crater_subcube/drop_layer 0 forced;
  - `occ diff` 0 everywhere;
  - **blast 11 forced vs the ≤ 6 band**, with the canonical setup (14 broken, 59 bodies) and an
    unchanged hard-contact maximum of 86.2 mm. The Phase 4 push applied to 0 bodies there
    (nothing older than spawn age 0 exists when it detonates).
- **Bench-rig bug found and FIXED (`tools/debris_settle_bench.py`):**
  - `restore_blast_site` refilled the slab with a plain `/api/world/fill`, which only fills
    EMPTY cells. Cubes the previous blast grazed kept their accumulated damage, so back-to-back
    blast runs broke 20 / 14 / 23 voxels (59–68 bodies) instead of 14. The fill now passes
    `replace: true`.
  - Three back-to-back blast runs after the fix: 14 broken / 59 bodies / 86.2 mm every time;
    forced sleeps 4 / 1 / 4 (in band). That rules damage carry-over out of the full run's 11
    forced; it is the GPU nondeterminism logged earlier (1–10).
- Units green: `ImpulseLaw` (4), `BlastPushMatchesTheBreakLaunchSpeedForStone`, and the 39-test
  mover/debris/kinematic set. Integration: 90/91, the one failure being the pre-existing
  `SceneIntegrationTest.AddSceneThenTransitionToIt`.
- `push.gpu_queued` reads true on a blast right after `clear_dynamics` (the pool still counts as
  active until the next frame). It is harmless, since 0 bodies are affected
  (`impulses_applied` 0), but the echo means "queued", not "pushed something".
- **SPIR-V check:** `solver_shared.h` feeds every solver shader. An anonymised `spirv-dis` diff
  vs HEAD shows only the ID-bound header changed in every shader except `solver_sync_in`.
- **Not done in Phase 4 yet:** `CombatSystem` swing cones, the chop hook beyond `applyDamage`,
  `try_push`, angular kicks (dv is linear only), and the `cast_test_spell` non-destroy path (VFX
  only, no `applyDamage`).

## Phase 5 — Debris in shipped games (separate design check before starting)

`EngineRuntime`/`GameShell` gain `GpuParticlePhysics`, a voxel `DamageSystem`, occupancy (1c),
the mover feed (3) and the impulse hooks (4). The `create_project.py` scaffold and
`minimal_game` follow.
- **L4 on the packaged binary** via the production harness (`--test`, `apply_damage` +
  `settle_probe`).
- The demolition makes this much smaller: there is only one debris world to port.

**Design check, 2026-10-07 (`/design-check`), verdict NEEDS WORK → resolved by the user's
decisions below.**

What exists:
- `RenderCoordinator` is SHARED engine code: debris compute/draw/shadow and the occupancy upload
  already run in standalone games. Only `setGpuParticlePhysics` (`RenderCoordinator.cpp:830`) is
  never called, because no standalone code creates the solver.
- EDITOR-ONLY (`Application.cpp`):
  - solver creation and kill switch (391–409), `update` (3544);
  - the three mover feeds (3892 / 4274 / 4320);
  - the break provider (`setGpuDebrisProvider`, 291) and 4 ad-hoc `DamageSystem`s;
  - every debris API handler (`apply_damage`, `gpu_physics`, `settle_probe`, `spawn_gpu_lattice`,
    `occupancy_diff`, `physics_impulse`).
- `GameApiService` serves the same routes through its own `CommandRegistry`, which lacks those
  actions, so the packaged exe answers `unknown action`.
- Nothing in a standalone game breaks voxels (the scaffold's `applyDamage` is combat HP).

User decisions (2026-10-07):
1. **Shared `DebrisRuntime`** (engine class): solver creation + kill switch, `update`, the three
   feeds, the `DamageSystem` entry and impulses. `Application` is refactored onto it and the debris
   API handlers are registered from ONE place for both the editor and `GameApiService`. This
   avoids two hand-synced copies.
2. **Break hook = engine API + scaffold spells**: games call `DebrisRuntime::applyDamage`; the
   `create_project.py` scaffold's spell/blast impacts call it, so a scaffolded game breaks voxels
   out of the box.
3. **On by default**, off via `game.json` `debris.enabled:false` or `--disable-gpu-debris` /
   `PHYXEL_DISABLE_GPU_DEBRIS`. The default is pinned by a unit test; GPU memory is measured.

Gate answers:
- **Aesthetic:** no new assets.
- **Chunks:** the occupancy window and the budget order bound cost; holding bodies next to
  unloaded chunks is the defined 1c behaviour; `DebrisContactOccupancyTest` still covers seams.
- **Generation:** none (runtime simulation; the switch lives in `game.json`, not the world recipe).
- **API:** no new routes; shared handlers.

Test plan:
- The packaged Release exe with `--test` on a test project whose `default.db` is a one-chunk flat
  Stone slab (the bench's `--build-lab` needs world fill, which the test API lacks).
- **Red:** today `apply_damage` → `unknown action`.
- **Works:** debris > 0 with `debris_refused` 0; the bench scenarios run against the packaged
  exe stay inside the editor's bands; `occupancy_diff` 0.
- **Control:** `--disable-gpu-debris` → one ERROR log, `debris_refused` = N, no crash.
- **Rig vs shipped:** editor Debug vs packaged Release; the bench steps a fixed tick, so per-tick
  metrics are comparable, but wall-clock ones are not.

Build steps:
- **5a.** Extract `DebrisRuntime` from `Application` with no behaviour change. Proof: the editor
  bench stays in band, and the existing mover/impulse L4 scripts give the same results.
- **5b.** Shared debris API handlers (editor + `GameApiService`).
- **5c.** Scaffold + `minimal_game` wiring, including `game.json` `debris.enabled`, the spell hook
  and the pinned default.
- **5d.** Packaged-binary L4 (red → green + control) and the bench parity run.

**DONE 2026-10-07 — 5a: `DebrisRuntime` (`engine/{include,src}/core/DebrisRuntime.*`).**
- Moved out of `Application`, unchanged: solver creation + kill switch (now also
  `Config::enabled`), wiring to `RenderCoordinator` / `ChunkManager`, `beginFrame` (= `update`),
  and the three feeds (`feedCharacters`, `feedKinematicObjects`, `feedRigidBodies`).
- `Application` keeps a non-owning `gpuParticlePhysics` alias in the same member slot (teardown
  order unchanged) and gathers its own character list (player, entities, NPCs) for
  `feedCharacters`. The "logged once" flags became members instead of function statics.
- No behaviour change, measured on the rebuilt editor (Debug, DebrisLab):
  - units: 51/51 (the mover, impulse, debris, damage and VoxelDynamics suites);
  - `npc_walk`: 26/32 moved, control 0;
  - `crate_walk`: 14/16 moved, control 0 / red 0;
  - `impulse_law`: 5.798 / 0.829 m/s, identical to before;
  - `spell_on_pile`: 32/32 moved;
  - `door_swing`: swing side 18/18, other side 0/18.
- **Door overlap varies widely between live runs:**
  - median 36.8 → 85.2 mm, max 138 → 159 mm; the 85 mm median is above the ≤ 50 mm packed-pile
    band. The object feed is code-identical before and after 5a, so this is the existing open
    item, not a regression;
  - the swing is wall-clock driven at Debug frame rates and GPU movers have no angular velocity.
    **This makes "angular velocity for GPU movers" a real open item, not a nicety.**
  - One 5a regression run read 3,016 door contacts / 1,324 contact ticks at median depth 0. It
    did not reproduce standalone (480 contacts / 54 ticks) and is unexplained; it is suspected to
    be a leftover from the `crate_walk` script run just before it, whose lane crosses the door.

**BUILT 2026-10-07 — 5b: shared debris API handlers (`engine/{include,src}/core/DebrisApiCommands.*`).**
- `registerDebrisCommands(reg, contextProvider)` registers:
  - `apply_damage`, `physics_impulse`, `occupancy_diff`;
  - the debug debris set: `spawn_gpu_particle`, `spawn_gpu_lattice`, `particle_log`,
    `gpu_physics`, `gpu_kinematic_box`, `settle_probe`, `spawn_voxel_body`, `clear_voxel_bodies`,
    `clear_dynamics`.
- The code moved VERBATIM out of `Application.cpp` by a script: the 301-line debug dispatcher,
  `occupancy_diff` (95 lines), `apply_damage` (56), `physics_impulse` (37). Only the host members
  were rebound to `DebrisApiContext`; `break_voxel` stays editor-only (it needs the editor's
  interaction system).
- The editor registers it in `registerEffectsCommands`. `GameApiService` always registers it
  through a `debrisContext` provider, and `GameShell::startTestApi` wires that provider (chunks,
  physics, renderer, its own `DebrisRuntime`). A game without debris answers "not available"
  instead of "unknown action".
- Editor parity after the move (bench `phase5b`): drop_layer 11.2 mm, packed 0 forced, blast
  14/59/86.2 mm with 3 forced, straddle 1.1 mm, occ diff 0; `impulse_law` 5.798 m/s.

**BUILT 2026-10-07 — 5c: shipped-game wiring.**
- `GameShell` owns a `DebrisRuntime` (`initDebris`, `debris()`).
- `DebrisRuntime` gains:
  - `shutdown()`, because a game's member outlives the Vulkan device; games call it first in
    `onShutdown`;
  - `applyDamage` (the game-code break entry);
  - `spellBlast(SpellDefinition)`: damaging spells only; area size ft→m, else 1 m; energy
    12 × average base damage, so a fireball 8d6 → 336, the scale of the editor's test-spell
    blast (350);
  - `handleArg` (`--disable-gpu-debris`, now shared by the editor main, the scaffold main and
    `minimal_game`).
- Scaffold (`tools/create_project.py`):
  - `game.json` `"debris": {"enabled", "spellsBreakVoxels"}`, both default true;
  - `initDebris` after the definition loads;
  - `beginFrame` before the CPU step; the three feeds at the end of `onUpdate`;
  - `shutdown` first in `onShutdown`;
  - every damaging spell's release frame blasts its target point (`playCastVisualFor`'s `fire`);
  - held NPC weapons are flagged `pushesDebris`.
- `minimal_game`: `initDebris`, `beginFrame`, `feedRigidBodies`, `shutdown`.
- Unit `DebrisRuntimeTest` (3): the shipped default pinned ON; disabled/deviceless is loud with no
  solver and safe no-ops; the `spellBlast` table. These tests have no stub-red: the API did not
  exist, and the default is a contract pin. The behavioural red is 5d's packaged
  `unknown action`.
- Bench gains `--no-verify` for a packaged game's test API, which serves the debris actions but
  not world queries. It refuses `blast`, whose site restore needs `/api/world/fill`.

**DONE 2026-10-07 — 5d: GPU debris verified in a PACKAGED Release game.**
Test game `DebrisShip` (`tools/produce_game.py`: scaffold → Release build → package → smoke, all
green), with DebrisLab's `game.json` + a fresh copy of its `worlds/default.db`, run as
`DebrisShip.exe --test 18097`.
- **Regression found and fixed on the way:** since 1d (`ObjectTemplateManager(ChunkManager*)`),
  the scaffold template still passed a second `nullptr`, so EVERY scaffolded game failed to
  compile. Fixed in `tools/create_project.py`. Projects generated before today carry the old line
  and need the same one-word fix when rebuilt.
- **Red (by inspection, no pre-Phase-5 package exists):** `GameApiService` had no `apply_damage`
  / `gpu_physics` handler, so they fell through to `unknown action`.
- **Green, the solver:** `gpu_physics` → `enabled:true`, flags 55, ticks running.
- **Green, blast parity:** the bench's blast through the shipped `apply_damage` gave the editor's
  canonical numbers exactly. 14 broken / 59 debris / 94 grazed / 33 stage-changed; hard-contact
  maximum 86.2 mm; 2 forced sleeps; 0 rebounds / 0 tunnelled / all asleep; `occupancy_diff`
  agrees.
- **Green, bench parity** (`phase5d-packaged`, `--no-verify`):

  | Scenario | Packaged result |
  |---|---|
  | drop_layer | 11.2 mm, identical to the editor |
  | drop_pile | 10 forced (band ≤ 20) |
  | packed / crater / crater_subcube | 0 forced |
  | box_through_pile | 8 forced (≤ 12); box 24/24, median 16.9 / max 72.4 mm |
  | box_single_straddle | 1.1 mm |

  `occ diff` 0 everywhere. Release matches Debug per tick, as predicted for the stepped bench.
- **Control, `--disable-gpu-debris`:**
  - one `[ERROR] [DebrisRuntime] GPU debris DISABLED` line;
  - `gpu_physics` reports the reason;
  - `apply_damage` broke the same 14 voxels and returned `debris` 0 / `debris_refused` 59;
  - the process stayed up.
- **Spell hook, end to end** (`game.spelltest.json`: a level-1 wizard knowing fire_bolt and
  burning_hands, a target NPC on the Stone slab, a settled 18-piece pile beside it; cast through
  the game's own `combat/player_cast` in turn-based mode):

  | Run | `impulses_applied` | New debris |
  |---|---|---|
  | Control, no cast | 0 | 0 |
  | Fire Bolt | 12 | 0 |
  | Burning Hands | 12 | 0 |

  So the cast → release frame → `spellBlast` → `DebrisRuntime::applyDamage` chain runs in the
  shipped game. **Two predictions were wrong, recorded as such:**
  - Fire Bolt moved 0 pieces ≥ 0.1 m: w ≤ 0.33 at 1.0–1.5 m gives dv ≤ 1 m/s on 6 kg Stone,
    which floor friction stops within ~6 cm. Burning Hands moved 3.
  - Burning Hands broke nothing. Its 1 m single-target blast is centred on the surface, so the
    nearest Stone voxel centre is at half the radius and receives 126 × 0.5^1.5 ≈ 45 < 110.
    Gameplay reading: level-1 spells do not dig stone, while a fireball reaches
    336 × 0.918^1.5 ≈ 296 at the nearest voxel and does. The scaffold's player is fixed at
    level 1, so a spell-driven break was not shown live; the break path is shown by the packaged
    `apply_damage` above.
- **Phase 5 DONE** (5a–5d).
  - Open: a spell-driven break live (needs a higher-level caster in the scaffold);
    `occupancy_diff`'s `break_voxel` sibling stays editor-only.
  - The scaffold feeds held items one frame late (`updateHeldWeapons` runs before the feeds at
    the end of `onUpdate`, which is fine) and runs its CPU step before the character update, so
    CPU-body obstacles reach the CPU world one step late.

## Phase 6 (optional, separate design check) — small readbacks and water

- **6a.** Debris pushes back on characters (per-owner summed force, about 2 frames latency, L3).
- **6b.** Sleep events (gatherable settle, audio).
- **6c.** Debris buoyancy and drag.

---

## §API (`/api/debug/*` convention: an omitted field means unchanged; responses echo the resulting state; clamps at entry with the reason in code)
| Endpoint | Fields (units) | Echo |
|---|---|---|
| `POST /api/physics/impulse` | `x,y,z` (world m), `radius` (m, 0.1–32), `impulse` (N·s at the centre, ≥0), `up_bias` (0–1), optional `direction` + `half_angle_deg` for a cone, `worlds` (`gpu`, `cpu`, `both`; default `both`) | `queued:true`, the clamped values used, the CPU bodies hit (same frame). The GPU bodies affected/woken appear in `gpu_physics` status 2 frames later |
| `POST /api/debug/gpu_kinematic_box` | `id`, `center`, `half` (m), `rotation` (quat), `velocity` (m/s), `angular_velocity` (rad/s), `ttl` (s, ≤10), `remove` | the box as stored, `kinematic_boxes`, `overflow` |
| `POST /api/debug/occupancy_diff` | `x1,y1,z1,x2,y2,z2` (world voxels, ≤ 64³) | `cell_mismatches`, `subcube_mismatches`, the first 20 mismatching cells with grid vs. world |
| `POST /api/debug/gpu_physics` (extended) | existing fields, plus `kinematic_range` (m, 4–128) | adds `kinematic_boxes`, `kinematic_overflow`, `impulses_applied`, `frozen_out_of_window`, `window_origin` |

**Defaults pinned:** flags 55, `kinematicRange` 48 m, 512 kinematic boxes, 64 impulses per tick,
window recentre at 64 m. The settle bench is the pin; a deliberate change updates its
expectations in the same commit.

## §Test plan (red first; DebrisLab, one new chunk per scenario; every scenario also asserts `occupancy_diff == 0`)

Validation depth:
- **L2** for every phase;
- **L4** live (the demo, plus the user watching) before each phase is called done;
- **L3** for 6a only;
- Phase 5 adds L4 on the packaged binary.

Phase 0's main gate is *unchanged* behaviour: the settle bench must stay inside the written
regression band (Phase 0, "done when" #3).

| Scenario | Phase | Setup | "Works" (measured) | Prediction today (red) | Control |
|---|---|---|---|---|---|
| `build_fails_on_bad_shader` | 1a | syntax error injected into a copy of `solver_voxel.comp` | the script exits non-zero | exits 0 (stale `.spv` kept) | a valid shader exits 0 |
| `subvoxel_floor` | 1c | 6×6 floor of 1/3-thick subcube slabs; cubes dropped | rest height = slab top ± 2 cm | float about 0.67 m (rebuild path) or fall through (stream path) | the same drop on a full-cube floor |
| `template_landing` | 1c | debris dropped onto a `spawn_template` crate | rests on the crate | falls through (crate not in the grid) | the same crate placed by `fill_region` |
| `far_drop` | 1c | the 36-cube control at x = 1000, z = 1000 | identical metrics to `drop_layer` | 36/36 tunnelled | `drop_layer` at the origin |
| `unload_ghost` | 1c | stream a chunk out, then drop debris there | frozen and counted (no ghost floor, no infinite fall) | rests on a ghost floor | a resident chunk |
| `box_through_pile` | 2 | settled 6×3×6 pile; scripted 1×2×1 box crosses at 2 m/s | ≥ 80 % of swept bodies displaced ≥ 0.2 m; penetration into the box ≤ 2 cm every tick; after the box leaves, 0 rebounds, asleep ≤ 3 s, 0 injected energy | 0 % displaced | the box path 3 m beside the pile (0 displaced); flag bit 32 off (0 displaced) |
| `npc_walk` | 3a | real NPC patrolling through a pile (L4) | as above | NPC passes through | NPC patrol beside the pile |
| `sword_swat` | 3b | NPC melee swing into a pile | the bodies hit move along the swing | no effect | swing in empty air |
| `crate_through_pile` | 3c | CPU crate launched at 3 m/s through a pile (live script, not the stepped bench — CPU bodies run in wall time) | as `box_through_pile` | 0 displaced | crate missing the pile |
| `impulse_on_pile` | 4 | settled pile; 40 N·s at 2 m from the edge, radius 4 | per-body Δv within ±10 % of J·w(d)/m; beyond the radius Δv = 0; re-settle ≤ 3 s | Δv = 0 | the same call with `impulse:0` |
| `blast_pushes_furniture` | 4 | `apply_damage` next to a dynamic crate | the crate's Δv > 0, directed away from the blast | crate untouched | blast out of range |
| `break_to_gpu` | 1d | B-key break of 20 voxels | `gpu_active` +N, 0 CPU dynamic cubes, scales preserved, settle checks pass | CPU bodies | `apply_damage` (already GPU) |
| `gpu_init_failure_is_loud` | 0 | `PHYXEL_DISABLE_GPU_DEBRIS=1`, then `apply_damage` | one ERROR log, debris disabled, every spawn counted as refused (no silent half-state) | silent: voxels vanish, no debris, no log | normal launch |
| `texture_parity` | 1d | physics frozen; capture an intact subcube/microcube wall; `break_voxel` the whole wall; capture again | viewport pixel difference over the wall region ≤ the capture-noise threshold | large difference: every piece shows its parent's centre texture slice | the same capture pair with no break (measures capture noise) |
| `edit_under_streaming` | 1c | 30+ new chunks streaming in; same frame, `apply_damage` a pile site | debris spawned in emptied cells gets 0 hard-contact pushes in its first 3 ticks; `occupancy_edit_backlog` = 0 at spawn | pushes in ticks 0–2 (no edit priority) | the same blast with no streaming |
| `unknown_freezes` | 1c | debris dropped over a chunk forced absent from the pool (overflow hook or out of box) | frozen in place, counted in `frozen_unknown_occupancy`, 0 below the floor | falls forever | the same drop over a resident chunk |
| `debris_contact_equals_grid` | 1c | unit: `voxelPointContact` through the packed pool vs. through `VoxelOccupancyGrid`, seams, mixed cubes, negative coords | identical results | n/a (new code) | lighting's `ChunkedOccupancyEqualsTheWholeRegionAcrossEverySeam` |

**Rig vs. shipped defaults** (also written into the evidence JSON):
- Debug build, flat Stone, one collider at a time.
- The scripted box has no angular velocity, unlike real limbs.
- The impulse test uses a single material; real blasts mix materials (Δv ∝ 1/m).

## Order

**0** (D4 → D5 → D3 → D2 → D6, small commits) → **1a** build safety → **1b** shared header →
**1c** one occupancy (the largest step) → **1d** D1 break debris to the GPU, with texture parity
→ 1e/1f → **2** contacts (deletes D7) → 3a → 3c → 3b → **4** impulses → **5** shipped games
(separate gate) → 6 (separate gate).

Each phase is shippable on its own and gated by the bench (plus `occupancy_diff` from 1c on).

**Why this order:**
- Deletions with no behaviour impact come first; each one shrinks what later steps touch.
- The only behaviour-changing deletion (D1) waits until debris collides with the same exact
  occupancy the CPU uses, so moving B-key debris to the GPU cannot regress sub-voxel or
  far-from-origin collision.
- 1c deletes a whole occupancy system plus ~20 call sites instead of patching them.

## Risks

- **(rev 2 item, superseded by 1c) 0c re-sync cost.** One chunk is 32 k cells (plus subcube masks). Spread it over frames, and
  freeze debris in not-yet-synced cells (counted).
- **Kinematic contact cost** at the 10 k cap with 512 boxes (about 5 M box tests per tick).
  Measure in Release first.
- **Fast limbs and weapons** (around 10 m/s) can tunnel debris. Detection at the predicted pose
  covers 1 body width per tick; measure `sword_swat`.
- **Behaviour change** for B-key debris (CPU → GPU, Phase 0) and the removal of the shove hack
  (Phase 2). Use before/after contact sheets.
- **Demolition reach.** `DynamicObjectManager` is wired into `ChunkManager` and the frame loop
  (D1, in 1d). Delete in small commits and build and run the bench after each.
- **Lighting occupancy as a shared dependency.**
  - Debris now depends on the lighting occupancy upload; check that no lighting toggle can turn
    it off (1c).
  - Its covered box is camera-centred; debris outside it is frozen and counted.
  - The micro-level escape search in mixed cubes costs more than the old unit lookup; measure it
    in Release.
- **Phase 5 scope** (shipped runtime) is large and touches the game host. It gets its own
  design check and plan.

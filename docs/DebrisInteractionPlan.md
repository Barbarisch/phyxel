# Debris Interaction Plan — everything that moves can push GPU debris

**Status:** rev 4.6, 2026-10-05. **Phase 0 DONE** (main `ed924498`; results under Phase 0).
**Phase 1 in progress** (pushed to main through 1c — 1c DONE):
- 1a build safety ✅ · 1b `shaders/solver_shared.h` ✅
- 1c one occupancy: steps 1–4 ✅ (tri-state query, edit-first repack, debris reads the shared
  pool, old bitfield deleted) · step 5 writer audit ✅ (7 of 8 gaps closed, red→green,
  `OccupancyCoverageTest`; bench `phase1c5` in band) · step 6 ✅ (`DebrisContactOccupancyTest`,
  `occupancy_diff`, asserted by every bench scenario)
- 1c blockers CLOSED: the session-state dependence was two solver leaks (slot order after
  `despawnAll`, warm-start hash never re-cleared); the blast "shift" was that leak — leak-free
  blast is 86.2 mm and a warm session now matches a fresh engine (details under 1c).
- Still open, minor: 1c step 5 gap 8 (incremental add does not filter broken/invisible
  sub-voxels); drop_pile varies run to run (GPU nondeterminism, not session state).
- 1d–1f not started. Phases 2–6 not started (Phase 4 holds the user's "spells don't hit debris").
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
  **never GPU debris**.
  - Up to 12 segment boxes per character, axis-aligned refits of rotated limbs (inflated up to
    about 1.4×), one whole-body velocity (no per-limb velocity), zero velocity while sitting or
    anchored.
  - Update-LOD skips ticks at 30, 60, 120 and 220 m, so obstacles keep a stale pose and
    velocity for up to 0.5 s.
  - Derez leaves a stale obstacle behind.
- **CPU bodies:** furniture (including grabbed and thrown), fragments, felled trees, item props
  and legacy cubes. They collide with each other and with characters, but **GPU debris passes
  through them**.
- **Touch NEITHER world:** doors and `KinematicAnimator` parts (`syncCollidersToPhysics()` is an
  empty stub), held weapons and items, all VFX, CombatSystem melee and knockback, wind, triggers,
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

**1e. Analyzer: external-input window.**
- Add `KINEMATIC_CONTACTS` and `IMPULSES_APPLIED` header counters.
- The judged window starts 0.5 s after the last external input; pushes are reported as "driven".

**1f. Scripted kinematic test box** (`POST /api/debug/gpu_kinematic_box`) isolates solver tests
from NPC AI.

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

## Phase 3 — Feed every mover

- **3a. Characters**: one feed for **all** `AnimatedVoxelCharacter`s (`NPCManager` plus entities
  plus the player), replacing the reassignable `animatedCharacter` pointer.
  - Rotated limb boxes (`SegmentBoxInfo.worldRotation`).
  - **Per-limb velocity** from the pose delta (the CPU obstacles get it too).
  - Fed **after** NPC and entity updates (fixes the one-frame staleness).
  - Characters skipped by update-LOD or derezzing are not fed.
  - Body plans with more than 12 segments are allowed.
- **3b. Held items and doors.** Weapons as kinematic boxes from their bone attachment. Doors and
  `KinematicAnimator` parts as kinematic boxes, **and** implement the empty
  `KinematicVoxelManager::syncCollidersToPhysics()` stub so doors block CPU bodies too.
- **3c. CPU bodies.** Every `VoxelDynamicsWorld` body near awake debris (furniture including
  grabbed, fragments, felled trees, item props), with compound boxes and velocities. Sleeping
  ones are included as supports.
- **Budget:** at most 512 boxes per tick, by distance to awake debris and then to the camera.
  Overflow is **counted and logged once**.

## Phase 4 — Impulses (both worlds)

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

## Phase 5 — Debris in shipped games (separate design check before starting)

`EngineRuntime`/`GameShell` gain `GpuParticlePhysics`, a voxel `DamageSystem`, occupancy (1c),
the mover feed (3) and the impulse hooks (4). The `create_project.py` scaffold and
`minimal_game` follow.
- **L4 on the packaged binary** via the production harness (`--test`, `apply_damage` +
  `settle_probe`).
- The demolition makes this much smaller: there is only one debris world to port.

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
| `crate_through_pile` | 3c | CPU crate launched at 3 m/s through a pile | as `box_through_pile` | 0 displaced | crate missing the pile |
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

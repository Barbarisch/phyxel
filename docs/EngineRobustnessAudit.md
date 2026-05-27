# Engine Robustness Audit

Status: in progress. Focused on physics, collision, character movement/interactions,
and dynamic furniture — anchored on observed defects (character fall-through, dynamic
chairs reappearing after removal, suspected stale world data).

Severity legend: 🔴 confirmed defect · 🟠 latent risk / partial · 🟡 needs verification

---

## ✅ 1. Character falls through the floor — FIXED (2026-05-25)

**Fix:** swept downward ground probe in `resolveKinematicMovement`
(`AnimatedVoxelCharacter.cpp` ~145, ~172). Capture `prevFeetY` before vertical
integration; search `findGroundY` from `max(prevFeetY, worldPosition.y)` with depth
covering the fall, so a floor crossed mid-frame is always detected (immune to fall
speed / frame stutter). The existing snap branch then re-grounds. Verified: NPC spawned
at y=28 lands at y=17; player stays grounded; no -500 runaway.

Remaining (lower severity, not gameplay-breaking): character still rests ~0.2 above the
surface (foot-IK/hip offset) so grounding state can briefly flicker — but it no longer
falls through. Worth fixing at the IK source later.

<details><summary>Original diagnosis</summary>


**Where:** `AnimatedVoxelCharacter::resolveKinematicMovement`
(`engine/src/scene/AnimatedVoxelCharacter.cpp:142–227`) +
`VoxelDynamicsWorld::findGroundY` (`engine/src/physics/VoxelDynamicsWorld.cpp:358`).

Not a collision-grid bug — logs always show a valid `groundY` (16/17), never
"No ground found". Three compounding issues:

1. **Resting float vs tolerance mismatch.** Character rests ~0.13–0.24 above the
   surface (logs: `pos.y=17.196, groundY=17`), but the "still grounded" band is only
   `groundY + 0.05` (line 190). At rest it never cleanly grounds; each frame it can
   fall into the "lose grounding" branch even for a 0.2 drop — far under
   `maxStep=0.444` (`AnimatedVoxelCharacter.h:530`).
2. **Downward-only ground search → tunneling.** `findGroundY` searches only down
   (`queryMax.y = feetPos.y - 1e-4`, line 365). Once feet pass below the floor top in
   one step the floor is invisible → falls forever. Debug-build stutters (145 ms
   frames ≈ 2.7 units/frame at ~19 m/s) trigger this easily.
3. **Recovery gated on already being grounded** (line 193) → a momentary loss becomes
   permanent free-fall.

**Repro:** CharacterTestbed; player stable at y=17, then drifted to y=-500 (free fall).
Spawned NPC at y=28 fell to y=-526. Engine process then exited (possible crash at
extreme coords — see §4).

**Fix direction:** swept grounding (snap when feet cross `groundY` between prev/next Y
regardless of speed); derive grounding tolerance from the actual resting offset rather
than hard-coded 0.05; clamp Y so one step can't pass through a solid surface. Also fix
the ~0.2 resting offset at its source (foot-IK/hip code in the recent
`AnimatedVoxelCharacter.cpp` change).
</details>

## ✅ 2. Dynamic chairs reappear after removal — FIXED (2026-05-25)

**Fix (non-ECS coordination):** (1) `DynamicFurnitureManager::discard()` — tears down an
active dynamic body + render without re-staticizing; (2) `deactivate()` now erases the
active entry up front (avoids re-entrancy) and only re-spawns the template **if the
placed object still exists**; (3) `PlacedObjectManager` gained a `PreRemoveCallback`,
wired in `Application.cpp` to call `discard()` — so *every* removal path tears down
dynamic furniture before the object disappears. Verified: activate chair → remove while
active → dynamic furniture count 0, object gone and stays gone; and the normal
activate→deactivate re-staticize path still restores the object. The full ECS-ownership
version remains the architecture target (see EngineArchitectureAudit.md / pilot).

<details><summary>Original diagnosis</summary>


**Where:** `DynamicFurnitureManager::deactivate`
(`engine/src/core/DynamicFurnitureManager.cpp:259–270`),
`PlacedObjectManager::remove` (`engine/src/core/PlacedObjectManager.cpp:459`).

- `deactivate()` (re-staticize) **always re-spawns the template** back into the world
  (`placeTemplate` line 267 / `spawnTemplate` line 269); the `else` at 269 re-spawns
  **even if the placed object was already removed** (no existence check).
- `PlacedObjectManager::remove()` clears voxels + erases its own entry but **never
  notifies `DynamicFurnitureManager`**. Removing a chair that's active as a dynamic
  body leaves the rigid body + kinematic render alive; any later
  `deactivate()`/`deactivateAll()` (rest, scene change, save) bakes it back in.

**Fix direction:** single removal owner — `remove()` calls `DynamicFurnitureManager`
to discard (not re-spawn), or `deactivate()` checks the placed object still exists
before re-spawning. Add a "discard" path distinct from "re-staticize".
</details>

## 🟠 3. Physics grid registration — dangling-pointer footgun

**Where:** `ChunkPhysicsManager` move ctor/assignment
(`engine/src/physics/ChunkPhysicsManager.cpp:23–46`) registers `&m_occupancyGrid`
with `VoxelDynamicsWorld::m_grids`.

Chunks currently live in `vector<unique_ptr<Chunk>>` (stable addresses), so not firing
today — but move ctor/assignment moves the grid **without updating the registration**.
Any future change storing chunks by value, or move-constructing a `Chunk`, silently
dangles every registered grid pointer → `findGroundY` reads freed memory → everyone
falls. **Fix:** re-register in move ops, or register a stable handle/ID instead of a
raw member address.

## 🟡 4. Engine exit during extreme fall

Engine process (pid 48716) exited when characters reached y≈-500. `cleanupDead`
(`VoxelDynamicsWorld.cpp:416`) kills rigid bodies below `m_fallThreshold`, but the
character is a kinematic capsule, not a `VoxelRigidBody`. Needs confirmation whether
this was a crash (assert/NaN at extreme coords) vs. normal close.

---

## 🟠 5. No format/schema versioning → silent world staleness

**Where:** `VoxelTemplate`, `WorldStorage`, `ChunkStreamingManager` — no `version`,
`magic`, or `FORMAT_VERSION` field anywhere.

Worlds bake placed templates into the DB as static voxels (per CLAUDE.md: "World
terrain pre-baked in worlds/default.db"). When a template's `.voxel` changes (many were
modified in checkpoint 2db4b49), existing worlds keep the OLD baked geometry with no way
to detect or migrate → silent staleness (the "outdated world" symptom).

**Fix direction:** add a schema/format version to the world DB and to `.voxel`/template
files; on load, compare and either migrate or warn. For content drift specifically,
store template name + version per placed object and re-voxelize on load when the
template is newer, or provide a "re-bake world" tool.

## 🟠 6. API thread vs. physics/grid reads — race risk

**Where:** `EngineAPIServer` server thread (`EngineAPIServer.cpp:112`) vs.
`VoxelDynamicsWorld::generateContacts` parallel grid reads (`VoxelDynamicsWorld.cpp:208`).

Most MCP handlers marshal to the main thread via `queueAndWait` (correct). Any handler
that mutates chunks/grids/bodies directly (without the queue) would race the parallel
physics step reading `m_grids`/`m_bodies`. **Action:** audit each handler for direct
mutation; ensure all world/physics mutations go through `queueAndWait`.

(Note: TTS dialogue hook is safe — it fires from the main-thread `applyPendingAIResponse`
consumption point, and `TTSService::speak` enqueues under a mutex.)

---

## 🔴 7. Dynamic voxels fall through each other (collision not always rigid)

**Symptom (user-reported):** collisions feel rigid most of the time, but sometimes
voxel bodies pass through one another.

**Where:** `VoxelDynamicsWorld::generateContacts` (`VoxelDynamicsWorld.cpp:196–204, 287`)
and `substep` (`:135`).

**Primary cause — sleeping bodies are invisible to body-body collision.** The `awake`
list excludes `isAsleep` bodies (line 199), and the body-body broadphase only inserts
*awake* bodies into the spatial hash (line 287). So once a body sleeps
(`SLEEP_TIME=1.2s`, `SLEEP_VELOCITY≈0.02 m/s` — i.e. quickly), it becomes **transparent**:
an awake body falling onto / sliding into a settled (sleeping) stack or resting piece
generates no contact and passes through. This exactly matches "rigid most of the time
(awake-vs-awake), but sometimes falls through (awake-vs-sleeping)." Note the
kinematic-obstacle phase (line 253) *does* wake bodies on overlap — body-body doesn't.

**Contributing causes:**
- **No continuous collision detection.** `substep` integrates velocity → generates
  contacts at start-of-step positions → solves → integrates positions
  (`VoxelDynamicsWorld.cpp:135–152`). A body moving more than a voxel's size per
  substep skips contact generation. At `fixedStep = 1/60`, anything past ~6 m/s tunnels
  a microcube (0.11 m), ~20 m/s a subcube (0.33 m).
- **Substep cap drops time on stutter.** `maxSubsteps = 3` (50 ms max/frame) and the
  accumulator is zeroed when capped (`:131–132`); debug-build frame hitches (145 ms
  observed) under-integrate and enlarge effective per-step motion → more tunneling.

**Fix direction (in priority order):**
1. Include sleeping (non-dead, finite-mass) bodies as collision *targets* in the
   body-body broadphase, and wake them on contact — mirror what the kinematic-obstacle
   phase already does. This is the main "sometimes falls through" fix.
2. Add speculative/swept contacts (or substep-on-fast-motion) for bodies whose
   per-step displacement exceeds their smallest box half-extent — kills fast-motion
   tunnelling. (Same class of bug as the §1 character fix, applied to rigid bodies.)
3. If penetration *recovery* (soft sinking, not full pass-through) is the complaint,
   raise `SOLVER_ITERATIONS` / Baumgarte bias rather than touching broadphase.

## Interaction pipeline — partial

- **Object animation (`KinematicAnimator`) looks robust.** Doors/drawers/lids only;
  idempotent `registerPart`, easing applied to progress fraction (interruptible
  without snapping), engine-agnostic, settle callback. Minor: re-registering a part
  mid-animation resets it to zero (closed) — could pop. Does NOT touch character Y.
- **Character-side interactions** (sit/climb/step) all route through
  `resolveKinematicMovement` → covered by Finding #1. Sit/stand/door FSM transitions
  not yet exhaustively traced.

## 🟡 8. `scan_region`/`query_voxel` under-report sub-voxels (diagnostic gap)

**Where:** MCP `scan_region` / `query_voxel` (chunk voxel query path).

These report only full **cubes**, not **subcubes/microcubes**. A `chair_wood` (built
from subcubes) baked into a chunk is invisible to `scan_region` — it returns only the
full-cube floor. `clear_region` / `clear_chunk` DO act on sub-voxels (confirmed: a region
`scan_region` reported empty still had 4 voxels that `clear_region` removed). Net effect:
you cannot reliably enumerate/locate orphaned furniture remnants via scan — you must
clear by chunk/region. This compounds finding #2/#5: orphaned baked subcubes from the
chair bug had no placed-object entry AND were invisible to the scan tool, so the only
reliable cleanup was `clear_chunk` + regenerate. (Observed 2026-05-25 while cleaning the
CharacterTestbed world.) Fix direction: make scan/query traverse subcube/microcube grids.

## Areas pending (next pass)

- **Character interaction FSM deep-dive** — sit/stand/climb/carry state transitions,
  `contact_rules.json` / `clip_selection.json` data-driven selection, `VoxelContactProbe`
  edge cases. Largest remaining area.
- **Confirm §4** (engine exit on extreme fall — crash vs. clean close).
- **Confirm §6** (enumerate any MCP handlers that bypass `queueAndWait`).

# GPU Compound Rigid Bodies — Phased Design (PLANNED, not built)

**Status: DESIGN 2026-08-07.** User-approved direction: "plan now, build
later." This is the destruction-scale follow-up to the item-physics work —
Phase 7+ of the GPU physics effort (`docs/` GPU physics plan; the XPBD/AVBD
debris pipeline in `GpuParticlePhysics` is Phases 1–6, shipped).

## Why

The CPU `VoxelDynamicsWorld` handles the current item/furniture load after the
2026-08-07 fixes (coarse ≤8-box colliders, substep cap, ≤6 concurrent dynamic
items). What it cannot do is SCALE: coherent destruction — a collapsing
building shedding hundreds of multi-voxel rigid fragments — would drown a
sequential-impulse CPU solver whose narrowphase and 14-iteration solve are
single-threaded per contact list. The GPU pipeline already simulates ~10k
single-box debris bodies; teaching it COMPOUND bodies unlocks:

- mass coherent destruction (fragments stay rigid chunks, not particle spray)
- effectively uncapped concurrent dynamic items (today capped at 6 on CPU)
- physics-heavy set pieces (avalanches of crates, collapsing shelves...)

## What blocks compounds today (verified 2026-08-07)

- `SolverBody` (`shaders/solver_types.glsl:17-33`): ONE OBB per body, and a
  **scalar sphere inertia** (`float invInertia`) — compounds need a tensor.
- `GPUConstraint` (`:39-49`): contact-only (normal + 2 friction). No joints,
  no shape lists.
- **No GPU→CPU readback**: debris renders from GPU buffers and never returns;
  items/fragments need settled poses back (retire, pickup sync, placed pose).
- Narrowphase kernels are body-pair = box-pair; no pair expansion.

## Phases (each independently testable)

### P1 — Full inertia tensor + compound shape storage
- `SolverBody.invInertia: float` → symmetric 3×3 (6 floats world-space,
  rebuilt per step from a body-local tensor + orientation, mirroring
  `VoxelRigidBody::updateInertiaTensorWorld`).
- New shape SSBO: `CompoundBox { vec3 offset; vec3 halfExtents; }` array +
  per-body `{ uint shapeStart; uint shapeCount; }` (single-box debris:
  count = 1 — zero behavior change).
- Broadphase AABB = union over the body's boxes.
- **Side benefit shipped immediately: single-box debris stops rotating like a
  sphere** (visible quality win on existing destruction).
- Test: CPU-reference parity harness — same 2-box compound stepped on CPU and
  GPU, poses within tolerance for 60 steps (the existing debug CSV logging
  path is the readback surrogate until P4).

### P2 — Two-stage narrowphase (pair expansion)
- Stage 1 kernel: per body pair from the broadphase, expand to CANDIDATE box
  pairs with a per-box world-AABB pre-reject; append to a global queue via
  atomics (the same bounded-queue pattern the pipeline's spawn queue uses).
- Stage 2 kernel: SAT per queued box pair (existing OBB-vs-OBB code reused),
  contacts written with (bodyA, boxI, bodyB, boxJ) identity.
- Overflow policy: queue cap with a stats counter — dropped pairs must be
  COUNTED, never silent (lesson from the kinematic face-cap silent drop).
- Test: N-compound stack (e.g. 4× 6-box crates) — contact counts match the
  CPU reference within tolerance; queue-overflow counter zero at design load.

### P3 — Persistent manifolds / warm starting
- GPU hash table keyed on (bodyA, boxI, bodyB, boxJ) carrying accumulated
  impulses across frames (the CPU `m_manifoldCache` analogue). Without this,
  compound stacks jitter — the CPU solver's warm start is why its stacks are
  stable.
- Test: 5-high compound stack settles and SLEEPS (graduated freeze) within
  N seconds; no orbiting/jitter (max velocity after settle < threshold).

### P4 — Pose readback ring
- Fence-synced ring buffer (2–3 frames deep) of `{bodyId, pos, quat, flags}`
  for bodies flagged "of interest" (items/fragments — not anonymous debris).
- 1–2 frame latency is ACCEPTABLE for retire/pickup/placed-pose sync; render
  keeps reading GPU-side (no latency there).
- Test: spawn flagged body, step, assert CPU-visible pose arrives within 3
  frames and matches the debug log.

### P5 — Item/fragment integration
- `ItemPropManager`/`CoherentFragmentService` route eligible bodies to GPU
  when counts exceed the CPU comfort zone (CPU remains the low-count path —
  it has richer contacts with kinematic character obstacles).
- Retire on GPU sleep flag → readback pose → kinematic freeze (existing
  retire flow).
- Rendering compounds: extend the dynamic-voxel instanced path with a
  per-instance body index → fetch pose from the solver buffer in the vertex
  shader (debris already does exactly this for single bodies).
- Test: L4 pile test at 100 compound items — FPS bounded, all settle+retire.

## Non-goals
- Joints/constraints between bodies (doors/ragdolls stay CPU/kinematic).
- Replacing the CPU world — it remains the low-count, gameplay-rich path
  (character interaction, water coupling, occupancy-grid terrain detail).

## Effort shape
Comparable to the near-shadow-cascade effort: ~4–5 focused sessions, one per
phase, each with red-first tests and an L4 checkpoint. P1 pays for itself
immediately (debris rotation quality); P2–P3 are the engineering meat; P4–P5
are integration.

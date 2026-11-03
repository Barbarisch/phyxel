# Collision Proxy Plan — simplified, capped collision shapes for coherent fragments

**Status: PLAN 2026-11-03, not started.** Written while the WorldForge v2 session owns the working
tree; nothing here is built, and no code was touched to write it. Needs `/design-check` and owner
sign-off on §7 before Phase 0 starts.

## 1. Why

Felled trees (and every coherent fragment: collapse chunks, smashed furniture) collide as the
**render voxels, greedy-merged into axis-aligned boxes in the body frame**. That shape is both
expensive and wrong:

- **Expensive.** Real fells carried 46 to ~150 merged boxes (DestructionSystemV2 §5.I). The CPU
  solver's cost is `awake bodies × boxes × (terrain cells + other boxes)`; 400 boxes landing
  together peaked at ~40 ms of `generate_contacts` in one substep (§15.5, after U1a fixed the
  world-size term). The fell itself spiked ~547 ms in `physicalize` (chop-kerf session, July).
  These are July numbers on July code — Phase 0 re-measures them.
- **Wrong.** An axis-aligned merge staircases every diagonal branch into many boxes, and slabs
  merged while the tree stood upright became a measured 6×3 m phantom platform 4–5 m up once it
  fell (`VoxelDynamicsWorld.cpp:903`, `:975`). Big trunks (> 800 cells,
  `DamageSystem.cpp:1510`) pad sub-cell wood to full 1-unit cells, so the collision hull and
  the render disagree (§15.6 C, still open, unmeasured).
- **Blocks the GPU.** GPU compound bodies (the 2026-08-07 plan, `git show
  448a972c^:docs/GpuCompoundBodies.md`) need a small, bounded box count per body. A proxy with a
  hard cap is its prerequisite.

**The lesson (Noita):** the physics never sees the pixels. Marching Squares traces the outline,
Ramer–Douglas–Peucker simplifies it within a tolerance, and Box2D gets a cheap polygon; the pixels
ride along as the visual. Ours is the 3D, box-only version: **the collision shape is a separate,
error-bounded, capped approximation; the voxels are render cargo.**

## 2. What exists today (verified 2026-11-03)

| Piece | Where | Note |
|---|---|---|
| Fell collision list | `DamageSystem.cpp:1505-1530` | subcube-resolution if ≤ 800 cells, else whole-cell; leaves excluded |
| Impact-fracture collision list | `CoherentFragmentManager.cpp:167-195` | one unit box per wood cell; pure-leaf chunk = one bbox |
| Merge | `CoherentFragmentService::mergeVoxelsToBoxes` (`:13`) | greedy +z, +y, +x in the body frame; no tolerance, no cap |
| Mass / COM / inertia | `CoherentFragmentService::physicalize` (`:228-248`) | from the merged collision boxes, not the render voxels |
| Body shape | `Physics::LocalBox` (`VoxelRigidBody.h:13`) | `offset + halfExtents + mass` — **no per-box rotation** |
| Character tests | `overlapsAnyBody`, `findGroundY` | exact OBB per box; cost linear in boxes near the player |
| GPU body | `SolverBody` (`solver_types.glsl:17`) | one OBB, scalar inertia |
| Diagnostics | `GET /api/debug/body_boxes`, `physics_broadphase_stats` | per-box world AABBs; call counts |

Trees lose their generator skeleton once stamped into chunks, and are then carved by chops
and blasts. **The proxy must be built from the voxels at fell time**, not from stored metadata.

## 3. The algorithm — the 3D analogue of RDP

RDP is a top-down recursive split. It fits a segment, finds the worst-fitting point, and splits
there until every piece is within ε. The box version:

1. **Input:** the fragment's structural voxels (wood; leaves stay excluded as today), at micro
   resolution where they have it.
2. **Fit:** one **oriented** box per cluster. Its axes come from PCA of the cluster's voxel
   centres, and its extents are the tight bounds along those axes.
3. **Error:** *phantom* = proxy volume containing no voxel; *miss* = voxel volume outside the
   proxy. Both are measured on the voxel grid, not estimated.
4. **Split:** while a box's error > ε **and** total boxes < cap K, split the worst box's cluster
   at its PCA long axis (or k-means 2). Stop when every box is within ε, or the cap is hit.
5. **Cap honesty:** hitting K with error > ε is **reported** (per-body stats + a counter), never
   silent — the kinematic face-cap silent drop is the precedent we refuse to repeat.

Why oriented boxes matter: a 45° branch is one OBB, not a staircase. The result is also stable
under rotation by construction. That is the property whose absence created the phantom platforms.

**Mass properties come from the full voxel set**, not the proxy. The proxy only decides contact,
so a sparse proxy can't make a tree tip over wrongly. This changes `physicalize`, which today
derives mass from the merged boxes.

**Known alternative considered.** Store each tree's generator skeleton and fit boxes per
RDP-simplified branch segment. Rejected as the primary path: carving invalidates it, templates
(pool mode) don't carry one, and it doesn't help non-tree fragments. Recursive PCA covers both.

## 4. Phases

Each phase lands red-before-green, with a solution-auditor check on its "works" claim.

### Phase 0 — Baseline on today's code (measurement only)
- **Rig:** a new flat test world. One chunk-sized stone floor, one `forge_oak_l` (the tree that
  surfaced §15.6), felled deterministically by an `/api/damage/apply` disc, not by a swing.
  Release build. No other bodies.
- **Record:**
  - box count per body (`body_boxes`);
  - `physicalize` ms;
  - `generate_contacts` ms and frame time over the fall and the settle window;
  - time to sleep;
  - **phantom volume and miss volume** of the current proxy, computed offline from the body's
    voxels and boxes.
- **Stress points:** 1, 4 and 16 simultaneous fells, plus three species (oak, birch, a conifer)
  and one partly carved trunk.
- Output: `docs/evidence/collision_proxy/baseline.json`. These numbers set the targets in §7.

### Phase 1 — `CollisionProxyBuilder` (pure, unit-tested, no engine wiring)
- `buildProxy(voxels, {epsilon, cap}) → {boxes (OBBs), phantomVol, missVol, capHit}`. It is a
  pure function, deterministic for a given input order.
- **Red tests first** (they run against the current `mergeVoxelsToBoxes` and fail):
  - *diagonal branch* — a 1-voxel-thick rod at 45° must take ≤ 2 boxes with phantom ≤ ε;
    greedy needs one box per step;
  - *rotation invariance* — the same tree rotated 0/30/45° gives box counts within ±1 and errors
    within ε;
  - *upright-slab phantom* — the measured crown case, phantom ≤ ε after a 90° fall pose.
- **Invariants:** the cap is never exceeded; capHit is reported; miss ≤ ε (contact must not fall
  through visible wood); deterministic.

### Phase 2 — Oriented local boxes in `VoxelDynamicsWorld`
- `LocalBox` gains a local rotation, and the world box is `bodyRot * localRot`. Touch points:
  contact generation (`generateOBBvsAABB`, OBB-OBB), `findGroundY`, `overlapsAnyBody`,
  `body_boxes`, `DebrisMoverFeed` (GPU kinematic boxes are already OBBs), and inertia
  accumulation.
- An identity rotation must stay bit-for-bit compatible with today: existing physics tests and
  the debris settle bench run unchanged.
- **Red test:** a body with one 45° local box rests on the floor at the right height. Today the
  type cannot express it at all.

### Phase 3 — Wire the proxy into fells, fractures and furniture shatter
- `physicalize` takes the proxy instead of greedy boxes, and mass properties from the full
  voxels. Both collision-list builders (`DamageSystem` fell, `CoherentFragmentManager`
  fracture) feed the builder, and the 800-cell whole-cell padding goes away.
- **L4** on the Phase 0 rig, same pose and stress points:
  - box count and contact ms against the baseline;
  - render-bottom vs collision-bottom gap (§15.6 C), measured;
  - drop-probe height map over the fallen tree, with no point higher than the tree's top
    voxel at that spot. That is the phantom-platform invariant, measured with the existing
    drop-probe method.
- Visual: before/after captures at one stated pose, with the fallen tree in frame and a
  `body_boxes` overlay.

### Phase 4 — Retirement and the population cost
Settled fragments already retire (§5.G) to static. Confirm that a retired proxy-based fragment
re-bakes identically. Measure character-query cost with 16 settled fells near the player.

### Phase 5 — GPU compound bodies (separate plan, revived)
With a capped proxy, the 2026-08-07 P1–P5 plan becomes tractable:
- inertia tensor and shape SSBO;
- pair-expansion narrowphase;
- persistent manifolds;
- pose readback;
- routing.

It needs its own `/design-check`. The 2026-10-03 "keep the split by role" decision has to be
revisited explicitly for each body class (see §7, decision 4). Out of scope here beyond making
the proxy GPU-ready: an OBB list, ≤ K boxes, and mass properties precomputed.

## 5. Design keys (FeatureDesignKeys gate, pre-answered)

- **Pipeline fit:** it lives in the destruction pipeline, between support-flood release and
  `physicalize`. Generation is unchanged.
- **API:**
  - `body_boxes` gains per-box rotation, plus per-body phantom/miss/capHit;
  - new `POST /api/debug/collision_proxy {epsilon, cap}` runtime A/B switch, with defaults
    pinned by a test;
  - a stats counter for cap hits.
- **Visual test:** a `body_boxes` overlay on the fallen tree, before and after, at the same pose.
- **Small test world:** a one-chunk flat stone floor, one tree, a disc fell, Release.
- **Chunks invisible:** proxy shape depends only on the fragment's voxels, never on chunk
  boundaries. Pin it with a test that builds the same fragment straddling a chunk seam.

## 6. Validation layers

| Placer / logic | Contract | Required | Red test |
|---|---|---|---|
| `CollisionProxyBuilder` | ≤ K boxes; phantom, miss ≤ ε or capHit reported; rotation-stable | L2 | diagonal rod, rotation, upright slab |
| Oriented `LocalBox` | identical results at identity; correct rest at 45° | L2 + existing suites | 45° box rest height |
| Fell integration | no phantom standing surface; contact cost ≤ target at 16 fells | L3 (drop-probe) + L4 | drop-probe map vs top-voxel map |

## 7. Decisions for the owner (before Phase 0)

1. **Box cap K.** It is set from Phase 0's measured distribution, not picked now. Suggest
   reading it off the box count where phantom/miss plateau on the three species.
2. **ε (tolerance).** Is it expressed as a fraction of fragment volume, or as an absolute
   distance (e.g. one subcube, ⅓ u, the finest collision step characters already feel)?
3. **Scope of Phase 3.** Trees only first, or trees + collapse fragments + furniture shatter
   together? They share one builder, but each needs its own L4 check.
4. **The GPU question.** Is moving coherent fragments to the GPU (Phase 5) a goal this plan should
   serve? If yes, the 2026-10-03 split decision gets re-opened, deciding per body class whether
   2-frame latency is acceptable. If no, Phase 5 is dropped and this stays a CPU quality and
   performance fix.

## 8. Sequencing with WorldForge

Phases 0–1 need only a new test world and a new pure library + tests. They don't touch any file
the WorldForge v2 session is modifying, but they do need a build and an engine run. Start them
in a **separate git worktree** (off `main`) so the two sessions never share a build directory or
an engine port. Phase 2 touches `VoxelDynamicsWorld` and must pass `tools/debris_settle_bench.py`
(the mover feed changes), so keep it off any branch WorldForge is merging into until it's green.

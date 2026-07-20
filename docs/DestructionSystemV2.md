# Destruction System v2 — Breakable Objects (design)

> Status: **design, awaiting build-order approval** (2026-07-14). Supersedes and continues
> [`docs/DestructionSystem.md`](DestructionSystem.md) (the P1–P5 roadmap; P1–P3 shipped). This
> document extends that energy/toughness core into a **general, tactile breakable-objects system**
> spanning trees, structures, furniture, and terrain — the four "feel" qualities the user asked for:
> **coherent fracture/topple · progressive damage · tool-driven impact · gatherable aftermath.**
>
> **Nothing here is built yet.** This is the plan we review before writing code. Numbers marked
> *(needs grounding)* are placeholders to be set by the grounding-auditor, not decisions.

---

## 1. Goal & scope

Make things in the world break in a **realistic, tactile** way — and mean four specific things by that:

| Feel quality | What it means | Today |
|---|---|---|
| **Coherent fracture / topple** | A cut tree falls as a *trunk*; a severed wall drops as a *slab* — pieces stay rigid, tumble, and settle. Not a particle spray, not a vanish. | **Missing** (roadmap P5). Severed pieces scatter as independent particles. |
| **Progressive damage** | Cracks appear, hits accumulate, materials chip / splinter / powder differently. You feel each blow land. | **Partial.** Accumulation model exists (cube-only); *visible* cracks (P4) do not. |
| **Tool-driven impact** | What you hold matters (axe vs pick vs fist); a real swing with wind-up + impact, not an instant delete. | **Missing.** Break is a keypress with (mostly) zero impulse; no tool affinity, no swing. |
| **Gatherable aftermath** | Broken things leave *persistent, pickup-able* resources (logs, rubble, shards) that feed crafting/survival. | **Missing.** Debris times out (25–30 s) and vanishes. |

Scope — **all four** thing-types must be breakable, but their representations differ, so each needs a
different amount of object-awareness work (§6):

- **Trees & flora** — the driving case (Emberwake woodcutting). Anonymous baked voxels.
- **Structures & walls** — bbox-tracked shells; walls are anonymous voxels.
- **Furniture & props** — already the most object-aware (furniture shatter works).
- **Terrain / ground** — anonymous chunk cells; energy-damage already breaks it.

This is **engine-generator work**, not hand-placement: everything routes through the destruction
service and physics, never through hand-authored per-case scripts.

---

## 2. Current state (the honest baseline)

What is **shipped and working** (cited, verified from source):

- **Energy-vs-toughness core** — `DamageSystem::applyDamage(center, radius, energy, type, dir, supportY, collapse)`
  (`engine/src/core/DamageSystem.cpp:91`). Two-phase: scan+decide against the pre-blast grid (so
  shielding is instantaneous), then apply removals + spawn debris. Radial falloff + ray-marched
  shielding (`solidVoxelsBetween`, `:51`).
- **Per-voxel toughness & tiered shatter** — overkill ratio `r = energy/toughness` picks intact cube
  (`r < s1`) → subcubes 1/3 (`s1 ≤ r < s2`) → microcubes 1/9 (`r ≥ s2`). `responseFor()` (`:24`).
- **Damage accumulation** — `Cube::addDamage` / `getAccumulatedDamage`; sub-threshold hits chip
  through over multiple blows (`DamageSystem.cpp:168`). **Cube-only** (sub-voxel cells break in one pass).
- **Structural collapse** — `collapseUnsupported` (`:335`) flood-fills connectivity from the rim of the
  hole; a component that can't reach the main mass (floods past `MAX_FLOOD=3000`) or a designer anchor
  (`supportY`) is severed and dropped. **But** the drop is `dropDetachedCell` (`:302`) = independent
  particle scatter, **not** a coherent falling piece.
- **Furniture coherent fracture** — `DynamicFurnitureManager::shatter` (`DynamicFurnitureManager.cpp:644`):
  `findConnectedComponents` (BFS, `:549`) → `mergeVoxelsGreedy` → `VoxelDynamicsWorld::createBody`
  (compound `VoxelRigidBody`). **Fragments ≥ `MIN_FRAGMENT_VOXELS=4` become real rigid bodies that
  fall coherently.** Smaller ones become GPU particles. Capped `MAX_DYNAMIC_FURNITURE=16`.
- **Coherent-fragment primitive** — `VoxelRigidBody` (`engine/include/physics/VoxelRigidBody.h`): a
  compound of `LocalBox`es with computed COM/mass/inertia, stepped by the CPU `VoxelDynamicsWorld`.
  "A single broken voxel has one LocalBox. A piece of furniture has several" (`:12`).
- **Debris backends** — `GpuParticlePhysics` (Vulkan AVBD, warm-started, 10 000 particles / 60 000
  faces, the scalable path) and `VoxelDynamicsWorld` (CPU sequential-impulse, soft ~500 bodies).
- **Render density** — the ~412k-face wall is **mostly resolved**: sub/micro greedy-merge is default-on
  (`ChunkRenderManager.cpp:25`), ~5–8× recovery. *Live* debris uses the dynamic pipeline (particle/
  kinematic), not the static greedy-mesher.

**The single most important fact for this design:** the *coherent falling piece* mechanism the user
wants **already exists** in `DynamicFurnitureManager::shatter`. It is fenced inside furniture. The core
of v2 is **generalizing it** and routing `collapseUnsupported`'s severed components into it instead of
`dropDetachedCell`.

Known dead/orphaned code (do not build on): the `Cube::Bond` 6-direction graph
(`engine/include/core/Cube.h`) is only consumed by the dropped `ForceSystem` mouse path — it is **not**
wired into any live break decision. `DestructionSystem.md` chose flood-fill connectivity over bonds;
v2 keeps that choice (§9).

---

## 3. The gap — what "realistic tactile" needs that's missing

1. **Coherent fragments for the WORLD, not just furniture.** `collapseUnsupported` identifies the
   severed set correctly; it just needs to hand that set to the furniture-style `createBody` path.
2. **Object-awareness for trees.** A tree is anonymous log/leaf voxels (`ObjectTemplateManager.cpp:430`
   stamps flora straight into chunks; no registry entry). To "fell a tree" we must identify the
   connected tree voxels at runtime (flood-fill). **Caveat (audited, §14-H1):** the existing collapse
   flood is *terrain-tuned* — a component that exceeds `MAX_FLOOD=3000` is treated as the supported main
   mass, so a large tree/canopy severed at the trunk would be **misclassified as ground and refuse to
   fall**. Tree felling therefore needs its own object-aware identification (flood by material/tag with
   its own budget), not a straight reuse of `collapseUnsupported`.
3. **Directional topple, not just fall.** A rigid body severed at its base and released under gravity
   already gets angular velocity from its offset COM, but a tree should *hinge* about its cut, not
   free-drop. Needs a small torque/pivot bias.
4. **Data-driven material break behavior.** Only 5 materials (Stone/Glass/Wood/Metal/Dirt) have tuned
   `toughness/s1/s2/absorption`, hardcoded as C++ `if`-chains (`DamageSystem.cpp:37-47`). The other
   ~96 fall back to `bondStrength*120`. These belong in `materials.json`.
5. **Gatherable settle.** Debris must be able to *become* a persistent resource item on settling,
   instead of always timing out.
6. **Tool + swing layer.** An equippable tool that drives the damage energy, plus a swing animation and
   an impact event (ties the open axe/swing feedback, `docs/feedback/inbox.md:15`).
7. **Visible progressive damage.** Cracks/darkening on damaged-but-unbroken voxels (P4).

---

## 4. Architecture — the unifying model

A single pipeline serves all four feels × four scopes. The insight is that **"break" is always the
same three-stage flow**, and the four feels are layers on it:

```
   DAMAGE SOURCE                 FRACTURE DECISION            PHYSICALIZE                AFTERMATH
   ─────────────                 ─────────────────            ───────────                ─────────
   tool swing / spell / ──►  DamageSystem::applyDamage  ──►  CoherentFragmentService ──►  settle → resource
   collision / explosion         (energy vs toughness,        (voxel set → compound        item (gatherable)
        │                         tiered shatter,              VoxelRigidBody) OR            OR fine debris
        │                         connectivity collapse)       GpuParticle scatter          times out
        │                              │                          │  (budget-gated)
        └── tool affinity ────────────┘                          └── render: kinematic
            (axe→Wood ×N)                                             faces / particles
                                       │
                                  Cube damage state ──► crack/darken shader (progressive)
```

**Four new/changed pieces** (everything else reuses shipped code):

- **(A) Data-driven `MaterialBreakProfile`** — move `toughness/brittleness/absorption` (and a new
  `resourceYield`) into `materials.json`; `DamageSystem::responseFor` reads them; delete the hardcoded
  exemplars. *Foundation — unblocks tuning all 101 materials and grounding.*
- **(B) `CoherentFragmentService`** — extract the furniture "connected voxel set → compound
  `VoxelRigidBody`" routine (`findConnectedComponents` + `mergeVoxelsGreedy` + `createBody`) into a
  standalone service callable from **world voxels** (not just kinematic furniture voxels). One code
  path, one body budget, used by furniture shatter AND world collapse. *The core new capability.*
- **(C) Fracture routing in `collapseUnsupported`** — severed component whose size ≤ budget →
  CoherentFragmentService (falls as one rigid slab/trunk); over budget or tiny → existing
  `dropDetachedCell` scatter (unchanged fallback). *Delivers toppling walls & trees.*
- **(D) `BreakAftermath` (gatherable)** — a settled fell persists as a full physical object; harvesting
  it (chop/gather) emits **resource items** per the material/object yield table. *Delivers the survival
  loop.*
- **(G) Fragment retirement tier** — settled fells FREEZE out of the physics solver to functionally-
  static (grid-free: arbitrary transform kept, faces merged, occupancy stamped) and reactivate lazily on
  interaction. *The scalability key — makes a large permanent population of full-fidelity fells
  affordable without downgrading them (§5.G).*

Plus two **feel layers** on top:

- **(E) Tool + swing interaction** — `AxeTool` (RpgItem) + a chop/swing animation family + an impact
  hook that computes energy from `tool × material affinity` and calls `applyDamage` at the hovered
  voxel. Progressive multi-hit.
- **(F) Damage visualization** — per-`Cube` normalized damage (0..1) surfaced to `static_voxel.vert` /
  `voxel.frag` as a crack overlay / darkening (P4).

---

## 5. Component designs

### (A) Data-driven `MaterialBreakProfile`
Add to each material's `physics` block in `resources/materials.json`:
```jsonc
"break": {
  "toughness":   110.0,   // energy to break one voxel   (needs grounding per material)
  "brittleS1":   1.8,     // overkill ratio → subcubes
  "brittleS2":   4.0,     // overkill ratio → microcubes
  "absorption":  0.9,     // shielding loss per solid voxel
  "resourceYield": { "item": "log", "perVoxel": 0.02 }  // §D (needs grounding)
}
```
`responseFor()` reads `def->break.*` with the current `bondStrength*120` formula as the fallback for
materials without a `break` block. Delete the 5 C++ exemplars. **Validation:** L2 unit test — a
material's JSON `break` block round-trips to the exact `MatResponse` (red: today `responseFor` ignores
JSON; green: reads it). Grounding-auditor sets the 101 values before ship.

### (B) `CoherentFragmentService` — the core
A standalone service (`engine/{include,src}/core/CoherentFragmentService.*`) with one primary entry:
```cpp
// Turn a connected set of FINE voxels into a coherent falling rigid body.
// `voxels` are individual cubes/subcubes/microcubes (each with its own scale +
// local offset), NOT whole-cell placeholders — see "Voxel tiers" below.
// Returns the created VoxelRigidBody id, or nullopt if over budget / too small
// (caller then falls back to particle scatter).
std::optional<uint32_t> physicalize(
    const std::vector<KinematicVoxel>& voxels,  // mixed cube/subcube/microcube, local-space
    const glm::vec3& initialVelocity,
    const glm::vec3& initialAngularVel,
    const FragmentBudget& budget);
```
Internals follow `DynamicFurnitureManager::shatter:705-771`:
`mergeVoxelsGreedy(voxels)` → `LocalBox`es around the COM → `voxelWorld->createBody(...)` → render via a
`KinematicVoxelObject` (`KinematicVoxelManager::add`). `DynamicFurnitureManager::shatter` is then
**refactored to call this service** so there is exactly one implementation. **Do NOT lift the tuning
verbatim (audited, §14-H4):** the furniture path bakes in furniture-scale constants — mass clamp
`[0.5, 10.0]`, raw-mass scale `0.05`, scatter `×2.0`, fixed `+(0,1,0)` pop (`DynamicFurnitureManager.cpp:726,755,757`).
A redwood trunk under those caps to 10 mass and hops like a stool. Mass/velocity must be **parameterized
by fragment scale**, not copied. This also means the "one implementation" is really *one back-half
(`physicalize`) + two front-halves*: furniture supplies already-kinematic voxels; world collapse must
gather sub-voxel geometry from chunks (above).

**Voxel tiers (cube / subcube / microcube) — explicit.** The compound-body + kinematic-render path is
already resolution-agnostic: `KinematicVoxel` and `LocalBox` each carry a `scale`, and furniture
fragments already comprise subcube/microcube geometry. The subtlety is the **source**: furniture
rebuilds fine geometry from its template, but a WORLD fragment (a tree, a subcube wall) has no template.
So collapse routing (C) must **gather the real sub-voxel geometry out of the chunk** — for each detached
cell, enumerate its actual subcubes/microcubes (`Chunk::getStaticSubcubesAt` + the microcube accessor),
emit one `KinematicVoxel` per fine voxel at its correct local offset+scale, and pass the whole set to
`physicalize`. This is the difference between a toppling tree that *looks like the tree tipping over*
(its own subcubes/microcubes preserved) and one that turns into a blocky proxy. It also means a fragment
body can carry many small boxes — feeding the render-face and body-mass budgets (§8).

`FragmentBudget` governs the CPU-body cap (§8): max active coherent bodies, max voxels/body, and an
over-budget policy (fall back to `GpuParticle` scatter — the current behavior). **Validation:** L2
(fragment geometry == input cells, mass/COM/inertia finite & centered), L3 (`TraversalProbe`/settle
sim: the body falls, contacts terrain, comes to rest without interpenetration or NaN), L4 (runtime).

### (C) Fracture routing in collapse
In `collapseUnsupported`, when a component is detached (`:398`), instead of the per-cell
`dropDetachedCell` loop:
1. remove all component cells from chunks (as today, so occupancy clears — both grids, §8),
2. if `component.size() ≤ budget.maxVoxelsPerBody` and a body slot is free → gather the removed cells
   as `VoxelCell`s and call `CoherentFragmentService::physicalize(...)` with a downward + slight
   outward velocity;
3. else → existing `dropDetachedCell` scatter (unchanged).

This is **behind a flag** (`applyDamage(..., coherentFragments=true)`) so the old scatter remains the
tested fallback and A/B is possible. **Validation:** L4 — `apply_damage` mid-height on a stone wall →
the top section topples as ONE rigid slab (assert: 1 new rigid body, voxel count == severed count),
vs. the flag off (N particles).

### (D) `BreakAftermath` — the settled object IS the gatherable (harvest by interaction)
**Decision (§11.2):** a settled piece does **not** auto-compact into a loose item pile; it **stays a
full physical object** (persistent sleeping `VoxelRigidBody`, `lifetime=FLT_MAX`, like furniture at
rest). So the aftermath is not a conversion event — it's an **interaction target**:
- On settle (`VoxelRigidBody::isAsleep`, `SLEEP_TIME=1.2s`), the fell body is registered as a persistent
  `PlacedObject` (category `item`/`debris`) so it survives, is addressable, and carries its
  `MaterialBreakProfile.resourceYield`.
- **Harvesting** the settled object (chop it further with a tool, or a "gather" interaction) removes
  voxels from it and emits `floor(harvestedVoxels × perVoxel)` resource items into the inventory /
  as pickup props (item props are never chunk-baked — `ItemPropManager.h:26` — so they persist). A
  whole felled trunk thus becomes logs *as you work it*, not instantly.
- **No lifetime timeout** for settled coherent objects — they persist until harvested or destroyed
  (this is what "stay a full physical object" requires). Fine *scatter* debris (the < MIN_FRAGMENT
  particle fallback) still times out as today.

**Cost this imposes (see §8):** every unharvested fell permanently holds a body slot + render group, so
the fell/harvest budget is now a persistent-population problem, not a transient one. **Validation:** L4
loop — fell tree → persistent settled log object → harvest → inventory count == yield, body slot freed
only on full harvest.

### (E) Tool + swing (ties feedback #1)
- **Item** — an `axe` `RpgItemDefinition` (`resources/rpg/`) with a `templateFile` model and break-
  relevant stats (a `chopPower` + optional per-material affinity table, e.g. axe×Wood ≫ axe×Stone).
- **Animation** — a chop/swing `.anim` family (wind-up → impact → recover), alongside the existing
  melee families (`docs/` melee_anim_families).
- **Impact hook** — on the impact frame, raycast from the tool, and at the hit voxel call the damage
  path with `energy = chopPower × affinity(tool, materialAtHit)`. **Progressive model (audited, §14-H3):**
  `Cube::addDamage` accumulation is **cube-only** — trees are sub-voxel cells with no accumulation field,
  so multi-hit chopping of a tree cannot reuse it. Model chopping **geometrically instead**: each swing
  carves a wedge of trunk voxels, and the sever/topple triggers when the trunk cross-section is fully
  cut through (a connectivity event), not when accumulated energy crosses a threshold. (Cube-only
  accumulation still serves full-cube targets like walls.)
- Left-click is already melee/attack (`InputController.cpp:361`); the axe swing slots into that verb
  when an axe is equipped. **Validation:** L4 — equip axe, swing at tree, assert N swings sever it
  (matches the material toughness), fists do not.

### (F) Damage visualization (P4)
`Cube` already stores accumulated damage. Surface a normalized `damage01 = accumulated/toughness` into
the static voxel instance data (spare bits or a parallel per-voxel buffer) and blend a crack overlay /
darken in `voxel.frag`. Purely additive; no gameplay change. **Validation:** L2 (damage state correct
after N sub-threshold hits) + L4 visual (`get_visual_diagnostic` before/after: pixels change on a
grazed voxel; the framed-demo integrity rule applies — confirm by pixel diff, not "looks cracked").

### (G) Fragment lifecycle & retirement — "cheap at rest" (the scalability key)
The mechanism that makes a large *permanent* population of settled full-fidelity fells affordable. A
coherent fragment moves through three states:

- **ACTIVE** — a live `VoxelRigidBody` in `VoxelDynamicsWorld`: simulated, in broadphase (falling,
  tumbling, being pushed). Counts against the CPU-body budget.
- **SLEEPING** — came to rest; body retained but skipped in the solver (`VoxelRigidBody::isAsleep`),
  still in broadphase so a contact can wake it. A short transition state.
- **FROZEN / RETIRED** — after sleeping stably & untouched for `T_retire` seconds: **removed from
  `VoxelDynamicsWorld` entirely** (frees the body slot AND the broadphase entry), its kinematic faces
  greedy-merged once, its rest-pose footprint stamped into a collision-only occupancy region, and its
  geometry+transform kept as plain data. A frozen fragment is now **functionally static** — it costs
  roughly what static geometry costs (merged faces to draw, occupancy to stand on, zero solver/
  broadphase cost). This is a **grid-free** freeze: the arbitrary rest transform is preserved (unlike
  furniture re-staticization, which snaps to the chunk grid).

**Reactivation is lazy & on-demand.** A frozen fragment rebuilds into an ACTIVE `VoxelRigidBody` from
its stored geometry the instant something interacts with it — a tool chop, a push, or a nearby
blast/impact (AABB/raycast test) — clearing its occupancy stamp on the way. So the object stays a
*permanent, full, interactive physical object* (satisfying the §9 decision) while only paying live-
physics cost during the brief windows it's actually moving.

**Why this is the budget fix:** the CPU-body ceiling now bounds only **ACTIVE + SLEEPING** (a small,
transient count), NOT the total settled population — which is instead bounded by render (merged faces)
and occupancy/memory, far higher ceilings. This is a generalization of the existing furniture
`activate()`/`deactivate()` pattern (`DynamicFurnitureManager`), made grid-free.

**Bounded subtlety — reactivation cascade:** waking one fragment in a stack must wake the frozen ones
resting on/against it (a neighbor-overlap query at reactivation), or piles won't collapse correctly.
Bound the cascade depth per interaction. **Validation:** L2 (frozen fragment has 0 solver/broadphase
presence; face count drops after merge; occupancy matches rest pose) + L3 (chop a frozen log →
reactivates → moves; a stack partially collapses) + L4 (accumulate M frozen fells → FPS ≥ threshold and
active-body count stays near zero at rest — the persistent-population gate, §12).

---

## 6. Per-scope behavior

| Scope | Object-awareness needed | How it breaks in v2 |
|---|---|---|
| **Terrain** | None (anonymous cells). | Energy damage as today; large severed overhangs *can* now topple coherently (C) within budget, else scatter. |
| **Structures / walls** | Low — flood-fill already severs sections. | A blasted/undermined wall section topples as a coherent slab (B+C). Structure bbox stays in `PlacedObjectManager` but its voxels are gone — **decide** whether to update/retire the registry entry (§11). |
| **Furniture** | Already object-aware. | Existing `shatter` refactored onto CoherentFragmentService (B); gains gatherable aftermath (D). Mostly consolidation + polish. |
| **Trees / flora** | **High** — must identify "the tree" at runtime. | Chop accumulates damage at the cut (E); once severed, flood-fill the connected log/leaf set above the cut and topple it as one body (B+C) with a hinge bias (§3.3); settle → logs (D). |

Trees are the hardest and the driving case, so they get the vertical slice first (§10 Phase 2).

---

## 7. Data & grounding

Everything sized must cite a real source (project HARD RULE — grounding-auditor). Values needing
grounding **before** the relevant phase ships:

- **Material break profiles** (toughness/brittleS1/S2/absorption) for all 101 materials — extend the
  5 tuned exemplars, ground the rest against relative real-world hardness/toughness (e.g. wood vs stone
  vs glass fracture energy ratios), not grid-convenience.
- **Resource yields** — how many logs a tree of a given voxel count yields; how much rubble a wall
  section yields. Ground against tree-volume→timber and masonry conventions, not vibes.
- **Tool chop power & affinities** — axe-vs-wood effectiveness, swings-to-fell. Ground against the
  chosen toughness scale so "N swings to fell a small oak" is a deliberate, defensible number.
- **Fragment budgets** — max coherent bodies, max voxels/body: ground against the measured CPU
  physics ceiling (§8), not guessed.

The doc does **not** invent these; it names them as grounding tasks.

---

## 8. Performance & constraints (hard limits this design must respect)

- **CPU rigid-body ceiling** — `VoxelDynamicsWorld`'s only recorded ceiling is a **Debug-build,
  synthetic worst-case** (500 bodies piled at one point = 0.7 FPS, `docs/DynamicVoxelPhysics.md:141`,
  via `tools/perf_stress_test.py --mode voxel`). This is **not** a Release number, **not** a regression
  test, and the same doc notes real-gameplay limits are "significantly higher." So treat "~500" as an
  order-of-magnitude prior, **not** a measured hard ceiling. Coherent fragments are CPU bodies → **a
  forest of simultaneously toppling trees could still blow whatever the real ceiling is.** **The
  primary mitigation is the retirement tier (§5.G):** settled fells FREEZE out of the solver, so this
  ceiling bounds only ACTIVE+SLEEPING bodies (a small transient count), not the total settled
  population. On top of that, `FragmentBudget` caps concurrently-active coherent bodies; over budget →
  particle scatter or a cheap "fade+poof"; distant trees fell with a reduced-fidelity path (LOD).
  **Grounding gate (Phase 1 prerequisite):**
  before `FragmentBudget.maxActiveBodies` is set, re-run the stress test against a **Release** build and
  encode the result as an automated assertion in `tests/benchmark/` (so the ceiling can't silently
  drift the way a hand-copied doc number can). This budget is a first-class design constraint, not an
  afterthought.
- **GPU particle cap** — 10 000 particles / 60 000 faces. The scatter fallback lives here; fine.
- **Render density (resurfaces in the KINEMATIC pipeline).** The static greedy-mesh fix does **not**
  cover fell fragments: coherent bodies render as **kinematic faces** (`KinematicFaceData`, pre-computed
  per-face, **not** greedy-merged). With the chosen model (full-fidelity + *permanent* settled objects),
  a forest floor of felled full-geometry logs accumulates un-merged kinematic faces **forever** — the
  412k-face problem returns via a different pipeline. **New required deliverable:** once a fragment is
  permanently at rest (geometry frozen), **greedy-merge its kinematic faces in place** (a one-shot
  consolidation — still a dynamic object at an arbitrary transform, just cheaper to draw). This is the
  render answer that lets "permanent full-fidelity settled" be affordable. The distance/budget LOD
  (proxy geometry for far/over-budget fells) is the other half.
- **Standable settled fragments — ✅ RESOLVED via (a), 2026-07-16. See §15.1.** *(Original framing,
  kept for context: the character grounds on the static occupancy grid, not on dynamic bodies, so
  "must be standable/blocking" (§11.2b decision) needs one of: (a) character↔`VoxelRigidBody` OBB
  collision (larger lift), or (b) a settled-footprint occupancy stamp — axis-aligned approximation.
  This section recommended (b).)* **What actually shipped was (a)** — `b104101` then `86d2d85`:
  character queries are exact against the body's ORIENTED boxes (OBB blocking + local-frame down-ray
  grounding). Better than the recommendation: the standable surface is the true angled geometry, not
  a grid approximation. The (b) stamp is **not** dead — it returns in §15.3 U5, scoped to *frozen*
  fragments, which by definition have left the solver and so cannot be queried as bodies.
- **Two occupancy grids** — every voxel removal must update **both** the CPU grid and the GPU grid
  (`updateOccupancyVoxel` already does; any new removal path must too) or debris/characters fall
  through the world (`docs/AgentContext.md` invariant).
- **Settling "popcorn"** — the debris no-sleep/popcorn problem is a known *parked* issue; coherent
  bodies use `VoxelRigidBody` sleeping (`SLEEP_TIME=1.2s`) which is better-behaved, but the aftermath
  conversion (D) must trigger on real sleep, and we should watch for coherent bodies that never settle.

---

## 9. Design decisions (carried forward + new)

Carried from `DestructionSystem.md` (unchanged): radial+shielding propagation · damage accumulation ·
anchor = connected-to-main-mass · everything destructible by default · `ForceSystem` mouse path
dropped.

New for v2:
- **Flood-fill connectivity, not the `Cube::Bond` graph.** Bonds are orphaned and redundant with
  occupancy flood-fill for connectivity. Reviving them is *not* a prerequisite for coherent fragments
  (furniture proves this — it uses component BFS, not bonds). Bonds could later encode *pre-authored
  weak points* (a tree's designed cut-line) but that is an optional refinement, not v1.
- **One coherent-fragment implementation.** Furniture shatter and world collapse must share
  `CoherentFragmentService` — no second copy of the merge/createBody logic.
- **Coherent fragments are opt-in per call** (flag), so the shipped scatter path stays the tested
  fallback throughout.

Locked by the 2026-07-14 decisions (user chose the highest-fidelity option on each):
- **No re-bake** — settled material stays in dynamic/kinematic space; the integer static grid would
  misalign an arbitrarily-angled rest pose.
- **Standable/blocking settled fragments** — requires the settled-footprint occupancy stamp (or
  character↔rigid-body collision); now a required deliverable, not a mitigation (§8).
- **Settled = permanent full physical object** — no lifetime timeout; each unharvested fell holds a
  body slot + render group, so the budget is a *persistent-population* problem. Resources come from
  **harvesting** the settled object (interaction), not an auto-conversion on sleep.
- **Full-fidelity topple + distance/budget LOD** — real subcube/microcube geometry preserved; far or
  over-budget fells use a proxy. Forces the kinematic-face freeze-time greedy-merge (§8).

---

## 10. Phased roadmap (proposed — this is the "then decide")

Each phase: functional contract · required validation layer (L1 artifact / L2 structural invariant /
L3 agent-simulation / L4 runtime) · a red-before-green test · a scale/stress test · solution-auditor
sign-off. Ordered so the load-bearing engine work (0–1) precedes the feel layers.

- **✅ Phase 0 — Data-driven material break model (A). SHIPPED 2026-07-14 (solution-auditor PASS).**
  `MaterialRegistry` parses an optional per-material `"break"` block (`toughness`/`brittleS1`/
  `brittleS2`/`absorption`) into `MaterialDef::breakProfile`; `DamageSystem::responseFor` consumes it,
  with the `bondStrength*120` fallback for materials without a block. The 5 hardcoded C++ exemplars are
  deleted; the 5 `break` blocks in `materials.json` are byte-identical to the old literals (auditor
  git-diffed). L2 test `tests/core/DamageSystemBreakProfileTest.cpp` (7 cases: parse, consume, fallback,
  all-101 finite) — red-before-green independently reproduced by the auditor (revert parse wiring →
  3 fail, `Metal`=114 fallback; restore → 7/7). Full suite 2805/0. **Grounding follow-up (separate
  task, non-blocking):** the other ~96 materials + re-grounding the 5 still need grounding-auditor
  values (§7); Phase 0 delivered the *mechanism*, not the tuned table.
- **Phase 1 — CoherentFragmentService + collapse routing (B, C).** *Contract:* a severed connected
  component ≤ budget falls as ONE rigid body; furniture shatter uses the same service. *Depth:* L2
  (fragment == component, mass/COM correct) + L3 (settles, no interpenetration/NaN) + L4 (`apply_damage`
  a wall → coherent slab topples). *Stress:* sever a 3000-voxel overhang (cap boundary); N walls at once
  vs. the body budget — assert budget honored, no false-scatter of in-budget pieces, no over-budget CPU
  collapse. *The core deliverable.*
- **Phase 1b — Fragment retirement tier (§5.G): freeze + standability + lazy reactivation** (required by
  the 2026-07-14 decisions; the scalability key). *Contract:* a fell at rest (i) persists with no
  lifetime timeout, (ii) freezes out of the solver to functionally-static (body slot + broadphase entry
  freed), (iii) becomes standable/blocking via a settled-footprint occupancy stamp, (iv) has its
  kinematic faces greedy-merged in place, and (v) reactivates into a full rigid body on interaction
  (chop/push/blast), with bounded neighbor-wake. *Depth:* L2 (frozen = 0 solver/broadphase presence;
  face count drops; occupancy matches rest pose) + L3 (chop a frozen log → reactivates & moves; a stack
  partially collapses) + L4 (character stands on a settled log; FPS with M frozen logs present).
  *Stress:* accumulate M permanent settled fells → assert FPS ≥ threshold, face count bounded, AND
  active-body count ≈ 0 at rest — the persistent-population gate (§12).
- **Phase 2 — Tree fell/topple vertical slice (trees, B+C + hinge).** *Contract:* flood-fill identifies
  the tree above a cut; it topples (hinges) and settles as a trunk. *Depth:* L3 (topple sim: base
  pivots, no ground-clip) + L4 (Emberwake tree). *Stress:* fell a giant megaflora (voxel count ≫
  MIN_FRAGMENT) and a tiny bush (< MIN_FRAGMENT → graceful scatter); a stand of trees (budget). *Closes
  feedback #2.*
- **Phase 3 — Gatherable aftermath (D).** *Contract:* harvesting a persistent settled object (chop/
  gather) emits grounded resource items and shrinks/frees the object as worked. *Depth:* L4 loop test
  (harvested voxels → inventory count == yield; slot frees on full harvest). *Stress:* fell a forest,
  harvest all, assert Σ items == Σ yields and no leaked bodies. *Grounded yields required.*
- **Phase 4 — Tool + swing (E, ties feedback #1).** *Contract:* equipped axe swing severs a tree in a
  grounded number of chops; tool/material affinity respected; fists ineffective on wood. *Depth:* L4.
  *Stress:* chop-spam (churn), wrong-tool, mid-swing tool-swap.
- **Phase 5 — Damage visualization (F, P4).** *Contract:* damaged-but-unbroken voxels show cracks/
  darken scaling with accumulated damage. *Depth:* L2 (state) + L4 (pixel-diff visual, not "looks
  cracked"). *Stress:* many partially-damaged voxels (render cost).
- **Cross-cutting (any phase that touches it):** persistence of broken/damaged state + spawned resource
  props to the world DB; the two-occupancy-grid invariant on every new removal path.

Recommended first build: **Phase 0 → Phase 1** (foundation + core), because they unblock everything and
are the honest engine work; then Phase 2 gives the visible, feedback-closing payoff.

**Progress (2026-07-14) — Phase 1 core complete, all auditor-PASS.**
- Phase 0 data-driven break profiles (`fd38d2c`).
- P1.1 shared greedy merge (`c9b2f87`/`108ac7a`).
- P1.2a `physicalize` — the "voxel set → falling compound rigid body" primitive, furniture refactored
  onto it (`7c4406f`).
- P1b `CoherentFragmentManager` (`9988114`) — the persistent lifecycle owner H12 exposed: ticks
  body→render each frame, no re-bake. (H12: `DamageSystem` is per-call so can't tick; furniture's owner
  re-staticizes — so world fragments needed their own owner first.)
- **P1.2b coherent world collapse** (`4199aae` + audit-fix `586bd06`) — `collapseUnsupported`'s severed
  components route into `CoherentFragmentManager` and topple as ONE rigid slab (opt-in `coherent` flag,
  default off; scatter stays the shipped default). Proven: live L4 (a floating wall split → one slab)
  **and** an automated integration A/B (`tests/integration/CoherentCollapseIntegrationTest.cpp`: flag
  ON → 1 body, OFF → scatter, on a real Vulkan `ChunkManager`). The first audit FAILed it (silent
  geometry-loss on spawn-failure, default-on vs framed-off, no automated test); all three fixed +
  re-audited PASS.

**Residuals (disclosed, non-blocking):** (a) a coherent commit is atomic, so one component up to
`COHERENT_MAX_VOXELS` can overshoot the `MAX_COLLAPSE` cap by its size in one shot; (b) the integration
test asserts "a body + cells cleared" but not the exact "gathered count == severed count" invariant
(§10 Phase-1 acceptance) — geometry correctness is source-verified, not yet test-asserted at that
granularity; (c) microcube coherent gathering is deferred to Phase 2 (a micro-only cell falls back to
scatter); (d) **H1 still stands** — a big tree/canopy over `MAX_FLOOD` is misclassified as main-mass and
won't sever (Phase 2 needs object-aware tree ID).

**Remaining in Phase 1:** P1.3 — the Release benchmark to replace the placeholder `FragmentBudget`
ceilings + `COHERENT_MAX_VOXELS` with grounded numbers.

**Phase 2 findings (2026-07-14, live-scoped; code reverted to baseline).** Confirmed live that real
trees do **not** fell yet, and *why*:
- **H1 is real and confirmed.** Chopping a real tree's trunk leaves the canopy **floating** — the log
  shows `totalDetached=0` (the severed top is misclassified as supported main-mass because it exceeds
  `MAX_FLOOD`). Screenshot showed the tell-tale floating canopy over a cut trunk.
- **A material-aware flood cap works for the CLEAN case.** A tweak (`TREE_MAX_FLOOD`, pure `Log`/`Leaf`
  components don't count "big" as "main mass") **did** detach a controlled floating 3584-cell pure-`Leaf`
  blob (log `collapse: 3528 voxels detached and fell`) — impossible under the old cap. So the H1
  misclassification fix is sound for clean/floating trees.
- **But real trees have harder connectivity.** Three flood-logic iterations (material-cap → leaf-doesn't-
  root → downward-`Log`-root-only) all **failed to fell the real test tree**, and a scan revealed *why*:
  that tree is partially **hill-embedded** (sparse trunk, terrain flanking it) and its canopy overhangs
  terrain — so the "severed top" reaches ground through multiple non-trunk paths. It was also a poor
  test case (its canopy wasn't even where I aimed).
- **Lesson / method for next time:** real tree felling needs a proper **tree-object identification** pass
  (flood the connected `Log`/`Leaf`, anchor ONLY via a trunk rooted to ground, ignore incidental
  leaf/side terrain contact), developed **test-first** against controlled materialed trees (now enabled:
  `ChunkManager`/`Chunk::addCube(localPos, material)` + a materialed-tree integration fixture) with the
  flood's *supported-reason* logged — NOT live trial-and-error on 2-minute engine reboots. The
  microcube-gather (P2.1) and hinge (P2.3) come after detachment reliably works.
- **Reverted** the three experimental flood changes to keep the audited baseline clean; they're captured
  here as the starting hypothesis, not committed.

**P2.2 SHIPPED (2026-07-14, test-first, auditor-PASS) — trees fell.** Redid it the right way:
`collapseUnsupported` is now **tree-object-aware** (`920cded` + audit-fix `ac54085`):
- a tree cell (`Log`/`Leaf`) never spreads the flood **into terrain** — so a canopy brushing a hill or a
  trunk flanked by a slope can't anchor a severed top;
- a tree is rooted **only** through its trunk — a `Log` with terrain directly below (or `yAnchor`);
- a pure-tree component floods to `TREE_MAX_FLOOD` (a big canopy isn't ground).
Developed **test-first** headless (`tests/integration/TreeCollapseIntegrationTest.cpp`, materialed trees
via the new `ChunkManager::addCubeWithMaterial`): `OverhangCanopy` red→green (canopy stayed 245 → falls),
`EmbeddedTrunk` (flanked trunk), `StandingTree_AdjacentTerrainBlast` (a real over-detach guard). The
solution-auditor **mutation-confirmed** all three rules (removing the leaf-skip → OverhangCanopy red at
245; removing the rooted-anchor → StandingTree red, tree wrongly falls) — genuine falsifiable coverage.
**Live-confirmed**: a rooted `Log`+`Leaf` tree, trunk chopped → `coherent collapse: 335 cells → 1 rigid
slab`, canopy toppled and fell as one body onto the slope, rooted stump stayed. **Closes the core of
feedback #2 for clean/rooted trees.** Remaining: microcube-only leaf cells still scatter (P2.1 —
`cellMaterial` reads only the first-level subcube); degenerate hill-embedded wild trees; big-tree
coherence needs a higher `COHERENT_MAX_VOXELS` (P1.3 benchmark); directional hinge (P2.3).

**P2.1 SHIPPED (2026-07-14, test-first, auditor-PASS) — microcube resolution + "leaves shed, wood
topples"** (`d978a53`). Ground truth first: standing leaves are voxel-backed foliage CARDS
(`FoliageRenderPipeline` draws per exposed leaf subcube; the data stays in the chunk grid), and the
kinematic pipeline has **no** card support — so a cohering canopy would render as solid blocks mid-fall.
User decision: **leaves shed, wood topples** — only wood forms the rigid body (at full microcube
resolution), Leaf* voxels scatter as light debris; a leaf-only component scatters entirely (the leaf
poof). Changes: `cellMaterial` sees micro-only cells (27-slot scan); `gatherCellVoxels` gathers
microcubes (center `wp+(sub*3+mic+0.5)/9`, verified against `Microcube::getWorldPosition`);
`collapseComponentCoherent` partitions wood/leaf. Both tests were RED on baseline (canopy stayed 75/75;
0 bodies) → GREEN; the auditor mutation-tested both rules independently (PASS), 8/8 integration + full
suite 2824/0. **Disclosed behavior change:** Leaf voxels that previously cohered into slabs now shed.
**Residuals:** leaves past `MAX_DEBRIS`=4000 in one blast lose geometry silently past the cap
(pre-existing cap class, boundary untested); live visual pass of the shed look pending (first look:
shed leaf debris renders as DARK clumps — leaf cutout texture on solid dynamic voxels; polish
candidate: tint/lighten or shrink leaf debris).

**P2.3 SHIPPED (2026-07-14, test-first, auditor-PASS) — directional hinge topple** (`23d8960`).
A severed piece is seeded with a rotation about the cut instead of free-dropping. Pivot = mass-weighted
centroid of the wood's lowest 1-unit band (≈ the cut face). Tip direction precedence (user: asymmetry
matters beyond the hinge): **1. mass asymmetry** (horizontal COM offset off the pivot — top-heavy side
wins) → **2. chop direction** (the damage `direction` bias) → **3. away from blast center** → 4. straight
drop (old behavior). Seed `ω = up×tipDir · 0.8·√(3g/L)` clamped [0.5,2.5] (rod-topple rate form —
grounded; 0.8/clamps disclosed feel-constants; originally 0.35/[0.2,1.5], raised during F4 because a
flat-cut trunk's base contacts damped the weaker seed and the tree balanced upright asleep) and
`v = ω×(COM−pivot)` so the initial motion IS a hinge
rotation; the subsequent tumble follows the body's real inertia tensor (asymmetric pieces tumble
asymmetrically for free). Tests red→green (`AsymmetricTop_TipsTowardItsHeavySide`,
`SymmetricTop_TipsAlongChopDirection`); auditor mutation-killed both the asymmetry rule and the whole
seed (exact predicted zeros), re-derived the math, reproduced 10/10. **Phase 2 (trees) is COMPLETE:**
P2.0 scoping → P2.2 tree-object flood → P2.1 micro + leaf-shed → P2.3 hinge.

**Post-demo fixes F1–F6 (2026-07-14/15)** — the live DestructionDemo (flat world + forge trees +
structures) broke the N=1-tested pipeline repeatedly; each fix is red-before-green:

- **F1 (`e539402`) — support flows through WOOD only; leaves are cargo, never debris.** The demo's
  floating birch (leaf-bridge to a rooted neighbor) + canopy-as-voxel-debris. Leaf cells are excluded
  from the support flood; a multi-source BFS assigns each leaf to its nearest wood (STAND wins ties);
  detached wood carries its canopy IN the fragment; orphan leaves are removed silently. Leaves never
  spawn voxel debris anywhere (user contract: leaves are foliage cards, never voxels).
- **F2 (`0e73d94`) — bounded collision boxes.** The 4-FPS pine was ONE body with 2005 collision boxes
  (the >64-grid merge fell back to per-voxel). physicalize gained a render/collision split: render
  keeps every fine voxel, collision = one unit box per WOOD cell, greedy-merged (`LongMicroBranch_…`
  asserts ≤12 boxes for a 10-cell branch).
- **F3 — kinematic foliage: a falling tree keeps its card canopy.** Leaf voxels riding a fragment are
  partitioned out of the solid face build into per-object card instances (`foliage_kinematic.vert`,
  push-constant model matrix, hashes seeded on stable local coords), drawn from a shared buffer with
  the body's live transform. Verified live: the felled birch lies with green cards along it.
- **F4 — acceptance (the user's contract): "a tiny explosion at the base → the tree falls over; no
  voxel shower."** First live run balanced upright asleep (flat-cut base contacts damped the 0.35
  seed) → seed raised to 0.8/[0.5,2.5] (disclosed feel-tune, doc above). Second run: one r2.0 cut →
  `coherent collapse: … -> 1 rigid body`, tilt 90.0° settled ASLEEP, chips-only debris, FPS 59–75.
- **F5 — MIXED log+leaf cells are wood-floodable (the ghost-canopy bug).** F4's fell left most of the
  canopy floating at the original position (probe: 340 real voxels). Root cause: cells were classified
  by their FIRST sub-voxel, but forge trees mix micro-Log branches and micro-Leaf foliage in the same
  cells — leaf-first mixed cells read "leaf", the wood flood skipped them, and all branch wood beyond
  was unreachable (the flood's completeness guarantee silently broke). Fix: content predicates
  (`scanCellTree` — any Log ⇒ wood-floodable; leaf-pure ⇒ cargo). Red test
  `MixedLogLeafCells_WholeCanopyFalls` (leaf-first barrier cell: upper wood + 20-cell canopy floated,
  fragment carried 2 wood/0 leaves) → green (nothing floats; fragment = exactly 5 wood + 18 leaves,
  the mixed cell's own foliage riding in-component).
- **F6 — support flows through STRUCTURAL wood only.** F5's any-wood transmission over-corrected:
  live, the pine stood after a base cut (its ground-touching micro-wood skirt/flare became rooted
  anchors) and the birch hung off the oak through twig-to-twig canopy contact. Physically, an ~11 cm
  twig cannot hold a multi-ton trunk. **Structural = log at cube (1 m) or subcube (1/3 m ≈ 33 cm
  limb) granularity**; micro-only twig wood is CARGO like leaves — transmits no support, never
  anchors, rides its nearest wood. Two mechanisms complete the model: a **cascade** (structural wood
  reachable only through a cargo/twig gap gets its own support flood, looping until nothing new
  turns up — otherwise limbs beyond twig gaps float like the pre-F5 ghost canopy), and **wood-bearing
  orphans** (severed twig clusters with no structural wood nearby) fall as their own coherent pieces
  instead of vanishing. Red tests `GroundTouchingTwigSkirt_DoesNotAnchor` +
  `TwigBridgeBetweenTrees_DoesNotTransmitSupport` (both reproduced the live failures exactly) →
  green; `MixedLogLeafCells` updated — a twig gap now yields two bodies (the twig can't rigidly bind
  them), same 5 wood + 18 leaf totals.

---

## 11. Open questions (decisions to make before/at each phase)

1. **Structure registry on partial destruction** — when a wall of a `PlacedObjectManager` structure is
   blown away, do we (a) leave the bbox entry stale, (b) shrink/retire it, or (c) ignore (structures
   are cosmetic to the registry)? Affects save/load fidelity.
2. **Re-bake / standable / persistence — DECIDED 2026-07-14 (§9).** No re-bake; settled fragments are
   permanent full physical objects in dynamic space; standable/blocking via a settled-footprint
   occupancy stamp (§8, the recommended path (b)); resources come from harvesting the settled object.
   The remaining sub-decision is deferred to Phase 1b implementation: whether standability uses true
   character↔rigid-body collision (path a) or the occupancy stamp (path b) — start with (b).
3. **Budget over-flow behavior** — over the *live* body budget mid-fell, is it particle scatter (loses
   the coherent look) or a cheap animated fade (keeps the look, no physics)? Separately, over the
   *persistent* settled population cap, do we refuse new fells, or demote the oldest settled object to a
   cheaper proxy (contradicts "permanent" — flag to user)? Recommend scatter for live overflow; decide
   settled-cap behavior when the Phase-1 benchmark sets the real ceiling.
4. **Persistence scope** — do damaged-but-unbroken cracks survive reload (needs per-voxel damage in the
   world DB), or only fully-broken state? `DestructionSystem.md` lists damage persistence as
   cross-cutting/optional.
5. **Grounding sources** — sign off the real-world references for toughness ratios, tree→log yields,
   and swings-to-fell before those numbers ship (grounding-auditor).

---

## 12. Risks

- **The persistent-population budget** (compounded by the 2026-07-14 decisions) — *largely answered by
  the retirement tier (§5.G), but must be proven at scale.* "Standable + permanent + full-fidelity"
  would, without retirement, make every unharvested fell a forever rigid body + forever un-merged render
  group + occupancy stamp, a population that only grows. The retirement tier removes the two runaway
  costs (solver body + un-merged faces) by freezing settled fells to functionally-static; what remains
  is render (merged faces) + occupancy + memory, plus reactivation-cascade cost. **Mandatory and now
  designed:** retirement/freeze (§5.G), freeze-time greedy-merge, distance/budget LOD, a cap on
  concurrently-ACTIVE fells, and the Phase-1 Release re-benchmark. **The gate that must actually pass
  (Phase 1b/§10):** M frozen fells present → FPS ≥ threshold AND active-body count ≈ 0 at rest. If that
  fails at forest scale, the fallbacks are far-distance culling of frozen collision and pile-merging
  adjacent frozen fragments — surface to the user before shipping a slideshow.
- **Coherent bodies that never settle** would leak the budget and never convert to resources. Aftermath
  (D) must handle a "stuck awake" timeout.
- **Refactor risk** — folding furniture shatter onto the shared service must not regress the working
  furniture path; it gets its own red-before-green regression test.
- **Integrity discipline** — visual claims ("topples nicely", "looks cracked") are NOT
  self-verifiable; every such claim needs a measurable check (body count, voxel count, pixel-diff) or
  the user's eyes, per the standing integrity rule.

---

## 13. Files (anticipated touch-set)

- New: `engine/{include,src}/core/CoherentFragmentService.*`, `BreakAftermath` (likely in
  `DamageSystem`/`ItemPropManager` seam), tests under `tests/core/`, `tests/integration/`.
- Changed: `engine/src/core/DamageSystem.cpp` (JSON profiles, collapse routing), `MaterialRegistry` +
  `resources/materials.json` (`break` block), `engine/src/core/DynamicFurnitureManager.cpp` (refactor
  onto the service), `resources/rpg/` (axe), a chop `.anim`, `voxel.frag`/`static_voxel.vert` (cracks).
- Docs: this file; update `docs/DestructionSystem.md` status; roadmap note in `docs/AgentContext.md`.

---

## 14. Known design holes (audited 2026-07-14, solution-auditor)

Self-critique of this doc, with each code-grounded claim independently verified against source. These
are **must-resolve-before-phase** items — the design is not "reuse the collapse pass and it's nearly
free." H1/H3 mean the **tree slice needs real new work**; H4/H6/H7/H8 mean the **retirement tier is more
than a one-shot consolidation**.

| # | Hole | Status | Gates | Fix direction |
|---|---|---|---|---|
| **H1** | `collapseUnsupported`'s `MAX_FLOOD=3000` tags any big severed component as supported main-mass → **a large tree/canopy won't fall** (`DamageSystem.cpp:372`). | **CONFIRMED (code)** | Phase 2 | Object-aware tree flood (by material/tag, own budget), not the terrain-tuned collapse cap. |
| **H2** | Felled trees respawn on chunk stream-in. | **REFUTED (code)** — dirty-flag + load-before-generate already handles it; removal paths mark dirty, both load paths check storage first (`ChunkStreamingManager.cpp:114,645`). | Phase 2 | Downgraded: only confirm no dev/debug force-regenerate route bypasses the guard (static-read caveat: not runtime-tested). |
| **H3** | Progressive chopping can't use `Cube::addDamage` — accumulation is **cube-only**; trees are sub-voxel (`DamageSystem.cpp:167`, no `addDamage` on Subcube/Microcube). | **CONFIRMED (code)** | Phase 2/4 | Model chop as geometric cross-section removal (sever = connectivity event), not energy accumulation. |
| **H4** | Reusing furniture shatter's mass/velocity constants verbatim breaks at tree/wall scale — clamp `[0.5,10]`, scale `0.05`, scatter `×2`, `+(0,1,0)` (`DynamicFurnitureManager.cpp:726,755,757`). | **CONFIRMED (code)** | Phase 1 | Parameterize mass/velocity by fragment scale. |
| **H5** | "Full fidelity" vs. `maxVoxelsPerBody` budget collide on the hero case — a giant tree is most likely to exceed the per-body cap and scatter instead of toppling. | Logical (design tension) | Phase 1/2 | Raise per-body cap for discrete objects, or a multi-body articulated fell; measure single big-tree solver cost. |
| **H6** | Freeze-time "greedy-merge kinematic faces in place" is a **new** algorithm — the greedy mesher is static-grid-only; kinematic path only culls, never merges (`KinematicVoxelManager.cpp:151`); `mergeVoxelsGreedy` merges physics boxes, not faces. | **CONFIRMED (code)** | Phase 1b | Build a mixed-resolution local-space face-merge (physics-box merge is a partial ingredient). |
| **H7** | Frozen fragments are removed from the physics broadphase, so reactivation ("nearby blast") has **no spatial index** to query them. | Design omission | Phase 1b | The retirement pool needs its own lightweight spatial index. |
| **H8** | Standability occupancy-stamp reintroduces the grid-misalignment we froze to avoid — visual is the true angled log, collision is a blocky grid stamp (float/clip), plus set/clear bookkeeping against the *shared terrain* occupancy grid. | Logical (design tension) | Phase 1b | Accept blocky collision for v1, or invest in char↔OBB collision; track stamp-owned cells for clean clear. |
| **H9** | Incremental-harvest yield rounds to zero — `floor(harvestedVoxels × perVoxel)` loses fractional yield per small chop. | Spec bug | Phase 3 | Accumulate fractional yield across chops. |
| **H10** | Stand on a frozen log, then chop it → it reactivates, occupancy clears, char↔dynamic-body collision is absent → player clips through the log they stood on. | Logical (edge) | Phase 1b | Needs char↔reactivated-body handling, or delay occupancy-clear until the body actually moves. |
| **H11** | Should terrain topple *coherently* at all? Overhangs are huge (worst budget fit) and a cliff frozen as a non-grid "object" is odd. | Open design question | Phase 1 | Consider restricting coherent fells to discrete objects (trees/walls/furniture); terrain stays scatter. |
| **H12** | **World coherent fragments need a persistent lifecycle OWNER** (tick body→render transform each frame; settle WITHOUT re-bake). `DamageSystem` is per-call/transient so cannot own the tick; `DynamicFurnitureManager` re-staticizes on settle (rejected re-bake). Discovered building P1.2b. | **CONFIRMED (code)** — `Application.cpp:11909` constructs `DamageSystem` per call; furniture ticks via `DynamicFurnitureManager::update()` which re-staticizes. | **Phase 1b BEFORE P1.2b** | Build a minimal `CoherentFragmentManager` (owns bodies, ticks transform, no-rebake settle → later frozen/retired). physicalize (P1.2a) is the primitive; this manager owns its outputs. Reorders the roadmap: the world topple needs this owner first. |

**Net:** the unify-furniture-fracture *architecture* holds, but the tree vertical slice (H1, H3) and the
retirement tier (H4, H6, H7, H8) each carry real new work the earlier sections understated. This table
is the honest scope.


## §5.H Fracture ladder — removal granularity scales with energy (approved 2026-07-16)

Design conversation with the user (post chop-felling milestone): removal was BINARY per
cell — below toughness nothing visible happens, above it the WHOLE cell vanishes. The
overkill tiers (brittleS1/S2) only govern debris fineness of removed matter (correct,
unchanged). The missing band is PARTIAL removal: fractures start at MICRO scale, and
removing a whole cube in one hit is the HIGH bar, not the only outcome.

Ladder (per impact, energy e vs effective toughness T):
  e/T < chipRatio          -> accumulate damage; at 50% accumulated, visible "cracked"
                              voxel state (state byte; renderer tint/decal — new state).
  chipRatio  <= e/T < chunkRatio -> CHIP: refine impact cell locally (cube->sub->micro)
                              and remove a direction-biased POCKET of micros at the
                              impact point; volume proportional to e/T.
  chunkRatio <= e/T < 1    -> CHUNK: subcube-scale bites near the point.
  e/T >= 1                 -> whole cell breaks (existing path); overkill tiers as today.

Tool efficiency: per-toolType x material-class MULTIPLIER table (data, not physics):
axe x fibrous = 8x, pickaxe x crystalline = 8x, wrong tool 0.5x, bare hand 0.25x.
(Area-scaled toughness noted as the possible later physical refactor.)

Material classes (5, each sets default chipRatio/chunkRatio in the break block;
per-material overrides allowed):
  fibrous     (wood/logs)          chip 0.10 / chunk 0.45  — chips readily
  brittle     (glass, ice)         chip 0.80 / chunk 0.85  — scratches, then shatters whole
  granular    (sand, dirt, gravel) chip 0.15 / chunk 0.35  — crumbles (no refinement kept:
                                                             removed micros are loose)
  crystalline (stone, bricks)      chip 0.40 / chunk 0.70  — pickaxe domain
  ductile     (metal, gold)        chip 0.95 / chunk 0.98  — deforms/holds; highest bar
Rationale recorded here once, ratios live in materials.json (grounding: fracture-mode
taxonomy, not per-number physical citations — these are gameplay dials per class).

Accepted consequences: refinement is irreversible for now (re-coarsening = separate task;
blast partial band gets a hard cap ~64 refined cells, band 15-99% of threshold,
blast-facing surfaces only). Chipped-to-micro cells become cargo (F6) and stop carrying
support — accepted for wood, revisit for masonry.

Rollout order (approved): (1) axe internals — the rim-sliver fallback and oversized
bites CHIP a pocket instead of vaporizing whole cells; (2) applyPointDamage(point,
energy, dir) primitive + tool multiplier table, chop + future tools consume it;
(3) blast partial band; (4) editor left-click instant break stays as-is (creative tool).


## §5.I Axe-chop kerf — shipped mechanics (as-built, 2026-07-15/16)

`DamageSystem::carveChopKerf` (per-swing, stateless) — FRACTURE, not blast; `applyDamage`
untouched for spells/explosions. The shipped pipeline, editor side first:

- **Blade contact** (`Application::tryAxeChopOnHitFrame`, per strike frame at progress
  >0.28): the axe head is the held kinematic object's farthest voxel × its live transform.
  Contact requires the head within **0.3 m of actual wood** — clamped to the nearest wood
  FRAGMENT in the cell (`closestWoodPointInCell`; never a cell box or union AABB, both of
  which put contact in air and produced whiff/zero-carve regressions). A zero-carve frame
  does NOT consume the swing (detection retries as the blade sweeps); puff VFX + chop
  sound only on a bite that removed material. Tree matter (Log*/Leaf*) never limb-blocks
  the swing (`checkSegmentVoxelOverlap` exemption) — point-blank chopping works; stone
  still cancels.
- **The bite**: a microcube-resolution slot at the blade — window hugs the contact
  (contactD −0.55/+0.45 along the chop dir, NOT capped by kerfDepth), slot cross-section
  ~2 subcubes (mouthHalfH 0.30 → apex 0.06), flares open near breakthrough. Cubes refine
  to subcubes to micros as the wedge touches them; cut faces repaint LogHeartwood;
  enclosed shell-hollows fill with heartwood first. Rim-sliver fallback = §5.H chip
  pocket (direction-biased micro pocket at the contact), never whole-cell vaporize.
  Diagnostics: every bite logs contact/contactD/window/nearD/pocket — a whiff is
  explainable from one log line.
- **Release** (every biting swing): (1) **band neck-shear** — survey rows anchor.y−1 ..
  anchor.y+2 (7×7 box per row, whole plane incl. disconnected islands; Log cube = 9
  units, Log subcube = 1, micros = 0/cargo); the WEAKEST row holding 0<units≤6
  (≲0.07 m² carrying tons of tree) shears. Single-row survey was a shipped defect: the
  blade anchors on fat rooted flare rows while the true neck sits a row up — a trunk
  visibly stood on slivers forever. (2) **flood re-seed** — the band's structural cells
  are pushed into the release seeds so the component ABOVE the cut is re-evaluated every
  swing (the rim-only flood anchored instantly on rooted flare stubs and never re-checked
  the top; a cargo-only micro neck now releases via the F6 cascade with nothing to
  shear). Release = ordinary support flood → one coherent hinged body (§5.B/P2.3).
- **F8 — vertical support needs FACE CONTACT (2026-07-16):** the flood conducts
  between vertically-adjacent TREE cells only when the lower cell holds structural
  Log in its TOP subcube layer AND the upper cell in its BOTTOM layer (full cube =
  all layers). Cell-granular adjacency alone conducted support across carved-out
  air gaps — an overhang's top-layer underside skin "touched" the rooted stub
  through the cell border while the only physical bridge was a cargo micro pillar
  (live case: a trunk visibly standing on ~5 microcubes, world-verified, never
  fell). Fresh trees hold wood in all three layers of trunk/flare cells
  (data-verified), so healthy trees are unaffected. Horizontal steps stay
  cell-granular for now (same class, lower stakes — noted below).
- **Known gaps** (task #15 remainder): no gradual near-detach state (lean/creak) before
  the binary snap; no live "how close to falling" query (the survey only runs mid-swing;
  diagnosing the live case required scan_micro + DEBUG flood logs); diagonal break
  planes across >4 rows still unevaluated; HORIZONTAL cell-granular conduction can
  still bridge a sub-cell gap sideways (F8 covers vertical only); the rooted-trunk
  anchor now requires TRUNK-LIKE ground contact (F9: full Log cube, or >=4
  structural subcubes in the cell's bottom layer — measured: flare cells 5+,
  twig tips 1-2). Any-log-over-terrain let a drooping branch TIP anchor a
  severed crown: a static ghost thicket stayed standing in the player's path,
  and in the red test the tree never fell at all.
- **Perf (task #7, 2026-07-16):** the falling-phase FPS collapse was
  `VoxelDynamicsWorld::generateContacts` querying terrain with the BODY's whole
  AABB (~1500 voxels for a fallen tree) x every collision box (~225k OBB tests
  per substep at subcube-proxy box counts; 870 ms/frame measured at 46 boxes).
  Now queried PER BOX (O(boxes x ~4)). Collision proxy is SUBCUBE resolution
  (F2+#13): cube = unit box, else 1/3-box per occupied slot (micro quorum >=4);
  >800-cell components stay coarse. Cargo stand-adjacency also obeys F8 (no
  static floaters over air gaps). CHARACTER-vs-body queries (overlapsAnyBody +
  groundHeight's dynamic-body pass) are EXACT against the body's ORIENTED boxes:
  blocking via the solver's OBB-vs-AABB test, grounding via down-rays through
  the character column in each box's local frame. Neither the whole-body AABB
  (invisible envelope walling the player off ~2m out) nor per-box CONSERVATIVE
  AABBs (upright-merged slabs inflate under rotation — a measured 6x3m phantom
  platform 4m over a fallen birch; characters levitated on it) survive a fallen
  tree. Diagnosis instrument: GET /api/debug/body_boxes (world-space per-box
  AABBs + kinematic render transforms). Red tests: dumbbell + 45-deg rotated
  slab in PhysicsIntegrationTest.

---

# §15 Universal destruction — the plan (scoped 2026-07-19)

**User goal, stated:** *"the felling mechanic is how I want ALL structures to behave — knock down
parts of houses, topple towers, chop down trees. Fire a damage action (spell, melee) at the bottom
of a wall and knock it down. An entire destruction system that applies to all world assets."*
Plus: **falling causes further fracture** — a toppling tree sheds weak branches on impact; a
toppling tower breaks into 2-3 chunks when it hits the ground.

Phases 0-5 delivered that for **trees**. §15 is the generalization to structures, objects, and
terrain. Nothing in §15 is built yet.

## 15.1 Corrected baseline — what is ACTUALLY shipped (audited 2026-07-19)

Correcting two stale claims elsewhere in this doc:

- **Standable settled fragments: SHIPPED** via §8 option **(a)**, character-vs-`VoxelRigidBody`
  collision — *not* the (b) footprint stamp this doc recommended. `b104101` (per-box rather than
  whole-body AABB) then `86d2d85` (exact ORIENTED boxes: OBB blocking + local-frame down-ray
  grounding). §8's "required deliverable" wording predates these commits and is obsolete. Path (a)
  turned out strictly better than the recommendation: the standable surface is the true angled
  geometry rather than an axis-aligned approximation.
- **Phase 1b is PARTIAL, not absent.** Shipped: (i) persistence, (iii) standable/blocking.
  Missing: (ii) freeze out of the solver, (iv) greedy-merge kinematic faces at rest, (v) lazy
  reactivation. `CoherentFragmentManager` (113 lines) spawns with `lifetime = FLT_MAX` and syncs
  transforms forever — bodies sleep but never leave broadphase.

**General already (material-agnostic, reusable as-is):** `CoherentFragmentService::physicalize`,
the shared greedy merge, `CoherentFragmentManager`, hinge-topple seeding (P2.3), bounded collision
boxes (F2 + #13), per-box terrain contact queries (#7), exact oriented character queries.
Phase 1 was in fact validated on **a wall** (`CoherentCollapseIntegrationTest`), not a tree.

**Tree-gated (the layers that do NOT generalize):** the support-transmission model — cargo vs.
structural wood, rooted-trunk anchoring, twig-bears-no-load (F1/F5/F6/F9) — is keyed on `Log*` /
`Leaf*` name prefixes. `carveChopKerf` hard-bails unless it finds a `Log` cell, and every carve
predicate tests the `Log` prefix. There is no general chop: a wooden door or a plank wall cannot
be hacked through today.

**Measured blockers (constants verified in source, 2026-07-19):**

| Constant | Value | Consequence for structures |
|---|---|---|
| `MAX_FLOOD` | 3000 cells | A non-tree component larger than this is declared `flood-cap-terrain` = **supported**. A real house or tower is at or over it, so **blast its base and it floats.** This is H1, still unfixed for structures — trees escaped it via `TREE_MAX_FLOOD` = 20000. |
| `COHERENT_MAX_VOXELS` | 2000 cells | A correctly-severed larger section **hard-bails to particle scatter** instead of toppling. |
| `MAX_COLLAPSE` / `MAX_DEBRIS` | 6000 / 4000 | Per-blast caps; silent geometry loss past `MAX_DEBRIS`. |
| break profiles | **6 of 102** materials | Stone, brick and glass run on the `bondStrength*120` fallback, so they do not yet *feel* materially distinct when broken. |

All four are **placeholders**: the P1.3 Release benchmark meant to ground them was never run.
`PlacedObjectManager` has zero destruction integration (§11 Q1 still open).

## 15.2 The unifying abstraction — `CrossSectionAnalyzer`

Three separate requirements reduce to one question: *how strong is this cross-section?*

| Consumer | Question asked of a candidate plane |
|---|---|
| **Statics** (undermining) | Can the remaining cross-section carry the mass above it? |
| **Chop severance** | Has the tool reduced this cross-section below its shear strength? |
| **Impact fracture** | Does the landing moment across this plane exceed its strength? |

The kerf's band neck-shear survey **already implements this**, for wood: rows are scored in
structural units (Log cube = 9, subcube = 1, micros = 0/cargo) and the weakest row holding
0 < units ≤ 6 shears. That is an approximate statics model in miniature, gated to trees.
Generalizing it — score a plane's strength as `Σ (occupied area × material shear strength)` over
any material at any voxel scale — yields **one component with three consumers**, and makes
approximate statics substantially cheaper than a from-scratch stress-propagation subsystem.

Per §9's "one coherent-fragment implementation" rule, the generalized analyzer **replaces** the
wood-specific neck-shear survey. No second copy.

## 15.3 Phases

Each phase carries the standing project discipline: functional contract, required validation
layer, red-before-green test, scale/stress test, solution-auditor sign-off. Ordered so
scalability lands before the features that multiply body counts.

- **U0 — Wire the damage entry points. ✅ SHIPPED 2026-07-20 (commit after U1a).** Spell hits
  (`Application::updatePendingSpellHits`) and the `cast_spell` destroy path each constructed a bare
  `DamageSystem` and took `coherentFragments = false`, so a fireball could never fell anything —
  only the axe chop and `apply_damage` opted in. Both now wire the persistent
  `coherentFragmentManager` and pass `collapse=true, coherentFragments=true`. Blast-path F1 fix: the
  Phase-A scan read `getStaticSubcubesAt` (subcubes only), so a micro-only leaf cell fell through to
  the `"Wood"` default and scattered wood voxel debris; it now uses `cellMaterial`
  (cube→subcube→**microcube**), so leaf cells are recognized and emit no debris. Red-before-green:
  `TreeCollapseIntegrationTest.BlastOnMicroLeafCell_EmitsNoVoxelDebris` (pre-fix broken=1/debris=12,
  post-fix debris=0). Live: canopy blast 200 broken / 252 debris (leaves excluded; scatter would be
  ~2400); fireball via `/api/spell/cast` routes through `applyDamage` with the coherent path enabled.
  **CAVEAT (not U0):** end-to-end "spell fells the tree" is still gated by felling *reliability* — a
  flat-ground forge oak does not sever coherently from a base blast (the flood keeps the crown
  "supported"; F9 / hard-connectivity). The **pre-existing** `CrossSpeciesLimbContact_DoesNotTransmit
  Support` integration test also fails on baseline (verified with U1a stashed) — likely a merge
  regression in the felling flood's palette-store material reads. Both belong to the felling-
  reliability thread (U2/U3), not U0's plumbing.
- **U1a — Broadphase over the occupancy grids (see §15.5). MUST precede U1.** Today
  `generateContacts` scans **every registered chunk grid for every collision box of every awake
  body, every substep** — cost scales with *world size*, not with the falling object. Index the
  grids by chunk coordinate (they are already keyed that way) and query only the handful a box's
  AABB overlaps; compute the chunk-coord span once per body and reuse it across its boxes; also
  unregister all-air chunks, which currently stay in the scan list forever (only *sealed* chunks
  unregister — `Chunk::applyAirRenderState` does not). *Depth:* L2 (a fell's queryAABB call count
  is independent of loaded-chunk count — the assertion that pins the fix) plus L4 (FPS while
  felling in a large streamed world). *Red test:* register N chunk grids, fell one tree, assert
  call count does not grow with N — currently linear in N.
- **U1 — Ground the budgets (the deferred P1.3).** Release-build benchmark of
  `VoxelDynamicsWorld` producing real ceilings for `FragmentBudget.maxActiveBodies` and
  `COHERENT_MAX_VOXELS`, encoded as an automated assertion in `tests/benchmark/` so they cannot
  silently drift the way a hand-copied doc number can. Everything downstream is currently sized
  against guesses. **Run this AFTER U1a**: benchmarking the body ceiling while a world-size-linear
  scan dominates the profile would measure the chunk count, not the body count, and bake a bogus
  ceiling into `tests/benchmark/`. *Depth:* L2 plus a benchmark gate.
- **U2 — `CrossSectionAnalyzer` + de-gate the chop.** Extract and generalize the neck-shear survey
  per §15.2: material-weighted, all three voxel scales, any axis. Then remove `carveChopKerf`'s
  `Log`-prefix gating so a tool cuts any material it has affinity for (fists still bounce off
  stone). *Depth:* L2 (the analyzer scores known geometry correctly — a one-voxel neck versus a
  full cross-section) plus L4 (chop through a plank wall). *Red test:* stone and plank
  cross-sections score non-zero — they currently score 0, i.e. they are invisible to the survey.
  *Stress:* a mixed-material plane spanning all three voxel scales.
- **U3 — Structure-object identity + flood caps.** The direct fix for "blast the base, the house
  floats." Buildings need the object-awareness trees got in P2.2: identify the structure component
  so it escapes `MAX_FLOOD`, anchored only through its real ground contact. Prefer querying
  `assembly_plan` / `featureAt` metadata over sniffing voxel materials (standing project rule).
  *Depth:* L2 (a severed building section detaches, headless, materialed fixture) plus L4.
  *Red test:* blast the base of a >3000-cell building — currently `flood-cap-terrain`, must
  detach. *Stress:* a settlement of adjacent buildings; assert no cross-building over-detach (the
  P2.2 `StandingTree_AdjacentTerrainBlast` guard, structure edition).
- **U4 — Approximate statics: support CAPACITY, not boolean connectivity.** *The architectural
  gap.* Today the anchor rule is "is there a path to ground," so a structure must be **fully
  severed** to fall: undermine a wall, leave one connected voxel path, and it hangs there,
  structurally fine by the engine's rules. Replace the boolean with a capacity test built on U2 —
  propagate carried mass downward and collapse where the supporting cross-section cannot bear it.
  *This is the phase that makes "fire at the bottom of a wall and knock it down" work.* *Depth:*
  L2 (a progressively undermined column collapses at the grounded threshold rather than at full
  severance) plus L3 plus L4. *Red test:* a wall with 90% of its base removed but one connected
  voxel path — currently stands, must fall. *Stress:* deep structures with load accumulating over
  many stories; assert no runaway cascade; measure the capacity flood's cost against the boolean
  one.
- **U5 — Retirement tier (the missing half of Phase 1b).** Freeze at rest: remove the body from
  `VoxelDynamicsWorld` (freeing the body slot and the broadphase entry), greedy-merge its
  kinematic faces once, keep geometry plus transform as plain data; lazy reactivation on
  chop/push/blast with bounded neighbor-wake. Standability is already solved for *live* bodies
  (§15.1), but a frozen fragment has left the solver, so the frozen state needs its own standable
  representation — the §8 (b) footprint stamp returns **here**, scoped to frozen fragments only.
  *Deliberately scheduled before U6, because impact fracture multiplies bodies.* *Depth:* L2 (zero
  solver/broadphase presence; face count drops after merge) plus L3 (chop a frozen log → it
  reactivates and moves; a stack partially collapses) plus L4 (FPS with M frozen fells present).
  *Stress:* the persistent-population gate (§12).
- **U6 — Impact fracture (secondary fracture on landing).** *New requirement, 2026-07-19.* A
  landing fragment breaks along the planes the impact overloads. Design:
  1. **Pre-score candidate weak planes at spawn** using U2's analyzer. This runs off the critical
     path — the voxel set is already in hand at `physicalize` time — and avoids an analysis hitch
     at the most performance-sensitive moment.
  2. **At contact**, evaluate impulse × lever-arm as a bending moment across each pre-scored
     plane; planes whose strength is exceeded break.
  3. **Partition** the voxel set along the broken planes (component BFS with the planes as cuts)
     and respawn each part as its own coherent fragment, inheriting the parent's velocity plus a
     separation impulse. Reuses `physicalize` wholesale.

  The desired behaviors are emergent rather than special-cased: branches have thin cross-sections
  and long lever arms, so they snap off, while the trunk's fat cross-section survives; a toppling
  tower has peak moment near mid-span, so it breaks into 2-3 chunks. **Required bounds:** a
  recursion depth cap (a fractured piece must not re-fracture forever), a minimum fragment size
  (below it, scatter to particles), a per-impact fracture budget, and mutation deferred to
  end-of-step — never mid-solve. *Depth:* L2 (a barbell fragment dropped on its midpoint splits
  into exactly 2; a thick uniform slab does not split) plus L3 (pieces settle, no interpenetration
  or NaN) plus L4 (fell a branched tree → branches shed on landing; topple a tower → it breaks
  into chunks). *Stress:* fracture cascade under the body budget — fell a stand of trees
  simultaneously and assert the budget is honored, recursion stays bounded, and there is no
  frame-time cliff at the moment of collective impact.
- **U7 — Registry reconciliation + persistence.** Closes §11 Q1: when a structure's voxels are
  destroyed, decide and implement the `PlacedObjectManager` outcome (shrink, retire, or ignore).
  Persist broken/damaged state and settled fragments to the world DB. *Depth:* L4 round-trip —
  destroy, save, load, and confirm damage state and fragments survive.

**Cross-cutting, runs alongside:** grounding the remaining ~96 material break profiles (§7). This
is required before stone/brick/glass destruction can *feel* right, and both U4 and U6 consume the
shear strengths it produces. Also standing: the two-occupancy-grid invariant on every new removal
path.

## 15.4 Risks

- **U4 is the one genuinely new subsystem.** U2 makes it far cheaper than full stress propagation,
  but "approximate statics that feels right and never runaway-cascades" is the hardest tuning
  problem in this plan. Expect a shakedown.
- **Trees needed nine post-N=1 fixes (F1-F9).** Structures have worse scale properties and more
  material variety. Budget for the same class of reality-versus-test-geometry surprises, and
  prefer the P2.2 method (test-first, headless, materialed fixtures, logged supported-reason) over
  live trial-and-error across two-minute engine reboots.
- **U6 multiplies bodies at the worst possible moment** (collective impact). U1 and U5 are its
  prerequisites for exactly that reason.
- **Render density returns via the kinematic pipeline** (§8): fragments are not greedy-merged
  until U5, and U6 creates more of them.

## 15.5 Why a single toppled tree costs so much (MEASURED & CONFIRMED 2026-07-20)

> **Status: hypothesis CONFIRMED live.** The arithmetic below was source-verified 2026-07-19, then
> measured 2026-07-20 with temporary instrumentation (`VoxelDynamicsWorld::BroadphaseStats` +
> `GET /api/debug/physics_broadphase_stats`, which report the last substep's `grid_count`,
> `awake_boxes`, `query_aabb_calls = Σ boxes × gridCount`, and phase timings). Method: drop N
> single-box CPU `VoxelRigidBody`s (each = 1 collision box, so `awake_boxes = N`) from height so the
> fall lasts long enough to sample past the endpoint's ~2 s/call latency, holding N fixed while
> varying the world — so `grid_count` is the only independent variable. `spawn_voxel_body` was used
> rather than a real fell because `apply_damage` would not reliably sever the forge test trees
> (the F9 hard-connectivity case — the crown stayed rooted; a separate issue, and irrelevant to a
> pure broadphase measurement). See the measured table at the end of this section.

**The user's intuition is correct and worth stating plainly:** a fallen trunk *is* a simple shape,
and the engine *does* run ~10 000 GPU debris particles comfortably. The reason a couple of falling
trunks can cost more than ten thousand debris voxels is that they run on two solvers with entirely
different cost models — and one of them scales with the wrong variable.

**GPU debris path:** each particle is one independent `SolverBody` on the Vulkan compute solver,
contacts only, tested against a GPU occupancy structure with an O(1) indexed lookup, massively
parallel. Cost is O(particles) with a constant-time spatial query. 10 000 is genuinely cheap.

**Coherent fragment path:** a CPU compound `VoxelRigidBody` in `VoxelDynamicsWorld`, whose per-
substep terrain broadphase is (`VoxelDynamicsWorld::generateContacts`):

```
for each awake body            (parallel over bodies)
  for each collision box bi
    for (VoxelOccupancyGrid* grid : m_grids)     <-- LINEAR OVER EVERY REGISTERED CHUNK
      grid->queryAABB(...)
```

Cost per substep = `awakeBodies × boxesPerBody × gridCount`. Note what is *not* in that product:
anything about the tree. **The dominant term is `gridCount` — the number of registered chunk
occupancy grids, i.e. the size of the loaded world.**

Concretely, with the Large-World work now merged (Phase 4.3 recorded 10.6k chunks stable, 3x the
old ceiling) and a fell carrying ~100 merged collision boxes (§5.I cites real fells at 46 and
~150 boxes), one falling tree issues on the order of **100 × several-thousand = hundreds of
thousands of `queryAABB` calls per substep**, multiplied again by substeps per frame. Each call is
individually cheap — `VoxelOccupancyGrid::queryAABB` opens with a 6-comparison chunk-level
rejection and returns — but every iteration chases a pointer out of a multi-thousand-entry vector
into a different grid object to read its `m_origin`, so the loop is a cache-miss generator. There
is **no spatial index over the grids at all**; the per-grid early-out *is* the broadphase.

**This explains the two things that looked contradictory:**
- *Why a "simple shape" is expensive* — the cost is not in the shape. Every box interrogates every
  chunk in the world.
- *Why felling appears to have regressed after pulling in origin/main* — nothing in the destruction
  code got slower. The Large-World work raised the loaded-chunk ceiling, and this loop is linear in
  exactly that. Felling got slower as a **side effect of the world getting bigger.**

**Aggravating detail:** only *sealed* chunks unregister their grid (`Chunk::applySealedRenderState`
→ `unregisterGridFromWorld`, Phase 4.4). `Chunk::applyAirRenderState` does **not** — so every
all-air chunk stays in the scan list permanently, always returning nothing. In a tall streamed
world, air chunks are the majority of loaded chunks, so most of the scan is provably wasted work.

**The fix is standard and contained (U1a):** index the grids by chunk coordinate — they are
*already* keyed that way — and query only the handful of chunks a box's AABB actually overlaps.
Compute the chunk-coord span once per body and reuse it across that body's boxes (a fragment's
boxes are all near each other by construction). Unregister air chunks. Together these turn the
inner loop from thousands of iterations into single digits, and make the cost independent of world
size. **The pinning assertion is a call-count invariant, not a timing:** register N chunk grids,
fell one tree, assert the `queryAABB` count does not grow with N.

### U1a as-built (2026-07-20)

Shipped exactly as scoped above. Changes:
- `VoxelDynamicsWorld` gained `m_gridByChunk` (`unordered_map<chunkKey, grid*>`) alongside the
  `m_grids` vector. `registerGrid`/`unregisterGrid` maintain both; dedup now goes through the map
  (**O(1)**, was an O(gridCount) `std::find`). `gatherGridsOverlapping(mn, mx, out)` returns only
  the grids whose chunk the AABB touches.
- `generateContacts` terrain phase: per awake body, gather the overlapping grids **once** (from the
  body AABB widened by 0.5 so a box on a chunk seam still sees the neighbour), then query only those
  across the body's boxes. Same for the character paths `findGroundY` and `overlapsTerrain`.
- Air chunks now leave the query set: `Chunk::applyAirRenderState` calls `unregisterGridFromWorld`
  (previously only *sealed* chunks did). The inverse transition is handled by the new idempotent
  `Chunk::ensurePhysicsRegistered`, called on the rebuild classifier's full-mesh branch (an air or
  sealed chunk `return`s before it, so it runs only for genuinely collidable chunks).
- The temporary `physics_broadphase_stats` `query_aabb_calls` now reports the **actual** call count
  (summed per-thread), so the endpoint directly shows the drop.
- Regression tests (`PhysicsIntegrationTest`): `BroadphaseCallCountIndependentOfGridCount` (one
  body, gridCount 10 vs 500 → identical, tiny call count — the pinning invariant) and
  `IndexedBroadphaseStillCollidesWithTerrain` (a body still lands on a floor cell reachable only
  through the index, among 50 decoy grids — the correctness guard).

**Measured before/after (same 400-box "several trees" drop, large streamed world):**

| | `grid_count` | `awake_boxes` | free-fall `query_aabb_calls` | near-ground peak calls | free-fall `terrain_ms` |
|---|---|---|---|---|---|
| **Pre-fix** | 378 | 400 | **151 200** (= 400×378) | 151 200 | 1.9 |
| **Post-fix** | 318 | 400 | **0** (over unloaded air) | **505** | 0.5 |

The `boxes × grid_count` identity is broken: 400×318 would be 127 200, actual peak is **505** —
a ~250–300× reduction, and the call count no longer tracks `grid_count`. The standout is **0 calls
while free-falling over unloaded air**: with no registered grid beneath the bodies, the gather
returns empty and the scan does not run at all — the altitude-independent pure-waste case (§15.5
point 4) is eliminated outright.

**Honest residual:** at ground impact `generate_contacts_ms` still peaked ~40 ms — but that is
**body-vs-body** contact solving for 400 boxes converging into one pile at a single point, an
artifact of this synthetic drop, NOT the terrain scan and NOT world-size-dependent. Real fells land
in *different* places, so inter-body contact is minimal; the world-size-linear terrain cost that
U1a targeted is gone. (Reducing the pile cost is a separate concern — the body-body spatial hash —
outside U1a's scope.)

*Tests: `BroadphaseCallCountIndependentOfGridCount` + `IndexedBroadphaseStillCollidesWithTerrain`
pass; `ChunkSealedTest` 7/7 (sealing/air transitions intact); 39/40 collapse+chop+physics
integration tests pass — the 1 failure (`CrossSpeciesLimbContact_DoesNotTransmitSupport`) is a
**pre-existing merge regression**, verified failing on baseline with U1a stashed, unrelated to this
change.*

### Measured results (2026-07-20)

Two worlds, N single-box falling bodies, `awake_boxes = N` held fixed while `grid_count` varies:

| World | `grid_count` | `awake_boxes` | `query_aabb_calls`/substep | free-fall `terrain_ms` | impact-peak `generate_contacts_ms` |
|---|---|---|---|---|---|
| CharacterTestbed (DB world, small) | 99 | 150 | **14 850** | 0.52 | ~5 |
| MiddleEarth1to1 (streamed, large) | 333 | 150 | **49 950** | 0.79 | 20.6 |
| MiddleEarth1to1, "several trees" | 378 | 400 | **151 200** | 1.9 | **43.2** |

**What the data proves:**
1. **The identity is exact, every sample, both worlds:** `query_aabb_calls == awake_boxes ×
   grid_count` (150×99=14 850; 150×333=49 950; 400×378=151 200). The multiplier is `grid_count` —
   the size of the loaded world — full stop.
2. **Linear in world size.** The *same* 150-box object costs 3.36× the broadphase work moving from
   99 to 333 grids (14 850 → 49 950 calls) — nothing about the object changed.
3. **Linear in box count** too — confirmed both across runs (150 → 400) and *within* a run as bodies
   sleep off (e.g. large world: 106 boxes × 333 = 35 298; 49 × 336 = 16 464 — every intermediate
   sample lands on the identity).
4. **The scan is altitude/emptiness-independent** — the smoking gun. Bodies free-falling 150+ units
   up over terrain, touching nothing, still issue the *full* `boxes × grid_count` scan every
   substep (14 850 / 49 650 / 151 200 while in the air over empty space). Pure waste.
5. **`grid_count` is world-driven and climbs with streaming**, standing still: 63 → 378 over a few
   minutes as the loadRadius-16 footprint filled — and kept rising past what solid terrain needs,
   consistent with the air-chunk-never-unregisters aggravator.
6. **The user's exact symptom reproduced.** At the "several trees" scale (400 boxes ≈ 3–4
   simultaneous fells) in the large world, `generate_contacts_ms` hit **43 ms in a single substep**
   at ground impact. At 3 substeps/frame that is ~130 ms of physics in one frame — a hard visible
   stall. That is the "several trunks tank the FPS" report, measured.

**Honesty caveats on the numbers.** (a) `terrain_broadphase_ms` is wall-clock of a *parallel* loop
(`parallelRange` over bodies), so it understates the serial work and is noisier than the call count
— which is why 3.36× the calls showed only ~1.5× the free-fall ms. **Lead with `query_aabb_calls`
(exact, deterministic); treat the ms as corroborating.** (b) The largest `grid_count` measured was
~378, not the 10.6k-chunk config the plan cites — but the scaling *law* is now proven exactly, so
extrapolation is sound: 400 boxes × 10 600 grids ≈ **4.2 M queryAABB/substep**. That specific
extreme is extrapolated, not measured. (c) The instrumentation is temporary; remove or gate it
after U1a lands, keeping the call-count assertion as the regression test.

**Other real costs, so this is not read as a single-cause story** (each smaller, none scaling with
world size):
- **Kinematic faces are never greedy-merged** (§8). `buildFaces` *does* cull at micro resolution
  (`5ce5fb7`), so a fell is not paying 6 faces per microcube — but an unmerged micro-fidelity
  surface is still a lot of faces, and they accumulate permanently with every fell since fragments
  never retire. This is U5's face-merge deliverable; it is a *population* cost, not a per-fell one.
- **Character queries** test exact OBBs against every box of every body (`86d2d85`). Correct and
  bounded, but grows linearly with the number of settled fragments near the player — another
  argument for U5 freezing them out.
- **Solver iterations** over the contact set, which the box count drives.

**Design consequence for U6 (impact fracture).** Fracture-on-landing *increases* body and box
count at the exact moment of peak contact activity. With the world-size-linear scan still in place,
a stand of trees landing together would multiply an already-quadratic-feeling cost. U1a is
therefore a hard prerequisite for U6, not merely an optimization — and it is the reason U5
(retirement) is scheduled before U6 as well.

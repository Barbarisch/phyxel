# Fracture Modes — deciding WHAT breaks and INTO WHAT

**Status:** **gate run once (2026-09-22) — NEEDS WORK, 7 items, all resolved below.** Nothing
built. Verdict of record: §11.

The design direction held — the extent/granularity split survived unchanged — but four findings were
hard gate requirements the first draft simply did not have (equality test, validation plan, clamp,
and the memory cost it called "unmeasured" while the numbers to measure it already existed).

**Parent:** [`DestructionSystemV2.md`](DestructionSystemV2.md) §5.A (material break model) and §5.E
(axe-chop kerf). This document does not supersede either; it proposes unifying them.
**Sibling:** [`VoxelDamageVisualization.md`](VoxelDamageVisualization.md) — the crack surface
treatment. §7 below records a real dependency between the two.

---

## 1. What this is

The engine currently decides destruction outcomes with a single number. This document proposes
splitting that into two, so that a **pickaxe can take a bite out of a stone block without deleting
the whole cubic metre** — the most common mining interaction in the genre, and one the engine
cannot currently express.

### 1.1 Aesthetic commitments (gate pass 1)

- **Carving is sub-voxel by construction.** Cells are removed at subcube (1/3) or microcube (1/9)
  resolution from a cube that stays standing. Nothing is authored in full cubes.
- **UNCONDITIONAL.** Carving is never behind a flag or quality tier. Cost may be bounded by *how
  many cells are evaluated*, never by *whether carving happens* — the `bladesForDistance` pattern.
  A carve that silently degrades to whole-cube removal at distance would be a design-key violation,
  not an optimization.
- **Cut-face material: the carved cell exposes the PARENT material, unchanged.** Stated because it
  was a gap, not because it is obvious: the axe kerf deliberately re-materials cut faces to
  `LogHeartwood` so a chopped log does not show bark on the cut (`DamageSystem.h` kerf section).
  Stone, Metal and Bricks have no interior analogue in `materials.json`, so V1 keeps the parent
  material and accepts that a carved stone face looks like stone. **Open for later:** an interior
  material per family (quarried/fresh-fracture faces) is a real aesthetic improvement and is
  recorded in §8 rather than guessed at now.

---

## 2. Honest baseline — what is shipped

| Layer | State | Evidence |
|---|---|---|
| Blast damage, tiered by overkill | **Live** | `DamageSystem::applyDamage`; `ratio = effective / mr.toughness` (`DamageSystem.cpp:197`) |
| Tier ladder: whole / subcube / microcube | **Live** | `DamageSystem.cpp:292` (`ratio < mr.s1` → intact cube), `:296` (`< mr.s2` → subcubes), else microcubes |
| Per-material `s1`/`s2` | **Live, data-driven** | `resources/materials.json` break blocks |
| Damage accumulation → cracks | **Live (P0–P3)** | `VoxelDamageVisualization.md`; ratio 0→1 renders 3 crack stages |
| **Sub-voxel carving from a point tool** | **Live, but ONLY for axes on wood** | `DamageSystem.h:138` — *"Axe-chop kerf: **FRACTURE, not blast**"*; `carveChopKerf` (`:174`) |
| Subdivision primitives | **Live, public** | `ChunkVoxelManager::subdivideAt` (`:110`), `subdivideSubcubeAt` (`:119`) |
| **Damage footprint / contact area** | **Does not exist** | `apply_damage` takes `radius`, `energy`, `shape`, `thickness`, `radii` — all describe the BLAST VOLUME, none describe the CONTACT AREA |
| **Detachment as its own failure mode** | **Does not exist** | `bondStrength` is used only to *derive* toughness when a material has no break block (`DamageSystem.cpp:43`) |

Measured tier thresholds, for reference (energy required, from `materials.json`):

| material | toughness | `s1` | `s2` | cracks | pops whole | subcubes | microcubes |
|---|---|---|---|---|---|---|---|
| Glass | 35 | 1.3 | 2.2 | 0–35 | 35–46 | 46–77 | 77+ |
| Dirt | 45 | 1.6 | 4.0 | 0–45 | 45–72 | 72–180 | 180+ |
| Wood | 70 | 3.5 | 9.0 | 0–70 | 70–245 | 245–630 | 630+ |
| Stone | 110 | 1.8 | 4.0 | 0–110 | 110–198 | 198–440 | 440+ |
| Metal | 200 | 4.5 | 11.0 | 0–200 | 200–900 | 900–2200 | 2200+ |
| Steel | 220 | 4.5 | 11.0 | 0–220 | 220–990 | 990–2420 | 2420+ |

### 2.1 Chunk independence — the equality test (gate pass 1)

Extent and granularity are already functions of world position: `applyDamage` iterates **world**
coordinates and resolves `getCubeAt(wp)` per cell, so no appearance quantity is chunk-derived.
`subdivideAt(localPos)` takes a chunk-LOCAL position, but that is storage addressing, not
appearance.

**The risk is a carve that straddles a chunk border.** A contact region at x ≈ 31.97 with radius
0.05 spans chunks (0,0,0) and (1,0,0). An implementation that resolves one chunk and works locally
would silently drop half the cells — the exact silent-drop class `FeatureDesignKeys.md` warns
about. `carveChopKerf` takes a single `hitCell` and **whether it handles crossing is unverified**;
F1 generalizes it, so it inherits whatever it does.

> **`FractureCarveSeamTest.CarveIsIdenticalAcrossAChunkBoundary`** — apply an identical carve (same
> energy, same `contact_radius`, same sub-voxel offset within the cell) at a chunk-INTERIOR cell and
> at a cell straddling **x = 31/32**. Assert the **set of removed cells is congruent under
> translation**.
> **Fails on a chunk-local implementation with:** `interior carve removed 14 cells, seam carve
> removed 8 — 6 cells in chunk (1,0,0) were never visited.`

F1 is not buildable until this test exists; the keys doc makes that explicit.

---

## 3. The problem: two questions, one number

`ratio` currently answers **both** of these:

1. **EXTENT** — how much material is removed?
2. **GRANULARITY** — what size are the resulting pieces?

They are not the same question, and conflating them is what makes the pickaxe impossible:

- To chip a little, you want a *low* ratio — but low ratio means "pops off whole".
- And **any** ratio ≥ 1.0 removes the **entire cube** (`DamageSystem.cpp:281`, `removeCubeFast`).

So the outcome space the engine can express is exactly: *nothing*, or *this whole cubic metre is
gone, in pieces of some size*. There is no "took a bite out of it".

**The engine already disagrees with itself about this.** The axe kerf is labelled
*"FRACTURE, not blast"* precisely because a chop must carve a notch rather than delete the log —
so the correct behaviour already exists, hardcoded for one tool and one material family, with no
shared model. This proposal is largely about generalizing it rather than inventing anything.

### 3.1 Order-independence — carving is a MUTATION, and that has a known trap

The keys doc's order-independence rule targets *generators*: identical output per-chunk or
whole-region, in any order. Carving is a **world mutation**, not a generator, so that rule does not
apply to it the same way — but a closely related one does, and the blast path already learned it
the hard way.

`applyDamage` computes shielding against the **PRE-BLAST grid** and defers every removal to Phase B
(`DamageSystem.cpp:149-152`), because removing voxels mid-scan un-shields the voxels behind them in
iteration order and carves an axis-aligned trench instead of a symmetric crater.

**Carving must obey the same discipline: decide against the pre-scan snapshot, mutate in Phase B.**
A carve that removes cells mid-scan reintroduces that exact bug. This is a build constraint, written
here so it is not rediscovered.

**World recipe:** not needed. `contact_radius` is per-call. If tool radii later become data they
belong in a tools/items JSON, **not** `world_meta` — stated because "per-material tuning"
instinctively reaches for the recipe.

---

## 4. Options considered

### 4.1 Option A — energy DENSITY instead of total energy

Divide by the loaded area, so a point tool has huge density and a blast rim has low density.

**Rejected as insufficient on its own.** It changes the *granularity* a pickaxe produces (dust
instead of a whole cube) but does nothing about *extent* — the pickaxe still deletes the entire
cube, just as powder. It fixes the wrong half of the problem.

### 4.2 Option B — distance-from-centre drives granularity

Whole cubes at the blast centre, finer pieces at the rim (or the reverse).

**Rejected.** Granularity already falls out of energy density via the falloff term, so this would
be a *second source of truth* that can disagree with the first. It is also backwards from both the
physics and the shipped behaviour: density is highest at the centre (`fall = 1.0`), so the current
model already produces a **pulverized core with chunky ejecta at the rim** — a crater. That is the
one case the existing model gets right, and it should not be disturbed.

### 4.3 Option C — footprint as a second axis ✅ **recommended**

Add the **contact footprint** to the damage call, and let it decide EXTENT while energy density
continues to decide GRANULARITY.

| footprint vs one voxel | extent | granularity |
|---|---|---|
| **smaller** (pickaxe, bullet, chisel) | subdivide the struck cube; remove only the cells inside the contact region. **The cube stays attached.** | subcube or microcube, by density |
| **comparable** (sword, small charge) | either — carve if concentrated, detach if spread | unchanged ladder |
| **larger** (blast) | **unchanged** — whole cubes removed, current tiering | unchanged ladder |

The blast path does not change at all. This is additive.

### 4.4 Option D — detachment as a distinct third failure mode

A cube pops off **whole** when its bonds to neighbours fail but the cube itself survives: enough
energy to knock it loose, not enough to fracture it. That is physically a different event from
comminution, and the engine already carries the data — `bondStrength` per material, plus a
`bonds` array on `Cube`.

Today `bondStrength` is only a *fallback to derive toughness* (`DamageSystem.cpp:43`), which
conflates "how hard to break it" with "how hard to knock it loose". Separating them would make the
pop-off-whole tier mean something physical rather than being "the bottom rung of the overkill
ladder".

**Recommended, but as a later phase than C** — it is a change to shipped blast behaviour and to a
material property 101 of 108 materials currently lean on, whereas C is purely additive.

---

## 5. Proposed model

Two axes, evaluated per voxel:

```
footprint  = contact radius of the damage source, in world units
density    = energy delivered to this voxel / area over which it is delivered

EXTENT:      footprint < voxelSize  ->  carve sub-voxel cells inside the contact region
             footprint >= voxelSize ->  whole-voxel removal (current behaviour)

GRANULARITY: unchanged -- ratio vs s1/s2 decides whole / subcube / microcube
```

**API shape (to be gated).** `apply_damage` gains one optional field. Existing callers are
unaffected, which matters because `radius`/`shape`/`thickness`/`radii` already describe the blast
*volume* and none of them describe *contact area*:

```jsonc
"contact_radius": 0.05   // world units; omitted = blast (footprint >= voxel), current behaviour
```

Per the API design key: units named at the field; omitted-means-unchanged follows the
`/api/debug/*` convention; the response must echo what the call actually did — at minimum
`carved` (sub-voxel cells removed) alongside the existing `broken`/`grazed`/`stage_changed`, so a
caller can tell a carve from a detach without photographing the result. It should ALSO echo
**resulting state**, not only the event: `cells_remaining` in the carved cube, so a caller can
assert mining PROGRESS rather than merely that a call happened (the same rule that made
`/api/world/voxel` echo `toughness` and `damage_stages_max`).

**CLAMP — `contact_radius` is floored at one microcube (1/9 world unit), and the reason belongs at
the clamp site.** Density is `energy / area`, so an unclamped radius breaks a real invariant:
- `contact_radius = 0` → **division by zero**, or an infinite density that pins every hit to the
  microcube tier;
- negative → a nonsense region;
- enormous → silently becomes a blast, which may be correct but must be deliberate rather than
  accidental.

One microcube is the floor because **energy cannot be delivered to less than the smallest
representable cell** — below that the model has nothing to remove, so a smaller number is not a
finer carve, it is an undefined one.

---

## 6. Phasing (proposed)

| Phase | Work | Why this order |
|---|---|---|
| **F0** | `contact_radius` plumbed through `apply_damage` → `DamageSystem`, echoed in the response. No behaviour change. | Observable before it is load-bearing |
| **F1** | **Carve branch only:** generalize the kerf from "axe on wood" to "any point tool on any material", via `subdivideAt` / `subdivideSubcubeAt` | Delivers the pickaxe — the case that is actually missing. Blast path untouched |
| **F2** | Occupancy / walkability regression pass on carved cells | Carving changes occupancy, which feeds `SpawnGate`, walkability validators and the structure vegetation gate |
| **F3** | Option D: detachment vs fracture split, `bondStrength` promoted to a real property | Changes shipped blast behaviour; wants its own evidence |

**F0 and F1 are additive** — with `contact_radius` omitted, every existing call behaves exactly as
today, so no pinned test moves.

**F3 is not**, and per the defaults-are-a-pinned-contract key it must name what it breaks and update
it in the same commit. It changes blast outcomes AND repurposes `bondStrength`, which **101 of 108
materials currently lean on to derive toughness** (`DamageSystem.cpp:43`). The tests that would
move: `DamageSystemBreakProfileTest` (pins `responseFor` values directly) and the **34 destruction
integration tests** — `ChopKerfIntegrationTest`, `CoherentCollapseIntegrationTest`,
`TreeCollapseIntegrationTest`. F3 does not start until a baseline run of those 34 is recorded.

---

## 7. Dependency on the crack work (real, not hypothetical)

Cracks are **cube-granularity state**. A *carved* cube is a subdivided cell, and subdivided cells
do not accumulate damage — `DamageSystem.cpp:196`: *"Accumulation is cube-only; sub-voxel cells
break in one pass."*

**So under F1, a partially-mined block would go visually PRISTINE the moment it is first carved.**
That is the same defect `VoxelDamageVisualization.md` §3.6 records for V1 and §15 (P0.5) proposes
to fix with a per-parent-cube damage aggregate.

This is the second independent route to that conclusion — the first arrived from the rendering
side (cracks cannot appear on generated buildings), this one from the physics side. **It is
evidence for scheduling V2 sooner**, and F1 should probably not ship before it.

---

## 8. Open questions (NOT resolved here)

1. **What is a tool's contact radius, physically?** A pickaxe tip is ~1 cm; a microcube is 11 cm.
   If contact radius is below one microcube, what fails — one microcube, or a region scaled by
   energy? Needs grounding, not a guessed constant.
2. **Cost of carving at scale — QUANTIFIED at the gate, and it is the sharpest risk to F1.**
   The first draft called this "unmeasured" while the numbers to measure it already existed:
   `VoxelDamageVisualization.md` §15 (P0.5) measured `Subcube` **120 B** and `Microcube` **128 B**,
   each plus an 8-byte `unique_ptr` slot.

   | state | memory |
   |---|---|
   | intact `Cube` | **176 B** |
   | carved to subcube resolution (27 cells) | ≈ **3.6 KB** |
   | one subcube further carved to micros | ≈ **7 KB** |
   | **fully micro-subdivided cube (729 cells)** | ≈ **97 KB** |

   That worst case is **over 500× the intact cube**, against a **1.00 MB/chunk** budget
   (`docs/AgentContext.md:650`) — so roughly **ten fully-carved cubes consume an entire chunk's
   memory budget**, and a mined tunnel is exactly that shape.

   Partial carves are far cheaper (≈ 7 KB), so the real question is **how fast repeated mining
   drives cubes toward full subdivision**. That is the measurement F1 needs BEFORE it is built:
   carve a tunnel of N cubes at mining cadence and record `Subcube`/`Microcube` counts, chunk RSS,
   and frame time. It is no longer a blank — it has a starting number and a budget to blow.

   **Face count is the second half of the same cost.** A carved cube emits sub-voxel faces instead
   of one merged cube face, so tunnel walls multiply face count the same way a damage gradient
   fragments merge runs (§3.5 of the damage plan measured that effect for cubes). Measure both in
   the same run.
3. **Does a carved cube become structurally weaker?** `CrossSectionAnalyzer` (`DamageSystem.h:178`)
   already scores cross-sections by occupied subcube area, so a carved cube would automatically
   score weaker for collapse. Whether that is desirable or a surprise is untested.
4. **Sub-voxel cells still break in one pass.** Should a carved cell be re-carvable at finer
   granularity (microcube out of a subcube), or does it break as a unit once subdivided?
5. **Detachment thresholds are ungrounded.** Option D needs real `bondStrength` values; today they
   exist mainly to derive toughness for the 101 materials with no break block — which is
   pre-existing open question #5 in `DestructionSystemV2.md` and is not made better by leaning on
   them harder.

---

## 9. Validation plan (gate pass 1 — the first draft had none)

**What "works" means, measurably.** After a carve at `contact_radius` r on a Stone cube:
1. the cube **still exists** — `hasVoxelAt(wp) == true`;
2. the removed cells are **exactly** those whose centres lie within r of the contact point, counted
   against a number computed in advance;
3. `carved > 0` and `broken == 0` in the response.

**Required depth: L2 + L3 + L4.**
- **L2** — structural invariant on real output: *which* cells were removed, read back from the
  world, not inferred from the response.
- **L3 is NOT dispensable here**, unlike the crack work. Carving changes **occupancy**, so a carved
  floor can open a hole a character falls through. `TraversalProbe` applies, and F2 exists for it.
- **L4** — live engine: the notch is visible and the cube is still standing.

### 9.1 R-F1 — the red test

> **`FractureCarveTest.PointToolCarvesWithoutRemovingTheCube`** — carve a Stone cube with
> `contact_radius = 0.05` and energy above toughness. Assert the cube survives and ≥ 1 microcube
> was removed.
> **Fails today with:** `hasVoxelAt(16,16,16) == false — the entire cube was removed`, because any
> `ratio >= 1` reaches `removeCubeFast` (`DamageSystem.cpp:281`). That single line is the whole
> feature gap, and this is the test that states it.

### 9.2 The rig

- **One Stone cube at (16,16,16)**, inside chunk (0,0,0), **above y = 16** (a Flat world's surface
  IS sea level, so a rig below it is buried and renders nothing — the trap that already cost the
  damage-visualization plan a pass).
- **One variable:** `contact_radius`. Energy held constant.
- **Prediction written before the run:** at r = 0.05 (under one microcube, so clamped to 1/9) the
  carve removes the single microcube containing the contact point. At r = 0.35 (just over one
  subcube) it removes the cells of one subcube neighbourhood. Exact counts computed and written
  down before executing.
- **Control, mandatory:** the **same energy with `contact_radius` omitted** must still remove the
  whole cube. Without it, a passing carve test could equally mean "the energy was too low to break
  anything", and the test would be measuring nothing. This is the in-frame-pristine lesson from the
  grass-coverage metric, applied to a physics test.
- **Verify the world, not the response** — `carved` counts what the engine *thinks* it did; read the
  cells back.

### 9.3 Chunk-independence and cost

- `FractureCarveSeamTest.CarveIsIdenticalAcrossAChunkBoundary` — §2.1, with its failure text.
- **The cost run is a gate on F1, not a follow-up** (§8.2): carve a tunnel of N cubes at mining
  cadence; record `Subcube`/`Microcube` counts, chunk RSS against the 1.00 MB budget, face count,
  and frame time. Prediction to be written before the run. If a tunnel of realistic length blows
  the chunk budget, F1's design is wrong and no amount of tuning fixes it — which is why this runs
  **before** the carve branch is built, not after.

### 9.4 How the rig differs from shipped defaults

A single cube on a Flat world is not a mining scene: face counts, merge behaviour and memory are
all unrepresentative, so **rig numbers may not be quoted as cost**. Cost comes from §9.3's tunnel
run only. The rig is for correctness (which cells, cube survives); the tunnel is for cost.

---

## 10. What this document does NOT claim

- **No measurement has been taken of the feature itself.** Every number in §2 is read from shipped
  code or `materials.json`; the memory figures in §8.2 are arithmetic on P0.5's measured struct
  sizes, not a profile of a carve.
- The footprint model is **not validated**; §4.3 is an argument, not evidence.
- §9 is a plan, not a result. Nothing in it has been run.

---

## 11. Gate verdict — verdict of record

### Pass 1 — 2026-09-22 — **NEEDS WORK, 7 items** (all resolved above)

> The design direction holds. Nothing here requires a redesign, and the extent/granularity split is
> sound — but the document is a proposal, not a build plan, and four of these are hard gate
> requirements.

| # | Item | § | Severity | Resolved in |
|---|---|---|---|---|
| 1 | No chunked-vs-whole-region equality test — the keys doc makes this a build blocker | §2 | **Blocker** | §2.1 |
| 2 | No validation plan at all — no measurable "works", depth, red test, rig, prediction or control | §5 | **Blocker** | §9 |
| 3 | No clamp on `contact_radius`; 0 is a division by zero / infinite density | §4 | **Blocker** | §5 |
| 4 | Carve memory cost called "unmeasured" when it was already quantifiable | §8 | **Significant** | §8.2 |
| 5 | Order-independence unaddressed; carving mid-scan reintroduces the un-shielding trench bug | §3 | Moderate | §3.1 |
| 6 | Cut-face material unspecified; the kerf sets `LogHeartwood`, stone/metal have no analogue | §1 | Moderate | §1.1 |
| 7 | F3 names no pinned tests despite changing shipped blast behaviour | §4 | Moderate | §6 |

**The finding worth remembering** is #4. The draft wrote "unmeasured" about carve cost while the
numbers needed to bound it already existed in a sibling document — P0.5 had measured `Subcube` at
120 B and `Microcube` at 128 B a few hours earlier. "Unmeasured" was true of the profile and false
of the estimate, and the estimate is alarming: **≈97 KB for a fully carved cube against a 1.00
MB/chunk budget.** Calling something unmeasured is not the same as having nothing to say about it,
and the gap between those two is where a feature gets built before anyone notices it cannot fit.

# Fracture Modes — deciding WHAT breaks and INTO WHAT

**Status:** proposal, not gated, nothing built. Written 2026-09-22 for `/design-check`.
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
caller can tell a carve from a detach without photographing the result.

---

## 6. Phasing (proposed)

| Phase | Work | Why this order |
|---|---|---|
| **F0** | `contact_radius` plumbed through `apply_damage` → `DamageSystem`, echoed in the response. No behaviour change. | Observable before it is load-bearing |
| **F1** | **Carve branch only:** generalize the kerf from "axe on wood" to "any point tool on any material", via `subdivideAt` / `subdivideSubcubeAt` | Delivers the pickaxe — the case that is actually missing. Blast path untouched |
| **F2** | Occupancy / walkability regression pass on carved cells | Carving changes occupancy, which feeds `SpawnGate`, walkability validators and the structure vegetation gate |
| **F3** | Option D: detachment vs fracture split, `bondStrength` promoted to a real property | Changes shipped blast behaviour; wants its own evidence |

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
2. **Cost of carving at scale.** Subdividing is 27 or 729 cells per cube. The kerf already pays
   this for single chops; a mining loop is many carves per second. Unmeasured.
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

## 9. What this document does NOT claim

- No measurement has been taken. Every number above is read from shipped code or `materials.json`.
- The footprint model is **not validated**; §4.3 is an argument, not evidence.
- No validation depth, red test, or test rig is specified yet — deliberately, since that is what
  the design-check gate is for.

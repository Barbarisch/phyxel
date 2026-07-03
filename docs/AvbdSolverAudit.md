# AVBD Solver Audit — Phyxel `GpuParticlePhysics` vs the published method

> Item #3 of [`docs/EngineAdvancesResearch.md`](EngineAdvancesResearch.md) ("cheap high-value
> audit"). This document compares Phyxel's GPU debris solver against **Augmented Vertex Block
> Descent** (Giles, Diaz, Yuksel — SIGGRAPH 2025 / ACM TOG 44(4) Art. 90, DOI
> [10.1145/3731195](https://doi.org/10.1145/3731195)). Analysis + plan only — **no engine code
> was changed for this audit.**
>
> **Provenance:** paper text was extracted locally (pypdf) from the official PDF
> ([Augmented_VBD-SIGGRAPH25.pdf](https://graphics.cs.utah.edu/research/projects/avbd/Augmented_VBD-SIGGRAPH25.pdf),
> 12 pages); all paper claims below cite its section/equation/table numbers. All Phyxel claims
> cite file:line of the current working tree (branch `structure-gen-placement-defect-fixes`,
> 2026-07-02). Where something could not be established from the code, it is marked **unclear**
> rather than guessed.

---

## 1. Executive summary

**Phyxel already implements the real AVBD algorithm, not just "XPBD with extra steps."**
The live compute pipeline (`solver_*.comp`) is a faithful primal-dual AVBD port — per-body 6-DOF
block solves with LDL factorization, augmented-Lagrangian contact forces `f = κ·C + λ` with
Coulomb-cone clamping, penalty ramping `κ += β|C|`, the paper's α-regularized error correction,
and frame-to-frame warm-starting of both λ and κ through a persistent GPU hash table. Code
comments repeatedly reference a source implementation called "Shallot"
([solver_types.glsl:1](../shaders/solver_types.glsl), [GpuParticlePhysics.cpp:821](../engine/src/core/GpuParticlePhysics.cpp));
no copy of Shallot exists in the repo, so its exact identity is **unclear** (it is not the
paper's own artifact name), but the transcription matches the paper's Algorithm 1 closely.

The audit therefore found **no formulation rewrite worth doing**. What it did find:

1. **Iteration economics headroom** — Phyxel runs **8 iterations × up to 4 substeps** where the
   paper stabilizes 110,000-body piles at **4 iterations, no substepping**. Halving iterations is
   the cheapest path to raising the 10k particle cap.
2. **Two genuine silent-failure defects** vs the paper: bodies whose graph color exceeds 11 (or
   that never get colored) **silently skip the primal solve**, and constraints past the 60,000
   cap are **silently dropped**. Both are masked by the non-physical hard-contact projection
   pass and would surface exactly in Phyxel's headline use case (dense piles).
3. Several deliberate simplifications that are *fine for cube debris* (scalar sphere inertia,
   no joints, no static/dynamic friction split) and a few loose ends (restitution plumbed but
   dead, `GAMMA` doing double duty as a damping constant, three dead shader files).

---

## 2. The published method (grounded in the paper)

Source: [project page](https://graphics.cs.utah.edu/research/projects/avbd/) ·
[paper PDF](https://graphics.cs.utah.edu/research/projects/avbd/Augmented_VBD-SIGGRAPH25.pdf) ·
[Real-Time Live! abstract](https://dl.acm.org/doi/10.1145/3721243.3735982) ·
[VBD predecessor (Chen et al. 2024)](https://arxiv.org/html/2403.06321v1) ·
[a readable WebGPU implementation](https://www.webgpu.com/showcase/webphysics-webgpu-avbd-solver/).

### 2.1 Formulation

VBD (§2.2) minimizes the implicit-Euler variational energy
`x = argmin ½Δt⁻²‖x−y‖²_M + E(x)` one *vertex/body block* at a time (Gauss-Seidel), each block
update being a single Newton step `H·Δx = f` on a small (3×3 particle / 6×6 rigid-body) system.
It is a **primal** method: robust to high *mass* ratios, weak at high *stiffness* ratios, and
unable to express hard (infinite-stiffness) constraints.

AVBD (§3) adds an **augmented Lagrangian** term per constraint (Eq. 8):

```
E_j = ½ κ_j C_j² + λ_j C_j          force: f_j = −(κ_j C_j + λ_j) ∂C_j/∂x     (Eq. 9)
```

with a **dual update after every primal iteration** (Eq. 11, Alg. 1 line 28):
`λ ← clamp(κ·C + λ, λ_min, λ_max)`, and **penalty ramping** (Eq. 12): `κ ← κ + β|C|`, applied
*only while λ is strictly inside its bounds* (§3.2). This makes AVBD a hybrid **primal-dual**
method: κ never needs to reach the true (infinite) stiffness — λ carries the converged force.

### 2.2 Contacts and friction (§3.2–3.3)

- Contact = 3-row constraint in an (n, t, b) basis (Eq. 15). Normal row bounds:
  `λ_n ∈ [0, ∞)` (push only). Friction rows: isotropic cone `‖λ_tb‖ ≤ μ·λ_n` enforced by
  clamping the multiplier vector.
- Optional **stiffness rescaling** (Eq. 14) for a better clamped-force Hessian; the paper's
  results use the simpler unclamped-Hessian variant for friction (§5) except one test (§5.3).
- **Static-friction anchoring**: if the previous frame's friction force stayed inside the cone,
  contact points are pinned tangentially in collision detection, and the solver switches between
  static/dynamic μ (§3.3). Explicitly "not required" but improves low-iteration behavior.

### 2.3 Stabilization, warm-starting, Hessians

- **Explosive-error-correction guard** (Eq. 18): `C(x) = C*(x) − α·C*(x_t)` with **α = 0.95** —
  only (1−α) of pre-existing penetration is corrected per step, Baumgarte-style (§6).
- **Warm start** (Eq. 19, Table 2): `κ⁰ = max(γ·κᵗ, κ_start)`, `λ⁰ = α·γ·λᵗ`, with **γ = 0.99**.
  λ is additionally scaled by α so error-correction energy is not carried over. Warm starting is
  the paper's biggest convergence lever — Fig. 2e's example converges in **1 iteration** with it.
- **Penalty ramp rate β = 10**, insensitive across β ∈ [1, 1000] (§3.1). κ and λ are bounded to
  large finite values for safety (§4 "Bounding the Dual Variables").
- **SPD Hessian guarantee** (§3.5): the geometric-stiffness term `G = λ·∂²C/∂x²` is replaced by
  a diagonal column-norm approximation; the always-quasi-Newton system is solved with **LDLᵀ**.
  For contacts, the paper drops the second-order Taylor term of C entirely (§4 "Approximating
  Constraints") — contacts are linearized about frame-start positions and cached.

### 2.4 Parallelization (§4)

One incremental **greedy body coloring** per time step (parallel Jacobi rounds, converges "after
a few iterations"); all bodies of a color solve in parallel; position updates are
**double-buffered** so a rare same-color conflict degrades that pair to a Jacobi update instead
of a race. One extra fully-parallel pass per iteration does the dual/κ updates. Velocities are
reconstructed at the end: `v = (x − x_t)/Δt` (Alg. 1 line 37) — no restitution model appears in
the paper.

### 2.5 Claimed numbers (all RTX 4090, Δt = 1/60, no substepping)

| Scene | Bodies | Iterations | Solver time | +collision detection |
|---|---|---|---|---|
| Fig. 1 pile smash | 110,000 | 4 | 3.5 ms | 9.8 ms |
| Fig. 3 double pile | 510,000 | 3 | 10.3 ms | 17.6 ms |
| Fig. 14 joints+cloth | 35,000 bodies, 72,000 joints, 10,000-vertex cloth | 10 | — | 16 ms |

Stability claims: stable stacking and friction at these iteration counts (Fig. 10–11), a
50-body articulated chain with a 50,000:1 mass ratio (Fig. 7), card towers held by static
friction (Fig. 6). Comparison baselines (Table 1): sequential impulse needed 15–27 iterations,
XPBD 6–26 *substeps*, VBD 8–15 iterations for the same scenes — and XPBD's 510k pile still
collapsed after several seconds at 26 substeps (§5.5).

Honest caveat on these numbers: they are for **rigid boxes with an LBVH broad phase** in a
purpose-built DX11 demo. They bound what's achievable; they don't predict Phyxel's frame times.

---

## 3. What Phyxel implements today (grounded, file:line)

### 3.1 Scope and pipeline

`GpuParticlePhysics` handles **broken-voxel debris only** (cubes/subcubes/microcubes); furniture
and characters live on the CPU `VoxelDynamicsWorld`
([GpuParticlePhysics.h:20-32](../engine/include/core/GpuParticlePhysics.h)). Two pipelines
exist; the flag `m_useNewPipeline` is **hard-coded true** — the "legacy XPBD" path
(`particle_integrate/collide.comp`) is dead code kept for reference
([GpuParticlePhysics.h:306-312](../engine/include/core/GpuParticlePhysics.h)). The live path is
the AVBD constraint solver. Additionally, `shaders/solver_apply.comp`, `solver_jacobi.comp`, and
`solver_graph_color.comp` are **orphaned** — no pipeline in `GpuParticlePhysics.cpp` references
them (grep confirms zero matches), they are leftovers of an earlier constraint-colored velocity
solver.

Per physics tick, `recordComputeCommandsNew` dispatches
([GpuParticlePhysics.cpp:781-1046](../engine/src/core/GpuParticlePhysics.cpp)):

```
sync_in → integrate → grid clear/build/scan/scatter → narrowphase (dyn-dyn)
→ voxel contacts (dyn-static) → CSR adjacency build → body coloring (16 rounds)
→ [ dual → primal ×12 colors ] ×8 iterations → hard-contact projection
→ sync_out → warmstart save
```

Timestep: fixed 1/60 s accumulator, **up to 4 ticks per render frame**
([GpuParticlePhysics.h:400](../engine/include/core/GpuParticlePhysics.h),
[GpuParticlePhysics.cpp:1141-1148](../engine/src/core/GpuParticlePhysics.cpp)).

### 3.2 Formulation — genuine AVBD primal-dual

- **Primal** ([solver_primal.comp:6-9](../shaders/solver_primal.comp)): per body of the target
  color, assembles the 6×6 block system `(M/Δt² + Σ κ·JᵀJ)·Δq = −(M/Δt²·(pos−inertial) + Σ JᵀF)`
  and solves via **LDL factorization** ([solver_primal.comp:31-76](../shaders/solver_primal.comp))
  — exactly the paper's Eq. 4 with the SPD/LDLᵀ recommendation of §3.5. Forces use the AL form
  `f_n = min(κ_n·C_n + λ_n, 0)` with the friction cone clamp `‖f_t‖ ≤ μ|f_n|`
  ([solver_primal.comp:223-234](../shaders/solver_primal.comp)) — Alg. 1 line 14.
- **Dual** ([solver_dual.comp:77-116](../shaders/solver_dual.comp)): per constraint, fully
  parallel, no body writes. `λ_n ← min(κ_n·C_n + λ_n, 0)`; friction multipliers cone-clamped;
  penalties grown by `κ ← min(κ + β|C|, κ_max)` **only while the constraint is active/inside the
  cone** ([solver_dual.comp:84-107](../shaders/solver_dual.comp)) — matches Eq. 11/12 + the §3.2
  "don't grow when clamped" rule. A `stick` flag (in/out of cone) is computed and persisted
  ([solver_dual.comp:115](../shaders/solver_dual.comp)).
- **α-regularized error correction**: constraints are linearized about frame-start positions
  with `C_n = −(1−α)·C_init + J·Δq`
  ([solver_dual.comp:60-67](../shaders/solver_dual.comp),
  [solver_primal.comp:209-213](../shaders/solver_primal.comp)) — the paper's Eq. 18 combined
  with the §4 Taylor approximation (second-order term dropped, as the paper itself does for
  contacts). Contact arms `rA/rB` are stored body-local and re-rotated by the current quaternion
  each iteration ([solver_types.glsl:13](../shaders/solver_types.glsl)).
- **Velocity reconstruction**: `v = (pos − initial)/Δt`, angular from the quaternion delta
  ([solver_sync_out.comp:38-42](../shaders/solver_sync_out.comp)) — Alg. 1 line 37.
- **Adaptive initialization**: the integrate pass backs the gravity prediction out for at-rest
  bodies via an `accelWeight` blend, explicitly noting it substitutes a **velocity-magnitude
  heuristic** for Shallot's acceleration-based weight because `prevVel` is not persisted across
  frames ([solver_integrate.comp:92-126](../shaders/solver_integrate.comp)). This corresponds to
  Alg. 1 line 4 ("initial guess with adaptive initialization").

**Iteration order note:** Phyxel runs *dual then primal* within each iteration
([GpuParticlePhysics.cpp:992-1010](../engine/src/core/GpuParticlePhysics.cpp)); the paper's
Alg. 1 runs *primal then dual*. Since Phyxel's constraints are seeded by warm-start before the
loop, the first dual pass operates on the α-scaled carried λ at the predicted positions — a
minor phase shift, not a different algorithm. No evidence this matters; flagged for completeness.

### 3.3 Contact generation

- **Dynamic-dynamic** ([solver_narrowphase.comp](../shaders/solver_narrowphase.comp)): 64³
  spatial hash grid broad phase (sorted via parallel prefix sum,
  [GpuParticlePhysics.cpp:855-912](../engine/src/core/GpuParticlePhysics.cpp)), then full
  15-axis SAT OBB-OBB, Sutherland-Hodgman clipped **4-point face manifolds** plus 1-point edge
  contacts ([solver_narrowphase.comp:165-356](../shaders/solver_narrowphase.comp)). Friction
  coefficient = plain average of the two bodies' μ
  ([solver_narrowphase.comp:473](../shaders/solver_narrowphase.comp)).
- **Dynamic-static** ([solver_voxel.comp](../shaders/solver_voxel.comp)): OBB vs unit-voxel SAT
  against the 512×256×512 occupancy bitfield, keeping the **4 deepest contacts per body**
  ([solver_voxel.comp:144-188](../shaders/solver_voxel.comp)). Comments in both emitters note the
  4-point manifold is essential because "single-point gives rocking/popcorn"
  ([solver_narrowphase.comp:257-259](../shaders/solver_narrowphase.comp)).

### 3.4 Warm-starting (the CLAUDE.md "warm-started" claim, verified)

What is actually carried frame-to-frame, per contact feature:
**λ (all 3 rows), κ (all 3 rows), the stick flag, and the local contact arms**, in a persistent
GPU open-addressed hash table (131,072 slots, 128-probe linear probing) keyed by body-pair/voxel
+ feature ([solver_types.glsl:50-80](../shaders/solver_types.glsl),
[solver_warmstart_save.comp](../shaders/solver_warmstart_save.comp)). On re-detection the
narrowphase seeds `λ⁰ = α·λᵗ` and `κ⁰ = clamp(γ·κᵗ, κ_min, κ_max)`
([solver_narrowphase.comp:141-153](../shaders/solver_narrowphase.comp),
[solver_voxel.comp:256-268](../shaders/solver_voxel.comp)). The table is initialized once and
deliberately **never cleared** — a comment records that clearing it per frame reduced AVBD to
pure penalty and bodies sank through the floor
([GpuParticlePhysics.cpp:820-830](../engine/src/core/GpuParticlePhysics.cpp),
[GpuParticlePhysics.h:335-338](../engine/include/core/GpuParticlePhysics.h)). Positions are NOT
warm-started beyond the adaptive initial guess (§3.2 above) — same as the paper.

Difference vs paper Eq. 19: Phyxel's λ seed uses **α only**, the paper uses **α·γ**. With
γ = 0.999 that is a 0.1% difference — numerically irrelevant, noted for fidelity.

### 3.5 Parameters (Phyxel vs paper)

| Parameter | Phyxel | Where | Paper | Note |
|---|---|---|---|---|
| α (error regularization) | **0.99** | [solver_types.glsl:83](../shaders/solver_types.glsl) | 0.95 (Table 2) | Comment: lower values "inject too much energy → popcorn" — a deliberate, scene-specific retune. |
| β (penalty ramp) | **100000.0** | [solver_types.glsl:84](../shaders/solver_types.glsl) | 10, insensitive in [1,1000] (§3.1) | Not directly comparable: β's effective scale depends on κ_start and M/Δt² units. Phyxel starts κ at 1.0 and must reach ~mass·3600; the comment says warm-start drives it there "in 1-2 frames" ([solver_types.glsl:85](../shaders/solver_types.glsl)). |
| γ (warm-start decay) | **0.999** | [solver_types.glsl:89](../shaders/solver_types.glsl) | 0.99 (Table 2) | Phyxel forgets stiffness 10× slower. Paper: γ too high → κ can't decrease when no longer needed. |
| κ_start / κ_max | 1.0 / 1e10 | [solver_types.glsl:85-86](../shaders/solver_types.glsl) | κ_start "not crucial" (§3.1); κ,λ bounded to large finite values (§4) | Equivalent policy. |
| Iterations / tick | **8** | [GpuParticlePhysics.h:316](../engine/include/core/GpuParticlePhysics.h) | **3–4** for 110k–510k piles (Table 1) | See gap G1. |
| Substeps | up to 4 fixed ticks/frame | [GpuParticlePhysics.cpp:1141-1148](../engine/src/core/GpuParticlePhysics.cpp) | none (Δt = 1/60 straight) | Phyxel's ticks are catch-up, not accuracy substeps. |
| Max colors | 12 | [GpuParticlePhysics.h:315](../engine/include/core/GpuParticlePhysics.h) | not stated | See defect D1. |
| Particle cap | **10,000** | [GpuParticlePhysics.h:35](../engine/include/core/GpuParticlePhysics.h) | 510,000 bodies demoed | Static buffer sizing (all SSBOs sized from `MAX_PARTICLES` / `MAX_CONSTRAINTS` at init, [GpuParticlePhysics.h:237-354](../engine/include/core/GpuParticlePhysics.h)). Not a solver limit. |
| Constraint cap | 60,000 | [GpuParticlePhysics.h:314](../engine/include/core/GpuParticlePhysics.h) | n/a | = 6/particle at cap. See defect D2. |

### 3.6 Parallelization

CSR body→constraint adjacency is rebuilt on GPU each tick
([GpuParticlePhysics.cpp:935-973](../engine/src/core/GpuParticlePhysics.cpp)), then
**Jones-Plassmann body coloring, 16 fixed rounds**
([solver_body_color.comp:5-8](../shaders/solver_body_color.comp),
[GpuParticlePhysics.cpp:975-986](../engine/src/core/GpuParticlePhysics.cpp)). Same-color bodies
share no constraints, so the primal pass writes bodies **in place** — there is **no
double-buffered fallback** like the paper's (§4). Race-freedom therefore depends entirely on the
coloring being proper and complete, which leads to:

**Defect D1 — silently skipped bodies.** The color pass ignores neighbor colors ≥ 12 when
building the used-color mask, and `findLSB(~usedColors)` can assign a color ≥ 12
([solver_body_color.comp:57-60](../shaders/solver_body_color.comp)); a body not settled after 16
rounds stays `UNCOLORED` (0xFFFFFFFF). The primal loop only dispatches colors 0–11
([GpuParticlePhysics.cpp:1003](../engine/src/core/GpuParticlePhysics.cpp)) and the primal shader
early-outs on color mismatch ([solver_primal.comp:138](../shaders/solver_primal.comp)). **A body
with color ≥ 12, or uncolored, receives zero primal updates that tick** — it just follows its
gravity prediction until the hard-contact pass shoves it out of the floor. In a dense pile
(precisely Phyxel's use case) a body can easily contact more than 12 differently-colored
neighbors. No counter records how often this happens — it is a silent failure. The paper's
answer is double-buffering (conflict degrades to a Jacobi update, still solved).

**Defect D2 — silent constraint drop at the cap.** Both emitters bump `SS_CONSTRAINT_COUNT`
atomically and simply `return` if the slot is past `MAX_CONSTRAINTS`
([solver_narrowphase.comp:125-126](../shaders/solver_narrowphase.comp),
[solver_voxel.comp:243-244](../shaders/solver_voxel.comp)). At 10k particles the budget is 6
constraints/particle; the voxel pass alone can emit 4/body and each touching pair up to 4 more.
A 10k-particle pile can exceed the cap, and the excess contacts vanish without any log or
counter (the same failure class as the silent-voxel-cap ghost the 10-story-tower stress test
caught — see CLAUDE.md "Stress Test Phase").

### 3.7 Departures from the paper (deliberate or incidental)

| # | Phyxel behavior | Where | Paper | Assessment |
|---|---|---|---|---|
| P1 | **Scalar sphere inertia** — `I = 2/5·m·r²`, r = max half-extent, one float | [solver_sync_in.comp:39-60](../shaders/solver_sync_in.comp), [solver_types.glsl:20](../shaders/solver_types.glsl) | full 6×6 mass matrix with rotated inertia tensor (§2.2) | Cheap and fine for near-cubic debris; wrong for elongated boxes (scale is a vec3, [GpuParticlePhysics.h:47](../engine/include/core/GpuParticlePhysics.h)). |
| P2 | **Geometric stiffness term omitted** — primal LHS is `M/Δt² + Σ κ·JᵀJ` only | [solver_primal.comp:100-124](../shaders/solver_primal.comp) | diagonal-approximated G̃ added (§3.5) | Consistent with the paper's own contact treatment (2nd-order Taylor term dropped for contacts, §4). Would matter only if joints/attachments are ever added. |
| P3 | **No restitution in the live path** — `restitution` is loaded into `SolverBody` but only the dead `solver_jacobi.comp` reads it | [solver_sync_in.comp:62](../shaders/solver_sync_in.comp), [solver_jacobi.comp:71](../shaders/solver_jacobi.comp) | paper has no restitution either (pure position-level, Alg. 1) | `materials.json` restitution values are silently ignored for debris. Decide: implement or remove the plumbing. |
| P4 | **No static/dynamic μ split, no tangential contact-point pinning** — the persisted `stick` flag is saved/loaded but nothing consumes it in dual/primal | [solver_dual.comp:115](../shaders/solver_dual.comp), grep: no other reader | §3.3 pins in-cone contact points and switches μs/μd | Paper calls pinning optional but says it improves static friction at low iteration counts — relevant if iterations are reduced (R1). |
| P5 | **Hard-contact projection pass** — post-solve MTV push-out of any remaining static-voxel overlap + normal-velocity kill + 0.5× angular damp | [solver_hardcontact.comp:6-23,173-187](../shaders/solver_hardcontact.comp), [GpuParticlePhysics.cpp:1012-1024](../engine/src/core/GpuParticlePhysics.cpp) | not in the paper | A pragmatic safety net; also the thing that *masks* D1/D2. Keep, but instrument how often it fires. |
| P6 | **`GAMMA` doing double duty** — besides warm-start decay it multiplies per-tick velocity damping (`vel *= linearDamp * GAMMA`) | [solver_integrate.comp:110-111](../shaders/solver_integrate.comp) | γ is exclusively the warm-start scale (Eq. 19) | Numerically harmless (0.999) but semantically wrong constant reuse; a future γ retune would silently change global damping. |
| P7 | **No sleeping/islanding in the live path** — legacy sleep flags exist only in dead shaders; comment: "No sleep system" | [particle_collide.comp:493](../shaders/particle_collide.comp), [GpuParticlePhysics.h:230](../engine/include/core/GpuParticlePhysics.h) (legacy constant) | not covered by the paper | Every resting particle pays the full 8-iteration pipeline every tick until its lifetime expires. |
| P8 | **Grid broad phase (64³ hash) vs LBVH** | [GpuParticlePhysics.h:274-275](../engine/include/core/GpuParticlePhysics.h) | LBVH (§4) | Fine — uniform unit-scale debris is the ideal case for a uniform grid; LBVH wins for mixed-size bodies, which Phyxel doesn't have here. |
| P9 | **Character interaction is a kinematic shove**, re-anchoring the inertial target | [solver_integrate.comp:131-163](../shaders/solver_integrate.comp) | out of scope | Gameplay hack, acceptable; not a solver concern. |

---

## 4. Gap analysis — what matters for Phyxel's use case

Phyxel's use case: **bursts of ≤10k cube debris from destruction, piling and coming to rest on
static voxel terrain**, plus stacking stability. No joints, no articulated chains, no cloth.

| Gap | Matters? | Why |
|---|---|---|
| **G1. Iteration count (8 vs paper's 3-4)** | **Yes — the #1 opportunity.** | The paper's central claim is that warm-started AVBD stabilizes piles at 3-4 iterations (Table 1). Phyxel already has the warm-start machinery the paper credits for this. Each iteration costs 1 dual dispatch + 12 primal dispatches with barriers ([GpuParticlePhysics.cpp:992-1010](../engine/src/core/GpuParticlePhysics.cpp)); 8 iters × 12 colors × up to 4 ticks = up to **384 primal dispatches per frame** of mostly-empty work (every dispatch covers all `count` bodies and early-outs on color mismatch). Halving iterations roughly halves solve cost → direct headroom against the 10k cap. |
| **G2. Silent skips (D1) and silent constraint drop (D2)** | **Yes — correctness at exactly the stress extreme.** | Both defects activate only in dense piles, are invisible in N=small tests, and are masked by the hard-contact pass (bodies get shoved instead of solved → visible as bottom-layer jitter/popcorn under big piles). This repo's standing rules (silent-failure hate, invariants at scale) say these get counters + fixes before any iteration-count tuning is trusted. |
| G3. Missing static-friction pinning / μs-μd (P4) | Moderate, coupled to G1. | The paper says pinning "can give better static friction behavior when using a lower maximum iteration count" (§3.3). If R1 reduces iterations, tangential drift on slopes/stacks is the expected first regression; pinning is the paper's mitigation. On flat terrain at 8 iterations it evidently hasn't been needed. |
| G4. Double-buffered primal (paper §4) | Only as the fix vehicle for D1. | Full double-buffering costs a second body buffer + a copy pass; the paper uses it to tolerate *imperfect* coloring. Phyxel could instead make coloring lossless (clamp assignment into 0..11 by rotating, or solve colors ≥ 12 in extra Jacobi-style passes). Either way, the invariant is "every active body gets a primal update every tick." |
| G5. Scalar inertia (P1) | Low. | Debris is near-cubic; the error shows up as slightly wrong tumbling for stretched boxes. Not worth a 3×3 inertia tensor until visibly wrong. |
| G6. No sleeping (P7) | Moderate for steady-state cost, orthogonal to the paper. | 10k resting particles pay full price for up to 30 s lifetimes ([GpuParticlePhysics.h:63](../engine/include/core/GpuParticlePhysics.h)). AVBD gives clean rest (accelWeight + warm-started λ), so a velocity-threshold island sleep is feasible. The paper offers no guidance here. |
| G7. Warmstart hash never ages | Low but real on the churn axis. | Entries are never evicted ([GpuParticlePhysics.cpp:820-830](../engine/src/core/GpuParticlePhysics.cpp)); dead keys accumulate, lengthening probe chains (MAX_PROBE 128, then insert overflow silently loses the warm-start, [solver_warmstart_save.comp:23-33](../shaders/solver_warmstart_save.comp)). Long sessions with heavy spawn/despawn churn degrade warm-start hit rate — the exact quantity the solver's stability at low iterations depends on. Counters exist (`SS_WARMSTART_HITS/LOADED/NAN`, [solver_types.glsl:74-76](../shaders/solver_types.glsl)) but nothing reads them **(unclear whether any CPU readback consumes them — none found in GpuParticlePhysics.cpp)**. |
| G8. Articulated bodies / hard joints / high stiffness ratios | Not applicable today. | The paper's biggest wins (Figs. 5, 7, 9: chains, 50,000:1 mass ratios, joint constraints) exercise machinery Phyxel's debris path never uses. **Do not** justify solver work by these results. They *would* transfer if the AL constraint type were reused for, e.g., breakable structure joints (paper Fig. 13 wall-break is exactly the destruction aesthetic) — a future feature, not an audit finding. |
| G9. Paper's absolute perf numbers | Don't transfer as-is. | 110k @ 3.5 ms is a dedicated DX11 demo with LBVH and no voxel-grid narrowphase. Phyxel's per-body voxel SAT scan ([solver_voxel.comp:157-188](../shaders/solver_voxel.comp)) and 4-tick catch-up are different cost centers. Use the paper for *iteration counts and stability*, not milliseconds. |

---

## 5. Recommendations (ranked)

Ordering follows expected value ÷ cost. Per the repo's standing rules, each carries the stress
test that proves it (scaling axis pushed to the extreme, invariant asserted at every step,
red-before-green where a defect is claimed).

### R1. Instrument the silent failures, then fix them (D1 + D2) — do this first
- **What:** add GPU counters (the `solverState` header has spare slots,
  [solver_types.glsl:69-77](../shaders/solver_types.glsl)) for (a) bodies with
  `bodyColor ≥ MAX_COLORS || UNCOLORED` at solve time, (b) constraints dropped past
  `MAX_CONSTRAINTS`, (c) hard-contact projections fired per tick; read them back with the
  existing readback path ([GpuParticlePhysics.cpp:1052+](../engine/src/core/GpuParticlePhysics.cpp)
  region / position-log machinery). Then fix: solve leftover colors in extra passes (or
  double-buffer per the paper §4) and either raise/scale `MAX_CONSTRAINTS` or log-once on drop.
- **Benefit:** correctness at the dense-pile extreme; removes the two failure modes that would
  otherwise corrupt every measurement in R2.
- **Cost:** low (counters ~hours; color fix small-medium).
- **Stress test (red first):** spawn 10,000 particles into a tight shaft so the pile is maximally
  dense. *Invariant at every tick:* skipped-body counter == 0, dropped-constraint counter == 0,
  no particle center below floor level. Expectation: **the counters are nonzero today** — that is
  the red state proving D1/D2 are real, before any fix is claimed.

### R2. Iteration-count economics: 8 → 4 (paper Table 1)
- **What:** make `SOLVE_ITERATIONS` ([GpuParticlePhysics.h:316](../engine/include/core/GpuParticlePhysics.h))
  runtime-tunable; measure stability and GPU time at 8/6/4/3/2 iterations using the GpuProfiler
  scopes already in place ([GpuParticlePhysics.cpp:796-797](../engine/src/core/GpuParticlePhysics.cpp)).
- **Benefit:** paper stabilizes 110k-body piles at 4 iterations with the same warm-start scheme
  Phyxel has; success ≈ half the solve cost → headroom to raise the cap or take ticks off the
  frame.
- **Cost:** trivial to test; the risk is tuning time if 4 regresses (then try 6, or add R4).
- **Stress test:** two axes. (a) *Stack height:* stacks of N = 1..20 boxes on flat voxel floor;
  invariant per height and per iteration count: every box's per-tick displacement < ε for
  T = 30 s after settle (use `startPositionLog` CSV,
  [GpuParticlePhysics.h:209](../engine/include/core/GpuParticlePhysics.h)). (b) *Pile count:*
  10k-particle drop; invariants at every tick after settle: total kinetic energy monotonically
  non-increasing (no popcorn), zero hard-contact projections (from R1's counter), no NaN
  (SS_WARMSTART_NAN == 0).

### R3. Raise `MAX_PARTICLES` once R1+R2 land
- **What:** the 10k cap is static buffer sizing, not algorithmic
  ([GpuParticlePhysics.h:35](../engine/include/core/GpuParticlePhysics.h)); all dependent buffers
  derive from it. Scale to 20k → 50k with `MAX_CONSTRAINTS` scaled proportionally (keep ≥ 6/body;
  R1's drop counter tells you the real demand).
- **Benefit:** directly serves the destruction ambition; memory is a non-issue on the RTX 4090
  target (SolverBody 208 B + GpuParticle 96 B + faces 6×64 B ≈ 0.7 KB/particle → ~35 MB at 50k).
- **Cost:** low-medium (buffer plumbing, `HASH_CAP` must stay ≥ 2× constraints and pow2,
  [GpuParticlePhysics.h:319-322](../engine/include/core/GpuParticlePhysics.h)); the real cost is
  whatever the perf measurement says.
- **Stress test:** count axis 10k/20k/50k pile drops; invariants at every scale: per-pass GPU
  times scale ~linearly (profiler), R1 counters stay zero, settle behavior unchanged, and 60 FPS
  budget documented honestly (this may *cap out* below 50k — that is a finding, not a failure).

### R4. Static-friction pinning + μs/μd split (paper §3.3) — only if R2 shows tangential drift
- **What:** consume the already-persisted `stick` flag: when set, re-anchor the contact point
  tangentially in narrowphase/voxel emit (the warm-start entry already stores `rA/rB` for this,
  [solver_types.glsl:58-59](../shaders/solver_types.glsl)); optionally split μ into
  static/dynamic.
- **Benefit:** the paper's stated mitigation for friction quality at low iteration counts.
- **Cost:** medium (touches both emitters and the warm-start contract).
- **Stress test:** ramp axis — boxes resting on voxel slopes of increasing angle (build
  staircase-approximated inclines), invariant: below tan⁻¹(μ) no box drifts more than ε over
  60 s at the R2-chosen iteration count; above it, all boxes slide. Plus the paper's Fig. 11
  scenario: rows of boxes with different μ sliding to a stop at distances monotonic in μ.
- **Grounding note:** any μs/μd values must come from `materials.json` / cited references, not
  invention (grounding rule).

### R5. Sleep/islanding for settled debris (not from the paper)
- **What:** velocity+contact-stability threshold → set a sleeping flag that skips
  integrate/narrowphase/solve for the body (wake on neighbor motion or occupancy change under
  its AABB). The legacy path's history warns about oscillating in/out of sleep in stacks
  ([particle_collide.comp:493-497](../shaders/particle_collide.comp)) — require M consecutive
  quiet ticks before sleeping.
- **Benefit:** steady-state cost of a settled 10k pile drops to ~0; complements R3 (bigger
  piles are only affordable if resting ones are cheap).
- **Cost:** medium-high (wake propagation across the grid is the hard part; getting it wrong
  reintroduces popcorn).
- **Stress test:** churn + repetition axis — drop 10k, wait for full sleep (invariant: solver
  dispatch body count → ~0, positions frozen), then break a voxel under the pile; invariant:
  affected region wakes within 2 ticks and re-settles, ×100 cycles without residual jitter or
  stuck-floating particles.

### R6. Hygiene (cheap, do opportunistically)
- Split `GAMMA` into `WARMSTART_GAMMA` and an explicitly named damping constant
  ([solver_integrate.comp:110-111](../shaders/solver_integrate.comp) vs
  [solver_narrowphase.comp:150](../shaders/solver_narrowphase.comp)) — P6.
- Delete or move to `deprecated/` the orphaned `solver_apply.comp`, `solver_jacobi.comp`,
  `solver_graph_color.comp`, and decide the fate of the dead legacy `particle_integrate/collide`
  pipeline objects ([GpuParticlePhysics.h:292-304](../engine/include/core/GpuParticlePhysics.h)).
- Decide restitution (P3): either implement a velocity-level bounce for debris (paper offers no
  recipe — this would be Phyxel-original) or stop loading it into `SolverBody` and document that
  debris restitution is intentionally dead.
- Add a periodic warmstart-table epoch/rebuild for long-session churn (G7) and surface the
  existing `SS_WARMSTART_*` counters in the perf overlay. **Stress test:** spawn/despawn 10k
  particles ×1000 cycles; invariant at every cycle: warm-start hit rate for persisting contacts
  stays above its cycle-1 baseline − ε, and insert overflow count stays 0.

### Explicitly NOT recommended now
- **Formulation changes** (full inertia tensors, geometric stiffness, stiffness rescaling
  Eq. 14): the paper itself uses the simple friction-Hessian variant for nearly all results
  (§5), and Phyxel's contact-only workload doesn't exercise the cases where these matter.
- **Joints/articulated constraints:** real value (breakable structural joints for destruction,
  paper Fig. 13) but that is a feature proposal, not a solver-parity item.
- **Porting AVBD to the CPU `VoxelDynamicsWorld`:** per
  [`EngineAdvancesResearch.md`](EngineAdvancesResearch.md) §3, only "if furniture stacking ever
  shows jitter" — no evidence of that today.

---

## 6. Honest transfer caveats

- The paper's stacks/chains are **rigid bodies with proper 6×6 mass matrices and exact
  manifolds**; Phyxel's bodies use scalar sphere inertia and voxel-face contact points. The
  *stability* claims (piles at rest, 4 iterations) should transfer because the mechanism
  (warm-started λ carrying contact forces) is identical and Phyxel implements it; the *quality*
  claims (card towers, precise friction, Fig. 6/11) may not, given P1/P4.
- Paper performance numbers come from a dedicated DX11 demo with LBVH broad phase on the same
  GPU class as the dev target (RTX 4090) but a completely different collision workload; treat
  Table 1 as an iteration-count reference only.
- Phyxel's constants (α = 0.99, β = 1e5, γ = 0.999) deviate from the paper's (0.95, 10, 0.99)
  with a recorded local justification only for α ([solver_types.glsl:83](../shaders/solver_types.glsl)).
  Whether the β/γ deviations were tuned or inherited from "Shallot" is **unclear** — retuning
  them toward paper values is only worth doing inside R2's measured stress harness, never blind.
- The "Shallot" reference implementation the shaders repeatedly cite is not in the repo and
  could not be identified from public sources during this audit; claims about "matching Shallot"
  in comments are therefore unverifiable here. Everything in this document is verified against
  the paper and Phyxel's own code only.

## 7. Sources

- Giles, Diaz, Yuksel, *Augmented Vertex Block Descent*, ACM TOG 44(4) Art. 90, SIGGRAPH 2025 —
  [PDF](https://graphics.cs.utah.edu/research/projects/avbd/Augmented_VBD-SIGGRAPH25.pdf) ·
  [DOI 10.1145/3731195](https://doi.org/10.1145/3731195) ·
  [project page](https://graphics.cs.utah.edu/research/projects/avbd/)
- *Crazy Fast Physics! AVBD in Action!* (Real-Time Live! abstract) —
  [DOI 10.1145/3721243.3735982](https://dl.acm.org/doi/10.1145/3721243.3735982)
- Chen, Liu, Yang, Yuksel, *Vertex Block Descent*, SIGGRAPH 2024 —
  [arXiv 2403.06321](https://arxiv.org/html/2403.06321v1)
- WebPhysics WebGPU AVBD solver (independent implementation notes: LBVH + greedy coloring +
  in-place colored solves) —
  [webgpu.com showcase](https://www.webgpu.com/showcase/webphysics-webgpu-avbd-solver/)
- Macklin, Müller, Chentanez, *XPBD*, MIG 2016 — cited via the AVBD paper §2.1 for the dual/
  substepping baseline characterization.
- Phyxel sources audited: [`engine/include/core/GpuParticlePhysics.h`](../engine/include/core/GpuParticlePhysics.h),
  [`engine/src/core/GpuParticlePhysics.cpp`](../engine/src/core/GpuParticlePhysics.cpp),
  [`shaders/solver_types.glsl`](../shaders/solver_types.glsl) and all `shaders/solver_*.comp`,
  legacy `shaders/particle_*.comp`, [`docs/EngineAdvancesResearch.md`](EngineAdvancesResearch.md).

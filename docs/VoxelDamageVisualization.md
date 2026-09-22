# Voxel Damage Visualization — progressive cracks (P4)

**Status:** planned, not built. Design-check gate run **three times** (2026-09-22), 11 items found
and resolved — see §11 for the full ledger:
- **Pass 1** → NEEDS WORK, 5 items: API readback, toughness normalization, world-position seeding,
  the geometric-spall position, stage quantization/merge cost (§3.1-3.5).
- **Pass 2** → NEEDS WORK, 3 items: sub-voxel coverage (§3.6), graze re-mesh (§3.7), plus CPU-mirror
  drift, float non-associativity and damage-loss-on-subdivision (§6.2 / §9 / §3.6).
- **Pass 3** → NEEDS WORK, 3 items, **all in the validation plan — the design held unchanged**: test
  placement (§6.0), a rig buried under terrain (§6.3), an overstated pipeline scope note (§4).

This document resolves all of them and is the build plan.
**Parent:** [`DestructionSystemV2.md`](DestructionSystemV2.md) §5(F) "Damage visualization (P4)" and
§10 Phase 5. That stub (8 lines) is superseded by this document; the roadmap entry stays.
**Gate:** [`FeatureDesignKeys.md`](FeatureDesignKeys.md). Section 9 below records the gate answers.

---

## 1. What this is

Voxels that have taken sub-threshold damage — hit, but not hard enough to break — must **look**
damaged. Today they accumulate damage correctly and render as a uniformly darker, rougher face,
which reads as *dirty*, not *cracked*.

The goal in one line: **a damaged voxel shows a fracture network that tells you how close it is to
breaking, and which way it will break** — pop off whole, shatter to subcubes, or powder to
microcubes.

That last clause is the part worth building. `DamageSystem::MatResponse` already carries `s1` (overkill
ratio → shatter to subcubes) and `s2` (→ microcubes), per material, from `materials.json`. The crack
can be keyed to them, so the surface *teaches the physics* before the blow lands. No new data needed.

> ### ⚠️ V1 SCOPE BOUNDARY — full-cube voxels only
>
> **V1 cracks render on full-cube voxels: terrain, and cube fills. They do NOT render on any
> generated building.** Every wall the structure generator produces is sub-cube resolution
> (`StructureRealizer.cpp:163-165` — exterior wall 0.333 = one subcube, interior 0.222 = two
> microcubes, foundation 0.667 = two subcubes, stamped via `fillMicroBox`), and sub-voxel faces carry
> no damage bits at all (`ChunkRenderManager.cpp:1062-1064`, `:1222-1224`).
>
> This is a **stated scope boundary, not a defect to be discovered later.** Closing it requires
> sub-voxel damage *state*, which does not exist — accumulation is cube-only by design
> (`DamageSystem.cpp:182`). See **§3.6** for the decision and **§7 V2** for the phase that fixes it.
> Do not demo this feature on a building until V2 ships.

---

## 2. Honest baseline — what is already shipped

The cube pipeline is ~70% built. This is not a greenfield feature, and it is also not as complete as
the shipped code's surface suggests — rows 7-9 are the gaps the gate re-run surfaced.

| Layer | State | Evidence |
|---|---|---|
| Per-voxel damage accumulation (**cube only**) | **Live** | `Cube::accumulatedDamage`, `addDamage()` / `getAccumulatedDamage()` / `resetDamage()` — `engine/include/core/Cube.h:98-102, 180-181`; cube-only by design per `DamageSystem.cpp:182` |
| Sub-threshold hits accumulate | **Live** | `engine/src/core/DamageSystem.cpp:183-189`; the comment already reads `// weakened but intact (cracks; visual feedback = P4)` |
| Damage → mesher | **Live** | read at `engine/src/graphics/ChunkRenderManager.cpp:351`, quantized 0-15 at `:370-374` |
| Damage → instance word, **cube faces** | **Live** | packed into `reserved` bits 11-14 at `ChunkRenderManager.cpp:741-744` (merged) and `:867` (per-face) |
| Merge key respects damage | **Live, correct** | `ChunkRenderManager.cpp:741-744` folds `dmgBits` into `faceKey` so damaged voxels never merge with pristine |
| Shader consumer | **Live, crude** | `shaders/voxel.frag:283-286` — `rough = mix(rough,1.0,dmg)` and `textureColor.rgb *= mix(1.0,0.55,dmg)` |
| Damage → instance word, **subcube / microcube faces** | **MISSING — bits always 0** | `ChunkRenderManager.cpp:1062-1064` and `:1222-1224` build `reserved` from emissive/transparent/alpha/mirror only. §3.6 |
| Re-mesh after a **graze** | **MISSING — never fires** | flush gated on `res.voxelsBroken > 0` (`DamageSystem.cpp:304-306`); graze branch `continue`s at `:186-189` before any `markChunkDirty`. §3.7 |
| **Crack pattern** | **Does not exist** | — |
| **API readback of damage** | **Does not exist** | `/api/world/voxel` returns `{position, exists}` only — `editor/src/Application.cpp:710-720` |
| **Damage persistence** | **Does not exist** | no `damage` in `ChunkStreamingManager.cpp` / `WorldDatabase.cpp` (grep empty) |

---

## 3. The resolutions

§3.1-3.5 answer the first gate run. §3.6-3.7 answer the re-run; **§3.7 is P0-critical** — without it the
feature is intermittently invisible even on the surfaces V1 does cover.

### 3.1 — API readback (was: blocking)

**Problem.** `apply_damage` returns `{success, broken, grazed, debris, coherent_bodies}`
(`editor/src/Application.cpp:14473-14476`) — counts of *events*, never resulting *state*. There is no
way to ask "what is the accumulated damage at (x,y,z)?". Per the gate's rule — *echo the resulting
state back so a caller can assert it took effect* — this is a straight miss, and it means **the L2
red test cannot be written at all**. Everything else is blocked on it.

**Decision: extend the existing voxel query, following the baked-light precedent.**

`setBakedLightQueryHandler` (`Application.cpp:723-745`) is the model: it exists because the light
field was "the engine's largest unobservable … the only way to look at it was to photograph a
surface and guess." Damage is in exactly that position today. Add to `setVoxelQueryHandler`
(`Application.cpp:710-720`):

```jsonc
{
  "position": {"x":8,"y":9,"z":8},
  "exists": true,
  "material": "Stone",              // added: needed to interpret the values below
  "damage_tracked": true,           // false for a SUBDIVIDED cell — see below
  "damage_energy": 33.0,            // accumulated energy, same units as apply_damage `energy`
  "toughness": 110.0,               // responseFor(material).toughness — the denominator, echoed
  "damage01": 0.30,                 // damage_energy / toughness, clamped [0,1]; 1.0 = at break
  "damage_stage": 1                 // quantized 0..3, EXACTLY what the shader receives
}
```

Rules this follows:
- **Units named at the field.** `damage_energy` is in `apply_damage` energy units; `damage01` is a
  fraction of break toughness; `damage_stage` is an ordinal.
- **The denominator is echoed** (`toughness`), so a caller can reproduce `damage01` and catch a
  normalization change without reading engine source.
- **`damage_stage` is the shader's actual input**, not a re-derivation. A test that asserts on
  `damage01` alone could pass while the shader sees something else; this closes that gap.
- An air voxel returns `exists:false` and omits the damage fields rather than reporting `0`, so
  "pristine" and "not there" are never confused.
- **`damage_tracked:false` for a subdivided cell**, with the damage fields *omitted*. A subcube wall
  is not pristine — its damage is **not tracked at that resolution** (§3.6). Reporting `damage01: 0`
  would be a lie, and it is exactly the lie that would let someone demo cracks on a building, see
  nothing, and conclude the shader is broken.

**Cost note (checked, not assumed).** `getCubeAt` → `getCubeAtFast` → `materializeAt`
(`ChunkVoxelQuerySystem.cpp:57-60`, `ChunkVoxelManager.cpp:420-422`), so **every existing call to
`/api/world/voxel` already materializes an overlay `Cube`**. Adding damage fields costs nothing new.
But it means a *bulk* damage scan would materialize a whole region — so this stays a **per-voxel
query**, and any future region scan must take a non-materializing path. Written here so it is not
rediscovered.

---

### 3.2 — Normalize by material toughness (was: shipped defect)

**Problem, quantified.** `ChunkRenderManager.cpp:370-374`:

```cpp
constexpr float kDamageRef = 30.0f;   // "prototype constant; a proper version would
float f = damage / kDamageRef;        //  normalise by the material's break toughness"
```

The comment concedes the flaw; here is its size. A voxel displays maximum damage at 30 energy
regardless of material, but breaks at its own `toughness` (`resources/materials.json` break blocks):

| Material | Toughness | Display saturates at | …which is this % of the way to breaking |
|---|---|---|---|
| Steel | 220 | 30 | **13.6%** |
| Metal | 200 | 30 | 15.0% |
| Stone | 110 | 30 | 27.3% |
| Wood / LogHeartwood | 70 | 30 | 42.9% |
| Dirt | 45 | 30 | 66.7% |
| Glass | 35 | 30 | **85.7%** |

The same visual stage means anywhere from 14% to 86% of the way to failure — a **6.3× spread**. On
Stone, the display maxes out after the first quarter of the damage and then says nothing for the
remaining 73%. The feature's entire purpose is to communicate proximity to breaking, and on the
most common structural material it is silent for three quarters of the range.

**Decision:** replace `kDamageRef` with the material's own toughness:

```cpp
const float toughness = damageSystem.responseFor(material).toughness;   // > 0 by construction
float f = clamp(damage / toughness, 0.0f, 1.0f);
```

`responseFor` is already public and const (`DamageSystem.h`) precisely so it can be unit-tested as
data-driven. **Note the coverage limit honestly:** only **7 of 108** materials carry a `break` block;
the other 101 fall back to `toughness = bondStrength * 120.0f` (`DamageSystem.cpp:42-46`). That
fallback is derived, not grounded — its grounding is **pre-existing open question #5** in
`DestructionSystemV2.md:625-627` ("sign off the real-world references for toughness ratios") and is
**not resolved here**. This change does not make that worse; it makes it *visible*, which is an
argument for closing it, not for keeping a global constant.

**Keep the clamp, and write down what it prevents.** The existing clamp at `:372-373` is correctly
placed — I verified there is **no** bit-15 overflow today. But the reason is unrecorded, and bit 15
is the `varied` texture-rotation flag (`ChunkRenderManager.cpp:394`, read at `voxel.frag:259`). An
unclamped stage ≥ 16 would silently switch on per-voxel texture rotation. Add that sentence at the
clamp site, per the gate's rule.

---

### 3.3 — Pin the crack to world position, never to face UV (was: latent chunk violation)

**The trap.** `static_voxel.vert:290` sets `uv = baseUV * vec2(float(sizeU), float(sizeV))` — UV space
is tied to the greedy-merged rectangle. Merge runs are computed inside a 32³ loop
(`ChunkRenderManager.cpp:313`, `constexpr int N = 32`), so **they terminate at chunk borders**. A
crack seeded from `uv` would scale and repeat differently on either side of a chunk seam: chunk
identity made visible, which is the grass-density failure the gate doc is built around.

**Decision: seed exclusively from the reconstructed absolute world position.** The handle already
exists in the file — `voxel.frag:260`:

```glsl
vec3 worldPosAbs = vChunkBaseAbs + (inWorldPos - vChunkBaseRel);
```

It is exact (integer chunk origin) and camera-independent, and it exists *because* the `varied` hash
needed a world-stable seed that does not re-roll when the camera moves (`voxel.frag:255-263`). The
crack is the same class of problem, so it reuses the same machinery, including
`worldFaceUV(worldPos, faceNormal)` (`voxel.frag:161`) to project onto the face plane.

**Hard rule for this feature:** the crack function must not read `sizeU`, `sizeV`, `texCoord`, or any
chunk-derived quantity. Its only inputs are `(worldPosAbs, faceNormal, damageStage, crackStyle)`.

**Deliberate consequence.** Cracks will not trace the *shape of the damaged region* across voxels —
doing so would require reading neighbour damage, which buys the stale-neighbour failure mode the gate
doc warns against. What the world-space field *does* give is a fracture network that is continuous
across voxel and chunk boundaries, so a damaged wall reads as one cracked surface rather than N
stamped decals. That is the visual win, and it comes free from doing it correctly.

---

### 3.4 — Position on geometric spalling (was: unstated)

**The discomfort.** This engine already does geometric fracture at microcube resolution, in the same
system: the axe kerf subdivides bitten cubes/subcubes, removes the micros inside the slot, and
re-materials cut faces to `LogHeartwood` (`DamageSystem.h`, kerf section). The primitives are public —
`ChunkVoxelManager::subdivideAt` (`:110`) and `subdivideSubcubeAt` (`:119`). A painted crack will sit
in the same frame as a carved kerf notch and read as the cheaper thing.

**Decision: shader-first, geometric second, and say so in the doc rather than shipping V1 as the
destination.**

- **V1 (this plan): painted fracture.** Surface-state treatment, same class as the shipped `vState`
  charred/wet modifiers (`voxel.frag:311-325`). Legitimate and sufficient on its own.
- **V1.5 (specified here, gated on V1 landing): spall at the top stage only.** At `damage_stage == 3`,
  subdivide the voxel and remove a bounded number of surface microcubes so the *silhouette* breaks,
  not just the shading. A microcube is **1/9 world unit ≈ 11.1 cm** (1 cube = 1 m: the fine-voxel item
  grid is 81 cells at ≈1.23 cm, `CLAUDE.md` / `docs/FineVoxelItems.md` — 81 × 1.23 cm ≈ 99.6 cm), which
  is a plausible chip off a stone block.

**Why not V1.5 first — three concrete reasons, not aesthetics:**
1. **Occupancy.** Removing micros changes occupancy, which feeds `SpawnGate` resolution-complete
   solidity, walkability validators, and the structure-build vegetation gate. Shader-only touches no
   occupancy and therefore cannot regress any of them.
2. **Order-independence.** The painted crack is a pure function (§3.3) and is order-independent.
   Spalling is *not*: removing a micro changes neighbours' exposed faces, so per-chunk and
   whole-region evaluation could diverge. That needs its own equality test.
3. **Cost.** Spalling converts one merged cube face into many microcube faces — a far larger
   face-count hit than merge-key fragmentation (§3.5), and it must be budgeted separately.

**V1.5 depends on §3.6.** Spalling a cube *replaces it with sub-voxels*, which under V1 rules stop
displaying damage entirely — the voxel would crack, spall once, and then go visually pristine. So
**V1.5 cannot ship before sub-voxel damage (V2)**, and the roadmap in §7 orders it accordingly. This
dependency was missed in the first draft of this plan.

**Binding the two together:** the painted crack's fracture cells are sized to the **microcube grid**,
so when a stage-3 voxel later spalls, the chips fall where the painted cracks already were. V1 and
V1.5 agree by construction instead of fighting.

---

### 3.5 — Quantize damage stages, and measure the merge cost (was: unmeasured)

**The cost mechanism.** Damage is in the merge key by design, so a damaged region locally becomes the
**un-merged** case. The engine's own M4 measurement puts that at merge ON **269,618 faces / 135-145
FPS** vs merge OFF **1,764,780 faces / 27.5 FPS** — 6.5× faces, ~5× FPS
(`docs/ContinuousLodPlan.md` §7b). A blast producing a radial damage gradient with 16 distinct levels
shatters merge runs into ~16 concentric single-voxel-wide bands.

**Decision: 3 visible damage stages + pristine (4 states, 2 bits used).**

- Fewer distinct levels ⇒ wider bands ⇒ longer surviving merge runs ⇒ fewer faces. Monotonic, so the
  coarsest quantization that still reads is the right one.
- **Keep the field 4 bits wide.** Bits 11-14 stay as they are; stages 4-15 become reserved. No
  instance-format change, no ABI churn, no shader-side repack — and headroom if the measurement says
  go finer.
- Sixteen perceptually distinct crack stages on a 1 m face is not a real thing; three (hairline /
  open / failing) maps to how the player actually uses the information.
- **Second dividend, added after the re-run:** coarse stages also cut *re-mesh* frequency, because
  §3.7 only dirties a chunk when a voxel crosses a stage boundary. Three stages means at most three
  re-meshes per voxel over its whole life instead of sixteen.

**This default is a hypothesis, and §6.4 is the experiment that ratifies or revises it.** Ship
whichever of {3, 7, 15} stages is the coarsest that still reads at gameplay distance, chosen from the
measured face-count / FPS table — not from this paragraph.

**Where the measurement must happen.** Not in the test rig, and not in the Lighting Lab. A cost
measured in a lab rig here has been wrong by ~4× before (`docs/AgentContext.md`; the +10.5 ms that was
really +39-52 ms in Ravenmere town). Merge cost is measured in a **real settlement scene** via
`GET /api/debug/gpu_scopes` (scopes nest — never sum them).

---

### 3.6 — Sub-voxel coverage: the V1 scope boundary (gate re-run, Finding A)

**Problem.** The subcube and microcube instance paths build `reserved` from emissive / transparent /
alpha / mirror only. **Damage bits 11-14 are always zero on every sub-voxel face**
(`ChunkRenderManager.cpp:1062-1064` and `:1222-1224`):

```cpp
faceInstance.reserved = static_cast<uint16_t>(
    (isEmissive ? 1u : 0u) | (isTransparent ? 2u : 0u) | (quantAlpha << 2u) | (isMirror ? (1u << 10) : 0u));
```

Only the two cube paths (`:741-744`, `:867`) carry `dmgBits`. And every generated wall is sub-cube
resolution — `StructureRealizer.cpp:163-165`:

```cpp
const int extT   = thicknessMicro(style.thicknessOf("exterior_wall",   0.333));  // 1 subcube
const int intT   = thicknessMicro(style.thicknessOf("interior_wall",   0.222));  // 2 microcubes
const int foundT = thicknessMicro(style.thicknessOf("foundation_wall", 0.667));  // 2 subcubes
```

stamped via `fillMicroBox`. **So no generated building can display a crack** — not a wall, not a
foundation. For a feature whose motivating case is damaging structures, that is most of the value.

**This is not a wiring gap.** Damage accumulation is cube-only *by design*
(`DamageSystem.cpp:182`: *"Accumulation is cube-only; sub-voxel cells break in one pass"*). `Subcube`
and `Microcube` have no damage field at all, so there is no state to plumb into those instance
writes. Closing it means designing sub-voxel damage storage — a feature, not a patch.

**Decision: V1 ships full-cube only, declared as a scope boundary (§1). Sub-voxel damage is its own
phase (§7, V2).**

Rationale:
- It keeps the crack model provable end-to-end on a surface that *already* carries the state, so
  red-before-green stays tight and the shader work is validated before the storage design begins.
- It avoids designing sub-voxel damage storage speculatively while the crack model is unvalidated.
  **Memory is the open question, and it is not small:** a cube's damage is one float on a materialized
  overlay `Cube`, but a subcube grid is 27× the cell count and a microcube grid 729×. A naive
  float-per-cell is not obviously affordable and needs its own design plus measurement — precisely the
  kind of thing that should not be rushed to unblock a shader.
- The alternative (sub-voxel damage as a prerequisite P0.5) front-loads the biggest design risk and
  delays any visual proof.

**What V1 must therefore do, so the boundary is honest rather than hidden:**
- Declare it in §1 (done) and report it through the API via `damage_tracked:false` (§3.1).
- The R3 rig uses a **full-cube Stone wall**, never a generated building (§6.3).
- No demo, screenshot, or claim about this feature may use a generated structure until V2.

**V2 must answer two questions this plan deliberately leaves open:**
1. **Where does sub-voxel damage live?** Per-cell field (exact, 27×/729× memory), or a per-parent-cube
   aggregate that all child cells share (cheap, coarse — the whole subdivided cell cracks as one)?
   The aggregate is likely right for walls and should be costed first.
2. **What happens to damage on subdivision?** Today `accumulatedDamage` lives on the `Cube` being
   replaced, so **V1 behaviour is that damage is silently lost** when a damaged cube is subdivided by
   the kerf, by Middle Click, or by V1.5's own spall. Acceptable in V1 only because every V1
   subdivision path is itself destructive (the kerf is carving the voxel apart anyway). V2 must choose
   deliberately: inherit the parent's `damage01` into each child cell, or reset to pristine.

---

### 3.7 — Re-mesh the graze (gate re-run, Finding B — P0-critical)

**Problem, at line level.** The batched re-mesh flush is gated on *breaks*
(`DamageSystem.cpp:304-306`):

```cpp
if (res.voxelsBroken > 0) {
    m_cm->updateDirtyChunks();
}
```

and the graze branch `continue`s at `:186-189`, before Phase B where the only `markChunkDirty` /
`removeCubeFast` calls live. **A blast that grazes without breaking anything records the damage and
never rebuilds the mesh** — the crack does not appear until something else happens to dirty that
chunk.

**Why this would have survived testing.** Any blast that breaks even one voxel calls
`updateDirtyChunks()` and re-meshes every touched chunk as a side effect, so grazed voxels in those
chunks *do* update. The bug is invisible in the common case and shows up exactly in the weak-hit case
this feature exists to visualize. Neither R1 (reads the API, not the mesh) nor a mixed-blast visual
test would catch it. It needs its own red — **R5, §6.5**.

**Decision:**
- In the graze branch, compute the damage stage **before and after** `addDamage`. If the stage
  changed, `markChunkDirty` on that voxel's chunk.
- Un-gate the flush: `if (res.voxelsBroken > 0 || res.voxelsStageChanged > 0) m_cm->updateDirtyChunks();`
- Add `voxelsStageChanged` to `DamageResult` and echo it from `apply_damage`, so a caller can assert a
  graze actually moved something — the same "echo the resulting state" rule as §3.1. This also makes
  R5 cheap to write.

**Why stage-gated rather than always dirty.** A graze that does not cross a stage boundary changes
zero pixels, so re-meshing for it is pure cost — and a single blast can graze thousands of voxels
(the §8.4 stress axis). Dirtying only on a stage transition means the re-mesh count is bounded by
*visible* changes, which is the same principle as §3.5: coarse stages, fewer rebuilds. The two
decisions reinforce each other.

---

## 4. The crack model

Inputs: `worldPosAbs`, `faceNormal`, `damageStage` (0-3), `crackStyle` (per material).

1. **Domain.** `vec2 p = worldFaceUV(worldPosAbs, faceNormal)` — world-plane coordinates, continuous
   across voxel and chunk boundaries by construction (§3.3).
2. **Fracture field.** Cellular (Voronoi) edge distance `F2 - F1`, cell size on the **microcube grid**
   (~1/9 unit ≈ 11 cm) so painted cracks and future spall chips share a lattice (§3.4).
3. **Stage widens the field, it does not swap it.** Stage 1 opens hairlines on the primary cell edges;
   stage 2 widens them and admits a second octave; stage 3 opens the full network. Because the field
   is continuous in stage, a voxel advancing 1→2→3 shows *the same cracks growing* rather than three
   unrelated patterns — the thing stamped decals cannot do.
4. **Style keys off existing per-material data.** `brittleS1` / `brittleS2` already encode shatter
   behaviour: Glass (s1 = 1.3) gets a dense fine network; Steel (s1 = 4.5) gets sparse wide fissures;
   Stone (1.8) sits between. This is the telegraphing in §1 — surface predicts shatter tier, driven by
   the same numbers the physics uses, so they cannot drift apart.
5. **Darken the crack, not the face.** The current code darkens the *entire* face
   (`voxel.frag:285`, `textureColor.rgb *= mix(1.0, 0.55, dmg)`) — which is exactly why it reads as
   dirt. Confine the darkening to crack pixels (cracks are self-shadowing) and keep a much smaller
   whole-face term for general wear. Roughness follows the same split.
6. **Cost gate.** `if (damageStage == 0u) { /* skip entirely */ }`. Pristine is ~100% of voxels in any
   real scene and the branch is spatially coherent, so the cost lands only where there is damage.
   This bounds **cost**, never appearance — the `bladesForDistance` pattern, not a quality tier. The
   detail is unconditional wherever damage exists.
7. **Single source of truth for the field.** The crack function lives in its own small include,
   `shaders/crack.glsl`, because R2's CPU mirror must be pinned to it (§6.2). Nothing else may
   re-implement it.

**Scope note — cracks are STATIC CHUNK FACES ONLY, structurally.** `voxel.frag` is shared by all
three voxel vertex pipelines, but only `static_voxel.vert` can carry a crack. The other two exclude
it by **two independent mechanisms**, each sufficient on its own:

1. **No flags.** `kinematic_voxel.vert:126` and `dynamic_voxel.vert:267-268` both hardcode
   `flags = 0u` (the latter comments it "Dummy flags"), so damage bits 11-14 are unreachable. The
   instance structs have no room either — `KinematicFaceData` is 48 bytes with no flags field
   (`KinematicVoxelManager.h:23-35`), and in `DynamicSubcubeInstanceData` `reserved1` is alignment
   padding while `reserved2` is baked light (`Types.h:208-217`).
2. **No world-position seed.** Both also set `vChunkBaseAbs = vChunkBaseRel = vec3(0.0)`, commented
   "varied disabled on this path (flags=0)". So even with flags plumbed, §3.3's `worldPosAbs` would
   reconstruct from a zero origin and the crack field would be seeded wrong.

**Consequence:** GPU debris and kinematic furniture (doors, moved furniture, coherent fragments)
**cannot** show cracks, in V1 or V2. This costs little — debris is already-broken matter, and
furniture cracking is a nice-to-have — but it is a structural limit, not a decision we could reverse
by setting a flag. Extending to those pipelines means widening two instance formats and plumbing a
chunk-base seed, which is not in this plan. **Do not describe this feature as "cracks on voxels"
without the qualifier.**

---

## 5. Required in the same commit as any `voxel.frag` change

Non-negotiable, from `CLAUDE.md`:

- `.\build_shaders.bat` and **commit the regenerated `.spv`**. `voxel.frag` is consumed by three
  pipelines; glslc does not track `#include` deps. Shipping the `.glsl` alone means it looks right on
  the author's machine and every other checkout renders the old shader (the transposed-AgX incident,
  `20341333`, five days of a pink world). This applies doubly to the new `crack.glsl` include.
- `tools/shader_manifest.py --check` must stay green, and must list `crack.glsl`. A shader with no
  manifest rule went stale for two weeks once (`gi_probe.comp`).
- `voxel.frag` is a **receiving shader** → update [`LightingPipeline.md`](LightingPipeline.md) §0
  receiver matrix + §9 change log, then `python tools/lighting_doc_check.py --update`
  (`build_and_test.ps1` runs `--check`).

---

## 6. Validation plan

**Required depth: L2 + L4.** Set by `DestructionSystemV2.md:455-457`, which already specifies Phase 5
as *"L2 (state) + L4 (pixel-diff visual, not 'looks cracked')"*. L3 does not apply — nothing walks on
a crack.

**No safety net exists.** `tests/golden/` holds `character_poses` only; nothing pins voxel surface
output. The tests below carry the whole load.

### 6.0 Where these tests can live — a hard constraint

`tests/CMakeLists.txt:23` links **`phyxel_core` only**. But `apply_damage`
(`editor/src/Application.cpp:14431`) and `setVoxelQueryHandler` (`:710`) are in the **editor**
target. **A `tests/core/` unit test therefore cannot observe the API at all**, so every red below
that asserts on a JSON field has to live somewhere else. Split by what is actually being asserted:

| Assertion | Lives in | Why |
|---|---|---|
| Accumulation, `responseFor` normalization, stage quantization at boundaries 0/1/2/3, §3.7 stage-transition dirty logic | `tests/core/` unit | All of it is `DamageSystem` / `ChunkRenderManager` — inside `phyxel_core` |
| Crack-field purity + `crack.glsl` hash guard (§6.2 first half) | `tests/core/` unit | CPU mirror, no engine needed |
| JSON field presence, `damage_tracked`, `stage_changed`, `damage01` values over HTTP | `tests/integration/` or live-engine L4 | Editor-hosted API; unreachable from core |
| Every pixel capture (§6.2 runtime half, R3, R5 L4 half) | live-engine L4 | Needs a real frame |

This was missed in the first two drafts, which filed all of it under `tests/core/`. §10's touch-set
reflects the split.

### 6.1 R1 — state (L2, red today)

Two tests, because §6.0 splits them.

**R1a — `tests/core/VoxelDamageStateTest.SubThresholdHitsAccumulate` (unit).** Drive `DamageSystem`
directly; apply three hits at 0.3 × toughness to a Stone voxel; assert `getAccumulatedDamage()` tracks
`K·e`, the voxel is still solid, and the quantized stage the mesher would compute advances 0 → 1 → 2.
No API involved, so this one is writable in `tests/core/`.

**R1b — `tests/integration/VoxelDamageApiTest.QuerySurfacesDamage` (integration/L4).** Same scenario
over HTTP; assert `damage_stage`, `damage01`, `toughness`, `damage_tracked` are present and consistent
with R1a's numbers, and `exists` stays `true`.

**R1b fails today with:** `damage_stage: field absent from /api/world/voxel response`. Stays red until
§3.1 lands. A second red, after §3.1 but before §3.2: assert Stone and Glass at the *same fraction of
their own toughness* report the same `damage_stage` — under `kDamageRef = 30` they do not (§3.2
table), so this fails with a concrete stage mismatch and passes once normalization lands.

**Third red, for the scope boundary (§3.6):** query a subcube wall voxel and assert
`damage_tracked == false` with the damage fields absent. Fails today with the field missing; protects
against a future change that starts reporting `damage01: 0` for untracked cells, which would read as
"pristine" and hide V1's boundary.

### 6.2 R2 — chunk-seam invariant (L2)

Two parts, and the weak one now has a guard so it cannot quietly become a fake check.

- **Purity.** `VoxelCrackSeamTest.CrackFieldIndependentOfChunkPartition`, FloraMarginTest shape,
  against a CPU mirror of the crack function (precedent: the wind-field probe is the CPU mirror).
  Assert the field over a damaged slab straddling x = 31/32 is bit-identical evaluated as one region
  vs two chunks.
  **Drift guard (added after the gate re-run):** a hand-ported CPU mirror silently stops matching the
  GLSL the moment the shader is edited, and the test keeps passing — a check named for a property it
  no longer measures. So the crack field lives in its own include (`shaders/crack.glsl`, §4.7) and the
  test asserts that file's content hash matches a recorded constant. Editing the shader fails the
  guard and forces a deliberate re-port. Same mechanism as `shader_manifest.py` hashing.
  Even guarded, this half is near-tautological and **cannot catch the real mistake** — a shader that
  reads `sizeU`. That is what the runtime half is for.
- **The one that bites (runtime).** Build a damaged wall across the x = 31/32 chunk boundary, capture
  at a fixed pose, and assert the luminance discontinuity at the seam column does not exceed
  neighbour-column variance. **Fails on a UV-seeded implementation with:**
  `crack(x=31.97)=0.82 vs crack(x=32.03)=0.11, discontinuity 0.71 > tol 0.05`.

### 6.3 R3 — visual (L4 pixel diff)

Fixed pose, fixed light, **`POST /api/debug/tonemap {"curve":0}` before every capture** — otherwise
everything passes through exposure ×8 + AgX and greys are unreadable (`CLAUDE.md` trap #1).

**Test world — deliberately small:**
- Flat world; one **8×4 full-cube Stone wall at x ∈ [8,15], y ∈ [17,20], z = 8** — wholly inside
  chunk (0,0,0) (which spans y 0-31). A rig spanning x/z −2..2 straddles four chunks, only one is
  resident in a fresh flat world, and 21 of 25 fills drop **silently** while the rig looks built.
- **⚠️ The wall must sit ABOVE y = 16.** A Flat world's surface is sea level —
  `col.surfaceY = static_cast<int>(kSeaLevelY)` (`WorldGenerator.cpp:941`, `kSeaLevelY = 16.0f` in
  `WorldConstants.h:17`) — and everything below is solid. Earlier drafts of this plan put the wall at
  y ∈ [8,11], i.e. **fully buried**: the mesher emits no face where the neighbour is solid, so the rig
  would have rendered *nothing* and looked exactly like a broken crack shader. Same class of trap as
  the four-chunk straddle above — the rig looks built and is not visible.
- **Full cubes, not a generated building** — per §3.6, a structure's subcube walls cannot display
  damage in V1, so building the rig from one would produce a null result that looks like a shader bug.
- **One variable:** accumulated damage on one voxel column. Nothing else moves.
- **Verify the world, not the response:** read damage back through §3.1. `apply_damage`'s `grazed`
  count says a call happened, not that state changed.

**Prediction, written before the run:** at stage 2 of 3, ≥ 12% of the target voxel's screen footprint
shifts by > 8/255 luminance; the pristine control shifts < 0.5%.

**Controls — both mandatory:**
- **C1, in-frame pristine voxel:** same material, same face, same lighting, same capture. Proves the
  metric measures *damage* and not an exposure or time-of-day drift. This is the grass-coverage
  lesson — that metric reported the exact opposite of the truth, and only a control caught it.
- **C2, null capture:** capture twice with no damage applied; diff must be ≈ 0. Catches TAA/dither
  nondeterminism, which would otherwise read as "cracks appeared."

**How the rig differs from shipped defaults, and what that does to the numbers:**
- Flat + tiny vs Perlin + streaming: face counts and merge-fragmentation cost are unrepresentative.
  **Do not quote rig FPS** — §3.5 costs come from a real settlement scene only.
- Near camera at 1 voxel = 1 m: cracks fill many pixels here and will be sub-pixel at distance. The
  rig is **optimistic about visibility**, and there is a speckle risk at range alongside the known
  character/grass sub-pixel speckle (`docs/RenderOptimization.md:489,513`).
- Run the **shipped smooth-lighting default**: with it off the mesher collapses per-face light so
  faces merge more freely, and the rig's face counts would lie.

### 6.4 R4 — stage-count / merge-cost A/B (ratifies §3.5)

In a **real settlement scene**, blast a wall to produce a damage gradient, then measure face count and
frame time at 3 / 7 / 15 stages via `GET /api/debug/gpu_scopes` and `get_render_stats`. Report the
table. Ship the coarsest quantization that still reads at gameplay distance. Control: the identical
scene with zero damage, for the un-fragmented baseline.

Report re-mesh counts alongside, since §3.7 ties rebuild frequency to stage transitions.

### 6.5 R5 — graze-only re-mesh (L4, red today; proves §3.7)

The test the other four cannot catch. **Pure-graze blast: energy chosen so `broken == 0` and
`grazed > 0`** against the R3 cube wall, with nothing else touching the chunk.

- **L2 half** (integration, per §6.0 — asserts on the `apply_damage` response): `broken == 0`,
  `grazed > 0`, `stage_changed > 0`.
- **L4 half** (live engine): capture before and after at a fixed pose; assert the target voxel's
  pixels changed.
- A **third, unit-level** companion in `tests/core/` asserts the stage-transition dirty logic itself:
  a graze that crosses a stage boundary marks the chunk dirty, one that does not leaves it clean
  (§3.7). That part needs no engine and guards the optimization directly.

**Fails today with:** state advances (`damage_stage` 0 → 1 via §3.1) while the capture is
**bit-identical** — because `updateDirtyChunks()` is never reached (`DamageSystem.cpp:304-306`) and no
rebuild occurred. That divergence between state and pixels is the exact signature of §3.7, and it is
the reason this red is separate from R1 and R3: R1 asserts state only, R3 uses a blast that also
breaks and therefore re-meshes as a side effect.

**Control:** the same blast energy against a chunk that *also* contains one breakable voxel — it
passes today (the break triggers the flush), proving the test measures the graze path specifically and
not a general rendering failure.

---

## 7. Phased roadmap

Ordered so each phase is provable before the next begins.

| Phase | Work | Depth | Gate to proceed |
|---|---|---|---|
| **P0** | §3.1 API readback (incl. `damage_tracked`) + **§3.7 graze re-mesh** + R1, R5 | L2+L4 | R1 green; R5 green; R1's Stone/Glass variant still **red** (proves it measures normalization) |
| **P1** | §3.2 toughness normalization + clamp comment | L2 | R1 Stone/Glass variant red→green |
| **P2** | §3.5 quantize to 3 stages (field width unchanged) | L2 | stage mapping unit-tested at boundaries 0/1/2/3; R5 still green with stage-gated dirtying |
| **P3** | §4 crack shader (`crack.glsl`, world-seeded) + R2 + R3 | L4 | R2 both parts green incl. the hash guard; R3 meets the written prediction with both controls |
| **P4** | R4 stage-count A/B in a real scene | L4 | table published; final stage count ratified or revised |
| **P5** | Per-material `crackStyle` from `brittleS1/S2` | L4 | visual A/B Glass vs Steel vs Stone, same pose |
| **V2** | **§3.6 sub-voxel damage** — storage design, sub/micro instance bits, subdivision inheritance | L2+L4 | memory cost measured before implementation; cracks visible on a generated building wall; §1 scope boundary retired |
| **V1.5** | §3.4 geometric spall at stage 3 | L2+L4 | **after V2** (§3.4) — needs its own order-independence test + occupancy regression pass |

Two things are **deliberately out of V1** and stated so they are known punts, not surprises:
- **Damage persistence** — cracks reset on reload. Pre-existing open question #4
  (`DestructionSystemV2.md:622-624`); needs a `world.db` schema change.
- **Cracks on buildings** — §3.6 / §1. Requires V2.

---

## 8. Open questions (not resolved here)

1. **Toughness grounding.** 101 of 108 materials use the `bondStrength * 120.0f` fallback
   (`DamageSystem.cpp:42-46`). Pre-existing open question #5. §3.2 makes it visible; it does not fix
   it. Needs grounding-auditor sign-off before those numbers are called grounded.
2. **Persistence.** Per-voxel damage in `world.db` — see §7.
3. **Sub-voxel damage storage + subdivision inheritance.** Promoted out of this list into §3.6 as a
   decided scope boundary with two open V2 questions. Left here as a pointer only.
4. **Does the store need a damage field?** Damage lives only on materialized overlay `Cube`s
   (`ChunkRenderManager.cpp:355`, `ChunkVoxelStore.h:19`), so mass grazing materializes mass Cubes.
   The stress axis for P0/P3: blast-graze a full chunk face and measure Cube count, memory, **and
   re-mesh count** (§3.7 makes the last one load-bearing).

---

## 9. Feature Design Keys — gate answers

| Key | Answer |
|---|---|
| **Voxel aesthetic** | Surface-state treatment, same class as shipped `vState` charred/wet. Painted cracks are explicitly a *stage*, not the destination — §3.4 commits to geometric spall and sizes V1's fracture cells to the microcube grid so the two agree. |
| **Sub-voxel authoring** | V1 authors no assets (N/A). V1.5 spall is microcube-resolution by construction. **V1 does not *render* on sub-voxels at all** — §3.6, declared in §1. |
| **Unconditional detail** | Yes. The `damageStage == 0` branch bounds **cost**, not appearance — no quality tier, no flag. |
| **Chunk independence** | The one real risk, resolved in §3.3: seeded from `worldPosAbs`, never `sizeU`/`sizeV`/`texCoord`. Merge-key fragmentation affects cost only. **No cross-chunk lookup** — and §3.3 records why adding one would be wrong. Pinned by R2 (§6.2), whose CPU mirror is hash-guarded against drift. |
| **Procedural pipeline** | Belongs to no generation stage — runtime world-state visualization, sibling of `vState`. Consumes nothing from terrain→structures. |
| **Order-independence** | The crack *function* is pure — inputs `(worldPosAbs, faceNormal, damageStage, crackStyle)`, no cross-cell reads, no mutable state — so per-chunk and whole-region evaluation cannot differ. One honest caveat: damage *accumulation* is `accumulatedDamage += amount` (`Cube.h:101`), and float addition is not associative, so overlapping blasts applied in different orders give bit-different totals. The divergence is orders of magnitude below one 3-stage quantization step, so it cannot change a rendered stage — but it is a real non-associativity and is recorded rather than glossed. |
| **World recipe** | Not needed for V1: damage is not persisted, so no existing world carries crack state a `materials.json` edit could silently change. `crackStyle` is a material appearance property, same class as its texture. Revisit if persistence lands. |
| **API** | §3.1 — units named per field, denominator echoed, `damage_stage` is the shader's real input, air omits the fields, subdivided cells report `damage_tracked:false` rather than a false zero. `stage_changed` echoed so a graze is assertable (§3.7). Clamp keeps its reason at the site (§3.2). |
| **Defaults** | Crack rendering ships ON (required). No golden-image test pins voxel surface output, so §6 carries it. |
| **Visual test plan** | §6 — L2+L4, five red tests each with its failure text, one-chunk full-cube rig, prediction written in advance, controls on R3 and R5, rig-vs-default deltas stated. |

---

## 10. Anticipated touch-set

- `editor/src/Application.cpp` — `setVoxelQueryHandler` (§3.1); `apply_damage` response gains
  `stage_changed` (§3.7)
- `engine/src/core/DamageSystem.cpp` — graze-branch dirty-mark + un-gated flush, `DamageResult::voxelsStageChanged` (§3.7)
- `engine/include/physics|core/DamageSystem.h` — `DamageResult` field (§3.7)
- `engine/src/graphics/ChunkRenderManager.cpp` — `:370-374` normalization + clamp comment (§3.2),
  stage quantization (§3.5); **V2 only:** sub/micro damage bits at `:1062-1064`, `:1222-1224` (§3.6)
- `shaders/crack.glsl` — **new**, the fracture field, single source of truth (§4.7)
- `shaders/voxel.frag` — crack model (§4); **plus `build_shaders.bat`, committed `.spv`,
  `shader_manifest.py` entry for `crack.glsl`, `LightingPipeline.md` §0/§9,
  `lighting_doc_check.py --update`** (§5)
- `resources/materials.json` — `crackStyle` per material (P5)
- **Tests, split per §6.0** (`tests/` links `phyxel_core` only; the API lives in the editor):
  - `tests/core/VoxelDamageStateTest.cpp` — accumulation + normalization + stage quantization (R1a)
  - `tests/core/VoxelCrackSeamTest.cpp` — crack-field purity + `crack.glsl` hash guard (§6.2 first half)
  - `tests/core/VoxelGrazeDirtyTest.cpp` — stage-transition dirty logic (R5 unit companion)
  - `tests/integration/VoxelDamageApiTest.cpp` — JSON contract: `damage_stage`, `damage01`,
    `damage_tracked`, `stage_changed` (R1b, R5 L2 half)
  - live-engine L4 captures — §6.2 runtime half, R3, R5 L4 half
- `scripts/mcp/phyxel_mcp_server.py` — `query_voxel` description, new fields
- `docs/DestructionSystemV2.md` §5(F) — cross-link to this doc

---

## 11. Change log

- **2026-09-22 (third pass)** — gate re-run against this document. **The design (§3.1-3.7) held
  unchanged**; all three findings were in the validation plan and one scope note, and all were
  mechanical (no design change, no scope decision). (a) **Test placement was impossible as filed** —
  `tests/CMakeLists.txt:23` links `phyxel_core` only while the API lives in the editor target, so the
  JSON-asserting reds could not run from `tests/core/`; new **§6.0** splits every test by what it
  asserts, and §6.1/§6.5/§10 follow it. (b) **The R3/R5 rig was buried in terrain** — the wall sat at
  y ∈ [8,11] in a Flat world whose surface is y = 16 (`WorldGenerator.cpp:941`), so it would have
  rendered nothing and read as a broken shader; moved to y ∈ [17,20] with the trap recorded.
  (c) **§4's scope note overstated reach** — `kinematic_voxel.vert:126` and
  `dynamic_voxel.vert:267-268` hardcode `flags = 0u` *and* zero the world-position seed, so debris and
  furniture are structurally excluded, not merely unwired.
- **2026-09-22 (re-run)** — second design-check pass against this document found three further items,
  now resolved: **§3.6** sub-voxel faces carry no damage bits (`ChunkRenderManager.cpp:1062-1064`,
  `:1222-1224`) and every generated wall is sub-cube (`StructureRealizer.cpp:163-165`), so V1 cannot
  crack a building — declared as a scope boundary in §1 with V2 as the fix; **§3.7** the graze path
  never triggers a re-mesh (`DamageSystem.cpp:304-306` gates the flush on breaks), now a P0 fix with
  its own red test **R5** (§6.5). Minor: R2's CPU mirror gained a hash guard against drift (§6.2),
  float-accumulation non-associativity recorded (§9), damage-loss-on-subdivision specified (§3.6),
  and V1.5 re-ordered to depend on V2 (§3.4, §7).
- **2026-09-22** — created. Design-check gate run against `FeatureDesignKeys.md`; verdict NEEDS WORK
  with five items; this document resolves all five (§3.1-3.5) and supersedes
  `DestructionSystemV2.md` §5(F). Nothing implemented yet.

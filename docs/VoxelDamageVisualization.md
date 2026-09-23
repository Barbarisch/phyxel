# Voxel Damage Visualization — progressive cracks (P4)

**Status:** **P0 + P0.5 + P1 + P2 + P3 built** on `feature/voxel-damage-cracks`.
**§14 visual review: SIGNED OFF 2026-09-22** (§13). One item still open before P3 is fully closed:
§6.2's RUNTIME seam test has not been run. Design-check gate run **five times**
(2026-09-22), 21 items found and resolved — see §11 for the full ledger:
- **Pass 1** → NEEDS WORK, 5 items: API readback, toughness normalization, world-position seeding,
  the geometric-spall position, stage quantization/merge cost (§3.1-3.5).
- **Pass 2** → NEEDS WORK, 3 items: sub-voxel coverage (§3.6), graze re-mesh (§3.7), plus CPU-mirror
  drift, float non-associativity and damage-loss-on-subdivision (§6.2 / §9 / §3.6).
- **Pass 3** → NEEDS WORK, 3 items, **all in the validation plan — the design held unchanged**: test
  placement (§6.0), a rig buried under terrain (§6.3), an overstated pipeline scope note (§4).
- **Pass 4** → NEEDS WORK, 5 items, **the design held unchanged again**; one ordering change, one
  correction, three validation gaps: V1's real coverage vs. phase order (§7 **P0.5**), a
  mis-attributed materialization cost (§8.4), stage-is-per-merge-run and what it does to R2's rig
  (§3.3 / §6.2), a file-hash guard that cannot catch divergence (§6.2), and no legibility
  measurement at gameplay distance (§6.4). Pass 4 also **audited the citations**: 7 of 7
  load-bearing code references checked verbatim against `main` @ `f3c85c1e` — all accurate.
- **Pass 5** → NEEDS WORK, 5 items. **The design held a third time**; the findings are one shipped-
  behaviour discovery and four build-spec defects: damage is lost on **chunk eviction**, not merely on
  reload (§7 — and no test in this plan could see it); §3.7 named the **wrong re-mesh function**
  (`markChunkDirty` persists to SQLite — a known save-stall regression); R4's stage-count A/B had **no
  mechanism** (§6.4); R2's runtime rig spans two chunks and never asserted the second is **resident**
  (§6.2); and an existing pinned test went unnamed (§9).

This document resolves all of them and is the build plan.
**Verdict of record for every pass: §12. Running build log (updated as each phase lands): §13.**
**§14 is the MANUAL VISUAL REVIEW gate — a human looks at it and signs off. P3 and P5 are not done
without it, and no automated pixel diff substitutes for it.**
**§16 is the single list of what remains open**, including one item that is NOT this feature's
(a shader-toolchain hazard, logged in `StructurePipelineGaps.md`).
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

**Be precise about what is continuous: the FIELD, not the STAGE (pass 4).** `damageStage` is folded
into the merge key (§3.5) and is therefore constant across a merged run and *piecewise-constant per
voxel* across a damage gradient. So crack **width** steps at every voxel boundary where the
neighbouring stage differs, even though the crack **geometry** flows through it unbroken. That is the
intended read — it is how a player sees which voxel is closest to failing — but it means the surface
is not literally "one continuous cracked plane", and §6.2's seam test must be built knowing it:
a stage step at x = 31/32 is a legitimate discontinuity, not a UV-seeding bug.

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
  ~~**Memory is the open question, and it is not small:** a cube's damage is one float on a
  materialized overlay `Cube`, but a subcube grid is 27× the cell count and a microcube grid 729×. A
  naive float-per-cell is not obviously affordable and needs its own design plus measurement.~~
  ⚠️ **This premise was WRONG and is retracted — see §15 (P0.5).** It assumed DENSE sub-voxel grids;
  this engine stores sub-voxels **sparsely** (`std::vector<std::unique_ptr<Subcube>>`, `Chunk.h:76-77`),
  so undisturbed terrain holds zero of them and "27× / 729×" describes a fully-subdivided chunk that
  never occurs. Measured cost of per-cell damage: **+8 bytes on a 120-byte `Subcube` (+6.7%)**. The
  scope boundary itself still stands — V1 ships cube-only — but it stands on *keeping one damage model*
  and on not designing storage before the crack is validated, **not** on memory.
- The alternative — building sub-voxel damage as a hard prerequisite — front-loads the biggest design
  risk and delays any visual proof.
  ⚠️ **Not to be confused with §7's `P0.5`**, added in pass 4, which is a *paper costing* of the two
  storage options below and writes no code. That one is cheap and is scheduled early precisely
  because P3's shader should be written knowing which way V2 will go. What is rejected here is
  *implementing* sub-voxel storage before the crack model is validated.

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
  changed, **`markChunkForRemesh`** on that voxel's chunk — *not* `markChunkDirty`.

> ⚠️ **Use `markChunkForRemesh`, and nothing else (pass 5 — the first draft named the wrong one).**
> There are three tiers (`ChunkManager.cpp:910-924`), and they are not interchangeable:
> - **`markChunkDirty`** also sets the chunk's **DB-dirty flag**, which makes the streaming evictor
>   re-save the chunk to SQLite (`DirtyChunkTracker.h:65-75`). Damage has **no DB field at all**, so
>   every one of those writes would persist nothing that changed — pure waste. The header records the
>   consequence in its own words: *"mass evictions of such chunks caused multi-hundred-ms save
>   stalls."* A blast grazes thousands of voxels across many chunks (§8.4), so this is precisely the
>   mass case. **Naming `markChunkDirty` here would have re-introduced a fixed regression.**
> - **`markChunkForRemesh`** is the budgeted, mesh-only tier, explicitly for "the voxel DATA is
>   unchanged, only the render mesh is stale." That is exactly a graze: the crack is a re-shade of
>   existing geometry. **This is the correct tier.**
> - **`markChunkForRemeshIdle`** is processed only when the primary queue is empty
>   (`DirtyChunkTracker.h:79-88`), for cosmetic convergence like neighbour re-culls. **Rejected:** a
>   crack is direct feedback on the player's own blow, so deferring it behind an arbitrary-length
>   queue would make weak hits feel unresponsive — the exact failure §3.7 exists to fix.
>
> Pin it: the §6.5 unit companion asserts the graze path marks the chunk for **re-mesh** and leaves
> `getIsDirty()` **false**, so a future edit cannot silently promote it back to the DB tier.
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

   ### 4.4a How `crackStyle` REACHES the shader (resolved at gate pass 2 — it was a blocker)

   **Neither obvious route had room.** The instance `reserved` word is **fully allocated**: bit 0
   emissive, bit 1 transparent, **bits 2-9 quantized alpha**, bit 10 mirror, bits 11-14 damage,
   bit 15 `varied` (`ChunkRenderManager.cpp:388-390`) — 16 of 16 bits. And the per-material props
   `vec4` in the atlas SSBO is **fully used**: `metallic`, `roughness`, `emStrength`,
   `emThreshold` (`voxel.frag:277-281`, packed at `AtlasManager.cpp:498-517`).

   **Decision: widen the per-material props array to a STRIDE OF TWO `vec4`s per layer.**
   ```
   props[gi*2 + 0] = (metallic, roughness, emStrength, emThreshold)   // unchanged
   props[gi*2 + 1] = (crackStyle, 0, 0, 0)                            // 3 floats spare
   ```
   Why this rather than reclaiming instance bits:
   - **`crackStyle` is a per-MATERIAL property, not per-face** — the props array is exactly what
     that array is for, and the instance word is not.
   - **No instance-format change**, so the 24-byte `InstanceData` and its hand-synced twin
     (`reference_dual_instancedata_struct`) are untouched — no ABI churn, no risk to the greedy
     merge key.
   - **The alternative is worse:** the only reclaimable bits are the 8-bit alpha (bits 2-9), and
     narrowing transparency precision to buy an appearance knob trades a shipped feature's fidelity
     for a new one's convenience.
   - **Cost is trivial and bounded:** the array is sized by *texture layer*, not by voxel, so this
     doubles a few KB.
   - It leaves **3 spare floats per material**, which is the first free slot for per-material
     appearance data since the props array was repurposed.

   ### 4.4b The `brittleS1` → `style` mapping (was unspecified)

   `crack.glsl` divides by `kCrackCell * style`, so **larger style = larger cells = sparser**.
   `brittleS1` spans 1.3 (Glass) to 4.5 (Steel), so the direction is already right, but passing it
   through raw is wrong: style 4.5 gives 1.5 m cells, **larger than a whole voxel**, so a 1 m face
   would show less than one cell.

   **Mapping: normalize `s1`'s observed range onto a bounded style range.**
   ```
   style = 0.75 + (clamp(s1, 1.3, 4.5) - 1.3) / 3.2 * 0.85      // -> [0.75, 1.60]
   ```
   | material | `s1` | style | cells per 1 m face |
   |---|---|---|---|
   | Glass | 1.3 | 0.75 | ~4.0 — dense, fine |
   | Stone | 1.8 | 0.88 | ~3.4 |
   | Wood | 3.5 | 1.33 | ~2.3 |
   | Steel | 4.5 | 1.60 | ~1.9 — sparse, wide |

   ⚠️ **The floor of 0.75 is load-bearing, not cosmetic.** P3 measured that the *primary* network
   had to sit on the subcube lattice to stay legible at 16 units; a style below ~0.75 pushes the
   brittle materials back toward the microcube lattice and **reintroduces the exact sub-pixel
   failure P3 fixed**. So the range is clamped, and **P4's distance ladder must be re-run at the
   style extremes**, not only at style 1.0. Like the stage count, this range is a **starting
   hypothesis to be measured**, not a settled number.

   **Red test for P5 (§7's gate was "visual A/B", which is a comparison, not a measurable claim):**
   `VoxelCrackSeamTest.StyleChangesCrackDensityMeasurably` — evaluate the CPU mirror over a fixed
   patch at Glass / Stone / Steel styles and assert the fraction of samples on a crack differs by
   at least 1.5× between the extremes. **Fails today with:** all three identical, because every
   material renders at style 1.0. This is L2 and runs before a single pixel is captured.
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
  **Drift guard — assert on VALUES, not on a file hash (revised, pass 4):** a hand-ported CPU mirror
  silently stops matching the GLSL the moment the shader is edited, and the test keeps passing — a
  check named for a property it no longer measures. The crack field therefore lives in its own
  include (`shaders/crack.glsl`, §4.7). The fix first proposed here was to hash that file and pin the
  digest. **Reject the hash as the primary guard:** it reddens on any edit including a comment, its
  only repair is bumping a constant, and it therefore trains exactly the reflex it was meant to
  prevent — bump the digest, skip the re-port. It detects *that the file changed*, never *that the
  mirror diverged*.
  **Instead:** pin a table of ~32 sampled field values at fixed `(worldPosAbs, faceNormal, stage)`
  inputs spanning cell interiors, cell edges and all four stages, and assert the CPU mirror
  reproduces them to a stated tolerance. A cosmetic shader edit leaves the table green; a real change
  to the field reddens it *and names the input that moved*. Generating that table from the GLSL
  offline and checking it in is better still, and is the preferred form if it is cheap.
  **Keep the file hash as a secondary tripwire** — it says the table may need regenerating — but it
  must never be the only guard.
  Even guarded, this half is near-tautological and **cannot catch the real mistake** — a shader that
  reads `sizeU`. That is what the runtime half is for.
- **The one that bites (runtime).** Build a damaged wall across the x = 31/32 chunk boundary, capture
  at a fixed pose, and assert the luminance discontinuity at the seam column does not exceed
  neighbour-column variance. **Fails on a UV-seeded implementation with:**
  `crack(x=31.97)=0.82 vs crack(x=32.03)=0.11, discontinuity 0.71 > tol 0.05`.
  ⚠️ **Rig precondition, or this test fails for the wrong reason (pass 4).** Per §3.3 the *stage* is
  piecewise-constant per voxel, so a damage gradient across the seam produces a real and correct
  luminance step that this assertion would read as a seam defect. **Damage both sides of x = 31/32 to
  the same quantized `damage_stage`, and assert that through §3.1's readback before capturing.**
  Verify the resulting *stage*, not the applied energy — two equal `apply_damage` calls can straddle
  a quantization boundary. With equal stages on both sides, any remaining discontinuity belongs to
  the crack field, which is exactly what is under test.
  ⚠️ **CONTROL, mandatory (gate pass 2).** Measure the luminance discontinuity at a **mid-chunk
  voxel boundary** in the SAME capture, and require the seam discontinuity to be no worse. Without
  it the metric cannot separate "no seam defect" from "measuring nothing": a uniformly smooth wall
  passes trivially, and a legitimate stage step reads as a failure. The control is what makes the
  threshold mean something.
  ⚠️ **This test is GREEN ON ARRIVAL, and that must not be dressed up as red-before-green
  (gate pass 2).** The shipped shader is already world-seeded, so the failure text below cannot be
  observed without deliberately breaking it. Demonstrate it the way §3.7's red was demonstrated:
  **temporarily seed `crackField` from `texCoord` instead of `worldPosAbs`, rebuild, observe the
  seam discontinuity, revert.** Record it in §13 as a retroactive red, stating plainly that it was
  retroactive.
  ⚠️ **Second precondition: BOTH chunks must be resident (pass 5).** This rig spans x = 31/32 by
  necessity, so the gate's "keep the rig inside ONE chunk" cannot apply — which makes the silent-drop
  trap *more* live here than anywhere else in this plan, not less. **Assert chunk (0,0,0) and chunk
  (1,0,0) both exist before filling, and read the voxels back afterwards** (`/api/world/fill` is async
  and returns no placed count). This is the same class of defect pass 3 found in §6.3's rig — a rig
  that looks built, is not, and whose null result reads as a broken crack shader.

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
  **So R3 proves the crack exists, never that it is legible in play (pass 4).** With ~11 cm fracture
  cells on a 1 m face (§4.2) the lattice is ~9×9 per face, squarely in that speckle regime once the
  face covers only a few dozen pixels. **R3 green is not evidence the feature reads** — that is R4's
  job (§6.4), and P3 may not be called done on R3 alone.
- Run the **shipped smooth-lighting default**: with it off the mesher collapses per-face light so
  faces merge more freely, and the rig's face counts would lie.

### 6.4 R4 — stage-count / merge-cost / legibility A/B (ratifies §3.5)

**Two axes, both measured — cost and legibility (second axis added pass 4).** Earlier drafts measured
cost only and left legibility as a judgement call inside the ship criterion ("the coarsest that still
reads at gameplay distance"). That is half the decision with no number attached, and R3 cannot supply
it (§6.3).

**Cost axis.** In a **real settlement scene**, blast a wall to produce a damage gradient, then measure
face count and frame time at 3 / 7 / 15 stages via `GET /api/debug/gpu_scopes` (scopes nest — never
sum them) and `get_render_stats`. Report the table. Control: the identical scene with zero damage,
for the un-fragmented baseline. Report re-mesh counts alongside, since §3.7 ties rebuild frequency to
stage transitions.

**Legibility axis.** Same scene, same damaged wall, captured at a **ladder of camera distances —
4 / 16 / 48 / 96 units** — for each stage count, with `POST /api/debug/tonemap {"curve":0}` as in
§6.3. At each distance, measured against an in-frame pristine control voxel (C1):
- the fraction of the damaged face's pixels differing from the control by > 8/255 luminance, and
- the variance across 3 captures at a **fixed** pose, which catches the speckle failure — a crack
  that shimmers rather than resolves.

**Cost-axis prediction, written before the run (gate pass 2 — the first draft's was directional
and therefore could not fail).** Damage is folded into the merge key, so a radial damage gradient
breaks merge runs once per distinct stage value: 15 stages produce up to 15 concentric bands, 3
produce 3. **Predicted: the DAMAGED REGION's face count at 3 stages is at least 2× lower than at 15
stages.** Total scene face count moves by less, in proportion to the damaged fraction of the scene —
so report both, and do not quote the scene total as if it were the effect size. **Falsifiable:** if
the damaged region's face count differs by less than ~1.3× between 3 and 15 stages, merge
fragmentation is not where the cost lives and §3.5's central cost argument is wrong.

**Pinned tests that move if P4 revises the stage count** (defaults are a pinned contract):
`VoxelDamageStateTest.StageQuantizationAtEveryBoundary`,
`.OnlyFourDistinctPackedValuesAcrossTheWholeDamageRange`, and
`.CoarseStagesBoundTheRemeshCountPerVoxel` all assert against `kDamageStagesVisible` and must be
updated in the same commit, with the measured table as the reason.

**Legibility prediction, written before the run:** legibility falls off monotonically with distance,
and the 3-stage variant stays distinguishable from pristine (≥ 5% of face pixels) further out than
the 15-stage variant, because wider bands survive minification. **If the crack is instead invisible at
every stage count beyond ~48 units, that is a finding, not a failure** — it means the fracture cell
size (§4.2, microcube grid) is wrong for gameplay viewing distance and must be re-derived. Far
cheaper to learn it here than after P5.

**Ship the stage count that is the coarsest acceptable on the cost table AND still legible at the
distances players actually fight at.** If those two disagree, say so and choose explicitly; do not
split the difference.

**How the A/B is actually switched (pass 5 — previously unspecified).** The quantization is a literal
today (`ChunkRenderManager.cpp:370-374`), so "measure at 3 / 7 / 15" silently implied three rebuilds
of a Release engine plus three scene setups — which is slow enough that it would have been skipped or
faked. Two constraints decide it:
- the stage count is baked into **both** the merge key (`:741-744`) and the instance word, so changing
  it **must force a full re-mesh of every loaded chunk**, not just a shader reload; and
- an A/B whose arms are separate binaries cannot hold "the exact same frame" fixed, which is what the
  gate's measure-don't-infer rule requires.

⚠️ **The knob cannot set a `constexpr`, and the fix has a threading consequence (gate pass 2).**
`kDamageStagesVisible` ships as `inline constexpr int` (`DamageStage.h`), so a runtime endpoint
cannot change it. Making it mutable matters because it is read **per voxel** during chunk meshing,
and chunk rebuilds run on **worker threads** — a naive mutable global is a cross-thread read in a
32,768-cell loop.

**Resolved:** make it a `std::atomic<int>` with an accessor, and have every consumer **read it ONCE
into a local** — the mesher at the top of a chunk rebuild, `DamageSystem` at the top of
`applyDamage`. That is simultaneously the race fix and the performance answer: a rebuild in flight
uses one consistent value throughout, there is no atomic load in the inner loop, and the forced
full re-mesh after a knob change leaves no chunk holding a stale count. The constant keeps its
doc comment; only its storage class changes.

**Decision: a debug endpoint, `POST /api/debug/damage_stages {"stages": N}`**, which sets the
quantization and triggers a full re-mesh before returning. Per the API key: `stages` is an **ordinal
count** (not a bit width); **omitted means unchanged**, matching the `/api/debug/*` convention; it is
**clamped to [1, 15]** at entry with the reason written at the clamp site — *above 15 the quantized
value overflows bits 11-14 into bit 15, which is the `varied` texture-rotation flag* (§3.2); and the
response **echoes back the applied `stages` and the number of chunks re-meshed**, so a caller can
assert the knob took effect rather than trusting a stale binary. It is a debug knob, so it ships
**default = the shipped stage count** and pins nothing.

**This is P4's only API addition beyond §3.1**, and it exists because a measurement with no mechanism
does not get made.

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
| **P0** ✅ | §3.1 API readback (incl. `damage_tracked`) + **§3.7 graze re-mesh** + R1, R5 | L2+L4 | R1 green; R5 green; R1's Stone/Glass variant still **red** (proves it measures normalization) — **DONE**, see §13 |
| **P0.5** ✅ | **Cost the V2 sub-voxel storage options on paper** (§3.6's two questions) — per-cell vs. per-parent-cube aggregate, memory measured, no implementation | doc | a costed recommendation exists **before P3 writes the shader** — **DONE, §15. The premise it was built on turned out to be wrong**; does not gate P1/P2 |
| **P1** ✅ | §3.2 toughness normalization + clamp comment | L2 | R1 Stone/Glass variant red→green — **DONE**, see §13 |
| **P2** ✅ | §3.5 quantize to 3 stages (field width unchanged) | L2 | stage mapping unit-tested at boundaries 0/1/2/3; R5 still green with stage-gated dirtying — **DONE**, see §13 |
| **P3** ✅ | §4 crack shader (`crack.glsl`, world-seeded) + R2 + R3 | L4 | R2 green (18/18); R3 met its written prediction with both controls; **§14 SIGNED OFF** — see §13. Remaining: §6.2 runtime seam test |
| **P4** | R4 stage-count A/B in a real scene | L4 | table published; final stage count ratified or revised |
| **P5** | Per-material `crackStyle` from `brittleS1/S2` | L4 | visual A/B Glass vs Steel vs Stone, same pose; **§14 visual review signed off** |
| **V2** | **§3.6 sub-voxel damage** — storage design, sub/micro instance bits, subdivision inheritance | L2+L4 | memory cost measured before implementation; cracks visible on a generated building wall; §1 scope boundary retired |
| **V1.5** | §3.4 geometric spall at stage 3 | L2+L4 | **after V2** (§3.4) — needs its own order-independence test + occupancy regression pass |

**What V1 actually renders on, added up in one place (pass 4).** The exclusions are each declared
honestly — §1, §3.6, §4 — but never totalled, and the total is the thing worth deciding against:
**terrain and hand-placed cube fills. Nothing else.** Not generated buildings (§3.6 — every wall is
sub-cube), not kinematic furniture or doors, not GPU debris (§4 — both pipelines hardcode
`flags = 0u`). The motivating case, damaging structures, is entirely V2.

That order is defensible: the crack model gets proven on a surface that already carries the state, so
red-before-green stays tight. But it does mean **P0–P5 ship a feature a player mostly cannot see in a
normal scene**, and P3 writes a shader without knowing whether it generalizes to sub-voxel cells or
gets rewritten for them. Hence **P0.5** — the V2 storage decision is *costed* early, on paper.
§3.6's rejection of a full sub-voxel *implementation* as a prerequisite still stands; this adds a
paper decision, not the build. Costing it is cheap; discovering it after P5 is not.

Two things are **deliberately out of V1** and stated so they are known punts, not surprises:
- **Damage persistence** — pre-existing open question #4 (`DestructionSystemV2.md:622-624`); needs a
  `world.db` schema change.
  ⚠️ **Corrected and upgraded, pass 5 — this is worse than "resets on reload", and it is the one
  finding that came from shipped behaviour rather than from this document.** Damage lives only on
  materialized overlay `Cube`s, and the streaming evictor **destroys the chunk object** once the
  camera passes `unloadRadius` (`ChunkStreamingManager.cpp:465-496`); it saves first only if
  `getIsDirty()`, and `saveChunk` persists **no damage field** (grep for `damage` across
  `ChunkStreamingManager.cpp` and `WorldDatabase.cpp` returns nothing). So the real V1 behaviour is:
  **damage a cliff, walk past the unload radius, walk back — the cracks are gone.** Not on reload.
  On a stroll.
  Two things make this sharper than an ordinary punt:
  1. **It fires in exactly the worlds where V1 has anything to show.** Terrain is V1's entire
     coverage (above), and terrain worlds are streaming worlds. DB-only worlds *"keep full residency
     and never evict"* (`ChunkStreamingManager.cpp:199`).
  2. **No test in this plan can see it.** The R3/R5 rig is a small flat DB-only world — it never
     evicts. Structurally identical to §3.7: invisible in the test path, live in the real one.
  **It stays out of V1** — it is a schema change, not a shader change, and V1's value does not depend
  on it. But it is now a **declared behaviour**, not a discovery waiting to happen: no demo or claim
  about this feature may describe cracks as persistent, and **V2's gate gains "damage survives a
  stream-out/stream-in round trip"** as a checklist item alongside the sub-voxel work.
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
   (`ChunkRenderManager.cpp:355`, `ChunkVoxelStore.h:19`); the mesher's store branch is explicitly
   commented `// store voxels carry no damage`.
   **Corrected, pass 4.** Earlier drafts read "mass grazing materializes mass Cubes", implying this
   feature causes the materialization. It does not. `DamageSystem` Phase A calls
   `m_cm->getCubeAt(wp)` on **every voxel in the blast AABB** (`DamageSystem.cpp:147`), and the
   ellipsoid reject (`d > 1.0f`) happens *after* it; `getCubeAt` → `getCubeAtFast` → `materializeAt`
   (`ChunkVoxelQuerySystem.cpp:57-60`, `ChunkVoxelManager.cpp:414-422`) allocates and **retains** a
   `Cube` in `cubes[index]`. So every blast already materializes its whole bounding box today,
   damage or no damage. The cost is **pre-existing, larger than this plan implied, and not worsened
   by P4** — it is also, incidentally, why terrain can crack at all: store-backed terrain gets a real
   `Cube` to accumulate onto (verified pass 4, and the reason §1's terrain claim holds).
   Two consequences: (a) the stress axis below measures a largely pre-existing cost, so a bad number
   is not by itself a reason to block this feature; (b) AABB-wide materialization deserves its own
   look as a destruction-system issue — filed here rather than silently inherited.
   The stress axis for P0/P3 stands: blast-graze a full chunk face and measure Cube count, memory,
   **and re-mesh count** (§3.7 makes the last one load-bearing) — reporting the zero-damage control
   alongside, so the pre-existing share is visible.

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
| **Defaults** | Crack rendering ships ON (required). No golden-image test pins voxel *surface* output, so §6 carries it — but the accumulation contract **is** already pinned: `ChunkVoxelAuthorityTest.cpp:97-101` asserts damage survives re-materialization (`addDamage(10.0f)` → same `Cube` → `10.0f`). §3.2 changes the **mesher's** normalization, not accumulation, so that pin stays green; R1a extends it rather than replacing it (pass 5). The one new default is §6.4's `damage_stages` debug knob, which ships at the shipped stage count and pins nothing. |
| **Visual test plan** | §6 — L2+L4, five red tests each with its failure text, one-chunk full-cube rig, prediction written in advance, controls on R3 and R5, rig-vs-default deltas stated. R4 measures **legibility across a 4/16/48/96-unit distance ladder** as well as cost, because R3 is near-camera and cannot show whether the crack reads in play (§6.3, §6.4). |

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
  - `tests/core/VoxelCrackSeamTest.cpp` — crack-field purity + **pinned sampled-value table** for the
    CPU mirror (~32 inputs across cell interiors/edges/stages), with the `crack.glsl` content hash
    kept only as a regenerate-the-table tripwire (§6.2 first half)
  - `tests/core/VoxelGrazeDirtyTest.cpp` — stage-transition dirty logic (R5 unit companion)
  - `tests/integration/VoxelDamageApiTest.cpp` — JSON contract: `damage_stage`, `damage01`,
    `damage_tracked`, `stage_changed` (R1b, R5 L2 half)
  - live-engine L4 captures — §6.2 runtime half, R3, R5 L4 half
- `scripts/mcp/phyxel_mcp_server.py` — `query_voxel` description, new fields
- `docs/DestructionSystemV2.md` §5(F) — cross-link to this doc

---

## 11. Change log

- **2026-09-22 (fifth pass)** — gate re-run before implementation began; branch
  `feature/voxel-damage-cracks` cut from this point. **The design held for a third consecutive pass.**
  One finding came from shipped behaviour and four were defects in the build spec.
  (a) **Damage is lost on chunk eviction, not on reload** — the evictor destroys the chunk and its
  materialized `Cube`s (`ChunkStreamingManager.cpp:465-496`) and no damage field is persisted, so
  cracks vanish when the player walks past `unloadRadius`. It fires only in streaming worlds, which is
  exactly where V1's terrain coverage lives, and **no test in this plan could observe it** because the
  rig is a non-evicting flat DB world. Restated in §7 as declared behaviour; added to V2's gate.
  (b) **§3.7 named the wrong re-mesh function** — `markChunkDirty` also sets the DB-dirty flag
  (`DirtyChunkTracker.h:65-75`), which would make the evictor re-save chunks for damage that has no DB
  field, at the "thousands of grazed voxels" scale the header blames for *"multi-hundred-ms save
  stalls."* Corrected to `markChunkForRemesh` (mesh-only, budgeted), with `markChunkForRemeshIdle`
  considered and rejected — a crack is direct feedback on the player's own blow and must not sit
  behind an idle queue. §6.5's unit companion now also asserts `getIsDirty()` stays false.
  (c) **R4's stage-count A/B had no mechanism** — the quantization is a literal, so 3/7/15 implied
  three rebuilds and separate frames, which the gate's measure-don't-infer rule forbids; specified
  `POST /api/debug/damage_stages` (ordinal, omitted-means-unchanged, clamped [1,15] with the bit-15
  `varied` overflow written at the clamp, echoes applied stages + chunks re-meshed).
  (d) **R2's runtime rig spans two chunks and never asserted the second was resident** — same class as
  the pass-3 buried-wall finding, in the test pass 3 did not revisit; residency + read-back is now a
  stated precondition.
  (e) **An existing pin went unnamed** — `ChunkVoxelAuthorityTest.cpp:97-101` already guards damage
  accumulation across re-materialization; §9's "no pinned test" was imprecise. It stays green under
  §3.2 (which changes the mesher, not accumulation).
- **2026-09-22 (fourth pass)** — independent review of this document against `main` @ `f3c85c1e`.
  **Citation audit first:** 7 of 7 load-bearing code references were checked verbatim and all are
  accurate, including the two the plan leans hardest on — the graze flush gate
  (`DamageSystem.cpp:304-306`) and the sub-voxel `reserved` writes (`ChunkRenderManager.cpp:1062-1064`,
  `:1222-1224`). **The design (§3.1-3.7) held unchanged for the second consecutive pass.** Five items,
  one of them an ordering change: (a) **V1's coverage totals to terrain + cube fills only** — each
  exclusion was declared but never added up, and the sum means P0-P5 ship something largely invisible
  in a normal scene; added the total to §7 and inserted **P0.5**, a paper costing of the V2 storage
  decision before P3 writes the shader (§3.6's rejection of a full sub-voxel *implementation* as a
  prerequisite stands). (b) **§8.4 mis-attributed the materialization cost** — `DamageSystem` Phase A
  already calls `getCubeAt` on the entire blast AABB before the ellipsoid reject, so whole-AABB
  materialization is pre-existing and not caused by damage; corrected, and noted that this is
  precisely why terrain can crack at all. (c) **Stage is per-merge-run, so §3.3's continuity claim
  covers the field and not the stage** — recorded, and turned into an explicit **precondition on
  R2's runtime rig** (equal quantized stage on both sides of x = 31/32, asserted via §3.1), without
  which that test fails for a legitimate reason and reads as a UV-seeding bug. (d) **R2's
  `crack.glsl` hash guard demoted to a secondary tripwire** — a content hash reddens on comment edits
  and is repaired by bumping a constant, training the exact reflex it was meant to prevent; the
  primary guard is now a pinned table of ~32 sampled field values. (e) **No legibility measurement at
  gameplay distance** — R3 is near-camera and self-described as optimistic, while R4 measured cost
  only; R4 gains a 4/16/48/96-unit distance ladder with its own written prediction, and P3 may no
  longer be called done on R3 alone.
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

---

## 12. Gate verdicts — verdict of record

Each design-check run's verdict, kept verbatim so a later reader can see what was *decided*
rather than reconstruct it from the change log. §11 records what changed; this records the call.

### Pass 5 — 2026-09-22 — **NEEDS WORK, 5 items** (immediately before P0 began)

> The design (§3.1–3.7) holds for a third consecutive pass. Nothing here requires a redesign, and
> no design key is violated in a way tuning cannot fix.

| # | Item | Where | Severity |
|---|---|---|---|
| **NEW-1** | Damage is lost on **chunk eviction**, not just reload — and no test in the plan can see it, because the rig is a non-evicting flat world | §7 punt, §6 | **Significant** |
| **NEW-2** | §3.7 names `markChunkDirty` without choosing among three re-mesh tiers; thousands of grazed voxels on the immediate tier is a hitch risk | §3.7 | Moderate |
| **NEW-3** | R4's 3/7/15 stage A/B has no mechanism, and needs a forced full re-mesh; if it becomes an API knob, the API rules apply | §3.5, §6.4 | Moderate |
| **NEW-4** | R2's two-chunk rig never asserts the second chunk is resident — same trap pass 3 fixed in §6.3 | §6.2 | Moderate |
| **NEW-5** | An existing pin (`ChunkVoxelAuthorityTest.cpp:97-101`) is unnamed; §9's "no pinned test" is imprecise. Not broken by §3.2 | §9 | Minor |

**Disposition:** all five resolved in this document before any code was written — see §11
(fifth pass). NEW-2 was found to be worse than graded during implementation: `markChunkDirty`
also sets the **DB-dirty** flag, so it would have re-introduced a fixed save-stall regression,
not merely mis-prioritized a rebuild.

### Passes 1–4 — 2026-09-22 — **NEEDS WORK** (5 / 3 / 3 / 5 items)

Verdicts and item lists are in the status block at the top and itemized in §11. Summary of the
arc: **pass 1** hit the design proper (API readback, normalization, world-position seeding,
spall ordering, stage quantization); **pass 2** found the two scope-level defects (sub-voxel
coverage, graze re-mesh); **passes 3–5 found nothing wrong with the design** — every finding was
in the validation plan, the phase order, or a mis-stated cost. That pattern is the useful
signal: the design stabilized after pass 2, and the remaining risk was all in *how it would be
proven*, not in *what it does*.

---

## 13. Build log

Appended as each phase lands. Every entry states what was built, what was measured, and **which
tests are expected to be red** — a phase is not done because a suite is green; it is done when
the phase's gate in §7 is met, and several gates require a specific test to still be failing.

### P0 — API readback + graze re-mesh — **IN PROGRESS** (branch `feature/voxel-damage-cracks`)

Built:
- **`engine/include/core/DamageStage.h` (new)** — `damageStage()` / `damageStageChanged()`, the
  single source of truth for "what stage does this voxel display". The **denominator is a
  parameter**, not baked in, so P1's toughness swap is a one-line change at each call site and the
  P0-interim mismatch is visible rather than silent. The clamp carries its reason at the site:
  a stage ≥ 16 would overflow bits 11-14 into **bit 15, the `varied` texture-rotation flag**.
- **`DamageSystem.cpp`** — graze branch quantizes before/after `addDamage` and calls
  **`markChunkForRemesh`** on a crossing; flush un-gated to
  `voxelsBroken > 0 || voxelsStageChanged > 0`.
- **`DamageSystem.h`** — `DamageResult::voxelsStageChanged`.
- **`ChunkRenderManager.cpp`** — the mesher now calls the shared `damageStage()` instead of its own
  inline `kDamageRef` arithmetic, so the graze path and the shader cannot drift apart.
- **`editor/src/Application.cpp`** — `/api/world/voxel` returns `material`, `damage_tracked`,
  `damage_energy`, `toughness`, `damage01`, `damage_stage`; `apply_damage` echoes `stage_changed`.
- **MCP** — `query_voxel` / `apply_damage` descriptions updated to the new contract.

⚠️ **Shipped behaviour corrected, not merely extended.** `/api/world/voxel`'s `exists` previously
reported a **solid subcube wall as `false`**, because it only tested for a full `Cube`. §3.1 needs
`damage_tracked:false` on subdivided cells, which presupposes such a cell *exists*; `exists` now
uses `hasVoxelAt`. Nothing pinned the old value (only the MCP passthrough consumed it), but this is
a change to a shipped endpoint and is logged as one.

Tests written:
- `tests/core/VoxelDamageStateTest.cpp` (R1a) — accumulation, quantization at boundaries, the
  clamp, the coarser-stage-count path, and the stage-transition predicate.
- `tests/integration/VoxelGrazeRemeshIntegrationTest.cpp` (R5 L2 half) — pure-graze stage change;
  **`getIsDirty()` stays false** (the tier guard, so a future edit cannot silently promote the
  graze back to the DB-persisting tier); plus two controls — a sub-stage graze that must request
  no rebuild, and a proof the same rig *can* break the voxel, so `broken == 0` above means a real
  pure graze rather than a blast that missed.
- Placement follows §6.0, **with one correction to it**: §6.0 filed the stage-transition dirty
  logic under `tests/core/`, but `ChunkManagerTestFixture` derives from `VulkanPhysicsTestFixture`,
  so a `ChunkManager` cannot be built there at all. The pure predicate is unit-tested in
  `tests/core/`; the wiring that consumes it is in `tests/integration/`.

**Expected red at P0, by design (§7 P0 gate):**
`VoxelDamageStateTest.NormalizationIsRelativeToMaterialToughness` — Stone and Glass at the same
fraction of their *own* toughness must display the same stage, and under the global
`kDamageDisplayRef = 30` they do not. **A green here during P0 would mean the test is not measuring
normalization.** It goes green at P1 and not before.

**R1a RESULT — 2026-09-22, Debug, `phyxel_tests.exe --gtest_filter='VoxelDamageStateTest.*'`:**
**7 tests, 6 passed, 1 failed — and the failure is the designed one.**

```
[  FAILED  ] VoxelDamageStateTest.NormalizationIsRelativeToMaterialToughness
G:\Github\phyxel\tests\core\VoxelDamageStateTest.cpp(174): error: Expected equality of these values:
  stoneStage  Which is: '\xF' (15)
  glassStage  Which is: '\t' (9)
Stone and Glass at 50% of their own toughness display different stages (15 vs 9).
```

This is the P0 gate being met, not a broken build. Two things are worth keeping:
1. **The measured saturation points match §3.2's table exactly** — the test reports Stone saturating
   at **27.27%** of the way to breaking and Glass at **85.71%**, against the table's 27.3% / 85.7%.
   The defect §3.2 describes is real, is this size, and is now measured rather than argued.
2. **Stone reads 15 while Glass reads 9 at the same real fragility.** Stone is *pinned at maximum
   damage* while still only half-broken — it has nothing left to say for the remaining 50%, which is
   the concrete form of "the display is silent for three quarters of the range on the most common
   structural material."

The other six cover accumulation, quantization at the boundaries, the bit-15 clamp, the coarser
stage-count path P2 will use, and both directions of the stage-transition predicate.

**R5 (L2 half) RESULT — 2026-09-22, Debug,
`phyxel_integration_tests.exe --gtest_filter='VoxelGrazeRemeshIntegrationTest.*'`.**

⚠️ **The fix was written before the test was run, so the red was demonstrated RETROACTIVELY** —
by reverting §3.7's graze block and flush gate to their pre-P0 form, rebuilding, and running. That
is weaker than writing the test first, and is recorded as what happened rather than dressed up.
`DamageResult::voxelsStageChanged` was left in the header during the revert, so the tests still
compiled and failed on **behaviour**, not on a missing symbol.

**RED (pre-P0 graze restored) — 3 failed, 1 passed:**
```
[  FAILED  ] VoxelGrazeRemeshIntegrationTest.PureGrazeReportsAStageChange
  VoxelGrazeRemeshIntegrationTest.cpp(95): error:
  Expected: (res.voxelsStageChanged) > (0), actual: 0 vs 0
  a graze from pristine to stage 10 changes what is drawn and must request a re-mesh
[  FAILED  ] VoxelGrazeRemeshIntegrationTest.GrazeDoesNotMarkTheChunkForDatabasePersistence
[  FAILED  ] VoxelGrazeRemeshIntegrationTest.SubStageGrazeRequestsNoRebuild
[       OK ] VoxelGrazeRemeshIntegrationTest.ControlTheSameRigCanBreakTheVoxel
```
**GREEN (§3.7 restored) — 4 passed**, with the engine log confirming the shapes:
`applyDamage E=20 r=1 -> broken=0 grazed=1` (a genuine pure graze) and
`applyDamage E=220 r=1 -> broken=1 grazed=0 debris=12` (the control).

**Why this red is trustworthy: the control passed while the three graze tests failed.** Had the rig
been broken — blast missing the voxel, chunk absent, materials unloaded — the control would have
failed too. Its passing localizes all three failures to the graze path specifically, which is the
whole point of §6.5 being a separate test from R1 and R3.

**R1b + R5 (L4 half) RESULT — 2026-09-22, live Debug engine, project `DamageLab`.**

Rig exactly as §6.3/§14.1 specify: Flat DB-only world, one chunk (0,0,0), Stone wall
x ∈ [8,15], y ∈ [17,20], z = 8. `get_terrain_height` returned `surface_y: 16`, confirming the
pass-3 trap is real and the wall clears it. Verified by reading voxels back, not by trusting
`fill_region`'s response.

**R1b — the JSON contract is live.** A pristine rig voxel returns:
```json
{"position":{"x":10,"y":18,"z":8},"exists":true,"material":"Stone",
 "damage_tracked":true,"damage_energy":0.0,"toughness":110.0,
 "damage01":0.0,"damage_stage":0}
```
`toughness` echoes materials.json's Stone break block exactly.

**The damage ladder (§14.1), built with pure grazes and read back through §3.1:**

| world x | energy | damage_energy | damage01 | damage_stage |
|---|---|---|---|---|
| 8 | — | 0.0 | 0.000 | 0 (control) |
| 9 | 6 | 6.0 | 0.055 | 3 |
| 10 | 12 | 12.0 | 0.109 | 6 |
| 11 | 18 | 18.0 | 0.164 | 9 |
| 12 | 24 | 24.0 | 0.218 | 12 |
| 13 | 45 | 45.0 | 0.409 | **15 (max)** |
| 14, 15 | — | 0.0 | 0.000 | 0 (control) |

**§3.2's defect, now measured live rather than argued:** at x = 13 the voxel is **40.9% of the way
to breaking and the display is already pinned at maximum**. The remaining **59% of the damage range
renders no change at all**. That is the "silent for three quarters of the range on the most common
structural material" claim, confirmed on a running engine.

**R5 L4 — pure grazes changed pixels, with nothing breaking.** All **32/32 wall voxels were still
present** afterwards, so no break occurred and no break-triggered `updateDirtyChunks()` could have
re-meshed the chunk as a side effect — which is the precise confound §6.5 exists to rule out. A
final single graze echoed the new counter:
```json
{"broken":0,"grazed":1,"stage_changed":1,"debris":0,"success":true}
```

**Measured per-column luminance** (`tonemap curve:0` per §14.2, 8 columns sampled across the wall,
mean over the central half of each column):

| stage | 0 | 3 | 6 | 9 | 12 | 15 | 0 | 0 |
|---|---|---|---|---|---|---|---|---|
| mean luminance | 88.72 | 81.15 | 79.07 | 73.43 | 70.41 | 66.14 | 86.96 | 89.50 |
| Δ vs control | — | -7.57 | -9.64 | -15.29 | -18.30 | -22.58 | -1.76 | +0.79 |

**Controls hold:** the two pristine columns on the far side read within **±1.8** of the near
pristine control, so the ramp is damage and not a lighting or exposure gradient across the wall.

### P0 visual baseline — what this establishes for P3

This is the "reads as dirty" baseline the crack has to beat, and it is worth stating numerically:
1. **It is a pure brightness ramp with no structure.** Monotonic darkening, nothing else. There is
   no fracture, no direction, nothing that suggests *how* the voxel will fail.
2. **The steps are very subtle.** Consecutive sampled columns are 3 stages apart and differ by only
   **2.08–5.65** luminance out of 255 — roughly **0.7–1.9 per single stage**, under 1%. A player
   cannot rank two adjacent stages by eye. This is strong independent support for §3.5's
   quantization decision: at 3 visible stages each step becomes ~7.5 units instead of ~1.5, which is
   the difference between legible and invisible. **Feed this into R4 rather than re-deriving it.**
3. **§3.7 works.** Before P0 this ladder would have rendered as pristine stone until something else
   dirtied the chunk; the pixels above are the fix, observed.

**Human sign-off (§14):** NOT YET TAKEN. Per §14.5, P0 is a 5-minute baseline look rather than a
review gate — there is no crack to judge. The rig is left standing in `DamageLab` for that look.

### P0 regression sweep

**Full unit suite: 3,967 tests, 2 failures, 3,765 s (≈63 min, Debug).** Both failures accounted for:

1. `VoxelDamageStateTest.NormalizationIsRelativeToMaterialToughness` — **the intentional P0 red**
   (§7 P0 gate). Goes green at P1.
2. `FineFaceMerge.SubcubeMerge_CrossCubeSplitsOnLightBoundaryBetweenCubes` — **PRE-EXISTING, not
   caused by this work. Verified empirically, not argued:** `ChunkRenderManager.cpp` was reverted to
   its committed state, rebuilt, and the test re-run — it fails **identically**
   (`FineFaceMergeTest.cpp:677`, `topSubFaces() 1 vs 2`, "cross-cube must split +Y at the light
   boundary"). The P0 mesher edit was then restored. Logged here so the next person does not
   re-bisect it; it is **not** this feature's to fix, and belongs to the subcube light-boundary
   split path, which P4 does not touch.

**Integration:** `VoxelGrazeRemeshIntegrationTest` 4/4; neighbouring destruction suites
(`ChopKerf*`, `CoherentCollapse*`, `TreeCollapse*`) **34/34**, no regressions.

**Note on suite cost:** the full Debug unit suite takes about an hour. Run targeted filters while
iterating and reserve the full sweep for phase boundaries; an unobserved hour-long run is also very
easy to mistake for a hang (it was, once, during P0).


### P1 — toughness normalization — **COMPLETE**

Built:
- **`DamageSystem::responseFor` is now `static`.** It only ever read `MaterialRegistry`, never
  instance state, so the mesher and the API can resolve toughness without owning a `DamageSystem`.
  Existing `ds->responseFor(x)` call sites still compile.
- **`DamageSystem::displayStage(material, damage, stageMax)` — THE entry point** for "what stage
  does this voxel display". The mesher, `/api/world/voxel` and the tests all resolve through it, so
  a stage cannot mean one thing in the shader and another in a test.
- **`ChunkRenderManager`**: `MatFace` gained `toughness`, resolved **once per material** (a rebuild
  visits up to 32,768 cells but only a handful of distinct materials), and the per-voxel quantization
  now divides by it instead of the global `kDamageDisplayRef`.
- **Graze path** uses `mr.toughness` — the same denominator — so the boundary that triggers a
  re-mesh is exactly the boundary the shader renders.

**Test discipline note, because it nearly went wrong.** The P0 red asserted on
`damageStage(damage, kDamageDisplayRef)` — a hand-picked denominator. Simply swapping the constant
in the test would have turned it green **while testing nothing but its own arithmetic**. The P1
version asserts through `DamageSystem::displayStage`, which is what the engine actually calls, across
four fractions (25/50/75/100%). Two tests were added alongside:
- `DisplayStageAgreesWithTheEchoedDenominator` — pins §3.1's promise that echoing `toughness` lets a
  caller reproduce the rendered stage, across five materials × six damage fractions.
- `UntouchedMaterialsStillFallBackToADerivedToughness` — §3.2 makes the `bondStrength * 120`
  fallback *visible* for the 101 materials without a break block but does **not** fix it (pre-existing
  open question #5). What must hold regardless is that every material — including an unknown one —
  resolves to a **positive** denominator, or the display divides by zero and reads pristine forever.

The integration test's rig assumption had the same latent flaw and was rewritten the same way: it
said `damageStage(20.0f, kDamageDisplayRef)`, which would still have **passed** after P1 while
silently testing a constant the engine no longer uses.

**RESULT — unit 9/9, integration 4/4.** The §7 P1 gate is met: R1's Stone/Glass variant went
**red → green**.

**L4, live, against a prediction written before the run** (`tools/damage_ladder_rig.py --measure`):

| world x | damage01 | predicted stage | actual | P0 stage (ref 30) |
|---|---|---|---|---|
| 8 | 0.000 | 0 | **0** | 0 |
| 9 | 0.200 | 3 | **3** | 11 |
| 10 | 0.400 | 6 | **6** | **15** |
| 11 | 0.600 | 9 | **9** | **15** |
| 12 | 0.800 | 12 | **12** | **15** |
| 13 | 0.950 | 14 | **14** | **15** |
| 14, 15 | 0.000 | 0 | **0** | 0 |

8/8 match. **The right-hand column is the defect in one view:** under the global reference,
everything from 27% damage onward rendered *identically* — four of these rungs were the same pixel
value. The display now spends its whole range on the whole damage range.

**Measured luminance (curve 0, same pose):** 83.02 / 75.37 / 73.84 / 68.23 / 65.60 / 63.39 across
stages 0→3→6→9→12→14, with the far pristine controls at 80.96 and 83.59 (within **±2.1** of the
near control). **Do not misread this as the P1 win:** the per-stage slope is ~1.40 lum/stage vs P0's
~1.51, i.e. essentially unchanged, because the shader's stage→darkness mapping was not touched. P1
changed *which damage values reach which stages*, not what a stage looks like. The slope being flat
is itself the argument for P2: **~1.4 luminance out of 255 per stage is below what anyone can rank
by eye.**

### The review rig is now one command

`tools/damage_ladder_rig.py` (§14.1). Builds terrain, wall, ladder, camera and tonemap; reads each
material's toughness **from the running engine** rather than hardcoding it, so the ladder stays
correct if a break block is retuned; and **verifies the world rather than the fill response**.

⚠️ **It found a real rig bug on its first run, which is the argument for having written it.**
Damage lives on the voxel and `fill` does **not** reset it, so re-running the rig over an existing
wall ADDED each column's energy to what it already carried: the 0.60 / 0.80 / 0.95 columns summed
past toughness and **broke**, silently converting a pure-graze rig into a demolition. The script's
own integrity check (`8/8 columns intact`) caught it; a screenshot would not have. The rig now
clears the region first, through the JobSystem so the engine keeps rendering.


### P2 — three visible stages — **COMPLETE**

**The packing decision, which is the whole of why this phase touched no shader.** The obvious
implementation drops the emitted range to 0..3 — but `voxel.frag:283` divides the packed value by
`15.0`, so that would have required editing the fragment shader, which means rebuilding **every**
shader and committing the `.spv` (glslc does not track `#include` deps), and leaving a bare `3.0` in
GLSL to be kept in sync with a C++ constant **by hand**. Instead the field stays 4 bits and the 4
stages are spread across **{0, 5, 10, 15}**, giving the shader 0.0 / 0.33 / 0.67 / 1.0 exactly as
before. Same merge-fragmentation win, same re-mesh reduction, same perceptual spacing, **no shader
change, no `.spv` churn, and no C++/GLSL constant that can drift.** What matters for both cost and
legibility is the NUMBER OF DISTINCT VALUES, not their magnitude.

`damage_stage` is now 0..3 — which is what §3.1 specified all along — and the response gained
`damage_stages_max` and `damage_stage_bits` (the packed value `voxel.frag` samples).

**RESULT — unit 13/13, integration 4/4.** Beyond the §7 gate (boundaries 0/1/2/3), two tests assert
the *reasons* for the change rather than its mechanics:
- `OnlyFourDistinctPackedValuesAcrossTheWholeDamageRange` sweeps 1,001 damage values and requires
  the continuous gradient to collapse to **exactly 4** distinct packed values. That is the
  merge-cost property stated as a test instead of trusted — every distinct value is a potential
  merge-run break.
- `CoarseStagesBoundTheRemeshCountPerVoxel` walks a voxel pristine → break and pins that it can
  cause at most **3** rebuilds, where 15 stages meant 15.

**L4, live** (`tools/damage_ladder_rig.py --measure`, same pose as P0/P1):

| stage | 0 | 1 | 2 | 3 |
|---|---|---|---|---|
| mean luminance | 88.14 (n=3) | 79.24 (n=2) | 72.39 (n=2) | 66.07 (n=1) |

| step | 0→1 | 1→2 | 2→3 |
|---|---|---|---|
| Δ luminance | **8.90** | **6.85** | **6.32** |

**The measurement that settles it is the noise floor, and it was not visible from the design
argument.** Columns carrying the SAME stage differ by ~1.6–3.2 luminance — ordinary lighting and
texture variation across the wall. At 15 stages the per-stage step was **~1.40, i.e. BELOW that
noise**: a single stage transition was literally indistinguishable from the wall's own variation, so
no amount of squinting could have ranked two adjacent stages. P2's steps sit **2–4× above** the noise
floor. Pristine controls remain within ±1.2 of each other.

**This does not ratify 3 as the final answer** — it rules out 15. R4 (§6.4) still measures 3 vs 7 vs
15 for cost AND legibility in a real settlement scene across the distance ladder, and may land on 7.
`kDamageStagesVisible` carries that caveat in its own doc comment so nobody treats it as settled.

**Two stale-test lessons, logged because it is now a PATTERN rather than an incident.** Both P1 and
P2 broke a test that had hardcoded a constant meaning "the maximum":
- At P1 it was the **denominator** (`kDamageDisplayRef`) — caught before it could pass wrongly.
- At P2 it was the **stage count**: tests said `kDamageStageMax` (15, the 4-bit FIELD WIDTH) where
  they meant *the top visible stage*. Those were the same number until P2 and silently stopped
  being. Both now reference `kDamageStagesVisible`, the semantic constant, with the reason in a
  comment at the assertion.

That second failure earned its keep: it **proved that echoing `toughness` alone is no longer enough**
for a caller to reproduce the rendered stage — they need the stage count too. That is exactly why
this phase added `damage_stages_max` to the response, and the test now asserts against both echoed
numbers plus the packed bits, so an external caller can reproduce the engine's result without
reading engine source (§3.1's stated contract).


### P3 — the crack shader — **BUILT, awaiting §14 sign-off**

`shaders/crack.glsl` (new) + `voxel.frag`, committed `.spv`, `LightingPipeline.md` receiver row and
change log updated, `lighting_doc_check.py --update` stamped.

**The model.** Voronoi edge distance (F2−F1) over a world-space lattice: ~0 exactly on a cell
boundary and growing toward interiors, so small values trace a CONNECTED NETWORK of cell walls — a
fracture, not a scatter of blobs. Stage WIDENS that field rather than swapping it, so a voxel
advancing 1→2→3 shows the same cracks growing (pinned by
`StageWidensTheSameNetworkRatherThanSwappingIt`). A second octave is admitted from stage ~0.45 so
the top stage reads as shattering rather than as one wider line. Seeded from `worldPosAbs`, never
`texCoord`/`sizeU`/`sizeV` (§3.3).

**§4.5 realized:** the darkening moved from the whole face to CRACK PIXELS (×0.18), with a small
whole-face wear term retained. The old flat `mix(1.0, 0.55, dmg)` is exactly why damage read as
grime — a uniformly dimmer stone face is a dirty stone face.

#### The legibility failure, and the measurement that caught it

The first P3 build put the fracture lattice on the MICROCUBE grid (1/9 m) to align with V1.5's spall
chips (§3.4). At 4 units it looked excellent. **At 16 units it measured WORSE than the flat
darkening it replaced:**

| step | P2 (flat) | P3 first build | P3 shipped |
|---|---|---|---|
| 0→1 | 8.90 | 4.55 | **5.69** |
| 1→2 | 6.85 | **2.08** | **3.78** |
| 2→3 | 6.32 | **2.02** | **3.83** |

Within-stage noise floor: **2.91**. The first build's upper two steps were INSIDE the noise — a 1 m
face carries a 9×9 network, ~5 px per cell at 16 units with crack width a fraction of that, so the
crack was sub-pixel and only the (deliberately weakened) whole-face term remained.

**This is exactly the risk §6.3 wrote down in advance** — *"the rig is optimistic about visibility"*
— and the cell size was the thing the optimistic rig was hiding. R3 alone would have passed it.

**Fix, without trading away §3.4:** the PRIMARY network moved to the subcube lattice (1/3 m, 3×3 per
face, ~15 px/cell at 16 units), and the second octave now lands exactly on the microcube lattice
(×3.0) — so V1.5's chips still fall where fine cracks already are. Whole-face wear 0.88 → 0.78 so
damage stays legible once cracks do go sub-pixel. **Prediction written before the run: every step
≥ 3.5 lum. MET** (5.69 / 3.78 / 3.83), all clear of the noise floor.

**Honest reading of that table:** P3's steps are SMALLER than P2's. That is the intended trade —
P2's larger separation came from crude whole-face dimming that read as dirt. P3 keeps every step
above the noise floor while adding actual fracture structure. Whether that trade is right is a
question for §14, not for a luminance table.

#### R2 — seam invariant (18/18 green)

`tests/core/VoxelCrackSeamTest.cpp`: partition-independence across x = 31/32, continuity across the
seam, stage-monotonicity, and a CPU mirror of the GLSL.

**The drift guard is a pinned SAMPLE TABLE, not a file hash** — §6.2 proposed a content hash and it
was rejected in implementation: a hash reddens on a comment edit, its only repair is bumping a
constant, and it therefore trains the exact reflex it was meant to prevent. The secondary tripwire
instead asserts that crack.glsl still contains the specific CONSTANTS the mirror depends on
(`kCrackCell`, the width ramp, the ×3.0 octave), which ignores comments and names what moved.

Two defects the guard caught on its own first outing, both worth keeping:
1. **A table of zeros.** The first generated table was almost entirely 0.0 — cracks are thin lines,
   so uniform sampling lands in cell interiors and misses them. A guard made of zeros stays green
   through almost any change to the crack. Now stratified across 0.00/0.25/0.50/0.75/0.98 at every
   stage plus both style extremes, with a meta-assertion that ≥ 10 samples sit ON cracks.
2. **Rounded sample coordinates.** Positions printed at `%.4f` did not reproduce: the field runs at
   ~230 per world unit near an edge, so a 1.9e-6 position shift moved the value by 4.3e-4 and the
   table failed against the very code that generated it. Fixed by raising generator precision to
   `%.7f` — NOT by loosening the tolerance, which would have weakened the guard.

#### Still open for P3

- **§6.2's RUNTIME half** (damaged wall straddling x=31/32, captured and diffed) is NOT run. The
  unit half cannot catch the real mistake — a shader that reads `sizeU` — and the CPU mirror is
  near-tautological about it. Requires the two-chunk rig with the residency precondition (§6.2).
- **§6.2's RUNTIME half** remains the one open P3 item (see above).

#### §14 VISUAL REVIEW — **SIGNED OFF**, 2026-09-22

Reviewed live in `DamageLab` at inspection range (≈4 units) and at the 16-unit pose, under
**shipped tonemapping** (the verdict view, §14.2), on a Stone ladder with pristine controls in
frame. Reviewer verdict: *"visually I think the cracks look good."*

**What that verdict covers, stated so it is not over-read later:**
- ✔ **Q1 — cracked, not dirty.** The motivating question, and the one the review actually turned
  on. A damaged face now carries a connected fracture network beside an untouched control face.
- ✔ **Q4 — voxel aesthetic, not a decal.** The field flows across the surface rather than reading
  as a stamp.
- ✔ **Q2 / Q3 — ranking and growth**, at close and mid range, supported by the measured ladder.
- 〇 **Q5 — legibility at 48 and 96 units: NOT reviewed.** Only 4 and 16 units were looked at. The
  distance ladder is R4's job (§6.4) and remains outstanding.
- 〇 **Q6 — material character: NOT applicable yet.** `crackStyle` is P5; every material currently
  renders at style 1.0.

**Recorded note, not a blocker.** At inspection range the Voronoi cells read as somewhat REGULAR
and polygonal — closer to a cracked glaze than to stone fissuring. The reviewer accepted the look
as-is; this is logged as the natural target for P5's per-material `crackStyle`, which drives
density and width from `brittleS1`/`brittleS2` and is where character comes from.

---

## 14. Manual visual review — human sign-off

**Why this exists as its own gate.** §6 measures pixels; it cannot judge them. R3 asserts "≥ 12% of
the footprint shifted by > 8/255 luminance" — a crack, a smear and a bruise all satisfy that. The
feature's stated purpose is that a damaged voxel **reads** as cracked and **tells you** how close it
is to breaking, and reading is a human act. So this is a gate, not a courtesy: **P3 and P5 are not
done until it is signed off**, and a green R3 does not substitute for it.

It also runs the other way. The design-keys gate asks whether a feature "matches the voxel
aesthetic" — that question has no automated test anywhere in this plan, and cannot have one.

### 14.1 The review rig — the damage ladder

One command, identical every time, so a review months apart is comparable.

- **World:** Flat, small, DB-only (never a streaming world — damage does not survive eviction, §7).
- **Subject:** a full-cube **Stone** wall at x ∈ [8,15], y ∈ [17,20], z = 8 — wholly inside chunk
  (0,0,0), and **above y = 16** because a Flat world's surface is sea level and anything below it is
  buried (the pass-3 trap, §6.3).
- **The ladder:** each column of the wall pre-damaged to a different stage, **0 at the left**, rising
  to max at the right, with the **pristine column left in frame as the control (C1)**. The whole
  progression is visible in one screenshot — which is the only way to judge whether the stages are
  *distinguishable from each other*, as opposed to merely different from pristine.
- **Full cubes, never a generated building** (§3.6) — a structure's sub-cube walls cannot show damage
  in V1, so a null result there would look like a broken shader.

### 14.2 Capture protocol — BOTH looks, and this is the part that is easy to get wrong

Automated tests capture with `POST /api/debug/tonemap {"curve":0}` so greys are readable. **A human
review must not stop there.** Neutral tonemap is a measuring instrument; it is not what the game
looks like. Shipping a crack that reads perfectly at curve 0 and vanishes under exposure ×8 + AgX
would pass every test in §6 and be worthless.

So capture each pose **twice**:
1. **`tonemap curve:0`** — the measurement view, matching R3/R4.
2. **Shipped tonemapping and default lighting** — the verdict view. **This one decides.**

Poses, per stage-ladder shot: **4 units** (inspection), **16** (combat), **48** (mid), **96** (far) —
the same ladder R4 measures, so the human verdict and the legibility numbers are directly comparable
rather than two unrelated opinions. Add one **low sun** shot: cracks are self-shadowing (§4.5) and
raking light is where that either reads or does not.

### 14.3 The questions being asked

Vague review produces vague results, so the reviewer is answering these, not "does it look good":

1. **Cracked, or dirty?** The shipped look darkens the whole face and reads as grime. Does this read
   as *fracture*? (This is the entire motivation, §1.)
2. **Can you rank two voxels by eye?** Put two different stages side by side: can you tell which is
   closer to breaking without being told? If not, the stage count is wrong — that is R4's decision,
   and this is the human half of it.
3. **Does it grow, or does it swap?** Advancing 1→2→3 should look like *the same cracks widening*,
   not three unrelated patterns (§4.3).
4. **Voxel aesthetic, or decal?** Does the fracture belong to the surface, or look stamped on it?
   Does it flow across voxel boundaries as one surface (§3.3)?
5. **At 48 and 96 units — legible, or speckle?** Does it resolve, or shimmer/sparkle? (The known
   character/grass sub-pixel speckle is the failure mode to watch for.)
6. **P5 only — does material read?** Glass dense-and-fine vs Steel sparse-and-wide vs Stone between,
   without being told which is which.

### 14.4 Recording the verdict

The reviewer's call goes in §13 with the date, as one of: **SIGNED OFF** · **SIGNED OFF WITH NOTES**
(notes become work items) · **REJECTED** (with what specifically failed). A rejection is a normal
outcome of a visual gate and is not a defect in the build — it is the gate doing its job.

### 14.5 Slate — when each review happens

| Phase | Reviewable? | What is on screen |
|---|---|---|
| **P0** | **Barely — worth 5 minutes, not more** | No crack exists yet. The only visible change is that **a pure graze now appears at all**: before §3.7 a weak hit recorded damage and the surface did not update until something else dirtied the chunk. So the honest P0 demo is a before/after on a pure graze, showing the (crude, dirt-like) darkening appear *promptly*. That same shot doubles as evidence for **why P3 is needed** — it is the "reads as dirty" baseline. |
| **P1** | No | Normalization changes *which* stage shows, not what a stage looks like. Covered by R1's Stone/Glass red going green. |
| **P2** | Optional | Stage count drops 15 → 3. Nothing new to look at, but the ladder gets coarser; worth a glance alongside R4. |
| **P3** | **YES — the main review** | The crack shader. Full §14.1–14.3 protocol. **Gate.** |
| **P4** | Alongside R4 | The human half of the stage-count decision (question 2 and 5 above), taken at the same poses R4 measures. |
| **P5** | **YES** | Per-material style. **Gate**, question 6. |
| **V2 / V1.5** | YES | First time cracks appear on a building; first time the silhouette breaks. |

**Consequence for scheduling:** the first review worth the reviewer's time is **P3**. P0's is a
5-minute before/after, and is best treated as *establishing the baseline the crack has to beat*
rather than as a review of anything new.

---

## 15. P0.5 — costing V2 sub-voxel damage storage (paper only)

Required by §7 **before P3 writes the shader**, so the crack model is authored knowing whether it
generalizes to sub-voxel cells or gets rewritten for them. No code. Answers §3.6's two open questions.

### 15.1 The premise this plan was carrying was WRONG

§3.6 argued V2's memory risk like this:

> *"a subcube grid is 27× the cell count and a microcube grid 729×. A naive float-per-cell is not
> obviously affordable and needs its own design plus measurement."*

**That assumed DENSE sub-voxel grids. This engine does not have them.** Sub-voxels are stored
sparsely, one heap object per cell that actually exists:

```cpp
std::vector<std::unique_ptr<Subcube>>   staticSubcubes;    // Chunk.h:76
std::vector<std::unique_ptr<Microcube>> staticMicrocubes;  // Chunk.h:77
```

Only cells that were actually subdivided allocate anything. A chunk of undisturbed terrain holds
**zero** subcubes and **zero** microcubes. So "27× / 729×" describes the *potential* cell count of a
fully-subdivided chunk — a configuration that never occurs — not the cost of anything real.

The honest cost of per-cell damage is therefore **4 bytes per sub-voxel that already exists**, and
the question is not "can we afford a 729× grid" but "what does adding a float to a 120-byte object
cost".

### 15.2 Measured sizes

Compiled against the real headers (MSVC 19.33, x64, `/std:c++17`):

| type | sizeof | + `unique_ptr` slot | notes |
|---|---|---|---|
| `Cube` | **176 B** | +8 | already carries `accumulatedDamage` |
| `Subcube` | **120 B** | +8 | no damage field |
| `Microcube` | **128 B** | +8 | no damage field |

Both sub-voxel types are 8-byte aligned and sized to a multiple of 8, so **adding a `float` grows
each by 8 bytes, not 4** (4 bytes of data + 4 of padding). That is the real per-cell price:

- `Subcube` 120 → 128 B = **+6.7%**
- `Microcube` 128 → 136 B = **+6.3%**

...of sub-voxel memory only, which is itself a small fraction of a chunk (≈ 1.00 MB/chunk budget,
`docs/AgentContext.md`), and **zero** in terrain that was never subdivided.

### 15.3 The two options, costed

| | **A. Per-cell field** | **B. Per-parent-cube aggregate** |
|---|---|---|
| Storage | +8 B per existing sub-voxel | +8 B per subdivided parent cube |
| Ratio | — | A costs 9× B for a 1-subcube-thick wall (9 of 27 cells occupied), up to 27× / 729× for fully-packed cells |
| Fidelity | a single micro chip can crack alone | the whole subdivided cell cracks as one |
| Merge cost | **worse** — per-cell damage splits sub-voxel merge runs the way §3.5 describes for cubes | **better** — one value per parent, so a wall face stays one run |
| Fits V1's model? | no — V1's stage is per-cube | **yes** — identical granularity to V1 |
| Damage accumulation | needs a per-cell `addDamage` path | reuses the existing cube-level path unchanged |

**Recommendation: B, the per-parent-cube aggregate.** Not primarily on memory — A is affordable now
that the 729× premise is gone — but because:

1. **It matches what the feature actually renders.** A crack is a surface treatment on a wall face.
   Wall faces are *built from* sub-voxels but are *read* as one surface; the player is judging "how
   damaged is this wall", not "how damaged is this 11 cm chip".
2. **It keeps one damage model, not two.** V1's stage is per-cube. B keeps exactly that granularity
   and changes only *which faces can display it*, so §3.2's normalization, §3.5's quantization and
   §3.7's re-mesh rule all carry over untouched. A forks the model in two.
3. **Merge cost points the same way.** Per-cell damage would fragment sub-voxel merge runs, and
   sub-voxel faces are where face counts are already highest.
4. **A stays reachable.** B is a strict subset of A's fidelity; if V1.5's geometric spall later needs
   per-chip state, it can be added then, informed by a shipped crack rather than speculatively.

### 15.4 §3.6's second question — damage on subdivision

**Inherit the parent's `damage01` into the subdivided cell; do not reset.**

Under B this is nearly free: the aggregate simply keeps the value the `Cube` already held. Resetting
would mean a damaged wall becomes *visually pristine* the instant something subdivides it — which is
the current V1 behaviour (§3.6 notes damage is silently lost on subdivision) and is only tolerable
in V1 because every V1 subdivision path is itself destructive.

Note this also removes a V1.5 blocker recorded in §3.4: spalling replaces a cube with sub-voxels, so
under "reset" a voxel would crack, spall once, then go pristine. Under B + inherit, it stays cracked.

### 15.5 What this means for P3 — the reason P0.5 ran first

**P3's crack model needs no change to generalize.** Under recommendation B, a sub-voxel face
displays *its parent cell's* stage, which is the same 0..3 value `voxel.frag` already samples from
instance bits 11-14. So V2 is a **plumbing** job — write `dmgBits` into the two sub-voxel instance
paths (`ChunkRenderManager.cpp:1062-1064`, `:1222-1224`) and give the aggregate a home — not a
redesign of the crack.

Concretely, P3 may proceed on these assumptions, and they are now costed rather than hoped:
- the crack function's inputs stay `(worldPosAbs, faceNormal, damageStage, crackStyle)`;
- `damageStage` stays 0..`kDamageStagesVisible`, cube-granularity, on every surface;
- §3.3's world-position seeding is what makes this work — because the field is seeded from world
  position and **not** from cell size, a 1/3-scale face samples the same continuous fracture field as
  a full cube, so cracks will flow across a wall built of mixed cube and sub-cube geometry without
  any per-scale special-casing.

**Still NOT decided here** (V2's own work, not P0.5's): where the aggregate physically lives — on the
parent `Cube` (simplest, but a subdivided cell's `Cube` may not be materialized) versus a per-chunk
side table. That is an implementation choice with no bearing on P3, which is why it is deferred
rather than guessed.

---

## 16. Open work — the one list

Everything still outstanding, in recommended order. §7 remains the authority on phase gates; this
section exists because the open items were spread across §6.2, §7, §13 and an engine-gap log, and
"what is next" should be answerable from one place.

**Gate pass 2 (2026-09-22) found 7 items, including 2 BLOCKERS — all resolved in place.** A blocker
in a plan is a decision not yet made, so both were decided rather than logged: P5's data path
(§4.4a) and P4's runtime knob (§6.4). Verdict of record: §17.

| # | Item | Where specified | Status | Blocks |
|---|---|---|---|---|
| ~~1~~ | ~~`build_shaders.bat` reports success on a FAILED shader compile~~ | `StructurePipelineGaps.md` 2026-09-22 | ✅ **FIXED 2026-09-22** — three nested cmd traps, shipped as `\|\| goto :shader_error`; regression test `tools/test_shader_build_fails_loudly.py` | — |
| **2** | **§6.2 RUNTIME seam test** — damaged wall straddling x = 31/32, captured and diffed | §6.2; rig built as `tools/crack_seam_test.py` | ⚠️ **NOT ACHIEVED after SIX metric designs.** The rig, both preconditions and the two-rig A/B framing all work and are committed; **no pixel statistic tried can distinguish a world-seeded crack from a uv-seeded one.** A PASS proves nothing. All six attempts and the reason each failed are in the tool. **Recommended next step is not another statistic — it is a debug view that renders `crackField` directly (§16.1)** | **P3 closure — still open** |
| **3** | **P4 — stage-count A/B**, 3 / 7 / 15 for cost AND legibility across the 4/16/48/96 ladder | §6.4 — knob storage resolved, cost prediction added, pinned tests named | **READY** | Ratifying or revising P2's choice of 3 |
| **4** | **P5 — per-material `crackStyle`** from `brittleS1`/`brittleS2` | §4.4 + **§4.4a data path, §4.4b mapping**, red test named | **READY** | — |
| 5 | **V2 — sub-voxel damage** (cracks on generated buildings) | §3.6, §15 | OPEN | Retiring §1's scope boundary; also wanted by `FractureModes.md` F1 |
| 6 | **V1.5 — geometric spall** | §3.4 | OPEN | Depends on V2 |

### Why this order

**1 first, even though it is not part of this feature.** It is cheap, it protects everything, and
it was found *by* this work — leaving it open means the next person to edit a shader can ship a
stale `.spv` with a green CI. It is the only item here whose blast radius is the whole repo.

**2 next, because P3 is not honestly closed without it.** The unit half of §6.2 is near-tautological
about the failure that actually matters — a shader that reads `sizeU` — and the CPU mirror cannot
catch it. Only a captured, diffed seam can. §14 is signed off; this is the last P3 item.

**3 and 4 are both "make it better", and they answer different questions.** P4 is the measurement
that could still overturn P2 (`kDamageStagesVisible` carries that caveat in its own doc comment, so
it is not treated as settled), and it is where Q5 — legibility at 48 and 96 units, explicitly NOT
covered by the §14 sign-off — finally gets answered. P5 is the one most likely to improve how it
LOOKS rather than how correct it is, and it directly targets the reviewer's recorded note that the
Voronoi cells read as regular and polygonal.

**5 and 6 are the larger arc.** V2 is now wanted by two independent tracks — this plan reached it
from rendering (cracks cannot appear on sub-cube building walls) and `FractureModes.md` reached it
from physics (a carved cube goes visually pristine). That convergence is the strongest argument yet
for scheduling it sooner than "after P5".

---

## 17. Gate verdict — pass 2 (the remaining work)

### 2026-09-22 — **NEEDS WORK, 7 items, 2 blockers** — all resolved in place

Run against §16 items 1-4, i.e. the next things to be built rather than the whole document.

> No design key is violated; nothing needs redesign. But two items are **not buildable as written**.

| # | Item | Severity | Resolved in |
|---|---|---|---|
| 1 | **P5 has no data path for `crackStyle`** — instance `reserved` is 16/16 bits allocated and the material-props `vec4` is 4/4 used | **Blocker** | §4.4a — props array widened to a stride of two `vec4`s |
| 2 | **P4's knob cannot set a `constexpr`**, and making it mutable adds a cross-thread read during worker-thread meshing | **Blocker** | §6.4 — `std::atomic<int>`, read ONCE per rebuild into a local |
| 3 | §6.2's runtime seam test names no control | Moderate | §6.2 — mid-chunk boundary in the same capture |
| 4 | §6.2 is green-on-arrival but framed as red-before-green | Moderate | §6.2 — deliberate `texCoord` break, recorded as retroactive |
| 5 | P5's `brittleS1` → `style` mapping unspecified, no red test | Moderate | §4.4b — bounded mapping + `StyleChangesCrackDensityMeasurably` |
| 6 | P4 has only a directional cost prediction, which cannot fail | Moderate | §6.4 — ≥ 2× on the damaged region, with a stated falsifier |
| 7 | P4 doesn't name the pinned tests asserting `kDamageStagesVisible` | Minor | §6.4 — three named |

**Plus a correction to this session's own gap log** (not a finding against the plan): the
`build_shaders.bat` fix is **one** change, not two. Recording the manifest only on a fully
successful run restores `--check` for free — a failed build leaves the old manifest against new
sources, which reddens by itself. The proposed `--verify-fresh` mode was unnecessary.

**The two blockers were worth the gate on their own**, because both were invisible from the plan
and only became apparent by reading the code: the instance word is *exactly* full, and the props
`vec4` is *exactly* full, so P5 — written as a small per-material follow-on — had nowhere to put
its one float. And `kDamageStagesVisible` is `constexpr`, so P4's endpoint could not have been
implemented as specified at all.

**Discipline note carried forward from the resolutions:** both §4.4b's style range and §3.5's stage
count are now explicitly **hypotheses to be measured, not settled numbers**, and both say so where
the value lives. §4.4b additionally records that the style floor is load-bearing — dropping it
would reintroduce the sub-pixel legibility failure P3 measured and fixed.

### 16.1 Why §6.2's runtime half is stuck, and the way out

Six metric designs were tried against a deliberately uv-seeded shader. None detected it. The
failures are not six accidents; they are two facts about the observable:

**Fact 1 — a pattern RESTART does not change brightness.** Both sides of a uv seam carry the same
statistical density of cracks; only the alignment differs. So every LEVEL-based statistic is blind
to it by construction: mean luminance, luminance step at the boundary, column-to-column contrast,
normalized contrast, and 2D dark-tail depth all measure *how bright/dark*, and that is exactly the
quantity the defect preserves.

**Fact 2 — the stone albedo swamps STRUCTURE at pixel scale.** The natural rock texture is
high-frequency noise, so the profile-correlation metric read ≈ 0 correlation at ORDINARY voxel
boundaries (mean 1.002 of a possible 1.0 discontinuity) — there is no headroom left for a seam to
stand out in. Differencing against a pristine capture of the same wall was the right instinct and
still failed, because the difference image is dominated by the uniform whole-face wear term, whose
near-constant value makes the correlation ill-conditioned.

**The way out is to stop inferring the field from a shaded frame and render the field itself.**
A debug view that outputs `crackField()` directly as greyscale — no albedo, no lighting, no wear
term — makes a uv seam a hard vertical edge in an otherwise smooth image, detectable by the very
first metric that was tried. The engine already has the shape for this (`Ctrl+F4` debug modes,
`/api/debug/*`), and such a view is independently useful for P5's style work.

**Cost of being wrong about this:** low. §3.3's hard rule is stated at the top of `crack.glsl`, the
unit half of R2 pins the CPU mirror, and code review sees the one line that would break it. What is
missing is an automated guard, not the correctness itself.

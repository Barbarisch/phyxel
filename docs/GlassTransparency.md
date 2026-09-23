# Glass is not transparent — investigation & fix plan

**Status:** OPEN. **Phase 0 COMPLETE** — glass measured fully opaque; the OIT pass runs.
**Phase 1 COMPLETE** — identical at the pre-branch baseline: **the break predates the crack branch.**
**Next: Phase 1b — gated bisect of `main` (§12.8), reviewer's decision 2026-09-23.** Results in §12. Gated through `FeatureDesignKeys.md` three
times (§9).

> **PROCESS RULE (added 2026-09-23, after it was broken).** This plan is the approved plan. When
> execution finds a defect IN the plan — a rig that cannot work, a step in the wrong order — execution
> **stops**, the plan is amended here first, and only then does work continue. Phase 0 broke this
> three ways (§12.3): the rig was changed in the tool without amending §2/§7, a Phase 4 code fix was
> committed inside a phase that says "NO code changes", and a fix was then proposed while Phase 1 was
> still the next step. A plan that is silently worked around is not a plan.

**Symptom:** glass does not render see-through in the editor. Reported by the reviewer 2026-09-22
while reviewing the damage-crack work, and again 2026-09-23 after a second wrong fix.

> ⚠️ **STANDING INSTRUCTION: do not report this fixed without the reviewer confirming it visually.**
> It has been reported fixed twice and was wrong both times. The second was worse than the first:
> a milky pane with the horizon faintly visible through it was read as "transparent". A reading of
> the pixels is not a reading of whether it looks right, and on this defect specifically the
> author's judgement has been demonstrated unreliable.

---

## 1. What is RULED OUT

**The crack rendering is not the cause.** Two fixes were attempted (`58b00dfd`, `03e68fa9`) and both
touched *only* the crack block in `voxel.frag` — softening crack darkening on transparent materials,
then excluding transparent materials from cracking altogether. Neither could have caused a
transparency bug and neither fixed one. Both are reverted (`2ee675d6`).

**Cracks on glass are not the problem and must not be removed again.** The reviewer confirms they
looked good. The exclusion added in `03e68fa9` deleted a working feature for no reason. Any fix that
special-cases transparent materials inside `voxel.frag` risks repeating exactly this.

---

## 2. The measurement — this is the spine of the plan

The two failed fixes share one root cause: **code was changed before "broken" was defined, and the
result was judged by eye.** So the first deliverable is not a fix. It is a number with a control.

**The question is not "does it look see-through".** It is: **does what is behind the pane change
what you see through it?**

Three captures, one camera pose, one variable:

| | scene | if glass works |
|---|---|---|
| **A** | same backdrops, **no pane** (control) | the backdrop, full strength |
| **B** | **`Bricks`** backdrop + glass pane | reads partly red |
| **C** | **`Ice`** backdrop + glass pane | reads partly pale blue |
| **F** | same backdrop swap behind an **opaque `Stone` pane** (floor) | T ≈ 0 by construction |

*(Amended 2026-09-23 from `glow` / `glow_blue` — see the struck paragraph below and §12.2.)*

**Transmission T** = (colour change B→C measured *through the pane*) ÷ (colour change measured in
the control A→A′ with the same backdrop swap).

⚠️ **T is `1 − materialAlpha`, NOT `materialAlpha`.** Standard blending gives
`P = a·G + (1−a)·C` where `a` is material alpha, `G` the pane's own lit colour and `C` the backdrop.
Subtracting the two captures: `P_a − P_b = (1−a)·(C_a − C_b)`, so the ratio measures
**(1−a)**. `Glass` declares `"alpha": 0.5` in `resources/materials.json`, so T ≈ 0.5 — but that is a
*coincidence of 0.5 being its own complement*. At any other alpha the prediction is `1 − alpha`, and
an earlier draft of this section got the derivation wrong while landing on the right number, which is
the most dangerous way to be right.

- **working:** T ≈ 0.5 (= 1 − 0.5) — half the backdrop survives
- **opaque:** T ≈ 0 — B and C identical; the backdrop is invisible
- **milky but blending:** 0 < T < 0.5 — a *different* defect from opacity

That last row is why the metric exists. "Milky" and "opaque" have different causes and were
conflated once already. The control (A) is what makes this a number rather than an impression:
without it, "the pane got darker" is unfalsifiable.

**What the ratio cancels, and what it does not.** `G` — the pane's own lit colour — appears in both
captures and cancels exactly, so the metric is immune to how the pane itself is lit or tinted. That
is the point of differencing rather than comparing absolute brightness.

⚠️ **It does NOT cancel a shadow the pane casts on the backdrop.** If the pane attenuates the
backdrop behind it by `s`, the ratio becomes `(1−a)·s` and a *working* pane reads as broken.

> ~~**Both this and the texture confound are solved by one choice: make the backdrop EMISSIVE.**~~
> **WITHDRAWN 2026-09-23 — this paragraph was wrong, and Phase 0 proved it.** (1) At the shipped
> exposure with AgX off, emissive surfaces CLIP to (255,255,255); a clipped pixel has no colour, so
> the control delta was exactly 0.0. (2) Worse, `glow`, `glow_blue` and `glow_green` reference the
> **same texture files** in `materials.json` with no colour or tint field — they are the same
> material under three names. Swept from exposure 4.0 down to 0.1 they never differed by more than
> 2.4/255. The paragraph below is kept only as the record of what was believed.
>
> **REPLACEMENT:** the backdrop is two ordinary materials of strongly different hue — **`Bricks` vs
> `Ice`** — at **exposure 1.0** with the rig **asserting** the sampled patch never reaches 250/255.
> The shadow confound is then bounded **by measurement**: an **opaque `Stone` pane arm** is the
> empirical zero-point. Whatever the pane does to its own backdrop's lighting, Stone and Glass meet it
> equally, so glass reading *at* the Stone floor transmits nothing regardless of `s`, and glass near
> 0.5 is working regardless of `s`. This is the same role the pristine-vs-pristine noise floor played
> in the damage stage-count rig.

(original, withdrawn:) There
is **no per-voxel tint API**, so a "red vs blue backdrop" swap must use two different *materials* —
and two ordinary materials differ in texture pattern *and* in how they take light, neither of which
the ratio removes. Emissive materials (`glow` warm-white vs `glow_blue`) are flat and self-lit:
`voxel.frag` takes the emissive branch and writes
`textureColor.rgb * ubo.emissiveMultiplier` directly, so the backdrop is **not shadowed, not
lit-direction-dependent, and not tonemap-ambiguous**. That retires the shadow confound without
needing to disable shadows at all, which the API cannot cleanly do anyway
(`/api/debug/shadow` sets a *distance*, not an off switch).

Residual: `glow` and `glow_blue` still carry different texture patterns. That is acceptable because
the ratio's numerator and denominator use the **same material pair over the same screen region**, so
the pattern cancels in the mean. Measure means over a fixed pixel rectangle, not per-pixel.

**T alone is not sufficient** — see §5 Phase 0, which pairs it with a pass-execution probe, because
T ≈ 0 has two distinct causes that need different fixes.

**And if T ≈ 0.5 but the reviewer still says it looks wrong** — an outcome this plan must not be
unable to absorb — then transmission is correct and the defect is in `G`, the pane's **own** colour:
too bright, too milky, wrongly tinted. That is a different measurement (compare `G` against the
expected lit glass albedo) and a different fix, and the plan branches there rather than treating a
correct T as proof the complaint is unfounded. The reviewer's judgement outranks the metric; T only
says *which* thing to go and look at.

---

## 3. What the code says — and the contradiction it creates

Read on 2026-09-23. **Every line below is evidence, not conclusion.**

- **The opaque chunk pipeline has blending DISABLED.** `RenderPipeline.cpp:90`, in
  `createGraphicsPipeline()`: `colorBlendAttachment.blendEnable = VK_FALSE`.
- **`voxel.frag` does NOT discard transparent faces.** It discards cutout-alpha fragments
  (`:323`, `textureColor.a < 0.1`) and mirror faces (`:326`, bit 10) — but nothing for bit 1.
  `7a36910f` ("Lighting pipeline phases 1-4: shadows, SSAO, glass, mirrors", an ancestor of HEAD)
  introduced those two discards and carried an explicit note that transparent voxels render in the
  opaque pass. **That note has since been deleted from the file while the behaviour stayed**, so the
  contract is currently undocumented — which is how it got mis-modified twice.
- **`voxel.frag` writes `textureColor.a`, not the material alpha.** Material alpha (bits 2–9) is read
  only by `transparent_voxel.frag:142`. So the opaque pass never sees `alpha: 0.5` at all.
- **The OIT pass is the only path that can produce transparency.** `transparent_voxel.frag:135`
  discards everything *without* bit 1, then composites with real blending
  (`RenderPipeline.cpp:1129/1141`).

**The contradiction.** Taken together, the code as read says glass drawn in the opaque pass is
**always** solid and writes depth — so glass should *never* have looked transparent. The reviewer
reports it did. **One of those is wrong**, and which one is the single most informative thing to
learn. Possibilities: transparent faces are excluded from the opaque submission somewhere not yet
found; or the OIT composite overwrites rather than blends over the opaque result; or the reviewer's
"used to work" memory predates a change that has been in for a while.

**Do not resolve this by reading more code.** Phase 1 settles it by measurement. It is recorded here
so that whoever runs Phase 1 knows what would be surprising.

---

## 4. A design-key violation found on the way — must be closed, not measured around

`FeatureDesignKeys.md`: *"Appearance must be a pure function of world position and persistent world
state. Per-chunk quantities may only bound COST — never how something looks."*

**`Chunk::m_hasTransparent` breaks this.** It is a cached per-chunk bool (`Chunk.h:98`) recomputed in
`recomputeRenderFlags()` (`Chunk.cpp:416`) on `rebuildFaces`, and it scans **the cube store only**:

```cpp
for (size_t i = 0; i < ChunkVoxelStore::kVoxels; ++i) {
    const Cube* cube = i < cubes.size() ? cubes[i].get() : nullptr;
    ...
    if (mat->alpha < 0.99f) m_hasTransparent = true;
}
```

It never scans `staticSubcubes` or `staticMicrocubes` — yet **both sub-voxel instance paths do set
the transparent bit and quantized alpha** (`ChunkRenderManager.cpp:1084`, `:1244`). The consumer is a
frame-global early-out:

```cpp
// RenderCoordinator.cpp:1898-1904
if (chunk && chunk->getNumInstances() > 0 && chunk->hasTransparentVoxel()) { anyVisibleTransparent = true; break; }
...
if (!anyVisibleTransparent) return;     // the WHOLE OIT pass is skipped
```

**So the flag is a cost bound that is not conservative.** A view whose only glass is sub-voxel skips
the entire OIT pass and renders that glass through the opaque path. Whether a glass *subcube* looks
transparent depends on whether some other, unrelated *full-cube* glass happens to be in view — which
is appearance coupled to chunk contents.

**Two consequences that make this load-bearing for the fix, not a footnote:**

1. **No generated building's window can be transparent.** Generated walls are subcube/microcube
   (`StructureRealizer` stamps via `fillMicroBox`), so every window in every generated structure hits
   this path. It is structurally the same gap as the V2 crack problem — damage is cube-only, walls
   are sub-voxel.
2. **A fix validated only on full-cube glass could ship with generated windows still opaque.** That
   is the Stone-wall mistake one rig over, and it is why §7's rig has a sub-voxel arm.

**Fix direction:** remove the dependence rather than plumb around it — make the scan conservative by
including the sub-voxel vectors, or drop the early-out. **Do not add a cross-chunk lookup**; it would
add a stale-neighbour failure mode instead of removing the coupling.

---

## 5. Phases

### Phase 0 — make "transparent" measurable. NO code changes. — ✅ COMPLETE, results in §12

*(Deviation on record, §12.3: the §4 flag fix was committed during this phase. It belongs to Phase 4.
It is not reverted — it is red-tested, correct, and independent — but it is recorded as out of order
rather than silently absorbed.)*

Build the rig in §7 and record T for full-cube glass **and** sub-voxel glass.

**Pair T with a pass-execution probe.** T ≈ 0 has two causes needing different fixes:

| observation | meaning | fix lives in |
|---|---|---|
| T ≈ 0, OIT pass **did not run** | the §4 chunk-flag gate skipped it | `Chunk::recomputeRenderFlags` / the early-out |
| T ≈ 0, OIT pass **ran** | the opaque pass's depth+colour write occludes the OIT result | the opaque submission or `voxel.frag` |

Without the probe these are indistinguishable, and guessing between them is what produced two wrong
fixes.

**Probe surface, named (it was hand-waved in the first draft).** There is no existing render-stats
field for "did the transparent pass submit", so use a **log line at the early-out's `return`** in
`RenderCoordinator.cpp:1904`, read back through the existing logs endpoint. That adds **no API
surface, no new endpoint and no default change** — which keeps Phase 0 genuinely measurement-only.
Do not add a debug endpoint for this; a log line is sufficient and reversible.

### Phase 1 — is the break on this branch?

> **AMENDED 2026-09-23, before running — the baseline was wrong.** This phase was written when
> `origin/main` did not contain the crack branch. It does now: the branch was merged at `248209ad`,
> so the three files below are **byte-identical** at `origin/main` and HEAD (`git diff --stat
> origin/main HEAD -- <files>` is empty). Reverting to `origin/main` would have rebuilt the same
> binary, measured the same T, and falsely reported "the break predates the branch".
>
> **The baseline is `1bdf0239`** — `main` immediately before the merge. It carries main's own lighting
> work (the light-march commits) and **none** of the crack work, and it differs from HEAD in exactly
> the three suspect files (+95/−15). `shaders/transparent_voxel.frag` is unchanged by the branch, so
> the OIT shader is the same in both arms. The HEAD-only render-flag fix (§4) and the OIT probe stay in
> both arms; neither can move full-cube T (the flag is already true for cube glass, §12.1).
>
> Mechanics: `git checkout 1bdf0239 -- <the three files>`, run `build_shaders.bat` (so `voxel.frag.spv`
> matches the reverted source — committed-SPIR-V staleness would otherwise make this arm measure the
> HEAD shader), build, measure, then `git checkout HEAD -- <files>` + `build_shaders.bat` to restore.
> Nothing is committed from the reverted state. The three revert **together**, which also satisfies
> the Phase 2 ordering hazard (never `VulkanDevice.cpp` without `AtlasManager.cpp`).

Measure T at HEAD, then with the three files that could reach glass rendering reverted to
**`1bdf0239`** (was: `origin/main`): `shaders/voxel.frag`, `engine/src/core/AtlasManager.cpp`,
`engine/src/vulkan/VulkanDevice.cpp`. Two measurements, one decision:

| T differs | the break is ours | → Phase 2 |
|---|---|---|
| **T identical, both broken** | predates the branch entirely | bisect `main` on T, bounded below by `7a36910f`, the commit that introduced glass handling in the opaque pass. Nothing in the crack work is implicated |

A baseline capture already exists — `screenshots/screenshot_20260923_072602_473.png`, the engine with
those three files reverted — but it is **unjudged, and should stay that way**. T decides this split
objectively. The reviewer's eye is spent on Phase 5, where it is irreplaceable.

### Phase 2 — localize, one file per build

Three builds, T after each. The file that moves T is the cause.

⚠️ **The revert ORDER is not free, and one ordering can corrupt memory.** P5 (`330ee050`) widened the
props SSBO to a stride of two vec4s per layer in `AtlasManager.cpp` **and** doubled the buffer
allocation in `VulkanDevice.cpp` to match. So:

- Reverting **`VulkanDevice.cpp` alone** leaves `AtlasManager` writing stride-2 data into a
  half-sized buffer → **overflow**. Never do this.
- Reverting **`AtlasManager.cpp` alone** is safe: stride-1 writes into an over-allocated buffer.
- The two must be reverted **as a pair**, or `AtlasManager` first.

`voxel.frag` reverts independently, but note it takes the crack shader with it — so that arm tests
"is it the fragment shader" and cannot simultaneously confirm cracks.

### Phase 3 — name the mechanism before touching anything

Ranked by §3's evidence. **Re-ranked 2026-09-23** after the first design-check pass:

1. **The §4 chunk-flag gate** — certain to be a real defect for sub-voxel glass, whether or not it is
   *the* reported bug. Independently worth closing.
2. **Glass drawn solid in the opaque pass, occluding the OIT result.** `blendEnable = VK_FALSE` plus
   no bit-1 discard means the opaque draw writes solid colour and depth. Predicts "opaque, not milky".
3. **`AtlasManager.cpp`** — the opaque pass keys on `textureColor.a`, so glass's *texture* alpha
   channel matters. BC7 supports alpha (`VK_FORMAT_BC7_SRGB_BLOCK`, `VulkanDevice.cpp:2727`), but
   whether the encoder preserved it is unverified. Note `AtlasManagerTest.BuildAtlasFromSourcePNGs`
   is currently red (on a warm BC7 cache) — probably unrelated, worth one look while here.
4. **P5's props-SSBO stride change** (`330ee050`) — **demoted to last.** §2 of the earlier draft called
   it the prime suspect; that was wrong. Alpha for the OIT pass comes from instance `reserved`
   bits 2–9, not from the props array, so the stride change can corrupt metallic/roughness but not
   alpha. Only the buffer *resize* in `VulkanDevice.cpp` could plausibly reach the transparent
   pipeline, via a descriptor mismatch.

### Phase 4 — fix, red first

Write the failing T assertion, watch it fail, then fix. **No fix lands on a hypothesis alone.**
If the fix changes whether the opaque pass draws transparent faces, that is a **default rendering
change** and needs its pin in the same commit — plus restoring the contract note `7a36910f` wrote and
something later deleted.

### Phase 5 — reviewer sign-off, then the guards

The reviewer judges the frame. Then:

- **T as an automated L4 test.**
- **The chunk-independence test** in §8.
- **Glass goes into the standard rigs.** The root cause of this shipping unnoticed is that every
  validation rig was a Stone wall — including all of the crack-system rigs. That is the actual
  process defect, and it is the only change here that prevents the *next* one.

---

## 6. Render-path facts gathered (context, not conclusions)

- `voxel.frag` has no transparency discard; `outColor = vec4(color, textureColor.a)`.
- `transparent_voxel.frag` discards without bit 1, reads material alpha from bits 2–9
  (`(flags >> 2u) & 0xFFu`), writes accumulation + reveal.
- `RenderCoordinator::renderTransparentGeometryOIT` runs it, gated on the cached per-chunk flag (§4).
- That flag is computed from MATERIAL alpha, not damage or instance bits — **damage cannot switch it
  off**, which independently clears the crack work.
- Instance `reserved` bits: 0 emissive · 1 transparent · 2–9 quantized alpha · 10 mirror ·
  11–14 damage · 15 `varied`. Damage does not overlap alpha.

---

## 7. Test rig

**Small, one chunk, one variable, prediction written in advance, with a control** — per
`FeatureDesignKeys.md` test-rig discipline.

- Flat world; pane at **x 8–11, y 17–20, z 8**; backdrop wall at **x 6–13, y 15–22, z 4** (wider and
  taller than the pane, so the pane is fully backed); camera at **(9.5, 18.5, 20)**, yaw −90, pitch 0
  — looking down −Z, square-on, with the backdrop behind the pane. Every coordinate is inside the
  single chunk at origin (0,0,0), so no fill drops silently.
- **Backdrop material: `Bricks` vs `Ice`** (amended 2026-09-23; was `glow` vs `glow_blue`, which
  clip and are the same material — §2, §12.2). No per-voxel tint API exists, so the swap is a
  material swap; the texture/lighting difference between the two is removed by the ratio's
  control, and the shadow confound is bounded by the floor arm below.
- **Floor arm: an opaque `Stone` pane** in the same position. T must read ≈ 0. Any glass reading at
  this level is transmitting nothing.
- **Exposure 1.0, curve 0, and an unclipped-patch assertion** (fail if any sampled channel ≥ 250).
  The shipped exposure 8 clips.
- **Two arms:** a full-cube glass pane and a **sub-voxel** (subcube) glass pane. §4 predicts they
  differ *today*.
- **One variable:** backdrop colour (red → blue). Everything else fixed.
- **Control:** the same backdrop swap with **no pane**, which converts "looks darker" into a ratio.
- **Prediction, written before running:** T ≈ 0.5 for both arms if glass works; T ≈ 0 for the
  sub-voxel arm today even if the cube arm passes.
- **Measure means over a fixed pixel rectangle** inside the pane, and the matching rectangle in the
  control — not per-pixel, because the two backdrop materials carry different texture patterns which
  only cancel in the mean.
- **Verify the world, not the API response** — `/api/world/fill` is async and returns no placed
  count. Query the voxels back before capturing.

**Rig deltas from shipped defaults:** captures use `POST /api/debug/tonemap {"curve":0,
"exposure":1.0}`, so T is measured pre-AgX at 1/8 the shipped exposure. Through the shipped curve the same T reads compressed. **State the curve with every
number.**

---

## 8. Deliverables — what must land with the fix

1. `GlassTransmissionTest` (L4) — T measured against the control; fails with
   `measured transmission T = 0.02 through Glass (alpha 0.50, expected T = 0.50) — the backdrop change did not survive
   the pane`, and names which pass ran.
2. **`ChunkRenderFlagsTest.SubVoxelGlassMarksTheChunkTransparent` (L2, deterministic, no engine)** —
   build a chunk whose *only* glass is a subcube, assert `hasTransparentVoxel()` is true. **Red
   today**, because `recomputeRenderFlags()` scans the cube store only (§4). This is far cheaper than
   the pixel test below and catches the §4 defect directly, so it is the **first** thing to write:
   per `FeatureDesignKeys.md`, depth is chosen by use, and a flag-computation bug is a structural
   invariant (L2), not something that needs live pixels to see. The L4 test below then proves the
   *rendering* consequence actually went away.
3. `GlassTransparencyChunkIndependenceTest.SubVoxelGlassTransmitsRegardlessOfCubeGlassInView` —
   T for a glass **subcube** in two scenes identical except that scene B also contains a full-cube
   glass voxel elsewhere **in view**. **T must be equal.** Today this fails: T ≈ 0 in A, T > 0 in B.
   (The early-out at `RenderCoordinator.cpp:1904` is **frame-global** across visible chunks, not
   per-chunk, so "in view" is the precise invariant; same-chunk is the `FloraMarginTest`-shaped
   special case of it.)
   This is the `FloraMarginTest` / `FaunaPlanTest` shape — chunking must not change the answer.
4. Restored contract note in `voxel.frag` saying what the opaque pass does with transparent faces
   and why.
5. Glass added to the shared render rigs.

---

## 9. Design-check record

- **Pass 1 (2026-09-23) — NEEDS WORK, 4 items.** Found the §4 chunk-independence violation. Items:
  add a sub-voxel glass arm; add a pass-execution probe; make the chunk-independence test a
  deliverable; re-rank hypotheses (P5 stride demoted, AtlasManager promoted). All four folded in
  above.
- **Pass 3 (2026-09-23) — READY.** Audited the rig's *executability* rather than its logic, which is
  where the remaining defects were:
  1. **No per-voxel tint API exists**, so the backdrop swap is a *material* swap — and two ordinary
     materials differ in texture and in lighting response, neither of which the ratio cancels. Fixed
     by making the backdrop **emissive** (`glow` / `glow_blue`), which also retires pass 2's shadow
     confound outright — better than disabling shadows, which `/api/debug/shadow` cannot cleanly do
     (it sets a distance, not an off switch).
  2. **The cheap test was missing.** Everything was specified at L4, but `recomputeRenderFlags()` is
     a pure function over chunk contents and can be tested at **L2 with no engine at all** — red
     today. Added as deliverable 2, and it is now the *first* thing to write.
  3. **No branch for "T is right but it still looks wrong."** If transmission measures correct and
     the reviewer still judges the pane wrong, the defect is in the pane's own colour, not
     transmission. The plan now says so instead of implicitly treating a passing metric as proof the
     complaint is unfounded.
  4. **Rig geometry was ambiguous** ("camera square-on at z+12"). Now exact coordinates, with the
     backdrop sized larger than the pane.
- **Pass 2 (2026-09-23) — READY**, after 5 further defects found *in this plan* and fixed in place.
  Recorded because four of the five were in the measurement itself, and a bad metric is worse than
  no metric — it produces confident wrong answers, which is the failure mode this whole document
  exists to stop:
  1. **The metric's derivation was wrong.** §2 claimed the measured ratio equals material alpha. It
     equals **1 − alpha**. `Glass` at 0.5 is its own complement, so the predicted number was right by
     coincidence while the reasoning was wrong — and would have mispredicted at any other alpha.
     Renamed the symbol to **T** so the two cannot be confused again.
  2. **An uncancelled confound.** The ratio cancels the pane's own lit colour, but *not* a shadow the
     pane casts on the backdrop: that turns T into `(1−a)·s`, so a **working** pane can read as
     broken. Control added.
  3. **A memory-corrupting bisect order.** Phase 2 said "one file per build" without constraining
     order. Reverting `VulkanDevice.cpp` alone leaves `AtlasManager` writing stride-2 data into a
     half-sized buffer. Order now constrained.
  4. **The pass-execution probe had no named surface** — "a counter through an existing debug
     endpoint" was hand-waving, and no such field exists. Now a log line at the early-out, which
     keeps Phase 0 measurement-only with zero API surface.
  5. **The `main` bisect was unbounded.** Now floored at `7a36910f`, where glass handling entered the
     opaque pass.

---

## 10. Gate answers

**Voxel aesthetic.** No new assets. Two binding constraints: cracks on glass must survive
(reviewer-confirmed; `03e68fa9` deleted them and was reverted), and sub-voxel glass is in scope
because generated windows are subcube/microcube. Nothing behind a flag.

**Chunk independence.** One violation, §4, with the equality test named in §8.2. No cross-chunk
lookup is introduced — the dependence is removed instead.

**Procedural generation.** Belongs to no generation stage; this is rendering. Generation-facing
consequence only: structure gen emits glass sub-voxel, which is what makes §4 player-visible. No
world-recipe persistence.

**API surface.** Phases 0–2 add no API — existing `/api/world/fill`, `/api/world/voxel`,
`/api/screenshot`, `/api/debug/tonemap`. The pass-execution probe surfaces through an existing debug
endpoint rather than a new one. No defaults change before Phase 4; if Phase 4 changes opaque-pass
behaviour, that is a pinned-contract change committed with its pin.

**Visual test plan.** "Works" = T ≈ 0.5 with a control (§2). Depth **L4** — a render-path defect is
invisible below live-engine. Red test and its message in §8.1. Rig, prediction, control and deltas
from shipped defaults in §7.

---

## 11. Why this is sequenced after the crack system

The crack system (`VoxelDamageVisualization.md`) reached V1 complete on 2026-09-23. Glass blocks none
of it, and the crack rendering is confirmed correct on opaque materials **and** on glass. Keeping the
two apart stops them contaminating each other — which already happened once, when a transparency bug
was mistaken for a crack bug and "fixed" twice inside the crack code.

---

## 12. Phase 0 — results, plan amendments, deviations (2026-09-23)

### 12.1 Results

Rig: `tools/glass_transmission.py`, as amended in §7. Curve 0, exposure 1.0, DamageLab project,
Debug build at `e08fcc74`. Prediction written in advance: T ≈ 0.50 (= 1 − alpha 0.5).

| arm | T | reading |
|---|---|---|
| control — no pane | — | backdrop swap moves the patch by \|RGB\| 41.3: the backdrop is visible |
| **Stone pane (floor)** | **0.008** | opaque, as it must be |
| **Glass, full cube** | **0.003** | **at the floor** |
| **Glass, subcube** | **0.010** | **at the floor** |

**Glass transmits nothing.** Not milky — as opaque as a stone wall, to within the floor.

**Pass-execution probe** (TRACE line at the OIT early-out, `RenderCoordinator.cpp`): with the glass
pane in view, **`SUBMITTING` on 1119 frames**. The OIT pass runs.

**Phase 0 decision (§5 table): T ≈ 0 with the OIT pass running → the opaque pass occludes the OIT
result.** This resolves §3's contradiction in favour of the code as read: `voxel.frag` has no discard
for transparent faces, and the opaque pipeline has `blendEnable = VK_FALSE`, so glass is drawn solid
with depth over whatever OIT produced. Hypothesis #2 in Phase 3 is now the front-runner. It is **not**
yet the confirmed cause — Phase 1 and Phase 2 still decide that, as planned.

The §4 chunk-flag defect is real (red-tested) but is **not** the reported bug: sub-voxel glass is
exactly as opaque as cube glass, and the probe shows the pass is not being skipped.

### 12.2 Plan defects found by executing it — amended above

1. **The emissive backdrop could not work** (§2, §7). Clipping at shipped exposure gave a control
   delta of 0.0, and `glow` / `glow_blue` / `glow_green` turned out to be one material under three
   names. Replaced by `Bricks` / `Ice` at exposure 1.0 with an unclipped-patch assertion, plus the
   `Stone` floor arm. All three design-check passes missed this: they reasoned about what emissive
   materials *should* do instead of checking what these ones *are*.
2. **The probe's first version reported nothing while appearing to work** — `LOG_TRACE_FMT` is
   ostringstream-based, and a printf format string was logged verbatim with its arguments dropped.
   Not a plan defect, but a tool defect caught only because the output was read rather than assumed.

### 12.3 Deviations from the plan — recorded, not hidden

1. **Code was committed inside Phase 0**, which says "NO code changes": the §4 fix in
   `Chunk::recomputeRenderFlags` (sub-voxel tiers now scanned) with `ChunkRenderFlagsTest`. That is
   Phase 4 work done in Phase 0. It is **kept**, because it was red-first (2 controls passing, 4
   failing → 6/6 green), is independent of the reported bug, and closes a design-key violation — but
   it should have been proposed as a plan amendment first, not committed and reported afterwards.
2. **The rig was changed in the tool without amending §2/§7.** Amended now (§12.2.1).
3. **A Phase 4 fix was proposed while Phase 1 was the next step.** Withdrawn. Phase 1 is next.

### 12.4 A constraint Phase 4 must satisfy, discovered now so it is not discovered then

If Phase 3 confirms hypothesis #2, the obvious fix is a bit-1 discard in the opaque pass. That would
leave glass drawn **only** by `transparent_voxel.frag`, which has **no crack block** — so cracks on
glass would vanish. §1 forbids that (the reviewer confirmed cracks on glass look right, and `03e68fa9`
already deleted them once by mistake). **Any Phase 4 fix of this shape must carry crack rendering into
the OIT shader in the same change**, and Phase 5's sign-off must include a cracked pane.

### 12.5 Next: Phase 1, exactly as written in §5

*(Baseline amended before running: `1bdf0239`, not `origin/main` — see the Phase 1 note in §5.)*

Measure T at HEAD (done: 0.003) and with `voxel.frag`, `AtlasManager.cpp` and `VulkanDevice.cpp`
reverted to `1bdf0239` — `AtlasManager.cpp` and `VulkanDevice.cpp` reverted **together** (§5
Phase 2 ordering hazard). Predicted, and stated so the result can falsify it: **T identical (≈ 0) —
the break predates this branch**, because hypothesis #2's mechanism has been in the code since
`7a36910f`. If T moves, the break is ours and Phase 2 bisects it.

### 12.6 Phase 1 result (2026-09-23)

Baseline `1bdf0239` (amended from `origin/main`, §5). The three suspect files were checked out from
`1bdf0239`, `build_shaders.bat` run, engine rebuilt, then restored to HEAD the same way — `git status`
clean on all three files and every `.spv` afterwards, manifest current. Runtime shader provenance: the
exe loads `shaders/*.spv` relative to its working directory; `phyxel.log` (also a relative path) was
written to the repo root, so the working directory was the repo root and the reverted `.spv`
(`378e5357…`, freshly built) was the one loaded — not the stale copy in `build/shaders/`.

| arm | HEAD | baseline `1bdf0239` |
|---|---|---|
| control — backdrop swap, no pane | \|RGB\| 41.3 | \|RGB\| 41.3 |
| Stone floor | 0.008 | 0.001 |
| **Glass, full cube** | **0.003** | **0.012** |
| **Glass, subcube** | 0.010 | 0.010 |

**T is identical within the floor in both arms. The break predates the crack branch** — matching the
prediction written in §12.5 before the run. Nothing in the crack work is implicated: not P5's
props-stride change, not the crack block in `voxel.frag`, not the SSBO resize in `VulkanDevice.cpp`.
The two wrong fixes (`58b00dfd`, `03e68fa9`) and the "glass regression" framing in the crack plan were
attributing to the crack work a defect that was already on `main`.

### 12.7 Plan defect found before starting the next step — execution STOPPED pending amendment

Phase 1's decision table sends this outcome to *"bisect `main` on T, bounded below by `7a36910f`"*.
Before running it:

- **The range is 804 first-parent commits** (`7a36910f..1bdf0239`), ~10 bisect steps, 73 of which
  touch the glass render path.
- **The lower bound was never verified as GOOD.** A bisect needs a commit where glass transmits. The
  front-running mechanism (§12.1: the opaque pass draws glass solid, with depth, over the OIT result)
  would have been present *at* `7a36910f` itself — that commit introduced the opaque-pass handling.
  If `7a36910f` is also opaque, there is no good end and the bisect is meaningless.
- **Old commits may not run the rig.** It depends on `/api/debug/tonemap`, `/api/world/subcubes/batch`
  and `clear_region` jobs; months back, some may not exist, and intervening build-system changes (the
  Bullet removal among them) make each step a full rebuild of an old tree.

Options, for the reviewer to choose — **not chosen by the executor**:

- **(A) Bisect as written, with a gate first:** measure T at `7a36910f`. If it is ~0.5 (good), bisect
  toward HEAD. If it is ~0, there is no good commit — glass has not transmitted since the OIT design
  landed — and the bisect is abandoned. Cost: one old-tree build to gate, then up to ~10 more.
- **(B) Replace the bisect with a direct mechanism experiment at HEAD (moves Phase 3 up):** add a
  temporary bit-1 discard to the opaque pass in `voxel.frag`, rebuild, measure T. If T → ~0.5 the
  occlusion mechanism is confirmed; if T stays ~0 it is ruled out. This answers *why*, which is what
  the fix needs; the bisect answers *when*, which the fix does not need unless the mechanism is
  unclear. The experiment is reverted after measuring and is not a fix (it would strip cracks from
  glass, §12.4). Cost: one build.

### 12.8 Decision on 12.7 — Option A: follow the plan, bisect `main` (reviewer, 2026-09-23)

Reviewer's words: *"all i know is that glass was transparent at one point or another in the engine's
history. it isnt now. follow the plan."* Option (B) is **not** taken. The bisect runs as Phase 1
prescribed, with the gate from 12.7 as its first step.

**Procedure — Phase 1b.**

1. **Isolation.** Every historical build happens in a separate `git worktree` outside the repo
   (`G:/Github/phyxel-bisect`), with its own build directory. The main working tree holds other
   sessions' uncommitted WIP and must not be checked out to old commits. Each worktree engine runs
   from the worktree root, so its relative `shaders/` path loads that commit's committed `.spv`.
2. **Gate.** Measure T at `7a36910f`. **Prediction, written before the run: T ~ 0** — because the
   occlusion mechanism (§12.1) is present at that commit. If the gate reads ~0.5, `7a36910f` is GOOD
   and the bisect runs toward `1bdf0239` (known BAD, §12.6).
3. **If the gate is BAD**, the reviewer's statement still stands (glass was transparent at some point),
   so the good commit is EARLIER, not absent. The search then steps **backwards** in coarse jumps along
   the first-parent line — each jump doubling the distance — until a GOOD commit is found, then bisects
   between it and the nearest BAD. 7a36910f is no longer treated as a floor.
4. **Classification per commit:** GOOD if T ≥ 0.25, BAD if T ≤ 0.08 (the Stone floor band), and
   ANYTHING BETWEEN IS RECORDED AS "PARTIAL" AND INVESTIGATED, never forced into good/bad. Every
   commit's T, floor and control go in the table in 12.9 — no step is summarised without its numbers.
5. **Rig compatibility.** Old commits may lack endpoints the rig uses. The rig degrades rather than
   guesses: each missing endpoint is detected and reported, and a commit where the CONTROL cannot be
   established (backdrop swap not visible) is marked **UNTESTABLE** and skipped (`git bisect skip`
   semantics), never counted as bad. The full-cube arm is the bisect signal; the subcube arm is
   recorded where the old API supports it.
6. **Nothing from a historical build is committed or merged.** The worktree is removed at the end.

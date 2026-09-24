# Glass is not transparent — investigation & fix plan

**Status:** OPEN. **Phase 0 COMPLETE** — glass measured fully opaque; the OIT pass runs.
**Phase 1 COMPLETE** — identical at the pre-branch baseline: **the break predates the crack branch.**
**Phase 1b COMPLETE — first bad commit is `2ea8b8d9` (#397), a texture-only commit that stripped the
alpha channel from Glass (§12.10).** **Fix design recorded (§13); all decisions made, including glass casting
no shadow (§13.9). Design-check pass 4 NEEDS WORK → 7 items folded in (§13.9–13.15). **Phase 3 COMPLETE (§14).**
**PHASE 4 STOPPED — the §13 design rests on a false premise: the OIT pass has been DISABLED since
`7a36910f` (§15); the blocker was investigated and is gone (§15.8).
**PHASE 4 BUILT (§16).** Reviewer: *"almost perfect. overall appearance looks really good"* — with one
defect: opaque faces beside glass are culled. **§17 plans the fix; design-check required before any
code.** NOT FIXED until the reviewer says so. Results in §12. Gated through `FeatureDesignKeys.md` three
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

### 12.9 Phase 1b — gate result and bisect table

**Gate: `7a36910f` is GOOD. The prediction written in 12.8 (BAD) was WRONG.**

`RESULT 7a36910f GOOD cube=0.500 subcube=0.387 floor=0.000 control=224.6`

Full-cube glass transmits **T = 0.500 — exactly `1 − alpha`** for Glass at alpha 0.5. The Stone floor
reads 0.000 and the control is strong, so the number is not an artefact. Glass *was* transparent, as
the reviewer said, and my assumption that the occlusion mechanism was present *at* `7a36910f` was
false. Whatever breaks transmission arrived between `7a36910f` (GOOD) and `1bdf0239` (BAD, §12.6).
The bisect therefore has a real good end, and step 3 of 12.8 (the backwards search) is not needed.

**Conditions — they differ from HEAD's, and every row states its own.** The gate build predates
`/api/debug/tonemap`, so it renders through its own default curve; at that curve the glass arm
clipped (peak 250/255) and the guard stopped the run rather than report a meaningless ratio. Ambient
was lowered to 0.30 via `/api/ambient` to bring the patch under the ceiling. Light LEVEL cancels in T
(ratio against a same-light control), and the thresholds (GOOD ≥ 0.25, BAD ≤ 0.08) are wide enough
that no tone curve moves 0.009 to 0.5 or back. Release build (the bisect's many steps need runtime
speed; T measures the render path, not the build config). `voxel.material` is absent from this era's
voxel query, so pane *existence* was verified but not its material.

**Environment needed to run a historical build** (recorded so the next step does not rediscover it):
isolated worktree `G:/Github/phyxel-bisect`; `bullet3` submodule fetched from upstream (the current
repo no longer carries it); the uv-managed Python 3.12 directory on `PATH` (those builds link
`python312.dll`); launched with `-p` on a **copy** of DamageLab so an older engine cannot rewrite the
real project's world DB, and so the project launcher does not block job completion; the rig run with
`--shot-root` pointing at the worktree, because screenshot paths are relative to the engine's working
directory.

**Rig changes made during this step** (tool fixes, not plan changes — each is visible in the RESULT
line): a 404 is now reported as a missing endpoint rather than "engine unreachable" (HTTPError
subclasses URLError); control failure yields UNTESTABLE (exit 3), never BAD; `--port`, `--label`,
`--shot-root`, `--ambient`, `--time-of-day` (time frozen when set). Revalidated at HEAD after the
change: `BAD cube=0.009 floor=0.007 control=41.2` — unchanged from Phase 0.

| commit | position (first-parent from `7a36910f`) | class | cube T | subcube T | floor | control | conditions |
|---|---|---|---|---|---|---|---|
| `7a36910f` | 0 | **GOOD** | 0.500 | 0.387 | 0.000 | 224.6 | ambient 0.30, engine default curve, Release |
| `3a4a2884` | 201 | **GOOD** | 0.500 | — | 0.000 | 224.5 | same |
| `9728a21b` | 301 | **GOOD** | 0.500 | — | 0.000 | 224.5 | same — first attempt UNTESTABLE (harness: stale CMake glob, see below) |
| `7371362e` | 351 | **GOOD** | 0.500 | — | 0.000 | 224.5 | same |
| `9c059ac8` | 376 | **GOOD** | 0.500 | — | 0.000 | 224.5 | same |
| `4c3182de` | 389 | **GOOD** | 0.573 | — | 0.000 | 37.9 | same (control drops: a lighting/tone change landed before here — not the glass break) |
| `94d7a68a` | 395 | **GOOD** | 0.573 | — | 0.000 | 37.9 | same |
| **`1acc7910`** | **396** | **GOOD** | **0.573** | — | 0.000 | 37.9 | same — **last good** |
| **`2ea8b8d9`** | **397** | **BAD** | **0.000** | — | 0.000 | 37.9 | same — **FIRST BAD** |
| `aadc632c` | 402 | **BAD** | 0.000 | — | 0.000 | 40.8 | same |
| `1bdf0239` | 804 | **BAD** | 0.012 | 0.010 | 0.001 | 41.3 | tonemap curve 0 exp 1.0, Debug (§12.6) |

### 12.10 Phase 1b result — the break is a TEXTURE, not code

**First bad commit: `2ea8b8d9` (#397, 2026-06-30) — "feat(textures): high-def regen for 64px
materials + fix Mirror missing texture".** Last good: `1acc7910` (#396).

**It changed no code and no shaders** — 72 PNGs under `resources/textures/source/`, one resources
file, and the generator `tools/gen_highdef_materials.py`. Going from #397 back to #396 did not even
relink the executable: **the GOOD and BAD measurements of that pair came from the same binary.** The
only difference between them is texture data. The reviewer called it independently from the captures
("when you used the old glass texture i could see through it") before the bisect had finished.

**What the texture change did** (read from git, no build):

| | Glass texture | alpha channel |
|---|---|---|
| `1acc7910` and earlier | 64×64 **RGBA** | **61.7% of texels below alpha 0.1** |
| `2ea8b8d9` → HEAD | 1024×1024 **RGB** | **none** — reads as 1.0 everywhere |

**Why that makes glass opaque** — resolving the §3 contradiction completely. The opaque pass
(`voxel.frag`, blending off) has `if (textureColor.a < 0.1) discard;`. With the old texture most glass
fragments had alpha below 0.1 and were **discarded from the opaque pass**, leaving the OIT pass to draw
them — transparent. With no alpha channel every glass fragment passes the test, the opaque pass draws
the whole pane **solid, with depth**, and the OIT result underneath never shows. §3 was right that the
opaque pass has no *transparency* discard; it missed that the *cutout* discard was doing the job, via
the texture. That is the mechanism to confirm in Phase 3 — it is the leading explanation, not yet a
confirmed one.

**Glass was not the only casualty.** The same regen stripped cutout alpha from **36 textures**: all 6
glass faces and **all 30 leaf textures** (oak, autumn, birch, jungle, spruce). **The leaves were
repaired afterwards** — `d030c90e` ("cutout leaf-cluster masks replace the flat ellipse cards") and
`f10d883c` ("leaf_forge authors full RGBA leaves; real transparent negative space") — and have cutout
alpha at HEAD. **Glass was never touched again.** So the repair pattern already exists in this repo for
the identical failure; it was applied to foliage and not to glass.

**Implication for the Phase 4 fix (recorded now, decided later):** the defect is in *data* and in the
*generator* that produced it. A shader change would be treating a symptom. The fix belongs in glass's
texture authoring (the generator must emit RGBA for alpha-bearing materials) plus a guard so a regen
cannot silently strip alpha again — the same guard would have protected the leaves. §12.4's constraint
~~(cracks on glass must survive) is satisfied automatically by a data fix, because nothing in the crack
path changes.~~ **WRONG — corrected 2026-09-23, see §13.3.** `voxel.frag` computes the crack (line 310)
*before* the cutout discard (line 331), so a transparent glass fragment is discarded crack and all, and
`transparent_voxel.frag` has no crack code. The cracks the reviewer approved on glass rendered *because
glass was being drawn opaque*. Making glass transparent again — by ANY means — removes them unless the
transparent shader learns to draw them.

**Harness defect found and fixed during the bisect** (tool, not plan): the first build of #301 failed
to LINK new symbols because the old tree's source lists are `file(GLOB ...)` and a bisect jump that
adds `.cpp` files does not re-run the glob. It was recorded UNTESTABLE — never BAD — and re-run with a
reconfigure on every step: GOOD. Steps before the fix (#402, #201) built successfully; a stale glob can
only omit *new, unreferenced* files, which cannot affect the render path, and the decisive boundary
pair (#396/#397) was measured with the fixed harness.

**Worktree removed** per 12.8 step 6; nothing from any historical build was committed.

---

## 13. Phase 4 design — the reviewer's direction (2026-09-23)

### 13.1 The direction, in the reviewer's words

> *"the old glass textures still look bad though, and dont fit with the higher res counterparts. i want
> to find some middle ground where glass still is transparent but doesnt completely look invisible so
> you know it is there. light distortion sounds cool, but also too expensive. i am thinking a much
> cleaner glass texture that maintains the transparency as much as possible sounds best. also we need to
> make sure cracks still work on glass too"*

Read as requirements:

| # | requirement | source |
|---|---|---|
| R1 | **Not** the old 64px texture — it looks bad and does not match the 1024px family | reviewer |
| R2 | **Transparent**, as much as possible | reviewer |
| R3 | **Not invisible** — a pane must read as present | reviewer |
| R4 | **No refraction / light distortion** — too expensive | reviewer |
| R5 | A **much cleaner** glass texture | reviewer |
| R6 | **Cracks must work on glass** | reviewer (and §1) |
| R7 | A regen must never again silently strip glass's alpha | §12.10 root cause |

### 13.2 Why the fix is data + a routing change, not data alone

The root cause is not just "the texture lost its alpha". It is that **glass's transparency depended on
its texture alpha falling below a 0.1 cutout threshold in the opaque pass** — an undocumented coupling
that a texture regeneration broke without any code changing, and that nothing tested. A new texture
alone would restore that coupling and leave it just as fragile.

It is also incompatible with R3 + R5. Under the cutout rule a texel is binary: alpha < 0.1 → discarded
(fully clear), alpha ≥ 0.1 → drawn **solid** by the opaque pass. There is no way to author "a faint
streak you can see through" — it is either invisible or opaque. That is exactly why the old 64px glass
looked bad: its visible detail was solid pixels punched through a clear pane.

**Design: transparent-material faces are drawn by the OIT pass ONLY.** The opaque pass discards faces
carrying the transparent bit (bit 1), the way it already discards mirror faces (bit 10) for the mirror
pass. Then texture alpha stops being a cutout and becomes **continuous coverage** in the OIT blend
(`alpha = max(textureColor.a, matAlpha)`, `transparent_voxel.frag:142-143`), so a clean texture can
carry faint, see-through surface detail — R3 and R5 together.

**Blast radius:** Glass is the only material with alpha < 0.99 (`materials.json`), so the routing
change affects glass and nothing else. Leaves are cutout materials (alpha 1.0), not bit-1 transparent,
and keep the cutout path unchanged.

**This restores the contract note `7a36910f` wrote and something later deleted (§3)** — the opaque
pass's handling of transparent faces becomes explicit, documented at the discard site.

### 13.3 Cracks on glass (R6) — must move into the transparent shader

With glass drawn only by OIT, the crack must be drawn there. `transparent_voxel.frag` already has the
instance `flags` (damage bits 11–14) and already includes `lighting.glsl`; it needs:

- `#include "crack.glsl"` — the **same** crack field as opaque materials, seeded from world position, so
  a crack reads identically on glass and stone and stays chunk-independent;
- the material's `crackStyle` from the props SSBO at stride 2 (`textureUVs[gi*2 + 1].x`, §P5). Glass is
  style ≈ 0.75 — the dense, fine network;
- a crack *look* for a transparent surface — **open decision §13.6 (c)**. On opaque stone a crack
  darkens the albedo. On glass that is not how fractures read: light scattering at a fracture surface
  makes cracks appear **brighter / frosted**, and more opaque than the clear pane around them.

Because the crack changes coverage, a cracked pane is measurably *less* transparent along the crack
lines — which is also what makes the crack visible through the pane.

### 13.4 "Not invisible" (R3) without refraction (R4)

Three cheap levers, none of which is refraction:

1. **Base coverage** — the material alpha. Today 0.5 (T = 0.50, a heavy tint). R2 argues for less.
   **Open decision §13.6 (a).**
2. **Surface detail in the texture** — faint streaks / slight edge weight at low alpha, now possible
   because alpha is continuous (§13.2).
3. **A sun specular highlight in the OIT shader.** ~~— a few ALU ops per glass fragment~~ **CORRECTED
   (design-check pass 4): it ALREADY EXISTS.** `transparent_voxel.frag:153-154` computes Blinn-Phong sun
   specular (`pow(max(dot(normal, halfVec), 0.0), 64.0) * 0.3`) and `:198` point-light specular. It has
   never been visible because the opaque pass drew glass solid on top of the OIT result. The work is to
   **make the existing highlight visible** (it becomes visible as a consequence of §13.2's routing) and
   to **tune it only if the reviewer judges it too weak** — not to write a new one. See §13.11.

### 13.5 The texture (R1, R5, R7)

- **1024 × 1024 RGBA**, matching the high-res family (`"resolution": 1024` in `materials.json`).
- **Authored by a generator, not hand-edited** — the same pattern that fixed the leaves (`leaf_forge`,
  `f10d883c`). A generator makes the look reproducible and reviewable, and it is where R7's guard lives.
- Clean: near-uniform low alpha, subtle detail, **no baked distortion** (R4).
- **R7 guard:** a test that fails if any material with `alpha < 0.99`, or any texture intended to carry
  cutout/coverage alpha, is stored without an alpha channel. It would have caught `2ea8b8d9` for glass
  **and** for the leaves.

### 13.6 Decisions — DECIDED by the reviewer 2026-09-23

| | decision | consequence |
|---|---|---|
| **(a)** | **T ≈ 0.80** — very clear | Glass material alpha 0.5 → **0.20**. Target band for the L4 test: T 0.75–0.85. |
| **(b)** | **Yes — sun highlight** on glass | Specular term in `transparent_voxel.frag`; the lever that keeps a very clear pane noticeable (R3). |
| **(c)** | **Bright, frosted crack lines** | Along the crack field, coverage rises and colour lifts toward white — the opposite of stone's darkening. |

The options as originally posed are kept below for the record.

#### Options as posed

**(a) How transparent?** Measured as T (fraction of the backdrop that comes through; §2).
Today's declared alpha 0.5 gives T 0.50. "As transparent as possible but visible" suggests roughly
**T ≈ 0.80 (alpha 0.20)** — proposed, not decided.

**(b) Sun highlight on glass?** Proposed yes — it is the lever that makes a very clear pane noticeable
without making it less clear.

**(c) What a crack looks like on glass?** Proposed: **bright/frosted, more opaque** along the crack
lines — how real fractured glass reads — rather than the dark lines used on stone.

### 13.7 Validation (red first, per §5 Phase 4)

| test | layer | red today | green when |
|---|---|---|---|
| T at the chosen target, full-cube **and** subcube glass (`glass_transmission.py`) | L4 | T ≈ 0.00 | T within ±0.05 of target |
| Opaque pass skips bit-1 faces — pane is drawn once, by OIT | L4 (probe) | glass drawn by both passes | opaque pass discards |
| **Cracks visible on glass**: damaged vs undamaged pane differ by more than the floor, through the pane | L4 | no crack in the see-through area (discarded) | crack measured above floor |
| **Cracks on stone unchanged**: existing crack tests + a stone ladder capture | L2/L4 | green | still green |
| R7 guard: transparent materials must ship alpha | L2 | **red** (Glass is RGB) | green |
| Chunk-independence (§8) | L2/L4 | already green (§4 fix) | still green |
| **Reviewer's visual sign-off**: clean pane, cracked pane, pane in a generated building | human | — | reviewer says so |

Standing instruction still applies: **not fixed until the reviewer confirms it by eye.**

### 13.8 Phase 3 still runs, as diagnosis only

Before building the fix, confirm §12.10's mechanism: temporarily restore the `1acc7910` RGBA glass
texture at HEAD, measure T (predicted ≈ 0.5), revert. It is **never shipped** — R1 rules the old texture
out — it proves the diagnosis the fix design rests on.

### 13.9 DECIDED: glass casts NO shadow (reviewer, 2026-09-23) — design-check pass 4, item 1

**Why a decision was needed.** `shadow.frag` is empty and the chunk shadow pass writes every face as an
occluder, so a T ≈ 0.80 pane would still cast a **fully black shadow** — contradicting R2. Reviewer:
*"no shadows from glass"*.

**Mechanism.** The chunk shadow pipeline already binds the full instance layout
(`Vulkan::InstanceData::getAttributeDescriptions()`, `ShadowMap.cpp:347`), so the flags word is present
at location 3; `shadow.vert` simply does not declare it. `shadow.vert` declares
`layout(location = 3) in uint inFlags;` and, for faces carrying the transparent bit (bit 1), writes a
**degenerate position outside the clip volume**, so the face rasterizes nothing in any cascade.
Culling in the vertex stage rather than `discard` in `shadow.frag` means zero fragment work, and it
leaves the shadow pass's 36-index both-windings draw untouched (that index count is load-bearing —
CLAUDE.md, "M5 settled empirically").

**Scope:** every cascade that uses the chunk shadow pipeline (near 40 u, mid 420 u, far 1600 u —
`NearShadowCascade.md`). Out of scope and recorded: broken-glass debris uses the dynamic/kinematic
shadow pipelines and keeps casting — revisit only if it reads wrong.

**Test (L4), red today:** a Stone pane and a Glass pane over flat ground, sun angled so each pane's
shadow lands on open ground. Metric: ground luminance in each pane's shadow footprint ÷ ground
luminance beside it. **Stone ≈ shadowed (control that the shadow is really there); Glass within the
noise floor of 1.0.** Red today: glass shadows like stone.

### 13.10 Lighting-doc rule — design-check pass 4, item 2

CLAUDE.md: `docs/LightingPipeline.md` must be updated **in the same commit** as any change to a
receiving shader or to shadow casting. This fix changes both — `transparent_voxel.frag` (row 41 of the
§0 receiver matrix: crack, coverage, world-position input) and chunk shadow casting (§13.9). Phase 4
deliverables therefore include: the §0 matrix rows, a §9 change-log entry, and
`python tools/lighting_doc_check.py --update` (`build_and_test.ps1` runs `--check`). Any `.spv` rebuilt
by `build_shaders.bat` is committed with its source (committed-SPIR-V rule).

### 13.11 The highlight already exists — design-check pass 4, item 3

See the correction in §13.4. Decision (b) "yes, a highlight" stands; the work changes from *write* to
*reveal and, if needed, tune*. Validation: the reviewer's sign-off frames include the pane at a grazing
angle to the sun, where the existing highlight should now appear.

### 13.12 Crack seed parity with stone — design-check pass 4, item 4

**Defect in the design as first written.** `transparent_voxel.frag` builds world position as
`inWorldPos + ubo.cameraWorld` (`:173`) — a float sum that loses precision far from the origin.
`voxel.frag` seeds the crack from the exact `vChunkBaseAbs + (inWorldPos - vChunkBaseRel)` (`:269`).
Seeding glass cracks the first way would make them differ from stone's pattern and shimmer at large
world coordinates — a world-position defect.

**Requirement.** The OIT pipeline is built with the **same** `static_voxel.vert` as the opaque pass
(`RenderPipeline::createOITPipeline`, "same static_voxel.vert as opaque pass"), which already emits
`vChunkBaseAbs` / `vChunkBaseRel` at locations 10 / 11. `transparent_voxel.frag` declares those two
inputs and seeds `crackField` from the identical expression. To stop the two copies drifting, the
expression moves into **one shared helper** in an include both shaders use, so the formula exists once.

**Test (L4):** the same damaged glass pane built near the origin and at a far coordinate (e.g.
x ≈ 100 000). The crack line-crossing count per scanline (the `VoxelCrackStyleTest` observable) must
match within tolerance. A precision defect shows as a far-pane count that disagrees or flickers across
captures.

### 13.13 The LOD install path, and the invariant it threatens — design-check pass 4, item 5

**Why this got sharper.** Under OIT-only routing, a wrong `hasTransparentVoxel()` no longer renders
glass *opaque* — it renders it **invisible** (the opaque pass discards it, the OIT pass is skipped).

**Covered:** the §4 fix makes the scan conservative for every tier (`ChunkRenderFlagsTest`, 6/6), and
`Chunk::rebuildFaces` is the only caller of `rebuildAllFaces`, so every fine-mesh path refreshes the
flag.

**Not covered:** `setLodFaces` (`RenderCoordinator.cpp:2975`) installs faces **without** recomputing
the flag. A chunk that only ever received LOD faces would keep the default `false`. Chunk LOD is
default-OFF, so exposure is small today, but the invariant must hold regardless.

**Invariant, pinned as a test:** *no face is ever both discarded by the opaque pass and skipped by
OIT.* L2 form: for a chunk containing glass, after **every** face-install path (`rebuildFaces`,
`setLodFaces`), `hasTransparentVoxel()` is true. Added to `ChunkRenderFlagsTest`; the fix makes
`setLodFaces` refresh the flag (or asserts it was computed). If the LOD mesher also drops bit 1, LOD
glass would render opaque rather than invisible — recorded for `docs/LodTierLedger.md`, which must be
updated if any tier's handling of glass changes.

### 13.14 R7 guard — where the list comes from — design-check pass 4, item 6

`materials.json` has no cutout flag: leaves are `alpha` 1.0, so "textures intended to carry alpha" had
no data behind it. **Chosen (executor, routine): an explicit list inside the test** — every texture
face of every material whose alpha is load-bearing (Glass's six faces, plus every leaf material's).
No loader or schema change, and the list is exactly the set a regen must not strip. The test asserts
each file **has an alpha channel AND carries coverage** (some texels meaningfully below 255) — an
alpha channel that is all-255 would pass a "has alpha" check and still be the `2ea8b8d9` failure.
It would have caught that commit for glass and for all thirty leaf textures.

### 13.15 A concrete rig for cracks on glass — design-check pass 4, item 7

`tools/glass_transmission.py` gains a crack arm:

- **Layout:** two identical glass panes side by side in one chunk, over the **same** backdrop.
- **Damage:** one pane damaged uniformly to ratio **0.45** of Glass's toughness (stage 3 of 7 — the
  same mid ratio the stage-count ladder used), `radius` 0.4 so the pane is uniform (asserted: corner
  and centre `damage01` agree).
- **Metric:** mean |RGB| difference between the two panes' patches.
- **Floor:** the same layout with **both** panes undamaged — the pane-to-pane noise floor (the lesson
  of the stage-count rig: without it a floor reading looks like signal).
- **Prediction:** difference > 2 × floor, **and** the damaged pane reads *brighter* than the clean one
  (frosted cracks, decision (c)) — the sign is part of the prediction.
- **Plus** the stone-cracks-unchanged check (existing crack tests + a stone ladder capture), and the
  reviewer's visual sign-off of a cracked pane.

### 13.16 Design-check pass 4 record

**NEEDS WORK, 7 items — all folded in above** (13.9 shadow decided by the reviewer; 13.10 lighting
doc; 13.11 highlight premise corrected; 13.12 crack-seed parity; 13.13 LOD invariant; 13.14 guard
list; 13.15 crack rig). Not REDESIGN: the core — OIT-only routing, data + generator fix, cracks in the
transparent shader — held against the code; the items tightened it.

---

## 14. Phase 3 — mechanism confirmed (2026-09-23)

**Procedure (§13.8):** at HEAD, with no code change, the six `glass_*.png` sources were replaced by
their `1acc7910` versions (64×64 RGBA), the engine restarted, T measured, and the files restored.
Verified in between, not assumed:

- the atlas loads sources as RGBA (`stbi_load(..., STBI_rgb_alpha)`, `AtlasManager.cpp:42`) and
  bilinear-resamples any size to the layer size (`:50`), so the 64px texture entered the 1024 array
  with its alpha — edges softened by the resample;
- the log shows the arrays were **rebuilt and re-encoded from source** ("Built texture array… /
  BC7-encoded"), not served from the BC7 cache, which is keyed by a hash of the sources;
- after the run, `git checkout HEAD --` on the six files; `git status` clean; `glass_side_n.png` back
  to RGB 1024×1024. **Nothing from this step is committed except this record.**

**Result** — curve 0, exposure 1.0, DamageLab, Debug build at HEAD:

| | HEAD, current RGB texture (§12.1) | HEAD + `1acc7910` RGBA texture |
|---|---|---|
| control | \|RGB\| 41.3 | \|RGB\| 41.6 |
| Stone floor | 0.008 | 0.000 |
| **Glass, full cube** | **0.003** | **0.667** |
| **Glass, subcube** | **0.010** | **0.669** |

**The mechanism is confirmed.** Changing only the texture's alpha channel takes glass from opaque to
transmitting on today's code. Nothing else in the render path is needed to explain the defect.

### 14.1 What did NOT match the prediction — and why it matters for Phase 4

The prediction was **T ≈ 0.5** (= 1 − alpha, and the 0.500–0.573 the historical builds measured).
**Measured 0.667.** The direction was right; the value was not, and I do not yet know why. Candidates,
none verified: weighted-blended OIT is an *approximation* of ordered blending and need not return
exactly `1 − alpha`; the bilinear resample changed the alpha distribution; today's lighting and
exposure differ from the historical builds'. Notably, the ~38% of old-texture texels at alpha ≥ 0.1
are drawn *solid* by the opaque pass, which should pull T **below** 1 − alpha — so the transmitting
part is transmitting even more than 0.5.

**Consequence, recorded now so Phase 4 does not rediscover it:** decision (a)'s mapping
"T ≈ 0.80 ⇒ material alpha 0.20" assumed T = 1 − alpha. **That assumption is not supported by this
measurement.** Phase 4 must **calibrate** glass's alpha against measured T to land in the decided band
(0.75–0.85), not compute it. The target is the reviewer's decision (T); the alpha is whatever produces
it, found by measurement.

### 14.2 Rig label to fix in Phase 4

`glass_transmission.py`'s per-arm label hardcodes a "working" band of 0.35–0.65 centred on 0.5, so it
printed `partial/milky` for 0.667 while the RESULT classification correctly said GOOD. With the target
moving to 0.75–0.85 the band must follow the target (a `--target` argument), or it will mislabel the
fix itself.

### 13.17 Found at the start of Phase 4 — the OIT shader samples the WRONG texture (plan amended)

Execution stopped and the plan amended here before any code, per the process rule.

`transparent_voxel.frag` was **never updated for the mixed-resolution atlas**
(`docs/TextureSystemOverhaul.md`). `voxel.frag` samples two class arrays — `textureArray` (binding 1,
512 px) and `textureArrayHi` (binding 5, 1024 px), selected by bit 15 of the texture index, via
`sampleVoxelPBR`. The OIT shader still declares only `textureArray` (binding 1) and an older props
header (`textureCount, fallbackIndex, _pad0, _pad1`, where `voxel.frag` has `count512, fallbackIndex,
count1024, _pad1` — the same byte layout with different meaning), and samples
`texture(textureArray, vec3(texCoord, getTextureLayer(textureIndex)))`.

Glass is a **1024-class** material (`"resolution": 1024`), so its index has bit 15 set, fails
`texIndex >= atlasUVs.textureCount` (which is really `count512`), and **falls back to the placeholder
layer**. Whenever the OIT pass draws glass, it draws the placeholder texture tinted by the material —
never glass's own texture.

**Consequence for the design:** without this fix, §13.5's new clean texture (R5) would never reach the
screen once §13.2's routing makes OIT the only path — and the reviewer would be judging the placeholder.

**Added to Phase 4 scope:** the OIT shader samples through the **same** class-aware path as `voxel.frag`
(bit-15 class select, `textureArrayHi` at binding 5, the real `count512` / `count1024` header), shared
where possible so the two cannot drift again. **Test (L4):** with the routing fix in, the pane's own
colour must track glass's texture — checked by temporarily giving glass a strongly coloured texture and
confirming the pane's colour follows it. Red without this fix: it would not (placeholder).

This is the third time a transparency-path copy of opaque-path code has silently drifted — the render
flags (§4), the crack seed (§13.12), and now the atlas. The shared-include rule of §13.12 is extended to
texture sampling for that reason.

---

## 15. STOP — the OIT pass has never drawn anything on `main` (found 2026-09-23, start of Phase 4)

### 15.1 The finding

`shaders/transparent_voxel.frag`, first statement of `main()`:

```glsl
void main() {
    // OIT is temporarily disabled: transparent voxels now render in the opaque pass
    // (voxel.frag). Re-enable when the bloom pipeline is wired up to fix the UNDEFINED
    // layout validation error that corrupts the post-process composite.
    discard;
```

`git log -S` places that `discard` in **`7a36910f` itself** — the commit that introduced glass handling
and the bisect's lower bound. **The transparent pass has been submitted every frame since, and has
drawn nothing.** Everything after the `discard` (material-alpha blending, the Blinn-Phong highlight,
point-light specular) is dead code.

### 15.2 What this corrects

- **§12.1 / Phase 0's decision** — *"T ≈ 0 with the OIT pass running → the opaque pass occludes the
  OIT result"*: **WRONG.** The probe measured that the pass was *submitted*, not that it *drew*. There
  was nothing to occlude. The probe answered a narrower question than the one it was used for.
- **§12.10's mechanism** — *"most glass fragments were discarded from the opaque pass, leaving the OIT
  pass to draw them — transparent"*: **half wrong.** The fragments were discarded; **nothing drew them.**
  Glass was see-through because the opaque pass punched **holes** where texture alpha < 0.1
  (`voxel.frag:331`). The old texture was 61.7% holes.
- **§14.1's unexplained T = 0.667** — **explained.** T was never `1 − materialAlpha`: material alpha
  is read only by the disabled pass, so **it has never affected the picture**. T was the fraction of
  the pane that is hole — 61.7% in the source, ~0.67 after the bilinear upscale softened hole edges.
  The historical 0.500–0.573 is the same quantity at the old native resolution.
- **Why the old glass looked bad** (reviewer, §13.1): it was a hole pattern punched through an opaque
  pane, not a translucent surface.
- **§13's design — "transparent faces drawn by the OIT pass ONLY"** — would have made glass
  **invisible**: the opaque pass would discard it and the OIT pass discards everything.
- **Decision (a) "alpha 0.20 → T ≈ 0.80"** has no effect under current rendering, for the same reason.

What still stands: the bisect (the regen removed the holes), the render-flag fix (§4), the atlas-path
bug in the OIT shader (§13.17 — still real, just dormant), the crack-seed parity requirement (§13.12),
the missing-alpha guard (R7), and the reviewer's requirements R1–R7 and decisions (b) highlight,
(c) frosted cracks, no shadow.

### 15.3 Why the plan did not catch this

Every design-check pass reasoned about what `transparent_voxel.frag` *computes* — its blend, its
specular, its atlas path — and read it top-down from the declarations. None read `main()`'s first
line. The Phase 0 probe was placed at the pass's *submission*, one step short of its *output*. The
lesson for this plan and the next: **a probe must sit where the effect is, not where the cause is
dispatched.**

### 15.4 Decision for the reviewer — what renders glass

The reviewer's requirements are unchanged: transparent as much as possible (R2), visibly present (R3),
no refraction (R4), a clean texture (R5), cracks work (R6), no shadow (13.9). Three ways to meet them:

**(A) Fix what disabled OIT, then build §13 as designed.** Investigate the "UNDEFINED layout validation
error that corrupts the post-process composite" — an image-layout transition for the OIT targets that
the bloom/post chain does not perform. Real weighted-blended transparency: smooth, tinted by material
alpha, the existing highlight becomes live. **Scope unknown until investigated** — it may be one missing
layout transition, or it may be deep in the post chain. Recommended as a **bounded first step**: re-enable
OIT under `PHYXEL_VALIDATION=1`, capture the exact error, and size the fix before committing to it.

**(B) Keep glass in the opaque pass as cutout holes, with a better hole pattern.** No pipeline work.
But it is structurally what the old glass was — binary holes, which is why it looked bad — and a finer
dither pattern trades that for noise up close. Unlikely to satisfy R5 ("much cleaner").

**(C) A simple alpha-blended forward pass for glass, not OIT.** Draw transparent faces after the opaque
pass into the main colour target: alpha blend on, depth test on, depth write off, unsorted. Avoids the
OIT accumulation/reveal targets entirely — which is where the layout error lives — so it sidesteps the
blocker. Real blending, lower risk than (A). Cost: a new pipeline; overlapping panes blend out of order
(rare for voxel windows, and invisible for panes of one material).

**Recommendation:** (A)'s bounded investigation first, because it is cheap and, if the blocker is one
missing transition, it restores the pass the engine was designed around. If the investigation sizes it
as deep, fall back to (C). (B) is listed for completeness.

Every §13 item that assumed a working OIT pass (13.2 routing, 13.3 cracks, 13.11 highlight, 13.12 seed
parity, 13.17 atlas) transfers unchanged to (A) and applies equally to (C)'s new shader. §13.9 (no
shadow) and §13.13–13.14 (flag invariant, alpha guard) are independent of the choice.

### 15.5 State at the stop

Red tests written and recorded failing (not yet committed with a fix): `TransparencyTextureGuardTest`
(the 6 glass faces: 3 channels, 0% coverage; all 30 leaf faces pass), `ChunkRenderFlagsTest`
`SetLodFaces*` (transparent and mirror flags wrong after `setLodFaces`). The L4 red pass
(transmission target, shadow, crack, far) was started on the unmodified build before this was found and
is recorded separately. **No engine code or shader has been changed.**

### 15.6 DECIDED: (A) — investigate OIT first (reviewer, 2026-09-23)

**Bounded investigation, before any fix is committed to:**

1. Remove the `discard;` at the top of `transparent_voxel.frag` **locally** (not committed), rebuild the
   shaders, run the engine with `PHYXEL_VALIDATION=1`, put glass in view.
2. Capture the exact validation message(s) from the log, and a frame showing the "corrupted
   post-process composite" the comment describes — the defect in frame, per the lighting rule.
3. Read the OIT targets' lifecycle (accum / reveal images: creation, `begin/endOITRenderPass`, the
   composite that samples them, and where bloom sits in that chain) and find the missing transition.
4. **Report the size** to the reviewer: the error, the cause, the proposed change and its blast radius.
   If it is small, fix it red-first and resume §13 on OIT. If it is deep, stop and fall back to (C).

The opaque pass still draws glass solid during this investigation, so glass will not *look* transparent
yet — the investigation is about the error, not the look. Nothing here is a fix until step 4.

### 15.7 L4 red baseline — status (run on the unmodified build)

| check | result | valid red? |
|---|---|---|
| transmission, target 0.80 | cube **0.023**, subcube **0.027** — OFF-TARGET | **yes** |
| shadow (13.9) | **UNTESTABLE** — Stone roof darkens the ground only 53.9 → 50.8 | no: the sun is not reaching under the roof as the rig assumes (noon not overhead, or `timeOfDay` is not in hours) |
| crack (13.15) | FAIL, but floor **130.4** > damaged diff 116.0 | no: two *clean* panes differ by 130, so the per-pane camera moves make captures incomparable |
| far (13.12) | **UNTESTABLE** — near pane "did not build" | no: generating the far chunk appears to reset the near scene |

Three of four L4 checks are **rig defects**, found by their own controls and preconditions rather than
reported as findings. They are independent of the (A)/(C) choice and are fixed after the OIT
investigation, before any of them is used as a red.

### 15.8 Result of the bounded OIT investigation (2026-09-23)

**Method — a control and an experiment, identical scene, validation layers on** (`PHYXEL_VALIDATION=1`,
Vulkan SDK 1.4.321.1; messages land in `phyxel.log` via `VulkanDevice::debugCallback`). Scene: Bricks
backdrop + a full-cube Glass pane in view (so the OIT pass is submitted, not early-outed), ~20 s of
rendering, one screenshot. Harness: `oit_validation_run.py` (scratchpad), raw JSON per arm.

- **Control:** shaders as committed (the `discard` in place).
- **Experiment:** the `discard` commented out **locally**, `build_shaders.bat` run. Evidence the change
  took effect: `transparent_voxel.frag.spv` grew 57 864 → 63 636 bytes — everything below the `discard`
  had been compiled out as dead code.
- Afterwards: `git checkout HEAD -- shaders/transparent_voxel.frag`, `build_shaders.bat`, `git status`
  clean under `shaders/`, `shader_manifest.py --check` OK. **Nothing from the experiment is committed.**

**Validation messages attributable to OIT: NONE.** Every message, normalised for handle values, appears
with the **same count in both arms** — including the one the `discard`'s comment blamed on OIT:

| message | control | OIT on |
|---|---|---|
| `vkQueueSubmit … expects VkImage … SHADER_READ_ONLY_OPTIMAL — instead, current layout is UNDEFINED` | 9 | 9 |
| vertex attribute at location 4 not consumed by vertex shader | 9 | 9 |
| fragment input at Location 3 not output by the vertex stage | 3 | 3 |
| descriptor pool: 12 storage buffers requested from a pool of 10 | 1 | 1 |
| swapchain semaphore may still be in use | 2 | 2 |

So the "UNDEFINED layout validation error" that got OIT disabled **fires just the same with OIT
disabled** — it is not OIT's. It, and the other four, are **pre-existing and unrelated to glass**;
logged as an engine gap in `docs/StructurePipelineGaps.md` (2026-09-23) and not fixed here - out of scope.

**Composite corruption: NONE.** Pixel diff of the two screenshots (> 8/255): **65 143 changed pixels
inside the glass pane, 40 outside** — and all 40 are at x 287–288, y 885–893, the **FPS readout in the
status bar** (110 vs 112). The scene outside the pane is bit-identical.

**What the experiment DID show — §13.17, now on screen.** With OIT drawing, the pane renders the
**magenta/black missing-texture checkerboard**: the OIT shader samples the placeholder layer for
1024-class glass, exactly as §13.17 read from the code. Frames:
`screenshots/screenshot_20260923_184820_078.png` (control — opaque pale glass) and
`screenshots/screenshot_20260923_185010_518.png` (OIT on — checkerboard composited over it).

**Size: SMALL.** Re-enabling OIT is the removal of one `discard`, plus work already in the §13 scope:
the atlas path (§13.17), the opaque-pass routing (§13.2), cracks (§13.3/13.12), no shadow (§13.9). The
investigation found no new pipeline or synchronisation work.

**Per §15.6 step 4, the next step is to resume Phase 4 on OIT, red-first** — the §13 design (with
§15's corrections: decision (a)'s alpha is now meaningful, because material alpha is read by a pass that
draws) — once the reviewer has seen this size.

---

## 16. Phase 4 — built, measured (2026-09-23/24)

### 16.1 What changed

| file | change | plan § |
|---|---|---|
| `shaders/voxel_world.glsl` (new) | ONE copy of `phxWorldPosAbs`, `worldFaceUV`, atlas class-select, props index, `phxCrackStyleOf`, class-aware `phxSampleAlbedo` | 13.12, 13.17 |
| `shaders/voxel.frag` | uses the shared helpers (identical formulas); **discards transparent faces (bit 1)** with the contract note `7a36910f` wrote and something later deleted | 13.2, 3 |
| `shaders/transparent_voxel.frag` | **the `discard` is gone**; declares `vChunkBaseAbs/Rel`, binds `textureArrayHi`, corrects the atlas header, samples through the shared class-aware path, **no texture-alpha discard** (coverage is continuous), **frosted cracks** from the same field and exact world seed as stone | 15, 13.3, 13.17 |
| `shaders/shadow.vert` | reads the flags (location 3) and collapses transparent faces outside the clip volume — glass casts no shadow in any cascade | 13.9 |
| `RenderCoordinator.cpp` | the OIT draw uses **6 indices, not 36** (see 16.3) | — (found in build) |
| `Chunk.h` `setLodFaces` | refreshes the render flags | 13.13 |
| `resources/materials.json` | Glass `alpha` 0.5 → **0.02**, calibrated by measurement to the decided T (16.2) | 13.6(a), 14.1 |
| `resources/textures/source/glass_*.png` | new 1024 px RGBA texture from `tools/gen_glass_texture.py` | 13.5 |
| `editor/src/Application.cpp` | debug-mode clamp 11 → 19 (16.5) | — |
| `docs/LightingPipeline.md` | receiver rows (the OIT row had described DEAD CODE), rule R10, change log | 13.10 |

### 16.2 Results — every check, with its numbers (curve 0, exposure 1.0 unless stated)

| check | red (before) | green (after) | verdict |
|---|---|---|---|
| **R7 guard** (L2) | 6 glass faces RGB, 0% coverage | RGBA, coverage present | **PASS** |
| **LOD flag invariant** (L2) | 2 red | 8/8 `ChunkRenderFlagsTest` | **PASS** |
| **Transmission, target 0.80** (L4) | cube 0.023, subcube 0.027 | **cube 0.804** at alpha 0.02 | **ON TARGET** |
| **No shadow from glass** (L4) | R = 0.000 (glass shadowed exactly like stone) | **R = 1.000** (ground 88.8 = no-roof 88.8; stone 60.6) | **PASS** |
| **Cracks visible + frosted** (L4) | +7.44 vs floor 0.17, DARKER (stone-style) | **+30.81 vs floor 0.24, BRIGHTER** | **PASS** |
| **Pane shows glass's texture** (L4) | magenta placeholder (§15.8 frame) | green test texture: green excess **89.9** vs backdrop 4.8 | **PASS** |
| **Stone cracks unchanged** | — | identical scene, committed vs new `voxel.frag`: **90.9 / 85.0 / 4.5% / 5.8% in BOTH** — bit-for-bit the same numbers | **UNCHANGED** |
| crack/damage/flag unit suites | — | **74/74** | PASS |
| Far-from-origin crack parity (13.12) L4 | — | **DEFERRED** (16.4) — guaranteed structurally instead | deferred |

### 16.3 Found during the build: the OIT pass blended every surface TWICE

With the `discard` gone, glass still read T = 0.021 at alpha 0.5. Cause: each instance is ONE face,
drawn with the 36-index cube buffer under `cullMode NONE`. The vertex shader folds corner IDs with
`& 3`, so of the 36 indices exactly two 6-index groups form the full quad (one per winding) and four
collapse to zero area — every transparent surface was composited twice, and blending compounds per
layer. Fix: 6 indices for the OIT draw only (the shadow / reflection / mirror draws CULL and still
need both windings). T at alpha 0.5 rose 0.021 → 0.083.

### 16.4 Open — recorded, not explained, not blocking

1. **T does not follow (1−a)² for a two-surface pane.** Two points fit T ≈ 0.86·(1−a)^3.4. About 14%
   of the backdrop's change is lost regardless of alpha, and it is **not** the probe field (GI off:
   0.804 → 0.818) and **not** a shadow (R = 1.000). Calibration by measurement (§14.1) makes this
   non-blocking for the target, but a scene with two panes in line will compound differently from
   the simple model. Worth a look on its own.
2. **Far-origin L4 test deferred.** A fill at x ≈ 100 000 landed after 17 s by hand but not reliably
   inside the rig, and a coordinate sweep left the engine at 10 GB. Parity is instead guaranteed by
   construction: both passes call the single `phxWorldPosAbs` in `voxel_world.glsl`.
3. **One engine crash, not reproduced.** During the first sign-off capture the engine exited with
   nothing logged while finalising a `clear_region` over a damaged glass pane. Not reproduced in six
   attempts (4 minimal cells: glass/stone × damaged/clean; 2 exact reruns of the capture sequence).
   Cause unknown; it may or may not involve this work.
4. **The texture repeats every voxel**, so its faint smudges read as a regular dot grid across a large
   pane. Flagged to the reviewer with the frames; a per-voxel variation or a smudge-free texture are
   the options if it is judged wrong.

### 16.5 Fixed on the way: the crack debug view was dead since the merge

Main took debug mode 11 for its G-18 rasterisation probe (flat grey, returned at the top of
`voxel.frag`'s `main()`); the crack branch took 11 for the crack-field view. The merge combined both
without a textual conflict and the probe returned first. The crack view is now **mode 19**, the
editor clamp is 0–19 (which also makes main's 12–18 reachable from the editor for the first time),
and `tools/crack_seam_test.py` points at 19. Verified: mode 11 flat (stdev 0.0), mode 19 the field
(stdev 80.1). The standalone test API's clamp (`GameApiService.cpp`, 0–18) was **not** touched: that
file carries another session's uncommitted work.

### 16.6 Next: Phase 5 — the reviewer's visual sign-off

Frames (shipped look: AgX, exposure 8, GI on): clean pane over bricks, the same pane angled, right
half cracked at 0.45, cracked close-up at 0.90, and the stone control. **Glass is not fixed until the
reviewer says so.** Still owed after sign-off: the pane in a generated building (§13.7).

---

## 17. Faces hidden by glass — opaque faces behind transparent neighbours are culled (plan, 2026-09-24)

**Status: IN PROGRESS. Design-checked twice (§17.12). Reviewer go-ahead 2026-09-24; D2 = option A
(fix the sub/micro border seam here, §17.5b). Rollback point: tag `glass-s17-start`.**

### 17.1 The defect, in the reviewer's words

> *"Since glass is transparent the voxel faces of non-transparent voxels that sit beside glass and
> would normally be obscured (and not rendered), should actually be seeable. Whatever code does face
> culling based on faces touching, needs an exception for transparent material."*

Every mesher in the engine drops a voxel face when the neighbouring cell is occupied — correct when
the neighbour is opaque, wrong when it is glass. With glass now genuinely transparent (§16), the
omission is visible: the stone faces lining a window opening (its "reveal") are not meshed, so
looking through the pane at an angle shows **missing geometry** where the reveal should be. The same
holds for any opaque voxel against any glass voxel, at every voxel size and across chunk borders.

### 17.2 The rule

A face of voxel **A** pointing at neighbour cell **B** is **hidden** iff

    B is occupied  AND  ( B is opaque  OR  A is transparent )

| A (face owner) | B (neighbour) | face drawn? | why |
|---|---|---|---|
| opaque | empty | **yes** | unchanged |
| opaque | opaque | no | unchanged |
| **opaque** | **transparent** | **yes — THE FIX** | it can be seen through B |
| transparent | empty | yes | unchanged |
| transparent | **transparent** | **no** | the interior faces of a thick pane. Drawing them would stack OIT layers inside the pane — with blending compounding per layer (§16.3), a 3-thick wall would read far more opaque than a 1-thick one, and every internal face would show its own smudges. Must stay culled. |
| transparent | opaque | no | the glass face lies on the stone's surface; the stone face is what is seen through the pane, and a glass face there would only tint it a second time |

**One consequence, recorded not solved:** two DIFFERENT transparent materials touching hide each
other's faces (row 5). Only one transparent material exists (`Glass`), so this cannot occur today;
coloured glass (a possible follow-up) would make it a question.

### 17.3 One definition of "transparent"

The engine already has FIVE independent copies of the same test, all `alpha < 0.99`:
the instance transparent bit (`ChunkRenderManager.cpp:388` cubes, `:1084` subcubes, `:1244`
microcubes), `Chunk::recomputeRenderFlags`, and `Chunk::computeVisibilityMask`. This change would add
more. **Add one helper and use it everywhere this change touches:**

    // MaterialRegistry.h
    inline bool isTransparentMaterial(const MaterialDef* m) { return m && m->alpha < 0.99f; }

The existing copies are refactored onto it in the same commit **only where this change already
edits the line**; the rest are listed for a follow-up rather than widening the blast radius.

### 17.4 Inventory — where "is the neighbour solid?" is asked, and what each must do

Found by reading the code on 2026-09-24. **CHANGE** = must adopt the §17.2 rule.
**KEEP** = must go on treating glass as solid. Line numbers are as of `e995caad`.

#### Must CHANGE (rendering)

| # | Where | Asks | Data available | Required change |
|---|---|---|---|---|
| **C1** | `ChunkRenderManager.cpp:618` lambda `neighborSolid`, used for **cube face emission** at `:760` | in-chunk: `solidVis[cell]` (= ANY visible cube, glass included) | the neighbour's material: `matFaces[cellMat[n]].reserved & 2` is its transparent bit (computed at `:388`, packed at `:393`); the face owner's material: `cellMat[cell]` | new predicate **`faceHiddenByNeighbor(x,y,z, selfTransparent)`** = `solidVis[n] && (!transparent[n] \|\| selfTransparent)` |
| **C2** | same lambda, **out-of-chunk** branch → `getNeighborCube` → `ChunkManager.cpp:530` → `Chunk::visibleSolidCubeAt` | across a chunk border: occupied? (bool) | nothing about material — the callback returns only a bool | the lookup must report **empty / opaque / transparent** (17.5) |
| **C3** | `ChunkRenderManager::subCellSolid` (`:945`): per-face subcube path `:1010`, merged path faces `:1543` | `cubeCellSolid` (parent cube) or `m_subOcc` contains the subcube | `m_subOcc` is **occupancy only** — no material | companion set **`m_subTransparent`** (keys of transparent subcubes), filled in `buildSubMicroOccupancy` (`:903`); parent-cube test uses the cube cell's transparency from C1; the predicate takes `selfTransparent` |
| **C4** | `ChunkRenderManager::microCellSolid` (`:952`): per-face micro path `:1181`, merged path faces `:1395` | cube, then parent subcube, then `m_microOcc` | occupancy only | companion **`m_microTransparent`**; same shape as C3 |
| **C5** | leaf **foliage exposure** — cube leaves `:643-645`, merged subcube leaves `:1489`, merged microcube leaves `:1328` | "is this billboarded leaf exposed on any side?" via the same predicates | as C1/C3/C4 | follow the **new** rule: a leaf behind glass is visible, so it must emit its foliage cards |
| **C6** | `ChunkManager::isChunkCapped` (`:459`), used at `:509` to SEAL a uniform-solid chunk (skip meshing its boundary wall) | uniform neighbour: `ns.visible(0)`; dense: `visibleSolidCubeAtIndex` over the facing layer | the neighbour's store material (`ChunkVoxelStore::material(idx)`) | "capped" requires the capping cell to be **opaque**. A glass layer must not seal the wall behind it |
| **C7** | `Chunk::rebuildFaces` (`Chunk.cpp:330`), the one function every re-mesh passes through; plus the uniform short-circuit (`ChunkManager.cpp:504–512`) | — | the chunk's own border cells | border-class **signature ripple** (§17.6): a changed border class re-meshes the facing neighbour. Also `markChunkForRemesh` after the template stamp's immediate rebuild (`ObjectTemplateManager.cpp:798`, `:1245`) |
| **C8** | `neighborSolid`'s out-of-chunk branch (`ChunkRenderManager.cpp:621`), which after C2 receives an enum | occupied? | the new enum | must return `!= NeighborOccupancy::Empty`, so **K2/K3 (grass) keep treating glass as cover across a border**. Only `faceHiddenByNeighbor` applies the new rule (T10b) |
| **C9** | sub/micro out-of-chunk branches (`:1008`, `:1176`, merged `:1543`, `:1395`) | nothing (assume exposed) | — | **decision D2**: option A = fine border lookup (§17.5b); option B = defer + gap entry |

Where the face owner's own transparency comes from, per path: cube — `cellMat[cell]`; per-face
subcube/microcube — the voxel's material is already resolved at `:1084` / `:1244`; merged
subcube/microcube — the cell's `Subcube*` / `Microcube*` is in hand (`grid[lx][ly][lz]`, `cells[ci]`)
at the point the neighbour test is made.

#### Must KEEP (glass stays solid)

| # | Where | Why it must not change |
|---|---|---|
| **K1** | `Chunk.cpp:797-949` — all ten `visibleSolidCubeAt` callers: `createChunkPhysicsBody`, `updateChunkPhysicsBody`, `forcePhysicsRebuild`, `createCubeCollisionShape`, `addCollisionEntity`, `batchUpdateCollisions`, `hasExposedFaces`, `buildInitialCollisionShapes`, `updateNeighborCollisionShapes`, `endBulkOperation` | **physics**. Glass is a solid you cannot walk through. This is exactly why the fix must NOT go into `visibleSolidCubeAt` itself, even though C2 reaches it |
| **K2** | `ChunkRenderManager.cpp:672` — `if (neighborSolid(x, y + 1, z)) continue; // top face covered → no grass` | **grass sprouting** is about physical cover. Under the new rule a glass cube sitting on grass would count as "not covering", and blades would grow INSIDE the glass voxel. The grass tests keep the OLD predicate — which is why C1 introduces a NEW predicate instead of editing `neighborSolid` |
| **K3** | `ChunkRenderManager.cpp:694` — grass edge taper `grassyColumn` ("covered top = a wall face") | same reason as K2 |
| **K4** | `ChunkManager.cpp:578` — skylight roof scan | lighting, currently a uniform placeholder (`089ff2cb`). Whether a glass roof should roof a column is a real question when skylight returns — **recorded, not decided here** |
| **K5** | `Chunk::computeVisibilityMask` | chunk occlusion graph — **already correct**: `if (mat && mat->alpha < 0.99f) continue; // transparent → air (see-through)`. It is the precedent for this whole change |
| — | `ChunkRenderManager::isCubeFaceVisible` (`:1822`) | dead stub (`return true`, "not used"); untouched |

#### DEFERRED, with the reason

| # | Where | Why not now |
|---|---|---|
| **D1** | `LodChunkMesh.cpp:139` — `if (volume.atClamped(nx, ny, nz).solid()) continue;` | the LOD mesher writes `inst.reserved = 0` (`:148`): **LOD draws glass OPAQUE** — no transparent bit at all. Applying the culling rule there alone would add faces hidden behind opaque LOD glass: cost with no visible effect. LOD glass is its own pre-existing defect, logged in `docs/StructurePipelineGaps.md`; distance-driven chunk LOD is default-OFF. Both fixes belong together, later |

### 17.5 The cross-chunk lookup (C2) — the interface change

Today: `using NeighborLookupFunc = std::function<bool(const glm::ivec3& worldPos)>;` meaning
"occupied?". It has **exactly one real supplier** (`ChunkManager.cpp:530`, the cross-chunk rebuild at
`:590`); every other `rebuildFaces()` passes `nullptr`, which treats chunk borders as exposed and
stays unchanged.

**Change:** the callback returns an occupancy class instead of a bool:

    enum class NeighborOccupancy : uint8_t { Empty = 0, Opaque = 1, Transparent = 2 };
    using NeighborLookupFunc = std::function<NeighborOccupancy(const glm::ivec3& worldPos)>;

supplied by a new **`Chunk::renderOccupancyAt(localPos)`**, built on `visibleSolidCubeAtIndex` plus
`isTransparentMaterial(store material)`. `visibleSolidCubeAt` itself is **not modified** (K1).

*Why one enum callback, not a second bool callback:* two callbacks mean two lookups per border cell
and two things that can disagree. One call answers both questions and cannot drift.

**Signatures touched (compile-checked, no behaviour change on the `nullptr` paths):**
`ChunkRenderManager.h:36` (the type), `:150`, `:161`, `:350` (parameters), `Chunk.h:307/312`,
`Chunk.cpp:330`, `ChunkManager.cpp:530`.

**Cost:** the supplier already reads each border cell's occupancy; adding a material lookup matches
what `computeVisibilityMask` already does for all 32 768 cells of every rebuild — here it is the
6 × 1 024 border cells only.

### 17.5b Sub/micro faces at chunk borders (C9, decision D2)

**Today.** The sub- and microcube meshers have **no** cross-chunk lookup. A face whose neighbour
sub/micro cell lies outside the chunk is always drawn: per-face subcube `ChunkRenderManager.cpp:1008`
("out of chunk: assume exposed"), per-face micro `:1176`, merged paths `:1543` / `:1395`. Behind an
opaque face that is cost only. Between **two glass** sub/micro cells across a border, both faces are
drawn and OIT stacks them. That is a visible seam in a sub-voxel pane, a pre-existing chunk-independence
violation. It matters because generated windows are 1-micro Glass panes (`StructureRealizer.cpp:395`).

**D2 — the reviewer decides:** (A, recommended) fix it here as C9, below; or (B) defer, log it in
`docs/StructurePipelineGaps.md`, and mark T8's transparent assertion expected-red.

**C9 design (option A).**

1. **Interface, one callback for both fine levels:**

       // level: 1 = subcube cell, 2 = microcube cell. worldMicro = worldCube*9 + sub*3 + micro
       // (for level 1, micro = 0 — the sub-cell's minimum corner).
       using NeighborFineLookupFunc =
           std::function<NeighborOccupancy(const glm::ivec3& worldMicro, int level)>;

2. **Semantics mirror the in-chunk predicates exactly,** so a border cell answers the same as an
   interior one, which is what T8's equality needs.
   - level 1 = `subCellSolid` (`:945`): the cube is solid, or that subcube exists. Microcubes do NOT
     fill a sub-cell.
   - level 2 = `microCellSolid` (`:952`): the cube, or the parent subcube, or that microcube.
   - The returned class is the class of whichever voxel answered (cube → store material; subcube /
     microcube → its own material), through `isTransparentMaterial`.
3. **Supplier:** new `Chunk::renderOccupancyAtFine(localMicro, level)`, all O(1): the store for the
   cube (`visibleSolidCubeAtIndex` + store material), then `getSubcubeAt` (`Chunk.h:210`), then
   `getMicrocubeAt` (`Chunk.h:216`), skipping broken or invisible voxels as `buildSubMicroOccupancy`
   does (`:903–935`). `ChunkManager` builds the callback next to `getNeighborCube`
   (`ChunkManager.cpp:530`), with the same last-chunk memo.
4. **Plumbing:** `Chunk::rebuildFaces` and `ChunkRenderManager::rebuildAllFaces` get one more
   parameter (default `nullptr`, so every null-lookup caller is unchanged); it is stored for the
   rebuild's duration the way `getNeighborCube` is. The four out-of-chunk branches call it instead of
   `faceVisible = true`, then apply the §17.2 rule with the face owner's transparency.
5. **Ripple:** covered by C7. The border signature already hashes subdivided border cells.
6. **Cost:** only sub/micro faces on the border plane query it, two hash lookups each.
7. **Effect on T8:** with C9, sub/micro border faces follow the same rule as interior ones, so T8's
   sub/micro assertion **tightens from ⊇ to equality**, and the transparent-face assertion goes green.

### 17.6 Chunk independence (design key) and re-mesh ripple

Appearance must not depend on where chunk borders fall. A stone voxel at x = 31 next to glass at
x = 32 (another chunk) must show its +X face exactly as it would if both sat inside one chunk. C2 is
what delivers that.

**Which rebuild uses the cross-chunk lookup (established by the design check, not assumed).** Every
dirty-chunk rebuild goes through `rebuildChunkFacesWithCrosschunkCulling`: the dirty-tracker callback
(`ChunkManager.cpp:169`), `updateChunk` (~`:410`) and `rebuildChunkFaces` (~`:451`). So C2 is the
steady state for every chunk. Two paths rebuild with a **null** lookup: an edit's immediate local
rebuild and `finalizeLoadedChunk` (`:1176`). With no lookup, border faces are treated as exposed and
**drawn**. That is extra cost until the next dirty rebuild, never a missing face. So a stale border has
exactly one cause: **the neighbour chunk is never marked for re-mesh.**

**The rule this change enforces:** *any change to a border cell's render class (empty / opaque /
transparent, at any voxel size) marks the facing neighbour chunk for re-mesh.* This uses
`markChunkForRemesh`, the mesh-only tier. It is not `markChunkDirty`, because the neighbour's voxel
data did not change, and a DB-dirty flag would make the evictor re-save it (the reason given at
`DamageSystem.cpp:222`). It is also not the idle tier the light ripple uses (`ChunkManager.cpp:593`):
a stale border here is a **hole**, not a cosmetic light seam.

**Edit-route matrix (from the code, 2026-09-24, design-check passes 1 and 2).** Border cell = local
coordinate 0 or 31 on the axis facing the neighbour.

| route | where / used by | neighbour re-meshed today? |
|---|---|---|
| R1 `VoxelManipulationSystem` place/break → `updateAfterCubePlace/Break` → `FaceUpdateCoordinator` | player place/break, `VoxelForceApplicator` | ✅ (`FaceUpdateCoordinator.cpp:120–170`) |
| R2 `ChunkVoxelModificationSystem::addCubeWithMaterial`, `addSubcube/MicrocubeWithMaterial`, legacy add/remove (`:94–188`) | API single-voxel place; game-definition `fill` (cube `:657`, sub/micro `:799/:804`) | ✅ (calls the update callbacks) |
| R3 `removeCubeFast` (`:44`) | `clear_region` job (`Application.cpp:18890`), fill-with-replace (`:18815`), editor remove (`:16235`), **damage breaks** (`DamageSystem.cpp:286,386,505`), game-definition `replace` (`GameDefinitionLoader.cpp:654`) | ❌ own chunk only |
| R4 `addCubeFast` (`:62`) | `fill_region` job **without a material** (`Application.cpp:18821`); a fill WITH a material calls `addCubeWithMaterial` (R2, `:18819`) | ❌ own chunk only |
| R5 `ChunkVoxelManager::addCube(overwrite=true)` (`:540`) | `Chunk::addCube(...,overwrite)` pass-through; no production caller found. Sets `needsUpdate` only; the **caller** owns the re-mesh | ❌ chunk-local |
| R6 **template / structure stamp** (`ObjectTemplateManager.cpp:556–565`, `:764–799`: `chunk->addCube/addSubcube/addMicrocube`, then a direct `rebuildFaces()` with **no lookup and no dirty mark**; also `:1245`) | `spawn_template` static, **structure generation** (generated glass windows are 1-micro Glass panes, `StructureRealizer.cpp:361–395`) | ❌ own chunks only, **and never rebuilt with the cross-chunk lookup** |
| R7 `PlacedObjectManager::clearRegion` (`:590–608`: `removeCubesBatch` + `clearSubdivisionAt` + `markChunkDirty(own)`) and `seatStructure` steps (`:554–561`) | structure replace/remove, seating | ❌ own chunk only |
| R8 `SettlementBuildService` (6 × `markChunkDirty(ch)`: `:382,711,1084,1308,1369,1456`) | settlement build | ❌ own chunk only |
| R9 `DynamicObjectManager` / `ChunkVoxelBreaker` / `ChunkVoxelManager` edit callbacks (`m_rebuildFaces()`) | subdivide, breaks into dynamics | ❌ own chunk only (null-lookup rebuild) |
| R10 chunk **load** / stream-in (`ChunkManager.cpp:114–122`) | streaming | ✅ idle re-mesh of the 6 adjacent chunks |
| R11 LOD → fine return (`RenderCoordinator.cpp:2982`), damage-stage re-mesh (`DamageSystem.cpp:228`) | LOD, cracks | no ripple needed: render classes unchanged |

**R3–R9 are partly a pre-existing bug.** Today, removing a border voxel through any of them leaves the
neighbour's facing face culled, a visible hole, until something else re-meshes that chunk. §17 makes it
worse in two ways. (a) A shattered glass pane on a border no longer reveals the neighbour's glass face.
(b) Placing glass beside the neighbour's glass leaves the neighbour's facing face drawn, so the two
layers stack and the smudges double.

**Why not patch the routes (the first draft's C7).** Seven routes bypass the neighbour today, and any
new route would reopen the bug. Fix it once instead, at the one function **every** re-mesh goes
through: `Chunk::rebuildFaces` (`Chunk.cpp:330`). Null-lookup and cross-chunk rebuilds both land there,
and it already refreshes the other derived state (`recomputeRenderFlags`, `computeVisibilityMask`).

**C7 — border-class signature ripple (the mechanism):**

1. **Signature.** `Chunk` gets `uint64_t m_borderSig[6]` + `bool m_borderSigValid`. For face `d`, the
   signature is a 64-bit hash (FNV-1a or `std::hash` combine) over that face's 32×32 border cells, in
   fixed order. For each cell it hashes: the cube's render class (`renderOccupancyAt`: Empty / Opaque /
   Transparent), and, **if the cell is subdivided**, the render class of each subcube and microcube in
   it, in fixed order. It hashes the whole cell, not only the part touching the face: this over-triggers
   a little, never under-triggers. Hashing uses the render **class**, not the material, so a stone →
   brick swap does not ripple.
2. **Where.** At the end of `Chunk::rebuildFaces(...)`, after `computeVisibilityMask()`: compute the
   new six. If `m_borderSigValid` and face `d` differs, set bit `d` of a change mask. Store, and set
   valid. Also compute it where the managed rebuild short-circuits a uniform chunk
   (`applyAirRenderState` / `applySealedRenderState`, `ChunkManager.cpp:504–512`), which skips
   `rebuildFaces`. For a uniform store the signature is O(1): one class repeated.
3. **First build never ripples** (`m_borderSigValid == false`). Otherwise the first mesh of every
   chunk on load would re-mesh its whole neighbourhood, and they theirs, which cascades. Neighbours of a
   newly loaded chunk are already re-meshed by R10.
> **Items 4–5 AMENDED in execution (2026-09-24), before C7 was coded. Superseded text kept below.**
> Chunks join `chunkMap` through direct map writes in two other classes, so a per-chunk sink set at
> "every insertion site" would have to be remembered by every future site. The replacement needs no
> wiring and adds no threading. **4′.** `Chunk` ORs the faces whose signature changed into a
> pending 6-bit mask (`m_pendingBorderRipple`), and never touches its neighbours. **5′.**
> `ChunkManager::rebuildChunkFacesWithCrosschunkCulling` (the managed rebuild) consumes that mask
> at its end, including its uniform short-circuits: for each bit, `getChunkAtCoord(coord + dir)` →
> `markChunkForRemesh`. That is the same place, and the same thread, where the existing light
> ripple already marks neighbours (`ChunkManager.cpp:600–608`). A direct null-lookup rebuild
> (R6/R9) records its change in the mask, and the change is delivered at that chunk's next managed
> rebuild. That is why item 7's rule is "every edit ends in a MANAGED re-mesh" (a dirty or remesh
> mark). Observability for T14: `Chunk::rebuildCount()` and `ChunkManager::borderRippleCount()`
> (monotonic).
>
4. *(superseded)* **Hand-off, thread-safe.** Chunk does not touch its neighbours. If the mask ≠ 0, it calls
   `m_borderSink(chunkCoord, mask)`, a `std::function` set by the owning `ChunkManager`. The sink
   pushes into a `std::mutex`-guarded `std::vector<std::pair<glm::ivec3,uint8_t>>` on `ChunkManager`.
   `ChunkManager::updateDirtyChunks` (main thread) drains it first: for each set bit,
   `getChunkAtCoord(coord + dir)` → `markChunkForRemesh`. A chunk with no sink (tests, preview chunks)
   simply doesn't ripple.
5. *(superseded)* **Sink wiring.** Set at every site where a chunk joins `chunkMap`: `ChunkInitializer.cpp:43`,
   `:76`; `ChunkStreamingManager.cpp:212`, `:620`, `:793`. Guard: the drain asserts, in Debug, that the
   chunk it re-meshes has a sink, so a sixth insertion site added later is caught.
6. **Convergence.** The neighbour's re-mesh recomputes **its own** signature, which describes its own
   content. That content did not change, so its signature does not change and the ripple stops after
   one hop.
7. **The one route rule left:** *every edit must end in a re-mesh of the edited chunk.* R1–R9 all do
   (dirty mark, remesh mark, or direct `rebuildFaces`); R5's callers do it for R5. A route that broke
   this rule would already show its own edit late, so this is not a new failure mode. R6/R9's direct null-lookup rebuilds
   still need a cross-chunk rebuild eventually, or their **own** border faces stay over-drawn forever
   (cost only; the ripple fixes the neighbour, not them). So R6 (`ObjectTemplateManager.cpp:798`,
   `:1245`) also gets `markChunkForRemesh(chunk)` after its immediate rebuild.
8. **Cost.** 6 × 1 024 class reads per rebuild. The same order as `isChunkCapped`'s dense scan, and
   ≤ 20% of the 32 768-cell `computeVisibilityMask` already run there, plus subdivided border cells.
   Measured in step 8 against the T0 mesh-timing instrumentation (`ChunkRenderManager.cpp:234`); a
   rebuild-time rise over 5% on the showcase is a finding.
9. **R5 decision:** `overwrite=true` has no production caller. Keep it: it is covered by C7 like
   every other route, so no special case.

**Sealing** (C6) is re-decided on every managed rebuild (`ChunkManager.cpp:504–512`), so C7's
neighbour re-mesh is also what unseals a chunk when the layer capping it turns to glass (T12b).

### 17.7 Cost

Extra faces appear **only** on opaque faces that touch glass, at most one per touching face — bounded
by the glass's surface area. The greedy-merge key is unchanged, so these faces merge like any other.

**Count covered cell-faces, not quads.** Cube faces are always greedy-merged
(`ChunkRenderManager.cpp:311`, `:851`), and sub/micro faces are merged when fine merge is ON. Adding
faces can therefore *lower* the quad count: filling the holes in a ground plane under a pane joins runs
that were split. A quad-count delta cannot be predicted exactly, so the exact accounting is by covered
unit faces (`coveredCellCentres`, the T6 helper).

**L2, exact.** For each test layout, the test computes
`P = #{(opaque cell face F) : the cell across F is transparent}`, at the finer of the two resolutions
(a cube face touching a glass subcube counts its 3×3 = 9 subcube-sized cells; only the ones touching
glass count). Assertion: `covered_after − covered_before == P` **exactly**. Every added covered face
must lie on an opaque cell and point at a transparent cell. This catches both under-emission (a reveal
face missing) and over-emission (a face added where the rule did not ask for one). Run with fine merge
ON and OFF; the covered sets must be identical (T6).

**L4, predicted from the world, before measuring.** The prediction is **computed by the rig from a
scan of the rig world**, not from memory of the scene. Design-check pass 2 caught exactly that
mistake: the first draft predicted +15 faces for "ground under the panes", and a live query showed no
voxel below the panes at all (`(4|12|20, 0…16, 8)` all empty, 2026-09-24). The rig script:

1. queries `/api/world/voxel` for the 6 neighbours of every glass cell in the scene;
2. computes `P` = the number of (opaque neighbour cell, face pointing at glass) pairs;
3. **prints `P` and the list of pairs before any capture.**

Hand derivation for today's showcase (§16.6), to be confirmed by step 1: panes A–C touch no opaque
cell (air all round, bricks 5 cells behind) → 0. Window D (3×4 glass cubes in a 1-thick stone wall,
x 27–29, y 19–22, z 8) → reveal left 4 + right 4 + top 3 + bottom 3 = **P = 14**. **Prediction: covered
static unit faces +14 exactly; quad count +4…+14.** Each reveal side is one straight run: 4 quads if
fully merged, up to 14 if light breaks the merges.

**Measurement route (new, step 10):** `GET /api/debug/chunk_faces?cx=&cy=&cz=`. It returns
`{chunk:[cx,cy,cz], rebuilds:<the chunk's rebuild counter>, quads:<n>, covered:{<material>:{<faceDir>:<unit faces>}}}`.
It is computed from the chunk's current instance buffer through the same covered-cell expansion T6
uses. That expansion is `coveredCellCentres`, today a helper **inside
`tests/graphics/FineFaceMergeTest.cpp`**. It moves into the engine (e.g. a free function next to
`ChunkRenderManager`), so the test and the route share one implementation and L2 and L4 count the
same way. It echoes the chunk and rebuild counter it
read, so a caller can tell whether the re-mesh has landed. The counter is added to `Chunk` if none
exists; it increments in `Chunk::rebuildFaces`. Read-only, no state change, no defaults.

**L4b — a generated window (the path real content takes).** One v2 structure whose program marks a
window `"infill": "glass"` (`BuildingProgram.cpp:51` parses it; `StructureRealizer.cpp:370–395` turns
it into a 1-micro Glass leaf between trim jambs). Built through the engine's generator, never
hand-placed (CLAUDE.md provenance rule). If the build route cannot pass that flag, that is a logged
gap, and the rig says which route produced the building. Prediction: the jambs' and sill's micro faces
facing the leaf become covered. `P` is computed the same way as above, from a `scan_region` of the
window at micro resolution, and printed first. Capture: through the pane at a grazing angle, before and
after, same pose, curve 0. **Control:** the same window with `"infill": "open"` (air), where the jamb
faces are drawn today. This covers the owed "pane in a generated building" check (§13.7).

### 17.8 Tests — red first

**L2, new `ChunkTransparentCullingTest`** (the `FineFaceMergeTest` shape — build a chunk, run the
mesher, count emitted faces by face direction and material):

| test | setup | expect | red today? |
|---|---|---|---|
| T0 control | stone next to stone | the shared faces NOT emitted | green (control) |
| T0b control | stone next to air | face emitted | green (control) |
| **T1** | stone cube next to glass cube | stone's face toward glass **emitted** | **RED** |
| **T2** | glass cube next to glass cube | shared faces NOT emitted (no layer stacking) | green — must STAY green |
| **T3** | glass cube next to stone cube | glass's face toward stone NOT emitted | green — must STAY green |
| **T4** | stone subcube next to glass subcube; stone subcube next to glass CUBE | stone subcube face emitted | **RED** |
| **T5** | same, microcubes | stone microcube face emitted | **RED** |
| **T6** | T1/T4/T5 with fine greedy merge ON and OFF | identical covered-cell sets (`coveredCellCentres`) | — |
| **T7** | two chunks, stone at x = 31 in A, glass at x = 0 in B, real cross-chunk lookup | A's +X face emitted; with stone in B instead, NOT emitted | **RED** |
| **T8** | **chunked vs whole-region equality** (design key; `FloraMarginTest` shape): a stone/glass pattern (cubes, subcubes, microcubes, mixed) built straddling a chunk border vs the same pattern inside one chunk, covered sets compared after translation | **cubes: identical.** **Sub/micro: straddling ⊇ whole.** Every face the whole-region mesh covers, the straddling mesh also covers; extra faces are allowed only on border cells (see note) | **RED** |
| **T9** | **edit-route matrix (§17.6):** for each of R1–R9 that a unit test can drive (R1–R5, R6 template stamp, R7 placed-object clear; R8 settlement is covered at L4), on a border cell with the neighbour chunk loaded: (a) remove a stone cell whose neighbour is stone, (b) remove a glass cell whose neighbour is glass, (c) place glass beside the neighbour's glass, (d) place stone beside the neighbour's glass. Then run the dirty pass | the neighbour's facing face matches a from-scratch cross-chunk rebuild of both chunks, for every route × case | **RED** for R3–R7 (they re-mesh only the edited chunk); R1/R2 green controls |
| **T10** | grass cube with a glass cube on top | **no grass blades** emitted (K2 unchanged) | green — must STAY green |
| **T10b** | grass cube at local y = 31, glass cube at y = 0 of the chunk above, real cross-chunk lookup | **no grass blades** (C8) | green — must STAY green |
| **T11** | billboarded leaf next to glass, otherwise enclosed | leaf **exposed** → foliage emitted (C5) | **RED** |
| **T12** | uniform stone chunk whose neighbour's facing layer is all glass | `isChunkCapped` = **false** (C6) | **RED** |
| **T12b** | uniform stone chunk, **sealed** by an all-stone neighbour layer. Then one capping cell is replaced by glass (remove + add) through R1 and through R3 + R4 | after the dirty pass the chunk is **unsealed** and its wall face under the glass cell is covered | **RED** (C6 + C7) |
| **T13** | glass cube | still a physics solid: `visibleSolidCubeAt` true, collision shape built (K1 unchanged) | green — must STAY green |
| **T14** | C7 convergence and gating: (a) first build of a chunk → no ripple; (b) re-mesh with unchanged content (the LOD return, a damage stage) → no ripple; (c) stone → brick swap on a border (same class) → no ripple; (d) stone → glass on a border → exactly the facing neighbour is marked, once | as stated | (a)–(c) green, (d) **RED** |
| **T15** | (D2 option A only) 1-micro glass pane straddling a chunk border, plus a stone jamb microcube across the border beside a glass microcube | no glass face covered on the border plane between glass micros; the jamb face covered | **RED** |

**T8 note: why sub/micro is ⊇, not =.** The sub- and microcube meshers treat a neighbour outside
the chunk as exposed (`ChunkRenderManager.cpp:1008`, `:1176`); they have no cross-chunk lookup. So a
sub-voxel pattern that straddles a border draws extra faces on the border layer. Behind an **opaque**
face those extra faces are hidden: cost only. Cube faces do use the lookup (C2), so cubes are held to
exact equality.

**Exception, and it is visible: glass sub-voxel against glass sub-voxel across a border.** Both
facing glass faces are drawn, and OIT stacks them. A sub-voxel glass pane that straddles a chunk
border would show a darker, doubled line at the border: **a visible chunk seam**. This is
pre-existing (the same "assume exposed" rule applied before §17) and is independent of this change.
T8 therefore gets a **third, separate assertion**: *no transparent face is covered in the straddling
mesh unless the whole-region mesh covers it.* It is expected RED today for sub/micro glass.

**Decision D2** (fix here, or defer) and its design: §17.5b.

**L4, live:** the showcase window (§16.6, stone wall with a glass window). Camera at a grazing angle
through the pane; the reveal's stone faces must be present. Metric: the reveal's pixel region reads as
stone (luminance/texture variance of stone) instead of sky, **with a control** — the same pose with the
window cell empty (the reveal is drawn there today, since air does not hide faces). Before/after at the
identical pose, curve 0. Then the **reviewer's visual sign-off**.

### 17.9 Validation depth

L2 (face emission is a structural property of the real mesher output — measured, not inferred), L4
(the rendered reveal), and human sign-off. Stress axes: thickness (1-, 2-, 3-thick panes: interior
faces must stay culled at every thickness — T2 extended), voxel size (cube / subcube / microcube,
mixed), and the chunk border (T7/T8).

### 17.10 Order of work

Each step ends with the named tests green and **every previously-green test still green**. Each step
is its own commit, so any step can be reverted alone. Starting point: tag `glass-s17-start`.

1. `isTransparentMaterial` helper (17.3).
2. Write T0–T15 (T15 only under D2 option A); confirm the RED ones fail for the stated reason and the
   controls/KEEPs pass. Commit the reds.
3. **C1 + C2 + C8 together** (cube rule in-chunk AND across borders: `NeighborOccupancy` enum,
   `Chunk::renderOccupancyAt`, `neighborSolid` maps to `!= Empty`) → T1, T2, T3, T7 green; T8
   cubes stay green; T10/T10b green. *Amended 2026-09-24, found in execution:* C1 alone turned
   T8 cubes /1 and /2 red. The in-chunk rule changed while the border branch kept the old one, so
   a stone face behind glass was drawn or not depending on where the chunk border fell. The cube
   rule is chunk-independent only if both branches change in the same commit.
   *Execution note:* steps 3 and 4 landed as ONE commit. Step-4 edits were started in the same two
   files before step 3 was committed, and the hunks could not be split non-interactively. Each
   step's test results are still recorded separately in §17.13.
4. C3, C4 (sub/micro, in-chunk, per-face and merged) → T4, T5, T6 green. *Amended in
   execution:* the sub/micro half of C5 (leaf exposure) moves into this step. The per-face paths
   derive leaf exposure from face visibility, so they adopt the new rule the moment C3/C4 land.
   Leaving the merged paths' exposure for step 8 would make leaves differ between fine merge ON
   and OFF in between.
5. (merged into step 3)
6. D2 as decided. **A:** C9 (§17.5b) → T8's sub/micro assertion tightened to equality, the transparent
   assertion and T15 green. **B:** gap entry; T8's transparent assertion marked expected-red with
   the gap's date.
7. C7 (border signature ripple + sink wiring at the five `chunkMap` sites + the template-stamp
   re-mesh mark) → T9 (every route × case), T14 green.
8. C5 (leaf exposure) → T11. C6 (`isChunkCapped` requires opaque) → T12; with C7 → T12b.
9. Full crack/damage/flag/merge/lighting suites; K1–K3 tests (T10, T10b, T13) still green; rebuild
   time on the showcase within 5% (§17.6 C7 item 8).
10. `coveredCellCentres` moved into the engine; `GET /api/debug/chunk_faces` (§17.7).
11. L4: showcase reveal (rig prints `P` first; prediction +14 covered / +4…+14 quads) with the
    empty-window control, then L4b, the generated glass window, with the open-window control.
    **Then the reviewer's visual sign-off.** Nothing here is "fixed" before that.
12. D1 logged in the gap file (done 2026-09-24).

### 17.11 Risks

| risk | guard |
|---|---|
| grass starts growing inside glass | K2/K3 keep the old predicate; T10 |
| grass grows under glass at a chunk border | C8; T10b |
| glass becomes walk-through | K1 untouched; T13 |
| thick panes get interior layers (looks opaque, smudges stack) | row 5 of the rule; T2 extended to 3-thick |
| cross-chunk faces go stale after a border edit, by any route | C7 at the one choke point (`Chunk::rebuildFaces`); the T9 route matrix |
| a new edit route added later bypasses the ripple | C7 does not depend on routes, only on "every edit ends in a re-mesh" (§17.6 item 7) |
| the ripple cascades through the world on load | first build never ripples (§17.6 item 3); T14(a) |
| the ripple loops | a signature describes only the chunk's own content; T14(b) |
| C7 touches chunks from a worker thread | the sink only enqueues under a mutex; the main thread drains (§17.6 item 4) |
| a chunk inserted without a sink silently never ripples | the Debug assert in the drain (§17.6 item 5) |
| C7 floods the re-mesh queue on large fills | only changed border faces ripple; mesh-only tier; the queue de-duplicates (`DirtyChunkTracker.cpp:58`) |
| C7 slows every rebuild | measured in step 9, 5% budget |
| a doubled glass seam at chunk borders (sub-voxel glass) | decision D2; T8's transparent assertion, T15 |
| a chunk behind a glass layer stops meshing its wall | C6; T12, T12b |
| merged and per-face paths disagree | T6 compares them directly |
| the L4 prediction is wrong because it was assumed | the rig computes `P` from a world scan and prints it first |
| the five copies of "transparent" drift again | the shared helper (17.3) |

### 17.12 Design check (2026-09-24): two passes, NEEDS WORK both times, folded in

**Pass 1** found no violated design key but four unresolved items: T9 said "to establish"; T8 was
unsatisfiable for sub-voxels; the cost measurement was vague; the rebuild path was marked "to verify".
Folded in by `aef39a78`.

**Pass 2** (on `aef39a78`) found five more:

1. **The route matrix was incomplete, and patching route by route would not hold.** Also bypassing the
   neighbour: the template/structure stamp (the route generated glass takes), placed-object clear,
   settlement build, and the edit callbacks. C7 is now one mechanism at `Chunk::rebuildFaces`, not
   per-route patches.
2. **C2's type change reaches the grass predicates** through `neighborSolid`'s border branch → C8, T10b.
3. **The L4 prediction was wrong.** It assumed ground under the panes that does not exist (checked
   live). The prediction is now computed from a scan by the rig: +14.
4. **The measurement route was unspecified** → `GET /api/debug/chunk_faces`, and the test helper
   moves into the engine.
5. **D2 was undesigned** → §17.5b specifies option A in full; option B is the deferral.

Also added: L4b, the generated glass window, which exercises the microcube path real buildings take.

**Awaiting before implementation:** the reviewer's go-ahead, and decision D2 (A recommended).
Both given 2026-09-24 (D2 = A).

### 17.13 Execution log (measured, per step)

The suite is `ChunkTransparentCullingTest` (the §17.8 tests plus T9f). "Regression list" = 20
neighbouring suites (mesher, fine merge, fine culling, grass, lighting, sealing, render flags, cracks
and damage, LOD mesh, window aperture; 187 tests).

| step | suite: ok / red / skipped | flipped red → green | new reds | regression list |
|---|---|---|---|---|
| 2 (baseline, `71f07857`) | 16 / 32 / 7 | — | — | — |
| 3 (C1 alone, not committed) | 15 / 34 / 7 | T1 | **T8 cubes /1, /2**: plan defect, amended (step 3 = C1+C2+C8) | — |
| 3 (C1+C2+C8) | 19 / 29 / 7 | T1, T7, T9e/R2 | none | 186 pass, 1 fail (below) |
| 4 (C3+C4+C5 sub/micro) | 24 / 24 / 7 | + T4a, T4b, T4c, T5a, T5b; T6 stays green | none | 186 pass, 1 fail (below) |
| 6 (C9, D2 = A) | 31 / 17 / 7 | + **all 9 T8 cases** (sub/micro now EXACT equality, not ⊇), T15 | none | 186 pass, 1 fail (below) |
| 7a (T14 written; counters only) | — | — | T14 red for the stated reason (no ripple); R6-as-fixed red | — |
| 7 (C7, first cut) | 45 / 4 / 7 | all 14 T9 route cases, T9f | **T14c**: stone→brick rippled | — |
| 7 (C7, delivered-signature fix) | 46 / 3 / 7 | + T14 | none | 191 pass, 1 fail (below; +ChunkManager/DirtyChunkTracker/FloraMargin) |
| 8 (C5 cube leaves + C6 opaque cap) | **49 / 0 / 7** | + T11, T12, T12b | none | 191 pass, 1 fail (below) |
| 10 (T16 cost + `GET /api/debug/chunk_faces`) | **50 / 0 / 7** | T16 | none | C7 signature cost, Debug, 20 reps: terrain **0.55%** of a rebuild (0.27 of 49.1 ms); worst case, a border layer entirely of subcubes, **3.92%** (3.60 of 91.9 ms). Both under the 5% budget; the worst case is within 1.1 points of it |
| 9 (FULL unit suite, step-8 build) | — | — | — | **4,011 pass / 20 skipped / 2 fail**, ~69 min Debug. Both failures are unrelated to §17: the light-boundary test (below) and `AtlasManagerTest.BuildAtlasFromSourcePNGs`, which fails whenever the BC7 cache exists (verified: passes with `cache/textures` moved aside; logged in `StructurePipelineGaps.md`) |

**C7 first cut was wrong, and T14 caught it.** `removeCube` re-meshes the chunk at once with the
cell EMPTY. A signature compared with "the previous rebuild", with changes OR-ed into a pending
mask, therefore recorded the transient empty state, and a stone → brick swap (same render class)
rippled. Fix: compare with the signature the neighbours were last **delivered**, and derive the
pending mask afresh from current vs delivered on every rebuild. A transient state nets out, and a
direct rebuild's change is still carried to the managed rebuild that delivers it.

**The one failure in the regression list** is
`FineFaceMerge.SubcubeMerge_CrossCubeSplitsOnLightBoundaryBetweenCubes`: "cross-cube must split +Y
at the light boundary", 1 vs 2. Light has been a uniform placeholder since `089ff2cb`
(2026-08-31), so the boundary it needs no longer exists. The test contains no transparent material,
and every §17 change is keyed on one, so for that input the old and new code take identical paths.
**Empirical evidence exists independently:** `docs/VoxelDamageVisualization.md:1319` records the
same failure, identical message and line (`topSubFaces() 1 vs 2`), verified pre-existing by
reverting `ChunkRenderManager.cpp`, rebuilding and re-running.

**Rig traps found while writing the reds (fixed in the test, recorded inline there):** (1) a chunk's
arrival queues an IDLE re-mesh of its neighbours, so R3/R7 falsely passed until the rig settled
before editing; (2) an all-air neighbour's first content takes a different path, which made R4/R7
placement falsely pass, so chunk B always carries filler.

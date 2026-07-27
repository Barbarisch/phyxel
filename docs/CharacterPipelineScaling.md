# Character Pipeline Scaling — architecture review (2026-07-27)

Goal: rich worlds with **many simultaneous characters**. This is a step-back audit of the
whole path a character takes from `.anim` file to pixels, written after the
`kCharacterInstanceCapacity` bug (see [[reference_character_instance_buffer]] / commit notes)
showed the pipeline had a silent hard ceiling.

**Status: findings + proposal. Nothing here is implemented yet** beyond the capacity fix.

---

## 1. How a character reaches the screen today

```
.anim MODEL section  ──> RagdollPart[]        (one part per authored Box, 1.1k-4.7k per rig)
                              │
AnimatedVoxelCharacter::update│  animation sample -> bone globalTransform
                              │  per bone-GROUP: write worldPos/worldRot to member parts
                              ▼
RenderCoordinator::batchParts │  (run TWICE per frame: shadow pass + main pass)
                              │  for EVERY character in the world, no visibility test
                              ▼
   std::vector<CharacterInstanceData>  (40 B per part)
                              │  memcpy -> single shared host-visible buffer
                              ▼
   one vkCmdDraw(36 verts, instanceCount, firstInstance) PER BONE GROUP PER CHARACTER
```

Key structural facts:
- **One shared instance buffer** for every character in the scene, sized in *parts*.
- **One draw call per bone group per character** (~37-42 groups for the imported rigs).
- **Each part is a full 36-vertex cube** — 12 triangles, all 6 faces, always.

## 2. Measured cost (the 27-creature demo scene)

Counts are derived from the shipped `.anim` library and the draw code, not estimated:

| Metric | 27-creature scene | 100 dense creatures |
|---|---|---|
| Parts (instances) | 62,378 | ~350,000 |
| Triangles (12/part) | ~749,000 | ~4.2 M |
| Draw calls / pass | 976 | ~4,200 |
| Draw calls / frame (2 passes) | 1,952 | ~8,400 |
| Instance upload / pass | 2.50 MB | 14.0 MB |
| Instance upload / frame | 5.0 MB (uploaded twice, identical) | 28 MB |

Observed: ~10 FPS with 27 creatures on screen in a **Debug** build (17 monsters alone: 41-51).

Per-rig part counts for scale: `monster_mushroomking` 4,667 · `monster_alien` 3,602 ·
`monster_orc` 3,500 · **`humanoid.anim` 1,116** · `ogre.anim` 1,119.

## 2b. Tier 0 measurement — render dominates, and Debug lies

Run after Tier 1 landed, CharacterTestbed, 100 humanoid NPCs (102,400 parts), identical camera
in every row. `cull_distance` switches character *rendering* on and off while leaving the
simulation running, which isolates render cost from update cost:

| Scene | drawn (main/shadow) | parts uploaded | **Release** | Debug |
|---|---|---|---|---|
| 100 NPCs, all rendered | 100 / 100 | 102,400 | **139 FPS** (7.19 ms) | 8 FPS |
| 100 NPCs, render-culled (still simulating) | 0 / 0 | 0 | **403 FPS** (2.48 ms) | 8 FPS |
| 0 NPCs (entities cleared) | 0 / 0 | 0 | **393 FPS** (2.54 ms) | 20 FPS |

**In Release:** rendering 100 characters costs **~4.7 ms — about 65% of the frame**. Their
simulation costs nothing measurable (403 vs 393 FPS is noise, and the "empty" row is
fractionally *lower*, which is pure jitter). So the render pass is the bottleneck and **F2/F3
are the right targets** — Tier 2 is confirmed, not speculative.

**In Debug the same experiment says the exact opposite**: culling all 100 characters changed
nothing (8 → 8 FPS) while deleting them doubled the frame rate (8 → 20). MSVC Debug inflates
the tight per-part CPU loops so severely that they swamp a GPU cost that is actually dominant.

> **Standing lesson: never prioritize performance work from Debug numbers.** This review was
> nearly re-ordered around a conclusion that was purely a Debug artifact — Tier 3 was promoted
> over Tier 2 on the strength of the Debug column before the Release run corrected it.

*Not yet measured:* how the 4.7 ms splits between the main pass and the shadow pass. The
obvious test (turn the camera away, so `drawn_main` → 0 while `drawn_shadow` stays 100) is
confounded — facing away puts far more terrain and foliage in frame, so the comparison measures
scene content rather than character cost. A dedicated shadow-character toggle is needed to
split it, and that split decides whether shadow-pass character LOD is worth building.

## 3. Findings

### F1 — No visibility culling for characters at all *(highest value / lowest effort)*
`batchParts` iterates every character the NPCManager knows about, with no frustum test and no
max-distance test, in both the shadow and main passes. A character behind the camera or 500
units away costs exactly as much as one filling the screen.
A `Utils::Frustum` with `intersects(center, radius)` **already exists** and is used for chunks.

### F2 — Character parts are never face-culled *(largest GPU cost)*
Every part issues `vkCmdDraw(36, ...)` — a full cube. World voxels *are* hidden-face culled
(that work took a furnished tavern 412k→55k faces); characters never got the equivalent. For a
surface-shell rig most parts expose 1-2 faces, not 6, so a large majority of those ~749k
triangles are interior geometry that can never be seen. This is the most likely reason the
scene is GPU-bound.

### F3 — Draw call per bone group per character
976 draws/pass at 27 creatures, ~4,200 at 100. The bone transform is passed via push constants,
which is *why* the draw has to be split per bone group.

### F4 — The instance buffer is fully re-uploaded twice per frame with identical bytes
The shadow pass and main pass each rebuild and memcpy the same 2.5 MB. The code comment already
admits the second upload is redundant ("byte-identical data ... the redundant memcpy is harmless").

### F5 — The budget is a compile-time constant, and overflow degrades arbitrarily
`kCharacterInstanceCapacity` is fixed at build time; a project cannot tune it. When it *is*
exceeded, which characters get dropped depends on NPC iteration order, not camera distance —
so the creature in front of you can vanish while one behind you renders.

### F6 — The old ceiling was ~8 humanoids, so this was never creature-specific ⚠️
`humanoid.anim` is 1,116 parts, so the old 10,000-part cap held **8 characters**; the 9th would
be truncated mid-character (partially drawn — a "melted" villager) and the 10th onward invisible.

That threshold is well inside normal settlement populations: `ResidentPlanner` plans **one
resident per Home/Work/Tavern location**, so a town's NPC count tracks its building count, and
`NPCManager`'s own comment cites a measured "village ≈ 14". A 14-resident village would have
rendered roughly 8 residents, 1 mangled, and ~5 not at all — regardless of camera, since
nothing culls. Sample game definitions ship only 1-4 NPCs, which is likely why this went
unnoticed for so long.

*Arithmetic and code path are certain; no pre-fix in-engine repro was run to watch it happen.
Cheap to confirm — see P0.2.*

### F7 — No render LOD
A 4,667-part creature 300 units away still draws all 4,667 microcubes, each far below a pixel.
There is no decimated representation and no impostor.

### F8 — Simulation side is in better shape, with two gaps
An update-LOD already exists and is good: beyond 30u characters tick at 30 Hz, beyond 60u at
15 Hz, with per-instance jitter so they stagger. Gaps: (a) `NPCManager::update` runs an
**O(n²)** XZ separation pass over all NPCs every frame — the comment flags this ("revisit with
a spatial grid before city-scale populations"); (b) there is no far-distance sleep and no
per-frame budget on full updates.

### F9 — No part budget at import
Nothing in the import pipeline caps or decimates part count. 4,667 parts for one creature is
what sets the whole scaling problem; the renderer is just paying for it downstream.

### F10 — Characters are not GPU-profiled
`GpuProfiler` has only `STATS_SLOT_STATIC` and `STATS_SLOT_SHADOW`. There is no character slot,
so GPU time currently cannot be attributed to this pass.

---

## 4. Proposal

### Tier 0 — measure first (do before Tier 2)
- **P0.1** Add `STATS_SLOT_CHARACTER` to `GpuProfiler` (F10). Without it, Tier 2 is guesswork.
- **P0.2** Verify F6 by spawning 14 humanoid NPCs on the *pre-fix* binary and counting rendered
  bodies. Confirms whether this silently degraded shipped villages.

### Tier 1 — cheap, immediate, low risk
- **P1.1** *(F1)* Frustum + max-distance cull before batching, both passes (shadow culls against
  the light frustum, not the camera). Reuses `Utils::Frustum`. Expect most scenes to drop a large
  fraction of batched characters outright.
- **P1.2** *(F5)* Sort candidates by camera distance and batch nearest-first, so budget
  exhaustion degrades gracefully instead of arbitrarily.
- **P1.3** *(F5)* Move the capacity to `EngineConfig`/`game.json` with the constant as default.
- **P1.4** *(F4)* Build the instance data once per frame and reuse it for both passes.

Together these are mostly mechanical and should land in one change set.

### Tier 2 — the real scaling work
- **P2.1** *(F2)* **Per-part face masking.** Bake a 6-bit exposed-face mask per part at import and
  draw only exposed faces. This is the single biggest GPU win and mirrors what world voxels
  already do. Requires an instance-format change plus a shader change — the kinematic pipeline's
  per-face instance layout (`KinematicFaceData`) is the existing precedent to copy.
- **P2.2** *(F3)* Move bone transforms into an SSBO indexed per instance, collapsing the per-bone-group
  draws into **one draw per character** (or one for all characters). 976 draws → ~27.
- **P2.3** *(F7)* Baked LOD chain per rig (microcube → subcube → cube merge), selected by
  screen-space size, with an impostor beyond the last level.

### Tier 3 — simulation at population scale
- **P3.1** *(F8a)* Replace the O(n²) separation with a spatial hash.
- **P3.2** *(F8b)* Far-distance sleep + a per-frame budget of full updates, round-robin.
- **P3.3** *(F9)* Part-count budget/decimation at import, with a warning when a rig exceeds it.

### Sequencing (after the §2b Release measurement)
Tier 0 and Tier 1 are **done**. The Release numbers confirm the original ranking:

1. **Split the 4.7 ms between main and shadow pass** (add a shadow-character toggle). Cheap,
   and it decides whether shadow-pass character LOD is worth building.
2. **Tier 2, in order: P2.1 face masking → P2.2 bone SSBO → P2.3 LOD.** Confirmed by
   measurement: character rendering is ~65% of the frame at 100 characters.
3. **Tier 3 when populations grow.** Character simulation is currently free in Release at
   n=100, so the spatial hash and update budget are not urgent — but `NPCManager`'s separation
   is O(n²) and will not stay free; re-measure at n≈500.

## 5. Relationship to existing work
- The sub/micro **greedy-meshing** item (`docs/RenderOptimization.md` #40) is the world-voxel
  sibling of P2.1 — same class of problem, different pipeline. Worth designing together.
- `docs/LargeWorldScalePlan.md` covers world streaming scale; this doc is the character-count axis.

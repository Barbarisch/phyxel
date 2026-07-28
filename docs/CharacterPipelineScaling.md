# Character Pipeline Scaling

Goal: rich worlds with **many simultaneous characters** (target: thousands). This documents
the audit, everything shipped, and — importantly — the measurements that repeatedly
overturned the plan.

**Status 2026-07-27:** Tier 0, Tier 1, P2.2, P2.3 and the CPU-batching work are shipped.
At 1024 characters: **82.74 ms → 35.61 ms while going from 256 to 1024 characters drawn**
(~9x better per-character throughput).

---

## 1. How a character reaches the screen

```
.anim MODEL section  ──> RagdollPart[]        (1.1k-4.7k per rig)
                              │
AnimatedVoxelCharacter::update│  update-LOD gate (distance tiers + per-frame tick budget)
                              │  animation sample -> bone globalTransform
                              │  per bone-GROUP: write worldPos/worldRot
                              ▼
RenderCoordinator::buildCharacterFrameData   ONCE per frame, before the shadow pass
                              │  frustum + distance cull (camera AND light frusta)
                              │  sort nearest-first
                              │  pick part-count LOD by distance
                              │  bulk-copy the CACHED per-character instance blob
                              │  append ~20 bone matrices per character
                              ▼
   one shared instance buffer + bone-transform SSBO, uploaded once
                              │
   main pass:   ONE vkCmdDraw per character (instance carries a local bone index,
                                             draw supplies boneBase)
   shadow pass: one draw per bone group (not yet collapsed — see P2.2b)
```

Key invariant that makes the caching work: **a part's offset/scale/color never change.**
Animation writes only `worldPos`/`worldRot`. So the instance payload is static per character
and is cached against `RagdollCharacter::partsVersion()`.

## 2. Measurement log

Every conclusion here was reached by A/B measurement, and three of them reversed an earlier
one. Read this section before optimizing anything in this pipeline.

### 2a. n=100, Debug vs Release — **Debug lies**

100 humanoid NPCs, identical camera, `cull_distance` used to toggle rendering only:

| Scene | **Release** | Debug |
|---|---|---|
| 100 rendered | 139 FPS | 8 FPS |
| 100 render-culled (still simulating) | 403 FPS | 8 FPS |
| 0 NPCs | 393 FPS | 20 FPS |

Release: rendering is ~4.7 ms, ~65% of frame; simulation is free. **Debug says the exact
opposite** — culling all 100 changes nothing while deleting them doubles the frame rate —
because MSVC Debug inflates the per-part CPU loops until they swamp a dominant GPU cost.

> **Never prioritize performance work from Debug numbers.**

### 2b. n=100 — draw calls are not the cost

Per-pass GPU scopes proved unreliable here (the "Characters" scope varied 1.29–2.57 ms
across consecutive samples; overlapping GPU work is attributed erratically). Frame-time A/B
is the ground truth.

P2.2 collapsed main-pass draws **2,000 → 100** and changed frame time by **nothing**
(7.09 → 7.15 ms, 30-sample medians). The earlier "~1.05 µs/draw" estimate was
*pass-cost ÷ draw-count*, which silently charges all geometry work to submission.

> **Don't compute µs-per-draw as pass-cost ÷ draw-count.**

### 2c. n=1024 — the picture inverts again

| Scene | frame |
|---|---|
| 1024 NPCs, 256 rendered | 82.74 ms |
| 1024 NPCs, all render-culled | 44.36 ms |
| empty baseline | ~2.5 ms |

At n=1024 the frame is **CPU-bound on character simulation**, and GPU render work hides
underneath: drawing 256 characters vs drawing none moved the frame by roughly nothing. At
n=100 rendering was 65% of the frame; at n=1024 it is effectively free.

> **Re-measure at your target scale.** Bottleneck order at n=100 and n=1024 are opposite.

### 2d. Instrumented breakdown at n=1024

`npc_update` in `/api/render/stats` splits it. Before the fixes:

| | |
|---|---|
| separation (was O(n²)) | ~8 ms |
| NPC update | 29.7 ms |
| **full character ticks** | **613 of 1024** |
| batching (`build_ms`) | 14.1 ms |

613/1024 full ticks despite the update-LOD existing. Cause: **LOD periods are wall-clock**,
so with a 46 ms frame the 15 Hz far tier (66 ms) only skipped every other frame. The LOD
stops deferring exactly when it is needed — a feedback trap. Distance tiers alone cannot
bound cost; only a per-frame budget can.

### 2e. Noise floor

FPS here jitters **±12%**. A single reading made the P2.2 change look like 141 → 154 FPS
(pure noise). **Every number in this document is a ≥25-sample median** from
`GET /api/debug/engine_timing`.

## 3. What shipped

| Item | Effect |
|---|---|
| **Instance-buffer capacity fix** | Characters past a 10k-part cap drew from stale memory and silently vanished; the one straddling it half-drew (which is what made a healthy `deer.anim` look corrupt). 10k → 262144, all-or-nothing per character, warnings on drop. The old cap held only **8 humanoids**, so settlements were already losing villagers. |
| **Frustum + distance culling** | Camera and light frusta tested *separately* — an off-screen character still casts into view. |
| **Nearest-first batching** | Budget overruns now drop the *far* characters, not whatever the NPC map iterated last. |
| **One build + upload per frame** | Shared by shadow, main and mirror passes; each used to rebuild and re-upload identical bytes. |
| **P2.2 bone-transform SSBO** | Main pass 2,000 → 100 draws. **No frame-time gain** — kept as structural headroom (draws now scale with characters, not characters × bone groups). |
| **Spatial hash for NPC separation** | O(n²) → O(n). 44.36 → 36.54 ms sim-only at n=1024; now 0.6 ms. |
| **Stop batching doomed characters** | Optimistic batch-then-rollback made every dropped character pay full price. `build_ms` 14.09 → 5.09 at n=1024. |
| **Update-LOD tiers + tick budget** | >120u = 6 Hz, >220u = 2 Hz, plus a per-frame full-tick budget (default 256) with a 0.5 s staleness escape so nothing starves. NPC update 29.66 → 12.10 ms; full ticks 613 → 270. |
| **P2.3 part-count LOD** | Removed the hard 256-character wall. Drawn 256 → 1028, dropped 768 → 0, **0.120 → 0.036 ms per drawn character**. |
| **Cached instance blob** | The static payload is no longer re-gathered from the 112-byte-stride part array each frame. `build_ms` 9.49 → 7.36. |

### Runtime knobs
`POST /api/debug/characters` — `capacity`, `cullDistance`, `shadows`, `updateBudget`,
`lod1`, `lod2`. Counters in `/api/render/stats` under `characters` and `npc_update`.

## 4. What's left

1. **Persistent per-character GPU allocations.** The remaining 7.36 ms of `build_ms` is
   mostly unavoidable traffic in the current design: ~8 MB instance copy + ~8 MB upload +
   ~20k bone-matrix builds + ~20k batch records per frame. Since a character's payload is
   static, an unchanged character should not be re-uploaded at all. This is the change that
   would collapse `build_ms` rather than trim it.
2. **P2.1 per-part face masking.** Measured potential across four rigs: average **2.4–3.2
   of 6 faces exposed**, i.e. **1.9–2.5x** vertex reduction (not the 3x originally
   assumed — thin features like fingers, tails and ears expose most of their faces, and a
   quarter of parts expose 4+). Mask must use **same-bone-group neighbours only**;
   cross-group adjacency is animation-dependent. Two refinements evaluated:
   dropping faces into sealed interior cavities gains ~0-1% (the per-group shells are open
   at their seams, so flood fill leaks in — no sealed cavities exist); cross-group masking
   would add 12–41% but is unsafe.
   Only worth doing once the frame is not CPU-bound.
3. **P2.2b — collapse the shadow pass.** Still one draw per bone group. Needs its own
   bone-only descriptor set: its pipeline has no descriptor sets, and handing it the shared
   one would bind the shadow map as a sampler while rendering into it. Low priority, since
   draws are known not to be the bottleneck.
4. **Per-tick cost of a full character update.** 270 full ticks now cost ~12 ms. The budget
   bounds it; making each tick cheaper is the next lever.
5. **LOD warm-up.** The first frame a crowd appears pays a one-time LOD build per character
   (cached after). Not measured as a hitch, but worth a warmup pass before a crowd streams in.
6. **Import-side part budget.** 4,667 parts for one creature is what sets the whole problem;
   nothing caps or decimates at import.

## 5. Method notes (earned the hard way)

- Never prioritize from **Debug** numbers — they inverted the answer.
- Never conclude from a **single FPS reading** — ±12% noise.
- **Re-measure at target scale** — n=100 and n=1024 have opposite bottlenecks.
- **GPU timestamp scopes here are unreliable** (overlap). Trust frame-time A/B.
- A perf toggle that appears to **do nothing may be masked** by another cost — the shadow
  toggle showed zero gain until the CPU batching fix exposed the GPU saving.
- **"Cache it" is not a win until measured.** The first version of the blob cache measured
  9.48 ms — no improvement — because it reintroduced an O(parts) scan, used
  `resize()`+`memcpy` (value-initializing 186k elements before overwriting them), and built
  an `unordered_map` per character. Only after removing all three did the number move.

## 6. Relationship to existing work
- Sub/micro **greedy meshing** (`docs/RenderOptimization.md` #40) is the world-voxel sibling
  of P2.1 — same class of problem, different pipeline.
- `docs/LargeWorldScalePlan.md` covers world streaming scale; this is the character-count axis.

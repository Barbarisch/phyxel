# Character Animation v2 — Paradigm Comparison & Design Direction

> **Status:** design / decision-support doc. Written 2026-07-09 in response to the standing
> "character animation has failed for months" problem. Compares the three viable paradigms against
> Phyxel's *actual* engine and *actual* history, then recommends a direction. **No code decision is
> locked** — the last section lists the forks the reader must pick.
>
> **Companion reading (do not skip):** [`LessonsLearned_ProceduralAnimation.md`](LessonsLearned_ProceduralAnimation.md)
> (5 prior failures), [`CharacterAnimationGuide.md`](CharacterAnimationGuide.md) (current clip/FSM
> pipeline), [`AnimatedCharacter.md`](AnimatedCharacter.md).

---

## 0. TL;DR

- **"Pick one paradigm" is the wrong frame.** Animation is four *independent* problems (source,
  selection+blend, grounding, reactivity) plus one cross-cutting enabler (a body-plan/rig
  abstraction). Different layers want different answers.
- **The single biggest blocker is not animation quality — it's that the entire system is hardcoded
  to one Mixamo humanoid** (`mixamorig:*` string lookups everywhere). Until that's abstracted, no
  paradigm can serve a D&D world of many creatures and sizes. **This is the foundation to build
  first, and it is paradigm-independent.**
- **The 5 prior failures were all humanoid + procedural/physics fighting baked clips.** That failure
  mode *inverts* for creatures (no clips to fight, no uncanny-valley expectation). So the honest
  recommendation is **not one paradigm — it's a split by body plan**:
  - **Humanoids → keep keyframe FSM** (it works; don't refight history) + turn on the foot-IK you
    *already built* + add motion-matching-lite for blend quality. Feed it Path A/B for clip variety.
  - **Creatures/monsters → procedural IK locomotion** (the one place procedural reliably wins).
  - **Reactions (both) → blend-to-canned + a light physics nudge**, *not* balance-controlled active
    ragdoll (proven-failed, and unsolved-hard by hand).
- **An automated verification pipeline is a HARD prerequisite (§9), not an afterthought.** The 5
  failures never converged because the only oracle was the owner's eyes. Every mechanical failure
  mode from the post-mortems (skate, knee flips, stretch, jitter, snap, collapse) is machine-
  checkable; the MotionOracle harness (P0a) ships before/with the rig abstraction, and manual
  visual review is the LAST gate only.
- **Physics-RL (ProtoMotions/DeepMimic) is the most general and the most dangerous.** It *would*
  solve the balance problem your PD controller couldn't — but it needs an articulated actuated
  ragdoll in-engine, an external training sim whose physics won't match yours, per-creature training,
  and an ONNX runtime. Recommended **parked**, not adopted.

---

## 1. Goals & constraints (from the owner)

Stated priorities, in the owner's words, condensed:

1. **Algorithmic over library.** "If I had an algorithmic way to create all animations, rich enough
   to cover the dynamic world, I would take that over what I have now."
2. **Physically grounded & realistic** motion in a **dynamic world with lots of variation** — the
   thing clip libraries + hand-blending have failed to deliver for months.
3. **Many body types & sizes** — a D&D-style game: humans, but also creatures/monsters, and
   Small↔Large size variants. "Flexible enough to support multiple character sizes, types and
   creatures."
4. **Not married to the format.** `humanoid.anim` "has just been the best option so far." The `.anim`
   text format is on the table for replacement.
5. **Voxel character model is (mostly) liked**, but "room in either simplifying or redoing that
   character" is explicitly open.
6. **Prior experience:** Mixamo import works and covers a lot but doesn't cover everything; blending
   for a dynamic world was never cracked; "LLM-generated animation has been shitty so far" (note:
   text-to-motion diffusion ≠ LLM — see §7).

Non-negotiable engineering reality (from `CLAUDE.md`): fixes are verified at **runtime**, features
get an **agent-designed stress test** on their scaling axis, and every dimension is **grounded**.
An animation system must be judged by *runtime look + measured invariants* (foot doesn't slide >X,
knee never inverts, every gait phase-continuous), not "it compiled."

---

## 2. Current system — ground truth

Verified against the tree 2026-07-09. Citations are file:line.

**Runtime is one C++ path:** `RagdollCharacter` → `AnimatedVoxelCharacter`
(`engine/src/scene/AnimatedVoxelCharacter.cpp`) + a stateless sampler `AnimationSystem`
(`engine/src/graphics/AnimationSystem.cpp`). Despite the name, `RagdollCharacter` is **not** a
physics ragdoll — it's a container of rigidly-bone-parented voxel boxes (`RagdollPart`,
`engine/include/scene/RagdollCharacter.h:23`).

| Subsystem | Reality | Key cite |
|---|---|---|
| **Skeleton** | Flat `std::vector<Bone>` + name→index map, hierarchy by `parentId`. **Data-driven data, hardcoded behavior.** | `graphics/Animation.h:11-51` |
| **Format** | Bespoke text `.anim` (`SKELETON`/`MODEL`/`ANIMATION`, count-driven, no blank lines/comments inside sections). Slow to parse (~5s Debug humanoid) → path-keyed cache + `prewarm`. | `AnimationSystem.cpp:11-201`; `tools/anim_pipeline/anim_format.py:1-27` |
| **Humanoid rig** | `BoneCount 65` full Mixamo rig; `MODEL BoxCount 1116` voxel boxes; ~76 clips. | `resources/animated_characters/humanoid.anim` |
| **Voxel→bone** | Each voxel box **rigidly parented to ONE bone** (`modelMatrix · bone.globalTransform · translate(offset)`). No skinning weights. | `AnimatedVoxelCharacter.cpp:727-1053` |
| **Blending** | **Single two-clip whole-skeleton crossfade.** No blend tree, no 1D/2D blendspace, no additive layers, no per-bone masks. `blendDuration` one global 0.2s. | `AnimationSystem.cpp:282-343`; `AnimatedVoxelCharacter.h:598` |
| **FSM** | ~40 states, **hardcoded switch** state→clip (+ optional override map). Per-state speeds hardcoded. | `AnimatedVoxelCharacter.h:36-72`; `.cpp:2133,3168+` |
| **Grounding** | Kinematic capsule, hand-written integrator (gravity, sweep ground query, axis-separated collision, step-up, mantle, ladder). Grounds on per-chunk `VoxelOccupancyGrid`s. | `AnimatedVoxelCharacter.cpp:53-373`; `physics/VoxelDynamicsWorld.h:85-99` |
| **Foot IK** | **Real analytic 2-bone IK EXISTS** (law-of-cosines + knee hint) + **foot-lock** + critically-damped body spring. Data-driven per-clip knobs. **Default-OFF** (`m_footIKEnabled=false`, ~360µs/char), humanoid-name-bound, used only for stairs/uneven. No FABRIK, no hand/look/full-body IK. | `AnimatedVoxelCharacter.cpp:3806-4479`; `.h:535` |
| **Cast/melee** | Data-driven family mappers (`SpellAnimMapper`/`MeleeAnimMapper`, JSON) → clip sequences with release/hit frames. C++ ports must stay in sync with Python resolvers. | `engine/src/core/SpellAnimMapper.*`, `MeleeAnimMapper.*` |
| **Import/retarget** | Mixamo FBX→glTF→`.anim`; **bone-ID remap + clip-name→FSM-key rename only**. No cross-rig retargeting, no bind-pose correction. Onto the one humanoid rig. | `tools/batch_import_mixamo.py`; `tools/asset_pipeline/extract_animation.py` |
| **Physics coupling** | Character segment boxes are **one-way kinematic pushers** (deflect debris/furniture, feed GPU particles). Character body **never receives impulses**. No joints/motors simulated. | `VoxelDynamicsWorld.h:102-113`; `AnimatedVoxelCharacter.cpp:4580-4637` |

**The decisive structural fact — everything is hardcoded to one humanoid:**

- Segment colliders, foot-bone resolution, hip/sit logic all look up literal `mixamorig:*`
  (`buildSegmentBoxes` `.cpp:4480`, `resolveFootBoneIds` `.cpp:3773`).
- The FSM state→clip map is hardcoded to humanoid clip names (`"idle"/"walk"/"run"`).
- `character_wolf.anim` (64 bones, quadruped), `character_spider.anim` (65 bones, **BoxCount 0** —
  no visual model), `character_dragon.anim` (113 bones) **load as skeletons+clips but are not
  functional gameplay creatures**: the FSM can't select their clips, they get no segment
  boxes/IK/sit, and `GameDefinitionLoader` defaults `animFile` to `humanoid.anim`
  (`GameDefinitionLoader.cpp`, exact line numbers have drifted since this doc's 2026-07-09 snapshot
  but the default still resolves to `humanoid.anim`). Removed `SpiderCharacter`/`PhysicsCharacter`
  classes are gone entirely as of this update — re-verified 2026-07-21, they no longer survive even
  as comments anywhere in `engine/`.

> **Takeaway:** the code is one well-tuned humanoid, not a character *system*. The D&D-creatures
> goal is blocked at the rig layer before any paradigm question is even reached.

---

## 3. The history we must respect

`LessonsLearned_ProceduralAnimation.md` records **5 failed attempts** on `feature/active-character`.
Summarized, with the pattern that matters:

| # | Approach | Why it failed | The real lesson |
|---|---|---|---|
| 1 | Pure procedural humanoid skeleton (spring pelvis, lean, hip bob, FK, 2-bone IK) | "Too many DOF with no ground truth" — technically valid, visually uncanny. | Procedural **humanoid** locomotion with **no reference** is a multi-year AAA R&D problem. |
| 2 | Active ragdoll + PD **balance** controller | Collapsed or oscillated; PD gains have no stable setting. | Hand-tuned ragdoll balance is **unsolved-hard**. (This is exactly what RL replaces — §5C.) |
| 3–5 | Foot-IK / full-limb-IK **overlaid on keyframe** walk | IK **fights the baked foot positions** → skating, knee flips, pelvis jitter. Each added chain made it worse. | IK-on-keyframe needs root-motion/phase awareness; "just snap the foot down" is a trap. |

**Codified takeaways from that doc:** keyframe works — don't fight it; procedural needs enormous
investment; IK-overlay is deceptively hard; don't layer complexity on a working system; AI-coding
can produce correct-but-uncanny IK/ragdoll that no parameter tweak fixes. Its own retry
recommendation: proven solver, **post-process pass** after anim writes bones, **feet-only first**,
**require root-motion data**, budget real visual iteration.

**Two things that reframe this history (and are the crux of §5–6):**

1. **All five failures were HUMANOID.** The uncanny-valley problem in #1 and the fight-the-clips
   problem in #3–5 are *humanoid-specific*. For a giant voxel spider or a dragon there is **no
   Mixamo clip to fight** and **no human intuition to violate** — procedural leg IK is the *standard*
   and *reliable* solution for those bodies (every indie "procedural spider walk"). The same tool
   that failed on humans is the *right* tool for creatures.
2. **The doc is partly historical.** It predates the Bullet removal (it references
   `btCapsuleShape`) and predates the *current* shipped 2-bone foot-IK + foot-lock + body-spring
   that already runs (default-off) for stairs (`.cpp:3806-4479`). So attempts 3–5's descendants
   *did* eventually produce a working, root-motion-aware, post-process foot IK — exactly the shape
   the retry-recommendation asked for. It's built. It's just off, and humanoid-bound.

---

## 4. The right decomposition

Animation is **four independent problems**. Conflating them is why "get more clips and blend them"
never converged — it only touches problem 1 and half of problem 2.

1. **Source** — where does raw motion come from? (hand-keyframe / mocap library / video mocap /
   text-to-motion / procedural synthesis / physics)
2. **Selection + blend** — at runtime, which motion(s) play and how are they combined for a dynamic
   world? (FSM / blend tree / 1D-2D blendspace / **motion matching**)
3. **Grounding** — how does motion adapt to *this* terrain/physics? (foot IK, foot-lock,
   pelvis/body adaptation, slope alignment)
4. **Reactivity** — how does motion respond to forces/hits? (canned reactions / blend-to-ragdoll /
   active ragdoll / learned physical control)

Plus the **cross-cutting enabler**:

0. **Rig / body-plan abstraction** — a creature-agnostic description (bones, limb chains, foot
   end-effectors, gait class, size scale) that layers 1–4 all consume, instead of `mixamorig:*`
   string lookups. **This is the true foundation and it is paradigm-independent.** Nothing below
   generalizes to a D&D bestiary without it.

The three "paradigms" the owner named are really different *combinations of answers* to problems 1–4.
That's why the recommendation (§6) is a **matrix, not a single choice**.

---

## 5. The three paradigms

Each judged against the goal axes: **variation** (dynamic world), **grounding**, **creature/size
coverage**, **authoring effort for the owner**, **implementation risk** (weighted by the 5 failures),
and **fit to this engine**.

### A. Clip-based + motion-matching + IK grounding

**What it is.** Keep authored/mocap clips as the source. Replace the hand-tuned FSM+crossfade with
a data-driven **blendspace** (1D speed, 2D speed×direction) and/or **motion matching** (search a
motion database each frame for the pose that best continues current velocity/trajectory — the modern
AAA locomotion answer, Ubisoft *For Honor* → now ubiquitous). Add **foot IK + foot-lock** on top for
grounding (you already have the solver).

- **Variation:** good *for humanoids* — motion matching is the best-looking dynamic humanoid
  locomotion that exists. Bounded by database coverage.
- **Grounding:** solved by the existing IK post-pass (turn it on) — this is precisely the retry
  recipe from the lessons doc (post-process, feet-only, root-motion-aware).
- **Creatures/sizes:** ✗ **fundamental miss.** Every clip source (Mixamo, AMASS, text-to-motion) is
  human-trained. No dragons, no spiders, no non-biped gaits. Size variants need retargeting per rig.
- **Authoring effort:** low-medium. Owner keeps a familiar workflow; Path A/B (§7) expand the
  library. Motion matching needs a *tagged* motion DB (some curation).
- **Risk:** **low.** Builds on the working system; the dangerous parts (IK-on-keyframe) are already
  solved and shipping. Motion matching is well-documented and clip-only (no physics instability).
- **Engine fit:** high for humanoids. `AnimationSystem` already samples clips; motion matching is a
  selection layer above it. Needs root-motion completeness (partial today).

**Verdict:** the right answer **for humanoids specifically.** Cannot carry the creature goal alone.

### B. Procedural / IK-driven

**What it is.** Synthesize motion at runtime from a rig + gait parameters. For legged locomotion:
place foot targets by raycasting the voxel terrain, step them in a phased gait, solve leg IK
(2-bone or FABRIK) to the targets, drive body bob/sway/spine procedurally. No clips for locomotion.

- **Variation:** ✅ **infinite & terrain-native by construction.** Any slope, any speed, any turn —
  the feet are *placed on the actual ground*, so "dynamic world" is the default, not a special case.
- **Grounding:** ✅ **it is grounding** — feet are solved to terrain contacts, so sliding/clipping
  don't arise the way they do for baked clips.
- **Creatures/sizes:** ✅ **best in class.** One system parameterized by rig → bipeds, quadrupeds,
  hexapods (spiders), serpentine, dragons, and Small↔Large by scaling stride/gait. This is how
  creatures are animated across the indie world.
- **Authoring effort:** ✅ **algorithmic** — the owner's stated preference. Define a rig + gait, get
  motion. No per-creature clip hunt.
- **Risk:** **the split-decision axis.**
  - **For humanoids: HIGH — this is attempt #1, and it failed.** Uncanny, no reference, DOF
    explosion. **Do not re-attempt procedural *humanoid* locomotion from scratch.**
  - **For creatures: LOW-MEDIUM.** The failure causes (no reference / fights clips / uncanny-valley)
    don't apply. Foot-IK gait synthesis for non-humanoids is a solved, well-trodden technique. The
    hard part becomes "does the *gait feel* good," which is tuning, not architecture.
- **Engine fit:** high. Voxel creatures are natively box-articulated (segment boxes already exist),
  which is ideal input for IK. The 2-bone solver already exists; FABRIK for >2-segment limbs
  (spider legs, dragon necks/tails) is a modest addition. Needs the rig abstraction (§4.0).

**Verdict:** the right answer **for creatures/monsters** — and the one place the owner's "algorithmic"
dream is actually realistic. **Explicitly not for humanoid locomotion** (history says so).

### C. Physics-based (analytic active-ragdoll → learned/RL, i.e. ProtoMotions/DeepMimic)

**What it is.** Simulate an articulated actuated skeleton and drive it with a controller.
Two sub-flavors, very different in cost and viability:

- **C1 — analytic/PD active ragdoll:** hand-tuned joint motors + a balance controller. **This is
  attempt #2. It failed, and the lesson doc calls balance "unsolved-hard."** Do not repeat.
- **C2 — learned control (RL):** train a policy (DeepMimic / ProtoMotions) in a physics sim to track
  reference motion or follow commands; the policy emits joint torques at runtime.
  - **Variation / grounding / reactivity:** ✅✅ best possible — physically correct balance, terrain
    adaptation, push recovery *emerge*. This is the only paradigm that gives true physical hit
    reactions without canned clips.
  - **Creatures:** ✅ possible, but **per-morphology training** (retrain for each body plan).
  - **Authoring effort:** ✗✗ **not an authoring workflow** — it's an ML research pipeline. Owner
    gets policies, not clips they can tweak.
  - **Risk:** **highest.** Requires (a) an **articulated actuated ragdoll in-engine** — you have
    *scaffolding* (`CharacterSkeleton::CharacterJointDef` with `motorStrength`/`motorDamping`
    literally commented "for active ragdoll in Phase 3", `.h:36`) but **no articulated solver**;
    `VoxelDynamicsWorld` does rigid bodies + contacts, **not joints/motors**. (b) training in an
    **external sim** (Isaac/MuJoCo/Genesis) whose contact model **won't match** `VoxelDynamicsWorld`
    → sim-to-sim transfer is itself a research risk. (c) a **PyTorch→ONNX runtime** embedded in a
    C++/Vulkan engine. (d) per-creature datasets + training compute.
  - **Crucial honest nuance:** C2 is *exactly the thing that fixes attempt #2* — RL learns the
    balance controller that hand-tuned PD could not. So "physics failed here before" is **not** a
    fair reason to dismiss C2. The fair reasons are cost, stack mismatch, and that it doesn't serve
    the owner's *authoring* goal.
  - **Engine fit:** low today (no articulated solver, no ML runtime).

**Verdict:** the most general and the most powerful for **reactivity**, but a **multi-quarter
research bet** with an architecture your engine doesn't have. **Park it.** Revisit only if physical
combat reactions become the headline feature and clip/blend reactions prove insufficient.

### Scorecard

| Axis | A. Clip + MM + IK | B. Procedural/IK | C2. Learned physics |
|---|---|---|---|
| Dynamic-world variation | ●●○ (humanoid) | ●●● | ●●● |
| Physical grounding | ●●○ (IK bolt-on) | ●●● | ●●● |
| Creatures & sizes | ○○○ | ●●● | ●●○ (retrain each) |
| "Algorithmic" authoring | ●○○ | ●●● | ○○○ (ML, not authoring) |
| Reactivity (hits/knockback) | ●○○ (canned) | ●○○ (canned) | ●●● |
| Implementation risk | **low** | **low (creatures) / high (humanoid)** | **very high** |
| Fit to this engine today | high (humanoid) | high (needs rig abstraction) | low |
| History verdict | not yet tried at scale | #1 failed *for humanoid only* | #2 (PD) failed; RL untried |

---

## 6. Recommended architecture — hybrid, split by body plan

The scorecard has no single winner because **the winner depends on the body plan.** The
recommendation composes A and B on a shared foundation, and holds C2 in reserve.

```
                      ┌─────────────────────────────────────────────┐
   FOUNDATION (0) ──▶ │  Rig / Body-Plan Abstraction                 │
   paradigm-agnostic  │  bones · limb chains · foot end-effectors ·  │
                      │  gait class · size scale · clip-key map      │
                      └───────────────┬─────────────────────────────┘
                                      │  consumed by ALL layers
             ┌────────────────────────┴────────────────────────┐
             ▼                                                  ▼
   HUMANOIDS (paradigm A)                          CREATURES (paradigm B)
   • keep keyframe FSM (works — don't refight)     • procedural IK locomotion
   • turn ON existing foot-IK + foot-lock (grounding)  (foot targets → gait → FABRIK/2-bone)
   • add motion-matching-lite / blendspace (blend) • body/spine procedural motion
   • Path A/B feed clip variety (§7)               • gait tuned per gait-class
             └────────────────────────┬────────────────────────┘
                                       ▼
   REACTIVITY (both): blend-to-canned reaction + light physics nudge (impulse offset),
                      NOT PD-balanced active ragdoll.   C2 (learned) parked.
```

**Why this is the right shape:**

- It **respects the history** — it does *not* re-attempt procedural humanoid locomotion (#1) or
  PD-balance ragdoll (#2), the two clean failures. It *reuses* the foot-IK that attempts #3–5
  eventually got working.
- It **puts each paradigm where it wins** — clips+MM where quality matters and reference exists
  (humans), procedural where it's the only realistic option and reference doesn't exist (creatures).
- It **unblocks the D&D goal** by making the rig abstraction the first deliverable, which every path
  needed anyway.
- It **matches the owner's "algorithmic" preference where that preference is realistic** (creatures)
  without betting the humanoid — where "algorithmic from scratch" is a known trap — on it.

**On "simplify or redo the character":** *don't* redo it — **generalize the rig, keep the voxels.**
The voxel model is an *asset* here: box-articulated segments are exactly what IK and (later) physics
want as input. The redo that pays off is replacing `mixamorig:*` hardcoding with the rig abstraction,
and (optionally) reducing the humanoid from 65 bones/1116 boxes to a leaner rig for authoring +
perf. The `.anim` text format can stay short-term (it's slow but cached); a binary/rig-metadata
format is a *later* optimization, not a prerequisite.

---

## 7. Where Path A (video mocap) and Path B (text-to-motion) fit

Both are **Source-layer (problem 1) tools for the humanoid branch only** — they expand the clip
library that paradigm A consumes. Neither is a foundation, and **neither covers creatures** (all are
human-trained: SMPL/AMASS/HumanML3D).

- **Path A — video → motion** (record yourself; WHAM / GVHMR / 4D-Humans locally on the 4090, or
  Rokoko/DeepMotion/Plask web). Unlimited *original human* motion. Best for bespoke gameplay actions
  Mixamo lacks.
- **Path B — text → motion** (MoMask / MDM / T2M-GPT locally). "Type the animation." **Note:** these
  are dedicated generative *motion* models trained on mocap (HumanML3D/AMASS) that output joint
  rotations directly — MDM is diffusion, MoMask a masked transformer, T2M-GPT autoregressive over
  motion tokens. None is "a text LLM writing keyframes," which is likely what produced the "shitty"
  results before. Still humanoid-only, and output quality varies — treat as a clip *drafting* tool
  (lint with `anim_lint.py`), not a guaranteed-good source.
- **Shared back-half = the retarget stage that doesn't exist yet.** Today's import is bone-ID remap +
  clip-rename onto the *one* humanoid rig; there's **no cross-rig retarget / bind-pose correction**
  (§2). A real retarget stage (source bind → rig-abstraction bind, bone-name map + rotation-basis
  correction — axes already measured: char faces +Z, −Z = arm-forward) is the piece that lets *any*
  of {Mixamo, Path A, Path B} feed the humanoid branch. **Prove it with one Mixamo clip end-to-end
  before building generators on top.**

Creatures do **not** get Path A/B. Their "source" is procedural synthesis (paradigm B). This is a
feature, not a gap — it's why procedural is the creature answer.

---

## 8. How this avoids the 5 prior failures

| Prior failure | How the recommendation avoids it |
|---|---|
| #1 Procedural humanoid from scratch | **Not attempted.** Humanoids stay on keyframe. Procedural is scoped to creatures, where the failure causes don't apply. |
| #2 PD-balance active ragdoll | **Not attempted.** Reactivity = blend-to-canned + light nudge. Balance-controlled physics parked (only C2/RL, not hand-PD, would revisit it). |
| #3–5 IK fighting baked clips | **Reuse the existing post-process foot-IK** (foot-lock, body-spring) that these attempts eventually produced — proven for the *stair* case; the always-on uneven-ground case must re-prove the skating invariant (P1, red-before-green), not assume it. Creatures avoid the conflict entirely (no baked clips to fight). |
| "Don't layer complexity on a working system" | The humanoid path *adds nothing structural* — it enables existing IK and adds a selection layer. Procedural is a *separate* branch for a *different* body class, not a layer on the humanoid. |
| "AI-coding produces correct-but-uncanny motion no tweak fixes" | Grounded per `CLAUDE.md`: runtime look + measured invariants (foot-slide ≤ ε, no knee inversion, phase-continuous gait), red-before-green, agent stress test on the scaling axis (N creatures, extreme slopes, size extremes). |

---

## 9. Verification & feedback pipeline — HARD REQUIREMENT

> Owner directive (2026-07-09): *"I have no easy way to indicate to a Claude session that an
> animation looks wrong, or that it 'doesn't work'. The solution must be automated, and only rely
> on manual visual verification as the last possible step."*
>
> This is also the root cause of the 5 failures. Lessons-doc takeaway #5, verbatim: the AI-coded
> IK/ragdoll "compiled and ran — it just didn't look right, and no amount of parameter adjustment
> could fix it." The only oracle was the owner's eyes, so every tuning loop bottlenecked on manual
> QA and never converged. **No animation work proceeds under this design without an automated
> oracle for its failure modes.** This is the animation instance of the repo-wide validation-layer
> discipline (L1–L4) and the red-before-green rule.

### 9.1 The oracle pyramid (cheapest/most-automated first; eyes last)

| Tier | What | Status | Catches |
|---|---|---|---|
| **T1 — clip statics** | `anim_lint.py`: unit quats, >120° geodesic jumps, loop closure, **calibrated per-bone angular velocity/accel envelopes** (from known-good clips via `calibrate`) | **EXISTS** (`tools/anim_pipeline/anim_lint.py:216,322`) | pops, snaps, mechanically impossible keys |
| **T2 — kinematic invariants on sampled motion (“MotionOracle”)** | Headless: load rig+clip (or run a procedural gait) in a **unit test**, sample bone globals at 60 Hz via `AnimationSystem` (no engine/GPU needed — skeleton code already links in `tests/scene/`), evaluate the metric suite (§9.2) against a synthetic terrain function. Deterministic, CI-able. | **BUILD — the core deliverable** | every mechanical failure mode from attempts #2–5 |
| **T3 — runtime probe** | Same metric suite live: drive the character over a **standard terrain gauntlet** via HTTP/MCP, record per-frame full-skeleton transforms, emit a JSON verdict (`/api/animation/validate`). The animation analog of `RealizedWorldValidator` + `TraversalProbe`. **Gap:** `get_bone_positions` returns only the 12 segment boxes — needs a full-skeleton dump endpoint. | **BUILD (thin — reuses T2 metrics)** | integration failures T2 can't see (grounding, physics coupling, real terrain) |
| **T4 — VLM visual judge** | Scripted capture (`seek_animation` to fixed phase points, fixed camera, N stills or a frame burst) → a vision model reviews against a written rubric (limb crossing, ground contact plausibility, silhouette). **Known trap:** voxel-character facing is visually ambiguous — the rubric must NOT judge facing (the arms-animated-backward incident); add a facing marker to the test character if needed. | semi-automated | aesthetic defects metrics miss |
| **T5 — human eyes** | Last gate only. Reserved for the aesthetic residual ("uncanny") that T1–T4 can't decide. | manual | taste |

### 9.2 The T2/T3 metric suite — "looks wrong," decomposed

Each metric maps to a *named symptom from the actual post-mortems*, so the suite provably covers
the known failure modes (red-before-green: each check must first FAIL on a synthetic bad clip or a
reproduced historical defect):

| Metric | Definition (per frame or per contact phase) | Kills which historical symptom |
|---|---|---|
| **Foot skate** | horizontal foot-bone speed while in contact (foot height < contact threshold) ≤ ε | attempts #4–5 "skating" — the canonical motion-synthesis eval metric |
| **Ground penetration / float** | stance-foot clearance within [−ε, +band] of terrain height | floating / sunk feet |
| **Chain-length preservation** | Σ‖bone‖ of each IK chain vs bind ≤ ε drift | attempt #3 "legs stretching" |
| **Joint-limit / inversion** | signed knee/elbow bend about the pole axis stays in range | attempt #3 "knees inverting" |
| **Smoothness envelopes** | per-bone angular vel/accel vs calibrated good-clip envelope (reuse T1's calibration) | attempt #5 "arms snap", pelvis jitter |
| **Pose continuity** | max per-bone geodesic delta across any state/blend transition ≤ θ | blending pops |
| **Root-motion consistency** | ‖animation root velocity − capsule velocity‖ ≤ ε | the *root cause* of skating (motion root vs planted feet) |
| **Self-intersection** | segment-box overlap volume = 0 (tolerance for adjacent joints) | limbs through torso |
| **Balance proxy** | COM projects inside the support polygon during stance (procedural/creatures) | attempt #2 collapse/oscillation |
| **Gait invariants** (creatures) | duty factor, inter-leg phase offsets, ≥k feet planted (static-stability gaits), stride∝speed | wrong/unstable procedural gaits |

**Threshold grounding (per the ground-all-dimensions rule):** humanoid envelopes calibrated from
known-good Mixamo clips (the `calibrate` mechanism already exists); creature gait parameters
(duty factors, phase relationships — e.g. quadruped walk/trot, hexapod alternating tripod) from
published biomechanics literature, cited in the config — never invented.

### 9.3 What this honestly does and doesn't cover

- **Covered automatically:** all *mechanical* failure modes — which is everything that killed
  attempts #2–5 (skate, knee flips, stretch, jitter, snap, collapse, pops).
- **Not covered by metrics:** attempt #1's *aesthetic* "uncanny." Mitigations, in order: envelope
  calibration against real mocap (statistical naturalness), T4 VLM review, T5 human. This residual
  is another reason humanoid locomotion stays keyframe-based (real mocap is natural by
  construction) while procedural is scoped to creatures — **procedural creature gaits are more
  objectively verifiable than humanoid naturalness** (gait correctness is physics, not taste).
- **Feedback loop for a Claude session:** T1+T2 run in the inner loop (every change), T3 before any
  "works" claim (solution-auditor gate), T4 before presenting to the owner, T5 = the owner's only
  job. "It doesn't work" arrives as a failing metric with a number, not a vibe.

---

## 10. Phased plan (proposed — not locked)

Ordered so the **paradigm-independent foundations ship first** and each phase is independently
verifiable — every "Verify" below runs through the §9 oracle, never through "the owner looks at it."

- **P0a — MotionOracle (T2 harness, §9).** The metric suite as a headless unit-test target +
  library, each metric proven red on a synthetic bad clip first. Ships BEFORE or WITH P0b — no
  animation feature lands without its oracle. *Verify:* suite flags seeded defects (skate, stretch,
  inversion, pop) in known-bad clips and passes the known-good Mixamo set.
- **P0b — Rig / body-plan abstraction (foundation, unblocks everything).** Replace `mixamorig:*`
  hardcoding with a rig descriptor (bones, limb chains, foot end-effectors, gait class, size scale,
  FSM clip-key map). Make `buildSegmentBoxes` / `resolveFootBoneIds` / FSM clip-selection consume it.
  *Verify:* the existing humanoid still works unchanged **and** `character_wolf.anim` loads with
  correct segment boxes + a selectable idle. **Stress:** load all 4 creature rigs.
- **P1 — Humanoid grounding (cheap, NOT free).** Enable the existing foot-IK by default on uneven
  ground. **Honest caveat:** the shipped IK was built and tuned *for stairs* — its own header
  comment (`AnimatedVoxelCharacter.h:531-534`) says it's a no-op on flat ground and for idle/walk
  "where the authored clip already has feet at floor level," i.e. it partly dodges the attempt-#3–5
  skating problem by *staying off* during normal walk cycles. Enabling it broadly on uneven terrain
  must re-prove the skating invariant, not assume it (foot-lock is the anti-skating mechanism and
  exists — but it's unproven in the always-on walk-cycle case). *Verify:* red-before-green via the
  P0a oracle — foot-skate/knee-inversion metrics on the stairs/slopes/rough gauntlet with IK off
  (failing), then on (slide ≤ ε, zero inversions). Also delivers the **T3 runtime probe**
  (full-skeleton dump endpoint + `/api/animation/validate`), since P1 is the first live consumer.
- **P2 — Retarget stage.** One Mixamo clip → rig-abstraction bind → pinned in-engine, provably
  correct. Then wire Path A and Path B behind the same stage. *Verify:* a non-Mixamo SMPL clip plays
  correctly on the humanoid.
- **P3 — Procedural creature locomotion.** FABRIK + phased gait on the rig abstraction; target the
  spider first (it has **BoxCount 0** — needs a voxel model too, and is the cleanest non-biped).
  *Verify:* gait invariants (§9.2) hold on the gauntlet — duty factor, ≥k feet planted, zero skate;
  **stress:** many creatures, extreme terrain, Small↔Large scale.
- **P4 — Blend quality (humanoid).** Motion-matching-lite or 2D blendspace to replace the single
  crossfade. *Verify:* speed/direction changes are pop-free.
- **P5 — Reactivity.** Blend-to-canned hit reactions + light physics nudge for both branches.
- **(Parked) P6 — Learned physical control (C2).** Only if physical combat reactions become the
  headline and P5 proves insufficient. Requires the articulated solver + ML runtime — a separate
  research track, scoped on its own.

---

## 11. Open questions for the owner (the real forks)

1. **Endorse the split-by-body-plan recommendation** (§6), or do you want a single unified paradigm
   even at the cost of the creature goal (A) or the humanoid quality (B)?
2. **First creature target** for P3 — spider (cleanest non-biped, but needs a voxel model built),
   quadruped/wolf (has a model), or a size-variant human (tests scaling with least new art)?
3. **Humanoid rig diet** — keep the 65-bone Mixamo rig (max clip compatibility) or design a leaner
   rig (easier authoring/perf, but retarget-on-import becomes mandatory)?
4. **Format** — keep `.anim` text short-term (recommended) or make a binary/rig-metadata format part
   of P0?
5. **Path A vs Path B priority** — record-yourself first (fastest to *your* motions) or
   text-to-motion first (matches "algorithmic," but humanoid-only and quality-variable)?
6. **Reactivity ambition** — is "blend-to-canned + nudge" (P5) enough, or is true physical reaction
   (C2) a headline feature worth the research track?
```

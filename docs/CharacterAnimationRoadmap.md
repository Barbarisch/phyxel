# Character & Animation Roadmap

> **Status:** PLAN, written 2026-10-01. Nothing in §5 is built. Every phase goes through
> `/design-check` before code, then the owner's go. This is the ONE forward plan for characters
> and animation. It replaces `CharacterLibraryPlan.md`, `CharacterAnimationV2.md` and
> `MotionBricksIntegrationPlan.md` (retired 2026-10-01, in git history) and the forward sections
> of `AnimationSystemV3Plan.md` (A6, A7, T2, T3, §8, §9), which is now the closed build ledger
> for A0–A5. `UniMateIntegrationPlan.md` stays as the detailed sub-plan for phase R3.

## 1. The goal, in the owner's words (2026-10-01)

> "A full set of rich animations for every character in the DnD world along with animation
> variations where appropriate, backed by models we have generated with Meshy.ai and converted to
> voxel format. … MotionBricks and kimodo.cpp to be the basis or at least design driver for the
> rich animation features of the engine. … developers to be able to add to the engine or their
> video games worlds seamlessly."

The goal breaks into four outcomes. Each gets a finish line a test can check, so "are we there
yet" has a number instead of an impression:

| # | Outcome | "Done" means |
|---|---|---|
| G1 | Rich animation for every D&D character | Every catalogue entry (336 monsters, 10 races × 12 classes) passes its body plan's **motion vocabulary** in the coverage report (§4) |
| G2 | Variations where appropriate | Every high-frequency state (idle, locomotion, basic attack, hit, death) has ≥ 3 reviewed variants per body plan, picked per character at runtime |
| G3 | Meshy-generated voxel models | Every catalogue entry is backed by a Meshy-sourced voxel rig that passed the import gates; no procedural-box body remains in shipped content |
| G4 | Seamless extension | A developer adds a character to the engine or to their game project with one manifest and one command (or MCP call), and gets lint, coverage and a live spawn check back |

## 2. Where we stand (measured 2026-10-01)

**Runtime — strong.** A0–A5 built and on `main` (`72854bb6`): one clip-selection site driven by
JSON body plans; typed `clip_meta` schema with factor coordinates; transition graph with phase
sync; masked/additive layers; grip/load from D&D item data; seat fit + seated solve; terrain
grounding on by default. Full suite 4152 pass / 5 pre-existing failures. Ledger:
`AnimationSystemV3Plan.md` §4b.

**Content — thin outside humanoids.** 84 rigs. Clip counts by what the 336 monsters use:

| Clips on the backing rig | Monsters |
|---|---|
| ≤ 5 (idle/walk/run/attack/death) | 203 |
| 6–20 | 23 |
| 21–90 (humanoid-derived rigs) | 60 |
| 159 (`humanoid`) | 50 |

172 monsters sit on parametric creature-forge rigs, 31 on the five Meshy quadrupeds, 50 share
the plain humanoid. All 10 races and 12 classes play the one humanoid set; 13 appearance presets
change proportions only. No class or race movement style exists.

**Variations — draft only.** Phase jitter and factor layers. 18 UniMate drafts (6 motions × 3)
sit `pending` in `unimate_review.json`. Nothing picks variants at runtime.

**Meshy — one lane, five quadrupeds.** `character_import.py --new-rig` + `retarget_quadruped.py`,
one creature at a time, hand-written binding. No Meshy biped. The imported-rig bind-rotation
render defect is not reproduced but not ruled out.

**Generative motion.** MotionBricks: optional runtime provider built behind `IMotionSource`, off
by default, G1 skeleton only, 2.43 s first window at 10 agents / 18.5 s at 100. UniMate: offline
drafter for arbitrary skeletons, M0 done, M1b review pending. kimodo.cpp: never evaluated before
this plan.

**Developer path — not seamless.** The parts exist (body-plan JSON, `clip_meta` schema,
`bindings.json`, `character_import.py`, creature forge, `anim_lint.py`, MCP tools). But the
developer docs (`AnimatedCharacter.md`, `CharacterAnimationGuide.md`, last touched 2026-07-21)
predate A2–A5; adding a creature takes several scripts and hand edits; the `.anim` parse cache
needs an engine restart; whether a game project can add its own rigs without touching engine
`resources/` is unverified.

## 3. Decisions this plan takes (and what it rejects)

**Kept as the system:** T1 from `AnimationSystemV3Plan.md` §0 — a library of motion priors
(mocap + generated clips) bent to the world by the runtime layers A3–A5. Generative models are
**authoring tools that fill the library offline**, not runtime dependencies.

**MotionBricks and kimodo as design drivers, not dependencies.** What we take from them:
1. **Intent in, pose out** — a character is driven by intent (move, face, act) and the motion
   layer resolves it. Already the shape of A3's transition graph and the `IMotionSource` seam.
2. **Text as the authoring interface** — "a dwarf stomps forward heavily" is how a clip is
   requested. Realised offline by UniMate.
3. **Style as a coordinate** — MotionBricks' style tokens map to our factor axes (§3b of the
   ledger: gait, grip, load, condition, mood) plus a new `style` factor for race/class movement.
4. **A measured oracle** — every generated motion is judged by numbers (skate, float,
   penetration, continuity) before a human sees it. Built in A1.

**Rejected or retired (each with the trigger that would reopen it):**

| Idea | Verdict | Why | Reopen if |
|---|---|---|---|
| MotionBricks as RUNTIME locomotion | **Rejected** | G1-only robot skeleton; 18.5 s for 100 agents; Vulkan build failed; locomotion is already covered by clips + A3 + A5 | A humanoid-skeleton model ships with crowd-scale latency |
| MotionBricks as an OFFLINE generator ("capture mode") | **Adopted** (owner, 2026-10-01) | Its cost doesn't matter offline: drive a humanoid with scripted intent, capture the learned motion, retarget G1 → humanoid (`g1ToMixamoRetargetMap` exists), write clips with `clip_meta`, review. A second generative source next to UniMate for locomotion and transitions | — |
| kimodo.cpp | **Superseded by UniMate** | Humanoid-only text-to-motion (SMPL-X, SOMA, G1). UniMate does the same job offline AND animates arbitrary skeletons, which is where 203 monsters need it | UniMate humanoid quality fails M1b, or we want text-to-motion inside the editor without Python |
| T2 learned runtime motion (Neural State Machine spike) | **Rejected** | A4's constraint solve delivered "any chair" without training data; no ML runtime in the engine | A contact task A4-style solves cannot handle |
| T3 physics-based learned control | **Rejected** | Research-scale; needs an articulated solver the engine lacks | Unchanged from the ledger's unpark conditions |
| Meshy's own animation library | **Rejected** (owner, 2026-08-25) | Retarget our packs instead; one motion source of truth | — |
| Local open-weight shape models (TRELLIS, TripoSR, Hunyuan3D) | **Rejected** | Meshy is the standard (owner, 2026-10-01) | Meshy licence or cost changes |
| Creature forge as a MODEL source | **Retired over time** | Meshy is the standard; forge rigs are replaced species by species as Meshy versions pass R2's gates | A body plan Meshy cannot shape at voxel scale |
| Video mocap (V2 "Path A") | **Parked** | UniMate + Mixamo cover the need | A motion the library and UniMate both cannot produce |
| Procedural humanoid locomotion, PD ragdoll | **Rejected** (history, `LessonsLearned_ProceduralAnimation.md`) | Five failed attempts | — |

**Kept:** the MotionBricks provider code (repurposed as R3's capture tool, still off at runtime),
creature-forge **spec and skeleton** generation (body plans for non-biped species are
still useful as the skeleton Meshy meshes are bound to — see R2), Mixamo as the humanoid mocap
source, the 12-rig quadruped mocap library, UniMate.

## 4. The measuring stick: motion vocabulary + coverage report

"Every character in the D&D world" needs a definition a test can check. Two data files and one
tool make it measurable, and they are the first thing built (R1):

- **Motion vocabulary per gait class** (`resources/anim/motion_vocabulary.json`, one file; see
  R1 decision 4): the states every character of that class must be able to play. Example for
  `quadruped_clips`: idle, walk, run, turn, hit, death, plus whatever its stat block adds.
- **Requirements per catalogue entry**, derived from the D&D data, not hand-written: movement
  modes from the stat block (`walk`, `fly`, `swim`, `climb`, `burrow`), attack kinds from its
  actions (`bite`, `claw`, `weapon`, `breath`, `spell`, `multiattack`), size, and for races ×
  classes the class's signature actions (spellcasting, unarmed strikes, ranged weapons, rage…).
  The exact rules are R1's decisions 1, 2 and 6.
- **Coverage report** (`tools/anim_pipeline/coverage_report.py`): for every catalogue entry,
  which required states its rig can play, how many reviewed variants each has, and the overall
  score. This is the scoreboard every later phase moves. Today's numbers become its red baseline.

## 5. Phases

Each phase lists its contract, the depth it is validated to, and its gate. Order is deliberate:
measure first, then models, then motion, then variation, then polish of the runtime, with the
developer path built into every phase rather than bolted on at the end.

### R0 — Consolidate (this document; ½ day) — **DONE 2026-10-01**
Retire superseded docs, record rejections in `EngineAdvancesResearch.md`, mark stale developer
docs, point every reference at this roadmap. Owner answers 2026-10-01: **A5 closed** at the
measured float (0.147 u on 1/3 u risers; the heel-off residual is a later polish item, not a
blocker); **MotionBricks code kept** and repurposed as an offline capture tool (R3); every
creature, race and class gets a checklist (R1); **Meshy credits are watched** (R2).

### R1 — Vocabulary and coverage scoreboard (3–5 days) — gate READY 2026-10-01 → **BUILT 2026-10-01**
Contract: §4. Validation L2 (the report runs on real data and its numbers are checked against
hand-counted samples) with a red test that fails on today's content. Deliverables: vocabulary
blocks for the shipped body plans, the requirement deriver over `resources/monsters/` +
`resources/races/` + `resources/classes/`, `coverage_report.py`, a pinned baseline in this doc,
and **`docs/CharacterChecklist.md` — generated, never hand-edited**: one row per monster, per
race and per class, listing its model status (Meshy / forge / humanoid-shared), each required
state with ✅ / ⚠️ draft / ❌ missing, and variant counts. Every later phase is "move rows to
green"; the owner reads progress there. No engine code.

**Gate 2026-10-01: NEEDS WORK (6 items) → decisions folded below.**

1. **Movement modes — re-ingest, don't infer.** All 336 stat blocks carry `speed` as one integer;
   fly/swim/climb/burrow were dropped by `tools/ingest_srd_monsters.py` (it reads dnd5eapi.co's full
   speed table and keeps `walk`, line 138). Fix at the source: the ingest also writes
   `movementModes: {walk, fly, swim, climb, burrow, hover}` in feet; `speed` stays the walk int, so
   every existing loader and the combat code are untouched. The hand-curated files (`beasts.json`,
   `humanoids.json`, `powerful.json`, `undead.json`) get the same field looked up by id against the
   same API; an id the API doesn't know keeps walk-only and its checklist row says
   **"movement unverified"** instead of guessing. Re-ingest is a data diff reviewed in the commit.
2. **Attack kinds — a table plus one schema key.** `resources/anim/attack_kinds.json` maps stat-block
   attack names to a kind by keyword: `bite, claw, tail, slam, gore, sting, beak, talons, hooves,
   tentacle, breath, spit, constrict, weapon_melee, weapon_ranged, spell, aura` (measured on the
   catalogue: Bite 147, Claw/Claws 87, Tail 32, breath weapons 24, Slam 14, Hooves 13, Beak 12…).
   Names no rule matches land in an **"unclassified"** bucket listed in the report, never dropped.
   Non-attack actions (Frightful Presence ×21) map to `aura` — an animation need, not a strike.
   Clips declare what they are with a new `clip_meta` key **`attackKind`** (enum = the kinds
   above, `factor: true` so R5 can select by it). Humanoid weapon clips need no new tag: the
   resolver derives `weapon_melee`/`weapon_ranged` from their existing `meleeFamily` /
   `weaponFamily`. Same commit updates the pins: `ClipFactorSchemaTest`, `test_clip_meta_schema.py`.
3. **Clip resolution — one fixture, two languages.** Creature rigs carry almost no `clip_meta`
   (`wolf_meshy.anim` 0 lines, `forge_dragon_adult.anim` 1, `humanoid.anim` 124), so the report must
   find "the clip for state X" the way the engine does: race/NPC `animationMapping` → plan
   `clipDefaults` → `clipFallbacks` (`AnimatedVoxelCharacter::clipForState`, `.cpp:3074`). The
   Python resolver writes `tests/fixtures/clip_resolution.json` (every shipped rig × every FSM state
   → clip or none); a new gtest loads every rig, asks the engine for the same table and asserts
   equality with the fixture; the Python test asserts the fixture equals its current output. Any
   drift in either language fails a test — the seat-margin parity pattern.
4. **Vocabulary keyed by gait class, not by plan.** 40 body plans fall into 5 gait classes
   (`quadruped_clips` 26, `dragon_clips` 6, `arachnid_clips` 5, `biped_fsm` 2, `flying_clips` 1).
   One file, `resources/anim/motion_vocabulary.json`, holds a base list per gait class; a plan may
   add `vocabularyExtra` for something only it does. Everything else a character needs comes from
   its own stat block (attack kinds, movement modes), so a new species costs zero vocabulary edits.
   Base lists (variant targets in brackets, outcome G2): `biped_fsm` — idle[3], walk[3], run, jump,
   fall, land, turn, strafe, crouch, sit set, stairs, hit[3], death[3]; every other class — idle[3],
   walk[3], run, turn, hit[3], death[3].
5. **Variants and review status — one rule, one source.** Two clips are variants of the same
   requirement when they resolve to the same state (or `attackKind`) with the same factor values
   and `role: base`. Review status becomes a `clip_meta` key **`review`**
   (`approved | shipped | draft | rejected`, `toolOnly`). A clip with no key reads as `shipped`
   (everything that existed before review existed); UniMate and MotionBricks-capture promotions
   write `draft`; the owner's verdict in the review panel writes `approved` or `rejected`.
   `unimate_review.json` stays the review *workflow* log; the clip's own key is the *status*. The
   checklist shows ✅ approved/shipped, ⚠️ draft, ❌ missing; rejected clips do not count.
6. **Rows: 336 monsters + 10 races + 12 classes, not combinations.** A race row checks body,
   proportions and (from R4) movement style; a class row checks its signature actions; a 120-cell
   race × class matrix adds nothing until R4's styles exist, so it is a report view, not rows.
   Class needs come from data through `resources/anim/class_actions.json`: `spellcastingType` →
   cast families, `weaponProficiencies` → melee/ranged families, named features → actions
   (Martial Arts → unarmed combo, Rage → rage stance, Sneak Attack → sneak, Wild Shape → transform,
   Bardic Inspiration → perform). Features with no entry are listed as unmapped, not dropped.
   Monster rows also show their model source from `bindings.json`: Meshy, forge, humanoid-shared.

**Tests (written red first):** (a) row count = 358 and every unclassified attack and unmapped
feature listed; (b) a `wolf_meshy`-backed monster with Bite + Claw shows at least one ❌ attack
(today one generic `attack` clip) — control: a longsword fighter's melee shows ✅; (c) ten
hand-counted rows across all five gait classes match the report; (d) two runs produce
byte-identical JSON and markdown; (e) the clip-resolution parity pair (decision 3); (f) the schema
pins (decision 2). Validation L2.

**Re-gate 2026-10-01: READY.** Checked on re-gate: the `srd_*.json` files are untouched since
the ingest commit `7cd5e2ae` (2026-07-26), so re-running the ingest with `movementModes` is a
clean, reviewable data diff, and it already preserves the hand-added `rig` field (`ingest_srd_monsters.py:156,199`).
One condition carried into the build: the parity gtest (decision 3) loads every bound rig, and
`humanoid.anim` alone is a 58 MB parse — measure its Debug run time first; above ~60 s it moves
to the stress suite rather than shrinking its rig list. Build order: schema keys + pins →
ingest `movementModes` → attack-kind and class-action tables → Python resolver + parity pair →
vocabulary file → report + checklist → hand-count check → pinned baseline in this doc.

**Built 2026-10-01 (owner: "lets get to work"), uncommitted.** What landed:
- Schema: `attackKind` (factor enum, now 18 kinds — `touch` added for Paralyzing Touch / Life Drain,
  which are neither slam nor spell), `review` (tool-only status), and `action` (tool-only string —
  class signature moves had no key that could express them). Pins: `ClipFactorSchemaTest` (8/8),
  `test_clip_meta_schema.py`. The 18 UniMate clips now carry `review=draft`; `unimate_promote.py`
  stamps `review=approved` on promotion (promotion IS the owner's approval).
- Movement data: `tools/ingest_srd_monsters.py` writes `movementModes` (and its API paths moved to
  `/api/2014/` — the unversioned path now returns HTML); `tools/backfill_monster_movement.py`
  added the field to all 336 existing monsters by TEXT insertion — **336 lines added, 0 removed**,
  hand-aligned files untouched, rerun is a no-op. All 336 ids were found in the API (none
  "unverified"). Restored: fly 105, swim 60, climb 38, burrow 21, hover 7.
- Tables: `resources/anim/attack_kinds.json` (all 120 attack names classified, 0 unclassified),
  `class_actions.json` (every class feature mapped, 0 unmapped), `motion_vocabulary.json`.
- **Decision 3 changed during the build, for the better:** the engine READING the death and hit
  paths showed it picks those clips by literal name (`death_front`/`death_back`,
  `hit_head`/`hit_stomach`/`hit_rib`), which a Python re-implementation would have had to copy.
  Instead the ENGINE writes the truth: `tests/stress/ClipResolutionFixtureTest.cpp` loads every
  catalogue rig (56), resolves every state through `clipForState` (Death via `die()`, HitReact via
  `hitReact()` + one update) and writes / checks `tests/fixtures/clip_resolution.json`. Python
  only reads it; `test_coverage_report.py` fails if the fixture is stale against the rig files.
  It took **106 s in Debug**, over the gate's 60 s line, so it lives in the stress suite
  (`PHYXEL_WRITE_CLIP_FIXTURE=1` regenerates).
- `tools/anim_pipeline/coverage_report.py` → `build/coverage/coverage_report.json` +
  **`docs/CharacterChecklist.md`** (generated; `build_and_test.ps1` fails if it is stale).
- Tests: `test_coverage_report.py` 6/6 — row count 358 with every attack and feature reconciled;
  the red test (owlbear: Beak ⚠️ via the one generic clip, Claw ❌; control guard's Spear ✅ —
  **the gate named a Meshy wolf with Bite + Claw, but no catalogue monster on a Meshy rig has
  both**); **eleven hand-derived rows, written from the raw fixture/rig/stat-block data before
  the report's rows were read, all matched on the first run**; byte-identical reruns; fixture
  staleness; checklist currency. `flying_clips` has no catalogue rig (only the unbound wyvern),
  so the hand rows span the four gait classes in use.

**Baseline, pinned 2026-10-01** (this is what R2–R5 move):

| | Rows | Avg score | Fully covered |
|---|---|---|---|
| Monsters | 336 | 53 % | 39 |
| Races | 10 | 89 % | 0 (style arrives in R4) |
| Classes | 12 | 78 % | 3 |

5,005 needs: **3,187 ✅ · 284 ⚠️ · 1,534 ❌**. Variant targets met: **116 of 2,003**. Biggest
holes: turns 227 ❌ each way, HitReact 204 ❌, Run 172 ❌, every movement mode ❌ (fly 105, swim 60,
climb 37, burrow 21, hover 7), attacks 121 ✅ / 283 ⚠️ stand-in / 215 ❌, class signature actions
(rage, sneak, wild shape, perform, lay on hands, second wind, turn undead, channel divinity,
deflect) all ❌. Monster models: forge 172, other 83, humanoid-shared 50, Meshy 31.

**Defects the scoreboard surfaced (logged, not fixed — R1 changes no engine code):**
1. **Every Meshy-backed monster (31) dies playing its idle.** `die()` only looks for
   `death_front`/`death_back`; the Meshy rigs' clip is `death`; `meshy_quadruped.json` has no
   `clipDefaults`/`clipFallbacks` entry, so `clipForState(Death)` falls through to the legacy
   `"idle"`. Same literal-name pattern for hits (`hit_head`/...) leaves 203 monsters with no hit
   reaction. Fix belongs in R5 (or a small fix item): name-free death/hit selection through the
   plan, which the A2 "nothing keys on names" rule already demands.
2. **Plan resolution picks near-misses:** `forge_spider_giant` resolves to the `forge_cephalopod`
   plan; all four forge dragon rigs and `forge_griffon` resolve to `forge_dragon_adult`. Not wrong
   for clip names today (they share clip tables), but a plan carries legs/masks/vocabulary, so it
   will matter in R3. Check `planForSkeleton` scoring before R3.

### R2 — Meshy → voxel character pipeline (2–3 weeks) — **BUILT 2026-10-02 (stress batch + all gates); owner review open**
Contract: one manifest + one command takes a Meshy model to a spawned, linted, bound character.
- **Research first:** which body plans Meshy's auto-rig covers (exercised only on quadrupeds
  here), its export formats and bone conventions, commercial terms, batch API.
- **The voxel binder.** A voxel character has no skin weights: every box belongs to one bone.
  So a Meshy *mesh* can be bound to *our* skeleton directly — voxelize, fit the body plan's
  skeleton (from the forge spec or a reference rig) to the voxel volume, assign each voxel to its
  nearest bone segment. This removes the dependency on Meshy's rigger for spiders, dragons,
  serpents, oozes and every plan it does not cover.
- Pin the bind-rotation render defect with a red test before importing at volume.
- Manifest (`resources/characters/<id>.json`): model source, body plan, appearance presets,
  clip sources, D&D binding. Command `tools/character_add.py` + MCP `add_character`.
- **Credit watching (owner rule, 2026-10-01):** `tools/meshy_credits.py` runs before and after
  every Meshy job and logs the spend; below **10 %** of the allotment it exits 2 and the owner is
  warned before anything else is spent. Batch jobs check between items, not only at the end.
  Needs `MESHY_API_KEY` in the environment (not configured on the dev machine as of 2026-10-01).
- Gates per import: voxel aesthetic (sub-voxel detail rule), lint, bone-name independence,
  oracle on idle/walk, live spawn screenshot at a stated pose.
- Stress: batch of 20 species across 5 body plans; every one passes or is refused with a reason.
Validation L4. Output: the first Meshy species replace forge rigs; coverage report moves.

**Gate 2026-10-01: NEEDS WORK (7 items) → decisions 1–6 folded below; item 7 is the owner's (Meshy licence for shipped games: confirmed 2026-10-01).**

Facts the gate established (Meshy API docs, 2026-10-01): rigging is `POST /openapi/v1/rigging`,
5 credits, **humanoid bipeds only** ("programmatic rigging currently only works well with standard
humanoid (bipedal) assets"), input faces +Z (our model-space convention). Text-to-3D is
`/openapi/v2/text-to-3d`, preview then refine, `pose_mode` a-pose / t-pose, each task reports
`consumed_credits`. In the repo: the five `*_meshy` rigs share ONE identical 27-bone quadruped
template (same bone list, md5-equal) — no neck, no jaw, legs that match none of our mocap, a baked
90° frame on every bone (`retarget_quadruped.py` header). The owner remembered it as "a single
skeleton for quadruped, not right for our needs" — confirmed. Every shipped Meshy rig has **0**
explicit voxel colours (`wolf_meshy`: 825 boxes, 0 coloured), so Meshy's texture is lost today.

1. **Voxel size and part budget follow D&D size.** Character voxels are their own class, finer
   than static microcubes; the standard pitch is **1/18 u** (the humanoid's measured 0.055 spacing,
   snapped to the world grid). Huge steps to **1/9** and Gargantuan to **2/9**, so a dragon is not
   made of 100k parts. Height (bipeds) or length (everything else), with 1 u ≈ 1 m, must sit in its
   D&D band — clamped at the manifest with the reason written there:

   | Size | Height / length (u) | Pitch | Part budget |
   |---|---|---|---|
   | Tiny | 0.3 – 0.6 | 1/18 | 600 |
   | Small | 0.6 – 1.2 | 1/18 | 1,500 |
   | Medium | 1.2 – 2.4 | 1/18 | 3,000 |
   | Large | 2.4 – 4.9 | 1/18 | 6,000 |
   | Huge | 4.9 – 9.8 | 1/9 | 8,000 |
   | Gargantuan | 9.8 – 20 | 2/9 | 12,000 |

   Voxelization keeps the SHELL only (interior voxels are never visible). The budget exists because
   every character shares one 262,144-part buffer and overflow makes characters invisible (MEM
   Character Instance Buffer): a reference crowd of 40 Medium + 10 Large + 2 Gargantuan must fit
   with 25 % headroom, checked by a test.
2. **Colour comes from Meshy's texture, quantized.** Each voxel samples the base-colour texture at
   the nearest surface point; the model's colours are reduced to a palette of at most **24** (k-means
   in Lab) and written as the box's explicit `r g b`. A palette keeps the voxel look; raw texels
   speckle. Test: an imported textured model has every box coloured and ≥ 2 palette entries (red
   today: 0 coloured boxes).
3. **Skeletons are ours, fitted to the Meshy mesh — the shared Meshy quadruped template is retired
   for new imports** (the five existing rigs are rebound when their species come up in the
   checklist). One binder for every body type:
   - **Humanoid bodies:** Meshy's rigging API (5 credits) is used only for its **joint positions**,
     as landmarks. Our humanoid skeleton is placed on them by role, so the 159-clip humanoid set
     plays unchanged. Meshy's bone names and frames are never used.
   - **Everything else:** the skeleton comes from the creature-forge species spec — it carries the
     necks, jaws, wings, tails and leg counts that species needs. It is fitted by measuring the
     voxelized mesh: body axis (Meshy faces +Z, so forward is known), head and tail as the +Z / −Z
     extremes, leg columns as the vertical voxel clusters under the body mass, wings as the lateral
     extents above it. Each spec segment is scaled to its measured landmark.
   - **Binding:** every voxel goes to the bone whose segment it is nearest. Pass bar (measured,
     per import): ≥ 98 % of voxels within 0.15 × body height of their bone; every voxel's
     left/right side matches its bone's side; and the species' own walk clip passes the A1 oracle
     (no self-intersection beyond the calibrated band, feet reach the ground).
   - **Generation pose:** humanoids `pose_mode: a-pose`; everything else is prompted "standing,
     legs straight, mouth closed, wings spread flat" so the binder can see every part.
   - The species skeleton must carry the bones its stat block needs (a jaw if it bites, a tail if
     it tail-attacks) — the R1 checklist reports a missing one.
4. **R2 ships moving characters; R3 enriches them.** A creature bound to its forge species skeleton
   inherits that species' generated clips; a humanoid inherits the humanoid set. So an import is
   playable the moment it lands.
5. **Manifest is the one source.** `resources/characters/<id>.json`: `id`, `serves` (the monster /
   race ids it backs), `size` (D&D), `height_u` (optional, clamped into the size band),
   `body` (`humanoid` or a forge species id), `source` (`{"meshy": {prompt, task_ids}}` or
   `{"file": "<model.glb>"}`), `palette` (≤ 24). The command `tools/character_add.py <manifest>`
   (and MCP `add_character`) writes the rig and **writes the `bindings.json` entries for `serves`**;
   a test fails if a binding backed by a manifest disagrees with it. It echoes JSON: rig path,
   pitch, parts vs budget, palette size, binder metrics, lint result, checklist rows before and
   after, screenshot paths, credits spent (from `consumed_credits` + `tools/meshy_credits.py`).
6. **Tests never spend credits.** Model fixtures live in `tests/fixtures/characters/`: until a real
   import exists, a generated low-poly textured GLB (a script builds a quadruped and a biped with a
   two-colour texture); afterwards, the first real Meshy outputs at low poly count. Red tests,
   written first: (a) determinism — same model + manifest → byte-identical `.anim`; (b) colour
   (above); (c) binder side-consistency and distance bar on the synthetic quadruped (no binder
   exists today); (d) part budget per size, including the crowd check (today's 0.05 pitch blows the
   Gargantuan budget); (e) manifest ↔ binding agreement. L4 per import on CharacterTestbed: spawn at
   5, 20 and 40 u, screenshot each plus `get_bone_positions`, owner reviews. That is also where the
   far-distance "unposed" render bug would show; it has never been reproduced, so it gets a visual
   check, not an invented red test.
7. **Licence — RESOLVED (owner, 2026-10-01):** Meshy output may ship in games ("that is the
   whole point of the service"). Imported characters are packaged like any other rig.

**Re-gate 2026-10-01: NEEDS WORK (4 items) → folded (owner chose option (a) for item 11).**

8. **Budget wins over pitch.** The pitch table is a preference, the budget is the rule: the
   voxelizer uses the FINEST of 1/18, 1/9, 2/9 whose shell fits the size's part budget, and the
   command's echo reports any step down and why. Reason: a Large creature at the top of its band
   (4.9 u) is ~2× `bear_meshy`'s length, ~4× its shell area — ~10,000 parts at 1/18 against a
   6,000 budget (`bear_meshy`: 2,603 parts at 0.05 u). Deterministic: same model, same choice.
9. **The species spec is completed from the stat block before binding.** Only 2 of the 32
   creature-forge specs (`tools/creature_forge/specs/`) have a jaw, yet Bite is the commonest
   attack (147 monsters). The import step checks the spec against the R1 needs for the monsters
   the manifest `serves` and adds what is missing using the forge's own part types: a jaw for
   `bite`, a tail chain for `tail`, wings for `fly`, extra limb chains for `tentacle`. The
   completed spec is written back to `tools/creature_forge/specs/` (reviewed in the diff), so the
   forge and the binder agree. The R1 checklist confirms the part exists.
10. **Palette quantization is deterministic.** k-means initial centres are taken from the sorted
    colour histogram (most frequent first, ties by Lab value), never a random seed; test (a)
    covers it.
11. **Source models are committed to git (owner, option a).** Every downloaded Meshy model goes to
    `resources/characters/source/<id>.glb`, next to the manifest, as the Mixamo sources already do
    (`resources/mixamo_imports/`, 113 MB). Requested at a moderate `target_polycount` (default
    30,000) and only the GLB format, to keep each file small; the command reports each file's size.
    Expected scale: ~2 MB × ~330 characters ≈ 650 MB over the life of the catalogue — accepted by
    the owner over LFS or out-of-repo storage. Rigs can always be rebuilt when the binder improves.

**Re-gate 2026-10-01 (third pass): READY.** Verified on this pass: the engine already reads an
explicit per-box `r g b` (`AnimationSystem.cpp:106`), so colour needs no engine change; and the two
forge specs that mention a jaw do so only in shape text (`reptile.json` "long jaw") — **0 of 32
specs have an articulated jaw**, which decision 9 adds through the forge's generic joint/chain
format. Conditions carried into the build:
1. Every import that completes a species spec regenerates that forge rig, so it also regenerates
   `tests/fixtures/clip_resolution.json` (stress test, `PHYXEL_WRITE_CLIP_FIXTURE=1`) and the
   checklist; `test_coverage_report.py` already fails if either is stale.
2. The first LIVE import needs `MESHY_API_KEY` from the owner; everything before it runs offline.
3. ~~Imported characters are excluded from packaged games until the owner confirms Meshy's licence~~
   (licence confirmed 2026-10-01; no exclusion)
   (item 7).
Build order: red tests + synthetic fixture models → pitch / budget / shell voxelizer → colour
palette → spec completion → binder (humanoid landmarks, species fit, assignment, pass bar) →
manifest + `character_add.py` + binding writer → MCP `add_character` → first live import.

**R2 build ledger (2026-10-01): offline pipeline BUILT; first live import blocked on `MESHY_API_KEY`.**
Red first: `tests/test_character_pipeline.py` failed at import (no `tools/characters/`); now
19/19 pass. Files: `tools/characters/{make_fixture_models,voxelize,palette,spec_complete,binder,
character_add}.py`, `tools/character_add.py` (CLI), MCP `add_character` (offline tool).

| Measured on the synthetic fixtures | value |
|---|---|
| quadruped, Medium, 1/18 | 1,190 parts (budget 2,500), palette 3 |
| quadruped, Large at 4.8 u | 1/18 overflows (9,436 > 6,000), steps to 1/9, reported |
| biped, Medium, 1/18 | 886 parts |
| reference crowd (40 M + 10 L + 2 G) | 184,000 of 262,144 |
| quadruped → forge_direwolf | within 1.00 (flat 0.89), side 0 |
| biped → humanoid | within 0.993, side 0, both arms rotated 49° onto the A-pose |
| wrong-skeleton controls (humanoid / serpent / bat) | within 0.33 / 0.965 / 0.79: all fail |

Deviations from the gate, each measured:
1. **Medium budget is 2,500, not 3,000.** The gate's own crowd check failed at 3,000.
2. **The distance bar is per bone, not a flat 0.15 × height.** Control: the shipped rigs fail the
   flat bar against their OWN bones (forge_bear 58 %, forge_serpent 8 %, humanoid 95.7 %); it
   measures body thickness, not binding. A voxel now passes within max(0.15 × length,
   1.25 × the species rig's own envelope for that bone × fit scale); the flat number is still
   echoed (`within_frac_flat`). Assignment uses the same relative distance: plain nearest-segment
   bound 16 front-torso voxels of the quadruped to the head.
3. **Humanoid fit is geometric until a real Meshy rig exists:** uniform height scale, arm chains
   rotated about the shoulder onto the mesh arm (mean direction) and stretched to its reach, legs
   moved onto the mesh leg columns. Meshy rigging landmarks replace this at the first live import.
4. **Creature fit adds a body-axis step:** head/spine/tail path joints go to the centre of the
   voxel cross-section at their z (the gate's "segment to its landmark").
5. **`feet_ground_err_u` is 0 by construction** (legs are snapped to the low voxels), so it does not
   discriminate. The gate's walk-clip oracle check is the real feet test; it runs at the
   per-import L4 step, not offline yet.
6. **Beak counts as a jaw** (owlbear needs `attack:beak` + `attack:claw`; the bear spec gets a jaw).
7. **Spec write-back inserts only the new entries** (a re-dump turned one jaw into a 1,125-line
   `bear.json` diff); round-trip asserted, 13–16 changed lines per species. The bindings map is
   rewritten in its exact checked-in style (indent 2, ASCII escapes: byte-identical, measured).
   Dry runs write nothing (tested).
8. **No new body plan per import:** the engine picks plans by bone names, and the capsule is
   extent-derived (`xz_extent`), so the species plan fits a rescaled rig.

Engine smoke (L4, Release, CharacterTestbed, 2026-10-01): two binder-written rigs from the
fixtures loaded, rendered with per-box colours and played `walk`; no bone box went below the
ground. Every character in that scene renders washed out, the shipped forge_direwolf included,
so colour judgement needs neutral light. The smoke rigs were deleted after the check.
Found on the way: `humanoid.anim` binds 48 pelvis boxes to `LeftHandPinky4` / `RightToe_End`
since its first commit (logged in `docs/StructurePipelineGaps.md`).
Key: env `MESHY_API_KEY`, else the git-ignored `tools/meshy.local.json` `{"api_key": "..."}`
(owner provides it; licence confirmed). Next: first live import, then the per-import L4 review at
5 / 20 / 40 u with the walk oracle.

**First live import — owlbear (2026-10-01): DONE, owner review pending.** Manifest
`resources/characters/owlbear.json` (Large, body `bear`, serves `owlbear`); source
`resources/characters/source/owlbear.glb` (5.0 MB, committed per decision 11); rig
`resources/animated_characters/owlbear.anim`; bindings regenerated through `bindings_map.json`.

| | try 1 | try 2 (shipped) |
|---|---|---|
| Meshy credits | 30 (5,161 → 5,131) | 30 (5,131 → 5,101); total 60, 98.8 % left |
| model | upright owl, wings spread | four-legged owlbear, beak at +Z, no wings |
| binder | within 0.970: REFUSED, nothing written | within 0.993, side 0, feet 0.11 u (bar 0.14) |
| parts | — | 2,032 at 1/9 (1/18 gave 8,814 > Large budget 6,000) |

Defects the live import exposed, each fixed red-before-green (`tests/test_character_pipeline.py`, 28 tests):
1. **Prompt suffix**: "wings spread flat" was appended to every creature, and Meshy drew a winged
   owl. Wings are now asked for only when a served monster flies; quadruped bodies are asked to stand
   on four legs (`prompt_for`).
2. **Jaw joints left behind by the body-axis fit**: the owlbear's Jaw sat ABOVE its skull. Side
   branches now move with the axis joint they hang from.
3. **No voxels on the jaw**: a completed jaw has no envelope (the regenerated `forge_bear` binds
   0 boxes to it), so the skull took the whole head. The jaw is now fitted to the mesh (hinge 35 %
   up the head in front of it, tip at the beak) and the lower head goes to it: 38 voxels on the
   owlbear.
4. **Clip root keys ignored the fit**: keys were mapped as s·key + t, so every owlbear clip lifted the
   body ~0.46 u (live readback: lowest bone box 18.14 vs ground 17). Keys are now bind_fitted +
   s·(key − bind_old); after the fix the owlbear reads 17.69 vs the shipped forge_bear's 17.66.
   The engine smoke on the synthetic rigs had only checked for sinking, which is how it was missed.
5. Also: tests now use a frozen jaw-less bear spec (`tests/fixtures/characters/bear_spec_nojaw.json`),
   because live imports complete the real specs.

Species side effects: `bear.json` gained the jaw (16-line diff) and `forge_bear.anim` was regenerated
(same pre-existing leg-fragment warnings as without the jaw); clip fixture (56 rigs) and checklist
regenerated (totals unchanged; the owlbear row: idle/walk/death OK, beak = generic attack, claw/run/
turns/hit missing → R3).

L4 (Release, CharacterTestbed): spawned at ~5, 21 and ~45 u: posed, walking, paws on the grass; at
the attack's hit frame the beak visibly splits open. Colour reads washed out in that scene as every
character does (forge controls included) — judge colour under neutral light. Owner review open.

**Imports 2 + 3 — dire wolf, orc (2026-10-02): DONE, owner review pending.** 30 credits each
(5,101 → 5,041, 97.7 % left). Sources committed: `dire_wolf.glb` 6.1 MB, `orc.glb` 5.9 MB,
`owlbear.glb` 5.0 MB.

| | owlbear | dire wolf | orc (first live humanoid) |
|---|---|---|---|
| body / pitch / parts | bear, 1/9, 2,032 / 6,000 | dire_wolf (+ jaw), 1/18, 3,042 / 6,000 | humanoid, 1/18, 2,030 / 2,500 |
| binder within / side | 0.999 / 0 | 1.000 / 0 | 0.9985 / 0 (girth 1.71) |
| jaw voxels | 37 | 77 | — |
| checklist row | 3 / 9 | 3 / 8 (Death fixed, Run LOST) | 21 / 21 (was 8 / 21) |

The orc was refused first (0.863) and the cause was the binder, not the model:
1. **Leg fit matched the TOE TIP** (the humanoid's lowest leaf) to the foot voxels and pushed both
   legs a foot-length (~0.2 u) backwards. Legs now match the rig's own foot boxes → 0.944.
2. **Bent arms**: one straight-arm rotation cannot fit an elbow; the arm is now fitted per segment
   (upper arm to the elbow region, forearm to the far end; new fixture `biped_bent.glb`) → 0.967.
3. **Girth**: bipeds are fitted by height only, so a bulky armoured body never reached the
   allowance; the humanoid envelope now scales by torso depth, mesh vs rig, clamped 1–2 → 0.9985.
   Controls re-run after the change: quadruped → humanoid 0.37 / 0.60, dire wolf → humanoid 0.49,
   biped → dire wolf 0.74: all still refused.
4. **Engine-skipped bones**: the engine never draws boxes on finger/toe/eye/"end" bones; the binder
   had put 80 biped-fixture voxels there (invisible toes). Now excluded (gaps log 2026-10-02).
5. **Stand-in overrides**: moving a monster to its manifest rig now drops the old rig's tint / scale
   / alpha / appearance / animationMapping (the owlbear had inherited forge_bear's tint and 1.1×).

Known trade (logged, not fixed): `dire-wolf` moved off `wolf_meshy` (which has `run` but whose
death never resolved) onto a rig without `run` — R3 must give the dire_wolf species a run clip.
`wolf`, `worg`, `winter-wolf` still ride `wolf_meshy`. Pinned rows in `test_coverage_report.py`
updated with the reasoning. L4 (Release, CharacterTestbed): orc and dire wolf idle/walk next to
their controls (shipped humanoid, forge_direwolf): lowest bone box within 0.1 u of the control;
posed at ~5, 20 and 40 u. Tests: 133 pass (`test_character_pipeline.py` 26).

**R2 stress batch + gates (2026-10-02): R2 BUILT — every contract item done; owner review open.**
20 Meshy species across 9 forge bodies + the humanoid (600 credits; 4,441 of 5,161 = 86 % left),
plus the first 3 imports. Every item passed every gate or was refused with a recorded reason
(`resources/characters/<id>.json` `status` / `refused_reason`; refused imports serve nothing).

| | imported — all gates pass | refused (reason) |
|---|---|---|
| canine (dire_wolf body) | dire_wolf, wolf, hyena, winter_wolf | worg (Meshy pose: crouched, feet 0.22 u up) |
| ursine / suine / feline | owlbear, brown_bear | black_bear (oracle: walk stance float 0.058 vs 0.027), boar (oracle: walk skate 1.63 vs 1.61 limit), panther (Meshy pose: paw 0.11 u up) |
| reptile | crocodile | giant_lizard (within 0.979: sprawled shoulders) |
| humanoid | tribal_warrior | orc, hobgoblin, bugbear (oracle: wide stance on narrow mocap — idle foot dip, walk skate 1.9–3.5×), gnoll (within 0.944: digitigrade legs), lizardfolk (within 0.896: tail, humanoid has none) |
| arachnid | — | giant_spider, giant_wolf_spider (within 0.93 / 0.95: abdomen bulkier than the forge spider) |
| serpent | — | constrictor_snake, giant_poisonous_snake (Meshy pose: coiled, head raised) |
| avian | — | giant_eagle (no wings on the raptor body; spec completion has no wing part) |

Per-import gates, as the contract names them:
- **Voxel aesthetic + lint** — `tools/characters/gates.py`, run by `character_add` before a rig is
  written: one-pitch voxel cubes finer than a subcube; no lint ERROR the source clips lack.
- **Bone-name independence** — imports are gameplay rigs in `rig_scope.json`; `BodyPlanScopeTest`
  passes on all of them.
- **Oracle on idle/walk** — `tests/stress/ImportedRigOracleTest.cpp` (MotionOracle, plan feet,
  each metric / body height vs the SOURCE skeleton's own clip). Verdicts go to
  `build/coverage/import_oracle.json`; `character_add.py --refuse-oracle-failures` refuses a
  written import with the oracle's reason and restores the monster's pre-import binding from
  git history. Humanoid bands are calibrated on the 11 shipped humanoid variants (they deviate
  ~0, so the fixed bands stand); creature skate uses a CHOSEN 10 % relative band (forge walks
  already skate ~1 height/s by this metric) — not calibrated.
- **Live spawn at a stated pose** — Release, CharacterTestbed: all 8 imports twice (16 + 2),
  drawn 16/16, dropped 0, 37,220 parts; posed at ~5, 20 and 40 u; duplicates agree; lowest bone
  box never below ground. The tabled "unposed beyond 5–7 u" defect's order test (two owlbears at
  equal 18.8 u, spawned left→right then right→left): identical frames. Not reproduced.
- **Bind-rotation defect** — per gate decision 6: visual check, never reproduced (above).

Binder fixes the batch forced (each red-before-green, `tests/test_character_pipeline.py` 36):
species rig compiled from the CURRENT spec (giant_lizard bound to a stale rig); refusal reasons
name the failing bar; ground-contact leg fit (connected near-ground clusters matched to legs —
the front/back half split left mid-stride legs with 0 voxels); left/right from bone POSITION
(forge spiders put `_L` at −X); tails never feet; bipeds centred on the hips (arms pulled the
bbox); biped spine follows a hunched torso; the spine fit leaves legs to the leg fit; legs keep
their shape (uniform scale); clip Speed from MEASURED stance speed (anim_lint's estimate where it
finds feet); a part spec completion cannot add is reported (spider jaw); refusals restore the
pre-import binding; manifests record their Meshy task ids. Tried and REVERTED: per-joint creature
girth — it let a quadruped bound to the serpent skeleton pass at 0.998.

Engine-side and test-side findings: oracle `maxKneeInversion` is a distance (normalised by
height now); MotionOracle's 3 cm planting window is absolute (scaled per rig in the test).

What R2 leaves for later (each named by the refusal that found it):
- **R3 (clips per body plan):** wide-stance humanoids need adapted clips (orc, hobgoblin,
  bugbear); the dire_wolf species has no `run` (dire-wolf traded run for a working death).
- **Body parts spec completion lacks:** wings (eagle), a tail on the humanoid (lizardfolk),
  digitigrade humanoid legs (gnoll), a head chain on the spider (bite jaw).
- **Meshy pose control:** coiled snakes, crouched worg, mid-stride panther despite the prompt —
  a pose check on the model BEFORE binding (re-prompt) would save the credits.
- **Bulkier-than-species bodies** (spiders): the envelope bar cannot tell bulk from a wrong
  skeleton; needs a better measure, not a looser bar.
- `add_character` MCP tool: the CLI it wraps is exercised; the tool itself was not called through
  a restarted MCP server.

### R3 — Clip libraries per body plan (3–4 weeks, parallel with R2 after its binder lands)
Contract: every body plan reaches its vocabulary. Sources in order: retarget existing mocap
(the 159-clip humanoid set onto every biped plan; the quadruped library onto every quadruped
plan) → **MotionBricks capture** for humanoid locomotion, starts, stops and turns (offline; the
provider drives a captured character, output retargeted from G1 and written as clips — its own
`/design-check`) → UniMate drafts for the gaps (`UniMateIntegrationPlan.md` M1b–M3; M3 creatures is in scope
now) → owner review in the panel. Every clip carries `clip_meta` state + factors. Validation L2
(oracle + lint) and L4 (panel review). Gate per body plan. Coverage report is the progress bar.

### R4 — Variation runtime (1–2 weeks) — gate: `/design-check`
Contract: a character picks among reviewed variants of a state deterministically from a seed
(its entity id), modulated by factors (mood, condition, `style`), and attack/idle variants
cycle without immediate repeats. Adds the `style` factor so a monk, a dwarf or a goblin moves
differently on the same clip family. Every race and class gets a style eventually (the R1
checklist tracks it); the first slice is the three that already have a distinct gait clip —
dwarf (heavy stride), halfling (`scamper_walk`), ogre (`ogre_walk`) — then the remaining races,
then classes. Validation L3 (headless: N characters, distribution and
no-repeat invariants) + L4. Variants come from R3.

### R5 — Runtime completeness (3–4 weeks, split into separate gates)
The runtime pieces the vocabulary needs that do not exist yet:
- movement modes: fly (take-off, hover, glide, land), swim, climb walls, burrow;
- A7 reactivity (hit reactions by direction and weight, impulse nudge);
- the seat approach walk deferred from A4;
- multi-part attacks (breath weapons, tail sweeps, multiattack chains) driven from the D&D action.
Each is its own `/design-check`; order set by the coverage report's largest gaps.

### R6 — Developer path finished (1–2 weeks) — gate: `/design-check`
Contract G4. Most of it is built inside R1–R4 (manifest, command, MCP tool, reports). R6 adds:
game-project character overrides layered over engine `resources/` (verify today's behaviour
first); `.anim` hot reload (the parse cache never invalidates today); one rewritten developer
guide that replaces `AnimatedCharacter.md` and `CharacterAnimationGuide.md`; a template project
that adds a custom creature end to end as its acceptance test.

## 6. Rules carried forward
- Verification discipline from the ledger §7: L1–L4 + wired, red before green, every claim cites
  the command and raw result, the owner owns every visual verdict.
- **A readback reports what was achieved, never what was requested** (A5 lesson, 2026-10-01).
- A visual claim needs a live off/on pair with bone positions, not a per-frame request trace.
- Nothing keys on bone names; everything resolves through the body plan.
- Detail is sub-voxel and unconditional; chunks never change appearance.
- No learned runtime becomes a required dependency.

## 7. Owner decisions (answered 2026-10-01)
1. **The four outcome definitions (§1)** are the finish lines the plan measures against; kept as
   written unless the owner changes one.
2. **MotionBricks:** keep the code; use it offline for generation/capture, never as a runtime source.
3. **A5:** closed at the measured float.
4. **Styles:** all races and classes eventually; order chosen by the plan (R4); the R1 checklist
   covers every creature, race and class.
5. **Meshy credits:** watched by `tools/meshy_credits.py`; warn the owner below 10 %.

**Next:** `/design-check` on R1.

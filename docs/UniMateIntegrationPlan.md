# UniMate Integration Plan — offline text-to-motion clip authoring

> **Status:** APPROVED 2026-09-29 for M0–M2 (re-gate verdict READY, scoped; M3 creatures re-gates
> separately). **M0 DONE, M1 in progress** (18 clips generated + evaluated, 4 viewed in-engine).
> **Next: M1b manual review loop — awaiting its own `/design-check`** (§5 M1b). Design-check items
> folded into the phase gates below (§6). No engine (`phyxel_core`) code is touched by any phase
> of this plan; M1b adds editor-panel UI only.
> **Scope:** an *offline* authoring loop that generates `.anim` clips from text prompts. It is a new
> **Source** for the existing clip/FSM pipeline (`CharacterAnimationV2.md` §7 "Path B"), not a
> runtime motion source (contrast `MotionBricksIntegrationPlan.md`).
>
> Upstream: [`Friedrich-M/UniMate`](https://github.com/Friedrich-M/UniMate) — "UniMate: One
> Unified Model to Animate Diverse Skeletons", SIGGRAPH Asia 2026
> ([arXiv 2609.05415](https://arxiv.org/abs/2609.05415)). Code MIT; preview weights at
> `huggingface.co/Linzhan/UniMate`. Full evaluation + verdict: `EngineAdvancesResearch.md` §10.

## 1. Outcome

Phyxel gains a repeatable way to draft new animation clips by typing a prompt, for the humanoid
first and, if that pays off, for creature rigs. Today every clip is a downloaded Mixamo / Quaternius
pack (limited, and the packs never cover bespoke gameplay actions); UniMate is the first
text-to-motion model that conditions on **an arbitrary skeleton** (5–70 joints, bipeds, quadrupeds,
avian, serpentine), which is the exact gap `CharacterAnimationV2.md` §7 recorded as "neither Path A
nor B covers creatures".

Success means: generated clips pass the same deterministic lint the mocap library passes, carry
their provenance, and read well in-engine as **drafts** — good enough to ship after review, or to
be edited, not guaranteed-good.

## 2. Decisions (locked)

1. **Offline only, forever.** PyTorch/CUDA never enters `phyxel_core` or a packaged game. Output is
   `.anim` clip data in `resources/`. Same reasoning that rejected MotionBricks as a runtime.
2. **External repo, pinned commit, referenced by path** (`UNIMATE_ROOT` env var, default
   `../UniMate`). Never vendored into `external/` — it is a 5 GB Python/CUDA stack.
3. **Python entry points, not the bash wrappers.** Every upstream wrapper hardcodes `blender -b`;
   the entry points themselves parse `sys.argv` (`common.py::parse_blender_argv`).
   **Amended 2026-09-29:** the "pip `bpy`, no Blender install" half did not survive contact —
   PyPI only has bpy ≥ 4.2 (Python 3.11+), and `download.blender.org` (both `/pypi/` and
   `/release/`) answers 403 from this network for curl, uv and WebFetch alike. Stages 1 & 5 (the
   only bpy users we need; they import numpy/loguru/tqdm/Motion + bpy, no torch) therefore run in a
   **portable Blender 4.0.2** (bundled Python 3.10 = the venv's) fetched from a mirror
   (`mirrors.ocf.berkeley.edu`), invoked `blender -b --python-use-system-env -P <entry> -- <args>`
   with the venv's `site-packages` on `PYTHONPATH`. The driver switches automatically
   (`unimate_generate.py::run(needs_bpy=True)`; `UNIMATE_BLENDER` overrides the path).
4. **Generated clips are namespaced.** Clip names MUST start with `unimate_`. The importer refuses
   any other name without `--force`, so a draft can never silently replace shipped mocap.
5. **Provenance is mandatory.** Every imported clip gets a `# clip_meta:` line:
   `source=unimate ckpt=<step> prompt="<text>" seed=<n> cfg=<x> rep=<k>`. The engine already
   parses `clip_meta` headers (`AnimatedVoxelCharacter.cpp:512`) and — verified 2026-09-29 —
   skips unknown keys and failed float parses per key. It splits values on WHITESPACE, so the
   prompt is stored in a separate `# unimate_prompt: <clip> <text>` header line, never inside
   `clip_meta`.
6. **Feed the rig's ORIGINAL source asset, not a canonical rebuild.** Stage 5
   (`animate_motion.py --char_path <original>`) writes the motion back onto the asset's own
   armature and bind pose, so the clip arrives on the rig's real bone names and the existing
   bone-name remap import (`batch_import_mixamo.py` shape) applies unchanged.

## 3. Why this fits (verified facts, 2026-09-29)

- **Custom-rig path exists and is documented**, three entry points in `data_process/README.md`.
  `preprocess_char.py` takes a rigged, *animated* GLB/FBX → `cond.npy` (skeleton conditioning) +
  `<name>_canonical.glb` + `motions/<name>-<clip>.npz`. Inference (`unimate.inference.sample`) only
  realizes object types that have ≥1 reference clip on disk (`_known_object_types`), hence
  "animated" is required, not optional.
- **Our humanoid source is already the right input.** `resources/mixamo_imports/Head Hit.fbx`:
  2 skinned meshes, 65 joints (`mixamorig:*`), 1 clip — probed via FBX2glTF + pygltflib. The other
  Mixamo files are skeleton-only (no mesh) and would fail the canonical bake's assert.
- **Importer round trip proven on a real Mixamo GLB:** 65/65 bone names matched onto
  `humanoid.anim`, 52 driven channels (13 finger bones are un-keyed in Mixamo — normal).
- **Weights:** `unimate_uniml3d_f60_v2` — 74.1 M params, 60 frames @ 30 fps, ≤70 joints, trained
  100 k steps; `checkpoint_step_100000.pt` = 1.19 GB; ships `dataset_stats.npy` with **separate**
  per-dataset normalization stats (truebones / mixamo / objaverse), which is §6 item 4.
- **Machine:** RTX 4090 24 GB; Python 3.10 via `uv`; torch 2.5.1+cu124 pins.

## 4. Design-key answers (from `/design-check`, 2026-09-29)

| Key | Answer |
|---|---|
| Voxel aesthetic | Produces bone rotations only; appearance stays the existing voxel box rig. No detail assets. Data in `resources/`, unconditional (no flag/tier). Mocap-realism-vs-stylized tension is a per-clip acceptance call, not a design violation. |
| Chunk independence | Zero chunk-derived quantities; character space only. The equality analogue is import determinism: same GLB + flags → `semantically_equal` clip (new test), on top of the pinned byte-identical round trip (`tests/test_humanoid_anim_integrity.py`). |
| Procedural pipeline | Source layer of the *character animation* pipeline, upstream of FSM + quadruped retargeter. Not world-gen; no world-recipe persistence. Persistence that matters = provenance `clip_meta` (Decision 5). Only downstream hazard = overwriting a shipped clip (Decision 4). |
| API | CLI only through M2. Fields/units in §5 M1. "Unchanged" = every other clip + meta line byte-identical. Clamps: name prefix refusal; `--min-match` 0.9 name-match gate (refuses to write); `--auto-speed` leaves Speed unset under 0.05 u/s. Defaults: none change; no pinned test moves (`test_anim_locomotion_speeds.py` only names the shipped locomotion set). Echo: JSON report next to the clip. |
| Visual test | "Works" = lint absolute + calibrated envelope pass, foot-slide under threshold (locomotion), loop-gap pass (`--looping`), plays in-engine. Depth L2 (lint on real `.anim`) + L4 (playback review). Red test = the new foot-slide metric on mocap `walk` with Speed doubled; control = unmodified `walk`. Rig = CharacterTestbed, one humanoid, one clip via `play_entity_animation`, `orbit_screenshots` fixed poses. Rig delta: forced playback bypasses FSM blending and ignores Speed → blend quality untested until wired into a state; slide judged offline only. |

## 5. Phases and gates

Each gate is red-before-green and auditor-confirmed. Nothing proceeds on "it ran".

### M0 — environment + one raw sample (½ day)
- Finish the `uv` 3.10 venv at `UNIMATE_ROOT/.venv`: torch cu124 pins; drop the
  `download.blender.org/pypi` index line (returns 403 and aborts the whole `uv` resolve) and install
  `bpy==4.0.0` from PyPI separately.
- `preprocess_char.py --char_path phyxel/assets/phyxel_humanoid.fbx --face_r mixamorig:RightUpLeg
  --face_l mixamorig:LeftUpLeg` → cond + canonical + motions.
- **§6 item 4 settled here, prediction written first:** place the asset under the `objaverse`
  layout (documented custom path) vs. renamed into the `mixamo` single-topology layout; run the same
  prompt/seed under each. *Prediction:* mixamo stats give lower peak angular velocity outliers on
  the humanoid than objaverse stats (they were fit on Mixamo humans). Keep whichever the lint
  report favors; record both numbers.
- `unimate.inference.sample --only_save_motion` on "a person walks forward" → `.npy` (T,J,12).
- `animate_motion.py --dataset_type objaverse --char_path <original FBX> --cond_path …` → GLB.
- **Gate:** animated GLB exists AND `unimate_import.py --dry-run` reports ≥90 % name match.
- **§6 item 3 here:** read `AnimatedVoxelCharacter.cpp` clip_meta parser; confirm unknown keys are
  ignored (or add a one-line skip). Red: a clip with a bogus key must still load.

### M1 — humanoid quality bar (1–2 days)
- **Build the red test first (§6 item 1):** humanoid foot-slide metric in `anim_lint.py` —
  per-foot stance-phase XZ velocity (foot below a height threshold) in units/s, against root
  Speed. Red = mocap `walk` with Speed ×2 must flag; control = unmodified `walk` passes.
- **Finish the importer (§6 item 2):** `unimate_` prefix refusal, provenance `clip_meta`, JSON
  report, determinism test. Draft exists: `tools/anim_pipeline/unimate_import.py` (has: GLB→temp
  `.anim` via `extract_animation.py --style box`, name remap, `--min-match` gate, `--auto-speed`).
- Fixed prompt set, 3 repetitions each, seeds recorded: walk, run, jump in place, sword slash
  horizontal, wave right hand, sit down. Import each as `unimate_<x>_r<k>`.
- **Gate per clip:** lint absolute pass; calibrated envelope pass (`calibration.json`); foot-slide
  under the mocap-`walk` threshold for walk/run; then L4 playback screenshots for the user.
- **Deliverable:** per-clip verdict table (incl. rejections) appended to `EngineAdvancesResearch.md`
  §10. **Decision point:** < 50 % usable as drafts → stop, record why.

### M1b — manual review loop (added 2026-09-29; user owns every verdict) — ✅ gate READY 2026-09-29, IN PROGRESS

> **Gate amendments (folded):** (1) no Loop control — Preview playback already loops (observed
> 2026-09-29: walk cycled across screenshots 40 s apart); (2) the ledger lives at the FIXED path
> `resources/animated_characters/unimate_review.json` with a `rig` field per entry, and the panel
> shows the prompt FROM THE LEDGER (the engine keeps no header comments and exposes no
> `.anim`-path accessor) — so zero `phyxel_core` changes hold.

**Why.** The user wants to manually review every generated clip. Today a verdict needs an MCP
session driven by an agent (`play_entity_animation` + screenshots). The repo's established
pattern for imported packs is *review name → in-engine look → `promote_*` script renames the
approved clips* (e.g. `promote_longbow_pack.py`: `review_bow_idle` → `bow_idle`). Generated clips
already sit in that pattern (`unimate_*` namespace, Decision 4); what is missing is the review
affordance and a data-driven promote.

**Contract ("works" means):** with the engine on CharacterTestbed and a humanoid selected, the
user can step through every `unimate_*` clip, see its prompt + eval numbers, and record
accept / reject / note without leaving the editor; the verdicts persist in a ledger; a promote
step applies exactly those verdicts to `humanoid.anim` and refuses to touch unreviewed clips.

**Components**
1. **Clip review UI in the Properties panel** — `editor/src/PropertiesPanel.cpp::
   renderAnimatedCharInspector`, the existing `Clips (N)` tree (today `BulletText` only). Make each
   row `Selectable` → `ch->playAnimation(name)` (the same call the `play_entity_animation` API
   uses; enters Preview); add **Prev / Next / Loop** controls (Loop = re-issue `playAnimation` when
   `getAnimationProgress()` wraps) and a **name filter** defaulting to `unimate_`. Beside the
   selected clip show the `# unimate_prompt:` line and the ledger's eval numbers when present.
   Editor-only code; no engine behaviour, FSM or asset change. Plain ImGui, no new dependency.
2. **Review ledger** — `resources/animated_characters/unimate_review.json`, keyed by clip name:
   `{verdict: accept|reject|pending, notes, reviewed_at, prompt, seed, cfg, ckpt, rep,
   eval: {root_speed, feet_speed, root_vs_feet, residual, lint_errors, lint_warns}}`.
   Written by two paths: `unimate_import.py` creates the `pending` entry with provenance + eval
   at import time; the panel's **Accept / Reject / Note** buttons update `verdict/notes/
   reviewed_at` (editor writes JSON through the existing scene-side JSON utilities; the file is
   small and rewritten whole). The ledger is the single source of truth for what a generated
   clip may become. "Unchanged" = every other entry byte-identical after a write.
3. **`tools/anim_pipeline/unimate_promote.py`** — reads the ledger: `accept` → rename to a
   target FSM name given in the ledger (`promote_to`) or keep as a variant; `reject` → remove
   the clip and its two header lines from `humanoid.anim`; `pending` → refuse the whole run
   (list them). Refuses a `promote_to` that would replace a shipped clip unless the ledger entry
   carries `replace_shipped: true`. Echoes a summary (promoted/removed/blocked) and exits non-zero
   when blocked. Same shape as the existing `promote_*` scripts, data-driven.
4. **Stage the full set for the first session:** import the remaining 14 objaverse clips as
   `pending`; equip a sword on the probe (`equip_item`) so the slash clips can be judged.

**Design-key answers (for the gate).** Voxel aesthetic: UI only, no assets. Chunk independence:
no world data touched. Procedural pipeline: none; the ledger is character-asset metadata, not
world state. API: editor panel + one JSON file + one CLI; units: seconds / units·s⁻¹ as in the
eval table; defaults: filter text `unimate_`, no engine default changes, no pinned test moves.

**Validation layers.** L1 ledger file exists and round-trips (`json` load = dump).
L2 (deterministic, red first): (a) `unimate_promote.py` on a ledger with one `pending` entry
must refuse and change nothing (byte-identical `.anim`); (b) `reject` removes exactly that clip
+ its `clip_meta` + `unimate_prompt` lines and nothing else (`semantically_equal` on all other
clips); (c) `accept` with `promote_to` colliding with a shipped name refuses without
`replace_shipped`. L4: the user steps through all 18 clips in the panel and the ledger holds 18
non-`pending` verdicts at the end of the session — that *is* the M1 verdict table.
**Test world:** CharacterTestbed, one spawned humanoid, panel open; one variable = the selected
clip. **Rig delta:** Preview playback ignores Speed and FSM blending (as before) — the panel
shows the eval numbers precisely so slide is judged from the metric, not the eye.

**Red tests:** `tests/test_unimate_promote.py` (a/b/c above, run against a temp copy of
`humanoid.anim`); the editor panel has no unit test — its L4 check is the completed ledger.

**M1b progress (2026-09-29):**
- `tools/anim_pipeline/unimate_ledger.py` (schema v1, load/save with sorted keys, pending entry
  from an import report, verdict setter); `unimate_import.py` now writes the `pending` entry on
  every non-dry-run import (`--ledger`); `tools/anim_pipeline/unimate_promote.py` (plan/apply,
  refusals: pending, unledgered `unimate_*` clip, shipped-name collision without
  `replace_shipped`). Red tests `tests/test_unimate_promote.py` (6) green; importer tests now
  isolate the ledger path (a first run had written a probe entry into the REAL ledger — removed).
- All 18 objaverse clips re-imported → `humanoid.anim` has 18 `unimate_*` clips, ledger has 18
  `pending` entries; `unimate_promote.py --dry-run` REFUSES listing all 18 (L2 red on real data).
- Editor: `PropertiesPanel::renderClipReview` (filter, Prev/Next, selectable rows → `playAnimation`,
  ledger verdict/prompt/eval display, Notes, Accept/Reject/Pending/Save note → whole-file JSON
  rewrite). **Built (Release) and verified rendering in-engine 2026-09-29.** Two usability fixes
  found while verifying: the full animated inspector (and therefore the review section) only
  appeared for an entity SELECTED in the World Outliner — the crosshair "nearest entity" fallback
  showed ID/position/health only — so the fallback now renders the animated inspector too, for
  plain `animated` entities AND for NPCs (an `NPCEntity` wraps its character:
  `getAnimatedCharacter()`). Looking at a character is enough to review it.
- **How to run a review session (user):** engine on CharacterTestbed (Release); an NPC probe
  `npc_unimate_probe` at (4, 17, 4) with `iron_sword` equipped (only NPCs take `equip_item`);
  look at it → Properties → *Clip review* (filter `unimate_`), click a row or Prev/Next to play,
  read prompt + eval numbers, type a note, **Accept / Reject / Pending** writes
  `resources/animated_characters/unimate_review.json` immediately. Drag the floating *Item
  Equipper* window aside if it covers the section. Then
  `python tools/anim_pipeline/unimate_promote.py --dry-run` shows what would happen; without
  `--dry-run` it applies (refusing while anything is still pending). Set `promote_to` in the
  ledger JSON for an accepted clip that should take an FSM name.
- Left open by the agent-side verification: ImGui cannot be clicked over the API, so the
  Accept/Reject write path is exercised by the user's first session (the Python side of the same
  file format is covered by `test_unimate_promote.py::test_ledger_round_trip_is_stable`).
- **User's first look (2026-09-29) found two defects — both traced to code, not guessed:**
  1. *Clicking a row did nothing (character stayed in idle).* The panel called only
     `playAnimation`; the `play_animation` API command calls `setAnimationState(Preview)` FIRST
     (`Application.cpp`), otherwise the FSM (an NPC's Idle) maps its state back to `idle`. Panel
     now issues the same two calls.
  2. *"That doesn't look like the iron sword"* — correct: the NPC `equip_item` handler never used
     the item's template; it attached a hard-coded grey box (0.15×0.4×0.15) to `right_hand`, a
     pre-fine-voxel placeholder. Fixed in the engine-side editor code: `updateNpcHeldItems()` now
     derives the held item from the NPC's equipped MainHand (CombatBehavior weapon keeps
     precedence) and builds the real fine-voxel template through the same grip path as the
     player; the placeholder attach and the `detachAll()` on unequip are removed. **Both fixes
     rebuilt and verified live 2026-09-29** (`screenshots/screenshot_20260929_152547_215.png`):
     `npc_unimate_probe` + `equip_item iron_sword` shows the fine-voxel longsword in the right
     hand; `play_entity_animation unimate_walk_r1` on the NPC → `get_animation_state` =
     `state: Preview, clip: unimate_walk_r1` and the panel shows *State: Preview* — the panel's
     row click now issues exactly those two calls. Panel L4 (Accept/Reject writes) remains the
     user's session.

### M2 — longer + loopable clips
- 60 frames = 2 s. Test `--motion_expand` (prompt chaining, `--expand_overlap`) and `--inbetween`
  with `--keep_frames 0,-1` for loop closure.
- **Gate:** a `unimate_walk` cycle passes `anim_lint --looping` and plays through the FSM `walk`
  state (temporarily rebound) with no pop at the seam; `test_anim_locomotion_speeds.py` still green.

### M3 — creatures (the real prize; **re-gate separately** — §6 item 6)
- Needs a `.anim`→GLB skeleton exporter for forge rigs, which does not exist. Start instead with an
  imported rig whose source GLB we still have (Meshy wolf/elk), then decide whether the exporter is
  worth building.
- Reuse the quadruped gates: `retarget_quadruped.py::gate` (death ≤60 % bind height, lowest bone
  near ground) and the four transfer rules in that file's header.

### M4 — ergonomics (only if M1–M3 pay off)
- MCP `generate_clip` tool wrapping the offline loop and returning the lint report.
- Re-check `MotionBricksIntegrationPlan.md`: UniMate conditions on our skeleton directly, so the
  G1→humanoid retarget stage that plan needs may no longer be the cheapest path to varied
  locomotion. User decision, not this plan's.

## 5b. Progress log

- **2026-09-29 — M1 red test built, red-then-green.** `anim_lint.py` gained
  `foot_slide_metrics` / `foot_slide_findings` + a `slide` subcommand. First formulation
  (assume +Z travel, 6 cm stance window) FAILED its own control (walk 70 % "slide", strafes
  >130 %): the direction cannot be assumed (strafes travel X) and 6 cm admits swing samples.
  Final formulation measures body velocity FROM the stance feet (3 cm window) and recovers every
  shipped locomotion Speed within 1–14 % (walk 1.667 vs 1.679, run 3.659/3.766, fast_run
  5.411/5.659, strafes ±3.37/3.62, walking_backward −1.03/1.20). Gate scoped to cyclic locomotion
  (`is_locomotion_clip`: meta type / name regex, transitions + combat/jump exempt). Red: walk with
  Speed ×2 → ERROR "foot skates". Tests: `tests/test_anim_foot_slide.py` (6). Pre-existing
  unrelated lint FAIL on `death_front` unchanged (identical on HEAD).
- **2026-09-29 — importer hardened.** `unimate_import.py`: `unimate_` prefix refusal (`--force`),
  provenance `clip_meta source=unimate ckpt seed cfg rep` + `# unimate_prompt:` header line,
  root X/Z stripping with Speed from the stance feet (fallback root travel), lint findings +
  JSON report. Tests: `tests/test_unimate_import.py` (3: determinism via `semantically_equal`,
  refusal leaves the file byte-identical, provenance survives round trip). Fixture = Mixamo
  `Head Hit.fbx` → FBX2glTF → GLB: 65/65 names.
- **2026-09-29 — driver written.** `tools/anim_pipeline/unimate_generate.py` (Decisions 2+3 in
  code): `preprocess` / `sample` / `animate` subcommands shell to UniMate's Python entry points in
  its venv with cwd + `PYTHONPATH` = `UNIMATE_ROOT`, never the bash wrappers. Verified the
  installed tyro 1.0.12 accepts both `--exp_dir` and `--exp-dir`.
- **Ordering note:** M1's tooling (slide metric, importer, driver) was built while the M0 venv
  install ran, because none of it depends on UniMate. The M0 *gates* (preprocess succeeds,
  stats/layout experiment, first GLB round trip) are still open and still come before any M1
  clip verdict.
- **2026-09-29 — M0 environment DONE.** venv: torch 2.5.1+cu124 sees the RTX 4090; Motion,
  CLIP, torch-geometric, transformers import. `bpy==4.0.0` unobtainable (Decision 3 amendment) →
  portable Blender 4.0.2 from a mirror, bundled Python 3.10.13, imports the venv packages via
  `--python-use-system-env` + `PYTHONPATH` (numpy 2.2.6 works under it). `flan-t5-base` cached.
  Driver footgun fixed: UniMate's UTF-8 progress glyphs killed the driver on a cp1252 console —
  `sys.stdout.reconfigure(errors="replace")`.
- **2026-09-29 — M0 preprocess DONE.** `phyxel_humanoid.fbx` (= Mixamo `Head Hit.fbx`) →
  `phyxel/features/`: cond.npy (object type `phyxel_humanoid`, **54 joints** — UniMate's
  motion-driven pruning merged 11 never-animated leaves: 10 finger tips + `RightToe_End`), one
  44-frame @ 30 fps reference clip, canonical GLB; scale factor 0.0081 (Mixamo cm → canonical
  diameter 2). Face pair `mixamorig:RightUpLeg`/`LeftUpLeg`.
- **2026-09-29 — M0 first round trip DONE (objaverse layout, seed 10, cfg 3.0).** 6 prompts × 3
  reps sampled (`(60, 54, 12)` per clip; ~30 s per sample on the 4090 — slow, 100 flow steps).
  `walk-rep_0` → stage 5 on the ORIGINAL FBX → GLB (54 joints, 60 keys, 1.967 s) →
  `unimate_import.py --dry-run`: **54/54 bone names matched, 0 lint errors.** First quality
  numbers: root travel 2.12 u in 1.97 s (`extract_animation` strips it into Speed 1.075) while the
  stance feet imply **0.77 u/s** (29 % mismatch, residual 0.45 u/s, stance L 37 % / R 78 %) — one
  slow stride in 2 s with some skate. Importer bug found+fixed: it discarded the extractor's Speed
  line (extract_animation already strips root X/Z); Speed now comes from the stance feet with the
  root-vs-feet mismatch kept in the report.
- **2026-09-29 — objaverse-layout batch evaluated** (`unimate_eval.py`, dry-run imports, all
  18 clips **54/54 names, 0 lint errors**; root u/s = extractor root travel, feet u/s = stance
  estimate, off = |root−feet|/root, residual = stance jitter u/s; mocap reference: walk residual
  0.172, run 0.197, off 1–14 %):

  | clip | root u/s | feet u/s | off | residual | note |
  |---|---|---|---|---|---|
  | walk r0 | 1.075 | 0.769 | 29 % | 0.452 | one slow stride, skates (WARN) |
  | walk r1 | 1.248 | 1.273 | 2 % | 0.159 | **mocap-grade** |
  | walk r2 | 1.023 | 0.958 | 6 % | 0.165 | good |
  | run r0/r1/r2 | 3.13/3.25/3.45 | 2.91/3.31/3.81 | 7/2/11 % | 0.47/0.45/0.51 | all in the mocap band (mocap run 3.66/3.77, 0.197) |
  | jump ×3 | 0.03–0.11 | — | — | 0.34–0.36 | in place as prompted |
  | sit ×3 | 0.02–0.14 | — | — | 0.04–0.25 | in place |
  | swordslash ×3 | 0.16–0.30 | — | — | 1.5 | feet pivot during swing; root drift ≈ mocap sword Speed 0.256 |
  | wave ×3 | 0.04–0.08 | — | — | 0.12–0.29 | in place |

  Importer rule added: stance-feet Speed only for locomotion names; actions keep the extractor's
  root-travel Speed exactly like the shipped mocap imports. Visual (L4) verdict still pending.
- **2026-09-29 — M0 stats/layout experiment DONE — prediction FALSIFIED.** Same 6 prompts ×
  3 reps, seed 10, cfg 3.0, under `objaverse` vs `mixamo` layout (= which per-dataset
  normalization stats the custom skeleton borrows). Calibrated lint (envelope ×1.5 over the
  vetted mocap set) + slide gate on all 18 clips each:

  | layout | envelope WARNs | loco root/feet off (mean / max) | loco residual | action residual | action root drift |
  |---|---|---|---|---|---|
  | objaverse | **196** | **9 % / 29 %** | 0.368 | 0.572 | 0.106 u/s |
  | mixamo | 260 | 14 % / 27 % | 0.342 | 0.468 | 0.080 u/s |

  Predicted: mixamo stats → fewer outliers on the humanoid. Measured: MORE envelope warnings and
  worse locomotion skate; only stance residuals marginally better. **Decision: objaverse layout
  (the documented custom path) is the default.** Both runs: 54/54 names, 0 lint ERRORs.
  Side note: the mixamo run took ~4 min vs ~15 for objaverse (GPU was shared with Blender
  during the first run — not a stats effect; re-time before believing it).
- **Envelope WARNs are dominated by jumps (25–39 per clip)** — the calibration set
  (idle/walk/run/fast_run/attack/boxing/point/wave) contains no jump, so exceeding it may be
  legitimate. Judge per bone in M1 before treating envelope WARNs as defects.
- **M0 gates: ALL CLOSED.** M1 in progress: `unimate_walk_r1`, `unimate_run_r1`,
  `unimate_jump_r1`, `unimate_swordslash_r0` imported into `humanoid.anim` (uncommitted) for
  the L4 look.
- **2026-09-29 — L4 look (Release, CharacterTestbed, spawned `animated` humanoid,
  `play_entity_animation`, side camera at (9.5, 18.3, 4) yaw 180 pitch −8; screenshots
  `screenshots/screenshot_20260929_1432*.png`).** All four clips load with their Speed lines
  (`list_entity_animations`: walk 1.273, run 3.305, jump 0.085, slash 0.295).
  | clip | L4 read | verdict |
  |---|---|---|
  | `unimate_walk_r1` | upright, clear stride with knee lift + heel plant, counter-swinging arms over 3 phases | **usable draft** — candidate to A/B against mocap `walk` in the FSM (M2) |
  | `unimate_run_r1` | forward lean, high trailing kick, arm drive | **usable draft** |
  | `unimate_jump_r1` | crouched wind-up (arms back) → airborne tuck | plausible draft; Y travel present — check `RootMotion` y flag vs the engine's jump warp before binding |
  | `unimate_swordslash_r0` | torso folded forward in one frame, lunge stance with arms forward in another; no weapon on the probe so the swing is ambiguous | **weak / unjudgeable as-is** — re-view with a sword equipped and more reps; stance residual 1.5 u/s says the feet shuffle a lot |
  Rig delta reminder: Preview mode ignores Speed and FSM blending — slide judged offline only.
- **M1 decision point (2026-09-29): PROCEED.** By the deterministic gates all 18/18 clips pass
  (names, 0 lint errors, locomotion skate in the mocap band for 5 of 6 walk/run reps); by eye 3 of
  the 4 viewed are usable drafts. Remaining M1 work before M2: view the other 14 (`unimate_eval`
  table already has their numbers), triage the envelope WARNs per bone (jump-heavy — calibration
  set has no jump), and read the weights licence (§6 item 5) before any clip ships.

## 6. Design-check verdict — NEEDS WORK (2026-09-29), items folded above

1. Humanoid foot-slide metric does not exist → **M1 first task**, shown red first.
2. Importer lacks overwrite refusal + provenance meta + JSON report → **M1**.
3. Engine `clip_meta` unknown-key behaviour unverified → **M0**.
4. Normalization stats / layout for a custom skeleton open → **M0**, prediction stated.
5. **Weights licence** unread (trained on Mixamo, Objaverse-XL, Truebones-commercial) → read before
   any generated clip ships in a packaged game; note in §10 of the research doc.
6. Creature exporter absent → **M3 is its own feature**, re-run `/design-check` when M1 has a verdict.

## 7. Risks, plainly

- Quality unknown; v2 weights are two days old (2026-09-27). Treat as a drafting tool.
- The custom-asset inference path is documented but has not been run here yet (M0 is the proof).
- ~5 GB of downloads (torch, flan-t5-base text encoder, checkpoint); all outside the repo.
- Windows: Git Bash suffices, but only because we call the Python entry points (Decision 3).

## 8. On-disk state (2026-09-29, nothing committed)

- `G:\Github\UniMate` — shallow clone @ `5d6aabe` (2026-09-27), `.venv` half-installed (install
  aborted on the blender index 403), `outputs/uniml3d_60frames_graph_adaln_v2/` holds
  `config.json`, `dataset_stats.npy`, `checkpoints/checkpoint_step_100000.pt` (1.19 GB),
  `phyxel/assets/phyxel_humanoid.fbx` (copy of `Head Hit.fbx`), `phyxel/test_cases_humanoid.json`.
- Phyxel repo, all untracked/uncommitted: `tools/anim_pipeline/unimate_import.py` (hardened, §6
  item 2 done), `tools/anim_pipeline/unimate_generate.py` (driver), `anim_lint.py` (modified:
  slide metric + `slide` subcommand), `tests/test_anim_foot_slide.py`,
  `tests/test_unimate_import.py`, this plan, `EngineAdvancesResearch.md` §10.

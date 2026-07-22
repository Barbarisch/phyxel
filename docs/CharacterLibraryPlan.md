# Character Library Plan — D&D Races to Full Bestiary

> Status: **Phase A in progress** (2026-07-21). Owner doc for the character-library
> workstream. Companion docs: [`CharacterAnimationV2.md`](CharacterAnimationV2.md)
> (rig/body-plan abstraction design that Phase D implements),
> [`AnimatedCharacter.md`](AnimatedCharacter.md) (runtime character),
> [`CharacterAnimationGuide.md`](CharacterAnimationGuide.md) (clip import).

## Goal

A robust character library covering D&D archetypes — human, halfling, dwarf, elf,
half-orc, tiefling, gnome, goliath, dragonborn, then goblin/ogre/giant and
eventually functional quadrupeds and dragons. Everything stays voxel-native:
external content (AI model/animation services) is voxelized into `.anim`; there is
no mesh render path.

**Strategy:** one master rig per body plan; races are data-driven appearance
presets + palettes on shared rigs. New `.anim` files only for genuinely different
body plans or silhouettes scaling can't reach.

## Architecture (Phase A, shipped pieces)

```
resources/races/<id>.json          — stats + NEW "visual" block
  visual: { animFile?, appearancePreset?, appearanceOverrides?, palette{skinTones[]} }
resources/appearance_presets.json  — 11 proportion presets (single source of truth,
                                     loaded by BOTH C++ AppearancePresetRegistry and
                                     tools/interaction_pipeline/morphology_presets.py)

Core::CharacterVisualResolver      — THE single resolution path:
  def{race?, appearance?, animFile?, role?} + npc name
    → { animFile, CharacterAppearance, raceId }
  order: seeded colors → race preset (SET proportions) → race palette skin tone
         (deterministic by name) → race appearanceOverrides → explicit appearance
         (may name a "preset") → explicit animFile.
  Callers: GameDefinitionLoader (NPCs + race/appearance-aware players),
  Application spawn_npc handler. set_npc_appearance expands "preset" via the
  registry. Do NOT add a spawn path that bypasses the resolver.
```

**Race dimensions are grounded in D&D 5e** (PHB average heights, ratio vs the
5'8" standard rig). GOTCHA: per-limb scales COMPOUND with heightScale
(effective leg = heightScale × legLengthScale), so a preset's standing height
is NOT its heightScale — the original interaction-testing "dwarf" (0.60h)
composed to 0.49× and stood SHORTER than the halfling, inverted vs D&D.
Race presets were tuned live (spawn → measure bone-AABB standing height →
iterate; scratch tool pattern in the tuning notes of
`resources/appearance_presets.json`, measured composite stored per preset as
`_heightRatio`): halfling 0.54 (3'0") < gnome 0.62 (3'6") < dwarf 0.75 (4'4",
145% bulk) < human 1.0 < half-orc 1.09 (6'2") < dragonborn 1.16 (6'6") <
goliath 1.30 (7'6"). `AppearancePresetRegistryTest.DndRaceHeightOrdering`
pins the ladder. standard/giant/child remain interaction-testing presets.

**Load-path GOTCHA (fixed 2026-07-21):** `loadModel()` originally never called
`applySkeletonProportions()`/`resizeController()` (only `loadFromSkeleton` and
`rebuildWithAppearance` did) — spawn-time race appearance scaled the boxes but
not the skeleton/keys, rendering characters as scattered fragments. All three
load paths now run the same sequence; keep them identical.

Legacy semantics preserved: raceless NPC with explicit `appearance` gets
defaults+fields (not seeded colors); plain players keep default appearance.
Changed intentionally: `spawn_npc` without race/role now seeds colors (matches
GameDefinitionLoader; previously default colors); race/appearance players get a
full `rebuildWithAppearance` so proportions apply (recolor-only silently dropped
them before).

MCP: `spawn_npc` / `create_game_npc` / `define_character` accept `race` +
`appearance`; `set_npc_appearance` accepts `preset` + tail/wing/neck scales.

## Phases

- **A — Humanoid race library on the existing rig** (this phase): wiring above,
  9 races, L3 validation matrix, stress test. No new art.
- **B — Scale-band hardening** (part 1 SHIPPED 2026-07-21): capsule half-width
  now scales as the ratio of the proportioned skeleton's torso X-span vs the
  unscaled template (standard keeps exactly the legacy 0.25 — golden-neutral;
  forearm/hand bones excluded so the T-pose doesn't inflate to wingspan);
  step height scales with heightScale×legLengthScale, floored just above the
  1/3-voxel subcube riser (every race can climb generated stairs) and capped
  at 0.70 (giants don't glide over half-walls). Live values: goblin 0.156/0.34,
  standard 0.25/0.444, ogre 0.376/0.693. New presets **goblin 3'3"** and
  **ogre 9'0"** (composite 1.59 — the band-stretch extreme; verified coherent
  walking, minor waist box seams). Tests: `CharacterCapsuleScalingTest.cpp`
  (red-before-green). **Silhouette shaping (2026-07-22):** two new
  CharacterAppearance scalars close part of the "big human ≠ ogre" gap —
  `bellyScale` (extra depth + half-width on hips/spine/spine1 boxes only, so a
  gut reads as a gut) and `postureLeanDeg` (forward spine hunch distributed
  across the spine chain, applied after animation sampling at the single
  per-frame pose choke point, so it shows in every clip). Ogre preset now
  belly 1.45 / lean 12° / shoulders 1.40 — verified hunched + pot-bellied
  walking in profile. **Variant-rig pattern SHIPPED (2026-07-22):**
  `resources/animated_characters/ogre.anim`, derived by
  `tools/anim_pipeline/derive_ogre_rig.py` — same skeleton/bone names/clips as
  humanoid.anim (drop-in with the whole FSM/IK stack), resculpted MODEL boxes:
  massive hands (×1.45), thick forearms, jaw slab + two ivory tusks on the
  face. Supporting format extension: MODEL `Box` lines take an optional
  trailing `r g b` explicit color (tusks/claws/teeth override the bone's
  appearance color) — parser + builder + anim_format.py round-trip. Base-rig
  fix: humanoid.anim was voxelized from a FEMALE Mixamo model; `tools/
  anim_pipeline/widen_base_torso.py` (idempotent — restores the pristine
  baseline from git first) widens chest ×1.14/shoulders + hips ×1.10 +
  thighs/calves ×1.07 for a neutral-male build. **Character facing is now an
  executable convention** (repeated backward-face mistakes): model-space face
  = +Z — docs/CoordinateSystem.md §Character Facing (authoritative, from
  code), CLAUDE.md one-liner, `CharacterFacingTest.cpp` pins engine + asset,
  derive script asserts features at +Z. Never read facing from screenshots of
  patrolling NPCs. **Narrow-door differential matrix PASSED (2026-07-22,
  `tools/interaction_pipeline/door_matrix.py` — builds/tears down per its
  docstring; run one race per wave, simultaneous NPCs in one lane block each
  other and contaminate the result):** goblin passes 2/3/4-tall doors; ogre
  honestly BLOCKED at 2-tall, passes 3-tall (visual 2.66 < 3.0); nobody
  tunnels the solid wall. Finding: standard (capsule 2.12, visual ~1.97)
  passes a 2-tall door — the capsule's +0.3 headroom pad is forgiven by the
  controller, so door fit tracks VISUAL height; sensible contract, kept.
  **Bone-clipping metric SHIPPED (2026-07-22):** `anim_lint.py clipcheck`
  (impl `tools/anim_pipeline/clip_metric.py`) — replicates the engine's
  proportion pipeline in Python, poses the skeleton through a clip, measures
  pairwise foreign-bone AABB overlap. The DELTA over the standard baseline is
  the signal (absolute % is inflated by conservative posed AABBs near the
  thighs). Calibration: dwarf is the worst shipped preset at +52.4pt (145%
  bulk on short legs — visually acceptable on chunky voxel bodies);
  `tests/test_preset_clipping.py` gates every preset at ≤ +55pt so future
  edits cannot clip worse than today's dwarf.
  **SEAT-FIT ENFORCEMENT SHIPPED (2026-07-22) — Phase B part 2 complete.**
  USER PRINCIPLE: accuracy over coverage — a character NEVER sits where it
  doesn't fit. What changed:
  - Fit rules are HARD errors: SEAT_TOO_NARROW / SEAT_TOO_SHALLOW /
    SEAT_TOO_TALL (feet dangle > 0.20) / new SEAT_TOO_LOW (knees rise > 0.35)
    all refuse the sit. BACKREST_BLOCKS_VIEW stays a cosmetic warn. Rules
    live TWICE and must stay in sync: `runSitCompatChecks`
    (editor/src/Application.cpp) + `interaction_kinds/sit.py`.
  - DENY-ON-MISSING: a seat with no metrics sidecar cannot prove fit → sit
    and can_interact refuse it (was allow). Characterize with
    tools/characterize_asset.py. Bypasses: force_interact (raw HTTP,
    test-only, deliberately NOT on MCP) or requireCompatibility=false points.
  - Refusals return `nearest_fitting_seat`; new `find_fitting_seat` API/MCP
    query enumerates free seats the character passes the gate on (the
    building block for NPC seat selection). `can_interact` is now on MCP.
  - Seat inventory covers every preset (tools/gen_seat_variants.py):
    `stool_low` (0.33 — smallfolk) and `bench_great` (1.33 — sized from
    MEASURED legs: ogre 1.58, goliath 1.27); fixed chair_wood's malformed
    `# interaction_point:` header (missing "# " — it was never sit-enabled
    live!) and added headers to stool/bench_wood/bar_stool.
  - GATE: `tools/interaction_pipeline/seat_matrix.py` — 9 presets × 7 seats:
    coverage (every preset ≥1 fitting seat), expectation cells, and gate
    parity (real sit result == can_interact verdict). Verified matrix:
    smallfolk→stool/bench_wood, dwarf also chair_wood, standard/elf/half_orc→
    chair_wood/bar_stool, goliath+ogre→bench_great only, test_chair (0.11
    sliver seat) refuses everyone.
  - Hop/climb-onto-tall-seat transitions intentionally deferred to Phase C
    (needs a sourced clip; the conditional remap via animationMapping is the
    planned mechanism — never force existing clips onto wrong-size seats).
- **C — External service bake-off + `tools/character_import.py`**: Mixamo (free
  baseline; pipeline exists — it built humanoid.anim) vs Meshy vs Tripo for
  (i) new humanoid-variant models, (ii) race-flavored animation sets, (iii) a
  quadruped. Budget $20–50; verify commercial licenses before assets land in
  resources/. Durable output: generalized glTF/FBX→voxelize→.anim importer with
  per-service bone maps that fail loudly.
- **D — Body-plan abstraction** (implements CharacterAnimationV2.md §4 item 0):
  de-hardcode `mixamorig:*` from segment boxes / foot IK / clip map; BodyPlan
  descriptor with auto-derivation for existing rigs; golden-regression humanoid
  neutrality; brings wolf → spider → dragon (ground gait) to life.
- **E — Bestiary at volume**: `resources/monsters/` visual blocks, variant
  multiplication (one goblinoid rig × goblin/hobgoblin/bugbear presets),
  manifest-driven batch import with auto lint→load→traversal→screenshot gauntlet.

## Risks / decisions

1. **Voxelization aesthetic** (top risk): naive voxelized meshes look "melted" —
   Phase C tests resolutions + box-density vs humanoid.anim; fallback = services
   for animations only.
2. **Scale band**: presets stay inside the validated band until Phase B widens it;
   a unit test (`CharacterVisualResolverTest.AllShippedRacesResolveInsideValidatedBand`)
   fails if a race JSON drifts outside.
3. **Phase B collision changes** regress-test existing content; feature-flag.
4. Dragon flight out of scope through E.

## Validation ledger (Phase A)

| Check | Layer | Where |
|---|---|---|
| Preset registry loads 11, dwarf = 0.60/1.50 | L1 | `tests/scene/AppearancePresetRegistryTest.cpp` |
| race → preset/palette/overrides resolution, legacy semantics, band | L1 | `tests/core/CharacterVisualResolverTest.cpp` |
| Runtime: spawn race=dwarf_mountain → appearance 0.60 (red: was 1.0) | L4 | MCP/HTTP spawn + get_npc_appearance |
| Every race walks a patrol course, grounded | L3 | walk matrix (all 9 pass, 2026-07-21) |
| Sit at size extremes (halfling + goliath) | L3 smoke | sit_character; SEAT_TOO_TALL correctly fires for halfling |
| 30 mixed-race NPCs, one scene | stress | live engine (29 FPS Debug, none fell) |

**Phase A follow-ups:** (1) seat calibration profiles assume standard body depth —
the halfling clips slightly into backrests; per-preset sit recalibration via the
interaction pipeline (presetId already flows through). (2) Full stairs/doorway
TraversalProbe matrix rides with Phase B's step-height work, where it gates the
capsule changes anyway.

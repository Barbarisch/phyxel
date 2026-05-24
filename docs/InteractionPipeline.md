# Interaction Pipeline

End-to-end automated validation + tuning loop for interaction assets (chairs,
benches, doors, ladders, etc.) against the Phyxel engine.

The pipeline answers a deceptively simple question:

> Does the humanoid actually sit in this chair correctly, and if not, can we
> tune the per-asset offset profile to make it correct — or is the misbehavior
> caused by an engine bug that no profile change can fix?

## Architecture — four stages

A correct interaction is the joint product of four upstream artifacts. Skipping
any stage means the tuner is writing offsets on top of unverified inputs, which
is exactly how the saved profiles drifted into garbage in May 2026 (Y=0.667
offsets, identical sit-down/idle clips, doors carrying sit offsets). Each stage
has explicit inputs, outputs, and a validation gate. **No stage may run until
the previous stage has produced a validated record.**

```
  ┌────────────────────┐    ┌────────────────────┐    ┌────────────────────┐    ┌────────────────────┐
  │ 1. ASSET           │ →  │ 2. CHARACTER       │ →  │ 3. ANIM SELECT     │ →  │ 4. TUNING          │
  │  annotate .voxel   │    │  measure metrics   │    │  pick clip set     │    │  sweep + detect +  │
  │  → interaction_pt  │    │  per morphology    │    │  per (char, asset) │    │  tune offsets      │
  │  + asset_features  │    │  → CharacterMetrics│    │  → ClipBinding     │    │  → ProfileRecord   │
  └────────────────────┘    └────────────────────┘    └────────────────────┘    └────────────────────┘
        validator              metric_audit              clip_selector            tuner (gated)
```

### Stage 1 — Asset annotation
- **Input:** `.voxel` template + sidecar.
- **Output:** `AssetRecord { interaction_points: [{point_id, kind, frame, features}] }`.
  - `frame` = origin + axes in the asset's local space.
  - `features` = kind-specific dims (e.g. for `sit`: `seat_top_y, seat_width_x, seat_depth_z, seat_center, backrest_height`).
- **Validator (gate):**
  - Required feature keys present and finite.
  - Frame axes orthonormal; origin lies inside the asset's voxel AABB.
  - Features physically plausible (positive widths, seat below backrest, …).
- **Today:** validator implemented in `tools/interaction_pipeline/asset_metrics.py` (`validate_asset_metrics`, `extract_door_features`); enforced by Stage 4 gate. ✅ Stage 1 live.

### Stage 2 — Character metrics
- **Input:** humanoid base + morphology preset (`standard | giant | dwarf | child | …`).
- **Output:** `CharacterMetrics { hip_height, hip_width, body_depth, leg_length, sitting_height, eye_height, shoulder_height, reach, arm_length, … }`. Sourced from the live skeleton via `GET /api/character/metrics`.
- **Validator (gate):**
  - All keys required by every registered kind are present and > 0.
  - Cross-checks: `sitting_height < eye_height`, `hip_width < shoulder_width`, etc.
  - Recorded as a snapshot tied to morphology ID for provenance.
- **Today:** validator implemented in `tools/interaction_pipeline/character_metrics.py` (`validate_character_metrics`, kind-gated required keys); enforced by Stage 4 gate. ✅ Stage 2 live.

### Stage 3 — Animation selection (CURRENTLY MISSING)
- **Input:** character (with available clips + ref-pose), asset (with features), kind.
- **Output:** `ClipBinding { alias → clip_name, score, rationale }` — picks the best variant for this combo from the character's clip library.
  - e.g. `sit_down → sit_down_low_cube` for a short stool, `sit_down → sit_down_chair` for a chair with backrest.
  - e.g. `door_open → door_push` vs `door_pull` based on character side of door.
- **Scoring inputs:** ref-pose hip Y vs `seat_top_y`, backrest presence, handle height vs shoulder height, clip duration, …
- **Validator (gate):** every required alias bound; binding score above a per-kind threshold; otherwise mark combo incompatible.
- **Today:** `tools/interaction_pipeline/clip_selector.py` + `resources/interactions/clip_selection.json` resolve aliases to actual clip names via regex+weight scoring; required aliases enforced. ✅ Stage 3 live.

### Stage 4 — Tuning (offsets + timing)
- **Input:** validated AssetRecord + CharacterMetrics + ClipBinding.
- **Output:** `ProfileRecord { offsets_per_clip, blend_duration, evidence, inputs_snapshot }` written to `humanoid_<archetype>.json`.
- **Validator (gate, before persistence):**
  - Detector findings classified `PROFILE` only (no engine bugs).
  - Smoke pass: the tuned profile runs the interaction end-to-end without escape conditions.
  - **Kind label matches the kind on the AssetRecord** (this would have caught doors saved as `"kind": "sit"`).
  - Offset magnitudes plausible (|y| ≤ seat_top_y + 0.3, |x|,|z| ≤ asset half-diagonal).
- **Today:** `tools/interaction_pipeline/ratification.py` combines Stages 1–3 into a `RatificationReport`; `cli.run_pipeline` calls `ratify_inputs` each iteration and refuses to POST profile deltas when `report.ok == False`. On success, `write_provenance` snapshots inputs to `resources/interactions/<archetype>.provenance.json` keyed by `(template|point|character)`. ✅ Stage 4 gate live.

## Gating contract

```
  AssetRecord       —— validator(1) ——→  ratified asset
  CharacterMetrics  —— validator(2) ——→  ratified character
  (ratified asset + ratified character) —→ clip_selector(3) —→ ClipBinding
  (asset + character + binding) —→ tuner(4) —→ ProfileRecord
```

- The tuner refuses to call `POST /api/interaction/profile` unless it holds a
  ratified record from each prior stage.
- The persisted profile JSON **embeds** the input snapshots and the clip
  binding under a `provenance` block so a failed profile can be traced back to
  which stage's input is at fault.
- Reverting a profile reverts its provenance with it (atomic).

## At a glance — Stage 4 implementation

```
┌──────────────────┐  ┌─────────────┐  ┌──────────────┐  ┌─────────┐  ┌──────────────┐
│ engine_lifecycle │→ │  sweep.py   │→ │ detectors.py │→ │tuner.py │→ │   cli.py     │
│ (start/stop/     │  │ (multi-clip │  │ (profile vs  │  │ (LLM    │  │ (orchestrate │
│  detect/crash)   │  │  telemetry) │  │  engine-bug) │  │  deltas)│  │  iterate)    │
└──────────────────┘  └─────────────┘  └──────────────┘  └─────────┘  └──────────────┘
                                              │                              │
                                              ▼                              ▼
                                  engine_fix_queue.json           game_smoke.py
                                  (human review)                  (final smoke test)
```

## File layout

```
tools/interaction_pipeline/
  __init__.py
  engine_lifecycle.py     EngineSession context manager + Mode enum + crash dumps
  sweep.py                run_sit_sweep() — multi-clip telemetry harvest
  detectors.py            Finding/DetectionResult + 4 detectors + engine fix queue
  tuner.py                LLM tuner (Anthropic/OpenAI/heuristic) + clamp/validate
  game_smoke.py           Final game-mode validation against a project
  cli.py                  Top-level orchestrator + argparse + run_pipeline()
  engine_fix_catalog.json Canonical map of engine-bug symptoms → suspected code
  contact_rules.json      (also at resources/) bone classification rules per interaction

resources/interactions/
  contact_rules.json      Per-interaction expected_contact / free / grounded bone names

.claude/commands/
  interaction-pipeline.md Chat skill — drives the same flow from a chat agent
```

## How a run unfolds

1. **`cli.py`** parses args, opens an `EngineSession(Mode.INTERACTION_EDITOR, target=asset)`.
   The lifecycle manager either reuses an already-running matching engine or
   launches `phyxel.exe --interaction-editor <asset>` and waits for
   `GET /api/engine/status → running=true`.

2. **`sweep.py`** iterates the standard sit clips (`stand_to_sit`, `sitting_idle`,
   `sit_to_stand`) and at each sample fraction `t ∈ {0.0, 0.2, … 1.0}`:
   - `POST /api/interaction/ie/seek?clip&t` (pauses on that frame)
   - settles ~150 ms
   - `GET /api/interaction/ie/telemetry?clip&t&include_bones=1`
   - optionally orbits 3 camera angles and captures screenshots

3. **`detectors.py`** classifies every interesting condition into one of:
   - `FindingKind.PROFILE` — a human/LLM tuner can fix by editing offsets
   - `FindingKind.ENGINE_BUG` — a code change in the engine is required

   Engine bugs are written to `engine_fix_queue.json` (append-only) with a
   `catalog_ref` pointing at the suspected file/line from `engine_fix_catalog.json`.

4. **`tuner.py`** consumes the sweep + detection result and emits
   `ProfileDelta` entries. Backend priority: Anthropic → OpenAI → heuristic
   fallback. All LLM outputs are hard-clamped (`|Δxyz|≤0.3`, `|Δyaw|≤15°`).
   The tuner refuses to recommend continuing when engine bugs are present —
   so we don't paper over real bugs with offset tweaks.

5. **`cli._apply_profile_deltas()`** GETs the current profile, applies the deltas
   to `sit_down_offset` / `sitting_idle_offset` / `sit_stand_up_offset`, and POSTs
   the merged profile back.

6. The loop repeats up to `--max-iterations` (default 3). It stops early when
   either the detector finds 0 profile findings or the tuner says
   `recommend_continue=false`.

7. **`game_smoke.py`** (optional, when `--project <dir>` is given) opens the
   engine in project mode, spawns a humanoid, fires the sit interaction, waits
   ~3 s, and compares key bone positions against the IE baseline within
   ±0.15 m tolerance.

## Engine endpoints introduced for the pipeline

| Endpoint | Method | Purpose |
|---|---|---|
| `/api/engine/status` | GET | `{running, mode, loaded_asset, loaded_project, pid, uptime_s, port}` |
| `/api/engine/shutdown` | POST | Clean glfwSetWindowShouldClose path |
| `/api/interaction/ie/telemetry` | GET | Per-frame centroid, facing_yaw, feet, classified bones |

`ie/telemetry` does the heavy lifting: it classifies every bone against the
loaded asset's voxel boxes (resolved via `ObjectTemplateManager`) into one of:

- `free` — outside the asset, no contact expected
- `desired_contact` — touching a surface that *should* support the character (seat top)
- `inside_asset` — penetrating geometry that should be free
- `inside_world` — penetrating non-asset world voxels

Each bone returns a signed distance (negative = penetration). The detectors
then apply tolerances from `resources/interactions/contact_rules.json`.

## Engine fix catalog

`engine_fix_catalog.json` maps detector IDs → suspected engine source location.
Current entries:

| ID | Suspected location |
|---|---|
| `POSITION_SNAP_AT_CLIP_BOUNDARY` | `engine/src/scene/AnimatedVoxelCharacter.cpp:1875–1900` (clip transition) |
| `POSE_FEET_DESYNC` | same — pose recompute on clip change |
| `INITIAL_TELEPORT` | `AnimatedVoxelCharacter.cpp:1231` (`sitAt()` entry) |
| `BLEND_DURATION_ASYNC` | same — blend duration not equal across the two state transitions |

When the pipeline writes to `engine_fix_queue.json` it includes the symptom,
evidence (frames, deltas), and the catalog entry. A human is expected to
investigate; the pipeline never modifies engine code on its own.

## Adding a new interaction type

To support a new interaction (e.g. `lying_down`, `climb_ladder`):

1. **Add to `resources/interactions/contact_rules.json`**:
   ```json
   "lying_down": {
     "expected_contact_bones": ["Spine", "Spine1", "Pelvis", ...],
     "expected_free_bones": ["Head", ...],
     "grounded_bones": [],
     "tolerances": { "contact": 0.05, "free_clearance": 0.03 }
   }
   ```

2. **Add clips to `sweep.py`** — extend `SIT_CLIPS` or add a new function
   `run_lying_sweep()` with the appropriate clip names from the .anim file.

3. **Extend detectors**:
   - The bone-contact detectors (`detect_offset_too_low_or_high`) already
     consume `contact_rules.json` and will work for any interaction key.
   - Add interaction-specific checks if the geometry has different invariants
     (e.g. for climb ladders: hand contact alternates frames).

4. **Add a catalog entry** if the new interaction can fail in an
   engine-specific way that profile changes can't fix.

5. **Extend `cli.py`** if the new interaction needs different
   `offset_keys` than the sit `sit_down/sitting_idle/sit_stand_up` triplet.

## Running locally

Direct CLI:

```powershell
.\.venv\Scripts\python.exe -m tools.interaction_pipeline.cli `
    resources/templates/chair_wood.voxel `
    --max-iterations 3 --samples 6
```

With game-mode smoke at the end:

```powershell
.\.venv\Scripts\python.exe -m tools.interaction_pipeline.cli `
    resources/templates/chair_wood.voxel `
    --project samples/sample_project --max-iterations 3
```

Dry run (sweep + diagnose, no profile writes):

```powershell
.\.venv\Scripts\python.exe -m tools.interaction_pipeline.cli `
    resources/templates/chair_wood.voxel --no-apply --no-screenshots
```

From a chat agent: `/interaction-pipeline chair_wood`

## Crash policies

`EngineSession` accepts `on_crash`:

| Policy | Behavior |
|---|---|
| `abort` (default) | Snapshot logs + state to `reports/crashes/<ts>/`, re-raise |
| `restart-once` | One automatic relaunch; if it crashes again, abort |
| `restart-and-skip` | Relaunch, but skip the current iteration's findings |

The dump always includes: `crash.json` (reason + last status), `phyxel.log` (tail),
and the in-flight sweep state if any.

## Testing

```powershell
.\.venv\Scripts\python.exe -m pytest tests/test_interaction_pipeline.py -v
```

19 deterministic unit tests covering:
- sweep sample helpers (`_samples_for_clip`, `_slugify`)
- All 4 detectors (positive + negative cases)
- engine_fix_queue append + catalog enrichment
- Heuristic tuner (raises y on penetration, clamps at ±0.2, defers on engine bugs)
- LLM JSON extraction (markdown-fenced + prose-wrapped)
- LLM response clamping (`±0.3` xyz, `±15°` yaw)

## Troubleshooting

| Symptom | Action |
|---|---|
| `engine launch timeout` | Verify `phyxel.exe` built; check port 8090 isn't held by zombie process |
| `crash dump in reports/crashes` | Read `crash.json` + `phyxel.log`; engine bug — fix before iterating |
| `tuner backend=heuristic` despite API key set | Re-run with `--tuner-backend anthropic` to surface the real error |
| Deltas don't reduce findings | Probable engine bug masquerading as profile issue — check `engine_fix_queue.json` |
| Game smoke FAIL but IE converged | Project player skeleton/archetype differs from IE character; `KEY_BONES` rebases against live Hips |
| `ie/telemetry` returns no bones | Asset has no voxel content, or template name doesn't match `.voxel` stem |

## Related skills / docs

- `/sit-validate` — visual-only spot check, no tuning
- `/generate` — produce a `.voxel` template before validating it
- `/visual-test` — generic visual regression skill
- [docs/CharacterAnimationGuide.md](CharacterAnimationGuide.md)
- [docs/MCPIntegration.md](MCPIntegration.md)

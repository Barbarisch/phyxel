# /interaction-pipeline — End-to-end interaction tuning pipeline

Automates the full loop for validating and tuning an interaction asset (chair, door, etc.)
against the engine. Drives engine lifecycle, runs a multi-clip animation sweep, classifies
findings as **profile** (auto-tunable) vs **engine_bug** (queued for human review), feeds
the LLM tuner, applies profile deltas, re-validates up to N iterations, and finishes with
a game-mode smoke test.

The asset/template to validate is: $ARGUMENTS

---

## When to use this skill

Use `/interaction-pipeline` when the user asks to:

- "make me a chair the humanoid can sit in"
- "tune the sit pose for chair_wood"
- "validate my new bench"
- "automatically fix the interaction profile for X"
- "verify X works in-game after tuning"

For **visual-only spot-checks** (no tuning, no game-mode test), use `/sit-validate`.
For **asset generation only** (no engine validation), use `/generate`.

---

## Parse Arguments

Extract from `$ARGUMENTS`:

- **`<template_name>`** — `.voxel` stem (e.g. `chair_wood`). Strip `.voxel` if included.
- **`--max-iterations N`** — tuning loop cap (default 3).
- **`--samples N`** — samples per clip (default 6; one at each 0.0, 0.2, … 1.0).
- **`--no-screenshots`** — telemetry-only, faster.
- **`--tuner anthropic|openai|heuristic`** — backend selection (default: auto).
- **`--no-apply`** — sweep + diagnose only; do NOT write profile changes.
- **`--project <path>`** — also run the game-mode smoke test against this project.
- **`--archetype humanoid_normal`** — character archetype (default).
- **`--point-id seat_0`** — interaction point on the asset (default).

Confirm: `template=<name>, max_iters=<n>, project=<path|none>, tuner=<backend>`

---

## Engine Lifecycle (handled by the controller — DO NOT manually launch/kill)

The `tools/interaction_pipeline/engine_lifecycle.py` controller owns the engine process.
It transparently:

- Detects an already-running engine in the **correct mode** → reuses it.
- Detects an engine in the **wrong mode/asset** → stops + relaunches.
- Detects an **unresponsive API** → hard-kills + relaunches.
- Watches `phyxel.log` for fatal patterns + the process for death → captures a crash dump
  under `tools/interaction_pipeline/reports/crashes/<ts>/` and re-raises.
- Only stops the engine on exit **if it started it** — never kills an engine the user
  launched manually.

You do not need to call `launch_engine` / `stop_engine` / `engine_running` MCP tools
yourself. Just invoke the orchestrator below.

If the build is stale, run `build_project` MCP tool **before** invoking the orchestrator.

---

## Pre-flight

1. Use the `engine_running` MCP tool to check current state. If a project-mode engine is
   already up and the user wants to keep it, **stop_engine first** before starting the
   pipeline (it will need IE mode).
2. If the binary was modified since last build, call `build_project` (config: "Debug").
3. Confirm the template `.voxel` file exists at
   `resources/templates/<template_name>.voxel`. If not, suggest running `/generate` first.

---

## Run the Pipeline

Invoke the Python orchestrator. Always use the venv's interpreter
(`g:/Github/phyxel/.venv/Scripts/python.exe` on Windows) so dependencies resolve.

```powershell
& g:/Github/phyxel/.venv/Scripts/python.exe -m tools.interaction_pipeline.cli `
    resources/templates/<template_name>.voxel `
    --max-iterations <N> `
    --samples <S> `
    [--tuner-backend <backend>] `
    [--no-apply] `
    [--project <project_dir>] `
    [--archetype humanoid_normal] `
    [--point-id seat_0]
```

The orchestrator streams progress to stdout. Each iteration logs:

```
[pipeline] === Iteration <i>/<N> ===
[sweep] clip=stand_to_sit t=0.00 (boundary=True)
[sweep] clip=stand_to_sit t=0.20 (boundary=False)
...
[pipeline] detector: <P> profile, <E> engine
[pipeline] tuner (<backend>): <D> deltas, confidence=<c>, continue=<bool>
[pipeline] applied profile deltas: {...}
```

Stop conditions:
- 0 profile findings → **converged**, skip remaining iterations.
- Tuner returns `recommend_continue=false` → stop.
- Tuner returns no deltas → stop.

---

## After the Pipeline Finishes

The orchestrator writes a top-level result file:

```
tools/interaction_pipeline/reports/<asset_stem>/pipeline_<ts>.json
```

Read it to summarize. Key fields to report back to the user:

- `converged` — bool
- `iterations[*].detection.profile_count` / `engine_count`
- `iterations[*].tuner.profile_deltas` — what was applied
- `iterations[*].applied_profile` — confirmation from the engine
- `engine_fix_queue` — path if any engine bugs were detected (these are NOT auto-applied;
  flag them clearly in the summary)
- `game_smoke_result.passed` / `.bone_deltas` — if `--project` was supplied

If `engine_fix_queue` is non-null, **prominently list** the queued engine bugs by their
catalog ID (e.g. `POSITION_SNAP_AT_CLIP_BOUNDARY`) and point the user at
`engine/src/scene/AnimatedVoxelCharacter.cpp` so a human can act on them.

---

## Visual Evidence

Each iteration's sweep produces:

- `reports/<asset_stem>/<run_id>/report.json` — full per-frame telemetry
- `reports/<asset_stem>/<run_id>/bones.csv` — flat per-bone-per-frame table
- `reports/<asset_stem>/<run_id>/screenshots/kf###_<clip>_t###_<view>.png`

When summarizing, embed 2–3 representative screenshots in the response:
- One at `stand_to_sit, t=0.00`
- One at `sitting_idle, t=0.50`
- One at `sit_to_stand, t=1.00`

If `--project` was used, also embed the `smoke_iso.png` from the game-smoke run.

---

## Final Summary Template

```
✅ Pipeline complete for <template_name>
   Iterations: <N>  (converged: <yes/no>)
   Final profile deltas: <list>
   Engine bugs queued for human review: <count>
     - POSITION_SNAP_AT_CLIP_BOUNDARY at AnimatedVoxelCharacter.cpp:1875
     - ...
   Game-mode smoke: <PASS/FAIL/skipped>
   Reports: tools/interaction_pipeline/reports/<stem>/pipeline_<ts>.json
```

Be honest. If the visual evidence contradicts the numbers, say so. If iterations failed
to converge, explicitly call it out — do not declare success.

---

## Troubleshooting

| Symptom | Likely cause | Action |
|---|---|---|
| `engine launch timeout` | Stale build, port in use, GPU init slow | Run `build_project`; check port 8090; raise `--launch-timeout` if available |
| Crash dump in `reports/crashes/<ts>/` | Engine crashed during sweep | Read `crash.json` reason + `phyxel.log` tail; fix engine bug first |
| `tuner backend=heuristic` despite key set | API call failed silently | Re-run with `--tuner-backend anthropic` to force-fail loudly |
| Profile deltas don't reduce findings | Engine bug masquerading as profile issue | Inspect `engine_fix_queue.json` — likely entry there too |
| Game smoke FAIL but IE converged | Project player skeleton differs from IE character | Verify same archetype; rebase comparison handled by `KEY_BONES` |

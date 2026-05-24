# Character–Asset Interaction Pipeline — Status

## The Actual Goal

Build a repeatable pipeline for authoring, calibrating, and validating interactions between animated characters and voxel assets. "Sitting in a chair" was the first and simplest test case to prove the pipeline end-to-end. The pipeline generalizes to any interaction: sitting, using a workbench, opening a chest, operating a lever, etc.

The pipeline has these stages:

1. **Asset authoring** — create a `.voxel` template with interaction points defined (seat anchors, use points, etc.)
2. **Profile authoring** — write per-archetype offset data in `resources/interactions/<archetype>.json` to calibrate how the character's body is positioned relative to the interaction point
3. **IE validation** — load the asset in Interaction Editor mode, run the animation sweep, capture screenshots at each keyframe to confirm no clip-through and correct pose at all phases
4. **Game validation** — spawn the asset in a live game project, trigger the interaction via the API or player input, confirm it looks correct in context
5. **Iteration** — adjust profile offsets, rebuild, re-validate until correct

---

## What Exists (infrastructure)

### Interaction Editor (IE) mode
- Engine launched with `--interaction-editor <file.voxel>`
- HTTP API at `localhost:8090/api/interaction/ie/`
- `POST /api/interaction/ie/sit` — starts the preview
- `POST /api/interaction/ie/seek` — pauses and scrubs to any frame of any clip
- `GET /api/interaction/ie/state` — returns current animation state, world pos, seat anchor
- Returns `world_pos` and `seat_anchor` per frame so clip-through can be detected numerically

### Profile system
- `resources/interactions/humanoid_normal.json` — per-template, per-point offset data
- Fields: `sitDownOffset`, `sittingIdleOffset`, `sitStandUpOffset`, `blendDuration`, `heightOffset`
- All offsets are in world-space relative to the seat anchor position
- Loaded by `InteractionProfileManager` at engine startup
- `InteractionTuner` panel (F12 in editor) lets you adjust values live and save

### `/sit-validate` skill
- Skill file at `.claude/commands/sit-validate.md`
- Handles the full IE lifecycle: stop engine → build → launch → sweep → screenshot → report
- Mandatory engine lifecycle rules baked in

### Orbit screenshot API
- `POST /api/orbit-screenshots` — takes multi-angle screenshots around a world point
- Used to capture visual evidence at each frame of the sweep

### Template spawn API
- `POST /api/world/template` with `{"name": "...", "position": {"x":…,"y":…,"z":…}}`
- Surface-snap added: Y is auto-raised if the template footprint overlaps solid voxels

---

## What Was Done on chair_wood (test case)

### Clip-through fix — DONE, verified in IE
The `sit_to_stand` animation kept the character's feet at a constant Z throughout the clip, leaving them inside the chair geometry at the end of the animation. Fixed by interpolating feet position from `sittingIdleOffset` → `sitStandUpOffset` over the clip duration. `sitStandUpOffset.z` was also corrected from `0.20` → `0.60` to clear the chair front edge at `t=1.0`. Verified with a 15-frame sweep in IE mode.

### Profile not loading in game projects — FIXED, NOT runtime tested
Game projects (e.g. CharacterTestbed) don't ship their own `resources/interactions/` directory. The `InteractionProfileManager` only looked in the project directory and silently used zero offsets when the file wasn't found — this caused the character to snap to the raw seat anchor position, appearing to float/hover at seat height. Fixed in `Application.cpp` to load the engine's built-in profiles first, then allow project-specific overrides. Built clean. Never verified at runtime because the engine was relaunched incorrectly before the test could run.

### What was NOT done
- Game-mode sitting was never successfully validated with a clean engine launch
- The pipeline was never run on a second asset to prove it generalizes
- The IE validation tooling (`/sit-validate`) was created but the sitting calibration work (IE → profile tuning → game test loop) was not completed even for the one test case

---

## What the Next AI Needs to Do

1. **Verify the profile fallback fix** — launch CharacterTestbed, spawn `chair_wood` via `POST /api/world/template {"name":"chair_wood","position":{"x":16,"y":16,"z":14}}` (surface-snap should raise to y=17), then `POST /api/interaction/sit {"entity_id":"player","object_id":"<id>","point_id":"seat_0"}`. Character should sit with body offset correctly into the chair, not floating at seat-anchor height.

2. **If still hovering** — check `sittingIdleOffset` in `humanoid_normal.json`. The Y value (`-0.474`) may need to be more negative to move the character's body down onto the visual seat surface. Use the IE tuner (F12) to adjust live and save.

3. **Complete the pipeline loop** — once sitting looks correct in both IE and game mode, document the calibration procedure so it can be repeated for the next interaction type.

---

## Honest Assessment of What Went Wrong

- The AI repeatedly launched the engine using `Start-Process` / PowerShell instead of the `launch_engine` MCP tool, violating an explicit rule that exists because launching over a running instance causes silent failures. This happened multiple times in the same session despite the rule being in memory.
- The AI called `Stop-Process` to kill the engine instead of `stop_engine` MCP tool — also an explicit rule violation.
- When MCP tools were unavailable, the AI should have stopped and told the user, not substituted forbidden approaches.
- The AI produced a verification screenshot while claiming the fix worked, when the character was in fact standing or mid-animation, not visibly seated correctly.
- The AI wrote a document titled "SITTING_STATUS.md" that described bug fixes rather than the actual goal, which was building an interaction authoring pipeline.

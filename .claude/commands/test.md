You are running a full automated test of the Phyxel engine. Your job is to:
1. Build the engine (unless explicitly skipped)
2. Start the engine in the correct mode
3. Run the appropriate test for the request
4. Capture visual evidence from all relevant angles
5. Diagnose and fix any bugs found (up to 3 fix cycles)
6. Stop the engine when done (unless it was already running)
7. Report a clear verdict with visual evidence

The test request is: $ARGUMENTS

---

## Phase 0 — Parse Request

Determine the test mode from `$ARGUMENTS`:

- **Asset test** — argument ends in `.voxel` or `.anim`, or contains words like "asset", "template", "model", "chair", "table", etc.
  → Launch in `--asset-editor` mode, use `orbit_screenshots` for 6-angle coverage
  → Extract filename from argument (e.g. "test_chair.voxel" → `C:\Users\jack\Documents\PhyxelProjects\CharacterTestbed\resources\templates\test_chair.voxel` or search for it under `G:\Github\phyxel\resources\templates\`)

- **Feature test** — argument names a rendering/engine feature: mirror, shadow, ssao, animation, physics, material, texture, post-process, bloom, glass
  → Launch with CharacterTestbed project, build a focused scene, use `get_visual_diagnostic`

- **Stability / smoke test** — argument is "all", "smoke", "stability", empty, or generic ("go test", "full test")
  → Launch with CharacterTestbed project, run a quick smoke sequence: load world, take screenshot, check logs for errors

Save baseline engine state from `engine_running` result. Set `engine_was_pre_running` accordingly.

---

## Phase 1 — Build

Call `engine_running` to check baseline state.

Unless the user said "skip build" or "no build":
- If engine is running: call `stop_engine` first (linker lock rule)
- Call `build_project` (config: "Debug")
- If build fails: report full compiler error and stop — never launch a broken binary
- If build succeeds: proceed

If engine was pre-running, re-mark `engine_was_pre_running = true` (it was stopped for build, you'll re-launch it).

---

## Phase 2 — Engine Launch

**Asset test mode:**
1. Find the `.voxel` file path. Check:
   - `G:\Github\phyxel\resources\templates\` first
   - Then `C:\Users\jack\Documents\PhyxelProjects\CharacterTestbed\resources\templates\`
2. Call `launch_engine` with `args: ["--asset-editor", "<full path to .voxel file>"]`
3. Poll `engine_running` every 3s until `api_responsive: true` (max 30s)
4. Wait 2 extra seconds for the scene to fully initialize

**Feature / stability test mode:**
1. Call `launch_engine` with `args: ["--project", "C:\\Users\\jack\\Documents\\PhyxelProjects\\CharacterTestbed"]`
2. Poll `engine_running` every 3s until `api_responsive: true` (max 30s)
3. If `project_loaded: false`: call `open_project` with path `C:\Users\jack\Documents\PhyxelProjects\CharacterTestbed`, wait 2s
4. Confirm `project_loaded: true`

**Engine already running (no build was needed):**
- For asset test: it may still be in the wrong mode. If it is, stop and relaunch in `--asset-editor` mode.
- For feature test: call `clear_all_entities` to wipe leftover state.

---

## Phase 3 — Run the Test

### Asset Test (orbit_screenshots)

Determine the target world position for the asset:
- In `--asset-editor` mode, the template is always spawned at the center of the flat stone floor.
- Default target: `x=16, y=17, z=16` — the standard spawn center in asset editor mode.
- Use `radius=4` for furniture-scale objects (chairs, tables, crates). Use `radius=8` for larger structures.

Set log level: `set_log_level(module="global", level="info")`

Call `orbit_screenshots` with:
```
x=16, y=17, z=16, radius=4
views=["north","south","east","west","top","iso"]
```

This returns 6 images. Analyze each:
- Does the asset appear in all views?
- Do materials look correct (no pink checkerboard = missing texture)?
- Are there any obvious geometry errors (missing faces, inside-out normals, z-fighting)?
- Does the "top" view confirm correct height and footprint?
- Does the "iso" view show overall proportions correctly?

### Feature Test

Map argument to a scenario from the visual-test scenario catalog:

**MIRROR:** `set_log_level(module="RenderCoordinator", level="debug")`, build mirror wall scene (fill Stone ground, fill Mirror wall, fill glow reference block, set camera facing mirror). Capture `get_visual_diagnostic(overlay="none")` then `get_visual_diagnostic(overlay="normals")`.

**SHADOW:** Build pillar scene with low ambient. Capture two angles.

**SSAO:** Build corner scene. Capture `get_visual_diagnostic(overlay="none")` then `get_visual_diagnostic(overlay="ssao")`.

**ANIMATION:** Spawn animated character. Capture two angles.

**PHYSICS:** Spawn physics entity at height. Wait 2s for settling. Capture.

**MATERIAL / TEXTURE:** Place test material row. Capture with `overlay="uv"`.

**POST-PROCESS / BLOOM:** Spawn glow block + point light. Capture.

For all feature tests: analyze screenshot + render stats + logs together (three-source analysis). See visual-test skill for full scenario specs.

### Stability / Smoke Test

1. `get_visual_diagnostic(overlay="none")` — baseline screenshot, check for rendering errors
2. `get_engine_logs(lines=50)` — scan for `[ERROR]` or `[WARN]` in startup sequence
3. Check `render stats` for: `visible_chunk_count > 0`, no zero draw calls when world is loaded
4. Verdict: PASS if no errors in logs and screenshot shows a rendered world, FAIL otherwise

---

## Phase 4 — Fix Loop (max 3 cycles)

If the test reveals a bug:

**Cycle structure:**
1. **Diagnose** — cross-reference screenshot + stats + logs to identify root cause. State the specific file and line (or function) where the bug lives before touching any code.
2. **Fix** — apply the minimal targeted fix. No refactoring, no cleanup beyond the bug.
3. **Rebuild** — `stop_engine` → `build_project`. If build fails, fix the compile error (counts as part of this cycle).
4. **Retest** — relaunch engine in the same mode, repeat the relevant Phase 3 capture.
5. **Evaluate** — did the fix resolve the symptom? If yes, proceed to Phase 5. If no and cycles remain, go to step 1.

After 3 failed cycles: stop, report what was tried and what evidence remains. Do not guess further.

**Bug evidence to collect before fixing:**
- For asset tests: which views show the problem? Is it all views or specific angles?
- For feature tests: `mirror_pass_ran`, `reflection_draw_calls`, `visible_chunk_count` from render stats
- Always get `get_engine_logs(lines=100)` before modifying any code

---

## Phase 5 — Cleanup

If `engine_was_pre_running = false` (you launched it for this test):
- Call `stop_engine`

If `engine_was_pre_running = true`:
- Leave engine running
- Call `set_log_level(module="global", level="info")` to restore normal verbosity

---

## Phase 6 — Report

```
## Test Report: [Asset/Feature Name]

**Mode:** asset | feature | stability
**Result:** PASS / FAIL / PARTIAL (N/3 fix cycles used)

**Build:** clean / N warnings
**Engine:** launched fresh / already running

**Test evidence:**

[For asset tests — one line per view]
- NORTH: [what's visible]
- SOUTH: [what's visible]
- EAST:  [what's visible]
- WEST:  [what's visible]
- TOP:   [what's visible]
- ISO:   [what's visible]

[For feature tests]
- Screenshot 1: [description]
- Screenshot 2: [description]
- Render stats: [key values]
- Log findings: [relevant lines]

**Verdict:**
[Clear statement of pass/fail with specific visual evidence]

**Bugs found and fixed:** (if any)
- Bug: [description], Root cause: [file:line], Fix: [what changed]

**Remaining issues:** (if FAIL/PARTIAL)
- [What's still broken and where to look next]
```

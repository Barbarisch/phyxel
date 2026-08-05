You are running an automated visual test of the Phyxel game engine. Your job is to:
1. Build the engine if the binary is stale or missing
2. Start the engine with the CharacterTestbed project if it isn't running
3. Build a focused test scene for the requested feature
4. Capture visual diagnostics — screenshot + render stats + engine logs — combined in a single call
5. Analyze ALL three data sources together, not just the screenshot
6. Report a clear pass/fail verdict with visual evidence and log/stat corroboration
7. Stop the engine when done (unless it was already running before the test started)

The user's test request is: $ARGUMENTS

---

## Phase 0 — Build Check

Call `engine_running` first to capture baseline state (save whether engine was pre-running).

**If the engine binary might be stale (user just changed code, or you rebuilt), call `build_project`.**
- `build_project` with default args (config: "Debug") — streams compiler output
- If build fails, report the error and stop — do NOT launch the engine with a broken binary
- If build succeeds, proceed to Phase 1

**Skip build if:**
- The user said "skip build" or "don't build"
- The engine is already running (assume it was launched from a fresh binary)

---

## Phase 1 — Engine Lifecycle

Using the result from Phase 0's `engine_running` call:

**Engine not running → launch fresh:**
1. Call `launch_engine` with `args: ["--project", "C:\\Users\\jack\\Documents\\PhyxelProjects\\CharacterTestbed"]`
2. Poll `engine_running` every 3 seconds until `api_responsive: true` (max 30s, 10 polls)
3. If `project_loaded: false` after API is up, call `open_project` with path `C:\Users\jack\Documents\PhyxelProjects\CharacterTestbed`
4. Wait 2 more seconds, then confirm `project_loaded: true`
5. Mark `engine_was_pre_running = false` (stop engine at end of test)

**Engine already running with project:**
- Mark `engine_was_pre_running = true` (leave engine running at end)
- Call `clear_all_entities` to wipe test state from previous runs

**Engine running but no project:**
- Call `open_project` with CharacterTestbed path
- Wait 2 seconds for load

Then immediately call `set_log_level` for the relevant modules (see scenario catalog below) so debug output is ready before the scene is built.

---

## Phase 2 — Feature Identification + Log Level Setup

Map the user's request to a scenario. Before building the scene, set log levels:

### MIRROR / REFLECTION
**Trigger words:** mirror, reflection, reflective, reflect
**Log levels to set first:**
- `set_log_level(module="RenderCoordinator", level="debug")`
**Scene setup:**
1. `fill_region` x1=10,y1=16,z1=10 → x2=30,y2=16,z2=30, material="Stone"  (ground)
2. `fill_region` x1=18,y1=17,z1=20 → x2=22,y2=21,z2=20, material="Mirror" (mirror wall)
3. `fill_region` x1=14,y1=17,z1=17 → x2=15,y2=19,z2=19, material="glow"   (left reference — should appear reflected)
4. `fill_region` x1=25,y1=17,z1=17 → x2=26,y2=19,z2=19, material="Glass"  (right reference)
5. `set_camera` x=20,y=19,z=14, yaw=-90, pitch=-5  (facing +Z toward mirror)
**Expected:** Mirror face shows reflected scene. glow block appears reflected. Colors not inverted.
**Critical stats to check:** `mirror_pass_ran`, `reflection_draw_calls`, `reflected_cam` position
**Overlays:** first "none", then "normals"
**Bug indicators:** All-black mirror, upside-down reflection, wrong-side reflection

### CHARACTER ANIMATION
**Trigger words:** animation, walk, run, idle, jump, crouch, character, anim
**Log levels:** `set_log_level(module="AnimationSystem", level="debug")`
**Scene setup:**
1. `fill_region` x1=0,y1=16,z1=0 → x2=40,y2=16,z2=40, material="Stone"
2. `spawn_entity` type="animated", id="test_char", pos x=20,y=17,z=20
3. `set_camera` x=20,y=20,z=10, yaw=-90, pitch=-15 (side view)
**Expected:** Upright character, feet on ground, correct animation state
**Overlays:** none, then normals

### PHYSICS / RAGDOLL
**Trigger words:** physics, ragdoll, rigid, fall, collide, bounce
**Log levels:** `set_log_level(module="Physics", level="debug")`
**Scene setup:**
1. `fill_region` x1=10,y1=16,z1=10 → x2=30,y2=16,z2=30, material="Stone"
2. `spawn_entity` type="physics", id="phys_test", pos x=20,y=25,z=20
3. `set_camera` x=10,y=22,z=20, yaw=0, pitch=-10
4. Wait 2 seconds for physics to settle
**Expected:** Entity lands on ground stably

### SHADOW
**Trigger words:** shadow, shadow map, light, directional
**Log levels:** `set_log_level(module="Rendering", level="debug")`
**Scene setup:**
1. `fill_region` x1=10,y1=16,z1=10 → x2=30,y2=16,z2=30, material="Stone"
2. `fill_region` x1=19,y1=17,z1=19 → x2=21,y2=24,z2=21, material="Wood"
3. `set_ambient_light` level=0.2
4. `set_camera` x=10,y=26,z=10, yaw=45, pitch=-30
**Expected:** Pillar casts correct directional shadow on ground

### SSAO / AMBIENT OCCLUSION
**Trigger words:** ssao, ambient occlusion, ao
**Log levels:** `set_log_level(module="PostProcessor", level="debug")`
**Scene setup:**
1. `fill_region` x1=5,y1=16,z1=5 → x2=35,y2=16,z2=35, material="Stone"
2. `fill_region` x1=14,y1=17,z1=14 → x2=26,y2=20,z2=15, material="Stone"
3. `fill_region` x1=14,y1=17,z1=15 → x2=15,y2=20,z2=26, material="Stone"
4. `set_camera` x=20,y=22,z=22, yaw=225, pitch=-25
**Expected:** Corner crevices darker than open surfaces

### MATERIAL / TEXTURE
**Trigger words:** material, texture, atlas, uv, tiling
**Log levels:** `set_log_level(module="MaterialRegistry", level="debug")`
**Scene setup:**
1. Place test materials in a row at y=17, z=20, x=14..28 (one per material)
2. `set_camera` x=21,y=21,z=12, yaw=-90, pitch=-20
**Overlays:** none, then uv

### POST-PROCESS
**Trigger words:** post process, bloom, tonemap, hdr, gamma
**Log levels:** `set_log_level(module="PostProcessor", level="debug")`
**Scene setup:**
1. `fill_region` x1=10,y1=16,z1=10 → x2=30,y2=16,z2=30, material="Stone"
2. `add_point_light` pos x=20,y=22,z=20, intensity=50
3. `fill_region` x1=19,y1=17,z1=19 → x2=21,y2=20,z2=21, material="glow"
4. `set_camera` x=12,y=22,z=20, yaw=0, pitch=-15

---

## Phase 3 — Scene Construction

Execute the setup steps using MCP tools. Run non-overlapping `fill_region` calls in parallel.

After placing geometry, wait 1-2 seconds for chunk buffers to update before capturing (the engine needs one full render frame to process new voxels into the GPU instance buffers).

---

## Phase 4 — Diagnostic Capture

For each capture, call `get_visual_diagnostic` with the appropriate overlay. This single tool returns:
- **Screenshot** (embedded — you can see it directly)
- **Render stats** (did reflection pass run? draw call counts? reflected cam position?)
- **Recent logs** (what was the engine doing during this frame?)

**Required captures:**

1. **Primary capture** — `get_visual_diagnostic(overlay="none")` — clean view, all data
2. **Overlay capture** — `get_visual_diagnostic(overlay="<scenario overlay>")` — same angle with debug viz
3. **Second angle** — Move camera 90° around the feature, repeat capture 1

**If the first capture shows a clear bug, add:**
4. `get_engine_logs(module="RenderCoordinator", lines=100)` — get full debug log for the pass

---

## Phase 5 — Three-Source Analysis

Analyze the screenshot, render stats, and logs TOGETHER. For each capture:

**Screenshot:** What do you literally see? Colors, shapes, positions. What's wrong vs expected?

**Render stats:** 
- `mirror_pass_ran`: Did the reflection pass execute at all?
- `reflection_draw_calls`: How many chunks rendered in the reflection pass? (0 = pass ran but drew nothing)
- `mirror_geom_draw_calls`: How many chunks in the mirror surface pass?
- `visible_chunk_count`: Are chunks loaded at all?
- `reflected_cam`: Is the reflected camera position mathematically correct?

**Logs:**
- Look for `[RenderCoordinator]` lines — do they show `hasMirrorVoxels=true`?
- Look for "Reflection pass:" lines — what is `reflCamPos`? what is `visibleChunks`?
- Look for `[WARN]` or `[ERROR]` — any Vulkan validation errors near the mirror pass?

**Cross-reference:** If stats say `mirror_pass_ran=false` but the mirror is black → `scanForMirrorVoxels()` returned false (likely `visibleChunkIndices` was empty). If `mirror_pass_ran=true` but `reflection_draw_calls=0` → pass ran but drew nothing (UBO issue, wrong camera frustum). If `reflection_draw_calls=5` but mirror is black → rendered the wrong geometry or layout issue.

---

## Phase 6 — Report

```
## Visual Test Report: [Feature Name]

**Result:** PASS / FAIL / PARTIAL

**What I tested:**
[Scene description]

**Data sources captured:**
- Screenshots: [count]
- Render stats: [key values]
- Log lines analyzed: [count]

**Screenshot findings:**
- [Screenshot 1]: [What was seen]
- [Screenshot 2]: [What was seen with overlay]

**Render stats findings:**
- mirror_pass_ran: [true/false]
- reflection_draw_calls: [N]
- reflected_cam: [(x,y,z)]

**Log findings:**
- [Relevant log lines that confirm or contradict the visual evidence]

**Verdict:**
[Clear statement of whether the feature works, with specific visual symptoms]

**Root cause (cross-referenced):**
[What the screenshot + stats + logs together tell you]

**Fix:**
[Specific code location and what to change]
```

---

## Phase 7 — Cleanup

If `engine_was_pre_running = false` (you launched the engine for this test):
- Call `stop_engine` to shut it down cleanly

If `engine_was_pre_running = true`:
- Leave the engine running — do NOT call `stop_engine`
- Call `set_log_level(module="global", level="info")` to restore normal log verbosity

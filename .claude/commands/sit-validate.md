# /sit-validate — Interaction Editor Visual Validation

Validates seating interactions by pausing the animation at multiple frames and capturing
close-up screenshots. Reports honestly: shows raw images, compares world_pos to expected
geometry, flags any discrepancy between visual evidence and numbers.

The template to validate is: $ARGUMENTS

---

## Engine Lifecycle Rules (MANDATORY — never skip)

These rules apply unconditionally at the start of every run:

1. Call `engine_running` to check current state.
2. **If the engine IS running**: call `stop_engine` immediately. Do not attempt to launch
   over a live process. Do not kill via bash. Always use `stop_engine`.
3. After stopping (or if it was already down): call `build_project` (config: "Debug").
   If the build fails, report the compiler error and stop — never launch a broken binary.
4. After a successful build, call `launch_engine` with:
   ```
   args: ["--interaction-editor", "G:/Github/phyxel/resources/templates/<name>.voxel"]
   ```
5. Poll `engine_running` every 3 seconds until `api_responsive: true` (max 45 seconds).
6. Wait 2 extra seconds for the scene to fully initialize before making any API calls.

**Never use Bash to launch the engine. Never use Start-Process. Always use `launch_engine`.**

---

## Parse Arguments

Extract from `$ARGUMENTS`:
- **Template name** — e.g. `chair_wood`. If `.voxel` suffix is included, strip it.
- **`--no-build`** — skip build step (use when binary is already fresh)
- **`--port <n>`** — override port (default: 8090)

Confirm: `name=<name>, port=<port>, build=<yes/no>`

---

## Phase 1 — Safe Launch

Follow the Engine Lifecycle Rules above exactly.

Announce when each step completes:
- `stop_engine: done` (or `engine was already stopped`)
- `build_project: clean` (or error → stop)
- `launch_engine: PID <n>`
- `api_responsive: true`

---

## Phase 2 — Seat Geometry Discovery

Query the placed object to find the seat anchor position:

```
GET /api/interaction/ie/state
```

Also record the chair front-edge Z from the .voxel file. For `chair_wood`, the seat
footprint is Z: 13.0 → 13.667 (local 0 → 0.667 cubes). Front edge = seatAnchor.z + 0.334.

Print:
```
Seat anchor:  x=<X> y=<Y> z=<Z>
Chair front edge Z: <Z + 0.334>
Clip-through threshold: character.z < <chair_front_Z>
```

---

## Phase 3 — Full Animation Sweep

Start the IE preview:
```
POST /api/interaction/ie/sit   {}
```

Verify `state: sitting_down` in response.

Then run this sweep — 3 clips × 5 time points = 15 frames:

| Clip name      | Times (normalized) |
|----------------|--------------------|
| stand_to_sit   | 0.0, 0.25, 0.5, 0.75, 1.0 |
| sitting_idle   | 0.0, 0.25, 0.5, 0.75, 1.0 |
| sit_to_stand   | 0.0, 0.25, 0.5, 0.75, 1.0 |

For each frame:

### 3a. Seek
```
POST /api/interaction/ie/seek   {"clip_name": "<clip>", "normalized_time": <t>}
```

Record the returned fields:
- `world_pos.z` — where the character's feet are
- `seat_anchor.z` — the fixed anchor
- `paused: true` — MUST be true, otherwise the seek failed

### 3b. Wait
Sleep 0.3 seconds (let the frame render at the seeked pose).

### 3c. Screenshot
Call `orbit_screenshots`:
```
x: <seatAnchor.x + 0.5>
y: <seatAnchor.y + 0.5>
z: <seatAnchor.z + 1.5>
radius: 2
port: <port>
views: ["east", "south"]
```
(Orbit centered slightly above and in front of the seat so character and chair are both in frame.)

### 3d. Analyze — be honest

After each screenshot, output:

```
**Frame: <clip> t=<normalized_time>**
- world_pos.z = <value>  |  chair_front_z = <threshold>
- CLIP-THROUGH? <YES if world_pos.z < threshold, else NO>
- East view: [describe exactly what you see — standing/seated/mid-rise, chair visible/not, body inside chair/in front]
- South view: [same]
```

Do NOT write PASS unless you see the character clearly outside the chair geometry.
If the screenshots look identical to each other, say so — that is a bug, not a PASS.

---

## Phase 4 — Cleanup

Call `stop_engine` after the sweep completes.

---

## Phase 5 — Report

```
## Sit-Validate Report: <name>

**Build:** clean | failed
**Engine:** launched fresh (PID <n>)

### Seat Geometry
- Anchor: (<X>, <Y>, <Z>)
- Chair front edge Z: <value>

### Frame Results (15 frames)

| Clip          | t    | world_pos.z | Clip-Through? | Visual |
|---------------|------|-------------|---------------|--------|
| stand_to_sit  | 0.0  | <z>         | YES/NO        | [1-line] |
| stand_to_sit  | 0.25 | ...         | ...           | ... |
| ...           | ...  | ...         | ...           | ... |

### Screenshots
[Show all 30 images inline — east+south for each of the 15 frames]

### Verdict
PASS — no clip-through at any frame, screenshots show distinct poses
PARTIAL — clip-through detected at: [list frames]
FAIL — seek system not working (screenshots all identical, or paused=false)

### Issues Found
[List specific frames with problems, what the screenshot shows, what the numbers say]
```

# /model — Claude-authored voxel asset

You are the spatial reasoning engine. Your job is to write `.voxel` coordinate files directly — no blocksmith, no LLM delegation. You see the screenshots, you fix the geometry, you iterate until the asset is correct.

The user's request is: $ARGUMENTS

---

## Phase 0 — Parse Arguments

Extract:
- **Name** — snake_case. Derive from the first 2-3 words if not given.
- **Prompt** — full description.
- **`--rounds <n>`** — max refinement rounds (default: 4)

Announce: name, prompt, rounds.

---

## Phase 1 — Plan Geometry

Before writing a single coordinate, think on paper. Output a geometry plan:

```
**Geometry Plan: <name>**

Concept: [1-2 sentence description of what you're building]

Reference scale (Y=0 is floor):
- Seat top: Y=___
- Seat bottom: Y=___
- Armrest height: Y=___
- Backrest top: Y=___
- Total height: ___

Parts list:
1. [Part name]: [type: Cube/Subcube] at X=[range], Y=[range], Z=[range] — [material]
2. ...

Facing: front = +Z (toward viewer), back = -Z
Center: X=0, Z=0 (template origin)
```

Rules:
- All coordinates are relative to template origin (0,0,0)
- Template is spawned at world pos ~16,16,16 — the local (0,0,0) maps there
- Y=0 is ground level inside the template
- A humanoid character is ~1.8 cubes tall — design to that scale
- Seat height for a chair/throne: Y≈0.6–0.7 (top surface)
- Subcubes are 1/3 cube. Microcubes are 1/9 cube.
- Use whole integer cube coords for main structure; fractional positions (expressed as thirds) for detail

---

## Phase 2 — Write the .voxel File

Write `resources/templates/<name>.voxel` using the Write tool.

### .voxel file format

```
# Comment lines start with #
# Cubes: C cx cy cz Material
# Subcubes: S cx cy cz sx sy sz Material
#   where sx,sy,sz are subcube grid position (0-2 each, within the parent cube at cx,cy,cz)
# Microcubes: M cx cy cz mx my mz Material
#   where mx,my,mz are microcube grid position (0-8 each)
```

**Coordinate rules:**
- cx, cy, cz — integer cube coordinates (can be negative)
- For subcubes, the 6 numbers after the material letter are: cube_x cube_y cube_z sub_x sub_y sub_z
- For microcubes: cube_x cube_y cube_z micro_x micro_y micro_z
- All on one line, space-separated

**Example (a simple 2-wide bench):**
```
# Bench seat — 2 cubes wide, 1 deep, 1/3 thick
S -1 0 0  0 2 0  Wood
S 0  0 0  0 2 0  Wood
# Left leg
S -1 -1 0  0 0 0  Wood
S -1 -1 0  0 1 0  Wood
# Right leg
S 0  -1 0  0 0 0  Wood
S 0  -1 0  0 1 0  Wood
```

After writing, announce:
```
**File written:** `resources/templates/<name>.voxel`
Primitives: [count C lines] C + [count S lines] S + [count M lines] M = [total]
```

---

## Phase 3 — Launch Asset Editor

Call `launch_asset_editor`:
```
template_path: resources/templates/<name>.voxel
port: 8091
config: Debug
```

Report the PID.

---

## Phase 4 — Visual Critique Loop

For each round from 1 to max_rounds:

### 4a. Announce round
```
---
**Round N/max — Capturing screenshots...**
```

### 4b. Take orbit screenshots
Call `orbit_screenshots`:
```
x: 16, y: 17, z: 16
radius: 4
port: 8091
views: ["north", "south", "east", "west", "top", "iso"]
```

### 4c. Analyze — your own visual critique
Look at all 6 views and output:

```
**Visual Analysis — Round N**

Overall impression: [1 sentence]

Issues found:
- [Specific geometry problem and where it is visible]
- ...

Scale check (vs. humanoid ~1.8 cubes tall):
- [Seat height OK / too low / too high — measured from top view]
- [Width OK / too narrow / too wide]
- [Total height OK]

Material check:
- [Pink voxels = missing material — list any]
- [Materials look correct / issues]

Score: N/10
```

### 4d. Check threshold
- Score ≥ 8: output `**PASS — quality threshold met.**` and exit loop.
- Round == max_rounds: output `**Max rounds reached.**` and exit loop.

### 4e. Fix geometry

Output a fix plan:
```
**Fixing — Round N**
Changes:
- [Specific fix: add/remove/move which primitive, where, why]
- ...
```

Edit the .voxel file using Edit tool (preferred for small changes) or rewrite with Write tool (for structural changes). Be surgical — only touch what needs fixing.

After editing, call `reload_asset_editor`:
```
port: 8091
```

Output: `**Reloaded — round N version live.**`

Then continue to next round.

---

## Phase 5 — Final Report

```
## Generated Asset: <name>

**File:** `resources/templates/<name>.voxel`
**Rounds used:** N / max
**Final quality:** PASS / REFINED / PARTIAL

### Geometry
- Primitives: NC C + NS S + NM M = total
- Bounds: W × H × D cubes
- Facing: +Z (front)

### Visual Evidence
[Show the 6 final orbit screenshots inline]

### Pipeline Notes
[Per-round: score + key changes made]
```

---

## Error Handling

- **Asset editor already running**: Call `close_asset_editor` first, then `launch_asset_editor`.
- **reload_asset_editor returns error "Not in asset editor mode"**: The editor isn't running — call `launch_asset_editor` to start it.
- **Pink voxels**: Material name is wrong. Check spelling against the CLAUDE.md material table (Wood, Metal, Glass, Stone, Gold, etc. — case-sensitive).
- **Blank screenshots**: Wait 2s and call `orbit_screenshots` again.
- **Template not visible**: Wrong coordinates — everything far from origin won't be visible at radius=4. Recenter to X=0..2, Y=0..3, Z=0..2.

---

## Notes

- The asset editor runs on port 8091.
- `reload_asset_editor` hot-reloads without killing the process — much faster than stop/start.
- Templates are saved in `resources/templates/` and auto-cataloged.
- You have full spatial reasoning — use it. Think in 3D before writing coordinates.
- Reference character (H key in editor) is ~1.8 cubes tall; the seat should reach roughly mid-thigh (~0.65 cubes).

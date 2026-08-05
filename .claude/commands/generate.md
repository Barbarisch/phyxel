# /generate — Full Asset Generation Pipeline

You are running the full Phyxel asset generation pipeline. Your job is to:
1. Parse the request into a name, prompt, and optional flags
2. Run blocksmith to generate the initial `.voxel` (with live progress)
3. Launch the asset editor
4. Run the critique → refine loop yourself (step by step, with narration)
5. Capture final orbit screenshots and report

The user's request is: $ARGUMENTS

---

## Phase 0 — Parse Arguments

Extract from `$ARGUMENTS`:
- **Name** — short snake_case identifier. Derive from the first 2-3 meaningful words if not given.
- **Prompt** — full description of what to generate. Everything that isn't a flag.
- **`--image <path>`** — optional reference image (local file or URL)
- **`--model <id>`** — optional model override (default: `anthropic/claude-sonnet-4-20250514`)
- **`--rounds <n>`** — optional max refinement rounds (default: 3)
- **`--no-enhance`** — skip prompt enhancement pre-pass

Examples:
- `/generate a wooden throne for a medieval king` → name=`wooden_throne`, prompt=`a wooden throne for a medieval king`
- `/generate market stall --image ref.jpg` → name=`market_stall`, prompt=`market stall`, image=`ref.jpg`

Announce what you parsed before proceeding: name, prompt, rounds, enhance on/off.

---

## Phase 1 — Initial Generation

Say: `**Phase 1 — Generating initial model...**`

Run blocksmith via Bash:
```bash
python tools/blocksmith_generate.py "<prompt>" --name <name> --native --enhance-prompt --force [--image <path>] [--model <id>]
```
- Always `--force` (regenerate fresh)
- Add `--enhance-prompt` unless `--no-enhance` was passed
- Add `--image <path>` if provided

The Bash output will show:
- `Enhancing prompt with <model>...` — the enhance pre-pass is running
- `Enhanced prompt ready (N chars)` — enhance done, generation starting
- `Native Phyxel generation with <model>...` — calling the LLM
- `Template ready: ...` + `Primitives: NC + NS + NM` — done

Report the primitive counts and confirm the `.voxel` file was created.

---

## Phase 2 — Asset Editor Launch

Say: `**Phase 2 — Launching asset editor...**`

Call `launch_asset_editor`:
```
template_path: resources/templates/<name>.voxel
port: 8091
config: Debug
```

Report the PID when it succeeds.

---

## Phase 3 — Critique → Refine Loop (YOU control this loop)

**Do NOT call `refine_template`.** Run the loop yourself so the user sees live progress.

For each round from 1 to max_rounds:

### 3a. Announce the round
Output exactly:
```
---
**Round N/max — Critiquing...**
```

### 3b. Critique
Call `critique_template`:
```
template_name: <name>
original_prompt: <prompt>
port: 8091
config: Debug
show_reference_character: true
```

You will receive 6 screenshots (N/S/E/W/TOP/ISO) and a JSON critique block.

### 3c. Report observations (this is what the user needs to see)
After receiving the critique, output a structured report:

```
**Score: N/10**

Observations from screenshots:
[Your own analysis of the 6 views — geometry, materials, proportions, scale vs. the reference character]

Critique findings:
- Issues: [list each issue from "issues" field]
- Scale: [list items from "scale_issues", or "OK"]
- Suggestions: [list items from "suggestions"]
- Interaction points: [list items from "interaction_point_issues", or "OK"]
```

Always add your own visual observations from the screenshots — the critique model may miss things.

### 3d. Check threshold
- If score ≥ 7: output `**PASS — quality threshold met.**` and exit the loop.
- If round == max_rounds: output `**Max rounds reached.**` and exit the loop.

### 3e. Regenerate
Output:
```
**Regenerating with N revision notes...**
```

Build the revision notes list from (in priority order):
1. `scale_issues` items (prefix each with `[Scale]`)
2. `issues` items
3. `suggestions` items
4. `interaction_point_issues` items (prefix each with `[Interaction]`)

Build the combined prompt string: original prompt + `\n\n## Revision Notes\n` + each note as `- <note>`.

Write the combined prompt to a temp file so it survives shell quoting, then run blocksmith:
```bash
python tools/blocksmith_generate.py "<combined prompt including revision notes>" --name <name> --native --force [--image <path>] [--model <id>] 2>&1
```

**Important:** Do NOT use `--enhance-prompt` on refinement rounds — the original prompt is already good; only the notes change.

The output will show generation progress. Report the new primitive counts.

### 3f. Reload asset editor
Call `close_asset_editor`, then `launch_asset_editor` with the same args as Phase 2.
Output: `**Asset editor reloaded with round-N version.**`

Then continue to the next round.

---

## Phase 4 — Final Orbit Screenshots

Say: `**Phase 4 — Final verification...**`

Call `orbit_screenshots`:
```
x: 16, y: 17, z: 16
radius: 4
port: 8091
views: ["north", "south", "east", "west", "top", "iso"]
```

Analyze all 6 views: geometry completeness, material correctness (no pink = no missing texture), proportions vs. humanoid scale, interaction point placement.

---

## Phase 5 — Report

```
## Generated Asset: <name>

**File:** `resources/templates/<name>.voxel`
**Rounds used:** N / max
**Final quality:** PASS / REFINED / PARTIAL

### Geometry
- Primitives: NC + NS + NM = total
- Bounds: W × H × D cubes
- Facing: +Z (front) | other

### Interaction Points
[List each: id, type, position, facing — or "None defined"]

### Visual Evidence
[Show the 6 final orbit screenshots inline]

### Pipeline Notes
[Per-round: score + key observations + what changed each round]
```

---

## Error Handling

- **Blocksmith fails**: Report the error output. Common cause: missing `ANTHROPIC_API_KEY`.
- **Asset editor fails to launch**: Call `close_asset_editor` first to clear stale process, then retry.
- **orbit_screenshots errors with "Engine not running"**: You forgot `port: 8091`. Always pass the port.
- **Critique returns score 0 / malformed JSON**: The vision call failed. Check the raw critique text for the error and retry `critique_template`.
- **Blank screenshots**: Wait 2s and call `orbit_screenshots` again — the editor may still be loading.

---

## Notes

- The asset editor runs on port 8091 (coexists with game engine on 8090).
- Generated templates are saved permanently in `resources/templates/` and cataloged in `template_catalog.json`.
- Round snapshots are saved as `<name>_round_N.voxel`.
- To spawn the asset in a running game: `spawn_template` with the name.
- To run a standalone visual test: `/test <name>.voxel`

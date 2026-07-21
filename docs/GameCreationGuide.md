# Game Creation Guide — AI-Driven Workflow

This guide walks through creating a complete voxel game using the Phyxel engine from an AI interface (Copilot, Claude Code, or any MCP-compatible agent).

## Prerequisites

- Phyxel engine built (`./build_and_test.ps1 -Config Debug`)
- Python environment with `pip install mcp httpx`
- For Copilot: the `@game-creator` agent is available in `.github/agents/`
- For Claude Code: MCP server configured (see CLAUDE.md)

## Quick Start (One-Shot Game)

The fastest path is a single `load_game_definition` call with a complete JSON payload:

```
@game-creator Create a medieval village game with mountains terrain,
a blacksmith NPC, a merchant, and a main quest about finding a lost sword.
```

The agent will:
1. Verify the engine is running
2. Build a game definition JSON
3. Validate it
4. Load it into the engine
5. Screenshot the result

## Critical: Editor vs. Project Mode

The engine has two modes. **Using the wrong mode is the #1 source of confusion.**

| Mode | Launch Command | Who Uses It | World Saved To |
|------|---------------|-------------|---------------|
| **Editor mode** | `.\phyxel.exe` | Engine development, testing | `<engine>/worlds/default.db` |
| **Project mode** | `.\phyxel.exe --project <path>` | **Game development** | `<project>/worlds/default.db` |

**When developing a game, ALWAYS use `--project` mode:**
```powershell
.\phyxel.exe --project C:\Users\jack\Documents\PhyxelProjects\MyGame
```

Project mode:
- Loads world from the **project's** `worlds/default.db`
- Auto-loads game definition from the **project's** `game.json`
- `save_world` writes back to the **project's** database
- `build_game` / `run_game` API endpoints build and launch the standalone game
- Window title shows the project name

**Without `--project`, all MCP changes go to the engine's own world — NOT your game.**

## Step-by-Step Workflow

### Step 1: Start the Engine

```
Check if the engine is running. If not, build and launch it.
```

Tools used: `engine_running` → `build_project` → `launch_engine`

### Step 2: Design Your World

Choose a world type and chunk range:

| World Type | Good For |
|------------|----------|
| Flat | Indoor scenes, custom builds, arenas |
| Perlin | Natural outdoor terrain, fields |
| Mountains | Epic landscapes, mountain villages |
| Caves | Dungeons, underground adventures |
| City | Urban environments, dystopian settings |

Chunk range determines world size. Each chunk is 32×32×32 blocks:
- `from: {-1,0,-1}` to `{1,0,1}` = 3×1×3 = 9 chunks (small world)
- `from: {-2,0,-2}` to `{2,0,2}` = 5×1×5 = 25 chunks (medium world)
- `from: {-3,0,-3}` to `{3,0,3}` = 7×1×7 = 49 chunks (large world)

### Step 3: Add Structures

Structures are placed after terrain generation. Two types — **use fills for shells/terrain,
templates for everything detailed** (furniture, props, decor: `search_templates` the catalog
FIRST; ~50 purpose-built assets with subcube/microcube detail exist — don't hand-build a
chair out of fill boxes).

**Fill regions** — solid or hollow boxes of a material:
```json
{"type": "fill", "from": {"x":0,"y":16,"z":0}, "to": {"x":10,"y":20,"z":10}, "material": "Stone"}
```
Fills only place into **empty air**; voxels already occupied by terrain or earlier fills are
skipped (the loader logs per-fill `placed/failed` counts). Add `"replace": true` to overwrite
occupied voxels (e.g. carving windows into an existing wall with Glass).

**Templates** — pre-built objects:
```json
{"type": "template", "name": "tree.voxel", "position": {"x":15,"y":17,"z":15}}
```

**A typical interior mixes both** — fills for the shell, templates for the furnishing:
```json
"structures": [
  {"type": "fill", "from": {"x":10,"y":16,"z":10}, "to": {"x":26,"y":16,"z":26}, "material": "Wood"},
  {"type": "fill", "from": {"x":10,"y":17,"z":10}, "to": {"x":26,"y":22,"z":26}, "material": "Stone", "hollow": true},
  {"type": "fill", "from": {"x":14,"y":18,"z":10}, "to": {"x":16,"y":20,"z":10}, "material": "Glass", "replace": true},
  {"type": "template", "name": "tavern_bar.voxel",  "position": {"x":12,"y":17,"z":20}},
  {"type": "template", "name": "tavern_table.voxel","position": {"x":18,"y":17,"z":14}},
  {"type": "template", "name": "chair_wood.voxel",  "position": {"x":20,"y":17,"z":14}},
  {"type": "template", "name": "fireplace.voxel",   "position": {"x":24,"y":17,"z":24}}
]
```

Browse the catalog with `search_templates` / `list_templates` (e.g. the tavern set:
`tavern_bar`, `tavern_table`, `chair_wood`, `stool`, `bench_wood`, `barrel`, `crate_wood`,
`fireplace`, `candle_holder`, `lantern`, `torch_wall`).

### Step 4: Create NPCs

Each NPC needs:
- **Unique name** (no duplicates)
- **Position** above terrain surface (Y = terrain height + 2)
- **Behavior**: `idle` (stays put), `patrol` (walks waypoints), `wander` (random movement)
- **Dialogue** (optional): branching conversation tree
- **Story character** (optional): personality traits, goals, faction

**Dialogue → gameplay state.** Conversation outcomes are machine-readable, so patterns like
"convince 3 NPCs, then the win unlocks" are fully declarative:

```json
"nodes": [
  { "id": "give_secret", "speaker": "Greta",
    "text": "...fine. The key is under the cellar hatch.",
    "actions": [
      {"type": "set_story_variable", "name": "greta_secret", "value": true},
      {"type": "complete_objective", "id": "obj_greta"}
    ],
    "nextNodeId": "" },
  { "id": "hub", "speaker": "Greta", "text": "What do you want?",
    "choices": [
      {"text": "Tell me the secret.", "targetNodeId": "give_secret",
       "condition": {"variable": "greta_trust", "equals": true}},
      {"text": "Nothing, sorry.", "targetNodeId": ""}
    ]}
]
```

- **Node `actions`** run when the node is shown — same vocabulary as trigger `then` entries:
  `set_story_variable {name,value}`, `complete_objective {id}`, `fail_objective {id}`,
  `transition_scene {target}`, `quit_game`.
- **Choice `condition`** hides a choice until earned: `equals` / `not_equals` / `gte` / `lte` /
  `"exists": true` against a story variable. Missing variables fail closed (choice hidden).
- Every node shown fires a **`dialogue_node_reached`** gameplay event `{tree, node, speaker}` —
  triggers can gate on it: `{"when": {"event":"dialogue_node_reached","node":"give_secret"},
  "then": [...]}`. Extra `when` keys must match the event payload.

### Step 5: Write the Story

Story arcs contain beats — key moments the player progresses through:
- **Hard beats**: Must happen in order
- **Soft beats**: Should happen but can be skipped
- **Optional beats**: Enrichment, not required

Constraint modes:
- **Strict**: Beats must happen in exact order
- **Guided**: General order with flexibility
- **Open**: Any order

### Step 6: Set Player & Camera

- **Player type**: `animated` (voxel character) — the only functional type. `physics` and `spider`
  are accepted but silently aliased to `animated` (the Bullet-era `PhysicsCharacter`/
  `SpiderCharacter` ragdoll classes were removed with the Bullet Physics removal); use `animated`
  directly.
- **Camera**: Position 30-50 units from the action, pitch -25° to -35° for overview

### Step 7: Load & Verify

1. Validate: `validate_game_definition` catches schema errors before loading
2. Load: `load_game_definition` creates everything atomically
3. Screenshot: `screenshot` lets you see the result
4. Iterate: use individual tools to refine

### Step 8: Add Game Mechanics

After your world, NPCs, and story are in place, layer on gameplay systems:

**Player Health & Respawn:**
- Player has a health system. When health reaches 0, a death overlay appears and a respawn timer starts.
- `set_spawn_point` — Set where the player respawns after death
- `damage_player` / `heal_player` / `kill_player` / `revive_player` — Manage player health
- `get_player_health` / `get_respawn_state` — Inspect current state

**Objectives & Progression:**
- `add_objective` — Track quests with title, description, priority, category
- `complete_objective` / `fail_objective` — Update objective status
- Active objectives appear in the default HUD's **Objectives panel (top-left)**, `[x]`/`[ ]`
  marked and priority-sorted; the panel auto-hides when empty
- The whole game HUD is **data-driven** on the custom-Vulkan UISystem (health, hotbar, objectives,
  dialogue, combat) and customizable via a `game.json "hud"` block — see `docs/HudSystem.md`

**Background Music:**
- `control_music` with `action: "add_track"` — Add music files to the playlist
- `control_music` with `action: "play"` — Start playback (loops automatically)
- Supports Sequential and Shuffle modes, volume control (0.0–1.0)
- Music persists across game sessions via `save_player` / `load_player`

**Day/Night Cycle:**
- `set_day_night` — Configure time of day, day length, time speed
- Dawn (6:00), Day (8:00), Dusk (18:00), Night (20:00)
- Ambient and sun colors animate automatically

**Combat & Equipment:**
- `equip_item` / `unequip_item` — Manage NPC/player equipment (6 slots)
- `attack` — Sphere+cone hit detection with knockback
- `damage_entity` / `heal_entity` — Apply damage/healing to any entity

**Crafting:**
- Not currently exposed over MCP/HTTP — `CraftingSystem` exists as a C++ gameplay class, but
  `add_recipe`/`craft_item` tools don't exist in `phyxel_mcp_server.py` or `EngineAPIServer.cpp`
  as of this writing.

**Pause System:**
- ESC key toggles pause (freezes world simulation, shows pause menu)
- `toggle_pause` / `get_pause_state` — Control via MCP

**Camera Mode & Control Scheme** (see `docs/CameraControlSystem.md` for the full design):
- The `camera` block accepts `"mode"`: `"first_person"` | `"third_person"` |
  `"overhead"` | `"isometric"` | `"free"`. Overhead and isometric are
  **orthographic** rigs that follow the player (overhead = straight-down map
  view; isometric = classic fixed ~35° 3/4 view).
- `"controlScheme"` picks how input drives the character:
  `"fps"` (mouse-look turns the view, W/S walk where you look, A/D strafe —
  default) | `"tank"` (A/D turn the body, W/S forward/back, RMB orbits the
  camera).
  ```json
  "camera": { "position": {"x":2,"y":24,"z":-6}, "yaw": 90, "pitch": -25,
              "mode": "first_person", "controlScheme": "fps" }
  ```
- Works per scene in multi-scene games (each scene's `definition.camera`) — the
  engine-side `GameShell` re-resolves the rig/scheme on every scene transition,
  so e.g. level1 can be first-person and level2 isometric. Defaults when
  unauthored: first-person + fps.
- `set_camera` (MCP/HTTP) accepts the same `mode` values plus `control_scheme`
  for live switching during a dev session; the editor's Camera panel has Rig and
  Scheme combos for the same thing.

**Timer Countdown HUD:**
- Add `"hud": true` (and optional `"hudLabel"`) to a timer trigger to show a large
  top-center countdown (turns red under 10 s):
  ```json
  { "id": "timeout", "when": {"event": "timer", "seconds": 60},
    "hud": true, "hudLabel": "Escape the maze!",
    "then": [{"type": "transition_scene", "target": "game_over"}], "once": true }
  ```
- Renders in the editor preview and in standalones; `list_triggers` reports the
  remaining seconds for hud timers.

**Dynamic Menu Text:**
- Menu-scene labels/buttons interpolate `{{token}}` per frame:
  - `{{playtime}}` — unpaused gameplay clock, formatted `M:SS.s` (speedrun time
    on a credits screen)
  - `{{story.<var>}}` — a StoryEngine world variable (set via `story_set_variable`
    or story beats), e.g. `"text": "Score: {{story.score}}"`
- Unknown tokens render literally.

### Step 9: Save

- `save_world` persists to `worlds/default.db`
- `save_player` persists player state (camera, health, spawn point, inventory) to SQLite
- `export_game_definition` exports as reusable JSON
- `save_template` saves structures as reusable templates

### Step 10: Package for Distribution

There are two paths: **editor preview** (quick iteration) and **standalone game** (distributable).

#### Path A: Editor Preview (Quick Iteration)

Use the editor's HTTP API to preview your game definition in the running engine.
This is NOT a standalone game — it requires the engine to be running.

```
1. Engine running with world loaded from load_game_definition
2. save_world (persists current state to worlds/default.db)
3. export_game_definition (exports as reusable JSON)
4. screenshot / iterate as needed
```

#### Path B: Standalone Game (Recommended for Distribution)

Creates a self-contained game project with its own C++ source and executable.

```powershell
# ── Step 1: Scaffold the project ──────────────────────────
python tools/create_project.py MyGame --game-definition samples/game_definitions/my_game.json
# Output: Documents/PhyxelProjects/MyGame/

# ── Step 2: Copy engine assets to the project ─────────────
# The scaffolder creates the project structure but does NOT copy
# compiled shaders or textures. You must copy these manually:
$proj = "$env:USERPROFILE\Documents\PhyxelProjects\MyGame"
Copy-Item "shaders\*.spv" "$proj\shaders\" -Force
Copy-Item "resources\textures\cube_atlas.png" "$proj\resources\textures\" -Force
Copy-Item "resources\textures\cube_atlas.json" "$proj\resources\textures\" -Force
# Copy animation files referenced by your NPCs:
Copy-Item "character.anim" "$proj\" -Force

# ── Step 3: Pre-bake the world ────────────────────────────
# The engine must be running with your game definition loaded.
# Save all chunks, then copy the database to the project:
curl.exe -s -X POST http://localhost:8090/api/world/save -H "Content-Type: application/json" -d '{"all": true}'
Copy-Item "worlds\default.db" "$proj\worlds\default.db" -Force

# ── Step 4: Build the game ────────────────────────────────
# CMake is not in system PATH on MSVC. Add it first:
$env:PATH += ";C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
Push-Location $proj
cmake -B build -S .
cmake --build build --config Debug
# Output: build/Debug/MyGame.exe (with all assets copied by CMake post-build)
Pop-Location

# ── Step 5: Package ──────────────────────────────────────
python tools/package_game.py MyGame --project-dir $proj
```

> **Important notes:**
> - `load_game_definition` is an **editor-only preview** — it does not create a project
> - `save_world` saves to the engine's current database path, not a custom one — you must copy the file
> - PowerShell aliases `curl` to `Invoke-WebRequest` — use `curl.exe` for raw HTTP
> - The scaffolder generates empty `shaders/` and `resources/` dirs — you must populate them

**Output structure** (`Documents/PhyxelProjects/MyGame/`):
```
MyGame/
├── MyGame.exe              # Standalone game executable
├── MyGame.cpp/.h           # Generated C++ source (GameCallbacks)
├── main.cpp                # Entry point
├── CMakeLists.txt          # Build config (links phyxel_core)
├── game.json               # NPCs, story, camera (no world gen)
├── engine.json             # Engine config (resolution, title)
├── character.anim          # Animation files (if NPCs use them)
├── shaders/                # Compiled SPIR-V shaders
├── resources/
│   └── textures/           # Texture atlas (cube_atlas.png/.json)
├── worlds/
│   └── default.db          # Pre-baked terrain (instant startup)
└── build/
    └── Debug/
        └── MyGame.exe      # Built executable + all runtime assets
```

**Package options:**
- `--project-dir` — use a scaffolded game project (own exe, no Python/MCP)
- `--prebake-world` — save engine world to SQLite first
- `--world-db` — supply an existing pre-baked database
- `--all-resources` — include everything (not just what the game references)
- `--config Release` — use Release build for final distribution
- `--output /path` — custom output directory

## Example Game Definitions

### Simple Arena

```json
{
  "name": "Arena",
  "world": {"type": "Flat", "from": {"x":0,"y":0,"z":0}, "to": {"x":0,"y":0,"z":0}},
  "structures": [
    {"type": "fill", "from": {"x":4,"y":16,"z":4}, "to": {"x":28,"y":16,"z":28}, "material": "Stone"},
    {"type": "fill", "from": {"x":4,"y":17,"z":4}, "to": {"x":28,"y":20,"z":28}, "material": "Stone", "hollow": true}
  ],
  "player": {"type": "animated", "position": {"x":16,"y":18,"z":16}},
  "camera": {"position": {"x":16,"y":35,"z":-5}, "yaw": 0, "pitch": -35}
}
```

### RPG Village

```json
{
  "name": "Mountain Village",
  "world": {"type": "Mountains", "seed": 42, "from": {"x":-1,"y":0,"z":-1}, "to": {"x":1,"y":0,"z":1}},
  "structures": [
    {"type": "fill", "from": {"x":10,"y":20,"z":10}, "to": {"x":22,"y":20,"z":22}, "material": "Stone"},
    {"type": "fill", "from": {"x":12,"y":21,"z":12}, "to": {"x":20,"y":25,"z":20}, "material": "Wood", "hollow": true},
    {"type": "template", "name": "tree.voxel", "position": {"x":25,"y":21,"z":15}}
  ],
  "player": {"type": "animated", "position": {"x":16,"y":28,"z":16}},
  "camera": {"position": {"x":50,"y":50,"z":50}, "yaw": -135, "pitch": -30},
  "npcs": [
    {
      "name": "Elder",
      "position": {"x":16,"y":22,"z":16},
      "behavior": "idle",
      "dialogue": {
        "id": "elder_talk", "startNodeId": "greet",
        "nodes": [
          {"id": "greet", "speaker": "Elder", "text": "Welcome, traveler. Our village needs your help."},
          {"id": "quest", "speaker": "Elder", "text": "A dragon has been spotted in the mountains. Will you investigate?"}
        ]
      },
      "storyCharacter": {
        "id": "elder", "faction": "village", "agencyLevel": 1,
        "traits": {"openness": 0.7, "conscientiousness": 0.9},
        "goals": [{"id": "protect_village", "description": "Keep the village safe", "priority": 0.95}]
      }
    }
  ],
  "story": {
    "arcs": [{
      "id": "dragon_quest", "name": "The Dragon Threat", "constraintMode": "Guided",
      "beats": [
        {"id": "arrive", "description": "Arrive at the village", "type": "Hard"},
        {"id": "talk_elder", "description": "Speak with the Elder", "type": "Hard", "requiredCharacters": "elder"},
        {"id": "find_dragon", "description": "Locate the dragon in the mountains", "type": "Hard"},
        {"id": "resolve", "description": "Deal with the dragon", "type": "Hard"}
      ]
    }]
  }
}
```

## Player Controls (Standalone Games)

Standalone games use the standard `GameSettings::defaultKeybindings()`:

| Key | Action |
|-----|--------|
| W/A/S/D | Move forward/left/backward/right |
| Space | Jump |
| Shift | Sprint |
| Ctrl | Crouch |
| E | Interact (talk to NPC) |
| Left Click | Attack |
| V | Toggle camera mode (1st/3rd/free) |
| Tab | Inventory |
| ESC | Pause menu / back |
| C | Place cube |

`GameSettings::defaultKeybindings()` also registers an `Attack → F` binding, but no code path
currently reads that action — attack is hardcoded to Left Mouse Button in both `FpsScheme` and
`TankScheme` (`engine/include/input/ControlScheme.h`). Likewise `Tab`/`ToggleInventory` and
`C`/`PlaceCube` are registered as rebindable actions but no `isActionPressed()` call consumes them
in the standalone `GameShell` path yet — treat those two as configured-but-not-yet-wired rather
than confirmed functional.

These can be rebound in the Settings → Keybindings screen (once wired to gameplay).

> **Editor-only controls** (not available in standalone games):
> K = toggle character, T = spawn template, F1-F7 = debug overlays

## Iterating on a Live Game

After loading, use these tools to refine:

| Task | Tool |
|------|------|
| Add terrain | `fill_region` with material |
| Remove terrain | `clear_region` |
| Add detail blocks | `place_voxel` or `place_voxels_batch` |
| Add trees/objects | `spawn_template` |
| Add NPCs | `create_game_npc` |
| Move camera | `set_camera` |
| Check state | `get_world_state`, `list_npcs`, `screenshot` |
| Undo changes | `create_snapshot` before editing, `restore_snapshot` to revert |
| Save progress | `save_world` |
| Export for reuse | `export_game_definition` |

## Multi-Scene Games

For games with multiple levels, scenes, or areas — see [docs/SceneSystem.md](SceneSystem.md) for full details.

### Quick Start

1. **Design your game definition** with a `"scenes"` array instead of a top-level `"world"` key
2. **Each scene** gets its own `id`, `worldDatabase`, and `definition` (same schema as single-scene games)
3. **`startScene`** sets which scene loads first
4. **`playerDefaults`** and **`globalStory`** sit at the top level and persist across transitions

### Pre-Baking Scene Worlds

Each scene needs a SQLite database with pre-generated terrain. Build them one at a time using the engine:

```
# 1. Launch the engine
launch_engine

# 2. For each scene, load just that scene's definition to generate terrain
load_game_definition  ←  pass the scene's world/structures config

# 3. Build terrain with fill_region, place_voxel, spawn_template, etc.

# 4. Save the world to the scene's database
save_world  ←  saves to worlds/<scene_id>.db

# 5. Repeat for each scene
```

Alternatively, if a scene's definition has a `"world"` key with generation config (Perlin, Flat, Mountains, etc.), the engine generates terrain on first load — no pre-baking needed.

### Scaffolding a Multi-Scene Project

```powershell
# Scaffold with a multi-scene game definition
python tools/create_project.py MyGame --game-definition multi_scene_game.json
```

The generated code automatically:
- Detects `"scenes"` in the game definition
- Creates a `SceneManager` and loads the manifest
- Wires `SceneCallbacks` so `sceneType: "menu"` scenes render via the data-driven `UISystem`
  (custom-Vulkan, no ImGui) and hand control to gameplay when a world scene becomes ready (see "Menus" below)

### Menus & Win/Lose Screens — Two Patterns (don't mix them)

A standalone game has **two** ways to show menus, and they must not be combined or
they draw on top of each other:

1. **Built-in shell (recommended for simple games).** The scaffolded standalone
   ships a `ScreenState` shell — Intro → Main Menu → Playing → Pause, plus Victory
   and Credits screens (`renderVictoryScreen` / `renderCreditsScreen`). With this
   pattern:
   - Set `startScene` to a **world** scene (not a menu scene).
   - End the game with triggers that call **`show_victory`** / **`show_credits`**
     (NOT `transition_scene` to a menu scene):
     ```json
     "triggers": [
       { "id": "win", "when": { "event": "player_jumped" },
         "then": [ { "type": "show_victory" } ], "once": true }
     ]
     ```
   - The built-in Main Menu's "New Game" goes straight to gameplay on the
     `startScene` world.

2. **Authored `menu` scenes (full visual control).** Define `sceneType: "menu"`
   scenes (intro / main_menu / credits) with a `menuLayout`, and drive flow with
   `transition_scene` actions on the buttons. The generated standalone now
   **detects an active menu scene and renders it instead of the built-in shell**
   (via the data-driven `UISystem` — `loadMenuInto`; no ImGui), so the two no longer collide. Use `transition_scene`
   to a world scene from the main menu's "New Game" button, and `transition_scene`
   to a credits **menu** scene to end the game.

Pick one. If your `startScene` is a menu scene, the engine uses pattern 2 and the
built-in shell stays hidden; if it is a world scene, the built-in shell drives.

### Packaging

```powershell
python tools/package_game.py MyGame --project-dir path/to/MyGame
```

The packager finds all `worldDatabase` entries in the scene manifest and copies
each `.db` file to the output `worlds/` directory. It also always includes
`game.json`, `engine.json`, the boot resource JSONs (`materials.json`,
`mc_texture_map.json`), `resources/fonts/` (menu text), and the default character
animation, so the packaged build matches what runs from `build/Debug/`.

### Scene Tools (MCP / HTTP)

| Tool | Use |
|------|-----|
| `list_scenes` | List all scenes in the manifest |
| `get_active_scene` | Get the currently active scene |
| `transition_scene` | Switch to a different scene |
| `add_scene` / `remove_scene` | Modify scenes at runtime |
| `save_scene_manifest` | Persist the manifest to disk |

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

Structures are placed after terrain generation. Two types:

**Fill regions** — solid or hollow boxes of a material:
```json
{"type": "fill", "from": {"x":0,"y":16,"z":0}, "to": {"x":10,"y":20,"z":10}, "material": "Stone"}
```

**Templates** — pre-built objects:
```json
{"type": "template", "name": "tree.txt", "position": {"x":15,"y":17,"z":15}}
```

Available templates: `tree.txt`, `tree2.txt`, `sphere.txt`, `test_castle_optimized.txt`

### Step 4: Create NPCs

Each NPC needs:
- **Unique name** (no duplicates)
- **Position** above terrain surface (Y = terrain height + 2)
- **Behavior**: `idle` (stays put), `patrol` (walks waypoints), `wander` (random movement)
- **Dialogue** (optional): branching conversation tree
- **Story character** (optional): personality traits, goals, faction

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

- **Player type**: `physics` (humanoid with ragdoll), `animated` (voxel character), `spider`
- **Camera**: Position 30-50 units from the action, pitch -25° to -35° for overview

### Step 7: Load & Verify

1. Validate: `validate_game_definition` catches schema errors before loading
2. Load: `load_game_definition` creates everything atomically
3. Screenshot: `screenshot` lets you see the result
4. Iterate: use individual tools to refine

### Step 8: Save

- `save_world` persists to `worlds/default.db`
- `export_game_definition` exports as reusable JSON
- `save_template` saves structures as reusable templates

### Step 9: Package for Distribution

The recommended flow creates a standalone game project with its own C++ source,
then packages it into a distributable directory:

```bash
# 1. Scaffold a game project from the definition
python tools/create_project.py MyGame --game-definition game.json

# 2. Pre-bake the world (engine must be running with the game loaded)
python tools/package_game.py MyGame --prebake-world --world-db worlds/default.db

# 3. Copy the pre-baked world into the project
copy worlds/default.db <project_dir>/worlds/default.db

# 4. Build the game project
cd <project_dir>
cmake -B build -S .
cmake --build build --config Debug

# 5. Package
python tools/package_game.py MyGame --project-dir <project_dir>
```

For quick iteration (uses engine exe directly — includes Python/scripting):
```bash
# Via MCP tool:
package_game(name="MyGame")

# Via CLI:
python tools/package_game.py MyGame --from-engine
python tools/package_game.py MyGame --definition game.json
```

**Output structure** (`Documents/PhyxelProjects/MyGame/`):
```
MyGame/
├── MyGame.exe              # Game executable (own C++ or engine binary)
├── Play MyGame.bat         # Double-click launcher
├── game.json               # Game definition (NPCs, story, camera)
├── engine.json             # Engine config
├── README.md               # Player instructions
├── shaders/                # Compiled SPIR-V only
├── resources/
│   ├── textures/           # Texture atlas
│   ├── templates/          # Only referenced templates
│   ├── animated_characters/# Only referenced anims
│   ├── dialogues/          # Dialogue files
│   └── sounds/             # Audio files
└── worlds/
    └── default.db          # Pre-baked world (instant startup)
```

**Options:**
- `--project-dir` — use a scaffolded game project (own exe, no Python/MCP)
- `--prebake-world` — save engine world to SQLite first
- `--world-db` — supply an existing pre-baked database
- `--all-resources` — include everything (not just what the game references)
- `--include-mcp` — bundle the MCP server for continued AI iteration
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
  "player": {"type": "physics", "position": {"x":16,"y":18,"z":16}},
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
    {"type": "template", "name": "tree.txt", "position": {"x":25,"y":21,"z":15}}
  ],
  "player": {"type": "physics", "position": {"x":16,"y":28,"z":16}},
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

## Player Controls After Loading

| Key | Action |
|-----|--------|
| K | Toggle character control mode |
| WASD | Move |
| Space | Jump (animated character) |
| Shift | Sprint |
| V | Toggle camera mode (1st/3rd/free) |
| Left Click | Attack / Break voxel |
| T | Spawn template object |
| ESC | Exit |

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

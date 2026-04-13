# CLAUDE.md — Phyxel Voxel Game Engine

## Project Overview

Phyxel is a voxel game engine built with C++17, Vulkan, and Bullet Physics. It features a 32³ chunk system, physics-based entity characters, animated voxel characters, embedded Python scripting, and an MCP server for AI agent integration.

## Build System

- **CMake 3.15+**, C++17 required, MSVC 2022
- **Dependencies**: Vulkan SDK, GLFW, GLM, Bullet Physics, ImGui, pybind11, nlohmann/json, cpp-httplib, Google Test, SQLite3, miniaudio
- **CMake is NOT in system PATH**. Before running cmake, add it:
  ```powershell
  $env:PATH += ";C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
  ```
- **Build commands**:
  ```powershell
  cmake -B build -S .
  cmake --build build --config Debug
  ```
- **Build + test shortcut**: `./build_and_test.ps1 -Config Debug -RunTests`
  - `-UnitOnly`, `-IntegrationOnly`, `-BenchmarkOnly`, `-StressOnly`, `-E2EOnly` flags available
- **Targets**: `phyxel_core` (engine static lib), `phyxel_editor` (editor/dev-tool lib + executable)
- **Test suites**: `tests/`, `tests/integration/`, `tests/benchmark/`, `tests/stress/`, `tests/e2e/`

## Coordinate System

- **Right-handed**: X = right (east), **Y = up**, Z = toward viewer (north)
- **Chunk size**: 32×32×32 = 32,768 voxels per chunk
- **Index formula** (Z-minor, matches X-major loop order):
  ```cpp
  size_t index = z + y*32 + x*1024;   // Z stride=1, Y stride=32, X stride=1024
  ```
- **Conversions**:
  ```cpp
  chunkCoord = worldPos / 32;
  localPos   = worldPos % 32;
  chunkOrigin = chunkCoord * 32;
  ```
- World coordinates can be negative. Local coordinates are always 0–31.

## Materials

Predefined in `MaterialManager` (engine/src/physics/Material.cpp). Names are **case-sensitive**.
Each material has its own 6-face texture set in the atlas (72 textures total, 256x256 atlas).

| Name     | Mass | Friction | Restitution | Color Tint (RGB)        | Notes                    |
|----------|------|----------|-------------|-------------------------|--------------------------|
| Wood     | 0.7  | 0.6      | 0.2         | (0.8, 0.6, 0.3) brown   | Light, natural feel      |
| Metal    | 4.0  | 0.7      | 0.1         | (0.7, 0.7, 0.8) silver  | Heavy, high density      |
| Glass    | 1.5  | 0.2      | 0.6         | (0.9, 0.95, 1.0) clear  | Brittle, bouncy          |
| Rubber   | 0.5  | 0.9      | 0.9         | (0.3, 0.3, 0.3) dark    | Very bouncy              |
| Stone    | 6.0  | 0.8      | 0.05        | (0.6, 0.5, 0.4) gray    | Heaviest, rough          |
| Ice      | 0.9  | 0.1      | 0.4         | (0.8, 0.9, 1.0) blue    | Very slippery            |
| Cork     | 0.2  | 0.5      | 0.4         | (0.8, 0.7, 0.5) tan     | Ultra-light              |
| glow     | 1.0  | 0.5      | 0.5         | (1.0, 1.0, 1.0) white   | Emissive material        |
| Default  | 1.0  | 0.5      | 0.3         | (1.0, 1.0, 1.0)         | Balanced defaults        |

### Texture Pipeline
- Source textures: `textures/` dir, 18x18 PNG per face per material (e.g. `stone_side_n.png`)
- Atlas builder: `python tools/texture_atlas_builder.py textures/ cube_atlas.png`
- Outputs: `resources/textures/cube_atlas.{png,json,h,glsl}`
- Texture generation: `python tools/generate_material_textures.py` (regenerates procedural textures)
- After atlas changes, rebuild shaders: `.\build_shaders.bat`
- Material→texture lookup: `TextureConstants::getTextureIndexForMaterial(materialName, faceID)` in `Types.h`

## Entity Types

Spawnable via MCP `spawn_entity` (POST `/api/entity/spawn`) or keybindings:

| Type       | Class                    | Capabilities |
|------------|--------------------------|--------------|
| `physics`  | `PhysicsCharacter`       | Humanoid ragdoll with Bullet Physics, WASD movement, camera-attached |
| `spider`   | (spider enemy)           | Spider-type enemy entity |
| `animated` | `AnimatedVoxelCharacter` | Voxel character loaded from `.anim` files. States: Idle, Walk, Run, Jump, Fall, Land, Crouch, CrouchIdle, CrouchWalk, StandUp, Attack, TurnLeft/Right, StrafeLeft/Right, WalkStrafeLeft/Right, Preview |

Control mode toggled with **K** key. Animated character supports Jump (Space), Attack (Left Click), Crouch (Ctrl), Sprint (Shift), Derez/explode (X).

## Chunk Layout

- Each chunk is 32×32×32. Chunks are positioned by their world-space origin (ivec3).
- **Default startup**: Loads all chunks from `worlds/default.db`. If database is empty, starts with an **empty world** (for Python scripting).
- Initial camera: position (50, 50, 50), looking toward (16, 16, 16), yaw=-135°, pitch=-30°.
- Chunks can be created dynamically: `MultiChunkDemo::createLinearChunks(n)`, `createGridChunks(w,h)`, `create3DGridChunks(x,y,z)`.
- World storage: SQLite database at `worlds/default.db`. Save with `save_world` MCP tool.

## World Generation

`WorldGenerator` supports these generation types (enum `GenerationType`):

| Type       | Description |
|------------|-------------|
| `Random`   | 70% fill rate, deterministic from seed |
| `Perlin`   | Height-map terrain using Perlin noise, base level Y=16 |
| `Flat`     | Solid below Y=16, empty above |
| `Mountains`| Multi-octave noise, base level Y=20, peaks up to ~60 |
| `Caves`    | Perlin terrain with 3D cave carving |
| `City`     | Flat ground at Y=15, procedural 16×16 buildings (height 20-60) |
| `Custom`   | User-supplied generation function |

**Multi-material terrain**: Generated terrain uses materials based on depth from surface:
- Surface layer: Default (or Ice on mountain peaks above Y=45)
- Sub-surface (1-4 blocks deep): Cork (earthy brown)
- Deep underground: Stone
- City buildings: Metal, ground: Stone/Default

The Python script `scripts/world_gen.py` provides demo functions: `generate_pyramid()`, `generate_platform()`, `generate_glow_pillars()`, `run_demo()`.

## Object Templates

Files in `resources/templates/` (spawnable via **T** / **Shift+T** or MCP `spawn_template`):

- `tree.voxel`, `tree2.voxel`, `tree2_hollow.voxel`
- `sphere.voxel`, `sphere_hollow.voxel`
- `test_castle_optimized.voxel`, `test_castle_optimized_lossy.voxel`
- `my_model.voxel`

**T** = spawn static (merged into terrain), **Shift+T** = spawn dynamic (physics objects).

### BlockSmith AI Model Generation

Generate furniture, props, buildings, and decorations from text prompts using [BlockSmith](https://github.com/Barbarisch/blocksmith) (fork at `external/blocksmith/`).

**Pipeline**: Text prompt → LLM (Claude/Gemini/OpenAI) → .bbmodel → voxelize → .voxel template → `spawn_template`

**Usage**:
```bash
# Generate a furniture template (cached for reuse)
python tools/blocksmith_generate.py "a wooden chair" --name chair --size 2 --material Wood

# Generate a building (multi-material, with door/window openings)
python tools/blocksmith_generate.py --building --name tavern_medieval \
  --building-type tavern --style medieval \
  --width 14 --depth 18 --stories 2 --height 5 \
  --materials '{"wall":"Stone","floor":"Wood","roof":"Wood"}'

# Batch-generate furniture library
python tools/generate_furniture_library.py             # Generate all missing
python tools/generate_furniture_library.py --list      # Show library status
python tools/generate_furniture_library.py --tier 1    # Essential furniture only

# List / search generated templates
python tools/blocksmith_generate.py --list
python tools/blocksmith_generate.py --search table
```

**MCP tools**: `generate_template`, `build_building`, `search_templates`, `list_generated_templates`

**Building mode** (`--building`): Uses a specialized system prompt that produces architecturally correct structures with exact block dimensions, door/window openings, floor slabs, and multi-material support. Direct conversion bypasses the mesh voxelizer — cuboid coordinates map 1:1 to voxels. Material is inferred from element names (wall_ → mat-wall → Stone, floor → mat-floor → Wood, etc.).

**Key parameters**:
- `--size`: Target size in world cubes (1.0 = one block). Furniture: 2-3, buildings: 8-15
- `--material`: Wood, Stone, Metal, Glass, etc. (for furniture/props)
- `--model`: LLM provider (`anthropic/claude-sonnet-4-20250514`, `gemini/gemini-2.5-pro`, `openai/gpt-4o`)
- Building-specific: `--width`, `--depth`, `--height`, `--stories`, `--building-type`, `--style`, `--door-facing`, `--windows`, `--materials`

**Templates are saved permanently** in `resources/templates/`. Duplicate requests reuse the cached template without API calls. Catalog at `resources/templates/template_catalog.json` (includes `category`, `tags`, and optional `building` metadata).

**Furniture library** (`tools/generate_furniture_library.py`): Defines ~25 furniture/prop pieces across 2 tiers (essential + themed). Each item has category and tags for programmatic selection.

**Env vars**: `PHYXEL_AI_API_KEY` or `ANTHROPIC_API_KEY` for Claude models.

## Animation Files

Root-level: `character.anim`, `character_box.anim`, `character_complete.anim`

In `resources/animated_characters/`:
- `character_wolf.anim`
- `character_spider.anim`, `character_spider2.anim`, `character_spider3.anim`
- `character_female.anim`, `character_female2.anim`, `character_female3.anim`
- `character_dragon.anim`

## Keybindings

### General
| Key | Action |
|-----|--------|
| ESC | Toggle Pause Menu (freeze world, show resume/settings/quit) |
| F1 | Toggle Performance Overlay |
| F3 | Toggle Force Debug Vis |
| F4 | Toggle Debug Rendering |
| Ctrl+F4 | Cycle Debug Vis Mode |
| F5 | Toggle Raycast Vis (also shows NPC FOV cones) |
| F6 | Toggle Lighting Controls |
| F7 | Toggle Profiler |
| ` (backtick) | Toggle Scripting Console |

### Camera & World
| Key | Action |
|-----|--------|
| V | Toggle Camera Mode (First/Third/Free) |
| C | Place Cube |
| Ctrl+C | Place Subcube |
| Alt+C | Place Microcube |
| Left Click | Break Voxel |
| Middle Click | Subdivide Cube |
| T / Shift+T | Spawn Static / Dynamic Template |
| P | Toggle Template Preview |
| -/= | Decrease/Increase Ambient Light |
| [/] | Decrease/Increase Spawn Speed |

### Character
| Key | Action |
|-----|--------|
| K | Toggle Character Control (Physics/Spider/Animated) |
| W/A/S/D | Movement |
| Shift | Sprint |
| Space | Jump (Animated) |
| Left Click | Attack (Animated) |
| Ctrl | Crouch (Animated) |
| X | Derez Character (explode into physics objects) |
| N/B | Next/Previous Animation (Preview Mode) |

## MCP Server (AI Agent Bridge)

Server: `scripts/mcp/phyxel_mcp_server.py` — connects to engine HTTP API at `localhost:8090`.

**Setup** (Claude Code config):
```json
{
  "mcpServers": {
    "phyxel": {
      "command": "python",
      "args": ["scripts/mcp/phyxel_mcp_server.py"],
      "cwd": "<path-to-phyxel-repo>"
    }
  }
}
```

**Requirements**: `pip install mcp httpx`. Engine must be running.

### MCP Tools

| Tool | Description |
|------|-------------|
| `engine_status` | Check if engine is running and responsive |
| `get_world_state` | Full snapshot: entities, camera, counts |
| `get_camera` | Camera position, yaw/pitch, front vector |
| `list_entities` | All entities with IDs, positions, types |
| `get_entity` | Detailed info for one entity |
| `spawn_entity` | Spawn physics/spider/animated entity at position |
| `move_entity` | Teleport entity to new position |
| `remove_entity` | Remove entity from world |
| `update_entity` | Update entity position/rotation/scale/color |
| `place_voxel` | Place single voxel with optional material |
| `remove_voxel` | Remove voxel at coordinates |
| `query_voxel` | Check if voxel exists at coordinates |
| `place_voxels_batch` | Place multiple voxels efficiently |
| `fill_region` | Fill 3D box with material (optional hollow). Max 100k |
| `clear_region` | Remove all voxels in 3D box. Max 100k |
| `scan_region` | Read all voxels in 3D box |
| `list_templates` | List available object templates |
| `spawn_template` | Place template at position (static or dynamic) |
| `generate_template` | Generate a voxel template from text prompt using BlockSmith AI (cached) |
| `search_templates` | Search generated template catalog by name or prompt |
| `list_generated_templates` | List all AI-generated templates with metadata |
| `build_building` | Generate + spawn an LLM-designed building (multi-material, doors/windows) |
| `build_structure` | Build procedural structure (house/tavern/tower/wall/furniture) at position |
| `list_structure_types` | List available structure types with parameters |
| `list_materials` | List all materials with properties |
| `get_chunk_info` | Loaded chunk count, origins, stats |
| `set_camera` | Move/orient camera |
| `run_script` | Execute Python in engine interpreter |
| `screenshot` | Capture current game view as PNG |
| `save_world` | Save world to SQLite (dirty or all chunks) |
| `poll_events` | Poll game events since cursor (entity spawn/remove/move, voxel place/remove, region fill/clear, world save) |
| `create_snapshot` | Capture named snapshot of a region for undo. Max 100k voxels, 50 snapshots |
| `restore_snapshot` | Restore a snapshot (clears region, replaces voxels with saved state) |
| `list_snapshots` | List all stored snapshots with metadata |
| `delete_snapshot` | Delete a named snapshot to free memory |
| `copy_region` | Copy voxel region to clipboard (relative offsets). Max 100k voxels |
| `paste_region` | Paste clipboard at new position with optional Y-axis rotation (0/90/180/270) |
| `get_clipboard` | Check clipboard status (has data, size, voxel count) |
| `generate_world` | Generate procedural terrain (Random/Perlin/Flat/Mountains/Caves/City) for chunks. Max 64 chunks |
| `save_template` | Save a region as a reusable .voxel template file, immediately available for spawn_template |
| `load_game_definition` | Load a complete game from a single JSON definition (world, structures, player, NPCs, story) |
| `export_game_definition` | Export current game state as a game definition JSON |
| `validate_game_definition` | Validate a game definition JSON without loading |
| `create_game_npc` | Composite: spawn NPC + dialogue + story character in one call |
| `build_project` | Build the Phyxel engine (cmake configure + build) |
| `launch_engine` | Launch the engine executable as background process |
| `engine_running` | Check if engine process is alive and API is responsive |
| `package_game` | Package game into standalone distributable directory (no source/build files) |
| `project_info` | Get info about the game project loaded via --project (dir, exe, game.json) |
| `build_game` | Build the game project from within the running engine (cmake) |
| `run_game` | Launch the built game executable from within the running engine |
| `list_entity_animations` | List all animation clips on an animated entity |
| `play_entity_animation` | Play a named animation clip (puts entity in Preview mode) |
| `get_animation_state` | Get FSM state, current clip, progress, duration, blend duration |
| `set_animation_state` | Set FSM state (Idle, Walk, Run, Jump, Attack, Preview, etc.) |
| `set_blend_duration` | Set crossfade blend duration (seconds) for transitions |
| `reload_entity_animation` | Hot-reload animation clips from .anim file (preserves skeleton/model) |
| **NPC Management** | |
| `list_npcs` | List all NPCs with positions, behaviors, health |
| `spawn_npc` | Spawn NPC with name, position, behavior, appearance |
| `remove_npc` | Remove NPC by name |
| `set_npc_behavior` | Change NPC behavior (idle/patrol/wander/behavior_tree) |
| `get_npc_appearance` | Get NPC appearance config |
| `set_npc_appearance` | Set NPC body part sizes/colors |
| `set_npc_dialogue` | Assign dialogue tree to NPC |
| `get_npc_blackboard` | Read NPC's behavior tree blackboard |
| `get_npc_perception` | Read NPC's perception state (seen entities, threats) |
| `set_npc_blackboard` | Write key/value to NPC's blackboard |
| `get_npc_needs` | Get NPC needs (hunger, rest, social, etc.) |
| `set_npc_needs` | Set NPC need values |
| `get_npc_schedule` | Get NPC daily schedule |
| `set_npc_schedule` | Set NPC daily schedule activities |
| `get_npc_relationships` | Get NPC relationships (trust/affection/respect/fear) |
| `set_npc_relationship` | Set relationship values between NPCs |
| `apply_npc_interaction` | Apply social interaction (greet/trade/gift/insult/etc.) |
| `get_npc_worldview` | Get NPC beliefs, opinions, observations |
| `set_npc_belief` | Set NPC belief with confidence |
| `set_npc_opinion` | Set NPC opinion/sentiment about a subject |
| `start_ai_conversation` | Start LLM-powered AI conversation with NPC |
| **Dialogue** | |
| `get_dialogue_state` | Get current dialogue state (active, node, choices) |
| `start_dialogue` | Start dialogue with NPC |
| `end_dialogue` | End current dialogue |
| `advance_dialogue` | Advance to next dialogue node |
| `select_dialogue_choice` | Select a dialogue choice by index |
| `load_dialogue_file` | Load dialogue tree from JSON file |
| `list_dialogue_files` | List available dialogue JSON files |
| `say_bubble` | Show speech bubble above NPC |
| **Health & Combat** | |
| `damage_entity` | Deal damage to entity |
| `heal_entity` | Heal entity |
| `set_entity_health` | Set entity health directly |
| `kill_entity` | Kill entity |
| `revive_entity` | Revive dead entity |
| `attack` | Perform combat attack (sphere+cone hit detection) |
| `get_equipment` | Get entity's equipped items |
| `equip_item` | Equip item to entity slot |
| `unequip_item` | Unequip item from entity slot |
| **Inventory & Items** | |
| `get_inventory` | Get player inventory contents |
| `give_item` | Give item to player inventory |
| `take_item` | Remove item from player inventory |
| `select_hotbar_slot` | Select active hotbar slot |
| `set_inventory_slot` | Set specific inventory slot contents |
| `clear_inventory` | Clear all inventory slots |
| `set_creative_mode` | Toggle creative mode (infinite items) |
| `list_items` | List all registered item definitions |
| `get_item` | Get item definition by ID |
| **Day/Night & Lighting** | |
| `get_day_night` | Get current time, day number, phase |
| `set_day_night` | Set time of day, day length, time speed |
| `list_lights` | List all point and spot lights |
| `add_point_light` | Add point light at position |
| `add_spot_light` | Add spot light at position |
| `remove_light` | Remove light by index |
| `update_light` | Update light properties |
| `set_ambient_light` | Set ambient light level |
| **Audio** | |
| `list_sounds` | List available sound files |
| `play_sound` | Play a sound effect |
| `set_volume` | Set audio channel volume |
| **Game State (Pause/Health/Respawn/Music/Save/Objectives)** | |
| `toggle_pause` | Toggle game pause state |
| `get_pause_state` | Get current pause state |
| `get_player_health` | Get player health, max, alive status |
| `damage_player` | Deal damage to player |
| `heal_player` | Heal player |
| `kill_player` | Kill player (triggers death sequence) |
| `revive_player` | Revive player |
| `get_respawn_state` | Get respawn system state (dead, timer, spawn point) |
| `set_spawn_point` | Set player respawn position |
| `force_respawn` | Force immediate respawn |
| `get_music_state` | Get music playlist state (playing, track, mode, volume) |
| `control_music` | Control music (play/stop/next/add_track/set_volume/set_mode) |
| `save_player` | Save player profile to SQLite |
| `load_player` | Load player profile from SQLite |
| `get_objectives` | Get all objectives with status |
| `add_objective` | Add new objective (title, description, priority, category) |
| `complete_objective` | Mark objective as completed |
| `fail_objective` | Mark objective as failed |
| `remove_objective` | Remove objective |
| **Story Engine** | |
| `story_get_state` | Get story engine state |
| `story_get_world` | Get story world state |
| `story_load_world` | Load story world from JSON |
| `story_list_arcs` | List all story arcs |
| `story_get_arc` | Get story arc details |
| `story_add_arc` | Add new story arc with beats |
| `story_list_characters` | List story characters |
| `story_get_character` | Get story character details |
| `story_add_character` | Add story character |
| `story_remove_character` | Remove story character |
| `story_set_agency` | Set character agency level |
| `story_set_variable` | Set world variable |
| `story_add_knowledge` | Add knowledge to character |
| `story_trigger_event` | Trigger story event |
| **Crafting & Hazards** | |
| `list_recipes` | List all crafting recipes |
| `get_recipe` | Get recipe details |
| `craft_item` | Craft item from recipe |
| `add_recipe` | Add new crafting recipe |
| **UI & Menus** | |
| `create_menu` | Create custom menu screen |
| `show_menu` | Show menu by name |
| `hide_menu` | Hide menu by name |
| `toggle_menu` | Toggle menu visibility |
| `remove_menu` | Remove menu definition |
| `list_menus` | List all menu screens |
| **Jobs (Async Operations)** | |
| `list_jobs` | List background jobs |
| `get_job_status` | Get job progress/result |
| `submit_job` | Submit async background job |
| `cancel_job` | Cancel running job |
| **Locations** | |
| `get_locations` | List named world locations |
| `add_location` | Register named location |
| `remove_location` | Remove named location |
| **Structures** | |
| `build_structure` | Build procedural structure (house, tavern, tower, wall, staircase, table, chair, counter, bed) |
| `list_structure_types` | List all structure types with parameters and defaults |
| **Terrain** | |
| `get_terrain_height` | Get terrain height at (x,z) |
| `clear_chunk` | Clear all voxels in a chunk |
| `rebuild_physics` | Rebuild physics collision meshes |
| **AI System** | |
| `get_ai_status` | Get AI system status |
| `configure_ai` | Configure AI provider/model/key |
| `send_ai_message` | Send message to AI system |
| **D&D RPG System** | |
| `roll_dice` | Roll dice using D&D notation (2d6+3, 1d20, etc.) with optional advantage/disadvantage. No engine required |
| `check_dc` | Roll d20 + bonus vs a DC. Returns pass/fail, natural 20/1 detection. No engine required |
| `get_party` | Get D&D party state: members, levels, alive status, leader, total/average level |
| `add_party_member` | Add an entity to the D&D party |
| `remove_party_member` | Remove an entity from the D&D party |
| `get_combat_state` | Get initiative tracker state: active, round, current entity, full turn order |
| `start_combat` | Start D&D combat and roll initiative for participants |
| `next_combat_turn` | Advance to next turn in combat |
| `end_combat` | End combat and reset initiative tracker |
| `set_initiative` | Manually set an entity's initiative value |
| `get_world_date` | Get in-game calendar date: day/month/year, season, moon phase, holidays |
| `advance_world_date` | Advance the fantasy calendar by N days |
| `set_world_date` | Set the calendar to a specific total day number |
| `add_journal_entry` | Add a campaign journal entry (SessionNote, WorldEvent, QuestUpdate, etc.) |
| `get_journal_entries` | Query journal by type, tag, day, or full-text search |
| `remove_journal_entry` | Remove a journal entry by ID |
| **Project Lifecycle** | |
| `list_projects` | List scaffolded game projects |
| `create_project` | Create new game project |
| `open_project` | Open project in engine |
| `reload_game_definition` | Hot-reload game.json changes |
| `clear_all_entities` | Remove all entities from world |

### AI Game Development Workflow

The `load_game_definition` tool enables creating entire games from a single JSON payload:

```json
{
  "name": "My Game",
  "world": {"type": "Perlin", "from": {"x":-1,"y":0,"z":-1}, "to": {"x":1,"y":0,"z":1}},
  "player": {"type": "animated", "position": {"x":16,"y":20,"z":16}},
  "camera": {"position": {"x":50,"y":50,"z":50}, "yaw": -135, "pitch": -30},
  "npcs": [
    {
      "name": "Guard", "position": {"x":10,"y":20,"z":10},
      "behavior": "patrol",
      "waypoints": [{"x":10,"y":20,"z":10}, {"x":20,"y":20,"z":10}],
      "dialogue": {
        "id": "guard_talk", "startNodeId": "start",
        "nodes": [{"id":"start","speaker":"Guard","text":"Hello traveler!"}]
      },
      "storyCharacter": {
        "id": "guard", "faction": "town_guard", "agencyLevel": 1,
        "traits": {"openness": 0.3, "extraversion": 0.7},
        "goals": [{"id":"protect","description":"Protect the town","priority":0.9}]
      }
    }
  ],
  "structures": [
    {"type": "fill", "from": {"x":0,"y":0,"z":0}, "to": {"x":31,"y":15,"z":31}, "material": "Stone"},
    {"type": "tavern", "position": {"x":8,"y":16,"z":20}, "width": 14, "depth": 18, "stories": 2,
     "facing": "south", "materials": {"wall":"Stone","floor":"Wood","roof":"Wood"}, "furnished": true},
    {"type": "house", "position": {"x":26,"y":16,"z":20}, "width": 8, "depth": 10, "height": 5,
     "facing": "south", "materials": {"wall":"Stone","floor":"Wood"}, "windows": 2},
    {"type": "tower", "position": {"x":4,"y":16,"z":4}, "radius": 3, "height": 10, "material": "Stone"}
  ],
  "story": {
    "arcs": [{"id":"main","name":"Main Quest","constraintMode":"Guided",
              "beats":[{"id":"b1","description":"Meet the guard","type":"Hard"}]}]
  }
}
```

**Typical AI workflow:**
1. `build_project` — Build the engine
2. `launch_engine` — Start the engine (use `args: ["--project", "<path>"]` to open a game project)
3. `engine_running` — Verify it's ready
4. `load_game_definition` — Load the full game definition
5. `screenshot` — See the result
6. Iterate with `create_game_npc`, `fill_region`, `place_voxel`, etc.
7. `build_game` — Build the game from within the engine
8. `run_game` — Launch the built game to test it
9. `package_game` — Package into standalone distributable directory

**Engine as Editor (--project mode):**
Launch the engine with `--project <dir>` to open a game project for development:
```
phyxel.exe --project C:\Users\jack\Documents\PhyxelProjects\FrozenHighlands
```
- Loads world from the project's `worlds/default.db`
- Auto-loads game definition from project's `game.json`
- Window title set from project's `engine.json`
- `save_world` writes back to the project's database
- `build_game` / `run_game` API endpoints build and launch the standalone game

**Asset Editor (--asset-editor mode):**
Launch with `--asset-editor <file.voxel>` to edit a voxel template on a clean flat scene:
```
phyxel.exe --asset-editor resources/templates/my_prop.voxel
```
- Spawns the template at a fixed origin on a Stone floor platform
- Right-side ImGui panel: material palette, reference character toggle (H), save button
- **H**: toggle humanoid reference character for scale
- **Ctrl+S**: save current voxel region back to the `.voxel` file
- Floor at Y=15 is unbreakable; template voxels above are fully editable
- Hovering over ImGui panels blocks voxel hover/break

**Anim Editor (--anim-editor mode):**
Launch with `--anim-editor <file.anim>` to inspect and resize character bones:
```
phyxel.exe --anim-editor resources/animated_characters/humanoid.anim
```
- Character spawns on a flat Stone floor in Preview animation state
- Right-side ImGui panel: animation clip selector, per-bone scale sliders (0.1×–3.0×)
- Scale sliders update the character model live; Reset All Scales restores originals
- **Ctrl+S**: writes modified `MODEL` section back to the `.anim` file (skeleton and animations unchanged)
- All voxel interaction disabled in this mode

**Game Project Scaffolding:**
Each game gets its own C++ source that links against `phyxel_core`. Games are
standalone executables — no Python scripting, MCP server, or dev tools included.

- Scaffold: `python tools/create_project.py MyGame --game-definition game.json`
- Build: `cd <project_dir> && cmake -B build -S . && cmake --build build --config Debug`
- The generated code uses `GameCallbacks` + `EngineRuntime` (same pattern as `examples/minimal_game/`)
- World terrain is pre-baked in `worlds/default.db` (instant startup)
- NPCs, dialogue, story, and camera are loaded from `game.json` at runtime (world key is stripped)

**Game Packaging:**
- `python tools/package_game.py MyGame --project-dir path/to/MyGame` — Package a game project
- `python tools/package_game.py MyGame --prebake-world --from-engine` — Pre-bake world + package
- `python tools/package_game.py MyGame --definition game.json` — Legacy: use engine exe
- Output in `Documents/PhyxelProjects/<GameName>/` — self-contained, no engine source
- See `docs/GameCreationGuide.md` for full workflow
- Use `@game-creator` Copilot agent for AI-assisted game creation

## Project Structure

```
engine/          # phyxel_core static library
  include/       # Public headers (core/, graphics/, physics/, scene/, ui/, input/, utils/)
  src/           # Implementation
  deprecated/    # Archived experimental code (ActiveCharacter, HybridCharacter) — not compiled
editor/          # phyxel_editor lib + phyxel executable (dev tool / world editor)
  include/       # Editor-specific headers (Application, InputController, AI, scene entities)
  src/           # Application.cpp, scripting bindings, etc.
examples/        # Standalone game examples
  minimal_game/  # Reference: GameCallbacks + EngineRuntime pattern
tests/           # Unit tests (Google Test)
  integration/   # Integration tests
  benchmark/     # Benchmark tests
  stress/        # Stress tests
  e2e/           # End-to-end tests
tools/           # Development tools
  create_project.py  # Scaffold new game projects
  package_game.py    # Package games into distributable directories
  anim_editor.py     # .anim file editor (list, add, remove, merge, export, generate, mirror, scale)
scripts/         # Python scripts (world_gen.py, startup.py, audio_demo.py)
  mcp/           # MCP server for AI agents
resources/       # Templates, animations, textures, sounds, recipes
shaders/         # GLSL shaders + compiled SPIR-V
external/        # Third-party: bullet3, glfw, glm, imgui, goose, miniaudio, sqlite3
docs/            # Documentation
```

## Key Engine Subsystems

- **ChunkManager**: Manages all 32³ chunks, Vulkan buffers, voxel maps, face culling
- **ChunkStreamingManager**: SQLite-backed chunk persistence and streaming
- **WorldGenerator**: Procedural terrain generation (Perlin, Flat, Mountains, Caves, City)
- **PhysicsWorld**: Bullet Physics integration for rigid body dynamics
- **EntityRegistry**: Central O(1) entity lookup by string ID, spatial queries, type queries
- **ForceSystem**: Applies forces to physics entities
- **RenderPipeline / RenderCoordinator**: Vulkan rendering with instanced drawing
- **ScriptingSystem**: Embedded Python (pybind11) with `phyxel` module
- **EngineAPIServer**: HTTP API on port 8090 for external tool integration
- **InputManager**: GLFW input handling, camera control, keybindings
- **HealthComponent**: Per-entity health with damage/heal/kill/revive and death callbacks
- **RespawnSystem**: Death sequence timer, spawn point, auto-respawn with configurable delay
- **MusicPlaylist**: Background music with Sequential/Shuffle modes, track management, volume
- **PlayerProfile**: Player state persistence (camera, health, spawn point, inventory) to SQLite
- **ObjectiveTracker**: Quest/objective system with priority, status tracking, and HUD rendering
- **CombatSystem**: Sphere+cone hit detection, damage events, knockback, invulnerability frames
- **EquipmentSystem**: Slot-based equipment (6 slots), type validation, stat aggregation
- **CraftingSystem**: JSON recipes, resource validation, crafting with byproducts
- **DayNightCycle**: Sun/ambient animation, time phases, day counter, configurable day length
- **HazardSystem**: 6 environmental hazard types with DOT, slow, stun effects
- **AchievementSystem**: Counter/Threshold/OneShot/Composite types, JSON persistence
- **NPCManager**: NPC lifecycle, behavior strategies, patrol/idle/BT behaviors
- **NavGrid**: 2D navigation grid from voxel data, walkable/blocked/nearWall cell classification
- **AStarPathfinder**: A* pathfinding over NavGrid with wall-avoidance penalties
- **DialogueSystem**: Branching conversation trees, typewriter effect, speech bubbles
- **StoryEngine**: Story arcs, beats, character agents, LLM-powered narrative
- **AIConversationService**: Direct LLM client (Claude/OpenAI/Ollama) for NPC conversations
- **BehaviorTree / UtilityAI**: Composable BT framework with perception, blackboard, utility scoring
- **DynamicObjectManager**: Bullet physics dynamic voxel lifecycle — spawn, update, expire, despawn. Manages full/sub/micro cubes with 300 object cap. Provides active + total count queries
- **GpuParticlePhysics**: Vulkan compute XPBD particle physics — 5-pass pipeline (grid_clear → grid_build → integrate → collide → expand), 10000 particle cap, sleep/wake system, face-buffer rendering
- **VoxelManipulationSystem**: Hybrid routing for broken voxels — routes to Bullet (close range, <300 cap) or GPU compute (far range or Bullet full). See `docs/DynamicVoxelPhysics.md`
- **DiceSystem**: Static D&D dice roller — all standard die types (d4–d100), advantage/disadvantage, expression parsing ("2d6+3"), critical detection, DC checks, seeded RNG
- **CharacterAttributes**: Six D&D ability scores (STR/DEX/CON/INT/WIS/CHA) with layered modifiers (base/racial/equipment/temporary), initiative/AC/carry helpers
- **ProficiencySystem**: D&D 18-skill proficiency table, saving throws, passive checks, proficiency bonus by level, half/full/expertise tiers
- **CharacterSheet / CharacterProgression**: Full character identity — class, race, background, XP, level-up, HP, hit dice, features, ASIs. Classes/races/backgrounds are data-driven JSON in `resources/rpg/`
- **ActionEconomy / InitiativeTracker**: D&D action/bonus action/reaction economy per turn; initiative order with round tracking, surprise, reaction usage
- **AttackResolver / ConditionSystem**: Attack rolls (to-hit vs AC), damage rolls with crits, 15 standard conditions (poisoned, stunned, grappled, etc.) with per-tick callbacks
- **SpellDefinition / SpellcasterComponent / SpellResolver**: Data-driven spell library (JSON), slot-based casting (Warlock Pact included), concentration, area targeting, short/long rest slot recovery
- **RpgItem / CurrencySystem / AttunementSystem / EncumbranceSystem**: Item stats/rarity/attunement, GP/SP/CP currency, encumbrance with carry limits
- **ReputationSystem / DialogueSkillCheck / SocialInteractionResolver**: Per-faction reputation tiers (Hostile→Exalted), skill-check gated dialogue choices, social interaction DCs and reputation deltas
- **RestSystem**: Short rest (spend hit dice, optional caster short-rest recovery) and long rest (full HP, half hit dice restored, spell slot recovery)
- **WorldClock**: Fantasy calendar — 360-day year, 12 months, 4 seasons, 28-day lunar cycle, day-of-week, named holidays. Syncs with DayNightCycle
- **Party**: D&D party roster — member add/remove/alive tracking, leader promotion, total/average level, budget calculation helper
- **LootTable**: Weighted random loot — per-entry weight + independent chance, variable roll counts, registry loaded from JSON in `resources/loot_tables/`
- **EncounterBuilder**: D&D 5e encounter design — XP budget by party level, monster multipliers, adjusted XP, difficulty evaluation (Easy/Medium/Hard/Deadly), fluent builder API
- **CampaignJournal**: Session notes, world events, quest updates, discoveries — tag/type/day/full-text filtering, JSON persistence. See `docs/DnDRPGSystem.md`

## Common Patterns

- **Namespace**: `Phyxel::Core`, `Phyxel::Physics`, `Phyxel::Graphics`, `Phyxel::Scene`, `Phyxel::UI`, `Phyxel::Input`, `Phyxel::Utils`
- **Logging**: `LOG_INFO("Tag", "message")`, `LOG_DEBUG`, `LOG_WARN`, `LOG_ERROR`, `LOG_TRACE_FMT`
- **Entity registration**: `registry.registerEntity(entity, "my_id", "type_tag")`
- **Voxel placement**: `chunkManager->addCube(worldX, worldY, worldZ)` or with material: `addCubeWithMaterial(x, y, z, "Stone")`

## Testing

### Automated Tests
```powershell
# All unit tests (1614, 3 AI E2E auto-skip without API key)
.\build\tests\Debug\phyxel_tests.exe --gtest_brief=1

# Integration tests (36)
.\build\tests\integration\Debug\phyxel_integration_tests.exe --gtest_brief=1

# Or use the build script
./build_and_test.ps1 -Config Debug -RunTests
```

### Manual Standalone Game Testing
Use `samples/game_definitions/testing_baseline.json` and follow the checklist in `docs/StandaloneGameTesting.md`.

Quick scaffold-and-test:
```powershell
python tools/create_project.py TestGame --game-definition samples/game_definitions/testing_baseline.json
```

The test definition exercises: player spawn + third-person camera, NPC patrol movement with gravity, gentle Perlin terrain (heightScale=4), menus (main + pause), input gating, and dialogue/story.

# Phyxel MCP Server — AI Agent Integration Guide

Connect Claude Code, Goose, or other MCP-compatible AI agents to the running
Phyxel game engine. The MCP server bridges the agent ↔ engine gap by
translating tool calls into HTTP requests against the engine's API server
(port 8090).

## Prerequisites

1. **Python packages**: `pip install mcp httpx`
2. **Phyxel engine** must be compiled and runnable (`phyxel.exe`)

## Quick Start

```bash
# 1. Start the game
./phyxel.exe

# 2. Verify the API is up (in another terminal)
curl http://localhost:8090/api/status
# → {"status":"ok","engine":"phyxel","api_version":"1.0","port":8090}

# 3. Test the MCP server manually (optional)
python scripts/mcp/phyxel_mcp_server.py
```

## Claude Code Configuration

Add the server to your Claude Code MCP config. The config file is typically at:

- **Windows**: `%USERPROFILE%\.claude\claude_code_config.json`
- **macOS/Linux**: `~/.claude/claude_code_config.json`

```json
{
  "mcpServers": {
    "phyxel": {
      "command": "python",
      "args": ["scripts/mcp/phyxel_mcp_server.py"],
      "cwd": "C:\\Users\\bpete\\Documents\\GitHub\\phyxel"
    }
  }
}
```

> Adjust `cwd` to the absolute path of your phyxel repository.

## Goose Configuration

For Goose, use the existing `scripts/mcp/phyxel_extension.py` (Goose MCP
extension). The MCP server (`phyxel_mcp_server.py`) also works with Goose
if configured as a generic MCP server.

## Environment Variables

| Variable | Default | Description |
|---|---|---|
| `PHYXEL_API_URL` | `http://localhost:8090` | Engine HTTP API base URL |
| `PHYXEL_API_TIMEOUT` | `10` | HTTP request timeout in seconds |

## Available Tools (166 total)

### Status & Observation

| Tool | Description |
|---|---|
| `engine_status` | Check if the engine is running |
| `get_world_state` | Full world snapshot (entities, camera, counts) |
| `get_camera` | Camera position, yaw, pitch, front vector |
| `screenshot` | Capture current frame as PNG (saves to `screenshots/`) |
| `poll_events` | Poll game events since cursor |

### Entity Management

| Tool | Description |
|---|---|
| `list_entities` | List all entities with IDs and positions |
| `get_entity` | Get details of one entity by ID |
| `spawn_entity` | Spawn a physics/spider/animated character |
| `move_entity` | Teleport entity to new position |
| `remove_entity` | Remove entity from world |
| `update_entity` | Update entity position/rotation/scale/color |
| `clear_all_entities` | Remove all entities from world |

### Voxel World Building

| Tool | Description |
|---|---|
| `place_voxel` | Place a single cube at (x,y,z) with optional material |
| `remove_voxel` | Remove a cube at (x,y,z) |
| `query_voxel` | Check what's at (x,y,z) |
| `place_voxels_batch` | Place many cubes in one call |
| `fill_region` | Fill 3D box with material (optional hollow). Max 100k |
| `clear_region` | Remove all voxels in 3D box. Max 100k |
| `scan_region` | Read all voxels in 3D box |
| `get_terrain_height` | Get terrain height at (x,z) |
| `clear_chunk` | Clear all voxels in a chunk |
| `rebuild_physics` | Rebuild physics collision meshes |

### Templates & Materials

| Tool | Description |
|---|---|
| `list_templates` | List available object templates |
| `spawn_template` | Place a pre-built template (castle, tree, etc.) |
| `save_template` | Save a region as a reusable template |
| `list_materials` | List all materials with properties |

### Camera & Scripting

| Tool | Description |
|---|---|
| `set_camera` | Move/rotate the camera |
| `run_script` | Execute arbitrary Python in the engine |

### World Generation & Persistence

| Tool | Description |
|---|---|
| `generate_world` | Generate procedural terrain (Random/Perlin/Flat/Mountains/Caves/City) |
| `save_world` | Save world to SQLite (dirty or all chunks) |
| `get_chunk_info` | Loaded chunk count, origins, stats |
| `create_snapshot` | Capture named snapshot of a region for undo |
| `restore_snapshot` | Restore a previously saved snapshot |
| `list_snapshots` | List all stored snapshots |
| `delete_snapshot` | Delete a named snapshot |
| `copy_region` | Copy voxel region to clipboard |
| `paste_region` | Paste clipboard at new position with optional rotation |
| `get_clipboard` | Check clipboard status |

### NPC Management

| Tool | Description |
|---|---|
| `list_npcs` | List all NPCs with positions, behaviors, health |
| `spawn_npc` | Spawn NPC with name, position, behavior, appearance |
| `remove_npc` | Remove NPC by name |
| `set_npc_behavior` | Change NPC behavior (idle/patrol/wander/behavior_tree) |
| `get_npc_appearance` | Get NPC appearance config |
| `set_npc_appearance` | Set NPC body part sizes/colors |
| `set_npc_dialogue` | Assign dialogue tree to NPC |
| `get_npc_blackboard` | Read NPC's behavior tree blackboard |
| `get_npc_perception` | Read NPC's perception state |
| `set_npc_blackboard` | Write key/value to NPC's blackboard |
| `get_npc_needs` | Get NPC needs (hunger, rest, social, etc.) |
| `set_npc_needs` | Set NPC need values |
| `get_npc_schedule` | Get NPC daily schedule |
| `set_npc_schedule` | Set NPC daily schedule activities |
| `get_npc_relationships` | Get NPC relationships |
| `set_npc_relationship` | Set relationship values between NPCs |
| `apply_npc_interaction` | Apply social interaction (greet/trade/gift/insult) |
| `get_npc_worldview` | Get NPC beliefs, opinions, observations |
| `set_npc_belief` | Set NPC belief with confidence |
| `set_npc_opinion` | Set NPC opinion/sentiment about a subject |
| `start_ai_conversation` | Start LLM-powered AI conversation with NPC |

### Dialogue

| Tool | Description |
|---|---|
| `get_dialogue_state` | Get current dialogue state |
| `start_dialogue` | Start dialogue with NPC |
| `end_dialogue` | End current dialogue |
| `advance_dialogue` | Advance to next dialogue node |
| `select_dialogue_choice` | Select a dialogue choice by index |
| `load_dialogue_file` | Load dialogue tree from JSON file |
| `list_dialogue_files` | List available dialogue JSON files |
| `say_bubble` | Show speech bubble above NPC |

### Animation

| Tool | Description |
|---|---|
| `list_entity_animations` | List all animation clips on an entity |
| `play_entity_animation` | Play a named animation clip |
| `get_animation_state` | Get FSM state, current clip, progress |
| `set_animation_state` | Set FSM state (Idle, Walk, Run, Jump, Attack, etc.) |
| `set_blend_duration` | Set crossfade blend duration |
| `reload_entity_animation` | Hot-reload animation clips from .anim file |

### Health & Combat

| Tool | Description |
|---|---|
| `damage_entity` | Deal damage to entity |
| `heal_entity` | Heal entity |
| `set_entity_health` | Set entity health directly |
| `kill_entity` | Kill entity |
| `revive_entity` | Revive dead entity |
| `attack` | Perform combat attack (sphere+cone hit detection) |
| `get_equipment` | Get entity's equipped items |
| `equip_item` | Equip item to entity slot |
| `unequip_item` | Unequip item from entity slot |

### Inventory & Items

| Tool | Description |
|---|---|
| `get_inventory` | Get player inventory contents |
| `give_item` | Give item to player inventory |
| `take_item` | Remove item from player inventory |
| `select_hotbar_slot` | Select active hotbar slot |
| `set_inventory_slot` | Set specific inventory slot contents |
| `clear_inventory` | Clear all inventory slots |
| `set_creative_mode` | Toggle creative mode (infinite items) |
| `list_items` | List all registered item definitions |
| `get_item` | Get item definition by ID |

### Day/Night & Lighting

| Tool | Description |
|---|---|
| `get_day_night` | Get current time, day number, phase |
| `set_day_night` | Set time of day, day length, time speed |
| `list_lights` | List all point and spot lights |
| `add_point_light` | Add point light at position |
| `add_spot_light` | Add spot light at position |
| `remove_light` | Remove light by index |
| `update_light` | Update light properties |
| `set_ambient_light` | Set ambient light level |

### Audio

| Tool | Description |
|---|---|
| `list_sounds` | List available sound files |
| `play_sound` | Play a sound effect |
| `set_volume` | Set audio channel volume |

### Game State (Pause / Health / Respawn / Music / Save / Objectives)

| Tool | Description |
|---|---|
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
| `get_music_state` | Get music playlist state |
| `control_music` | Control music (play/stop/next/add_track/set_volume/set_mode) |
| `save_player` | Save player profile to SQLite |
| `load_player` | Load player profile from SQLite |
| `get_objectives` | Get all objectives with status |
| `add_objective` | Add new objective (title, description, priority, category) |
| `complete_objective` | Mark objective as completed |
| `fail_objective` | Mark objective as failed |
| `remove_objective` | Remove objective |

### Story Engine

| Tool | Description |
|---|---|
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

### Crafting

| Tool | Description |
|---|---|
| `list_recipes` | List all crafting recipes |
| `get_recipe` | Get recipe details |
| `craft_item` | Craft item from recipe |
| `add_recipe` | Add new crafting recipe |

### Game Definition

| Tool | Description |
|---|---|
| `load_game_definition` | Load complete game from JSON definition |
| `export_game_definition` | Export current game state as JSON |
| `validate_game_definition` | Validate a game definition without loading |
| `create_game_npc` | Composite: spawn NPC + dialogue + story character |
| `reload_game_definition` | Hot-reload game.json changes |

### UI & Menus

| Tool | Description |
|---|---|
| `create_menu` | Create custom menu screen |
| `show_menu` | Show menu by name |
| `hide_menu` | Hide menu by name |
| `toggle_menu` | Toggle menu visibility |
| `remove_menu` | Remove menu definition |
| `list_menus` | List all menu screens |

### Jobs (Async Operations)

| Tool | Description |
|---|---|
| `list_jobs` | List background jobs |
| `get_job_status` | Get job progress/result |
| `submit_job` | Submit async background job |
| `cancel_job` | Cancel running job |

### Locations

| Tool | Description |
|---|---|
| `get_locations` | List named world locations |
| `add_location` | Register named location |
| `remove_location` | Remove named location |

### AI System

| Tool | Description |
|---|---|
| `get_ai_status` | Get AI system status |
| `configure_ai` | Configure AI provider/model/key |
| `send_ai_message` | Send message to AI system |

### Project Lifecycle

| Tool | Description |
|---|---|
| `build_project` | Build the Phyxel engine |
| `launch_engine` | Launch the engine executable |
| `engine_running` | Check if engine process is alive |
| `project_info` | Get info about loaded game project |
| `build_game` | Build game project from within the engine |
| `run_game` | Launch built game executable |
| `package_game` | Package game into distributable directory |
| `list_projects` | List scaffolded game projects |
| `create_project` | Create new game project |
| `open_project` | Open project in engine |

## Architecture

```
┌─────────────┐    MCP (stdio)     ┌───────────────────┐   HTTP (8090)   ┌──────────────┐
│ Claude Code  │ ◄═══════════════► │ phyxel_mcp_server │ ◄═════════════► │ phyxel.exe   │
│ / Goose      │    JSON-RPC       │      (Python)     │   REST/JSON     │ (C++ engine) │
└─────────────┘                    └───────────────────┘                 └──────────────┘
```

1. AI agent sends a tool call via MCP protocol (JSON-RPC over stdio)
2. MCP server translates it to an HTTP request to `localhost:8090`
3. Read-only requests (entity list, camera, voxel query) run on the HTTP thread
4. Mutation requests (spawn, move, place voxel) go through the `APICommandQueue`
   and execute on the main game loop thread
5. Screenshot requests capture the Vulkan swapchain framebuffer via single-time
   commands and save as PNG

## Screenshot Details

The `screenshot` tool uses Vulkan's `vkCmdCopyImageToBuffer` to read back the
swapchain image after rendering. Screenshots are saved to the `screenshots/`
directory with timestamped filenames (e.g., `screenshot_20250308_142530_123.png`).

The engine's swapchain images are created with `VK_IMAGE_USAGE_TRANSFER_SRC_BIT`
to enable framebuffer readback.

## HTTP API Reference

All endpoints are also available directly via HTTP for custom tooling:

```bash
# Status
GET  /api/status

# Entities
GET  /api/entities
GET  /api/entity/{id}
POST /api/entity/spawn      {"type":"physics","position":{"x":0,"y":20,"z":0}}
POST /api/entity/move       {"id":"npc_01","position":{"x":10,"y":20,"z":30}}
POST /api/entity/remove     {"id":"npc_01"}

# Voxels
GET  /api/world/voxel?x=0&y=0&z=0
POST /api/world/voxel       {"x":0,"y":5,"z":0,"material":"stone"}
POST /api/world/voxel/remove {"x":0,"y":5,"z":0}
POST /api/world/voxel/batch {"voxels":[{"x":0,"y":5,"z":0},{"x":1,"y":5,"z":0}]}

# Templates
GET  /api/templates
POST /api/world/template    {"name":"castle","position":{"x":0,"y":0,"z":0},"static":true}

# Camera
GET  /api/camera
POST /api/camera            {"position":{"x":0,"y":20,"z":0},"yaw":-90,"pitch":-20}

# Scripting
POST /api/script            {"code":"print('hello from engine')"}

# Screenshot
GET  /api/screenshot

# World State
GET  /api/state
```

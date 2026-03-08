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

## Available Tools (17 total)

### Status & Observation

| Tool | Description |
|---|---|
| `engine_status` | Check if the engine is running |
| `get_world_state` | Full world snapshot (entities, camera, counts) |
| `get_camera` | Camera position, yaw, pitch, front vector |
| `screenshot` | Capture current frame as PNG (saves to `screenshots/`) |

### Entity Management

| Tool | Description |
|---|---|
| `list_entities` | List all entities with IDs and positions |
| `get_entity` | Get details of one entity by ID |
| `spawn_entity` | Spawn a physics/spider/animated character |
| `move_entity` | Teleport entity to new position |
| `remove_entity` | Remove entity from world |

### Voxel World Building

| Tool | Description |
|---|---|
| `place_voxel` | Place a single cube at (x,y,z) with optional material |
| `remove_voxel` | Remove a cube at (x,y,z) |
| `query_voxel` | Check what's at (x,y,z) |
| `place_voxels_batch` | Place many cubes in one call |

### Templates

| Tool | Description |
|---|---|
| `list_templates` | List available object templates |
| `spawn_template` | Place a pre-built template (castle, tree, etc.) |

### Camera & Scripting

| Tool | Description |
|---|---|
| `set_camera` | Move/rotate the camera |
| `run_script` | Execute arbitrary Python in the engine |

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

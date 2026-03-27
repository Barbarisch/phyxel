#!/usr/bin/env python3
"""
Phyxel Engine MCP Server — Claude Code / AI Agent Bridge

An MCP (Model Context Protocol) server that connects AI coding agents
(Claude Code, Goose, etc.) to the running Phyxel game engine via the
HTTP API on localhost:8090.

Unlike the original phyxel_extension.py (which maintained local state
and never connected to the engine), this server makes real HTTP calls
to the EngineAPIServer running inside the game process.

Setup for Claude Code:
  Add to ~/.claude/claude_code_config.json:
  {
    "mcpServers": {
      "phyxel": {
        "command": "python",
        "args": ["scripts/mcp/phyxel_mcp_server.py"],
        "cwd": "<path-to-phyxel-repo>"
      }
    }
  }

Requirements:
  pip install mcp httpx

The game must be running (phyxel.exe) for tools to work.
"""

import json
import sys
import os
import logging
import subprocess
import asyncio
import httpx
from typing import Any
from pathlib import Path

# MCP SDK imports
from mcp.server import Server
from mcp.server.stdio import stdio_server
from mcp.types import Tool, TextContent

# ============================================================================
# Configuration
# ============================================================================

ENGINE_API_URL = os.environ.get("PHYXEL_API_URL", "http://localhost:8090")
ENGINE_API_TIMEOUT = float(os.environ.get("PHYXEL_API_TIMEOUT", "10"))

# Logging to stderr (stdout is MCP protocol)
logging.basicConfig(
    stream=sys.stderr,
    level=logging.INFO,
    format="[phyxel-mcp] %(levelname)s: %(message)s"
)
logger = logging.getLogger("phyxel_mcp")

# ============================================================================
# HTTP Client (async — must not block the MCP event loop)
# ============================================================================

http_client = httpx.AsyncClient(base_url=ENGINE_API_URL, timeout=ENGINE_API_TIMEOUT)


async def api_get(path: str, params: dict | None = None) -> dict:
    """GET request to the engine API. Returns parsed JSON."""
    try:
        resp = await http_client.get(path, params=params)
        resp.raise_for_status()
        return resp.json()
    except httpx.ConnectError:
        return {"error": f"Engine not running. Start phyxel.exe first. (tried {ENGINE_API_URL}{path})"}
    except Exception as e:
        return {"error": str(e)}


async def api_post(path: str, body: dict) -> dict:
    """POST request to the engine API. Returns parsed JSON."""
    try:
        resp = await http_client.post(path, json=body)
        resp.raise_for_status()
        return resp.json()
    except httpx.ConnectError:
        return {"error": f"Engine not running. Start phyxel.exe first. (tried {ENGINE_API_URL}{path})"}
    except Exception as e:
        return {"error": str(e)}


ASYNC_POLL_INTERVAL = 1.0    # seconds between polls
ASYNC_POLL_TIMEOUT  = 300.0  # max seconds to wait for async result


async def api_post_async(path: str, body: dict) -> dict:
    """POST to an async endpoint and poll until the result is ready.

    The engine returns {"status": "accepted", "async_id": "..."} immediately.
    We then poll GET /api/async/:id until status becomes "complete".
    """
    initial = await api_post(path, body)
    if "error" in initial:
        return initial
    if initial.get("status") != "accepted" or "async_id" not in initial:
        # Not an async response — return as-is (backwards compat)
        return initial

    async_id = initial["async_id"]
    elapsed = 0.0
    while elapsed < ASYNC_POLL_TIMEOUT:
        await asyncio.sleep(ASYNC_POLL_INTERVAL)
        elapsed += ASYNC_POLL_INTERVAL
        poll = await api_get(f"/api/async/{async_id}")
        if "error" in poll:
            return poll
        if poll.get("status") == "complete":
            return poll
        # Still processing — continue polling
    return {"error": f"Async operation timed out after {ASYNC_POLL_TIMEOUT}s", "async_id": async_id}


# ============================================================================
# MCP Server Setup
# ============================================================================

server = Server("phyxel-engine")


@server.list_tools()
async def list_tools() -> list[Tool]:
    """Return all available engine tools."""
    return [
        # ================================================================
        # Status & State
        # ================================================================
        Tool(
            name="engine_status",
            description="Check if the Phyxel game engine is running and responsive.",
            inputSchema={
                "type": "object",
                "properties": {},
                "required": []
            }
        ),
        Tool(
            name="get_world_state",
            description="Get a full snapshot of the game world: all entities (with positions and types), camera position/orientation, and entity count.",
            inputSchema={
                "type": "object",
                "properties": {},
                "required": []
            }
        ),
        Tool(
            name="get_camera",
            description="Get the current camera position, orientation (yaw/pitch), and front direction vector.",
            inputSchema={
                "type": "object",
                "properties": {},
                "required": []
            }
        ),

        # ================================================================
        # Entity Management
        # ================================================================
        Tool(
            name="list_entities",
            description="List all registered entities in the world with their IDs, positions, and types.",
            inputSchema={
                "type": "object",
                "properties": {},
                "required": []
            }
        ),
        Tool(
            name="get_entity",
            description="Get detailed information about a specific entity: position, rotation, scale, debug color.",
            inputSchema={
                "type": "object",
                "properties": {
                    "id": {"type": "string", "description": "Entity ID to look up"}
                },
                "required": ["id"]
            }
        ),
        Tool(
            name="spawn_entity",
            description="Spawn a new game entity (character) in the world. Types: 'physics' (humanoid with physics), 'spider' (spider enemy), 'animated' (animated voxel character).",
            inputSchema={
                "type": "object",
                "properties": {
                    "type": {
                        "type": "string",
                        "description": "Entity type to spawn",
                        "enum": ["physics", "spider", "animated"]
                    },
                    "x": {"type": "number", "description": "Spawn X coordinate"},
                    "y": {"type": "number", "description": "Spawn Y coordinate"},
                    "z": {"type": "number", "description": "Spawn Z coordinate"},
                    "id": {"type": "string", "description": "Optional ID to assign (auto-generated if omitted)"},
                    "animFile": {"type": "string", "description": "Animation file for 'animated' type (default: character.anim)"}
                },
                "required": ["type", "x", "y", "z"]
            }
        ),
        Tool(
            name="move_entity",
            description="Teleport an entity to a new position instantly.",
            inputSchema={
                "type": "object",
                "properties": {
                    "id": {"type": "string", "description": "Entity ID to move"},
                    "x": {"type": "number", "description": "Target X coordinate"},
                    "y": {"type": "number", "description": "Target Y coordinate"},
                    "z": {"type": "number", "description": "Target Z coordinate"}
                },
                "required": ["id", "x", "y", "z"]
            }
        ),
        Tool(
            name="remove_entity",
            description="Remove an entity from the world.",
            inputSchema={
                "type": "object",
                "properties": {
                    "id": {"type": "string", "description": "Entity ID to remove"}
                },
                "required": ["id"]
            }
        ),

        # ================================================================
        # Voxel World Building
        # ================================================================
        Tool(
            name="place_voxel",
            description="Place a single voxel (cube) at world coordinates. Optionally specify a material.",
            inputSchema={
                "type": "object",
                "properties": {
                    "x": {"type": "integer", "description": "X coordinate"},
                    "y": {"type": "integer", "description": "Y coordinate"},
                    "z": {"type": "integer", "description": "Z coordinate"},
                    "material": {"type": "string", "description": "Material name (e.g., 'stone', 'wood', 'dirt')"}
                },
                "required": ["x", "y", "z"]
            }
        ),
        Tool(
            name="remove_voxel",
            description="Remove a voxel (cube) at world coordinates.",
            inputSchema={
                "type": "object",
                "properties": {
                    "x": {"type": "integer", "description": "X coordinate"},
                    "y": {"type": "integer", "description": "Y coordinate"},
                    "z": {"type": "integer", "description": "Z coordinate"}
                },
                "required": ["x", "y", "z"]
            }
        ),
        Tool(
            name="query_voxel",
            description="Check if a voxel exists at the given world coordinates.",
            inputSchema={
                "type": "object",
                "properties": {
                    "x": {"type": "integer", "description": "X coordinate"},
                    "y": {"type": "integer", "description": "Y coordinate"},
                    "z": {"type": "integer", "description": "Z coordinate"}
                },
                "required": ["x", "y", "z"]
            }
        ),
        Tool(
            name="place_voxels_batch",
            description="Place multiple voxels at once. More efficient than calling place_voxel repeatedly. Pass an array of voxel positions with optional materials.",
            inputSchema={
                "type": "object",
                "properties": {
                    "voxels": {
                        "type": "array",
                        "description": "Array of voxels to place",
                        "items": {
                            "type": "object",
                            "properties": {
                                "x": {"type": "integer"},
                                "y": {"type": "integer"},
                                "z": {"type": "integer"},
                                "material": {"type": "string"}
                            },
                            "required": ["x", "y", "z"]
                        }
                    }
                },
                "required": ["voxels"]
            }
        ),

        # ================================================================
        # Templates
        # ================================================================
        Tool(
            name="list_templates",
            description="List all available voxel object templates (e.g., 'castle', 'tree', 'sphere') that can be spawned in the world.",
            inputSchema={
                "type": "object",
                "properties": {},
                "required": []
            }
        ),
        Tool(
            name="spawn_template",
            description="Spawn a pre-built voxel object template at a world position. Use list_templates first to see available templates.",
            inputSchema={
                "type": "object",
                "properties": {
                    "name": {"type": "string", "description": "Template name (e.g., 'castle', 'tree')"},
                    "x": {"type": "number", "description": "X position"},
                    "y": {"type": "number", "description": "Y position"},
                    "z": {"type": "number", "description": "Z position"},
                    "static": {"type": "boolean", "description": "If true, merges into terrain. If false, creates physics objects.", "default": True}
                },
                "required": ["name", "x", "y", "z"]
            }
        ),

        # ================================================================
        # Camera Control
        # ================================================================
        Tool(
            name="set_camera",
            description="Move the camera to a specific position and/or orientation.",
            inputSchema={
                "type": "object",
                "properties": {
                    "x": {"type": "number", "description": "Camera X position"},
                    "y": {"type": "number", "description": "Camera Y position"},
                    "z": {"type": "number", "description": "Camera Z position"},
                    "yaw": {"type": "number", "description": "Camera yaw (horizontal rotation in degrees)"},
                    "pitch": {"type": "number", "description": "Camera pitch (vertical rotation in degrees)"}
                },
                "required": []
            }
        ),

        # ================================================================
        # Scripting
        # ================================================================
        Tool(
            name="run_script",
            description="Execute Python code inside the running engine. Has access to the full phyxel Python API: phyxel.get_app(), phyxel.Logger, etc. Use this for operations not covered by other tools.",
            inputSchema={
                "type": "object",
                "properties": {
                    "code": {"type": "string", "description": "Python code to execute in the engine's embedded interpreter"}
                },
                "required": ["code"]
            }
        ),

        # ================================================================
        # Region Filling
        # ================================================================
        Tool(
            name="fill_region",
            description="Fill a 3D box region with voxels. Specify two opposite corners. Optionally make it hollow (shell only). Max 100,000 voxels per call.",
            inputSchema={
                "type": "object",
                "properties": {
                    "x1": {"type": "integer", "description": "First corner X"},
                    "y1": {"type": "integer", "description": "First corner Y"},
                    "z1": {"type": "integer", "description": "First corner Z"},
                    "x2": {"type": "integer", "description": "Opposite corner X"},
                    "y2": {"type": "integer", "description": "Opposite corner Y"},
                    "z2": {"type": "integer", "description": "Opposite corner Z"},
                    "material": {"type": "string", "description": "Material name (e.g. 'Stone', 'Wood', 'Metal', 'Glass', 'glow'). Use list_materials to see all."},
                    "hollow": {"type": "boolean", "description": "If true, only fill the outer shell (walls/floor/ceiling)", "default": False}
                },
                "required": ["x1", "y1", "z1", "x2", "y2", "z2"]
            }
        ),

        # ================================================================
        # Materials
        # ================================================================
        Tool(
            name="list_materials",
            description="List all available voxel materials with their physical properties (mass, friction, restitution, color tint, metallic, roughness).",
            inputSchema={
                "type": "object",
                "properties": {},
                "required": []
            }
        ),

        # ================================================================
        # Chunk Info
        # ================================================================
        Tool(
            name="get_chunk_info",
            description="Get information about loaded chunks: count, world origins, cube counts per chunk, and rendering statistics.",
            inputSchema={
                "type": "object",
                "properties": {},
                "required": []
            }
        ),

        # ================================================================
        # Entity Property Update
        # ================================================================
        Tool(
            name="update_entity",
            description="Update one or more properties of an entity: position, rotation (quaternion), scale, and debug color. Only provided fields are changed.",
            inputSchema={
                "type": "object",
                "properties": {
                    "id": {"type": "string", "description": "Entity ID"},
                    "x": {"type": "number", "description": "New X position"},
                    "y": {"type": "number", "description": "New Y position"},
                    "z": {"type": "number", "description": "New Z position"},
                    "rotation_w": {"type": "number", "description": "Quaternion W"},
                    "rotation_x": {"type": "number", "description": "Quaternion X"},
                    "rotation_y": {"type": "number", "description": "Quaternion Y"},
                    "rotation_z": {"type": "number", "description": "Quaternion Z"},
                    "scale_x": {"type": "number", "description": "Scale X"},
                    "scale_y": {"type": "number", "description": "Scale Y"},
                    "scale_z": {"type": "number", "description": "Scale Z"},
                    "color_r": {"type": "number", "description": "Debug color red (0-1)"},
                    "color_g": {"type": "number", "description": "Debug color green (0-1)"},
                    "color_b": {"type": "number", "description": "Debug color blue (0-1)"},
                    "color_a": {"type": "number", "description": "Debug color alpha (0-1)"}
                },
                "required": ["id"]
            }
        ),

        # ================================================================
        # Screenshot / Observation
        # ================================================================
        Tool(
            name="screenshot",
            description="Capture a screenshot of the current game view. Returns the path to the saved PNG file along with dimensions. Use this to observe the visual result of your actions.",
            inputSchema={
                "type": "object",
                "properties": {},
                "required": []
            }
        ),

        # ================================================================
        # Region Scanning
        # ================================================================
        Tool(
            name="scan_region",
            description="Scan a 3D region and return all voxels within it. Returns position, material, and visibility for each occupied voxel. Max 100k volume. Use to understand what's in a region before modifying it.",
            inputSchema={
                "type": "object",
                "properties": {
                    "x1": {"type": "integer", "description": "First corner X"},
                    "y1": {"type": "integer", "description": "First corner Y"},
                    "z1": {"type": "integer", "description": "First corner Z"},
                    "x2": {"type": "integer", "description": "Opposite corner X"},
                    "y2": {"type": "integer", "description": "Opposite corner Y"},
                    "z2": {"type": "integer", "description": "Opposite corner Z"}
                },
                "required": ["x1", "y1", "z1", "x2", "y2", "z2"]
            }
        ),

        # ================================================================
        # Region Clearing
        # ================================================================
        Tool(
            name="clear_region",
            description="Remove all voxels in a 3D bounding box. Max 100k volume. Returns count of removed and skipped voxels.",
            inputSchema={
                "type": "object",
                "properties": {
                    "x1": {"type": "integer", "description": "First corner X"},
                    "y1": {"type": "integer", "description": "First corner Y"},
                    "z1": {"type": "integer", "description": "First corner Z"},
                    "x2": {"type": "integer", "description": "Opposite corner X"},
                    "y2": {"type": "integer", "description": "Opposite corner Y"},
                    "z2": {"type": "integer", "description": "Opposite corner Z"}
                },
                "required": ["x1", "y1", "z1", "x2", "y2", "z2"]
            }
        ),

        # ================================================================
        # World Persistence
        # ================================================================
        Tool(
            name="save_world",
            description="Save the current world state to the SQLite database. By default saves only modified (dirty) chunks. Set 'all' to true to force saving every chunk.",
            inputSchema={
                "type": "object",
                "properties": {
                    "all": {"type": "boolean", "description": "Save all chunks (true) or only dirty chunks (false, default)", "default": False}
                },
                "required": []
            }
        ),

        # ================================================================
        # Event Polling
        # ================================================================
        Tool(
            name="poll_events",
            description="Poll game events since a cursor. Returns entity spawned/removed/moved/updated, voxel placed/removed, region filled/cleared, and world save events. Pass the 'cursor' value from the previous response to get only new events. Start with cursor=0 to get all buffered events (up to 1000).",
            inputSchema={
                "type": "object",
                "properties": {
                    "cursor": {"type": "integer", "description": "Event cursor from last poll (0 for first poll)", "default": 0}
                },
                "required": []
            }
        ),

        # ================================================================
        # Snapshots (undo/redo)
        # ================================================================
        Tool(
            name="create_snapshot",
            description="Create a named snapshot of a voxel region for undo. Captures all voxels (with materials) in the bounding box. Use before making destructive changes. Restore with restore_snapshot. Max region 100k voxels, max 50 snapshots (oldest auto-evicted).",
            inputSchema={
                "type": "object",
                "properties": {
                    "name": {"type": "string", "description": "Unique name for this snapshot (e.g. 'before_castle')"},
                    "x1": {"type": "integer", "description": "First corner X"},
                    "y1": {"type": "integer", "description": "First corner Y"},
                    "z1": {"type": "integer", "description": "First corner Z"},
                    "x2": {"type": "integer", "description": "Opposite corner X"},
                    "y2": {"type": "integer", "description": "Opposite corner Y"},
                    "z2": {"type": "integer", "description": "Opposite corner Z"}
                },
                "required": ["name", "x1", "y1", "z1", "x2", "y2", "z2"]
            }
        ),
        Tool(
            name="restore_snapshot",
            description="Restore a previously created snapshot, undoing changes to that region. Clears the region first, then places all voxels from the snapshot with their original materials.",
            inputSchema={
                "type": "object",
                "properties": {
                    "name": {"type": "string", "description": "Name of the snapshot to restore"}
                },
                "required": ["name"]
            }
        ),
        Tool(
            name="list_snapshots",
            description="List all stored snapshots with metadata (name, region bounds, voxel count). Does not include voxel data.",
            inputSchema={
                "type": "object",
                "properties": {},
                "required": []
            }
        ),
        Tool(
            name="delete_snapshot",
            description="Delete a named snapshot to free memory.",
            inputSchema={
                "type": "object",
                "properties": {
                    "name": {"type": "string", "description": "Name of the snapshot to delete"}
                },
                "required": ["name"]
            }
        ),

        # ================================================================
        # Clipboard (copy / paste regions)
        # ================================================================
        Tool(
            name="copy_region",
            description="Copy a voxel region into the clipboard. Captures all voxels with their materials as relative offsets from the min corner. Use paste_region to place the copied data at a new location. Max region 100k voxels.",
            inputSchema={
                "type": "object",
                "properties": {
                    "x1": {"type": "integer", "description": "First corner X"},
                    "y1": {"type": "integer", "description": "First corner Y"},
                    "z1": {"type": "integer", "description": "First corner Z"},
                    "x2": {"type": "integer", "description": "Opposite corner X"},
                    "y2": {"type": "integer", "description": "Opposite corner Y"},
                    "z2": {"type": "integer", "description": "Opposite corner Z"}
                },
                "required": ["x1", "y1", "z1", "x2", "y2", "z2"]
            }
        ),
        Tool(
            name="paste_region",
            description="Paste the clipboard contents at a new world position. Optionally rotate by 0/90/180/270 degrees clockwise around the Y axis. The clipboard min corner maps to the given position. Does NOT clear the destination first — use clear_region beforehand if needed.",
            inputSchema={
                "type": "object",
                "properties": {
                    "x": {"type": "integer", "description": "Destination X (maps to clipboard min corner)"},
                    "y": {"type": "integer", "description": "Destination Y"},
                    "z": {"type": "integer", "description": "Destination Z"},
                    "rotate": {"type": "integer", "description": "Rotation in degrees: 0, 90, 180, or 270 (clockwise around Y)", "default": 0}
                },
                "required": ["x", "y", "z"]
            }
        ),
        Tool(
            name="get_clipboard",
            description="Check the clipboard status: whether it has data, its size, and voxel count.",
            inputSchema={
                "type": "object",
                "properties": {},
                "required": []
            }
        ),

        # ================================================================
        # World Generation
        # ================================================================
        Tool(
            name="generate_world",
            description="Generate procedural terrain for one or more chunks. Types: Random (70% fill), Perlin (height-map, base Y=16), Flat (solid below Y=16), Mountains (multi-octave, peaks ~60), Caves (Perlin + cave carving), City (flat ground + buildings). Specify chunks as a list of chunk coordinates OR a from/to range. Max 64 chunks per call. Chunk coordinates are world_pos/32 (e.g., chunk {0,0,0} covers world 0-31, chunk {1,0,0} covers 32-63).",
            inputSchema={
                "type": "object",
                "properties": {
                    "type": {"type": "string", "description": "Generation type: Random, Perlin, Flat, Mountains, Caves, or City", "enum": ["Random", "Perlin", "Flat", "Mountains", "Caves", "City"]},
                    "seed": {"type": "integer", "description": "Random seed (0 for default)", "default": 0},
                    "chunks": {"type": "array", "description": "List of chunk coordinates [{x,y,z}, ...]", "items": {"type": "object", "properties": {"x": {"type": "integer"}, "y": {"type": "integer"}, "z": {"type": "integer"}}}},
                    "from": {"type": "object", "description": "Range start chunk coord {x,y,z} (use with 'to')", "properties": {"x": {"type": "integer"}, "y": {"type": "integer"}, "z": {"type": "integer"}}},
                    "to": {"type": "object", "description": "Range end chunk coord {x,y,z} (use with 'from')", "properties": {"x": {"type": "integer"}, "y": {"type": "integer"}, "z": {"type": "integer"}}},
                    "params": {"type": "object", "description": "Optional terrain parameters", "properties": {
                        "heightScale": {"type": "number", "description": "Max terrain height (default 16)"},
                        "frequency": {"type": "number", "description": "Noise frequency (default 0.05)"},
                        "octaves": {"type": "integer", "description": "Noise octaves (default 4)"},
                        "persistence": {"type": "number", "description": "Octave contribution (default 0.5)"},
                        "lacunarity": {"type": "number", "description": "Frequency multiplier per octave (default 2.0)"},
                        "caveThreshold": {"type": "number", "description": "Cave threshold (default 0.3)"},
                        "stoneLevel": {"type": "number", "description": "Stone level Y (default 8)"}
                    }}
                },
                "required": ["type"]
            }
        ),

        # ================================================================
        # Template Creation
        # ================================================================
        Tool(
            name="save_template",
            description="Save a voxel region as a reusable .txt template file. Scans all cubes in the bounding box, converts to relative coordinates, and writes to resources/templates/<name>.txt. The template is immediately available for spawn_template. Max 100k voxels.",
            inputSchema={
                "type": "object",
                "properties": {
                    "name": {"type": "string", "description": "Template name (becomes filename, no extension)"},
                    "x1": {"type": "integer", "description": "First corner X"},
                    "y1": {"type": "integer", "description": "First corner Y"},
                    "z1": {"type": "integer", "description": "First corner Z"},
                    "x2": {"type": "integer", "description": "Opposite corner X"},
                    "y2": {"type": "integer", "description": "Opposite corner Y"},
                    "z2": {"type": "integer", "description": "Opposite corner Z"}
                },
                "required": ["name", "x1", "y1", "z1", "x2", "y2", "z2"]
            }
        ),

        # ================================================================
        # NPC Management
        # ================================================================
        Tool(
            name="spawn_npc",
            description="Spawn an NPC with an animated voxel character. NPCs can have idle or patrol behaviors and can be interacted with using the E key. Supports dialogue trees via set_npc_dialogue.",
            inputSchema={
                "type": "object",
                "properties": {
                    "name": {"type": "string", "description": "Unique NPC name/identifier"},
                    "animFile": {"type": "string", "description": "Animation file (default: character.anim)", "default": "character.anim"},
                    "position": {"type": "object", "description": "Spawn position {x,y,z}", "properties": {
                        "x": {"type": "number"}, "y": {"type": "number"}, "z": {"type": "number"}
                    }},
                    "behavior": {"type": "string", "description": "Behavior type: idle or patrol", "enum": ["idle", "patrol"], "default": "idle"},
                    "waypoints": {"type": "array", "description": "Patrol waypoints [{x,y,z}, ...] (required for patrol)", "items": {
                        "type": "object", "properties": {"x": {"type": "number"}, "y": {"type": "number"}, "z": {"type": "number"}}
                    }},
                    "walkSpeed": {"type": "number", "description": "Walk speed for patrol (default 2.0)", "default": 2.0},
                    "waitTime": {"type": "number", "description": "Wait time at waypoints (default 2.0)", "default": 2.0}
                },
                "required": ["name"]
            }
        ),
        Tool(
            name="remove_npc",
            description="Remove an NPC from the world by name.",
            inputSchema={
                "type": "object",
                "properties": {
                    "name": {"type": "string", "description": "NPC name to remove"}
                },
                "required": ["name"]
            }
        ),
        Tool(
            name="list_npcs",
            description="List all NPCs in the world with their positions, behaviors, and interaction radii.",
            inputSchema={"type": "object", "properties": {}, "required": []}
        ),
        Tool(
            name="set_npc_behavior",
            description="Change an NPC's behavior (idle or patrol with waypoints).",
            inputSchema={
                "type": "object",
                "properties": {
                    "name": {"type": "string", "description": "NPC name"},
                    "behavior": {"type": "string", "description": "Behavior type: idle or patrol", "enum": ["idle", "patrol"]},
                    "waypoints": {"type": "array", "description": "Patrol waypoints (for patrol behavior)", "items": {
                        "type": "object", "properties": {"x": {"type": "number"}, "y": {"type": "number"}, "z": {"type": "number"}}
                    }},
                    "walkSpeed": {"type": "number", "description": "Walk speed", "default": 2.0},
                    "waitTime": {"type": "number", "description": "Wait time at waypoints", "default": 2.0}
                },
                "required": ["name", "behavior"]
            }
        ),
        Tool(
            name="get_npc_appearance",
            description="Get the current appearance (colors and proportions) of an NPC. Returns heightScale, bulkScale, headScale, armLengthScale, legLengthScale, torsoLengthScale, shoulderWidthScale, and color values.",
            inputSchema={
                "type": "object",
                "properties": {
                    "name": {"type": "string", "description": "NPC name"}
                },
                "required": ["name"]
            }
        ),
        Tool(
            name="set_npc_appearance",
            description=(
                "Set or update appearance (colors and proportions) of an existing NPC. "
                "Only provided fields are changed; others keep their current values. "
                "Proportion changes rebuild the character's skeleton in real-time. "
                "Fields: heightScale (0.4-1.6), bulkScale (0.5-1.8), headScale (0.6-1.6), "
                "armLengthScale (0.5-1.5), legLengthScale (0.5-1.5), torsoLengthScale (0.6-1.5), "
                "shoulderWidthScale (0.5-1.6), "
                "skinColor/torsoColor/armColor/legColor ({r,g,b,a} 0.0-1.0)."
            ),
            inputSchema={
                "type": "object",
                "properties": {
                    "name": {"type": "string", "description": "NPC name"},
                    "appearance": {
                        "type": "object",
                        "description": "Partial appearance object — only include fields you want to change",
                        "properties": {
                            "heightScale": {"type": "number"},
                            "bulkScale": {"type": "number"},
                            "headScale": {"type": "number"},
                            "armLengthScale": {"type": "number"},
                            "legLengthScale": {"type": "number"},
                            "torsoLengthScale": {"type": "number"},
                            "shoulderWidthScale": {"type": "number"},
                            "skinColor": {"type": "object", "properties": {"r": {"type": "number"}, "g": {"type": "number"}, "b": {"type": "number"}, "a": {"type": "number"}}},
                            "torsoColor": {"type": "object", "properties": {"r": {"type": "number"}, "g": {"type": "number"}, "b": {"type": "number"}, "a": {"type": "number"}}},
                            "armColor": {"type": "object", "properties": {"r": {"type": "number"}, "g": {"type": "number"}, "b": {"type": "number"}, "a": {"type": "number"}}},
                            "legColor": {"type": "object", "properties": {"r": {"type": "number"}, "g": {"type": "number"}, "b": {"type": "number"}, "a": {"type": "number"}}}
                        }
                    }
                },
                "required": ["name", "appearance"]
            }
        ),

        # ================================================================
        # Dialogue & Speech Bubbles
        # ================================================================
        Tool(
            name="set_npc_dialogue",
            description="Assign a dialogue tree (JSON) to an NPC. When a player presses E near this NPC, the dialogue will play in the RPG dialogue box at the bottom of the screen. Dialogue trees have nodes with speaker, text, optional choices, and navigation.",
            inputSchema={
                "type": "object",
                "properties": {
                    "name": {"type": "string", "description": "NPC name to assign dialogue to"},
                    "tree": {
                        "type": "object",
                        "description": "Dialogue tree JSON: {id, startNodeId, nodes: [{id, speaker, text, emotion?, nextNodeId?, choices?: [{text, targetNodeId}]}]}",
                        "properties": {
                            "id": {"type": "string"},
                            "startNodeId": {"type": "string"},
                            "nodes": {"type": "array", "items": {"type": "object"}}
                        },
                        "required": ["id", "startNodeId", "nodes"]
                    }
                },
                "required": ["name", "tree"]
            }
        ),
        Tool(
            name="start_dialogue",
            description="Immediately start a dialogue conversation. If the named NPC exists, their dialogue tree is used. Otherwise a standalone dialogue is started with the provided tree. The RPG dialogue box appears at the bottom of the screen with typewriter text reveal. Player uses Enter to advance, 1-4 for choices, Esc to end.",
            inputSchema={
                "type": "object",
                "properties": {
                    "npc": {"type": "string", "description": "Speaker name (or NPC name if NPC exists)"},
                    "tree": {
                        "type": "object",
                        "description": "Dialogue tree JSON",
                        "properties": {
                            "id": {"type": "string"},
                            "startNodeId": {"type": "string"},
                            "nodes": {"type": "array", "items": {"type": "object"}}
                        },
                        "required": ["id", "startNodeId", "nodes"]
                    }
                },
                "required": ["npc", "tree"]
            }
        ),
        Tool(
            name="end_dialogue",
            description="End the currently active dialogue conversation.",
            inputSchema={"type": "object", "properties": {}, "required": []}
        ),
        Tool(
            name="say_bubble",
            description="Show a floating speech bubble above an entity. The bubble fades out after the specified duration. Great for ambient NPC chatter or quick messages.",
            inputSchema={
                "type": "object",
                "properties": {
                    "entityId": {"type": "string", "description": "Entity registry ID (e.g. 'npc_Guard')"},
                    "text": {"type": "string", "description": "Text to display in the bubble"},
                    "duration": {"type": "number", "description": "How long the bubble lasts in seconds (default 3.0)", "default": 3.0}
                },
                "required": ["entityId", "text"]
            }
        ),
        Tool(
            name="get_dialogue_state",
            description="Get the current state of the dialogue system. Returns whether a conversation is active, current speaker, text, revealed text (typewriter progress), emotion, and available choices.",
            inputSchema={"type": "object", "properties": {}, "required": []}
        ),
        Tool(
            name="advance_dialogue",
            description="Advance the active dialogue: skip the typewriter effect or move to the next node. Equivalent to pressing Enter.",
            inputSchema={"type": "object", "properties": {}, "required": []}
        ),
        Tool(
            name="select_dialogue_choice",
            description="Select a dialogue choice by index (0-based) during choice selection state.",
            inputSchema={
                "type": "object",
                "properties": {
                    "index": {"type": "integer", "description": "Choice index (0-based)"}
                },
                "required": ["index"]
            }
        ),
        Tool(
            name="load_dialogue_file",
            description="Load a dialogue tree from a JSON file in resources/dialogues/. Optionally assign it to an NPC. If no NPC is specified, returns the parsed tree.",
            inputSchema={
                "type": "object",
                "properties": {
                    "filename": {"type": "string", "description": "JSON filename (e.g. 'guard_intro.json')"},
                    "npc": {"type": "string", "description": "Optional NPC name to assign the dialogue to"}
                },
                "required": ["filename"]
            }
        ),
        Tool(
            name="list_dialogue_files",
            description="List all available dialogue JSON files in resources/dialogues/.",
            inputSchema={"type": "object", "properties": {}, "required": []}
        ),

        # ================================================================
        # Story System
        # ================================================================
        Tool(
            name="story_get_state",
            description="Get the full story engine state including all characters, arcs, world state, and memories.",
            inputSchema={"type": "object", "properties": {}, "required": []}
        ),
        Tool(
            name="story_list_characters",
            description="List all story characters with their IDs, names, and factions.",
            inputSchema={"type": "object", "properties": {}, "required": []}
        ),
        Tool(
            name="story_get_character",
            description="Get detailed info about a specific story character including profile, memory, and emotional state.",
            inputSchema={
                "type": "object",
                "properties": {
                    "id": {"type": "string", "description": "Character ID"}
                },
                "required": ["id"]
            }
        ),
        Tool(
            name="story_list_arcs",
            description="List all story arcs with their current status.',",
            inputSchema={"type": "object", "properties": {}, "required": []}
        ),
        Tool(
            name="story_get_arc",
            description="Get detailed info about a specific story arc including beats and tension.",
            inputSchema={
                "type": "object",
                "properties": {
                    "id": {"type": "string", "description": "Arc ID"}
                },
                "required": ["id"]
            }
        ),
        Tool(
            name="story_get_world",
            description="Get the story world state: factions, locations, and variables.",
            inputSchema={"type": "object", "properties": {}, "required": []}
        ),
        Tool(
            name="story_load_world",
            description="Load a full world definition from JSON. Defines factions, characters, story arcs, and world variables in one call.",
            inputSchema={
                "type": "object",
                "properties": {
                    "definition": {"type": "object", "description": "Full world definition JSON object with world, characters, and storyArcs sections"}
                },
                "required": ["definition"]
            }
        ),
        Tool(
            name="story_add_character",
            description="Add a single character to the story engine.",
            inputSchema={
                "type": "object",
                "properties": {
                    "id": {"type": "string", "description": "Unique character ID"},
                    "name": {"type": "string", "description": "Character display name"},
                    "faction": {"type": "string", "description": "Faction ID (optional)"},
                    "agencyLevel": {"type": "integer", "description": "0=Scripted, 1=Templated, 2=Guided, 3=Autonomous"},
                    "traits": {"type": "object", "description": "Big Five personality traits (openness, conscientiousness, extraversion, agreeableness, neuroticism) from 0.0-1.0"},
                    "goals": {"type": "array", "items": {"type": "object"}, "description": "Character goals with id, description, priority"}
                },
                "required": ["id", "name"]
            }
        ),
        Tool(
            name="story_remove_character",
            description="Remove a character from the story engine.",
            inputSchema={
                "type": "object",
                "properties": {
                    "id": {"type": "string", "description": "Character ID to remove"}
                },
                "required": ["id"]
            }
        ),
        Tool(
            name="story_trigger_event",
            description="Trigger a world event in the story engine. Events propagate to characters and can advance story beats.",
            inputSchema={
                "type": "object",
                "properties": {
                    "type": {"type": "string", "description": "Event type name"},
                    "data": {"type": "object", "description": "Event data as key-value pairs"},
                    "location": {"type": "string", "description": "Location ID where event occurs (optional)"},
                    "participants": {"type": "array", "items": {"type": "string"}, "description": "Character IDs involved (optional)"}
                },
                "required": ["type"]
            }
        ),
        Tool(
            name="story_add_arc",
            description="Add a story arc with beats and constraint mode.",
            inputSchema={
                "type": "object",
                "properties": {
                    "id": {"type": "string", "description": "Unique arc ID"},
                    "name": {"type": "string", "description": "Arc display name"},
                    "constraintMode": {"type": "string", "description": "Scripted, Guided, Emergent, or Freeform"},
                    "beats": {"type": "array", "items": {"type": "object"}, "description": "Story beats with id, type, conditions"},
                    "tensionCurve": {"type": "array", "items": {"type": "number"}, "description": "Tension values over arc progress (optional)"}
                },
                "required": ["id", "name"]
            }
        ),
        Tool(
            name="story_set_variable",
            description="Set a world variable in the story engine.",
            inputSchema={
                "type": "object",
                "properties": {
                    "name": {"type": "string", "description": "Variable name"},
                    "value": {"description": "Variable value (bool, int, float, or string)"}
                },
                "required": ["name", "value"]
            }
        ),
        Tool(
            name="story_set_agency",
            description="Set a character's agency level (how autonomous their behavior is).",
            inputSchema={
                "type": "object",
                "properties": {
                    "id": {"type": "string", "description": "Character ID"},
                    "level": {"type": "integer", "description": "0=Scripted, 1=Templated, 2=Guided, 3=Autonomous"}
                },
                "required": ["id", "level"]
            }
        ),
        Tool(
            name="story_add_knowledge",
            description="Add a piece of knowledge to a character's memory.",
            inputSchema={
                "type": "object",
                "properties": {
                    "characterId": {"type": "string", "description": "Character ID"},
                    "fact": {"type": "string", "description": "Knowledge fact text"},
                    "category": {"type": "string", "description": "Knowledge category (optional)"}
                },
                "required": ["characterId", "fact"]
            }
        ),

        # ================================================================
        # GAME DEFINITION (AI Game Development)
        # ================================================================

        Tool(
            name="load_game_definition",
            description=(
                "Load a complete game from a single JSON definition. This is the primary tool for AI-driven "
                "game creation. Provide a game definition with world generation, structures, player, NPCs "
                "(with dialogue and story roles), camera setup, and story arcs — all in one call. "
                "Sections: name, description, version, world, structures, player, camera, npcs, story. "
                "All sections are optional. Example: {\"name\": \"My Game\", \"world\": {\"type\": \"Perlin\", "
                "\"from\": {\"x\":-1,\"y\":0,\"z\":-1}, \"to\": {\"x\":1,\"y\":0,\"z\":1}}, "
                "\"player\": {\"type\": \"physics\", \"position\": {\"x\":16,\"y\":20,\"z\":16}}, "
                "\"npcs\": [{\"name\": \"Guard\", \"position\": {\"x\":10,\"y\":20,\"z\":10}, "
                "\"dialogue\": {\"id\":\"talk\", \"startNodeId\":\"start\", "
                "\"nodes\": [{\"id\":\"start\",\"speaker\":\"Guard\",\"text\":\"Hello!\"}]}}]}"
            ),
            inputSchema={
                "type": "object",
                "properties": {
                    "name": {"type": "string", "description": "Game name"},
                    "description": {"type": "string", "description": "Game description"},
                    "version": {"type": "string", "description": "Version string"},
                    "world": {
                        "type": "object",
                        "description": "World generation config. type: Perlin|Flat|Mountains|Caves|City|Random. "
                                       "Specify chunks as array of {x,y,z} or from/to range. Max 64 chunks.",
                        "properties": {
                            "type": {"type": "string", "enum": ["Perlin", "Flat", "Mountains", "Caves", "City", "Random"]},
                            "seed": {"type": "integer"},
                            "chunks": {"type": "array", "items": {"type": "object"}},
                            "from": {"type": "object"},
                            "to": {"type": "object"},
                            "params": {"type": "object", "description": "heightScale, frequency, octaves, persistence, lacunarity, caveThreshold, stoneLevel"}
                        }
                    },
                    "structures": {
                        "type": "array",
                        "description": "Array of structures to place. Each has type='fill' (from/to/material/hollow) or type='template' (template/position/dynamic).",
                        "items": {"type": "object"}
                    },
                    "player": {
                        "type": "object",
                        "description": "Player entity. type: physics|spider|animated. position: {x,y,z}. Optional: id, animFile.",
                        "properties": {
                            "type": {"type": "string", "enum": ["physics", "spider", "animated"]},
                            "position": {"type": "object"},
                            "id": {"type": "string"},
                            "animFile": {"type": "string"}
                        }
                    },
                    "camera": {
                        "type": "object",
                        "description": "Camera position and orientation.",
                        "properties": {
                            "position": {"type": "object"},
                            "yaw": {"type": "number"},
                            "pitch": {"type": "number"}
                        }
                    },
                    "npcs": {
                        "type": "array",
                        "description": "Array of NPCs. Each has: name, animFile, position, behavior (idle|patrol), "
                                       "waypoints, walkSpeed, waitTime, dialogue (DialogueTree JSON), "
                                       "storyCharacter (id, faction, agencyLevel, traits, goals, roles).",
                        "items": {"type": "object"}
                    },
                    "story": {
                        "type": "object",
                        "description": "Story engine setup. world: {factions, locations, variables}. arcs: [{id, name, constraintMode, beats}]."
                    }
                }
            }
        ),
        Tool(
            name="export_game_definition",
            description="Export the current game state as a game definition JSON. Returns camera, NPCs, and story state.",
            inputSchema={"type": "object", "properties": {}}
        ),
        Tool(
            name="validate_game_definition",
            description="Validate a game definition JSON without loading it. Returns {valid: bool, error?: string}.",
            inputSchema={
                "type": "object",
                "properties": {
                    "definition": {"type": "object", "description": "The game definition to validate"}
                },
                "required": ["definition"]
            }
        ),
        Tool(
            name="create_game_npc",
            description=(
                "Create a complete NPC in one call: spawn animated character + set dialogue tree + "
                "register story character with personality, goals, and faction. More convenient than "
                "calling spawn_npc + set_npc_dialogue + story_add_character separately."
            ),
            inputSchema={
                "type": "object",
                "properties": {
                    "name": {"type": "string", "description": "NPC name (unique identifier)"},
                    "animFile": {"type": "string", "description": "Animation file (default: character.anim)"},
                    "position": {"type": "object", "description": "{x, y, z} world coordinates",
                                 "properties": {"x": {"type": "number"}, "y": {"type": "number"}, "z": {"type": "number"}}},
                    "behavior": {"type": "string", "enum": ["idle", "patrol"], "description": "NPC behavior type"},
                    "waypoints": {"type": "array", "description": "Patrol waypoints [{x,y,z}]", "items": {"type": "object"}},
                    "walkSpeed": {"type": "number"},
                    "waitTime": {"type": "number"},
                    "dialogue": {
                        "type": "object",
                        "description": "Dialogue tree: {id, startNodeId, nodes: [{id, speaker, text, emotion?, nextNodeId?, choices?: [{text, targetNodeId}]}]}"
                    },
                    "storyCharacter": {
                        "type": "object",
                        "description": "Story character profile: {id, faction, agencyLevel (0-3), traits: {openness, conscientiousness, extraversion, agreeableness, neuroticism}, goals: [{id, description, priority}], roles: [string]}"
                    }
                },
                "required": ["name"]
            }
        ),

        # ================================================================
        # PROJECT LIFECYCLE
        # ================================================================

        Tool(
            name="build_project",
            description=(
                "Build the Phyxel engine project using CMake. Runs cmake -B build -S . and "
                "cmake --build build --config <config>. Returns build output including errors."
            ),
            inputSchema={
                "type": "object",
                "properties": {
                    "config": {"type": "string", "enum": ["Debug", "Release"], "description": "Build configuration (default: Debug)"},
                    "reconfigure": {"type": "boolean", "description": "Run cmake configure before build (default: false)"}
                }
            }
        ),
        Tool(
            name="launch_engine",
            description=(
                "Launch the Phyxel engine executable as a background process. "
                "The engine must be built first. Once running, all other tools become available. "
                "Returns the process ID."
            ),
            inputSchema={
                "type": "object",
                "properties": {
                    "config": {"type": "string", "enum": ["Debug", "Release"], "description": "Which build config to launch (default: Debug)"},
                    "args": {"type": "array", "items": {"type": "string"}, "description": "Additional command-line arguments"}
                }
            }
        ),
        Tool(
            name="engine_running",
            description="Check if the engine process is currently running and responsive.",
            inputSchema={"type": "object", "properties": {}}
        ),
        Tool(
            name="package_game",
            description=(
                "Package a game into a standalone distributable directory. "
                "Creates a self-contained folder with game executable, shaders, "
                "resources, and game definition — no source code, build system, or dev tools. "
                "Use projectDir to package from a scaffolded game project (own C++ exe). "
                "Use prebakeWorld to save the engine's current world to SQLite for instant startup."
            ),
            inputSchema={
                "type": "object",
                "properties": {
                    "name": {"type": "string", "description": "Game name (used for exe and folder)"},
                    "definition": {"type": "object", "description": "Game definition JSON (optional — if omitted, exports current engine state)"},
                    "output": {"type": "string", "description": "Output directory path (default: Documents/PhyxelProjects/<name>)"},
                    "config": {"type": "string", "enum": ["Debug", "Release"], "description": "Build config binary to package (default: Debug)"},
                    "projectDir": {"type": "string", "description": "Path to a scaffolded game project directory (uses its own compiled exe)"},
                    "prebakeWorld": {"type": "boolean", "description": "Save the running engine's world to SQLite first (default: false)"},
                    "allResources": {"type": "boolean", "description": "Include all resources, not just those referenced (default: false)"},
                    "includeMcp": {"type": "boolean", "description": "Include MCP server for AI iteration (default: false)"},
                    "title": {"type": "string", "description": "Window title (default: game name)"}
                },
                "required": ["name"]
            }
        ),

        # ================================================================
        # IN-ENGINE PROJECT BUILD / RUN
        # ================================================================

        Tool(
            name="project_info",
            description=(
                "Get info about the game project currently loaded in the engine "
                "(via --project flag). Returns project dir, whether game.json/CMakeLists.txt/exe exist."
            ),
            inputSchema={"type": "object", "properties": {}}
        ),
        Tool(
            name="build_game",
            description=(
                "Build the game project from within the running engine. "
                "Requires the engine to be running with --project <dir>. "
                "Runs cmake configure (if needed) and cmake build on the project directory. "
                "Returns build output including any errors."
            ),
            inputSchema={
                "type": "object",
                "properties": {
                    "config": {"type": "string", "enum": ["Debug", "Release"], "description": "Build configuration (default: Debug)"},
                    "reconfigure": {"type": "boolean", "description": "Force cmake reconfigure (default: false)"}
                }
            }
        ),
        Tool(
            name="run_game",
            description=(
                "Launch the built game executable from within the running engine. "
                "Requires the engine to be running with --project <dir> and the game to be built."
            ),
            inputSchema={"type": "object", "properties": {}}
        ),

        # ================================================================
        # PROJECT MANAGEMENT (launcher / discovery)
        # ================================================================

        Tool(
            name="list_projects",
            description=(
                "List all game projects discovered in the PhyxelProjects directory "
                "(typically ~/Documents/PhyxelProjects). Returns project names, paths, "
                "last-opened timestamps, and metadata (has game.json, CMakeLists.txt, world DB)."
            ),
            inputSchema={"type": "object", "properties": {}}
        ),
        Tool(
            name="create_project",
            description=(
                "Create a new empty game project. Scaffolds a minimal project directory "
                "with engine.json and resource folders in the PhyxelProjects directory. "
                "The project can then be opened with open_project or via the launcher UI."
            ),
            inputSchema={
                "type": "object",
                "properties": {
                    "name": {"type": "string", "description": "Project name (used as directory name and window title)"}
                },
                "required": ["name"]
            }
        ),
        Tool(
            name="open_project",
            description=(
                "Open/switch to a game project in the running engine. "
                "Loads the project's world database and game definition. "
                "The engine must be running."
            ),
            inputSchema={
                "type": "object",
                "properties": {
                    "path": {"type": "string", "description": "Absolute path to the project directory"}
                },
                "required": ["path"]
            }
        ),

        # ================================================================
        # ASYNC JOB SYSTEM
        # ================================================================

        Tool(
            name="submit_job",
            description=(
                "Submit a long-running operation as a background job. The engine continues "
                "running while the job executes. Returns a job_id to poll for status. "
                "Supported job types: fill_region, clear_region, generate_world, save_world, project_build."
            ),
            inputSchema={
                "type": "object",
                "properties": {
                    "type": {
                        "type": "string",
                        "enum": ["fill_region", "clear_region", "generate_world", "save_world", "project_build"],
                        "description": "The job type to submit"
                    },
                    "params": {
                        "type": "object",
                        "description": (
                            "Job-specific parameters. "
                            "fill_region: {x1,y1,z1,x2,y2,z2, material?, hollow?}. "
                            "clear_region: {x1,y1,z1,x2,y2,z2}. "
                            "generate_world: {type, from_x,from_y,from_z, to_x,to_y,to_z, seed?}. "
                            "save_world: {dirty_only?}. "
                            "project_build: {config?}."
                        )
                    }
                },
                "required": ["type", "params"]
            }
        ),
        Tool(
            name="get_job_status",
            description=(
                "Get the status of a background job by ID. Returns state (Pending/Running/"
                "Completing/Complete/Failed/Cancelled), progress (0.0-1.0), message, and "
                "result (when complete)."
            ),
            inputSchema={
                "type": "object",
                "properties": {
                    "job_id": {"type": "integer", "description": "The job ID returned by submit_job"}
                },
                "required": ["job_id"]
            }
        ),
        Tool(
            name="list_jobs",
            description="List all active and recent background jobs with their status.",
            inputSchema={"type": "object", "properties": {}}
        ),
        Tool(
            name="cancel_job",
            description="Cancel a running or pending background job.",
            inputSchema={
                "type": "object",
                "properties": {
                    "job_id": {"type": "integer", "description": "The job ID to cancel"}
                },
                "required": ["job_id"]
            }
        ),

        # ================================================================
        # LIGHTING CONTROL
        # ================================================================

        Tool(
            name="get_inventory",
            description=(
                "Get the player's inventory state: all slots, hotbar contents, selected slot, "
                "and creative mode status. Hotbar is slots 0-8."
            ),
            inputSchema={"type": "object", "properties": {}}
        ),
        Tool(
            name="give_item",
            description="Give items to the player's inventory. Stacks automatically.",
            inputSchema={
                "type": "object",
                "properties": {
                    "material": {"type": "string", "description": "Material name (e.g. 'Stone', 'Wood', 'Metal')"},
                    "count": {"type": "integer", "description": "Number of items to give (default 1)", "default": 1}
                },
                "required": ["material"]
            }
        ),
        Tool(
            name="take_item",
            description="Remove items from the player's inventory.",
            inputSchema={
                "type": "object",
                "properties": {
                    "material": {"type": "string", "description": "Material name to remove"},
                    "count": {"type": "integer", "description": "Number to remove (default 1)", "default": 1}
                },
                "required": ["material"]
            }
        ),
        Tool(
            name="select_hotbar_slot",
            description="Set the currently selected hotbar slot (0-8). This determines which material is used when placing voxels.",
            inputSchema={
                "type": "object",
                "properties": {
                    "slot": {"type": "integer", "description": "Hotbar slot index (0-8)"}
                },
                "required": ["slot"]
            }
        ),
        Tool(
            name="set_inventory_slot",
            description="Set a specific inventory slot to a material and count. Omit 'material' to clear the slot.",
            inputSchema={
                "type": "object",
                "properties": {
                    "slot": {"type": "integer", "description": "Slot index (0-35)"},
                    "material": {"type": "string", "description": "Material name (omit to clear slot)"},
                    "count": {"type": "integer", "description": "Stack count (default 1)", "default": 1}
                },
                "required": ["slot"]
            }
        ),
        Tool(
            name="clear_inventory",
            description="Clear all items from the player's inventory.",
            inputSchema={"type": "object", "properties": {}}
        ),
        Tool(
            name="set_creative_mode",
            description="Toggle creative mode. In creative mode, items are never consumed when placing voxels.",
            inputSchema={
                "type": "object",
                "properties": {
                    "enabled": {"type": "boolean", "description": "True for creative mode, false for survival"}
                },
                "required": ["enabled"]
            }
        ),

        # ================================================================
        # HEALTH / DAMAGE
        # ================================================================

        Tool(
            name="damage_entity",
            description="Deal damage to an entity. Returns actual damage dealt and updated health state.",
            inputSchema={
                "type": "object",
                "properties": {
                    "id": {"type": "string", "description": "Entity ID"},
                    "amount": {"type": "number", "description": "Damage amount"},
                    "source": {"type": "string", "description": "Source entity ID (optional)", "default": ""}
                },
                "required": ["id", "amount"]
            }
        ),
        Tool(
            name="heal_entity",
            description="Heal an entity. Returns actual amount healed and updated health state.",
            inputSchema={
                "type": "object",
                "properties": {
                    "id": {"type": "string", "description": "Entity ID"},
                    "amount": {"type": "number", "description": "Heal amount"}
                },
                "required": ["id", "amount"]
            }
        ),
        Tool(
            name="set_entity_health",
            description="Set entity health properties directly (health, maxHealth, invulnerable).",
            inputSchema={
                "type": "object",
                "properties": {
                    "id": {"type": "string", "description": "Entity ID"},
                    "health": {"type": "number", "description": "Set current health"},
                    "maxHealth": {"type": "number", "description": "Set max health"},
                    "invulnerable": {"type": "boolean", "description": "Set invulnerability"}
                },
                "required": ["id"]
            }
        ),
        Tool(
            name="kill_entity",
            description="Instantly kill an entity (set health to 0).",
            inputSchema={
                "type": "object",
                "properties": {
                    "id": {"type": "string", "description": "Entity ID"}
                },
                "required": ["id"]
            }
        ),
        Tool(
            name="revive_entity",
            description="Revive a dead entity with optional health percentage (default 100%).",
            inputSchema={
                "type": "object",
                "properties": {
                    "id": {"type": "string", "description": "Entity ID"},
                    "healthPercent": {"type": "number", "description": "Health percentage to revive with (0.01-1.0, default 1.0)", "default": 1.0}
                },
                "required": ["id"]
            }
        ),

        # ================================================================
        # DAY/NIGHT CYCLE
        # ================================================================

        Tool(
            name="get_day_night",
            description=(
                "Get the current day/night cycle state: time of day (0-24h), sun direction/color, "
                "ambient strength, cycle enabled/paused, day length, and time scale."
            ),
            inputSchema={"type": "object", "properties": {}}
        ),
        Tool(
            name="set_day_night",
            description=(
                "Configure the day/night cycle. Can set time of day, enable/disable the cycle, "
                "pause it, adjust day length and time scale."
            ),
            inputSchema={
                "type": "object",
                "properties": {
                    "timeOfDay": {"type": "number", "description": "Time of day in hours (0-24). 0=midnight, 6=dawn, 12=noon, 18=dusk"},
                    "enabled": {"type": "boolean", "description": "Enable/disable the day/night cycle"},
                    "paused": {"type": "boolean", "description": "Pause/unpause the cycle"},
                    "dayLengthSeconds": {"type": "number", "description": "Real seconds for a full day cycle (default 600)"},
                    "timeScale": {"type": "number", "description": "Time speed multiplier (1.0 = normal)"}
                }
            }
        ),

        Tool(
            name="list_lights",
            description=(
                "List all lights in the scene (point lights, spot lights) and the ambient light strength. "
                "Returns light IDs, positions, colors, intensities, radii, and enabled state."
            ),
            inputSchema={"type": "object", "properties": {}}
        ),
        Tool(
            name="add_point_light",
            description="Add a point light to the scene. Returns the light ID. Max 32 point lights.",
            inputSchema={
                "type": "object",
                "properties": {
                    "x": {"type": "number", "description": "Light X position"},
                    "y": {"type": "number", "description": "Light Y position"},
                    "z": {"type": "number", "description": "Light Z position"},
                    "color": {
                        "type": "object",
                        "description": "Light color (default white)",
                        "properties": {
                            "r": {"type": "number", "default": 1.0},
                            "g": {"type": "number", "default": 1.0},
                            "b": {"type": "number", "default": 1.0}
                        }
                    },
                    "intensity": {"type": "number", "description": "Light intensity (default 1.0)"},
                    "radius": {"type": "number", "description": "Light radius in voxel units (default 10.0)"},
                    "enabled": {"type": "boolean", "description": "Whether the light is enabled (default true)"}
                },
                "required": ["x", "y", "z"]
            }
        ),
        Tool(
            name="add_spot_light",
            description=(
                "Add a spot light to the scene. Returns the light ID. Max 16 spot lights. "
                "Direction (dx, dy, dz) is a normalized vector. Cone angles are cosine values "
                "(inner_cone=0.9 ≈ 25° half-angle, outer_cone=0.8 ≈ 37° half-angle)."
            ),
            inputSchema={
                "type": "object",
                "properties": {
                    "x": {"type": "number", "description": "Light X position"},
                    "y": {"type": "number", "description": "Light Y position"},
                    "z": {"type": "number", "description": "Light Z position"},
                    "dx": {"type": "number", "description": "Direction X (default 0)", "default": 0},
                    "dy": {"type": "number", "description": "Direction Y (default -1, pointing down)", "default": -1},
                    "dz": {"type": "number", "description": "Direction Z (default 0)", "default": 0},
                    "color": {
                        "type": "object",
                        "description": "Light color (default white)",
                        "properties": {
                            "r": {"type": "number", "default": 1.0},
                            "g": {"type": "number", "default": 1.0},
                            "b": {"type": "number", "default": 1.0}
                        }
                    },
                    "intensity": {"type": "number", "description": "Light intensity (default 1.0)"},
                    "radius": {"type": "number", "description": "Light radius (default 20.0)"},
                    "inner_cone": {"type": "number", "description": "Inner cone cosine (default 0.9)"},
                    "outer_cone": {"type": "number", "description": "Outer cone cosine (default 0.8)"},
                    "enabled": {"type": "boolean", "description": "Whether enabled (default true)"}
                },
                "required": ["x", "y", "z"]
            }
        ),
        Tool(
            name="remove_light",
            description="Remove a point or spot light by its ID.",
            inputSchema={
                "type": "object",
                "properties": {
                    "id": {"type": "integer", "description": "The light ID to remove"}
                },
                "required": ["id"]
            }
        ),
        Tool(
            name="update_light",
            description=(
                "Update properties of an existing light (point or spot). "
                "Only specify the fields you want to change. The light type is auto-detected by ID."
            ),
            inputSchema={
                "type": "object",
                "properties": {
                    "id": {"type": "integer", "description": "The light ID to update"},
                    "x": {"type": "number", "description": "New X position"},
                    "y": {"type": "number", "description": "New Y position"},
                    "z": {"type": "number", "description": "New Z position"},
                    "dx": {"type": "number", "description": "New direction X (spot lights only)"},
                    "dy": {"type": "number", "description": "New direction Y (spot lights only)"},
                    "dz": {"type": "number", "description": "New direction Z (spot lights only)"},
                    "color": {
                        "type": "object",
                        "properties": {
                            "r": {"type": "number"},
                            "g": {"type": "number"},
                            "b": {"type": "number"}
                        }
                    },
                    "intensity": {"type": "number"},
                    "radius": {"type": "number"},
                    "inner_cone": {"type": "number", "description": "Spot light inner cone cosine"},
                    "outer_cone": {"type": "number", "description": "Spot light outer cone cosine"},
                    "enabled": {"type": "boolean"}
                },
                "required": ["id"]
            }
        ),
        Tool(
            name="set_ambient_light",
            description="Set the global ambient light strength (0.0 = pitch black, 1.0 = normal, 2.0 = maximum).",
            inputSchema={
                "type": "object",
                "properties": {
                    "strength": {"type": "number", "description": "Ambient light strength (0.0 to 2.0)"}
                },
                "required": ["strength"]
            }
        ),

        # ================================================================
        # AUDIO CONTROL
        # ================================================================

        Tool(
            name="list_sounds",
            description="List available sound files and audio channels.",
            inputSchema={"type": "object", "properties": {}}
        ),
        Tool(
            name="play_sound",
            description=(
                "Play a sound effect. Specify just the filename (e.g. 'click.wav'). "
                "For 3D positional audio, include x/y/z coordinates. "
                "Channels: Master, SFX (default), Music, Voice."
            ),
            inputSchema={
                "type": "object",
                "properties": {
                    "file": {"type": "string", "description": "Sound filename (e.g. 'click.wav', 'whoosh.wav')"},
                    "volume": {"type": "number", "description": "Volume 0.0 to 1.0 (default 1.0)"},
                    "channel": {
                        "type": "string",
                        "description": "Audio channel",
                        "enum": ["Master", "SFX", "Music", "Voice"]
                    },
                    "x": {"type": "number", "description": "3D position X (omit for 2D sound)"},
                    "y": {"type": "number", "description": "3D position Y"},
                    "z": {"type": "number", "description": "3D position Z"}
                },
                "required": ["file"]
            }
        ),
        Tool(
            name="set_volume",
            description="Set the volume for an audio channel (Master, SFX, Music, Voice).",
            inputSchema={
                "type": "object",
                "properties": {
                    "channel": {
                        "type": "string",
                        "description": "Audio channel to adjust",
                        "enum": ["Master", "SFX", "Music", "Voice"]
                    },
                    "volume": {"type": "number", "description": "Volume level (0.0 to 1.0)"}
                },
                "required": ["channel", "volume"]
            }
        ),

        # ================================================================
        # Custom UI Menu Management
        # ================================================================
        Tool(
            name="list_menus",
            description="List all registered custom UI menu screens and their visibility state.",
            inputSchema={
                "type": "object",
                "properties": {},
                "required": []
            }
        ),
        Tool(
            name="create_menu",
            description=(
                "Create a custom UI menu from a JSON definition. The menu is added as a named screen. "
                "Definition format: {\"id\": \"menu_id\", \"title\": \"Title\", \"anchor\": \"Center\", "
                "\"size\": [400, 500], \"children\": [{\"type\": \"label\", \"id\": \"lbl\", \"text\": \"Hello\", \"isTitle\": true}, "
                "{\"type\": \"slider\", \"id\": \"fov\", \"label\": \"FOV \", \"value\": 70, \"min\": 30, \"max\": 120, \"size\": [380, 32]}, "
                "{\"type\": \"checkbox\", \"id\": \"vsync\", \"label\": \"VSync\", \"checked\": false, \"size\": [380, 32]}, "
                "{\"type\": \"dropdown\", \"id\": \"quality\", \"label\": \"Quality \", \"options\": [\"Low\",\"Medium\",\"High\"], \"selected\": 1, \"size\": [380, 40]}, "
                "{\"type\": \"button\", \"id\": \"back\", \"text\": \"Close\", \"size\": [380, 40]}]}"
            ),
            inputSchema={
                "type": "object",
                "properties": {
                    "name": {"type": "string", "description": "Unique name for the menu screen (e.g. 'game_menu')"},
                    "definition": {
                        "type": "object",
                        "description": "Menu definition JSON with id, title, anchor, size, children array of widgets"
                    }
                },
                "required": ["name", "definition"]
            }
        ),
        Tool(
            name="show_menu",
            description="Show (make visible) a previously created custom UI menu screen.",
            inputSchema={
                "type": "object",
                "properties": {
                    "name": {"type": "string", "description": "Name of the menu screen to show"}
                },
                "required": ["name"]
            }
        ),
        Tool(
            name="hide_menu",
            description="Hide a custom UI menu screen.",
            inputSchema={
                "type": "object",
                "properties": {
                    "name": {"type": "string", "description": "Name of the menu screen to hide"}
                },
                "required": ["name"]
            }
        ),
        Tool(
            name="toggle_menu",
            description="Toggle the visibility of a custom UI menu screen.",
            inputSchema={
                "type": "object",
                "properties": {
                    "name": {"type": "string", "description": "Name of the menu screen to toggle"}
                },
                "required": ["name"]
            }
        ),
        Tool(
            name="remove_menu",
            description="Remove a custom UI menu screen entirely.",
            inputSchema={
                "type": "object",
                "properties": {
                    "name": {"type": "string", "description": "Name of the menu screen to remove"}
                },
                "required": ["name"]
            }
        ),

        # ================================================================
        # Bulk Operations (fast chunk-level ops)
        # ================================================================
        Tool(
            name="clear_chunk",
            description="Instantly clear ALL voxels in a chunk. Much faster than clear_region for whole chunks. Takes chunk coordinates (not world coordinates).",
            inputSchema={
                "type": "object",
                "properties": {
                    "x": {"type": "integer", "description": "Chunk X coordinate (world_x / 32)"},
                    "y": {"type": "integer", "description": "Chunk Y coordinate (world_y / 32)"},
                    "z": {"type": "integer", "description": "Chunk Z coordinate (world_z / 32)"},
                },
                "required": ["x", "y", "z"]
            }
        ),
        Tool(
            name="rebuild_physics",
            description="Force rebuild physics collision shapes. Optionally target a specific chunk. Useful after bulk modifications.",
            inputSchema={
                "type": "object",
                "properties": {
                    "chunk": {
                        "type": "object",
                        "description": "Optional: rebuild only this chunk (chunk coordinates)",
                        "properties": {
                            "x": {"type": "integer"},
                            "y": {"type": "integer"},
                            "z": {"type": "integer"},
                        }
                    }
                },
                "required": []
            }
        ),
        Tool(
            name="clear_all_entities",
            description="Remove ALL entities from the world (player, NPCs, all characters). Use before reload_game_definition to start fresh.",
            inputSchema={
                "type": "object",
                "properties": {},
                "required": []
            }
        ),
        Tool(
            name="reload_game_definition",
            description="Destructively reload a game definition: clears all entities, NPCs, dialogue, and story, then loads the provided definition fresh. Unlike load_game_definition which is additive, this replaces everything.",
            inputSchema={
                "type": "object",
                "properties": {
                    "definition": {
                        "type": "string",
                        "description": "Complete game definition as a JSON string"
                    }
                },
                "required": ["definition"]
            }
        ),
        Tool(
            name="get_terrain_height",
            description="Query the surface Y coordinate at a given (x, z) world position. Returns the highest voxel Y and the spawn Y (surface + 1). Useful for placing entities on terrain.",
            inputSchema={
                "type": "object",
                "properties": {
                    "x": {"type": "integer", "description": "World X coordinate"},
                    "z": {"type": "integer", "description": "World Z coordinate"},
                    "max_y": {"type": "integer", "description": "Maximum Y to search (default: 255)"},
                    "min_y": {"type": "integer", "description": "Minimum Y to search (default: 0)"},
                },
                "required": ["x", "z"]
            }
        ),
    ]


@server.call_tool()
async def call_tool(name: str, arguments: dict) -> list[TextContent]:
    """Route tool calls to the engine HTTP API."""
    logger.info(f"Tool call: {name}({json.dumps(arguments, default=str)[:200]})")

    result = await _dispatch_tool(name, arguments)

    return [TextContent(type="text", text=json.dumps(result, indent=2))]


async def _dispatch_tool(name: str, args: dict) -> dict:
    """Dispatch a tool call to the appropriate API endpoint."""

    # --- Status & State ---
    if name == "engine_status":
        return await api_get("/api/status")

    elif name == "get_world_state":
        return await api_get("/api/state")

    elif name == "get_camera":
        return await api_get("/api/camera")

    # --- Entity Management ---
    elif name == "list_entities":
        return await api_get("/api/entities")

    elif name == "get_entity":
        return await api_get(f"/api/entity/{args['id']}")

    elif name == "spawn_entity":
        body: dict[str, Any] = {
            "type": args["type"],
            "position": {"x": args["x"], "y": args["y"], "z": args["z"]}
        }
        if "id" in args:
            body["id"] = args["id"]
        if "animFile" in args:
            body["animFile"] = args["animFile"]
        return await api_post("/api/entity/spawn", body)

    elif name == "move_entity":
        return await api_post("/api/entity/move", {
            "id": args["id"],
            "position": {"x": args["x"], "y": args["y"], "z": args["z"]}
        })

    elif name == "remove_entity":
        return await api_post("/api/entity/remove", {"id": args["id"]})

    # --- Voxel World ---
    elif name == "place_voxel":
        body = {"x": args["x"], "y": args["y"], "z": args["z"]}
        if "material" in args:
            body["material"] = args["material"]
        return await api_post("/api/world/voxel", body)

    elif name == "remove_voxel":
        return await api_post("/api/world/voxel/remove", {
            "x": args["x"], "y": args["y"], "z": args["z"]
        })

    elif name == "query_voxel":
        return await api_get("/api/world/voxel", {
            "x": str(args["x"]), "y": str(args["y"]), "z": str(args["z"])
        })

    elif name == "place_voxels_batch":
        return await api_post("/api/world/voxel/batch", {"voxels": args["voxels"]})

    # --- Templates ---
    elif name == "list_templates":
        return await api_get("/api/templates")

    elif name == "spawn_template":
        return await api_post("/api/world/template", {
            "name": args["name"],
            "position": {"x": args["x"], "y": args["y"], "z": args["z"]},
            "static": args.get("static", True)
        })

    # --- Camera ---
    elif name == "set_camera":
        body: dict[str, Any] = {}
        if "x" in args or "y" in args or "z" in args:
            body["position"] = {
                "x": args.get("x", 0),
                "y": args.get("y", 0),
                "z": args.get("z", 0)
            }
        if "yaw" in args:
            body["yaw"] = args["yaw"]
        if "pitch" in args:
            body["pitch"] = args["pitch"]
        return await api_post("/api/camera", body)

    # --- Scripting ---
    elif name == "run_script":
        return await api_post("/api/script", {"code": args["code"]})

    # --- Region Fill ---
    elif name == "fill_region":
        body: dict[str, Any] = {
            "x1": args["x1"], "y1": args["y1"], "z1": args["z1"],
            "x2": args["x2"], "y2": args["y2"], "z2": args["z2"]
        }
        if "material" in args:
            body["material"] = args["material"]
        if "hollow" in args:
            body["hollow"] = args["hollow"]
        return await api_post_async("/api/world/fill", body)

    # --- Materials ---
    elif name == "list_materials":
        return await api_get("/api/materials")

    # --- Chunk Info ---
    elif name == "get_chunk_info":
        return await api_get("/api/world/chunks")

    # --- Entity Update ---
    elif name == "update_entity":
        body: dict[str, Any] = {"id": args["id"]}
        # Build position if any coord given
        if any(k in args for k in ("x", "y", "z")):
            body["position"] = {
                "x": args.get("x", 0), "y": args.get("y", 0), "z": args.get("z", 0)
            }
        # Build rotation if any quat component given
        if any(k in args for k in ("rotation_w", "rotation_x", "rotation_y", "rotation_z")):
            body["rotation"] = {
                "w": args.get("rotation_w", 1), "x": args.get("rotation_x", 0),
                "y": args.get("rotation_y", 0), "z": args.get("rotation_z", 0)
            }
        # Build scale if any component given
        if any(k in args for k in ("scale_x", "scale_y", "scale_z")):
            body["scale"] = {
                "x": args.get("scale_x", 1), "y": args.get("scale_y", 1), "z": args.get("scale_z", 1)
            }
        # Build debugColor if any component given
        if any(k in args for k in ("color_r", "color_g", "color_b", "color_a")):
            body["debugColor"] = {
                "r": args.get("color_r", 1), "g": args.get("color_g", 1),
                "b": args.get("color_b", 1), "a": args.get("color_a", 1)
            }
        return await api_post("/api/entity/update", body)

    # --- Screenshot ---
    elif name == "screenshot":
        return await api_get("/api/screenshot")

    # --- Region Scan ---
    elif name == "scan_region":
        return await api_get("/api/world/scan", {
            "x1": str(args["x1"]), "y1": str(args["y1"]), "z1": str(args["z1"]),
            "x2": str(args["x2"]), "y2": str(args["y2"]), "z2": str(args["z2"])
        })

    # --- Region Clear ---
    elif name == "clear_region":
        return await api_post_async("/api/world/clear", {
            "x1": args["x1"], "y1": args["y1"], "z1": args["z1"],
            "x2": args["x2"], "y2": args["y2"], "z2": args["z2"]
        })

    # --- World Save ---
    elif name == "save_world":
        body: dict[str, Any] = {}
        if "all" in args:
            body["all"] = args["all"]
        return await api_post("/api/world/save", body)

    # --- Event Polling ---
    elif name == "poll_events":
        params = {}
        if "cursor" in args:
            params["since"] = str(args["cursor"])
        return await api_get("/api/events", params)

    # --- Snapshots ---
    elif name == "create_snapshot":
        return await api_post("/api/snapshot/create", {
            "name": args["name"],
            "x1": args["x1"], "y1": args["y1"], "z1": args["z1"],
            "x2": args["x2"], "y2": args["y2"], "z2": args["z2"]
        })

    elif name == "restore_snapshot":
        return await api_post("/api/snapshot/restore", {"name": args["name"]})

    elif name == "list_snapshots":
        return await api_get("/api/snapshots")

    elif name == "delete_snapshot":
        return await api_post("/api/snapshot/delete", {"name": args["name"]})

    # --- Clipboard ---
    elif name == "copy_region":
        return await api_post("/api/clipboard/copy", {
            "x1": args["x1"], "y1": args["y1"], "z1": args["z1"],
            "x2": args["x2"], "y2": args["y2"], "z2": args["z2"]
        })

    elif name == "paste_region":
        body = {"x": args["x"], "y": args["y"], "z": args["z"]}
        if "rotate" in args:
            body["rotate"] = args["rotate"]
        return await api_post("/api/clipboard/paste", body)

    elif name == "get_clipboard":
        return await api_get("/api/clipboard")

    # --- World Generation ---
    elif name == "generate_world":
        body: dict[str, Any] = {"type": args["type"]}
        if "seed" in args:
            body["seed"] = args["seed"]
        if "chunks" in args:
            body["chunks"] = args["chunks"]
        if "from" in args:
            body["from"] = args["from"]
        if "to" in args:
            body["to"] = args["to"]
        if "params" in args:
            body["params"] = args["params"]
        return await api_post_async("/api/world/generate", body)

    # --- Template Save ---
    elif name == "save_template":
        return await api_post("/api/template/save", {
            "name": args["name"],
            "x1": args["x1"], "y1": args["y1"], "z1": args["z1"],
            "x2": args["x2"], "y2": args["y2"], "z2": args["z2"]
        })

    # --- NPC Management ---
    elif name == "spawn_npc":
        body: dict[str, Any] = {"name": args["name"]}
        if "animFile" in args:
            body["animFile"] = args["animFile"]
        if "position" in args:
            body["position"] = args["position"]
        if "behavior" in args:
            body["behavior"] = args["behavior"]
        if "waypoints" in args:
            body["waypoints"] = args["waypoints"]
        if "walkSpeed" in args:
            body["walkSpeed"] = args["walkSpeed"]
        if "waitTime" in args:
            body["waitTime"] = args["waitTime"]
        return await api_post("/api/npc/spawn", body)

    elif name == "remove_npc":
        return await api_post("/api/npc/remove", {"name": args["name"]})

    elif name == "list_npcs":
        return await api_get("/api/npcs")

    elif name == "set_npc_behavior":
        body = {"name": args["name"], "behavior": args["behavior"]}
        if "waypoints" in args:
            body["waypoints"] = args["waypoints"]
        if "walkSpeed" in args:
            body["walkSpeed"] = args["walkSpeed"]
        if "waitTime" in args:
            body["waitTime"] = args["waitTime"]
        return await api_post("/api/npc/behavior", body)

    elif name == "get_npc_appearance":
        return await api_get(f"/api/npc/appearance?name={args['name']}")

    elif name == "set_npc_appearance":
        return await api_post("/api/npc/appearance", {
            "name": args["name"], "appearance": args["appearance"]
        })

    # --- Dialogue & Speech Bubbles ---
    elif name == "set_npc_dialogue":
        return await api_post("/api/npc/dialogue", {
            "name": args["name"], "tree": args["tree"]
        })

    elif name == "start_dialogue":
        return await api_post("/api/dialogue/start", {
            "npc": args["npc"], "tree": args["tree"]
        })

    elif name == "end_dialogue":
        return await api_post("/api/dialogue/end", {})

    elif name == "say_bubble":
        body = {"entityId": args["entityId"], "text": args["text"]}
        if "duration" in args:
            body["duration"] = args["duration"]
        return await api_post("/api/speech/say", body)

    elif name == "get_dialogue_state":
        return await api_get("/api/dialogue/state")

    elif name == "advance_dialogue":
        return await api_post("/api/dialogue/advance", {})

    elif name == "select_dialogue_choice":
        return await api_post("/api/dialogue/choice", {"index": args["index"]})

    elif name == "load_dialogue_file":
        body: dict[str, Any] = {"filename": args["filename"]}
        if "npc" in args:
            body["npc"] = args["npc"]
        return await api_post("/api/dialogue/load", body)

    elif name == "list_dialogue_files":
        return await api_get("/api/dialogue/files")

    # --- Story System ---
    elif name == "story_get_state":
        return await api_get("/api/story/state")

    elif name == "story_list_characters":
        return await api_get("/api/story/characters")

    elif name == "story_get_character":
        return await api_get(f"/api/story/character/{args['id']}")

    elif name == "story_list_arcs":
        return await api_get("/api/story/arcs")

    elif name == "story_get_arc":
        return await api_get(f"/api/story/arc/{args['id']}")

    elif name == "story_get_world":
        return await api_get("/api/story/world")

    elif name == "story_load_world":
        return await api_post("/api/story/load", {"definition": args["definition"]})

    elif name == "story_add_character":
        return await api_post("/api/story/character/add", args)

    elif name == "story_remove_character":
        return await api_post("/api/story/character/remove", {"id": args["id"]})

    elif name == "story_trigger_event":
        body: dict[str, Any] = {"type": args["type"]}
        if "data" in args:
            body["data"] = args["data"]
        if "location" in args:
            body["location"] = args["location"]
        if "participants" in args:
            body["participants"] = args["participants"]
        return await api_post("/api/story/event", body)

    elif name == "story_add_arc":
        return await api_post("/api/story/arc/add", args)

    elif name == "story_set_variable":
        return await api_post("/api/story/variable", {
            "name": args["name"], "value": args["value"]
        })

    elif name == "story_set_agency":
        return await api_post("/api/story/agency", {
            "id": args["id"], "level": args["level"]
        })

    elif name == "story_add_knowledge":
        body = {"characterId": args["characterId"], "fact": args["fact"]}
        if "category" in args:
            body["category"] = args["category"]
        return await api_post("/api/story/knowledge", body)

    # --- Game Definition (AI Game Development) ---

    elif name == "load_game_definition":
        return await api_post_async("/api/game/load_definition", args)

    elif name == "export_game_definition":
        return await api_get("/api/game/export_definition")

    elif name == "validate_game_definition":
        definition = args.get("definition", args)
        return await api_post("/api/game/validate_definition", definition)

    elif name == "create_game_npc":
        return await api_post("/api/game/create_npc", args)

    # --- Project Lifecycle ---

    elif name == "build_project":
        return await _build_project(args)

    elif name == "launch_engine":
        return await _launch_engine(args)

    elif name == "engine_running":
        return await _check_engine_running()

    elif name == "package_game":
        return await _package_game(args)

    # --- In-Engine Project Build / Run ---

    elif name == "project_info":
        return await api_get("/api/project/info")

    elif name == "build_game":
        return await api_post("/api/project/build", args)

    elif name == "run_game":
        return await api_post("/api/project/run", args)

    # --- Project Management ---

    elif name == "list_projects":
        return await api_get("/api/projects/list")

    elif name == "create_project":
        return await api_post("/api/projects/create", {"name": args["name"]})

    elif name == "open_project":
        return await api_post("/api/projects/open", {"path": args["path"]})

    # --- Async Job System ---

    elif name == "submit_job":
        return await api_post("/api/job/submit", {
            "type": args["type"],
            "params": args.get("params", {})
        })

    elif name == "get_job_status":
        job_id = args["job_id"]
        return await api_get(f"/api/job/{job_id}")

    elif name == "list_jobs":
        return await api_get("/api/jobs")

    elif name == "cancel_job":
        job_id = args["job_id"]
        return await api_post(f"/api/job/{job_id}/cancel", {})

    # --- Inventory ---
    elif name == "get_inventory":
        return await api_get("/api/inventory")

    elif name == "give_item":
        body = {"material": args["material"], "count": args.get("count", 1)}
        return await api_post("/api/inventory/give", body)

    elif name == "take_item":
        body = {"material": args["material"], "count": args.get("count", 1)}
        return await api_post("/api/inventory/take", body)

    elif name == "select_hotbar_slot":
        return await api_post("/api/inventory/select", {"slot": args["slot"]})

    elif name == "set_inventory_slot":
        body: dict[str, Any] = {"slot": args["slot"]}
        if "material" in args:
            body["material"] = args["material"]
            body["count"] = args.get("count", 1)
        return await api_post("/api/inventory/set_slot", body)

    elif name == "clear_inventory":
        return await api_post("/api/inventory/clear", {})

    elif name == "set_creative_mode":
        return await api_post("/api/inventory/creative", {"enabled": args["enabled"]})

    # --- Health / Damage ---
    elif name == "damage_entity":
        body = {"id": args["id"], "amount": args["amount"]}
        if "source" in args:
            body["source"] = args["source"]
        return await api_post("/api/entity/damage", body)

    elif name == "heal_entity":
        return await api_post("/api/entity/heal", {"id": args["id"], "amount": args["amount"]})

    elif name == "set_entity_health":
        body: dict[str, Any] = {"id": args["id"]}
        if "health" in args:
            body["health"] = args["health"]
        if "maxHealth" in args:
            body["maxHealth"] = args["maxHealth"]
        if "invulnerable" in args:
            body["invulnerable"] = args["invulnerable"]
        return await api_post("/api/entity/set_health", body)

    elif name == "kill_entity":
        return await api_post("/api/entity/kill", {"id": args["id"]})

    elif name == "revive_entity":
        body = {"id": args["id"]}
        if "healthPercent" in args:
            body["healthPercent"] = args["healthPercent"]
        return await api_post("/api/entity/revive", body)

    # --- Day/Night Cycle ---
    elif name == "get_day_night":
        return await api_get("/api/daynight")

    elif name == "set_day_night":
        body: dict[str, Any] = {}
        for key in ("timeOfDay", "enabled", "paused", "dayLengthSeconds", "timeScale"):
            if key in args:
                body[key] = args[key]
        return await api_post("/api/daynight/set", body)

    # --- Lighting ---
    elif name == "list_lights":
        return await api_get("/api/lights")

    elif name == "add_point_light":
        body: dict[str, Any] = {"x": args["x"], "y": args["y"], "z": args["z"]}
        if "color" in args:
            body["color"] = args["color"]
        if "intensity" in args:
            body["intensity"] = args["intensity"]
        if "radius" in args:
            body["radius"] = args["radius"]
        if "enabled" in args:
            body["enabled"] = args["enabled"]
        return await api_post("/api/light/point/add", body)

    elif name == "add_spot_light":
        body: dict[str, Any] = {"x": args["x"], "y": args["y"], "z": args["z"]}
        for key in ("dx", "dy", "dz", "intensity", "radius", "inner_cone", "outer_cone", "enabled"):
            if key in args:
                body[key] = args[key]
        if "color" in args:
            body["color"] = args["color"]
        return await api_post("/api/light/spot/add", body)

    elif name == "remove_light":
        return await api_post("/api/light/remove", {"id": args["id"]})

    elif name == "update_light":
        body: dict[str, Any] = {"id": args["id"]}
        for key in ("x", "y", "z", "dx", "dy", "dz", "intensity", "radius",
                     "inner_cone", "outer_cone", "enabled", "color"):
            if key in args:
                body[key] = args[key]
        return await api_post("/api/light/update", body)

    elif name == "set_ambient_light":
        return await api_post("/api/ambient", {"strength": args["strength"]})

    # --- Audio ---
    elif name == "list_sounds":
        return await api_get("/api/audio/sounds")

    elif name == "play_sound":
        body: dict[str, Any] = {"file": args["file"]}
        for key in ("volume", "channel", "x", "y", "z"):
            if key in args:
                body[key] = args[key]
        return await api_post("/api/audio/play", body)

    elif name == "set_volume":
        return await api_post("/api/audio/volume", {
            "channel": args["channel"],
            "volume": args["volume"]
        })

    # --- Custom UI Menu Management ---
    elif name == "list_menus":
        return await api_get("/api/ui/menus")

    elif name == "create_menu":
        return await api_post("/api/ui/menu", {
            "name": args["name"],
            "definition": args["definition"]
        })

    elif name == "show_menu":
        return await api_post("/api/ui/menu/show", {"name": args["name"]})

    elif name == "hide_menu":
        return await api_post("/api/ui/menu/hide", {"name": args["name"]})

    elif name == "toggle_menu":
        return await api_post("/api/ui/menu/toggle", {"name": args["name"]})

    elif name == "remove_menu":
        return await api_post("/api/ui/menu/remove", {"name": args["name"]})

    # --- Bulk Operations ---
    elif name == "clear_chunk":
        return await api_post("/api/world/clear_chunk", {
            "x": args["x"], "y": args["y"], "z": args["z"]
        })

    elif name == "rebuild_physics":
        body = {}
        if "chunk" in args:
            body["chunk"] = args["chunk"]
        return await api_post("/api/world/rebuild_physics", body)

    elif name == "clear_all_entities":
        return await api_post("/api/entities/clear", {})

    elif name == "reload_game_definition":
        return await api_post("/api/game/reload", {
            "definition": args["definition"]
        })

    elif name == "get_terrain_height":
        params = {"x": str(args["x"]), "z": str(args["z"])}
        if "max_y" in args:
            params["max_y"] = str(args["max_y"])
        if "min_y" in args:
            params["min_y"] = str(args["min_y"])
        return await api_get("/api/world/terrain_height", params)

    else:
        return {"error": f"Unknown tool: {name}"}


# ============================================================================
# Project Lifecycle Helpers
# ============================================================================

# Resolve project root (parent of scripts/mcp/)
PROJECT_ROOT = Path(__file__).resolve().parent.parent.parent

# CMake path (MSVC 2022)
CMAKE_PATH = r"C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"


def _get_cmake():
    """Return cmake executable path."""
    if os.path.exists(CMAKE_PATH):
        return CMAKE_PATH
    # Fallback to PATH
    return "cmake"


async def _build_project(args: dict) -> dict:
    """Build the project using CMake."""
    config = args.get("config", "Debug")
    reconfigure = args.get("reconfigure", False)
    cmake = _get_cmake()
    build_dir = PROJECT_ROOT / "build"

    output_lines = []

    if reconfigure or not build_dir.exists():
        try:
            result = subprocess.run(
                [cmake, "-B", "build", "-S", "."],
                cwd=str(PROJECT_ROOT),
                capture_output=True, text=True, timeout=120
            )
            output_lines.append("=== CMake Configure ===")
            if result.stdout:
                output_lines.append(result.stdout[-2000:])
            if result.returncode != 0:
                output_lines.append(f"Configure failed (exit {result.returncode})")
                if result.stderr:
                    output_lines.append(result.stderr[-2000:])
                return {"success": False, "output": "\n".join(output_lines)}
        except subprocess.TimeoutExpired:
            return {"success": False, "error": "CMake configure timed out"}

    try:
        result = subprocess.run(
            [cmake, "--build", "build", "--config", config],
            cwd=str(PROJECT_ROOT),
            capture_output=True, text=True, timeout=300
        )
        output_lines.append(f"=== CMake Build ({config}) ===")
        if result.stdout:
            # Trim to last 3000 chars to avoid huge output
            output_lines.append(result.stdout[-3000:])
        if result.returncode != 0:
            output_lines.append(f"Build failed (exit {result.returncode})")
            if result.stderr:
                output_lines.append(result.stderr[-2000:])
            return {"success": False, "output": "\n".join(output_lines)}
        return {"success": True, "output": "\n".join(output_lines)}
    except subprocess.TimeoutExpired:
        return {"success": False, "error": "CMake build timed out (300s)"}


_engine_process = None


async def _launch_engine(args: dict) -> dict:
    """Launch the engine executable as a background process."""
    global _engine_process
    config = args.get("config", "Debug")
    extra_args = args.get("args", [])

    # Check if already running
    if _engine_process and _engine_process.poll() is None:
        return {"success": True, "message": "Engine already running", "pid": _engine_process.pid}

    exe_path = PROJECT_ROOT / "build" / "editor" / config / "phyxel.exe"
    if not exe_path.exists():
        # Try old path (pre-rename)
        exe_path = PROJECT_ROOT / "build" / "game" / config / "phyxel.exe"
    if not exe_path.exists():
        # Try root copy
        exe_path = PROJECT_ROOT / "phyxel.exe"
    if not exe_path.exists():
        return {"error": f"Engine executable not found. Build the project first."}

    try:
        _engine_process = subprocess.Popen(
            [str(exe_path)] + extra_args,
            cwd=str(PROJECT_ROOT),
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL
        )
        return {"success": True, "pid": _engine_process.pid, "executable": str(exe_path)}
    except Exception as e:
        return {"error": f"Failed to launch engine: {e}"}


async def _check_engine_running() -> dict:
    """Check if the engine is running and responsive."""
    global _engine_process

    process_alive = _engine_process is not None and _engine_process.poll() is None

    try:
        result = await api_get("/api/status")
        api_ok = "error" not in result
    except Exception:
        api_ok = False

    return {
        "process_alive": process_alive,
        "api_responsive": api_ok,
        "pid": _engine_process.pid if process_alive else None
    }


# ============================================================================
# Game Packaging
# ============================================================================

async def _package_game(args: dict) -> dict:
    """Package a game into a standalone distributable directory."""
    name = args.get("name", "MyGame")
    definition = args.get("definition")
    output_path = args.get("output")
    config = args.get("config", "Debug")
    all_resources = args.get("allResources", False)
    include_mcp = args.get("includeMcp", False)
    title = args.get("title")
    project_dir_path = args.get("projectDir")
    prebake = args.get("prebakeWorld", False)

    # If no definition provided, export from running engine
    if not definition:
        try:
            result = await api_get("/api/game/export_definition")
            if "error" in result:
                return {"error": f"Cannot export from engine: {result['error']}"}
            definition = result
        except Exception as e:
            return {"error": f"Engine not running and no definition provided: {e}"}

    # Import the packaging tool
    import importlib.util
    pkg_script = PROJECT_ROOT / "tools" / "package_game.py"
    if not pkg_script.exists():
        return {"error": "tools/package_game.py not found"}

    spec = importlib.util.spec_from_file_location("package_game", str(pkg_script))
    pkg_mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(pkg_mod)

    if output_path:
        output_dir = Path(output_path)
    else:
        docs = Path(os.environ.get("USERPROFILE", os.path.expanduser("~"))) / "Documents" / "PhyxelProjects" / name
        output_dir = docs
    output_dir = output_dir.resolve()

    # Pre-bake world if requested
    world_db_path = None
    if prebake:
        try:
            world_db_path = pkg_mod.prebake_world(ENGINE_API_URL)
        except Exception as e:
            return {"error": f"Failed to pre-bake world: {e}"}

    project_dir = Path(project_dir_path).resolve() if project_dir_path else None

    result = pkg_mod.package_game(
        name=name,
        output_dir=output_dir,
        definition=definition,
        config=config,
        include_all_resources=all_resources,
        include_mcp=include_mcp,
        window_title=title,
        project_dir=project_dir,
        world_db_path=world_db_path,
    )

    return result


# ============================================================================
# Entry Point
# ============================================================================

async def main():
    logger.info(f"Starting Phyxel MCP server (engine API: {ENGINE_API_URL})")
    async with stdio_server() as (read_stream, write_stream):
        init_options = server.create_initialization_options()
        await server.run(read_stream, write_stream, init_options)


if __name__ == "__main__":
    import asyncio
    asyncio.run(main())
